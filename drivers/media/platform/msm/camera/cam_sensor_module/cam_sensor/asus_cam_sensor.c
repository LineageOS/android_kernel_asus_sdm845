#include "asus_cam_sensor.h"
#include <linux/proc_fs.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/delay.h>
#include "cam_sensor_core.h"
#include "cam_eeprom_dev.h"//EEPROM
#include "cam_actuator_dev.h"//Actuator
#include "asus_ois.h"
#include <linux/thermal.h>
#include "asus_cam_sensor_util.h"

#undef  pr_fmt
#define pr_fmt(fmt) "SENSOR-ATD %s(): " fmt, __func__

#define MAX_CAMERA_ID CAMERA_2

#include "asus_cam_sensor_def.h"

static struct cam_sensor_ctrl_t *g_sensor_ctrl[MAX_CAMERA_ID+1]={0};
static uint16_t g_sensor_id[MAX_CAMERA_ID+1]={0};
static uint8_t  g_sensor_init_state[MAX_CAMERA_ID+1]={0};
static uint32_t g_module_changed[MAX_CAMERA_ID + 1];

#ifdef PROJECT_DRACO
//check ov8856 type
static uint8_t g_ov8856_R1B[2] = {0,0};//front,rear2
#endif

#include "asus_cam_sensor_utility.c"
#include "asus_cam_sensor_spec.c"

#ifdef PROJECT_DRACO

int is_OV8856_R1B(uint32_t id)
{
	if(id == CAMERA_1)
	{
		return g_ov8856_R1B[0];
	}
	else if(id == CAMERA_2)
	{
		return g_ov8856_R1B[1];
	}
	else
	{
		pr_err("invalid camera id %d for ov8856\n",id);
		return -1;
	}
}


int verify_imx363_id(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc;
	uint32_t revision, chip_id;

	do
	{
		rc = cam_sensor_read_byte(s_ctrl,0x18,&revision);
		if(rc < 0)
		{
			pr_err("Failed to get sensor revision! rc %d\n",rc);
			break;
		}
		rc = cam_sensor_read_word(s_ctrl,0x16,&chip_id);
		if(rc < 0)
		{
			pr_err("Failed to get chip id! rc %d\n",rc);
			break;
		}

		rc = 0;
		if(revision >= 0x11)
		{
			if(chip_id != 0x363)
			{
				pr_err("chip id 0x%x is not 0x363 in MP rev 0x%x!\n",chip_id, revision);
				rc = -ENODEV;
			}
		}
		else
		{
			if(chip_id != 0x363 && chip_id != 0x333)
			{
				pr_err("chip id 0x%x in Early rev 0x%x!\n",chip_id, revision);
				rc = -ENODEV;
			}
		}

	}while(0);

	return rc;
}
#endif

static int modules_proc_read(struct seq_file *buf, void *v)
{
	int rc = 0;
	struct cam_sensor_ctrl_t *s_ctrl= (struct cam_sensor_ctrl_t *)buf->private;

	if(!s_ctrl)
	{
		seq_printf(buf, "s_ctrl is NULL\n");
		pr_err("s_ctrl is NULL!\n");
	}
	else
	{
#ifdef PROJECT_DRACO
		if(s_ctrl->sensordata->slave_info.sensor_id == SENSOR_ID_OV8856)
		{
			if(s_ctrl->id == CAMERA_1)
				seq_printf(buf, "%s %s\n",get_sensor_name(SENSOR_ID_OV8856),g_ov8856_R1B[0]?"R1B":"R1A");
			else if(s_ctrl->id == CAMERA_2)
				seq_printf(buf, "%s %s\n",get_sensor_name(SENSOR_ID_OV8856),g_ov8856_R1B[1]?"R1B":"R1A");
		}
		else
#endif
		seq_printf(buf, "%s\n",get_sensor_name(s_ctrl->sensordata->slave_info.sensor_id));
	}
	return rc;
}

static int module_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, modules_proc_read, PDE_DATA(inode));
}
static struct file_operations camera_module_fops = {
	.owner = THIS_MODULE,
	.open = module_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static char *g_module_proc_file_name[MAX_CAMERA_ID + 1]=
{	PROC_REAR_MODULE_1,
	PROC_FRONT_MODULE_1,
	PROC_REAR_MODULE_2,
};

static void module_create(uint16_t camera_id)
{
	char* file_name = g_module_proc_file_name[camera_id];
	create_proc_file(file_name,&camera_module_fops,g_sensor_ctrl[camera_id]);
}

//camera_res +++

static sensor_define_t project_camera[MAX_CAMERA_ID+1] = {
	{SENSOR_ID_IMX363, DIRECTION_REAR},
	{SENSOR_ID_OV8856, DIRECTION_FRONT},
	{SENSOR_ID_OV8856, DIRECTION_REAR},
};
static char g_sensors_res_string[64];

static void fill_sensors_res_string(void)
{
	//asume each side has MAX_CAMERA_ID+1 cameras
	uint8_t front_cam_res[MAX_CAMERA_ID+1];
	uint8_t rear_cam_res[MAX_CAMERA_ID+1];

	int i;
	int front_count=0;
	int rear_count=0;

	char *p = g_sensors_res_string;
	int offset = 0;

	memset(front_cam_res,0,sizeof(front_cam_res));
	memset(rear_cam_res,0,sizeof(rear_cam_res));

	for(i=0;i<MAX_CAMERA_ID+1;i++)
	{
		if(project_camera[i].direction == DIRECTION_FRONT)
		{
			front_cam_res[front_count++] = get_sensor_resolution(project_camera[i].sensor_id);
		}
		else if(project_camera[i].direction == DIRECTION_REAR)
		{
			rear_cam_res[rear_count++] = get_sensor_resolution(project_camera[i].sensor_id);
		}
	}

	sort_cam_res(front_cam_res,front_count);
	sort_cam_res(rear_cam_res,rear_count);

	//format: front0+front1+...+frontN-rear0+rear1+...+rearN
	for(i=0;i<front_count;i++)
	{
		if(i==0)
			offset+=sprintf(p+offset, "%dM",front_cam_res[i]);
		else
			offset+=sprintf(p+offset, "+%dM",front_cam_res[i]);
	}

	for(i=0;i<rear_count;i++)
	{
		if(i==0)
			offset+=sprintf(p+offset, "-%dM",rear_cam_res[i]);
		else
			offset+=sprintf(p+offset, "+%dM",rear_cam_res[i]);
	}

	offset+=sprintf(p+offset,"\n");
}

static int sensors_res_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%s",g_sensors_res_string);
	return 0;
}

