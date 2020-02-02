#include <linux/proc_fs.h>
#include "asus_ois.h"
#include "onsemi_interface.h"
#include "onsemi_i2c.h"
#include "asus_cam_sensor_util.h"
#include "cam_eeprom_dev.h"
#include "asus_cam_sensor.h"

#undef  pr_fmt
#define pr_fmt(fmt) "OIS-ATD %s(): " fmt, __func__

#define	PROC_POWER	"driver/ois_power"
#define	PROC_I2C_RW	"driver/ois_i2c_rw"
#define PROC_ON     "driver/ois_on"
#define PROC_STATE "driver/ois_state"
#define	PROC_MODE	"driver/ois_mode"
#define	PROC_CALI	"driver/ois_cali"
#define PROC_RDATA  "driver/ois_rdata"
#define	PROC_ATD_STATUS	"driver/ois_atd_status"
#define	PROC_PROBE_STATUS "driver/ois_status"
#define	PROC_DEVICE	"driver/ois_device"
#define PROC_FW_UPDATE "driver/ois_fw_update"
#define	PROC_MODULE	"driver/ois_module" //ASUS_BSP Lucien +++: Add OIS SEMCO module
#define PROC_VCM_ENABLE "driver/ois_vcm_enable" //for debug
#define PROC_LENS_MOVING  "driver/ois_lens_moving" //notify AF state

#define RDATA_OUTPUT_FILE "/sdcard/gyro.csv"

#define FACTORYDIR "/vendor/factory/"

#define OIS_GYRO_K_OUTPUT_FILE_OLD ""FACTORYDIR"OIS_calibration_old"
#define OIS_GYRO_K_OUTPUT_FILE_NEW ""FACTORYDIR"OIS_calibration"

#define OIS_VCM_BACKUP ""FACTORYDIR"OIS_VCM_VERSION"
#define OIS_MODULE_SN  ""FACTORYDIR"OIS_MODULE_SN"
#define OIS_FW_UPDATE_TIMES ""FACTORYDIR"OIS_FW_UPDATE_TIMES"
#define OIS_FW_UPDATE_TIME_LIMIT 500

#define BATTERY_CAPACITY "/sys/class/power_supply/bms/capacity"
#define BATTERY_THRESHOLD 3

#define UPDATE_FW_AT_PROBE 1

typedef enum {
	CHECK_BAD_IO = -1,
	CHECK_PASS = 0,
	CHECK_BAD_ID,
	CHECK_BAD_FW,
	CHECK_BAD_FUNCTION,
	CHECK_VENDOR_MISMATCH,
	CHECK_VENDOR_INVALID,
	CHECK_CUSTOMER_INVALID,
	CHECK_VCM_INVALID,
	CHECK_FW_VERSION_INVALID,
}chip_check_result_t;

typedef enum{
	UPDATE_FAIL = -1,
	UPDATE_OK = 0,
	NO_NEED = 1,
	BATTERY_LOW,
	NO_MATCH_FW,
	VENDOR_INVALID,
	NO_VCM_BACK,
	EXCEED_LIMIT,
	BAD_IO,
	BAD_FW,
}fw_trigger_result_t;

static struct cam_ois_ctrl_t * ois_ctrl = NULL;

uint8_t g_ois_status = 0;
uint8_t g_ois_power_state = 0;

static uint8_t g_ois_camera_open = 0;
static char g_module_vendor[8] = "UNKNOWN";
static uint32_t g_fw_version = 0;
static uint8_t g_ois_mode = 255;//only after set mode, mode value is valid

static uint8_t g_atd_status = 0;//fail

static uint16_t g_reg_addr = 0xF012;
static uint32_t g_reg_val = 0;
static uint16_t g_slave_id = 0x003E;
static enum camera_sensor_i2c_type g_data_type = CAMERA_SENSOR_I2C_TYPE_DWORD; //dword
static uint8_t g_operation = 0;//read

static char ois_subdev_string[32] = "";

static stReCalib g_calInfo;

static struct mutex g_busy_job_mutex;

static uint32_t g_vendor_id = 0;
static uint32_t g_module_sn = 0;
static struct cam_eeprom_ctrl_t * g_ectrl = NULL;

static uint8_t g_vcm_enabled = 1;

static uint32_t g_dac_10cm = 0;
static uint32_t g_dac_50cm = 0;
static int32_t g_dac_per_cm = 0;
static uint32_t g_distance_prev = 0;
static struct timeval g_ssc_config_time_prev;
static uint32_t g_dac_10cm_dit = 0;
static uint32_t g_dac_50cm_dit = 0;
static uint32_t g_dac_10cm_base = 0;
static uint32_t g_dac_50cm_base = 0;
static uint32_t g_lens_shift_10cm_to_50cm = 0;
static uint32_t g_lens_shift_10cm = 0;
static uint8_t  g_lens_moving = 0;//1 moving, 0 stationary
static uint8_t  g_verbose_log = 0;

static struct timeval g_power_up_time;

static void set_ssc_gain_if_need(void);
#if 0
static void onsemi_read_check(struct cam_ois_ctrl_t *o_ctrl)
{
	char buf[512];

	onsemi_dump_state(o_ctrl,buf,sizeof(buf));
	pr_info("dump state is\n%s\n",buf);

	onsemi_ois_go_on(o_ctrl);
	onsemi_ssc_go_on(o_ctrl);
	onsemi_dump_state(o_ctrl,buf,sizeof(buf));
	pr_info("dump state is\n%s\n",buf);

	onsemi_check_sequence_read(o_ctrl);
}
#endif

static int read_vendor_id_from_eeprom(uint32_t * vendor_id)
{
	int rc;
	uint32_t reg_addr = 0x09;

	if(g_ectrl == NULL)
	{
		pr_err("eeprom ctrl is NULL!\n");
		return -1;
	}
	camera_io_init(&(g_ectrl->io_master_info));
	rc = camera_io_dev_read(&(g_ectrl->io_master_info),
	                          reg_addr,
	                          vendor_id,
	                          CAMERA_SENSOR_I2C_TYPE_WORD,//addr_type
	                          CAMERA_SENSOR_I2C_TYPE_BYTE);//data_type
	camera_io_release(&(g_ectrl->io_master_info));
	if(rc < 0)
	{
		pr_err("EEPROM read reg 0x%x failed! rc = %d\n",reg_addr,rc);
		return -2;
	}
	else
	{
		pr_info("EEPROM read reg 0x%x get val 0x%x\n",reg_addr,*vendor_id);
		if(*vendor_id == 0x01)
			*vendor_id = VENDOR_ID_LITEON;
		else if(*vendor_id == 0x06)
			*vendor_id = VENDOR_ID_PRIMAX;
	}
	return 0;
}

static int read_module_sn_from_eeprom(uint32_t * module_sn)
{
	int rc;
	uint32_t reg_addr = 0x0E;
	uint8_t  sn[4];

	if(g_ectrl == NULL)
	{
		pr_err("eeprom ctrl is NULL!\n");
		return -1;
	}
	camera_io_init(&(g_ectrl->io_master_info));
	rc = camera_io_dev_read_seq(&(g_ectrl->io_master_info),
	                              reg_addr,
	                              sn,
	                              CAMERA_SENSOR_I2C_TYPE_WORD,//addr type
	                              CAMERA_SENSOR_I2C_TYPE_BYTE,//data type
	                              4);
	camera_io_release(&(g_ectrl->io_master_info));
	if(rc < 0)
	{
		pr_err("EEPROM read reg 0x%x failed! rc = %d\n",reg_addr,rc);
		return -2;
	}
	else
	{
		*module_sn = (sn[0]<<24 | sn[1]<<16 | sn[2]<<8 | sn[3]);
		pr_info("EEPROM seq read reg 0x%x get SN 0x%08x\n",reg_addr,*module_sn);
	}
	return 0;
}

