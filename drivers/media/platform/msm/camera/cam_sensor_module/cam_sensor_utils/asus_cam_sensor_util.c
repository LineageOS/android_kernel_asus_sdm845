#include "asus_cam_sensor_util.h"
#include <linux/fs.h>
#include <asm/uaccess.h>

#undef  pr_fmt
#define pr_fmt(fmt) "ASUS-UTIL %s(): " fmt, __func__

void asus_util_delay_ms(uint32_t time)
{
	usleep_range(time*1000,time*1000+time*10);
}

void asus_util_delay_us(uint32_t time)
{
	usleep_range(time,time+time/100);
}

int64_t asus_util_diff_time_us(struct timeval *t1, struct timeval *t2 )
{
	return (((t1->tv_sec*1000000)+t1->tv_usec)-((t2->tv_sec*1000000)+t2->tv_usec));
}

int asus_util_fs_read_text_uint8(char *filename, uint8_t *value)
{
	struct file *fp = NULL;
	loff_t pos_lsts = 0;
	ssize_t read_size = 0;
	mm_segment_t old_fs;
	char buf[5];

	memset(buf,0,sizeof(buf));

	/* open file */
	fp = filp_open(filename, O_RDONLY, S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_err("file (%s) not exist!\n",filename);
		return -ENOENT;	/*No such file or directory*/
	}

	pos_lsts = 0;

	old_fs = get_fs();
	set_fs(get_ds());
	read_size = vfs_read(fp, (void __user *)buf, sizeof(buf), &pos_lsts);
	set_fs(old_fs);

	if(read_size < 1) {
		pr_err("read failed!\n");
		/* close file */
		filp_close(fp, NULL);
		return -1;
	}
	else
	{
		pr_info("read size is %ld\n",read_size);
	}

	sscanf(buf, "%hhu", value);

	/* close file */
	filp_close(fp, NULL);

	return 0;
}

int asus_util_fs_read_text_uint32(char *filename, uint32_t *value)
{
	struct file *fp = NULL;
	loff_t pos_lsts = 0;
	char buf[8];
	ssize_t read_size = 0;
	mm_segment_t old_fs;

	/* open file */
	fp = filp_open(filename, O_RDONLY, S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_err("file (%s) not exist!\n",filename);
		return -ENOENT;	/*No such file or directory*/
	}

	pos_lsts = 0;

	old_fs = get_fs();
	set_fs(get_ds());
	memset(buf,0,sizeof(buf));
	read_size = vfs_read(fp, (void __user *)buf, sizeof(buf), &pos_lsts);
	set_fs(old_fs);

	if(read_size < 3) {
		pr_err("read failed!\n");
		/* close file */
		filp_close(fp, NULL);
		return -1;
	}
	else
	{
		pr_info("read size is %ld\n",read_size);
	}

	sscanf(buf, "%u", value);

	/* close file */
	filp_close(fp, NULL);

	return 0;
}

int asus_util_fs_write_byte_seq_text(char *filename, uint8_t *value, uint32_t size)
{
	struct file *fp = NULL;
	int i = 0;
	char buf[3];
	ssize_t ret;

	/* Open file */
	fp = filp_open(filename, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_err("open (%s) failed!\n",filename);
		return -1;
	}

	for(i = 0; i < size; i++){
		sprintf(buf, "%02x", value[i]);
		buf[2] = '\n';

		ret = __kernel_write(fp, buf, 3, &fp->f_pos);
		if(ret < 3)
		{
			pr_err("write failed!\n");
			/* Close file */
			filp_close(fp, NULL);
			return -2;
		}
	}

	/* Close file */
	filp_close(fp, NULL);
	return 0;
}

int asus_util_fs_read_text_word_seq_hex(char *filename, uint16_t *value, uint32_t size)
{
	int i = 0;
	struct file *fp = NULL;
	loff_t pos_lsts = 0;
	char buf[5];
	ssize_t buf_size = 0;
	mm_segment_t old_fs;
	/* open file */
	fp = filp_open(filename, O_RDONLY, S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_err("file (%s) not exist!\n",filename);
		return -ENOENT;	/*No such file or directory*/
	}

	pos_lsts = 0;
	for(i = 0; i < size; i++){
		old_fs = get_fs();
		set_fs(get_ds());
		buf_size = vfs_read(fp, (void __user *)buf, 5, &pos_lsts);
		set_fs(old_fs);

		if(buf_size < 5) {
			pr_err("read failed!\n");
			/* close file */
			filp_close(fp, NULL);
			return -1;
		}
		buf[4]='\0';
		sscanf(buf, "%hx", &value[i]);
	}

	/* close file */
	filp_close(fp, NULL);

	return 0;
}