static int sensors_res_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, sensors_res_proc_read, NULL);
}

static struct file_operations sensors_res_fops = {
	.owner = THIS_MODULE,
	.open = sensors_res_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void sensors_res_create(void)
{
	fill_sensors_res_string();
	create_proc_file(PROC_SENSORS_RES,&sensors_res_fops,NULL);
}

//camera_res ---


static int temperature_proc_read(struct seq_file *buf, void *v)
{
	int32_t rc = 0;
	int32_t temperature = 0;
	struct cam_sensor_ctrl_t *s_ctrl= (struct cam_sensor_ctrl_t *)buf->private;
	if(!s_ctrl)
	{
		seq_printf(buf, "s_ctrl is NULL\n");
		pr_err("s_ctrl is NULL!\n");
	}
	else
	{
		rc = read_sensor_temperature(s_ctrl,&temperature);
		if(rc == 0)
			seq_printf(buf, "%hhd\n",temperature);
		else
			seq_printf(buf, "error: %hhd\n",rc);
	}
	return 0;
}

static int temperature_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, temperature_proc_read, PDE_DATA(inode));
}

static struct file_operations temperature_fops = {
	.owner = THIS_MODULE,
	.open = temperature_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static char *g_temperature_proc_file_name[MAX_CAMERA_ID + 1]=
{	PROC_THERMAL_REAR,
	PROC_THERMAL_FRONT,
	PROC_THERMAL_REAR_2,
};

static void temperature_create(uint16_t camera_id)
{
	char* file_name = g_temperature_proc_file_name[camera_id];
	create_proc_file(file_name,&temperature_fops,g_sensor_ctrl[camera_id]);
}

//Thermal OPs +++

static char *g_thermal_type_name[MAX_CAMERA_ID + 1]=
{	THERMAL_TYPE_REAR,
	THERMAL_TYPE_FRONT,
	THERMAL_TYPE_REAR_2,
};
static struct thermal_zone_device * g_thermal_zone_devices[MAX_CAMERA_ID + 1];
static int g_thermal_zone_id_for_camera_sensor[MAX_CAMERA_ID + 1];
static uint8_t g_thermal_zone_init_done[MAX_CAMERA_ID + 1];

static int thermal_read_temperature(struct thermal_zone_device *tzd, int *temperature)
{
	int rc;
	struct cam_sensor_ctrl_t *s_ctrl;

	if(tzd == NULL)
	{
		pr_err("themal_zone_device is NULL!\n");
		return -EINVAL;
	}

	s_ctrl = (struct cam_sensor_ctrl_t *)tzd->devdata;
	if(s_ctrl == NULL)
	{
		pr_err("s_ctrl is NULL!\n");
		return -EINVAL;
	}
	if(g_thermal_zone_init_done[s_ctrl->id])
		rc = read_sensor_temperature(s_ctrl,temperature);
	else
	{
		pr_info("register on going, do dummy temperature read\n");
		rc = 0;
		*temperature = 0;
	}

	return rc;
}

static struct thermal_zone_device_ops tzd_ops ={
	.get_temp = thermal_read_temperature,
};

static void thermal_create(uint16_t camera_id)
{
	char* type_name = g_thermal_type_name[camera_id];
	struct thermal_zone_device *tzd;
	tzd = thermal_zone_device_register(type_name,0,0,g_sensor_ctrl[camera_id],&tzd_ops,NULL,0,0);
	if(IS_ERR(tzd))
	{
		pr_err("create thermal zone for camera %d failed!\n",camera_id);
	}
	else
	{
		pr_info("create(%s) type(%s) for camera %d done!\n",tzd->device.kobj.name,tzd->type,camera_id);
		g_thermal_zone_devices[camera_id] = tzd;
		g_thermal_zone_id_for_camera_sensor[camera_id] = tzd->id;
		g_thermal_zone_init_done[camera_id] = 1;
	}
}
//Thermal OPs ---

//OTP +++
typedef struct
{
	uint32_t num_map;
	uint32_t num_data;
	uint8_t *mapdata;
	uint8_t  id;
}eeprom_info_t;

static eeprom_info_t g_eeprom_info[MAX_CAMERA_ID + 1];
static eeprom_info_t g_dit_eeprom_info[MAX_CAMERA_ID + 1];
static uint8_t g_otp_data_banks[MAX_CAMERA_ID + 1][OTP_DATA_LEN_BYTE*3];
static uint8_t g_otp_data_override_done[MAX_CAMERA_ID + 1];

static int otp_proc_read(struct seq_file *buf, void *v)
{
	int i;
	int bank;

	uint8_t* p_otp_data;
	uint32_t camera_id = 0;

	struct cam_sensor_ctrl_t *s_ctrl = (struct cam_sensor_ctrl_t *)buf->private;

	if(s_ctrl == NULL)
	{
		seq_printf(buf, "s_ctrl is NULL!\n");
		return 0;
	}

	camera_id = s_ctrl->id;

	p_otp_data = &g_otp_data_banks[camera_id][0];

	//Camera 0 & 2 share EEPROM 0
	if((camera_id == 0 || camera_id == 2) && !g_otp_data_override_done[camera_id])
	{
		if(g_eeprom_info[0].mapdata == NULL || g_eeprom_info[0].num_data <= 0)
		{
			pr_err("eeprom_data of camera %d is NULL! not override otp\n",camera_id);
		}
		else
		{
			override_otp_from_eeprom(p_otp_data,g_eeprom_info[0].mapdata,camera_id);
			g_otp_data_override_done[camera_id] = 1;
		}
	}

	for(bank=0;bank<3;bank++)
	{
		p_otp_data = &g_otp_data_banks[camera_id][OTP_DATA_LEN_BYTE*bank];
		for( i = 0; i < OTP_DATA_LEN_WORD; i++)//show 32 bytes, although one bank have 64 bytes
		{
			seq_printf(buf,"0x%02X ",p_otp_data[i]);
			if( (i&7) == 7)
				seq_printf(buf,"\n");
		}
		if(bank<2)
			seq_printf(buf ,"\n");
	}

	return 0;
}

static int otp_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, otp_proc_read, PDE_DATA(inode));
}