static fw_trigger_result_t trigger_fw_update(struct cam_ois_ctrl_t *ctrl, uint8_t update_mode, uint8_t force_update, uint32_t* updated_version)
{
	uint32_t fw_version;
	uint32_t actuator_version;

	uint8_t  module_vendor, vcm_version;
	uint8_t  backup_vcm = 0;
	uint32_t module_sn = 0;
	uint16_t fw_update_times = 0;

	uint8_t  battery_capacity;
	fw_trigger_result_t ret;

	int rc = 0;

	rc = onsemi_read_dword(ctrl,0x8000,&fw_version);
	if(rc < 0)
	{
		pr_err("read fw version failed!\n");
		return BAD_IO;
	}

	rc = onsemi_read_dword(ctrl,0x8008,&actuator_version);
	if(rc < 0)
	{
		pr_err("read actuator version failed!\n");
		return BAD_IO;
	}

	if(!f40_need_update_fw(fw_version,actuator_version,force_update))
	{
		return NO_NEED;//NO NEED UPDATE FW
	}

	if(asus_util_fs_read_text_dword_seq_hex(OIS_MODULE_SN,&module_sn,1) == 0 && g_module_sn == module_sn)
	{
		if(asus_util_fs_read_text_word_seq_hex(OIS_FW_UPDATE_TIMES,&fw_update_times,1) == 0 &&
			fw_update_times >= OIS_FW_UPDATE_TIME_LIMIT)
		{
			if(!force_update && fw_version != 0x0) //not force update nor bad FW
			{
				pr_err("fw has updated %d times, can not update anymore for safety\n",fw_update_times);
				return EXCEED_LIMIT;
			}
		}
	}
	else
	{
		fw_update_times = 0;
		if(fw_version == 0x0)
		{
			pr_err("Bad FW at First, not save it...\n");
			return BAD_FW;
		}
	}

	if(asus_util_fs_read_text_uint8(BATTERY_CAPACITY,&battery_capacity) == 0)
	{
		pr_info("get battery capacity is %d%%\n",battery_capacity);
		if(battery_capacity<=BATTERY_THRESHOLD)
		{
			pr_err("battery is too low, not update FW\n");
			return BATTERY_LOW;
		}
	}

	if(fw_version == 0x0)
	{
		if(g_vendor_id == VENDOR_ID_LITEON || g_vendor_id == VENDOR_ID_PRIMAX)
		{
			pr_info("Saving failed module ... Vendor ID from EEPROM is 0x%x\n",g_vendor_id);
			module_vendor = g_vendor_id;
			//read backup vcm version
			if(asus_util_fs_read_text_byte_seq_hex(OIS_VCM_BACKUP,&backup_vcm,1) == 0)
			{
				pr_info("Got backup vcm %d from factory partition\n",backup_vcm);
				vcm_version = backup_vcm;
			}
			else
			{
				pr_err("Can not get backup vcm! Can not save failed module...\n");
				return NO_VCM_BACK;
			}
		}
		else
		{
			pr_err("Vendor ID 0x%x from EEPROM invalid, Can not save failed module...\n",g_vendor_id);
			return VENDOR_INVALID;
		}
	}
	else
	{
		module_vendor = fw_version >> 24;
		vcm_version = (actuator_version & 0x0000FF00)>>8;
	}

	if(update_mode == 1)
	{
		pr_info("warning: mode is 1, force change to 0\n");//0 don't erase user reserve area
		update_mode = 0;
	}

	if(fw_update_times == 0)//backup module sn & vcm for the first time
	{
		pr_info("Update FW FIRST time in this module!\n");
		if(asus_util_fs_write_dword_seq_text_change_line(OIS_MODULE_SN,&g_module_sn,1,1,1) == 0)
		{
			pr_info("back up module SN 0x%08x done!\n",g_module_sn);
			if(asus_util_fs_write_byte_seq_text(OIS_VCM_BACKUP,&vcm_version,1) == 0)
				pr_info("back up vcm version 0x%x done!\n",vcm_version);
		}
	}

	rc = f40_update_fw(ctrl, update_mode, module_vendor, vcm_version, updated_version);
	if(rc != 0xF0)//execute updating process
	{
		fw_update_times++;
		if(asus_util_fs_write_word_seq_text(OIS_FW_UPDATE_TIMES,&fw_update_times,1) == 0)
		{
			pr_info("Save FW update times %d done\n",fw_update_times);
		}

		if(rc == 0)
		{
			ret = UPDATE_OK;
		}
		else
		{
			pr_err("update FW failed!\n");
			ret = UPDATE_FAIL;
		}
		get_module_name_from_fw_id(g_fw_version,g_module_vendor);
	}
	else
	{
		pr_err("Can not find correct FW to update for module 0x%x, vcm 0x%x...\n",module_vendor,vcm_version);
		ret = NO_MATCH_FW;//Not find FW to update
	}
	return ret;
}

static chip_check_result_t onsemi_f40_check_chip_info(struct cam_ois_ctrl_t *o_ctrl)
{
	int rc;
	struct cam_ois_soc_private     *soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	struct cam_ois_i2c_info_t      *i2c_info = &soc_private->i2c_info;

	uint32_t chip_id;
	uint32_t fw_id;
	uint32_t actuator_id;
	uint8_t  vendor,customer,vcm;
	uint32_t servo_state;
	chip_check_result_t result;
	//check i2c
	rc = F40_IORead32A(o_ctrl,i2c_info->id_register,&chip_id);
	if(rc < 0)
	{
		pr_err("read chip id failed! rc = %d\n",rc);
		return CHECK_BAD_IO;
	}

	//check fw version
	rc = onsemi_read_dword(o_ctrl,0x8000,&fw_id);
	if(rc < 0)
	{
		pr_err("read from reg 0x%x failed! rc = %d\n",0x8000,rc);
		return CHECK_BAD_IO;
	}

	//check vcm
	rc = onsemi_read_dword(o_ctrl,0x8008,&actuator_id);
	if(rc < 0)
	{
		pr_err("read from reg 0x%x failed! rc = %d\n",0x8008,rc);
		return CHECK_BAD_IO;
	}

	if(chip_id != i2c_info->chip_id)
	{
		pr_err("read chip id 0x%x not expected\n",chip_id);
		return CHECK_BAD_ID;
	}

	g_fw_version = fw_id;
	get_module_name_from_fw_id(fw_id,g_module_vendor);//ATD

	vendor = fw_id >> 24;
	customer = (fw_id & 0x00ff0000) >> 16;
	vcm = (actuator_id & 0xff00) >> 8;

	pr_info("read fw id 0x%x, vendor 0x%x, customer 0x%x, vcm 0x%x\n",
			fw_id,vendor,customer,vcm);

	if(fw_id == 0x0)
	{
		pr_err("FW is bad!\n");
		result = CHECK_BAD_FW;
	}
	else if(!onsemi_is_servo_on(o_ctrl))
	{
		onsemi_read_dword(o_ctrl,0xF010,&servo_state);
		pr_err("servo state is 0x%x after power up, function bad!\n",servo_state);
		result = CHECK_BAD_FUNCTION;
	}
	else if(customer != 0x13)
	{
		pr_err("This module is not for ASUS!\n");
		result = CHECK_CUSTOMER_INVALID;//error customer
	}
	else if(vendor != VENDOR_ID_LITEON && vendor != VENDOR_ID_PRIMAX)
	{
		pr_err("Module vendor 0x%x invalid!\n",vendor);
		result = CHECK_VENDOR_INVALID;//vendor not valid
	}
	else if( (g_vendor_id != 0 && g_vendor_id != 0xFF) && g_vendor_id != vendor)
	{
		pr_err("Module vendor 0x%x mismatch that from eeprom 0x%x!\n",vendor,g_vendor_id);
		result = CHECK_VENDOR_MISMATCH;//vendor mismatch with eeprom
	}
	else
	{
		result = CHECK_PASS;
		if(vendor == VENDOR_ID_PRIMAX)
		{
			if(vcm != 0x01 && vcm != 0x02)
			{
				pr_err("PRIMAX VCM version 0x%x invalid!\n",vcm);
				result = CHECK_VCM_INVALID;
			}
			else if(fw_id < PRIMAX_VERNUM_BASE)
			{
				pr_err("PRIMAX FW version 0x%x invalid!\n",fw_id);
				result = CHECK_FW_VERSION_INVALID;//version not valid
			}
		}
		else if(vendor == VENDOR_ID_LITEON)
		{
			if(vcm != 0x00 && vcm != 0x01 && vcm != 0x02)
			{
				pr_err("LITEON VCM version 0x%x invalid\n",vcm);
				result = CHECK_VCM_INVALID;
			}
			else if(fw_id < LITEON_VERNUM_BASE)
			{
				pr_err("LITEON FW version 0x%x invalid!\n",fw_id);
				result = CHECK_FW_VERSION_INVALID;//version not valid
			}
		}
	}

	return result;
}
static int ois_power_up_internal(struct cam_ois_ctrl_t *o_ctrl)
{
	int rc;
	struct cam_hw_soc_info         *soc_info = &o_ctrl->soc_info;
	struct cam_ois_soc_private     *soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t *power_info = &soc_private->power_info;

	rc = cam_sensor_core_power_up(power_info, soc_info);
	if (rc) {
		pr_err("ois power up failed, rc %d\n", rc);
		return -1;
	}
	if (o_ctrl->io_master_info.master_type == CCI_MASTER)
	{
		rc = camera_io_init(&(o_ctrl->io_master_info));
		if (rc < 0) {
			pr_err("cci init failed!\n");
			rc = msm_camera_power_down(power_info, soc_info);
			if (rc) {
				pr_err("ois power down failed, rc %d\n", rc);
			}
			return -2;
		}
	}
	return rc;
}