int asus_util_fs_read_text_dword_seq_hex(char *filename, uint32_t *value, uint32_t size)
{
	int i = 0;
	struct file *fp = NULL;
	loff_t pos_lsts = 0;
	char buf[9];
	ssize_t buf_size = 0;
	mm_segment_t old_fs;
	/* open file */
	fp = filp_open(filename, O_RDONLY, S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_err("file (%s) not exist!\n",filename);
		return -ENOENT;	/*No such file or directory*/
	}

	pos_lsts = 0;
	for(i = 0; i < size; i++){
		old_fs = get_fs();
		set_fs(get_ds());
		buf_size = vfs_read(fp, (void __user *)buf, 9, &pos_lsts);
		set_fs(old_fs);

		if(buf_size < 9) {
			pr_err("read failed!\n");
			/* close file */
			filp_close(fp, NULL);
			return -1;
		}
		buf[8]='\0';
		sscanf(buf, "%x", &value[i]);
	}

	/* close file */
	filp_close(fp, NULL);

	return 0;
}

int asus_util_fs_read_text_byte_seq_hex(char *filename, uint8_t *value, uint32_t size)
{
	int i = 0;
	struct file *fp = NULL;
	loff_t pos_lsts = 0;
	char buf[3];
	ssize_t read_size = 0;
	mm_segment_t old_fs;

	/* open file */
	fp = filp_open(filename, O_RDONLY, S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_err("file (%s) not exist!\n",filename);
		return -ENOENT;	/*No such file or directory*/
	}

	pos_lsts = 0;
	for(i = 0; i < size; i++){
		old_fs = get_fs();
		set_fs(get_ds());
		read_size = vfs_read(fp, (void __user *)buf, 3, &pos_lsts);
		set_fs(old_fs);

		if(read_size < 3) {
			pr_err("read failed!\n");
			/* close file */
			filp_close(fp, NULL);
			return -1;
		}

		buf[2]='\0';

		sscanf(buf, "%02hhx", &value[i]);
	}

	/* close file */
	filp_close(fp, NULL);

	return 0;
}

int asus_util_fs_write_word_seq_text(char *filename, uint16_t *value, uint32_t size)
{
	struct file *fp = NULL;
	int i = 0;
	char buf[5];
	ssize_t ret;

	/* Open file */
	fp = filp_open(filename, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_err("open (%s) failed!\n",filename);
		return -1;
	}

	for(i = 0; i < size; i++){
		sprintf(buf, "%04x", value[i]);
		buf[4] = '\n';

		ret = __kernel_write(fp, buf, 5, &fp->f_pos);
		if(ret < 5)
		{
			pr_err("write failed!\n");
			/* Close file */
			filp_close(fp, NULL);
			return -2;
		}
	}

	/* close file */
	filp_close(fp, NULL);
	return 0;
}

int asus_util_fs_write_word_seq_text_change_line(char *filename, uint16_t *value, uint32_t size, uint32_t number)
{
	struct file *fp = NULL;
	int i = 0;
	char buf[5];
	ssize_t ret;

	/* Open file */
	fp = filp_open(filename, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_err("open (%s) failed!\n",filename);
		return -1;
	}

	for(i = 0; i < size; i++){
		sprintf(buf, "%04x", value[i]);
		if((i + 1) % number == 0)
			buf[4] = '\n';
		else
			buf[4] = ',';

		ret = __kernel_write(fp, buf, 5, &fp->f_pos);
		if(ret < 5)
		{
			pr_err("write failed!\n");
			/* Close file */
			filp_close(fp, NULL);
			return -2;
		}
	}

	/* close file */
	filp_close(fp, NULL);
	return 0;
}

int asus_util_fs_write_dword_seq_text_change_line(char *filename, uint32_t *value, uint32_t size, uint32_t number, bool full_format)
{
	struct file *fp = NULL;
	int i = 0;
	char buf[9];
	ssize_t ret;

	/* Open file */
	fp = filp_open(filename, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_err("open (%s) failed!\n",filename);
		return -1;
	}

	for(i = 0; i < size; i++){
		if(full_format)
		{
			sprintf(buf, "%08x", value[i]);
			if((i + 1) % number == 0)
				buf[8] = '\n';
			else
				buf[8] = ',';
			ret = __kernel_write(fp, buf, 9, &fp->f_pos);
			if(ret < 9)
			{
				pr_err("write failed!\n");
				/* Close file */
				filp_close(fp, NULL);
				return -2;
			}
		}
		else
		{
			sprintf(buf, "%04x", value[i]);
			if((i + 1) % number == 0)
				buf[4] = '\n';
			else
				buf[4] = ',';
			ret = __kernel_write(fp, buf, 5, &fp->f_pos);
			if(ret < 5)
			{
				pr_err("write failed!\n");
				/* Close file */
				filp_close(fp, NULL);
				return -2;
			}
		}
	}

	/* close file */
	filp_close(fp, NULL);
	return 0;
}

int32_t asus_util_fs_get_file_size(const char *filename, uint64_t* size)
{
    struct kstat stat;
    mm_segment_t fs;
    int rc = 0;

	stat.size = 0;

    fs = get_fs();
    set_fs(KERNEL_DS);

	rc = vfs_stat(filename,&stat);
	if(rc < 0)
	{
		pr_err("vfs_stat(%s) failed, rc = %d\n",filename,rc);
		rc = -1;
		goto END;
	}

    *size = stat.size;
END:
	set_fs(fs);
    return rc;
}