static struct file_operations otp_fops = {
	.owner = THIS_MODULE,
	.open = otp_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static char *g_otp_proc_file_name[MAX_CAMERA_ID + 1]=
{	PROC_OTP_REAR_1,
	PROC_OTP_FRONT,
	PROC_OTP_REAR_2,
};

static void otp_dump_create(uint16_t camera_id)
{
	char* file_name = g_otp_proc_file_name[camera_id];
	if(read_sensor_otp(g_sensor_ctrl[camera_id],&g_otp_data_banks[camera_id][0]) < 0)
		pr_err("read otp for camera sensor %d failed!\n",camera_id);
	create_proc_file(file_name,&otp_fops,g_sensor_ctrl[camera_id]);
}
//OTP ---
static struct kobject* kobj_sensor_test = NULL;
static ssize_t resolutions_read(struct device *dev, struct device_attribute *attr, char *buf);
static struct kobject* kobj_resolution = NULL;
static struct device_attribute dev_attr_camera_resolutions[MAX_CAMERA_ID + 1] ={
	__ATTR(SYSFS_ATTR_REAR_1, S_IRUGO | S_IWUSR | S_IWGRP, resolutions_read, NULL),
	__ATTR(SYSFS_ATTR_FRONT_1, S_IRUGO | S_IWUSR | S_IWGRP, resolutions_read, NULL),
	__ATTR(SYSFS_ATTR_REAR_2, S_IRUGO | S_IWUSR | S_IWGRP, resolutions_read, NULL),
};

static ssize_t resolutions_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int i = 0;

	if(!attr)
	{
		ret = sprintf(buf, "attr is NULL!\n");
		pr_err("attr is NULL!\n");
	}
	else
	{
		for(i=0;i<=MAX_CAMERA_ID;i++)
		{
			if(!strcmp(attr->attr.name,dev_attr_camera_resolutions[i].attr.name))
				break;
		}

		if(i > MAX_CAMERA_ID)
		{
			ret = sprintf(buf, "can't find resolution\n");
			pr_err("find resolution for %s failed\n",attr->attr.name);
		}
		else
		{
			ret = sprintf(buf, "%dM\n",get_sensor_resolution(g_sensor_id[i]));
		}
	}
	return ret;
}

static void resolution_create(uint16_t camera_id)
{
	if(kobj_sensor_test == NULL)
		create_sysfs_root_dir(SYSFS_ROOT_DIR, &kobj_sensor_test);
	if(kobj_resolution == NULL)
		create_sysfs_dir(SYSFS_RESOLUTION_DIR,&kobj_resolution,kobj_sensor_test);
	if(kobj_resolution)
		create_sysfs_file(kobj_resolution,&dev_attr_camera_resolutions[camera_id]);
}

static ssize_t status_read(struct device *dev, struct device_attribute *attr, char *buf);
static struct kobject* kobj_status = NULL;
static struct device_attribute dev_attr_camera_status[MAX_CAMERA_ID + 1] =
{
	__ATTR(SYSFS_ATTR_REAR_1, S_IRUGO | S_IWUSR | S_IWGRP, status_read, NULL),
	__ATTR(SYSFS_ATTR_FRONT_1, S_IRUGO | S_IWUSR | S_IWGRP, status_read, NULL),
	__ATTR(SYSFS_ATTR_REAR_2, S_IRUGO | S_IWUSR | S_IWGRP, status_read, NULL),
};

static ssize_t status_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int i = 0;

	if(!attr)
	{
		ret = sprintf(buf, "attr is NULL!\n");
		pr_err("attr is NULL!\n");
	}
	else
	{
		for(i=0;i<=MAX_CAMERA_ID;i++)
		{
			if(!strcmp(attr->attr.name,dev_attr_camera_resolutions[i].attr.name))
				break;
		}

		if(i > MAX_CAMERA_ID)
		{
			ret = sprintf(buf, "can't find status\n");
			pr_err("find status for %s failed\n",attr->attr.name);
		}
		else
		{
			if(cam_sensor_test_i2c(g_sensor_ctrl[i]) < 0)
				return sprintf(buf, "%s\n%s\n", "ERROR: i2c r/w test fail","Driver version:");
			else
				return sprintf(buf, "%s\n%s\n%s 0x%x\n", "ACK:i2c r/w test ok","Driver version:","sensor_id:",g_sensor_id[i]);
		}
	}
	return ret;
}

static void status_create(uint16_t camera_id)
{
	if(kobj_sensor_test == NULL)
		create_sysfs_root_dir(SYSFS_ROOT_DIR, &kobj_sensor_test);
	if(kobj_status == NULL)
		create_sysfs_dir(SYSFS_STATUS_DIR,&kobj_status,kobj_sensor_test);
	if(kobj_status)
		create_sysfs_file(kobj_status,&dev_attr_camera_status[camera_id]);
}