static int ois_power_down_internal(struct cam_ois_ctrl_t *o_ctrl)
{
	int rc;
	struct cam_hw_soc_info         *soc_info = &o_ctrl->soc_info;
	struct cam_ois_soc_private     *soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t *power_info = &soc_private->power_info;

	if(o_ctrl->io_master_info.master_type == CCI_MASTER)
	{
		rc = camera_io_release(&(o_ctrl->io_master_info));
		if (rc < 0)
			pr_err("cci release failed!\n");
	}

	rc = msm_camera_power_down(power_info, soc_info);
	if (rc) {
		pr_err("ois power down failed, rc %d\n", rc);
	}
	return rc;
}

static int ois_probe_status_proc_read(struct seq_file *buf, void *v)
{
	mutex_lock(&ois_ctrl->ois_mutex);

	seq_printf(buf, "%d\n", g_ois_status);

	mutex_unlock(&ois_ctrl->ois_mutex);
	return 0;
}

static int ois_probe_status_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ois_probe_status_proc_read, NULL);
}

static const struct file_operations ois_probe_status_fops = {
	.owner = THIS_MODULE,
	.open = ois_probe_status_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_atd_status_proc_read(struct seq_file *buf, void *v)
{
	mutex_lock(&ois_ctrl->ois_mutex);

	seq_printf(buf, "%d\n", g_atd_status);
	g_atd_status = 0;//default is failure

	mutex_unlock(&ois_ctrl->ois_mutex);
	return 0;
}

static int ois_atd_status_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ois_atd_status_proc_read, NULL);
}

static ssize_t ois_atd_status_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t rc;
	char messages[16]="";
	uint32_t val;

	rc = len;

	if (len > 16) {
		len = 16;
	}

	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);
	mutex_lock(&ois_ctrl->ois_mutex);

	switch(val)
	{
		case 0:
			g_atd_status = 0;
			g_verbose_log = 0;
			break;
		case 1:
			g_atd_status = 1;
			break;
		default:
			g_atd_status = 1;
			g_verbose_log = 1;
	}
	mutex_unlock(&ois_ctrl->ois_mutex);

	pr_info("ATD status changed to %d\n",g_atd_status);

	return rc;
}

static const struct file_operations ois_atd_status_fops = {
	.owner = THIS_MODULE,
	.open = ois_atd_status_proc_open,
	.read = seq_read,
	.write = ois_atd_status_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_cali_proc_read(struct seq_file *buf, void *v)
{
	uint32_t x,y;
	uint32_t cal_data[64] = {0};
	uint16_t pos1x,pos1y,pos7x,pos7y,stepx,stepy;

	uint32_t code_per_pixel_x;
	uint32_t code_per_pixel_y;
	int16_t  hall_x,hall_y;
	static int16_t  min_x = 32767;
	static int16_t  min_y = 32767;
	static int16_t  max_x = -32768;
	static int16_t  max_y = -32768;

	mutex_lock(&ois_ctrl->ois_mutex);

	if(g_ois_power_state)
	{
		onsemi_gyro_read_xy(ois_ctrl,&x,&y);
		seq_printf(buf, "Factory(0x%x,0x%x), Recal(0x%x,0x%x), Diff(0x%x 0x%x) (%d %d), ReadVal(0x%x,0x%x)\n",
					g_calInfo.SsFctryOffX,g_calInfo.SsFctryOffY,
					g_calInfo.SsRecalOffX,g_calInfo.SsRecalOffY,
					g_calInfo.SsDiffX,g_calInfo.SsDiffY,
					g_calInfo.SsDiffX,g_calInfo.SsDiffY,
					x,y
				);
		onsemi_get_calibration_data(ois_ctrl, cal_data, sizeof(cal_data)/sizeof(uint32_t));
		seq_printf(buf, "old_hall x[0x%x,0x%x] y[0x%x,0x%x]\n\
new_hall x[0x%x,0x%x] y[0x%x,0x%x]\n\
new_hall x[%d,%d] y[%d,%d]\n\
hall_center(0x%x,0x%x) (%d,%d)\n\
Factory1(0x%x,0x%x),Factory2(0x%x,0x%x)\n",
						cal_data[2]>>16,cal_data[1]>>16, cal_data[6]>>16,cal_data[5]>>16,
						cal_data[4]>>16,cal_data[3]>>16, cal_data[8]>>16,cal_data[7]>>16,
						(int16_t)(cal_data[4]>>16),(int16_t)(cal_data[3]>>16), (int16_t)(cal_data[8]>>16),(int16_t)(cal_data[7]>>16),
						cal_data[15]>>16,cal_data[16]>>16,(int16_t)(cal_data[15]>>16),(int16_t)(cal_data[16]>>16),
						cal_data[19]>>16,cal_data[20]>>16,cal_data[49]>>16,cal_data[50]>>16
				);
		pos1x = (uint16_t)(cal_data[41]>>16);
		pos1y = (uint16_t)(cal_data[41]&0xffff);
		pos7x = (uint16_t)(cal_data[47]>>16);
		pos7y = (uint16_t)(cal_data[47]&0xffff);
		stepx = (uint16_t)(cal_data[48]>>16);
		stepy = (uint16_t)(cal_data[48]&0xffff);
		if(stepx > 32767)
			stepx = 65536 - stepx;
		if(stepy > 32767)
			stepy = 65536 - stepy;
		code_per_pixel_x = stepx*60/(abs(pos1x-pos7x));
		code_per_pixel_y = stepy*60/(abs(pos1y-pos7y));

		seq_printf(buf,"POS1X 0x%04x,%d, POS7X 0x%04x,%d, StepX 0x%04x, %d\n\
POS1Y 0x%04x,%d, POS7Y 0x%04x,%d, StepY 0x%04x, %d\n\
LN1 0x%08x, LN7 0x%08x, LN Step 0x%08x\n\
code/pixel (%d,%d)\n",
						pos1x,pos1x,pos7x,pos7x,stepx,stepx,
						pos1y,pos1y,pos7y,pos7y,stepy,stepy,
						cal_data[41],cal_data[47],cal_data[48],
						code_per_pixel_x,code_per_pixel_y
					);
		onsemi_read_pair_sensor_data(ois_ctrl,0x01b0,0x01b4,&x,&y);
		hall_x = (int16_t)(x>>16);
		hall_y = (int16_t)(y>>16);
		if(hall_x < min_x)
			min_x = hall_x;
		if(hall_x > max_x)
			max_x = hall_x;
		if(hall_y < min_y)
			min_y = hall_y;
		if(hall_y > max_y)
			max_y = hall_y;

		seq_printf(buf,"hall raw(0x%08x,0x%08x)=>(0x%04x,0x%04x)=>(%d,%d), x[%d,%d], y[%d,%d]\n",
					x,y,
					hall_x,hall_y,hall_x,hall_y,
					min_x,max_x,
					min_y,max_y
		);

	}
	else
	{
		seq_printf(buf,"POWER DOWN\n");
	}

	mutex_unlock(&ois_ctrl->ois_mutex);

	return 0;
}

static int ois_cali_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ois_cali_proc_read, NULL);
}