int asus_util_fs_read_file_into_buffer(const char *filename, uint8_t* data, uint32_t size)
{
    struct file *fp;
    mm_segment_t fs;
    loff_t pos;
	int rc;

    fp =filp_open(filename,O_RDONLY,S_IRWXU | S_IRWXG | S_IRWXO);
    if (IS_ERR(fp)){
		pr_err("open(%s) failed\n",filename);
        return -1;
    }

    fs =get_fs();
    set_fs(KERNEL_DS);

    pos =0;
	rc = vfs_read(fp,data, size, &pos);

    set_fs(fs);
    filp_close(fp,NULL);

    return rc;
}

int asus_util_format_hex_string(char *output_buf,int len, uint8_t* data,int count)
{
	int i;
	int offset;

	if(len<count*5+1+1)
		return -1;

	offset=0;

	for(i=0;i<count;i++)
	{
		offset+=sprintf(output_buf+offset,"0x%02x ",data[i]);
	}
	offset+=sprintf(output_buf+offset,"\n");

	return offset;
}

bool asus_util_i2c_setting_contain_address(struct cam_sensor_i2c_reg_setting * setting, uint32_t addr, uint32_t* data, uint16_t* index)
{
	int i;

	for(i=setting->size-1;i>=0;i--)
	{
		if(setting->reg_setting[i].reg_addr == addr)
		{
			*data = setting->reg_setting[i].reg_data;
			*index = i;
			return true;
		}
	}
	return false;
}


#define STRFORMAT(enum_name) case enum_name: return ""#enum_name"";
static const char *power_seq_type_to_string(enum msm_camera_power_seq_type type)
{
	switch(type)
	{
		STRFORMAT(SENSOR_MCLK)
		STRFORMAT(SENSOR_VANA)
		STRFORMAT(SENSOR_VDIG)
		STRFORMAT(SENSOR_VIO)
		STRFORMAT(SENSOR_VAF)
		STRFORMAT(SENSOR_VAF_PWDM)
		STRFORMAT(SENSOR_CUSTOM_REG1)
		STRFORMAT(SENSOR_CUSTOM_REG2)
		STRFORMAT(SENSOR_RESET)
		STRFORMAT(SENSOR_STANDBY)
		STRFORMAT(SENSOR_CUSTOM_GPIO1)
		STRFORMAT(SENSOR_CUSTOM_GPIO2)
		STRFORMAT(SENSOR_SEQ_TYPE_MAX)
		default:
		return "Unknown Seq Type";
	}
}
void asus_util_dump_cam_power_setting(struct cam_sensor_power_ctrl_t *ctrl)
{
	int i;
	struct cam_sensor_power_setting *power_setting;

	if(ctrl == NULL)
	{
		pr_err("cam_sensor_power_ctrl is NULL!\n");
		return;
	}

	if(ctrl->power_setting_size <=0)
	{
		pr_err("power_setting_size is 0!\n");
		return;
	}

	for(i = 0; i < ctrl->power_setting_size; i++)
	{
		power_setting = &ctrl->power_setting[i];
		if(power_setting == NULL)
		{
			pr_err("power setting[%d] is NULL!\n",i);
			continue;
		}
		pr_info("power setting[%d]: seq_type %d -> %s, seq_val %hu, config_val %ld\n",
				i,
				power_setting->seq_type,power_seq_type_to_string(power_setting->seq_type),
				power_setting->seq_val,
				power_setting->config_val
		);
	}

	for(i = 0; i < ctrl->power_down_setting_size; i++)
	{
		power_setting = &ctrl->power_down_setting[i];
		if(power_setting == NULL)
		{
			pr_err("power down setting[%d] is NULL!\n",i);
			continue;
		}
		pr_info("power down setting[%d]: seq_type %d -> %s, seq_val %hu, config_val %ld\n",
				i,
				power_setting->seq_type,power_seq_type_to_string(power_setting->seq_type),
				power_setting->seq_val,
				power_setting->config_val
		);
	}
}

const char * asus_util_cam_op_to_string(uint32_t op_code)
{
	switch(op_code)
	{
		STRFORMAT(CAM_QUERY_CAP)
		STRFORMAT(CAM_ACQUIRE_DEV)
		STRFORMAT(CAM_START_DEV)
		STRFORMAT(CAM_STOP_DEV)
		STRFORMAT(CAM_CONFIG_DEV)
		STRFORMAT(CAM_RELEASE_DEV)
		STRFORMAT(CAM_SD_SHUTDOWN)
		STRFORMAT(CAM_FLUSH_REQ)
		STRFORMAT(CAM_SENSOR_PROBE_CMD)
		default:
		    return "UNKNOWN OP CODE";
	}
}