static uint8_t g_camera_id;
static uint32_t g_camera_reg_addr;
static uint32_t g_camera_reg_val;
static enum camera_sensor_i2c_type g_camera_data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
static uint8_t g_camera_sensor_operation = 0;
static int sensor_i2c_debug_read(struct seq_file *buf, void *v)
{
	uint32_t reg_val = 0xEEEE;
	int rc;

	if(g_sensor_ctrl[g_camera_id] == NULL)
	{
		pr_err("Sensor ID %d Not Exist!\n",g_camera_id);
		seq_printf(buf,"Camera ID %d Not Exist!\n",g_camera_id);
		return 0;
	}
	mutex_lock(&(g_sensor_ctrl[g_camera_id]->cam_sensor_mutex));
	seq_printf(buf,"=========================\n\
Camera %d, PowerState %d\n\
Camera %d, PowerState %d\n\
Camera %d, PowerState %d\n\
=========================\n",
	CAMERA_0,g_sensor_ctrl[CAMERA_0]?g_sensor_ctrl[CAMERA_0]->power_state:-1,
	CAMERA_1,g_sensor_ctrl[CAMERA_1]?g_sensor_ctrl[CAMERA_1]->power_state:-1,
	CAMERA_2,g_sensor_ctrl[CAMERA_2]?g_sensor_ctrl[CAMERA_2]->power_state:-1
    );
	if(g_sensor_ctrl[g_camera_id]->power_state != 1)
	{
		pr_err("Please power up Camera Sensor %d first!\n",g_camera_id);
		seq_printf(buf,"Camera ID %d POWER DOWN!\n",g_camera_id);
	}
	else
	{
		if(g_camera_data_type == CAMERA_SENSOR_I2C_TYPE_BYTE)
			rc = cam_sensor_read_byte(g_sensor_ctrl[g_camera_id],g_camera_reg_addr,&reg_val);
		else
			rc = cam_sensor_read_word(g_sensor_ctrl[g_camera_id],g_camera_reg_addr,&reg_val);

		if(g_camera_sensor_operation == 1)//write
		{
			if(reg_val == g_camera_reg_val)
			{
				pr_info("read back the same value as write!\n");
			}
			else
			{
				pr_err("write value 0x%x and read back value 0x%x not same!\n",
						g_camera_reg_val,reg_val
				);
			}
		}
		seq_printf(buf,"Camera %d reg 0x%04x val 0x%x\n",g_camera_id,g_camera_reg_addr,reg_val);
	}
	mutex_unlock(&(g_sensor_ctrl[g_camera_id]->cam_sensor_mutex));
	return 0;
}

static int sensor_i2c_debug_open(struct inode *inode, struct  file *file)
{
	return single_open(file, sensor_i2c_debug_read, NULL);
}

static ssize_t sensor_i2c_debug_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	int n;
	char messages[32]="";
	uint32_t val[4];
	int rc;

	ret_len = len;
	if (len > 32) {
		len = 32;
	}
	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	n = sscanf(messages,"%x %x %x %x",&val[0],&val[1],&val[2],&val[3]);

	if(n < 2 || n > 4 )
	{
		rc = -1;
		pr_err("Invalid Argument count %d!\n",n);
		goto RETURN;
	}
	else if( n == 2)
	{
		//camera id, reg addr
		g_camera_id = val[0];
		g_camera_reg_addr = val[1];

		g_camera_data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		g_camera_sensor_operation = 0;
	}
	else if( n == 3)
	{
		//camera id, reg addr, data type
		g_camera_id = val[0];
		g_camera_reg_addr = val[1];
		if(val[2] == 1)
			g_camera_data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		else if(val[2] == 2)
			g_camera_data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		else
			g_camera_data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		g_camera_sensor_operation = 0;
	}
	else if( n == 4)
	{
		//camera id, reg addr, data type, reg val
		g_camera_id = val[0];
		g_camera_reg_addr = val[1];
		if(val[2] == 1)
			g_camera_data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		else if(val[2] == 2)
			g_camera_data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		else
			g_camera_data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		g_camera_reg_val = val[3];
		g_camera_sensor_operation = 1;
	}

	if(g_camera_id > MAX_CAMERA_ID)
	{
		pr_err("Invalid Sensor ID %d!\n",g_camera_id);
		g_camera_id = CAMERA_0;//reset to default ID
		goto RETURN;
	}

	if(g_sensor_ctrl[g_camera_id] == NULL)
	{
		pr_err("Sensor ID %d Not Exist!\n",g_camera_id);
		g_camera_id = CAMERA_0;//reset to default ID
		goto RETURN;
	}

	mutex_lock(&(g_sensor_ctrl[g_camera_id]->cam_sensor_mutex));
	if(g_sensor_ctrl[g_camera_id]->power_state != 1)
	{
		pr_err("Please power up Camera Sensor %d first!\n",g_camera_id);
		goto RETURN;
	}

	pr_info("gona %s Camera ID %d reg 0x%04x, data type %s\n",
			g_camera_sensor_operation ? "WRITE":"READ",
			g_camera_id,
			g_camera_reg_addr,
			g_camera_data_type == CAMERA_SENSOR_I2C_TYPE_BYTE?"BYTE":"WORD"
			);


	if(g_camera_sensor_operation == 1)
	{
		if(g_camera_data_type == CAMERA_SENSOR_I2C_TYPE_BYTE)
			rc = cam_sensor_write_byte(g_sensor_ctrl[g_camera_id],g_camera_reg_addr,g_camera_reg_val);
		else
			rc = cam_sensor_write_word(g_sensor_ctrl[g_camera_id],g_camera_reg_addr,g_camera_reg_val);

		if(rc < 0)
		{
			pr_err("write 0x%x to camera id %d addr 0x%04x FAIL\n",g_camera_reg_val,g_camera_id,g_camera_reg_addr);
		}
		else
		{
			pr_info("write 0x%x to camera id %d addr 0x%04x OK\n",g_camera_reg_val,g_camera_id,g_camera_reg_addr);
		}
	}