static ssize_t ois_cali_proc_write(struct file *dev, const char *buf, size_t count, loff_t *ppos)
{
	uint8_t rc;
	#define GYRO_K_REG_COUNT 2
	uint32_t gyro_data[GYRO_K_REG_COUNT];
	pr_info("E\n");

	mutex_lock(&g_busy_job_mutex);

	mutex_lock(&ois_ctrl->ois_mutex);

	pr_info("start gyro calibration\n");
	rc = onsemi_gyro_calibration(ois_ctrl, &g_calInfo);
	if(rc != 0)
	{
		pr_err("onsemi_gyro_calibration failed! rc = 0x%02x\n", rc);
		g_atd_status = 0;
	}
	else
	{
		pr_info("onsemi_gyro_calibration success!\n");
		g_atd_status = 1;
	}
    //ASUS_BSP Lucien +++: Save one Gyro data after doing OIS calibration
	rc = onsemi_gyro_read_xy(ois_ctrl, &gyro_data[0], &gyro_data[1]);
	if(rc < 0) pr_err("onsemi_gyro_read_xy get fail! rc = %d\n", rc);
	onsemi_shift_dword_data(gyro_data,GYRO_K_REG_COUNT);
	rc = asus_util_fs_write_dword_seq_text_change_line(OIS_GYRO_K_OUTPUT_FILE_NEW,gyro_data,GYRO_K_REG_COUNT,GYRO_K_REG_COUNT,0);
	if(rc != 0) pr_err("asus_util_fs_write_dword_seq_text_change_line fail! rc = %d\n", rc);
	//ASUS_BSP Lucien ---: Save one Gyro data after doing OIS calibration

	mutex_unlock(&ois_ctrl->ois_mutex);

	mutex_unlock(&g_busy_job_mutex);
	pr_info("X\n" );

	return count;
}

static const struct file_operations ois_cali_fops = {
	.owner = THIS_MODULE,
	.open = ois_cali_proc_open,
	.read = seq_read,
	.write = ois_cali_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_mode_proc_read(struct seq_file *buf, void *v)
{
    mutex_lock(&ois_ctrl->ois_mutex);
	seq_printf(buf, "%d\n", g_ois_mode);
	mutex_unlock(&ois_ctrl->ois_mutex);
	return 0;
}

static int ois_mode_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ois_mode_proc_read, NULL);
}

static ssize_t ois_mode_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint32_t val;
	int rc;
	ret_len = len;

	if (len > 16) {
		len = 16;
	}

	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);

	mutex_lock(&ois_ctrl->ois_mutex);

	pr_info("start change ois mode\n");

	rc = onsemi_switch_mode(ois_ctrl,val);

	if(rc == 0)
	{
		g_atd_status = 1;
		g_ois_mode = val;
		pr_info("OIS mode changed to %d by ATD\n",g_ois_mode);
	}
	else
	{
		g_atd_status = 0;
		pr_err("switch to mode %d failed! rc = %d\n",val,rc);
	}

	mutex_unlock(&ois_ctrl->ois_mutex);

	return ret_len;
}

static const struct file_operations ois_mode_fops = {
	.owner = THIS_MODULE,
	.open = ois_mode_proc_open,
	.read = seq_read,
	.write = ois_mode_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};


static int ois_device_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%s\n",ois_subdev_string);
	return 0;
}

static int ois_device_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_device_read, NULL);
}

static ssize_t ois_device_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret_len;
	char messages[64]="";

	ret_len = len;
	if (len > 64) {
		len = 64;
	}
	if (copy_from_user(messages, buf, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages, "%s", ois_subdev_string);

	pr_info("write subdev=%s\n", ois_subdev_string);
	return ret_len;
}

static const struct file_operations ois_device_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ois_device_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ois_device_write,
};

//ASUS_BSP Lucien +++: Add OIS SEMCO module
static int ois_module_read(struct seq_file *buf, void *v)
{
	mutex_lock(&ois_ctrl->ois_mutex);
	seq_printf(buf, "%s\n",g_module_vendor);
	mutex_unlock(&ois_ctrl->ois_mutex);
	return 0;
}

static int ois_module_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_module_read, NULL);
}

static ssize_t ois_module_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{
	return 0;
}

static const struct file_operations ois_module_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ois_module_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ois_module_write,
};
//ASUS_BSP Lucien ---: Add OIS SEMCO module
static int ois_i2c_debug_read(struct seq_file *buf, void *v)
{
	uint32_t reg_val;
	int rc;

	mutex_lock(&ois_ctrl->ois_mutex);

	if(g_ois_power_state)
	{
		F40_WaitProcess(ois_ctrl,0,__func__);
		ois_ctrl->io_master_info.cci_client->sid = g_slave_id;
		switch(g_data_type)
		{
			case CAMERA_SENSOR_I2C_TYPE_BYTE:
				rc = onsemi_read_byte(ois_ctrl,g_reg_addr,&reg_val);
				break;
			case CAMERA_SENSOR_I2C_TYPE_WORD:
				rc = onsemi_read_word(ois_ctrl,g_reg_addr,&reg_val);
				break;
			case CAMERA_SENSOR_I2C_TYPE_DWORD:
				rc = onsemi_read_dword(ois_ctrl,g_reg_addr,&reg_val);
				break;
			default:
				rc = onsemi_read_dword(ois_ctrl,g_reg_addr,&reg_val);
		}

		if(g_operation == 1)//write
		{
			if(reg_val == g_reg_val)
			{
				pr_info("read back the same value as write!\n");
			}
			else
			{
				pr_err("write value 0x%x and read back value 0x%x not same!\n",
						g_reg_val,reg_val
				);
			}
		}

		if(rc == 0)
		{
			g_atd_status = 1;
		}
		else
		{
			g_atd_status = 0;
			pr_err("read from reg 0x%x failed! rc = %d\n",g_reg_addr,rc);
		}

		seq_printf(buf,"0x%x\n",reg_val);
	}
	else
	{
		seq_printf(buf,"POWER DOWN\n");
	}

	mutex_unlock(&ois_ctrl->ois_mutex);

	return 0;
}