RETURN:
	mutex_unlock(&(g_sensor_ctrl[g_camera_id]->cam_sensor_mutex));
	return ret_len;
}
static const struct file_operations sensor_i2c_debug_fops = {
	.owner = THIS_MODULE,
	.open = sensor_i2c_debug_open,
	.read = seq_read,
	.write = sensor_i2c_debug_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static void sensor_i2c_create(void)
{
	create_proc_file(PROC_SENSOR_I2C_RW,&sensor_i2c_debug_fops,NULL);
}

static uint32_t g_eeprom_reg_addr = 0x0000;
static uint32_t g_eeprom_id = 0;//camera id
static struct cam_eeprom_ctrl_t* g_eeprom_ctrl[MAX_CAMERA_ID+1] = {0};

static int eeprom_read(struct cam_eeprom_ctrl_t *e_ctrl, uint32_t reg_addr, uint32_t * reg_data)
{
	int rc;

	rc = camera_io_dev_read(&(e_ctrl->io_master_info),
								reg_addr,
								reg_data,
								CAMERA_SENSOR_I2C_TYPE_WORD,//addr_type
								CAMERA_SENSOR_I2C_TYPE_BYTE);//data_type
	if(rc < 0)
	{
		pr_err("EEPROM read reg 0x%x failed! rc = %d\n",reg_addr,rc);
	}
	else
	{
		pr_info("EEPROM read reg 0x%x get val 0x%x\n",reg_addr,*reg_data);
	}
	return rc;
}

static int eeprom_i2c_debug_read(struct seq_file *buf, void *v)
{
	uint32_t reg_val = 0xEEEE;
	int rc;

	struct cam_eeprom_ctrl_t * e_ctrl = g_eeprom_ctrl[g_eeprom_id];

	if(e_ctrl == NULL)
	{
		seq_printf(buf,"e_ctrl for eeprom %d is NULL!\n",g_eeprom_id);
		return 0;
	}
	mutex_lock(&(g_sensor_ctrl[g_eeprom_id]->cam_sensor_mutex));
	if(g_sensor_ctrl[g_eeprom_id]->power_state != 1)
	{
		pr_err("Please Power UP Camera Sensor %d for eeprom %d!\n",g_eeprom_id,g_eeprom_id);
		seq_printf(buf,"EEPROM %d POWER DOWN!\n",g_eeprom_id);
	}
	else
	{
		rc = eeprom_read(e_ctrl,g_eeprom_reg_addr,&reg_val);
		if(rc < 0)
		{
			seq_printf(buf,"read reg 0x%04x failed, rc = %d\n",g_eeprom_reg_addr,rc);
		}
		else
			seq_printf(buf,"0x%x\n",reg_val);
	}
	mutex_unlock(&(g_sensor_ctrl[g_eeprom_id]->cam_sensor_mutex));
	return 0;
}

static int eeprom_i2c_debug_open(struct inode *inode, struct  file *file)
{
	return single_open(file, eeprom_i2c_debug_read, NULL);
}

static ssize_t eeprom_i2c_debug_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";

	ret_len = len;

	if (len > 16) {
		len = 16;
	}

	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%x",&g_eeprom_reg_addr);

	pr_info("Gona read EEPROM reg addr 0x%04x\n",g_eeprom_reg_addr);

	return ret_len;
}

static const struct file_operations eeprom_i2c_debug_fops = {
	.owner = THIS_MODULE,
	.open = eeprom_i2c_debug_open,
	.read = seq_read,
	.write = eeprom_i2c_debug_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static void eeprom_i2c_create(void)
{
	create_proc_file(PROC_EEPROM_I2C_R,&eeprom_i2c_debug_fops,NULL);
}

#define DUAL_CALI_START 0x422
#define DUAL_CALI_END   0xC21
#define DUAL_CALI_SIZE (DUAL_CALI_END-DUAL_CALI_START+1)

#define PDAF_CALI_START 0x21
#define PDAF_CALI_END 0x409
#define PDAF_CALI_SIZE (PDAF_CALI_END-PDAF_CALI_START+1)

int32_t get_file_size(const char *filename, uint64_t* size);

static int put_dual_cali_bin_buffer(struct seq_file *buf)
{
	int ret = 0;
	uint64_t i, size;
	if (asus_util_fs_get_file_size(DUAL_CALI_BIN, &size) == 0 && size == DUAL_CALI_SIZE) {
		uint8_t* pBuffer;
		pBuffer = kzalloc(sizeof(uint8_t) * DUAL_CALI_SIZE, GFP_KERNEL);
		if (pBuffer) {
			ret = asus_util_fs_read_file_into_buffer(DUAL_CALI_BIN, pBuffer, size);
			if (ret == DUAL_CALI_SIZE) {
				for (i = 0; i < size; i++)
				{
					seq_putc(buf, pBuffer[i]);
				}
			}
			kfree(pBuffer);
		}
	}
	return ret;
}

static int dual_cali_read(struct seq_file *buf, void *v)
{
	int i;
	eeprom_info_t * eeprom_info = (eeprom_info_t *)buf->private;

	if (put_dual_cali_bin_buffer(buf) == DUAL_CALI_SIZE) return 0;

	if(eeprom_info == NULL || eeprom_info->mapdata == NULL)
	{
		seq_printf(buf,"eeprom info invalid!\n");
		return 0;
	}

	if(eeprom_info->num_data < DUAL_CALI_END+1)
	{
		seq_printf(buf,"eeprom data not cover all dual cali data!\n");
		return 0;
	}

	for(i=DUAL_CALI_START;i<=DUAL_CALI_END;i++)
	{
		seq_putc(buf, eeprom_info->mapdata[i]);
	}
	return 0;
}

static int dual_cali_open(struct inode *inode, struct  file *file)
{
	return single_open(file, dual_cali_read, PDE_DATA(inode));
}

static const struct file_operations dual_cali_fops = {
	.owner = THIS_MODULE,
	.open = dual_cali_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void dual_cali_dump_create(void)
{
	if(g_eeprom_info[CAMERA_0].mapdata && g_eeprom_info[CAMERA_0].num_data > DUAL_CALI_END+1)
	{
		create_proc_file(PROC_ARCSOFT_CALI,&dual_cali_fops,&g_eeprom_info[CAMERA_0]);
	}
	else
		pr_err("eeprom data not correct! can not create dual cali data dump node\n");
}
//dual cali data ---

static char *dit_eeprom_file_name[MAX_CAMERA_ID + 1]=
{	FACTORYDIR DIT_DUT_REAR,
	FACTORYDIR DIT_DUT_FRONT,
	FACTORYDIR DIT_DUT_REAR2,
};

static char *dit_eeprom_golden_file_name[MAX_CAMERA_ID + 1]=
{	GOLDENDIR DIT_DUT_REAR,
	GOLDENDIR DIT_DUT_FRONT,
	GOLDENDIR DIT_DUT_REAR2,
};

#define AFC_SIZE 6
uint8_t AFC_golden_data[AFC_SIZE] =
{
	0x04,
	0x07,
	0x04,
	0x87,
	0x05,
	0xD4
};

uint8_t is_AFC_data_valid(void)
{
	uint8_t is_AFC_valid = 0;
	int i;
	if(g_eeprom_info[0].mapdata)
	{
		for(i=0;i<AFC_SIZE;i++)
		{
			if(g_eeprom_info[0].mapdata[i] != 0xFF)
			{
				is_AFC_valid = 1;
				break;
			}
		}
	}
	return is_AFC_valid;
}


static void set_actual_afc_data(void)
{
	uint32_t dac_10cm, dac_50cm;

	if(is_AFC_data_valid())
	{
		dac_10cm = g_eeprom_info[0].mapdata[4]<<8|g_eeprom_info[0].mapdata[5];
		dac_50cm = g_eeprom_info[0].mapdata[2]<<8|g_eeprom_info[0].mapdata[3];
	}
	else
	{
		dac_10cm = AFC_golden_data[4]<<8|AFC_golden_data[5];
		dac_50cm = AFC_golden_data[2]<<8|AFC_golden_data[3];
	}

	set_ois_afc_data_from_eeprom(dac_10cm,dac_50cm);
}

static void set_dit_afc_data(void)
{
	#define DAC_10CM_DIT_K "/vendor/factory/vcm_macro.txt"
	#define DAC_50CM_DIT_K "/vendor/factory/vcm_50cm.txt"

	uint32_t dac_10cm;
	uint32_t dac_50cm;

	if(asus_util_fs_read_text_uint32(DAC_10CM_DIT_K,&dac_10cm) == 0)
	{
		if(asus_util_fs_read_text_uint32(DAC_50CM_DIT_K,&dac_50cm) == 0)
		{
			set_ois_dit_afc_data_from_eeprom(dac_10cm, dac_50cm);
			return;
		}
	}
	set_ois_dit_afc_data_from_eeprom(0, 0);
}

static void copy_to_dit_eeprom(struct cam_eeprom_ctrl_t *e_ctrl,uint32_t camera_id)
{
	if(g_dit_eeprom_info[camera_id].mapdata == NULL)
	{
		g_dit_eeprom_info[camera_id].mapdata = kzalloc(e_ctrl->cal_data.num_data, GFP_KERNEL);
		if(!g_dit_eeprom_info[camera_id].mapdata)
		{
			pr_err("alloc memory for DIT eeprom mapdata failed!\n");
			return;
		}
	}
	memcpy(g_dit_eeprom_info[camera_id].mapdata, e_ctrl->cal_data.mapdata, e_ctrl->cal_data.num_data);
}

static int cover_dit_cal_data(struct cam_eeprom_ctrl_t *e_ctrl,uint32_t camera_id)
{
	int num;
	char* load_file_name;

	if(e_ctrl->cal_data.num_data < EEPROM_SIZE)
	{
		pr_err("EEPROM %d map size %d is less than %d!\n",camera_id, e_ctrl->cal_data.num_data, EEPROM_SIZE);
		return -1;
	}

	if(g_module_changed[camera_id])
	{
		load_file_name = dit_eeprom_golden_file_name[camera_id];
	}
	else
	{
		load_file_name = dit_eeprom_file_name[camera_id];
	}

	num = asus_util_fs_read_file_into_buffer(load_file_name,e_ctrl->cal_data.mapdata,EEPROM_SIZE);
	if(num>0)
	{
		pr_info("[DIT_EEPROM] read %d bytes from %s",num,load_file_name);
	}else if(!g_module_changed[camera_id] && (num = asus_util_fs_read_file_into_buffer(dit_eeprom_golden_file_name[camera_id],e_ctrl->cal_data.mapdata,EEPROM_SIZE)) >0)
	{
		pr_info("[DIT_EEPROM] read %d bytes from %s",num,dit_eeprom_golden_file_name[camera_id]);
	}else{
		pr_err("[DIT_EEPROM] read %s failed,rc %d",dit_eeprom_file_name[camera_id],num);
	}
	if(camera_id == CAMERA_0)
	{
		if(num <= 0)
		{/*If there is no dit bin ,set user space eeprom data to 0*/
			memset(e_ctrl->cal_data.mapdata,0,e_ctrl->cal_data.num_data);
		}
		//dit_eeprom:   AF:0x1150~0x1153    PDAF:0x1500~18E8
		if(e_ctrl->cal_data.mapdata[0x1158] == 0 && e_ctrl->cal_data.mapdata[0x1159] == 0)
		{/*If there is no dit bin or af check data=0,copy af data from eeprom*/
			if(!is_AFC_data_valid())
			{
				#ifdef ASUS_FACTORY_BUILD
					pr_err("[DIT_EEPROM] AFC in EEPROM invalid! FACTORY build, not override golden data\n");
				#else
					pr_err("[DIT_EEPROM] AFC in EEPROM invalid! Override with golden data for userspace...\n");
					memcpy(e_ctrl->cal_data.mapdata+0x1150,AFC_golden_data+1,1);
					memcpy(e_ctrl->cal_data.mapdata+0x1151,AFC_golden_data,1);
					memcpy(e_ctrl->cal_data.mapdata+0x1152,AFC_golden_data+5,1);
					memcpy(e_ctrl->cal_data.mapdata+0x1153,AFC_golden_data+4,1);
				#endif
			}
			else
			{
				pr_info("[DIT_EEPROM] camera %d, copy AF data from eeprom",camera_id);
				memcpy(e_ctrl->cal_data.mapdata+0x1150,g_eeprom_info[0].mapdata+1,1);
				memcpy(e_ctrl->cal_data.mapdata+0x1151,g_eeprom_info[0].mapdata,1);
				memcpy(e_ctrl->cal_data.mapdata+0x1152,g_eeprom_info[0].mapdata+5,1);
				memcpy(e_ctrl->cal_data.mapdata+0x1153,g_eeprom_info[0].mapdata+4,1);
			}
		}
		/*Copy pdaf cali data from eeprom*/
		memcpy(e_ctrl->cal_data.mapdata+0x1500, g_eeprom_info[0].mapdata+PDAF_CALI_START, PDAF_CALI_SIZE);
	}
	else if(camera_id == CAMERA_2
			&& g_dit_eeprom_info[0].mapdata != NULL) // check g_dit_eeprom_info[0] to fix crashdump when no camera 0
	{
		memcpy(e_ctrl->cal_data.mapdata+0x115A,g_dit_eeprom_info[0].mapdata+0x115A,16);
	}
	copy_to_dit_eeprom(e_ctrl,camera_id);
	return 0;
}

static int save_actual_eeprom_info(struct cam_eeprom_ctrl_t * e_ctrl, uint32_t camera_id)
{
	if(camera_id == CAMERA_0)
	{
		g_eeprom_info[camera_id].id = camera_id;
		g_eeprom_info[camera_id].num_map = e_ctrl->cal_data.num_map;
		g_eeprom_info[camera_id].num_data = e_ctrl->cal_data.num_data;
		if(g_eeprom_info[camera_id].mapdata == NULL)
		{
			g_eeprom_info[camera_id].mapdata = kzalloc(e_ctrl->cal_data.num_data, GFP_KERNEL);
			if(!g_eeprom_info[camera_id].mapdata)
			{
				pr_err("alloc memory for eeprom mapdata failed!\n");
				return -1;
			}
		}
		pr_info("actual mapdata size of EEPROM 0 is %d\n",e_ctrl->cal_data.num_data);
		memcpy(g_eeprom_info[camera_id].mapdata, e_ctrl->cal_data.mapdata, e_ctrl->cal_data.num_data);
	}
	return 0;
}

static int eeprom_proc_read(struct seq_file *buf, void *v)
{
	int i;
	uint8_t *index = (uint8_t *)buf->private;

	pr_info("eeprom info, id %d, num_map %d, num_data %d\n",
			g_eeprom_info[*index].id, g_eeprom_info[*index].num_map, g_eeprom_info[*index].num_data);
	if(!g_eeprom_info[*index].mapdata)
	{
		seq_printf(buf,"eeprom data is NULL\n");
	}
	else
	{
		for( i = 0; i < g_eeprom_info[*index].num_data; i++)
		{
			seq_printf(buf, "0x%04X 0x%02X\n",i,g_eeprom_info[*index].mapdata[i]);
		}
	}
	return 0;
}
static int eeprom_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, eeprom_proc_read, PDE_DATA(inode));
}