static int ois_i2c_debug_open(struct inode *inode, struct  file *file)
{
	return single_open(file, ois_i2c_debug_read, NULL);
}
static ssize_t ois_i2c_debug_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
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

	mutex_lock(&ois_ctrl->ois_mutex);

	if(n == 1)
	{
		g_reg_addr = val[0];
		g_operation = 0;
		g_data_type = 4;//default dword
	}
	else if(n == 2)
	{
		//data type
		// 1 byte 2 word 4 dword
		g_reg_addr = val[0];
		g_data_type = val[1];
		g_operation = 0;
	}
	else if(n == 3)
	{
		g_reg_addr = val[0];
		g_data_type = val[1];
		g_reg_val = val[2];
		g_operation = 1;
	}
	else if(n == 4)
	{
		g_reg_addr = val[0];
		g_data_type = val[1];
		g_reg_val = val[2];
		g_slave_id = val[3];//slave id
		g_operation = 1;
	}

	if(g_data_type != 1 && g_data_type != 2 && g_data_type != 4 )
		g_data_type = 4;//default dword
	pr_info("gona %s SLAVE 0x%X reg 0x%04x, data type %s\n",
			g_operation ? "WRITE":"READ",
			g_slave_id,
			g_reg_addr,
			g_data_type == 1?"BYTE":(g_data_type == 2?"WORD":"DWORD")
	);

	switch(g_data_type)
	{
		case 1:
			g_data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			break;
		case 2:
			g_data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			break;
		case 4:
			g_data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
			break;
		default:
			g_data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
	}

	if(g_operation == 1)
	{
		F40_WaitProcess(ois_ctrl,0,__func__);
		switch(g_data_type)
		{
			case CAMERA_SENSOR_I2C_TYPE_BYTE:
				rc = onsemi_write_byte(ois_ctrl,g_reg_addr,g_reg_val);
				break;
			case CAMERA_SENSOR_I2C_TYPE_WORD:
				rc = onsemi_write_word(ois_ctrl,g_reg_addr,g_reg_val);
				break;
			case CAMERA_SENSOR_I2C_TYPE_DWORD:
				rc = onsemi_write_dword(ois_ctrl,g_reg_addr,g_reg_val);
				break;
			default:
				rc = onsemi_write_dword(ois_ctrl,g_reg_addr,g_reg_val);
		}
		if(rc < 0)
		{
			pr_err("write 0x%x to reg 0x%04x FAIL\n",g_reg_val,g_reg_addr);
			g_atd_status = 0;
		}
		else
		{
			pr_info("write 0x%x to reg 0x%04x OK\n",g_reg_val,g_reg_addr);
			g_atd_status = 1;
			if(g_data_type == CAMERA_SENSOR_I2C_TYPE_DWORD && g_reg_addr == 0xF01A)
			{
				set_vcm_lens_pos_from_ois_writing(g_reg_val&0x07FF);
			}
		}
	}

	mutex_unlock(&ois_ctrl->ois_mutex);

	return ret_len;
}
static const struct file_operations ois_i2c_debug_fops = {
	.owner = THIS_MODULE,
	.open = ois_i2c_debug_open,
	.read = seq_read,
	.write = ois_i2c_debug_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_solo_power_read(struct seq_file *buf, void *v)
{
    mutex_lock(&ois_ctrl->ois_mutex);
    pr_info("g_ois_power_state = %d\n", g_ois_power_state);
	seq_printf(buf,"%d\n",g_ois_power_state);
	mutex_unlock(&ois_ctrl->ois_mutex);
	return 0;
}

static int ois_solo_power_open(struct inode *inode, struct  file *file)
{
	return single_open(file, ois_solo_power_read, NULL);
}

//just for ATD test
static ssize_t ois_solo_power_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	int val;
	int rc;
	struct cam_hw_soc_info         *soc_info = &ois_ctrl->soc_info;
	struct cam_ois_soc_private     *soc_private =
		(struct cam_ois_soc_private *)ois_ctrl->soc_info.soc_private;

	struct cam_sensor_power_ctrl_t *power_info = &soc_private->power_info;

	ret_len = len;
	if (len > 16) {
		len = 16;
	}
	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);
	mutex_lock(&ois_ctrl->ois_mutex);
	if(g_ois_camera_open == 0)
	{
		if(val == 0)
		{
			if(g_ois_power_state == 1)
			{
				camera_io_release(&(ois_ctrl->io_master_info));
				rc = msm_camera_power_down(power_info, soc_info);
				if (rc) {
					pr_err("%s: msm_camera_power_down fail rc = %d\n", __func__, rc);
				}
				else
				{
					g_ois_power_state = 0;
					g_ois_mode = 255;
					pr_info("OIS POWER DOWN\n");
				}
			}
			else
			{
				pr_info("OIS already power off, do nothing\n");
			}
		}
		else
		{
			if(g_ois_power_state == 0)
			{
				rc = cam_sensor_core_power_up(power_info, soc_info);
				if (rc) {
					pr_err("%s: msm_camera_power_up fail rc = %d\n", __func__, rc);
				}
				else
				{
					asus_util_delay_ms(100);
					g_ois_power_state = 1;
					g_ois_mode = 255;
					camera_io_init(&(ois_ctrl->io_master_info));
					if(g_ois_status == 1)
					{
						F40_WaitProcess(ois_ctrl,0,__func__);
						onsemi_config_ssc_gain(ois_ctrl,100);
					}
					else
					{
						pr_err("OIS probe failed, not config ssc gain\n");
					}
					pr_info("OIS POWER UP\n");
				}
			}
			else
			{
				pr_info("OIS already power up, do nothing\n");
			}
		}
	}
	else
	{
		pr_err("camera has been opened, can't control ois power\n");
	}
	mutex_unlock(&ois_ctrl->ois_mutex);
	return ret_len;
}
static const struct file_operations ois_solo_power_fops = {
	.owner = THIS_MODULE,
	.open = ois_solo_power_open,
	.read = seq_read,
	.write = ois_solo_power_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_state_proc_read(struct seq_file *buf, void *v)
{
	char dump_buf[512];//must be large enough
	uint8_t battery_capacity;
	mutex_lock(&ois_ctrl->ois_mutex);
	if(g_ois_power_state == 1)
	{
		F40_WaitProcess(ois_ctrl,0,__func__);
		onsemi_dump_state(ois_ctrl,dump_buf,sizeof(dump_buf));
		seq_printf(buf, "%s\n", dump_buf);
		if(asus_util_fs_read_text_uint8(BATTERY_CAPACITY,&battery_capacity) == 0)
		{
			pr_info("get battery capacity is %d%%\n",battery_capacity);
			seq_printf(buf, "battery: %d%%\n", battery_capacity);
		}
	}
	else
	{
		seq_printf(buf, "POWER DOWN\n");
	}
	mutex_unlock(&ois_ctrl->ois_mutex);
	return 0;
}

static int ois_state_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, ois_state_proc_read, NULL);
}

static const struct file_operations ois_state_fops = {
	.owner = THIS_MODULE,
	.open = ois_state_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_on_proc_read(struct seq_file *buf, void *v)
{
	int state;
	mutex_lock(&ois_ctrl->ois_mutex);
	if(g_ois_power_state)
	{
		if(onsemi_is_ois_on(ois_ctrl))
			state=1;
		else
			state=0;
		seq_printf(buf, "%d\n", state);
	}
	else
		seq_printf(buf, "%s\n", "POWER DOWN\n");
	mutex_unlock(&ois_ctrl->ois_mutex);
	return 0;
}

static int ois_on_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_on_proc_read, NULL);
}

static ssize_t ois_on_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint32_t val;
	int rc;

	ret_len = len;

	if (len > 16) {
		len = 16;
	}

	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);
	pr_info("OIS on mode %d\n",val);

	mutex_lock(&ois_ctrl->ois_mutex);
	if(g_ois_power_state)
	{
		switch(val)
		{
			case 0:
				rc = onsemi_ois_go_off(ois_ctrl);
				break;

			case 1:
				rc = onsemi_ois_go_on(ois_ctrl);
				break;
			default:
				pr_err("Not supported command %d\n",val);
				rc = -1;
		}

		if(rc == 0)
		{
			g_atd_status = 1;
		}
		else
		{
			g_atd_status = 0;
			pr_err("OIS on/off failed! rc = %d\n",rc);
		}
	}
	else
	{
		pr_err("OIS POWER DOWN, power it up first!\n");
		g_atd_status = 0;
	}
	mutex_unlock(&ois_ctrl->ois_mutex);

	return ret_len;
}