static struct file_operations eeprom_proc_fops = {
	.owner = THIS_MODULE,
	.open = eeprom_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int dit_eeprom_proc_read(struct seq_file *buf, void *v)
{
	int i;
	uint8_t *index = (uint8_t *)buf->private;

	pr_info("[DIT_EEPROM], id %d, num_map %d, num_data %d\n",
			g_dit_eeprom_info[*index].id, g_dit_eeprom_info[*index].num_map, g_dit_eeprom_info[*index].num_data);
	if(!g_dit_eeprom_info[*index].mapdata)
	{
		seq_printf(buf,"eeprom data is NULL\n");
	}
	else
	{
		for( i = 0; i < g_dit_eeprom_info[*index].num_data; i++)
		{
			seq_printf(buf, "0x%04X 0x%02X\n",i,g_dit_eeprom_info[*index].mapdata[i]);
		}
	}
	return 0;
}
static int dit_eeprom_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, dit_eeprom_proc_read, PDE_DATA(inode));
}

static struct file_operations dit_eeprom_proc_fops = {
	.owner = THIS_MODULE,
	.open = dit_eeprom_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static char *g_dit_eeprom_proc_file_name[MAX_CAMERA_ID + 1]=
{	PROC_DIT_EEPROM_REAR,
	PROC_DIT_EEPROM_FRONT,
	PROC_DIT_EEPROM_REAR2,
};

static char *g_eeprom_proc_file_name[MAX_CAMERA_ID + 1]=
{	PROC_EEPROM_REAR,
	PROC_EEPROM_FRONT,
	PROC_EEPROM_REAR2,
};


void eeprom_dump_create(struct cam_eeprom_ctrl_t * e_ctrl)
{
	char* file_name;
	char* dit_file_name;
	uint32_t camera_id;
	uint32_t module_sn;

	if(e_ctrl == NULL)
	{
		pr_err("e_ctrl is NULL!\n");
		return;
	}

	if(get_camera_id_for_submodule(SUB_MODULE_EEPROM,e_ctrl->soc_info.index,&camera_id) != 0)
	{
		pr_err("can not find related camera id for eeprom index %d, not create debug node",e_ctrl->soc_info.index);
		return;
	}

	if(g_eeprom_ctrl[camera_id] == NULL)//first time
	{
		save_actual_eeprom_info(e_ctrl,camera_id);
		g_eeprom_ctrl[camera_id] = e_ctrl;
		file_name = g_eeprom_proc_file_name[camera_id];
		dit_file_name=g_dit_eeprom_proc_file_name[camera_id];
		if(camera_id == CAMERA_0)
		{
			create_proc_file(file_name,&eeprom_proc_fops,&g_eeprom_info[camera_id].id);
			dual_cali_dump_create();
			if(g_eeprom_info[camera_id].num_data >= 18)
			{
				module_sn = (g_eeprom_info[camera_id].mapdata[14]<<24 | g_eeprom_info[camera_id].mapdata[15]<<16 |
							 g_eeprom_info[camera_id].mapdata[16]<<8  | g_eeprom_info[camera_id].mapdata[17] );
				pr_info("CAMERA 0: Module ID 0x%x, Vendor ID 0x%x\n, Module SN 0x%08x",
						g_eeprom_info[camera_id].mapdata[8],g_eeprom_info[camera_id].mapdata[9],module_sn);
				set_ois_module_vendor_from_eeprom(g_eeprom_info[camera_id].mapdata[9]);
				set_ois_module_sn_from_eeprom(module_sn);
				set_actual_afc_data();
			}
			else
			{
				pr_err("eeprom data size %d is less than 18!\n",g_eeprom_info[camera_id].num_data);
			}
		}
		create_proc_file(dit_file_name,&dit_eeprom_proc_fops,&g_dit_eeprom_info[camera_id].id);

		g_dit_eeprom_info[camera_id].id=camera_id;
		g_dit_eeprom_info[camera_id].num_map = e_ctrl->cal_data.num_map;
		g_dit_eeprom_info[camera_id].num_data = e_ctrl->cal_data.num_data;
	}
	else
	{
		pr_info("EEPROM debug file for camera %d already created\n",camera_id);
	}

	cover_dit_cal_data(e_ctrl,camera_id);
	if(camera_id == CAMERA_0)
		set_dit_afc_data();
}

static struct cam_actuator_ctrl_t *g_actuator_ctrl[MAX_CAMERA_ID+1];

#ifdef PROJECT_DRACO
void set_vcm_lens_pos_from_ois_writing(uint32_t dac_value)
{
	if(g_actuator_ctrl[CAMERA_0])
	{
		mutex_lock(&(g_actuator_ctrl[CAMERA_0]->actuator_mutex));
		g_actuator_ctrl[CAMERA_0]->lens_pos = dac_value;
		mutex_unlock(&(g_actuator_ctrl[CAMERA_0]->actuator_mutex));
	}
}

int get_current_lens_position(uint32_t *dac_value)
{
	if(g_actuator_ctrl[CAMERA_0])
	{
		mutex_lock(&(g_actuator_ctrl[CAMERA_0]->actuator_mutex));
		*dac_value = g_actuator_ctrl[CAMERA_0]->lens_pos;
		mutex_unlock(&(g_actuator_ctrl[CAMERA_0]->actuator_mutex));
		return 0;
	}
	else
	{
		pr_err("actuator not init!\n");
		return -1;
	}
}
#endif

static int vcm_proc_read(struct seq_file *buf, void *v)
{
	struct cam_actuator_ctrl_t * a_ctrl = (struct cam_actuator_ctrl_t *)buf->private;

	if(a_ctrl)
	{
		mutex_lock(&(a_ctrl->actuator_mutex));
		seq_printf(buf,"%d\n",a_ctrl->lens_pos);
		mutex_unlock(&(a_ctrl->actuator_mutex));
	}
	else
		seq_printf(buf,"a_ctrl is NULL!\n");
	return 0;
}

static int vcm_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, vcm_proc_read, PDE_DATA(inode));
}