static const struct file_operations ois_on_fops = {
	.owner = THIS_MODULE,
	.open = ois_on_proc_open,
	.read = seq_read,
	.write = ois_on_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int process_rdata(int count)
{
#define PAIR 3
	int rc;
	int i;
	uint32_t old_state;
	struct timeval t1,t2;

	uint32_t *pData = NULL;
	uint32_t reg_addr[PAIR*2] = {  0x0258,0x025c,//gyro raw
							     0x01b0,0x01b4, //hall raw
							     0x05e0,0x060c //acc raw
						        };
	int j;
	pData = kzalloc(sizeof(uint32_t)*count*PAIR*2,GFP_KERNEL);
	if (!pData)
	{
		pr_err("no memory!\n");
		return -1;
	}

	onsemi_get_ois_state(ois_ctrl,&old_state);
	//onsemi_ois_go_on(ois_ctrl);//servo and ois must be on

	do_gettimeofday(&t1);
	for(i=0;i<count;i++)
	{
		for(j=0;j<PAIR;j++)
		{
			rc = onsemi_read_pair_sensor_data(ois_ctrl,
											reg_addr[2*j],reg_addr[2*j+1],
											pData+PAIR*2*i+2*j,pData+PAIR*2*i+2*j+1
											);
			if(rc < 0)
			{
				pr_err("read %d times x,y failed! rc = %d\n",i,rc);
				rc = -2;
				break;
			}
		}
		if(rc<0)
		{
			break;
		}

		if(count%16 == 0)
			asus_util_delay_ms(1);
	}
	do_gettimeofday(&t2);
	pr_info("read %d times x,y values, cost %lld ms, each cost %lld us\n",
		i,
		asus_util_diff_time_us(&t2,&t1)/1000,
		asus_util_diff_time_us(&t2,&t1)/(i)
	);

	onsemi_restore_ois_state(ois_ctrl,old_state);

	if(i == count)
	{
		//all pass, store to /sdcard/gyro.csv
		//remove file....do it in script
		onsemi_shift_dword_data(pData,count*PAIR*2);
		if(asus_util_fs_write_dword_seq_text_change_line(RDATA_OUTPUT_FILE,pData,count*PAIR*2,PAIR*2,0) == 0)
		{
			pr_info("store gyro data to %s succeeded!\n",RDATA_OUTPUT_FILE);
			rc = 0;
		}
		else
		{
			pr_err("store gyro data to %s failed!\n",RDATA_OUTPUT_FILE);
			rc = -3;
		}
	}
	else
	{
		pr_err("read data failed, read %d data\n",i);
	}


	kfree(pData);

	return rc;
}

static int ois_rdata_proc_read(struct seq_file *buf, void *v)
{
	uint8_t* pText;
	uint64_t size;

	mutex_lock(&ois_ctrl->ois_mutex);

	if(asus_util_fs_get_file_size(RDATA_OUTPUT_FILE,&size) == 0 && size > 0)
	{
		pText = kzalloc(sizeof(uint8_t)*size,GFP_KERNEL);
		if (!pText)
		{
			pr_err("no memory!\n");
			mutex_unlock(&ois_ctrl->ois_mutex);
			return 0;
		}
		asus_util_fs_read_file_into_buffer(RDATA_OUTPUT_FILE,pText,size);//Text File

		seq_printf(buf,"%s\n",pText);//ASCII

		kfree(pText);
	}
	else
	{
		seq_printf(buf,"file is empty!\n");
	}

	mutex_unlock(&ois_ctrl->ois_mutex);

	return 0;
}

static int ois_rdata_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_rdata_proc_read, NULL);
}

static ssize_t ois_rdata_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	ssize_t ret_len;
	char messages[16]="";
	uint32_t val;
	int rc;

	ret_len = len;

	if (len > 16) {
		len = 16;
	}

	if (copy_from_user(messages, buff, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages,"%d",&val);

	mutex_lock(&g_busy_job_mutex);

	mutex_lock(&ois_ctrl->ois_mutex);

	if(g_ois_power_state)
	{
		rc = process_rdata(val); //ASUS_BSP Lucien +++: Save val numbers of Gyro X/Y and ACC X/Y data
		if(rc == 0)
		{
			g_atd_status = 1;
		}
		else
		{
			g_atd_status = 0;
			pr_err("OIS Rdata failed! rc = %d\n",rc);
		}
	}
	else
	{
		pr_err("OIS POWER DOWN, power it up first!\n");
		g_atd_status = 0;
	}

	mutex_unlock(&ois_ctrl->ois_mutex);

	mutex_unlock(&g_busy_job_mutex);

	return ret_len;
}

static const struct file_operations ois_rdata_fops = {
	.owner = THIS_MODULE,
	.open = ois_rdata_proc_open,
	.read = seq_read,
	.write = ois_rdata_proc_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int ois_update_fw_read(struct seq_file *buf, void *v)
{
    mutex_lock(&ois_ctrl->ois_mutex);
	seq_printf(buf, "%x\n", g_fw_version);
	mutex_unlock(&ois_ctrl->ois_mutex);
	return 0;
}

static int ois_update_fw_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_update_fw_read, NULL);
}

static ssize_t ois_update_fw_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret_len;
	char messages[64]="";
	int n;
	uint32_t val[2];
	uint32_t force_update, update_mode;

	ret_len = len;
	if (len > 64) {
		len = 64;
	}
	if (copy_from_user(messages, buf, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	n = sscanf(messages, "%x %x", &val[0],&val[1]);
	if(n == 1)
	{
		force_update = val[0];
		update_mode = 0;
	}
	else if(n == 2)
	{
		force_update = val[0];
		update_mode = val[1];
	}
	else
	{
		pr_err("Invalid argument count %d!\n",n);
		return ret_len;
	}

	mutex_lock(&g_busy_job_mutex);

	mutex_lock(&ois_ctrl->ois_mutex);

	if(g_ois_power_state == 0)
	{
		pr_err("OIS POWER DOWN, power it up first!\n");
		goto END;
	}

	pr_info("trigger fw update, force update %d, update mode %d\n",force_update,update_mode);

	trigger_fw_update(ois_ctrl, update_mode, force_update, &g_fw_version);

END:
	mutex_unlock(&ois_ctrl->ois_mutex);
	mutex_unlock(&g_busy_job_mutex);
	return ret_len;
}

static const struct file_operations ois_update_fw_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ois_update_fw_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ois_update_fw_write,
};


static int ois_vcm_enable_read(struct seq_file *buf, void *v)
{
    mutex_lock(&ois_ctrl->ois_mutex);
	seq_printf(buf, "%d\n", g_vcm_enabled);
	mutex_unlock(&ois_ctrl->ois_mutex);
	return 0;
}

static int ois_vcm_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_vcm_enable_read, NULL);
}

static ssize_t ois_vcm_enable_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret_len;
	char messages[8]="";

	uint32_t val;

	ret_len = len;
	if (len > 8) {
		len = 8;
	}
	if (copy_from_user(messages, buf, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages, "%d", &val);

	mutex_lock(&ois_ctrl->ois_mutex);

	if(val == 0)
		g_vcm_enabled = 0;
	else
		g_vcm_enabled = 1;

	pr_info("ois vcm enabled set to %d\n",g_vcm_enabled);

	mutex_unlock(&ois_ctrl->ois_mutex);

	return ret_len;
}

static const struct file_operations ois_vcm_enable_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ois_vcm_enable_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ois_vcm_enable_write,
};

static int ois_af_state_read(struct seq_file *buf, void *v)
{
	mutex_lock(&ois_ctrl->ois_mutex);
	seq_printf(buf, "LensMoving: %d\n", g_lens_moving);
	seq_printf(buf, "DAC_10cm: %d, DAC_50cm: %d\n",
				     g_dac_10cm, g_dac_50cm);
	seq_printf(buf, "DAC_10cm_DIT: %d, DAC_50cm_DIT: %d\n",
				     g_dac_10cm_dit, g_dac_50cm_dit);
	mutex_unlock(&ois_ctrl->ois_mutex);
	return 0;
}

static int ois_af_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, ois_af_state_read, NULL);
}

static ssize_t ois_af_state_write(struct file *dev, const char *buf, size_t len, loff_t *ppos)
{
	ssize_t ret_len;
	char messages[8]="";

	uint32_t val;

	ret_len = len;
	if (len > 8) {
		len = 8;
	}
	if (copy_from_user(messages, buf, len)) {
		pr_err("%s command fail !!\n", __func__);
		return -EFAULT;
	}

	sscanf(messages, "%d", &val);

	mutex_lock(&ois_ctrl->ois_mutex);

	if(val == 0)
		g_lens_moving = 0;
	else
		g_lens_moving = 1;

	//pr_info("ois lens moving state set to %d\n",g_lens_moving);
	set_ssc_gain_if_need();
	mutex_unlock(&ois_ctrl->ois_mutex);

	return ret_len;
}

static const struct file_operations ois_af_state_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ois_af_state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= ois_af_state_write,
};



void track_mode_change_from_i2c_write(struct cam_sensor_i2c_reg_setting * setting)
{
	uint32_t data[2];
	uint16_t index[2];
	bool     ret[2];

	ret[0] = asus_util_i2c_setting_contain_address(setting,0xF013, &data[0], &index[0]);//movie/still mode
	ret[1] = asus_util_i2c_setting_contain_address(setting,0xF012, &data[1], &index[1]);//ois on/off

	if(ret[1] && data[1] == 0x0)
	{
		g_ois_mode = 0;
		CAM_INFO(CAM_OIS, "mode => %d CENTER by HAL",g_ois_mode);
	}
	else if(ret[0])
	{
		if(data[0] == 0x0)
		{
			g_ois_mode = 1;
			CAM_INFO(CAM_OIS, "mode => %d MOVIE by HAL",g_ois_mode);
		}
		else if(data[0] == 0x1)
		{
			g_ois_mode = 2;
			CAM_INFO(CAM_OIS, "mode => %d STILL by HAL",g_ois_mode);
		}
		else
		{
			CAM_INFO(CAM_OIS, "ois mode set val 0x%x not valid",data[0]);
		}
	}
}

static void create_proc_file(const char *PATH,const struct file_operations* f_ops)
{
	struct proc_dir_entry *pde;

	pde = proc_create(PATH, 0666, NULL, f_ops);
	if(pde)
	{
		pr_info("create(%s) done\n",PATH);
	}
	else
	{
		pr_err("create(%s) failed!\n",PATH);
	}
}

static void create_ois_proc_files_shipping(void)
{
	static uint8_t has_created = 0;
	if(!has_created)
	{
		create_proc_file(PROC_DEVICE, &ois_device_proc_fops);
		create_proc_file(PROC_LENS_MOVING, &ois_af_state_proc_fops);

		has_created = 1;
	}
	else
	{
		pr_err("OIS shipping proc files have already created!\n");
	}
}

static void create_ois_proc_files_factory(void)
{
	static uint8_t has_created = 0;

	if(!has_created)
	{
		create_proc_file(PROC_POWER, &ois_solo_power_fops);
		create_proc_file(PROC_I2C_RW,&ois_i2c_debug_fops);//ATD
		create_proc_file(PROC_ON, &ois_on_fops);
		create_proc_file(PROC_STATE, &ois_state_fops);
		create_proc_file(PROC_MODE, &ois_mode_fops);//ATD
		create_proc_file(PROC_CALI, &ois_cali_fops);//ATD
		create_proc_file(PROC_RDATA, &ois_rdata_fops);//ATD
		create_proc_file(PROC_ATD_STATUS, &ois_atd_status_fops);//ATD
		create_proc_file(PROC_PROBE_STATUS, &ois_probe_status_fops);//ATD
		create_proc_file(PROC_FW_UPDATE, &ois_update_fw_proc_fops);
		create_proc_file(PROC_MODULE, &ois_module_proc_fops);//ATD ASUS_BSP Lucien +++: Add OIS SEMCO module
		create_proc_file(PROC_VCM_ENABLE, &ois_vcm_enable_proc_fops);

		has_created = 1;
	}
	else
	{
		pr_err("OIS factory proc files have already created!\n");
	}
}
static void check_chip_and_update_status(void)
{
	chip_check_result_t check_result;
	check_result = onsemi_f40_check_chip_info(ois_ctrl);
	switch(check_result)
	{
		case CHECK_BAD_IO:
			 g_ois_status = 0;
			 break;
		case CHECK_PASS:
			 g_ois_status = 1;
			 break;
		case CHECK_BAD_ID:
			 g_ois_status = 2;//ID not match
			 break;
		case CHECK_BAD_FW:
			 g_ois_status = 3;//FW bad
			 break;
		case CHECK_BAD_FUNCTION:
		     g_ois_status = 4;//Function bad
			 break;
		case CHECK_VENDOR_MISMATCH:
		     g_ois_status = 5;//vendor in eeprom mismatch fw
		     break;
		case CHECK_VENDOR_INVALID:
		case CHECK_CUSTOMER_INVALID:
		case CHECK_VCM_INVALID:
		case CHECK_FW_VERSION_INVALID:
			 g_ois_status = 6;//module invalid
			 break;
	}
}
void ois_probe_check(void)
{
	int rc;
#if UPDATE_FW_AT_PROBE
	fw_trigger_result_t trigger_result;
#endif
	if(ois_ctrl == NULL)
	{
		pr_err("ois_ctrl is NULL!!!\n");
		return;
	}
	rc = read_vendor_id_from_eeprom(&g_vendor_id);
	if(rc == 0)
	{
		pr_info("Got Vendor ID 0x%x from EEPROM Read\n",g_vendor_id);
	}
	else
		g_vendor_id = 0;

	rc = read_module_sn_from_eeprom(&g_module_sn);
	if(rc == 0)
	{
		pr_info("Got Module SN 0x%08x from EEPROM Read\n",g_module_sn);
	}
	else
		g_module_sn = 0;

	asus_util_delay_ms(60);
	F40_WaitProcess(ois_ctrl,500,__func__);//waiting after power on

	check_chip_and_update_status();
	if(g_ois_status == 1 || g_ois_status == 3)
	{
	#if UPDATE_FW_AT_PROBE
		pr_info("trigger fw update...ois status 0x%x\n",g_ois_status);
		trigger_result = trigger_fw_update(ois_ctrl,0,0,&g_fw_version);
		if(trigger_result == UPDATE_OK || trigger_result == UPDATE_FAIL)//execute FW updating process
		{
			check_chip_and_update_status();
			pr_info("after FW update, ois status is 0x%x\n",g_ois_status);
		}
		else
		{
			pr_info("FW not update\n");
		}
	#endif
	}
	else
	{
		pr_err("check chip failed! ois status 0x%x\n",g_ois_status);
	}
	if(g_ois_status == 1)
		create_ois_proc_files_shipping();
}

uint8_t get_ois_probe_status(void)
{
	if(ois_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return 0;
	}
	return g_ois_status;
}

void ois_lock(void)
{
	if(ois_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return;
	}
	mutex_lock(&ois_ctrl->ois_mutex);
}

void ois_unlock(void)
{
	if(ois_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return;
	}
	mutex_unlock(&ois_ctrl->ois_mutex);
}

void ois_wait_process(void)
{
	if(ois_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return;
	}
	F40_WaitProcess(ois_ctrl,0,__func__);
}

int ois_busy_job_trylock(void)
{
	if(ois_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return 0;
	}
	return mutex_trylock(&g_busy_job_mutex);
}