static struct file_operations vcm_proc_fops = {
	.owner = THIS_MODULE,
	.open = vcm_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static char *g_vcm_proc_file_name[MAX_CAMERA_ID + 1]=
{	PROC_VCM_REAR,
	PROC_VCM_FRONT,
	PROC_VCM_REAR2,
};

void vcm_pos_create(struct cam_actuator_ctrl_t * a_ctrl)
{
	char* file_name;
	uint32_t camera_id;
	if(a_ctrl)
	{
		if(get_camera_id_for_submodule(SUB_MODULE_ACTUATOR,a_ctrl->soc_info.index,&camera_id) == 0)
		{
			if(g_actuator_ctrl[camera_id] == NULL)
			{
				g_actuator_ctrl[camera_id] = a_ctrl;
				file_name = g_vcm_proc_file_name[camera_id];
				create_proc_file(file_name,&vcm_proc_fops,a_ctrl);
			}
			else
				pr_err("vcm pos for camera %d already created!\n",camera_id);
		}
		else
			pr_err("can not find related camera id for vcm index %d, not create debug node",a_ctrl->soc_info.index);
	}
	else
		pr_err("a_ctrl is NULL!\n");
}

//Module Changed ++++

static int module_changed_read(struct seq_file *buf, void *v)
{
	uint32_t* module_changed = (uint32_t *)buf->private;
	seq_printf(buf, "%d\n",*module_changed);
	return 0;
}

static int module_changed_open(struct inode *inode, struct file *file)
{
	return single_open(file, module_changed_read, PDE_DATA(inode));
}

static ssize_t module_changed_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret_len;
	char messages[8]="";

	uint32_t val;

	uint32_t *module_changed = PDE_DATA(file_inode(dev));

	ret_len = len;
	if (len > 8) {
		len = 8;
	}
	if (copy_from_user(messages, buf, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages, "%d", &val);

	if(val == 0)
		*module_changed = 0;
	else
		*module_changed = 1;

	return ret_len;
}

static const struct file_operations module_changed_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= module_changed_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= module_changed_write,
};

static char *g_module_changed_file_name[MAX_CAMERA_ID + 1]=
{	PROC_MODULE_CHANGE_REAR,
	PROC_MODULE_CHANGE_FRONT,
	PROC_MODULE_CHANGE_REAR2
};

void module_changed_create(uint16_t camera_id)
{
	char* file_name = g_module_changed_file_name[camera_id];
	create_proc_file(file_name,&module_changed_proc_fops,&g_module_changed[camera_id]);
}

//Module Changed ----

void asus_cam_sensor_init(struct cam_sensor_ctrl_t *s_ctrl)
{
	if(!s_ctrl)
	{
		pr_err("s_ctrl is NULL\n");
		return;
	}

	if(s_ctrl->id > MAX_CAMERA_ID)
	{
		pr_err("s_ctrl id %d invalid!\n",s_ctrl->id);
		return;
	}

	if(!g_sensor_init_state[s_ctrl->id])
	{
		g_sensor_ctrl[s_ctrl->id] = s_ctrl;
		g_sensor_id[s_ctrl->id] = s_ctrl->sensordata->slave_info.sensor_id;
		pr_info("CAMERA ID %d, Sensor id 0x%X E\n",s_ctrl->id,g_sensor_id[s_ctrl->id]);
		module_create(s_ctrl->id);
		resolution_create(s_ctrl->id);
		status_create(s_ctrl->id);
		otp_dump_create(s_ctrl->id);
		temperature_create(s_ctrl->id);
	#ifdef PROJECT_DRACO
		if(s_ctrl->id == CAMERA_0)
	#endif
		thermal_create(s_ctrl->id);
		module_changed_create(s_ctrl->id);

		if(s_ctrl->id == CAMERA_0)
		{
			ois_probe_check();//trigger ois check, camera 0 already powered up, so is OIS
			sensor_i2c_create();//for debug
			eeprom_i2c_create();//for debug
			sensors_res_create();//for shipping image
		}
		pr_info("CAMERA ID %d, Sensor id 0x%X X\n",s_ctrl->id,g_sensor_id[s_ctrl->id]);
		g_sensor_init_state[s_ctrl->id] = 1;
	}
	else
	{
		pr_err("id %d already inited!\n",s_ctrl->id);
	}
}