void ois_busy_job_unlock(void)
{
	if(ois_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return;
	}
	mutex_unlock(&g_busy_job_mutex);
}

void set_ois_module_vendor_from_eeprom(uint8_t vendor_id)
{
	if(vendor_id == 0x01)
		g_vendor_id = VENDOR_ID_LITEON;
	else if(vendor_id == 0x06)
		g_vendor_id = VENDOR_ID_PRIMAX;
	pr_info("Got Vendor ID 0x%x from EEPROM DUMP Info\n",g_vendor_id);
}

void set_ois_module_sn_from_eeprom(uint32_t sn)
{
	g_module_sn = sn;
	pr_info("Got Module SN 0x%08x from EEPROM DUMP Info\n",g_module_sn);
}

void set_rear_eeprom_ctrl(struct cam_eeprom_ctrl_t * e_ctrl)
{
	struct cam_sensor_cci_client *cci_client = NULL;

	if(e_ctrl != NULL)
	{
		if (e_ctrl->io_master_info.master_type == CCI_MASTER)
		{
			cci_client = e_ctrl->io_master_info.cci_client;
			if (!cci_client) {
				pr_err("failed: cci_client %pK",cci_client);
				return;
			}
			cci_client->cci_i2c_master = e_ctrl->cci_i2c_master;
			cci_client->sid = (0xA0) >> 1;
			cci_client->retries = 3;
			cci_client->id_map = 0;
			cci_client->i2c_freq_mode = 1;

			g_ectrl = e_ctrl;
			pr_info("config ectrl done!\n");
		}
	}
	else
	{
		pr_err("e_ctrl is NULL!\n");
	}
}

uint8_t ois_allow_vcm_move(void)
{
	return g_vcm_enabled;
}

void set_ois_afc_data_from_eeprom(uint32_t dac_10cm, uint32_t dac_50cm)
{
	g_dac_10cm = dac_10cm;
	g_dac_50cm = dac_50cm;
	g_dac_per_cm = (dac_10cm - dac_50cm)/40;//dac larger, distance nearer
	pr_info("Get AFC data, [10cm %d], [50cm %d] , [%d dac/cm]\n",g_dac_10cm, g_dac_50cm, g_dac_per_cm);
}

void set_ois_dit_afc_data_from_eeprom(uint32_t dac_10cm, uint32_t dac_50cm)
{
	g_dac_10cm_dit = dac_10cm;
	g_dac_50cm_dit = dac_50cm;
	pr_info("Get DIT AFC data, [10cm %d], [50cm %d]\n",g_dac_10cm_dit,g_dac_50cm_dit);
}

static int32_t dac_to_distance_cm(uint32_t dac_value, uint32_t* distance_cm)
{
	int32_t  temp_shift_value;
	uint32_t shift_value;
	int32_t  rc;
	//dac larger, shift larger
	temp_shift_value = (int32_t)((int32_t)dac_value - (int32_t)g_dac_10cm_base)*(int32_t)g_lens_shift_10cm_to_50cm/((int32_t)g_dac_10cm_base - (int32_t)g_dac_50cm_base) + (int32_t)g_lens_shift_10cm;
	if(temp_shift_value <= 0)
		temp_shift_value = 0;
	shift_value = temp_shift_value;

	rc = onsemi_lens_shift_to_distance(shift_value,distance_cm);

	if(g_verbose_log && rc == 0)
		pr_info("DAC %d -> shift %d -> distance %d\n",dac_value,shift_value,*distance_cm);

	return rc;
}

static void set_ssc_gain_if_need(void)
{
	uint32_t distance_cm;
	uint32_t current_dac;
	struct timeval t1,t2;
	if( g_lens_moving == 0
	    && g_ois_power_state
	    && g_dac_per_cm
        && get_current_lens_position(&current_dac) == 0
        && dac_to_distance_cm(current_dac,&distance_cm) == 0 )
	{
		if(distance_cm != g_distance_prev)
		{
			if(g_distance_prev <= 20 || distance_cm <= 20)
			{
				do_gettimeofday(&t1);
				/*
				if(asus_util_diff_time_us(&t1,&g_ssc_config_time_prev) < 200*1000)
				{
					pr_info("SKIP %d cm, interval %lld us\n",distance_cm,asus_util_diff_time_us(&t1,&g_ssc_config_time_prev));
					return;
				}
				*/
				onsemi_config_ssc_gain(ois_ctrl,distance_cm);
				do_gettimeofday(&t2);
				pr_info("CONFIG ssc %d cm, cost %lld us\n",distance_cm,asus_util_diff_time_us(&t2,&t1));
				g_ssc_config_time_prev = t1;
				g_distance_prev = distance_cm;
			}
		}
	}
}

void asus_ois_init_config(void)
{
	if(ois_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return;
	}

	if(g_ois_status == 1)
	{
		F40_WaitProcess(ois_ctrl,500,__func__);
		onsemi_config_ssc_gain(ois_ctrl,100);
	}
	else
	{
		pr_err("OIS probe failed, not config ssc gain\n");
	}
	g_ois_power_state = 1;
	g_ois_camera_open = 1;
	g_ois_mode = 255;
	g_distance_prev = 0;
	memset(&g_ssc_config_time_prev,0,sizeof(struct timeval));

	if(g_dac_10cm_dit && g_dac_50cm_dit)
	{
		g_dac_10cm_base = g_dac_10cm_dit;
		g_dac_50cm_base = g_dac_50cm_dit;
	}
	else
	{
		g_dac_10cm_base = g_dac_10cm;
		g_dac_50cm_base = g_dac_50cm;
	}
}

void asus_ois_deinit_config(void)
{
	g_ois_power_state = 0;
	g_ois_camera_open = 0;
	g_ois_mode = 255;
	g_distance_prev = 0;
	memset(&g_ssc_config_time_prev,0,sizeof(struct timeval));

	g_dac_10cm_base = 0;
	g_dac_50cm_base = 0;
}

int ois_power_up(void)
{
	int rc;
	if(ois_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return -1;
	}
	mutex_lock(&ois_ctrl->ois_mutex);
	rc = ois_power_up_internal(ois_ctrl);
	do_gettimeofday(&g_power_up_time);
	mutex_unlock(&ois_ctrl->ois_mutex);
	return rc;
}

int ois_power_down(void)
{
	int rc;
	if(ois_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return -1;
	}
	mutex_lock(&ois_ctrl->ois_mutex);
	if(g_ois_status == 1)
		onsemi_ois_go_off(ois_ctrl);
	rc = ois_power_down_internal(ois_ctrl);
	memset(&g_power_up_time,0,sizeof(struct timeval));
	mutex_unlock(&ois_ctrl->ois_mutex);
	return rc;
}

void ois_wait_internal_boot_time_ms(uint32_t limit_ms)
{
	struct timeval now;
	int64_t interval_us;

	if(ois_ctrl == NULL)
	{
		pr_err("OIS not init!!!\n");
		return;
	}

	do_gettimeofday(&now);
	interval_us = asus_util_diff_time_us(&now,&g_power_up_time);
	if(interval_us < limit_ms*1000)
	{
		asus_util_delay_us(limit_ms*1000-interval_us);
		pr_info("delay %lld us for ois internal boot",limit_ms*1000-interval_us);
	}
}

void asus_ois_init(struct cam_ois_ctrl_t * ctrl)
{
	if(ctrl)
		ois_ctrl = ctrl;
	else
	{
		pr_err("ois_ctrl_t passed in is NULL!\n");
		return;
	}

	create_ois_proc_files_factory();

	mutex_init(&g_busy_job_mutex);
	onsemi_get_50cm_to_10cm_lens_shift(&g_lens_shift_10cm_to_50cm);
	onsemi_get_10cm_lens_shift(&g_lens_shift_10cm);
	memset(&g_power_up_time,0,sizeof(struct timeval));
}
