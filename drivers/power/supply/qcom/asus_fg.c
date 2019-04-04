#define pr_fmt(fmt)	"FG: %s: " fmt, __func__

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_batterydata.h>
#include <linux/platform_device.h>
#include <linux/iio/consumer.h>
#include <linux/qpnp/qpnp-revid.h>
#include <linux/proc_fs.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/extcon.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/rtc.h>
#include "fg-core.h"
#include "fg-reg.h"
#include "asus_fg.h"

static struct qpnp_adc_tm_chip *g_adc_tm_dev;
static struct qpnp_adc_tm_btm_param g_adc_param;
struct extcon_dev water_detect_dev;
struct extcon_dev battery_version_dev;
struct extcon_dev battery_id_dev;
static int g_wp_state, g_liquid_state, g_wmsg_state;
static int low_thr_det, high_thr_det_1, high_thr_det_2, liquid_high_bound, liquid_low_bound;
bool g_wp_enable = false;
char g_battery_version[40] = {0};
bool g_ready_to_report_1 = false;
int g_cyclecount_initialized = 0;
int init_backup_batinfo_ok = -1;

unsigned long g_last_batt_time[BAT_TIME_TYPE_COUNT] = {0};

extern bool g_boot_complete;
struct BATT_SAFETY_UPGRADE_PARAMS batt_safety_upgrade_params[BATT_SAFETY_UPGRADE_PARAMS_COUNT] = {
    [0] = {
        .float_voltage_uv = 4357500,
        .vbat_full_mv = 4330,
    },
    [1] = {
        .float_voltage_uv = 4300000,
        .vbat_full_mv = 4270,
    },
    [2] = {
        .float_voltage_uv = 4250000,
        .vbat_full_mv = 4220,
    },
};

struct CYCLE_COUNT_DATA g_cycle_count_data = {
    .magic = CYCLE_COUNT_DATA_MAGIC,
    .charge_cap_accum = {0},
    .charge_last_soc = {0},
    .bat_time_accum = {
        .item = {
            .battery_total_time = 0,
            .high_vol_total_time = 0,
            .high_temp_total_time = 0,
            .high_temp_vol_time = 0}},
    //~ .battery_total_time = 0,
    //~ .high_vol_total_time = 0,
    //~ .high_temp_total_time = 0,
    //~ .high_temp_vol_time = 0,
    .reload_condition = 0
};

//ASUS_BS battery health upgrade +++
#define	BATTERY_HEALTH_UPGRADE_TIME 1 //ASUS_BS battery health upgrade
#define	BATTERY_METADATA_UPGRADE_TIME 60 //ASUS_BS battery health upgrade
#define BAT_HEALTH_DATA_OFFSET 0
#define BAT_HEALTH_DATA_OFFSET1 2048
#define BAT_HEALTH_DATA_MAGIC  0x86
#define BAT_HEALTH_DATA_BACKUP_MAGIC 0x87
#define ZS620KL_DESIGNED_CAPACITY 3150 //mAh
#define BAT_HEALTH_DATA_SD_FILE_NAME   "/sdcard/.bh"
#define BAT_HEALTH_START_LEVEL 70
#define BAT_HEALTH_END_LEVEL 100
static bool g_bathealth_initialized = false;
static bool g_bathealth_trigger = false;
static bool g_last_bathealth_trigger = false;
static bool g_health_debug_enable = false;
static bool g_health_upgrade_enable = true;
static int g_health_upgrade_index = 0;
static int g_health_upgrade_start_level = BAT_HEALTH_START_LEVEL;
static int g_health_upgrade_end_level = BAT_HEALTH_END_LEVEL;
static int g_health_upgrade_upgrade_time = BATTERY_HEALTH_UPGRADE_TIME;
static int g_bat_health_avg;

int batt_health_csc_backup(void);

static struct BAT_HEALTH_DATA g_bat_health_data = {
    .magic = BAT_HEALTH_DATA_MAGIC,
    .bat_current = 0,
    .bat_current_avg = 0,
    .accumulate_time = 0,
    .accumulate_current = 0,
    .bat_health = 0,
    .start_time = 0,
    .end_time = 0
};

static struct BAT_HEALTH_DATA_BACKUP g_bat_health_data_backup[BAT_HEALTH_NUMBER_MAX] = {
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0},
	{"", 0}
};

struct delayed_work battery_health_work;
struct delayed_work battery_metadata_work;
struct wake_lock bat_health_lock;
//ASUS_BS battery health upgrade ---


struct delayed_work check_water_proof_work;
extern int fg_bp_params_config(struct fg_chip *chip);
extern void fg_notify_charger(struct fg_chip *chip);

static const unsigned int vbus_liquid_ext_supported_cable[] = {
	EXTCON_NONE,
};
static const unsigned int battery_version_ext_supported_cable[] = {
	EXTCON_NONE,
};
static const unsigned int battery_id_ext_supported_cable[] = {
	EXTCON_NONE,
};

//ASUS BSP +++
int record_magic[]={
	0xDCBA,
	0xABCE,
	0xABCF,
};

int record_pos[]={
	1024,
	1040,
	1056,
};
//ASUS BSP ---

int setup_vadc_monitor(struct fg_chip *chip);

/* enum qpnp_state_request:
 * 0 - ADC_TM_HIGH_THR_ENABLE/ADC_TM_COOL_THR_ENABLE
 * 1 - ADC_TM_LOW_THR_ENABLE/ADC_TM_WARM_THR_ENABLE
 * 2 - ADC_TM_HIGH_LOW_THR_ENABLE
 * 3 - ADC_TM_HIGH_THR_DISABLE/ADC_TM_COOL_THR_DISABLE
 * 4 - ADC_TM_LOW_THR_DISABLE/ADC_TM_WARM_THR_DISABLE
 * 5 - ADC_TM_HIGH_LOW_THR_DISABLE
 */
static ssize_t vadc_enable_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data){
	int val;
	char messages[8]="";
	int rc=0;

	len =(len > 8 ?8:len);
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);

	if(val == 1){
		g_adc_param.state_request = ADC_TM_HIGH_LOW_THR_ENABLE;// 2
		g_wp_enable = true;
	}else{
		g_adc_param.state_request = ADC_TM_HIGH_LOW_THR_DISABLE;// 5
		g_wp_enable = false;
		if (g_ready_to_report_1 || g_charger_mode)
			extcon_set_state_sync(&water_detect_dev, 0, 0);
	}

	/* Get the ADC device instance (one time) */
	g_adc_tm_dev = qpnp_get_adc_tm(g_fgChip->dev, "water-detection");
	if (IS_ERR(g_adc_tm_dev)) {
			rc = PTR_ERR(g_adc_tm_dev);
			BAT_DBG("%s qpnp_get_adc_tm fail(%d)\n", __func__, rc);
	}

	rc = qpnp_adc_tm_channel_measure(g_adc_tm_dev, &g_adc_param);
	if (rc){
		BAT_DBG_E("%s: qpnp_adc_tm_channel_measure fail(%d) ---\n", __func__,rc);
	}

	BAT_DBG("%s: enable vadc btm (%d)\n",__func__,g_adc_param.state_request);

	return len;
}


static int vadc_enable_proc_read(struct seq_file *buf, void *v)
{
	int result = g_adc_param.state_request;

	BAT_DBG("%s: %d\n", __func__, result);
	seq_printf(buf, "%d\n", result);
	return 0;
}
static int vadc_enable_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, vadc_enable_proc_read, NULL);
}

static void create_vadc_enable_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  vadc_enable_proc_open,
		.write = vadc_enable_proc_write,
		.read = seq_read,
		.llseek = seq_lseek,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/water_proof_enable", 0666, NULL, &proc_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}


static ssize_t vadc_high_thr_1_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data){
	int val;
	char messages[8]="";

	len =(len > 8 ?8:len);
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);

//	g_adc_param.high_thr = val;
	high_thr_det_1 = val;

	BAT_DBG("%s: set vadc high_thr_1 (%d)\n",__func__, high_thr_det_1);

	return len;
}

static int vadc_high_thr_1_proc_read(struct seq_file *buf, void *v)
{
//	int result = g_adc_param.high_thr;
	int result = high_thr_det_1;

	BAT_DBG("%s: %d\n", __func__, result);
	seq_printf(buf, "%d\n", result);
	return 0;
}
static int vadc_high_thr_1_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, vadc_high_thr_1_proc_read, NULL);
}

static void create_vadc_high_thr_1_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  vadc_high_thr_1_proc_open,
		.write = vadc_high_thr_1_proc_write,
		.read = seq_read,
		.llseek = seq_lseek,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/vadc_high_thr_1", 0666, NULL, &proc_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}


static ssize_t vadc_high_thr_2_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data){
	int val;
	char messages[8]="";

	len =(len > 8 ?8:len);
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);

//	g_adc_param.high_thr = val;
	high_thr_det_2 = val;

	BAT_DBG("%s: set vadc high_thr_2 (%d)\n",__func__, high_thr_det_2);

	return len;
}

static int vadc_high_thr_2_proc_read(struct seq_file *buf, void *v)
{
//	int result = g_adc_param.high_thr;
	int result = high_thr_det_2;

	BAT_DBG("%s: %d\n", __func__, result);
	seq_printf(buf, "%d\n", result);
	return 0;
}
static int vadc_high_thr_2_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, vadc_high_thr_2_proc_read, NULL);
}

static void create_vadc_high_thr_2_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  vadc_high_thr_2_proc_open,
		.write = vadc_high_thr_2_proc_write,
		.read = seq_read,
		.llseek = seq_lseek,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/vadc_high_thr_2", 0666, NULL, &proc_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}


static ssize_t vadc_low_thr_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data){
	int val;
	char messages[8]="";
	
	len =(len > 8 ?8:len);
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);

//	g_adc_param.low_thr = val;
	low_thr_det = val;

	BAT_DBG("%s: set vadc low_thr (%d)\n",__func__, low_thr_det);

	return len;
}

static int vadc_low_thr_proc_read(struct seq_file *buf, void *v)
{
//	int result = g_adc_param.low_thr;
	int result = low_thr_det;

	BAT_DBG("%s: %d\n", __func__, result);
	seq_printf(buf, "%d\n", result);
	return 0;
}
static int vadc_low_thr_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, vadc_low_thr_proc_read, NULL);
}

static void create_vadc_low_thr_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  vadc_low_thr_proc_open,
		.write = vadc_low_thr_proc_write,
		.read = seq_read,
		.llseek = seq_lseek,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/vadc_low_thr", 0666, NULL, &proc_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}


static ssize_t liquid_high_bound_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data){
	int val;
	char messages[8]="";

	len =(len > 8 ?8:len);
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);

	liquid_high_bound = val;

	BAT_DBG("%s: set liquid_high_bound (%d)\n",__func__, liquid_high_bound);

	return len;
}

static int liquid_high_bound_proc_read(struct seq_file *buf, void *v)
{
	int result = liquid_high_bound;

	BAT_DBG("%s: %d\n", __func__, result);
	seq_printf(buf, "%d\n", result);
	return 0;
}
static int liquid_high_bound_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, liquid_high_bound_proc_read, NULL);
}

static void create_liquid_high_bound_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  liquid_high_bound_proc_open,
		.write = liquid_high_bound_proc_write,
		.read = seq_read,
		.llseek = seq_lseek,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/liquid_high_bound", 0666, NULL, &proc_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}


static ssize_t liquid_low_bound_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data){
	int val;
	char messages[8]="";

	len =(len > 8 ?8:len);
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);

	liquid_low_bound = val;

	BAT_DBG("%s: set liquid_low_bound (%d)\n",__func__, liquid_low_bound);

	return len;
}

static int liquid_low_bound_proc_read(struct seq_file *buf, void *v)
{
	int result = liquid_low_bound;

	BAT_DBG("%s: %d\n", __func__, result);
	seq_printf(buf, "%d\n", result);
	return 0;
}
static int liquid_low_bound_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, liquid_low_bound_proc_read, NULL);
}

static void create_liquid_low_bound_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  liquid_low_bound_proc_open,
		.write = liquid_low_bound_proc_write,
		.read = seq_read,
		.llseek = seq_lseek,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/liquid_low_bound", 0666, NULL, &proc_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}


//ASUS_BSP LiJen config PMIC GPIO12 to ADC channel +++
static int water_detection_adc_proc_file_proc_read(struct seq_file *buf, void *v)
{
	int result = 0, err=0;
	struct qpnp_vadc_chip *vadc_dev;
	struct qpnp_vadc_result adc_result;
	int32_t adc;
	
	vadc_dev = qpnp_get_vadc(g_fgChip->dev, "water-detection");
	if (IS_ERR(vadc_dev)) {
		BAT_DBG("%s: qpnp_get_vadc failed\n", __func__);
		result = 0;
	}else{
		err = qpnp_vadc_read(vadc_dev, VADC_AMUX5_GPIO_PU3, &adc_result); //Read the GPIO12 VADC channel with 1:1 scaling
		adc = (int) adc_result.physical;
		//adc = adc / 1000; /* uV to mV */
		BAT_DBG("%s: adc=%d\n", __func__, adc);
		result = adc;
	}

	BAT_DBG("%s: %d\n", __func__, result);
	seq_printf(buf, "%d\n", result);
	return 0;
}
static int water_detection_adc_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, water_detection_adc_proc_file_proc_read, NULL);
}

static void create_water_detection_adc_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  water_detection_adc_proc_open,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/water_detection_adc", 0444, NULL, &proc_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}

static int wp_state_proc_file_proc_read(struct seq_file *buf, void *v)
{
	BAT_DBG("%s: %d\n", __func__, g_wp_state);
	seq_printf(buf, "%d\n", g_wp_state);
	return 0;
}
static int wp_state_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, wp_state_proc_file_proc_read, NULL);
}

static void create_wp_state_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  wp_state_proc_open,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/wp_state", 0444, NULL, &proc_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}

static int liquid_state_proc_file_proc_read(struct seq_file *buf, void *v)
{
	BAT_DBG("%s: %d\n", __func__, g_liquid_state);
	seq_printf(buf, "%d\n", g_liquid_state);
	return 0;
}
static int liquid_state_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, liquid_state_proc_file_proc_read, NULL);
}

static void create_liquid_state_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  liquid_state_proc_open,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/liquid_state", 0444, NULL, &proc_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}

static int wmsg_state_proc_file_proc_read(struct seq_file *buf, void *v)
{
	BAT_DBG("%s: %d\n", __func__, g_wmsg_state);
	seq_printf(buf, "%d\n", g_wmsg_state);
	return 0;
}
static int wmsg_state_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, wmsg_state_proc_file_proc_read, NULL);
}

static void create_wmsg_state_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  wmsg_state_proc_open,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/wmsg_state", 0444, NULL, &proc_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}
//ASUS_BSP LiJen config PMIC GPIO12 to ADC channel ---


//ASUS_BSP LiJen config PMIC GPIO12 VADC_BTM channel +++
static int32_t get_vadc_voltage(void){
	struct qpnp_vadc_chip *vadc_dev;
	struct qpnp_vadc_result adc_result;
	int32_t adc;

	vadc_dev = qpnp_get_vadc(g_fgChip->dev, "water-detection");
	if (IS_ERR(vadc_dev)) {
		BAT_DBG("%s: qpnp_get_vadc failed\n", __func__);
		return -1;
	}else{
		qpnp_vadc_read(vadc_dev, VADC_AMUX5_GPIO_PU3, &adc_result); //Read the GPIO12 VADC channel with 1:1 scaling
		adc = (int) adc_result.physical;
		//adc = adc / 1000; /* uV to mV */
		BAT_DBG("%s: adc=%d\n", __func__, adc);
	}

	return adc;
}

static void vadc_notification(enum qpnp_tm_state state, void *ctx)
{
	if (state == ADC_TM_HIGH_STATE) { //In High state
		pr_err("%s: ADC_TM_HIGH_STATE\n", __func__);
	}
	else { //In low state
		pr_err("%s: ADC_TM_LOW_STATE\n", __func__);
	}

	setup_vadc_monitor(ctx);
}

bool double_check_is_liquid_mode(int32_t adc)
{
	int32_t adc1=-1, adc2=-1, adc3=-1, adc4=-1, adc5=-1, adc6=-1, adc7=-1, adc8=-1, adc9=-1, adc10=-1;
	bool ret;

	adc1 = adc;
	msleep(300);
	adc2 = get_vadc_voltage();
	if(adc2 >= 200000 && adc2 <= 1300000){
		msleep(300);
		adc3 = get_vadc_voltage();
		if(adc3 >= 200000 && adc3 <= 1300000){
			msleep(300);
			adc4 = get_vadc_voltage();
			if(adc4 >= 200000 && adc4 <= 1300000){
				msleep(300);
				adc5 = get_vadc_voltage();
				if(adc5 >= 200000 && adc5 <= 1300000){
					msleep(300);
					adc6 = get_vadc_voltage();
					if(adc6 >= 200000 && adc6 <= 1300000){
						msleep(300);
						adc7 = get_vadc_voltage();
						if(adc7 >= 200000 && adc7 <= 1300000){
							msleep(300);
							adc8 = get_vadc_voltage();
							if(adc8 >= 200000 && adc8 <= 1300000){
								msleep(300);
								adc9 = get_vadc_voltage();
								if(adc9 >= 200000 && adc9 <= 1300000){
									msleep(300);
									adc10 = get_vadc_voltage();
									if(adc10 >= 200000 && adc10 <= 1300000){
										ret = true;
										BAT_DBG("%s: adc(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d) ret(%d)\n", __func__, adc1, adc2, adc3, adc4, adc5, adc6, adc7, adc8, adc9, adc10, ret);
										return ret;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	ret = false;
	BAT_DBG("%s: adc(%d, %d, %d, %d, %d, %d, %d, %d, %d, %d) ret(%d)\n", __func__, adc1, adc2, adc3, adc4, adc5, adc6, adc7, adc8, adc9, adc10, ret);
	return ret;
}

int setup_vadc_monitor(struct fg_chip *chip)
{
	int rc=0;
	int32_t adc;

restart:
	adc = get_vadc_voltage();

	/* Get the ADC device instance (one time) */
	g_adc_tm_dev = qpnp_get_adc_tm(chip->dev, "water-detection");
	if (IS_ERR(g_adc_tm_dev)) {
			rc = PTR_ERR(g_adc_tm_dev);
			BAT_DBG("%s qpnp_get_adc_tm fail(%d)\n", __func__, rc);
	}

	if(adc > liquid_high_bound){ //Normal
		g_adc_param.low_thr = low_thr_det; //uV
		g_adc_param.state_request = ADC_TM_LOW_THR_ENABLE;
		g_wp_state = 0;
		g_liquid_state = 0;
		g_wmsg_state = 0;
	}else if(adc < liquid_low_bound){ //Cable in
		g_adc_param.high_thr = high_thr_det_2; //uV
		g_adc_param.state_request = ADC_TM_HIGH_THR_ENABLE;
		g_wp_state = 2;
		g_liquid_state = 0;
		g_wmsg_state = 0;
	}else{	//With Liquid
		if (g_wp_state != 1) {
			if (double_check_is_liquid_mode(adc)) {
				g_adc_param.high_thr = high_thr_det_1; //uV
				g_adc_param.state_request = ADC_TM_HIGH_THR_ENABLE; 
				g_wp_state = 1;
				g_liquid_state = 1;
				g_wmsg_state = 1;
                ASUSErclog(ASUS_WATER_ALERT, "Water_alert is triggered");
			} else {
				goto restart;
			}
		} else {}
	}

    if (g_wp_enable) {
        // set state
        if (g_ready_to_report_1 || g_charger_mode)
			extcon_set_state_sync(&water_detect_dev, 0, g_wmsg_state);
    }

//	pr_err("%s low_thr(%d), high_thr(%d), state_request(%d)\n", __func__, g_adc_param.low_thr, g_adc_param.high_thr, g_adc_param.state_request);
	pr_err("%s adc(%d), g_wp_state(%d), low_thr(%d), high_thr(%d), state_request(%d)\n", __func__, adc, g_wp_state, g_adc_param.low_thr, g_adc_param.high_thr, g_adc_param.state_request);
    ASUSEvtlog("%s adc(%d), g_wp_state(%d), low_thr(%d), high_thr(%d), state_request(%d)\n", __func__, adc, g_wp_state, g_adc_param.low_thr, g_adc_param.high_thr, g_adc_param.state_request);
	g_adc_param.channel = 0x76;
	g_adc_param.timer_interval = ADC_MEAS2_INTERVAL_1S;
	g_adc_param.btm_ctx = chip;
	g_adc_param.threshold_notification = vadc_notification;
	rc = qpnp_adc_tm_channel_measure(g_adc_tm_dev, &g_adc_param);
	if (rc){
		BAT_DBG_E("%s: qpnp_adc_tm_channel_measure fail(%d) ---\n", __func__,rc);
	}

	return rc;
}
//ASUS_BSP LiJen config PMIC GPIO12 VADC_BTM channel ---

void asus_check_water_proof_work(struct work_struct *work)
{
	static bool first_time = true;
    if (g_boot_complete) {
		if (first_time) {
			printk("[BAT][CHG] water_proof: boot completed first time, g_wmsg_state status = %d, report UI 5s later\n", g_wmsg_state);
			first_time = false;
			schedule_delayed_work(&check_water_proof_work, msecs_to_jiffies(5000));
			return;
		}
		g_ready_to_report_1 = true;
        printk("[BAT][CHG] water_proof: boot completed, g_wmsg_state status = %d\n", g_wmsg_state);
		// report the event to UI when ims ready
		extcon_set_state_sync(&water_detect_dev, 0, g_wmsg_state);
    } else {
        schedule_delayed_work(&check_water_proof_work, msecs_to_jiffies(5000));
        printk("[BAT][CHG] water_proof: boot NOT completed yet, retry 5s\n");
    }
}

void asus_water_detection_init(struct fg_chip *chip)
{
	int rc=0;
	g_adc_param.low_thr = 1100000;//uv
	g_wp_state = 0;
	g_liquid_state = 0;
	g_wmsg_state = 0;
	low_thr_det = 1100000;//uv
	high_thr_det_1 = 1300000;//uv
	high_thr_det_2 = 300000;//uv
	liquid_high_bound = 1300000;//uv
	liquid_low_bound = 200000;//uv

	if(g_wp_enable){ //disable water detection default
		BAT_DBG("%s: enter\n", __func__);
		water_detect_dev.supported_cable = vbus_liquid_ext_supported_cable;
		water_detect_dev.name = "vbus_liquid";
		dev_set_name(&water_detect_dev.dev, "vbus_liquid");
		rc = extcon_dev_register(&water_detect_dev);
		if (rc) {
			pr_err("vbus liquid registration failed");
			return;
		}

		rc = setup_vadc_monitor(chip);
		if(rc){
			BAT_DBG_E("%s: setup_vadc_monitor fail(%d) ---\n", __func__,rc);
		} else {
	        if (g_charger_mode == 0 && g_wp_enable) {
	            INIT_DELAYED_WORK(&check_water_proof_work, asus_check_water_proof_work);
	            schedule_delayed_work(&check_water_proof_work, 0);
	        }
		}
	}
}

void asus_battery_version_init(void)
{
    int rc = 0;

    battery_version_dev.supported_cable = battery_version_ext_supported_cable;
    battery_version_dev.name = "C11Pxxxx";
    dev_set_name(&battery_version_dev.dev, "battery");
    rc = extcon_dev_register(&battery_version_dev);
    if (rc) {
        pr_err("battery version registration failed");
    }
}

void asus_set_battery_version(void)
{
    char Battery_Model_Name[10] = "C11Pxxxx";
    char Cell_Supplier_Code = 'X';
    char battery_id[3] = "00";
    char profile_version[5] = "0001";
    char sw_driver_version[15] = "90.04.101.150";

    if(g_fgChip->batt_id_ohms <= 56100 && g_fgChip->batt_id_ohms >= 45900) {
        Cell_Supplier_Code = 'O';
        strncpy(battery_id, "01", sizeof(battery_id));
    }
    else {
        pr_info("Unknow battery \n");
    }
    strncpy(Battery_Model_Name, "C11P1708", sizeof(Battery_Model_Name));
    strncpy( profile_version, "0002", sizeof(profile_version));

    memset(g_battery_version, 0, sizeof(g_battery_version));
    snprintf(g_battery_version, sizeof(g_battery_version), "%s-%c-%s-%s-%s", Battery_Model_Name, Cell_Supplier_Code, battery_id, profile_version, sw_driver_version);
    printk("battery_version = %s\n", g_battery_version);
    battery_version_dev.name = (const char *)g_battery_version;
}

void asus_battery_id_init(void)
{
    int rc = 0;

    battery_id_dev.supported_cable = battery_id_ext_supported_cable;
    battery_id_dev.name = "battery_id";
    dev_set_name(&battery_id_dev.dev, "battery_id");
    rc = extcon_dev_register(&battery_id_dev);
    if (rc) {
        pr_err("battery id registration failed");
    }
}

void asus_check_batt_id(struct fg_chip *chip)
{
    bool in_range = (chip->batt_id_ohms <= 56100 && chip->batt_id_ohms >= 45900);

    pr_info("%s: batt_id_ohms = %d, in_range = %d\n", __func__, chip->batt_id_ohms, in_range);
    extcon_set_state_sync(&battery_id_dev, 0, in_range);
}

#ifdef ASUS_FACTORY_BUILD
#define gaugeIC_status_PROC_FILE "driver/gaugeIC_status"

static int gaugeIC_status_proc_read(struct seq_file *buf, void *v)
{
	u8 value;
	int rc = -1;

	if(!g_fgChip || !g_fgChip->mem_if_base){
		pr_err("g_fgChip is NULL or addr is ZERO !\n");
		seq_printf(buf, "0\n");
		return rc;
	}

    rc = fg_read(g_fgChip, MEM_IF_INT_RT_STS(g_fgChip), &value, 1);
	if (rc) {
		seq_printf(buf, "0\n");
		pr_err("proc read INT_RT_STS vaule failed!\n");
		return rc;
	}

	seq_printf(buf, "1\n");
	return 0;
}

static int gaugeIC_status_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, gaugeIC_status_proc_read, NULL);
}

static const struct file_operations gaugeIC_status_fops = {
	.owner = THIS_MODULE,
	.open = gaugeIC_status_proc_open,
	.read = seq_read,
};

static void create_asus_ATD_gaugeIC_status_interface(void)
{
	struct proc_dir_entry *gaugeIC_status_proc_file = proc_create(gaugeIC_status_PROC_FILE, 0444, NULL, &gaugeIC_status_fops);

	if (gaugeIC_status_proc_file) {
		printk("[FG][Proc]gaugeIC_status create ok!\n");
	} else{
		printk("[FG][Proc]gaugeIC_status create failed!\n");
	}
}

#define ATD_battery_current_PROC_FILE "driver/battery_current"

static int ATD_battery_current_proc_read(struct seq_file *buf, void *data)
{
    int rc;
    union power_supply_propval current_now;

    if(!g_fgChip){
        pr_err("fgchip is NULL!\n");
        return -1;
    }

    rc = power_supply_get_property(g_fgChip->fg_psy, POWER_SUPPLY_PROP_CURRENT_NOW, &current_now);
    if (rc) {
        pr_err("cannot read batt current\n");
        return -1;
    }

    seq_printf(buf, "%d\n", (current_now.intval /1000 * -1));

    return 0;
}
static int ATD_battery_current_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, ATD_battery_current_proc_read, NULL);
}

static const struct file_operations ATD_battery_current_fops = {
	.owner = THIS_MODULE,
	.open = ATD_battery_current_proc_open,
	.read = seq_read,
};
void create_asus_ATD_battery_current_interface(void)
{
	struct proc_dir_entry *ATD_battery_current = proc_create(ATD_battery_current_PROC_FILE, 0444, NULL, &ATD_battery_current_fops);
	if(!ATD_battery_current)
		pr_err("creat ATD_battery_current proc inode failed!\n");
}

#define ATD_battery_voltage_PROC_FILE "driver/battery_voltage"
static int ATD_battery_voltage_proc_read(struct seq_file *buf, void *data)
{
    int rc;
    union power_supply_propval voltage_now;

    if(!g_fgChip){
        pr_err("fgchip is NULL!\n");
        return -1;
    }

    rc = power_supply_get_property(g_fgChip->fg_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &voltage_now);
    if (rc) {
        pr_err("cannot read batt voltage\n");
        return -1;
    }

    seq_printf(buf, "%d\n", (voltage_now.intval /1000));

	return 0;
}

static int ATD_battery_voltage_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, ATD_battery_voltage_proc_read, NULL);
}

static const struct file_operations ATD_battery_voltage_fops = {
	.owner = THIS_MODULE,
	.open = ATD_battery_voltage_proc_open,
	.read = seq_read,
};

void create_asus_ATD_battery_voltage_interface(void)
{
	struct proc_dir_entry *ATD_battery_voltage = proc_create(ATD_battery_voltage_PROC_FILE, 0444, NULL, &ATD_battery_voltage_fops);
	if(!ATD_battery_voltage)
		pr_err("creat ATD_battery_voltage proc inode failed!\n");
}

#endif

static ssize_t thermal_test_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data){
	int val;
	char messages[8]="";

	len =(len > 8 ?8:len);
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);

	if(val < -200 ||val > 700)
		fake_temp = FAKE_TEMP_INIT;
	else
		fake_temp = val;

	BAT_DBG("%s: set fake temperature as %d\n",__func__,fake_temp);

	return len;
}

static int thermal_proc_read(struct seq_file *buf, void *v)
{
	int result = fake_temp;

	BAT_DBG("%s: %d\n", __func__, result);
	seq_printf(buf, "%d\n", result);
	return 0;
}
static int thermal_test_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, thermal_proc_read, NULL);
}

static const struct file_operations thermal_test_temp_fops = {
	.owner = THIS_MODULE,
	.open =  thermal_test_proc_open,
	.write = thermal_test_proc_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void create_thermal_test_proc_file(void)
{

	struct proc_dir_entry *proc_file = proc_create("driver/ThermalTemp", 0666, NULL, &thermal_test_temp_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}

static int g_full_cycle_thresh = CYCLE_FULL_THRESH_DEFAULT;
static int g_bootup_soc[3];

static int file_op(const char *filename, loff_t offset, char *buf, int length, int operation)
{
	int filep = 0;
	mm_segment_t old_fs;
	int rc = 0;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	if(FILE_OP_READ == operation)
		filep= sys_open(filename, O_RDONLY|O_CREAT, 0666);
	else if(FILE_OP_WRITE == operation)
		filep= sys_open(filename, O_RDWR|O_CREAT, 0666);
	else {
		pr_err("Unknown partition op err!\n");
		rc = -1;
		goto INVALID_PARAM;
	}
	if(filep < 0) {
		pr_err("open %s err! error code:%d\n", filename, filep);
		rc = -2;
		goto OUT;
	}
    else
        pr_info("open %s success!\n", filename);

	sys_lseek(filep, offset, SEEK_SET);
	if(FILE_OP_READ == operation)
		sys_read(filep, buf, length);
	else if(FILE_OP_WRITE == operation) {
		sys_write(filep, buf, length);
		sys_fsync(filep);
	}
	rc = length;
OUT:
	sys_close(filep);
INVALID_PARAM:
	set_fs(old_fs);

	return rc;
}

#define BAT_CONDITON_FILE_NAME "/asdf/Batpercentage"
int notify_battery_condition(int condition)
{
    struct file *fp = NULL;
    mm_segment_t old_fs;
    loff_t pos_lsts = 0;
    char buf[5] = {0};
    int value;
    int rc;

    if (condition < 0 || condition > 2) {
        pr_err("%s: condition(%d) not valid\n", __func__, condition);
        return -1;
    }

    switch (condition) {
    case 0 :
        value = 100;
        break;
    case 1 :
        value = 95;
        break;
    case 2 :
        value = 90;
        break;
    default:
        break;    
    }
    BAT_DBG("%s: now_condition:%d, value=%d\n", __func__, condition, value);
    snprintf(buf, sizeof(buf), "%d\n", value);

    fp = filp_open(BAT_CONDITON_FILE_NAME, O_RDWR|O_CREAT, 0777);
    if (IS_ERR_OR_NULL(fp)) {
        pr_err("%s: open (%s) fail(%ld)\n", __func__, BAT_CONDITON_FILE_NAME, PTR_ERR(fp));
        return -1;
    }

    /*For purpose that can use read/write system call*/
    old_fs = get_fs();
    set_fs(KERNEL_DS);

    rc = vfs_write(fp, (uint8_t *)buf, sizeof(buf), &pos_lsts);
    BAT_DBG("write value is %s\n", buf);
    if (rc < 0) {
        pr_err("%s: write to (%s) fail(%d)", __func__, BAT_CONDITON_FILE_NAME, rc);
        set_fs(old_fs);
        filp_close(fp, NULL);
        return rc;
    }

    set_fs(old_fs);
    filp_close(fp, NULL);

    return 0;   
}
static int write_back_cycle_count_data(void);
int asus_batt_cycle_count_init(void)
{
	int i, rc = 0;
	struct CYCLE_COUNT_DATA buf;

	/* Read cycle count data from emmc */
	rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
		(char*)&buf, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_READ);
	if(rc < 0) {
		pr_err("Read cycle count file failed!\n");
		return rc;
	}

	/* Check data validation */
	if(buf.magic != CYCLE_COUNT_DATA_MAGIC) {
		pr_err("Cycle count data not exist or invalid, use current data\n");
		g_cyclecount_initialized = 1;
		write_back_cycle_count_data();
		return -1;
	}
	else {
		/* Update current value */
		pr_info("Update current value!\n");
		for(i =0; i < 3; i++) {
			g_cycle_count_data.charge_cap_accum[i] += buf.charge_cap_accum[i];
			/* Accumulate SOC which increased not in kernel */
			if(buf.charge_last_soc[i] < g_bootup_soc[i])
				g_cycle_count_data.charge_cap_accum[i] += (g_bootup_soc[i] - buf.charge_last_soc[i]);
		}

		g_cycle_count_data.bat_time_accum.item.high_temp_total_time = buf.bat_time_accum.item.high_temp_total_time;
		g_cycle_count_data.bat_time_accum.item.high_temp_vol_time = buf.bat_time_accum.item.high_temp_vol_time;
		g_cycle_count_data.bat_time_accum.item.high_vol_total_time = buf.bat_time_accum.item.high_vol_total_time;
		g_cycle_count_data.reload_condition = buf.reload_condition;
		g_cycle_count_data.bat_time_accum.item.battery_total_time = buf.bat_time_accum.item.battery_total_time;
		pr_info("reload_condition=%d;high_temp_total_time=%lu;high_temp_vol_time=%lu;high_vol_total_time=%lu;battery_total_time=%lu\n",
			buf.reload_condition, buf.bat_time_accum.item.high_temp_total_time,buf.bat_time_accum.item.high_temp_vol_time,
			buf.bat_time_accum.item.high_vol_total_time,buf.bat_time_accum.item.battery_total_time);
	}
	pr_info("Cycle count data initialize success!\n");
	g_cyclecount_initialized = 1;
	return 0;
}

int check_sdcard_file_ok(void)
{
	int rc;

	rc = (int)sys_access("/sdcard", 0);
	if(rc < 0){
		pr_err("data partion is not ok!\n");
	}
	return rc;
}

int asus_backup_batinfo_to_sdcard(void)
{
	char *buf = NULL;
	int rc;

	buf = kmalloc(BACKUP_BATINFO_SIZE, GFP_KERNEL);
	if(buf == NULL){
		pr_err("%s:kmalloc buf size fail!\n", __func__);
		return -1;
	}

	rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
		buf, BACKUP_BATINFO_SIZE, FILE_OP_READ);
	if(rc < 0){
		pr_err("%s:read %s failed!\n", __func__, CYCLE_COUNT_FILE_NAME);
		goto ERROR;
	}

	rc = file_op(BACKUP_BATINFO_NAME, CYCLE_COUNT_DATA_OFFSET,
		buf, BACKUP_BATINFO_SIZE, FILE_OP_WRITE);
	if(rc < 0){
		pr_err("%s:write %s failed!\n", __func__, BACKUP_BATINFO_NAME);
		goto ERROR;
	}

ERROR:
	kfree(buf);
	buf = NULL;
	return rc;
}

static int write_back_cycle_count_data(void)
{
	int rc = 0;

	pr_info("%s:write back charge_cap_accum --> %d, %d, %d\n",__FUNCTION__, g_cycle_count_data.charge_cap_accum[0],
		g_cycle_count_data.charge_cap_accum[1], g_cycle_count_data.charge_cap_accum[2]);
	pr_info("%s:write back charge_last_soc --> %d, %d, %d\n",__FUNCTION__, g_cycle_count_data.charge_last_soc[0],
		g_cycle_count_data.charge_last_soc[1], g_cycle_count_data.charge_last_soc[2]);
	rc = file_op(CYCLE_COUNT_FILE_NAME, CYCLE_COUNT_DATA_OFFSET,
		(char *)&g_cycle_count_data, sizeof(struct CYCLE_COUNT_DATA), FILE_OP_WRITE);
	if(rc<0)
		pr_err("%s:Write file:%s err!\n", __FUNCTION__, CYCLE_COUNT_FILE_NAME);

	init_backup_batinfo_ok = asus_backup_batinfo_to_sdcard();

    // Notify ims battery condition
    notify_battery_condition(g_cycle_count_data.reload_condition);
    return rc;
}

int asus_update_cycle_count(struct fg_chip *chip)
{
	static int last_soc[3] = {-1, -1, -1};
	int cur_soc[3] = {-1, -1, -1};
	static int charge_cap_accum[3];
	int rc =0, wb_flag=0, ft_flag=0,i, cap_delta;
	u8 reg[4];

	/* Read current Soc */
	/* Battery Soc */
	rc = fg_sram_read(chip, CYCLE_BATT_SOC_REG, CYCLE_BATT_SOC_OFFSET,
			reg, 4, FG_IMA_DEFAULT);
	if (rc) {
		pr_err("Failed to read battery soc rc: %d\n", rc);
		return -1;
	}
	cur_soc[0] = reg[3];
	/* System Soc */
	rc = fg_sram_read(chip, CYCLE_SYSTEM_SOC_REG, CYCLE_SYSTEM_SOC_OFFSET,
			reg, 2, FG_IMA_DEFAULT);
	if (rc) {
		pr_err("Failed to read system soc rc: %d\n", rc);
		return -1;
	}
	cur_soc[1] = reg[1];
	/* Monotonic Soc */
	rc = fg_sram_read(chip, CYCLE_MONO_SOC_REG, CYCLE_MONO_SOC_OFFSET,
			reg, 2, FG_IMA_DEFAULT);
	if (rc) {
		pr_err("Failed to read monotonic soc rc: %d\n", rc);
		return -1;
	}
	cur_soc[2] = reg[1];

	/* Try one more time if previous data not loaded */
	if(!g_cyclecount_initialized) {
		asus_batt_cycle_count_init();/* Try one more time */
	}

	/* Store current soc */
	g_cycle_count_data.charge_last_soc[0] = cur_soc[0];
	g_cycle_count_data.charge_last_soc[1] = cur_soc[1];
	g_cycle_count_data.charge_last_soc[2] = cur_soc[2];
	/* first time use cur_soc as last_soc */
	for(i = 0; i<3; i++) {
		if(last_soc[i]<0 || last_soc[i] > 255) {
			ft_flag = 1;
			last_soc[i] = cur_soc[i];
			g_bootup_soc[i] = cur_soc[i];
		}
	}
	if(ft_flag) {
		pr_err("last_soc invalid!\n");
		return 0;
	}

	for(i = 0; i<3; i++) {
		cap_delta = cur_soc[i] - last_soc[i];
		/* Only accumulate increased charge capacity */
		if(cap_delta > 0) {
			g_cycle_count_data.charge_cap_accum[i] += cap_delta;
			charge_cap_accum[i] += cap_delta;
			if(charge_cap_accum[i] >= CYCLE_COUNT_WRITEBACK_THRESH) {
				pr_info("%s:charge_cap_accum[%d]=%d >= %d, begin write back\n",__FUNCTION__, i,
					charge_cap_accum[i], CYCLE_COUNT_WRITEBACK_THRESH);
				wb_flag = 1;
				charge_cap_accum[i] = 0;
			}
		}
	}

	if(wb_flag) {/* Write data back to emmc when  accum_soc exceed write back threshold*/
		write_back_cycle_count_data();
	}
    /* Update last_soc */
	for(i = 0; i<3; i++) {
		last_soc[i] = cur_soc[i];
	}
	return 0;
}

static void asus_update_fv_vf(struct fg_chip *chip, int float_volt_uv, int vbatt_full_mv)
{
    chip->bp.float_volt_uv = float_volt_uv;
    chip->bp.vbatt_full_mv = vbatt_full_mv;
    // Update FG SRAM data
    fg_bp_params_config(chip);
    // Notify charger FV change
    fg_notify_charger(chip);
    // Save data to disk
    write_back_cycle_count_data();
}

static void calculation_battery_time(unsigned long time)
{
	static unsigned long last_time = 0;
	unsigned long count_time = 0;

	if(last_time > 0){
		count_time = ((time-last_time)>0) ? (time-last_time):0;
        g_cycle_count_data.bat_time_accum.item.battery_total_time += count_time;
		last_time = time;
		return ;
	}

#if 0 // only accumulated device up time now
	if(0 == g_cycle_count_data.bat_time_accum.item.battery_total_time)
		goto ERASE; //skip for erase fsc_old partion can not read rtc time

	if(g_cycle_count_data.bat_time_accum.item.battery_total_time <= time){
        g_cycle_count_data.bat_time_accum.item.battery_total_time = time;
	}else{//add up time when pull out the battery
		g_cycle_count_data.bat_time_accum.item.battery_total_time += time;
	}
ERASE:
#endif
	pr_info("time=%lu;battery_total_time=%lu\n", time,g_cycle_count_data.bat_time_accum.item.battery_total_time);
	last_time = time;
}

/*
 * type:
 * BAT_TIME_TYPE_HIGH_VOL - 0
 * BAT_TIME_TYPE_HIGH_TEMP - 1
 * BAT_TIME_TYPE_HIGH_TEMP_VOL - 2
*/
static void calculation_time_fun(int type)
{
    unsigned long now_time;
    unsigned long temp_time = 0;
    int rc = 0;

    if (type < 0 || type > BAT_TIME_TYPE_COUNT -1) {
        pr_err("invalid batt time type(%d)\n", type);
        return ;
    }

    rc = asus_qpnp_rtc_read_time(&now_time);
    if (rc) {
        pr_err("read rtc time fail\n");
        return ;
    }

    if (0 == g_last_batt_time[type]) {
        g_last_batt_time[type] = now_time;
        pr_info("now_time=%lu;bat_time_type[%d]=%lu\n", now_time, type,
                    g_cycle_count_data.bat_time_accum.total_time[type + BAT_TIME_TYPE_OFFSET]);
    } else {
        temp_time = now_time - g_last_batt_time[type];
        if(temp_time > 0)
            g_cycle_count_data.bat_time_accum.total_time[type + BAT_TIME_TYPE_OFFSET] += temp_time;
        g_last_batt_time[type] = now_time;
    }
}

extern int asus_clear_reload_battery_profile(struct fg_chip *chip);
void asus_batt_safety_upgrade_work(struct work_struct *work)
{
    struct fg_chip *chip = container_of(work,
            struct fg_chip, safety_upgrade_work.work);
    union power_supply_propval prop = {0, };
    int rc;
    int pre_condition, new_condition;
    int capacity, temperature;
    unsigned long now_time = 0;
    static bool first_time = true;
    static int count = 0;

    // 1. Calculate time conditions
    rc = asus_qpnp_rtc_read_time(&now_time);
    if (rc) {
        pr_err("read rtc time fail\n");
        goto out;
    }

    if (!chip->batt_psy) {
        pr_err("batt_psy not initialized yet, try again later\n");
        goto out;
    }

    if (!g_cyclecount_initialized) {
        rc = asus_batt_cycle_count_init();
        if(rc < 0){
            pr_err("init batt cycle count failed!\n");
            goto out;
        }
    }

    rc = power_supply_get_property(chip->batt_psy, POWER_SUPPLY_PROP_CAPACITY, &prop);
    if (rc < 0) {
        pr_err("Error in getting battery capacity, rc=%d\n", rc);
        goto out;
    }
    capacity = prop.intval;
    calculation_battery_time(now_time);

    rc = power_supply_get_property(chip->batt_psy, POWER_SUPPLY_PROP_TEMP, &prop);
    if (rc < 0) {
        pr_err("Error in getting battery temperature, rc=%d\n", rc);
        goto out;
    }
    temperature = prop.intval;

//    BAT_DBG("%s: rtc time now: %lu secs, cap: %d, temp: %d\n", __func__, now_time, capacity, temperature);

    if (capacity == FULL_CAPACITY_VALUE) {
        calculation_time_fun(BAT_TIME_TYPE_HIGH_VOL);	
    } else {
        g_last_batt_time[BAT_TIME_TYPE_HIGH_VOL] = 0; //exit high vol
    }

    if (temperature >= HIGHER_TEMP) {
        calculation_time_fun(BAT_TIME_TYPE_HIGH_TEMP);
    } else {
        g_last_batt_time[BAT_TIME_TYPE_HIGH_TEMP] = 0; //exit high temp
    }

    if (temperature >= HIGH_TEMP && capacity == FULL_CAPACITY_VALUE) {
        calculation_time_fun(BAT_TIME_TYPE_HIGH_TEMP_VOL);
    } else {
        g_last_batt_time[BAT_TIME_TYPE_HIGH_TEMP_VOL] = 0; //exit high temp and vol
    }

    if (count++ > BAT_TIME_WRITEBACK_THRESH) {
        count = 0;
        write_back_cycle_count_data();
    }

    // 2. Judge lower FV/VF conditions
    if (first_time) {
        BAT_DBG("%s: first time initialize, current condition: %d\n", __func__, g_cycle_count_data.reload_condition);
        asus_update_fv_vf(chip,
                                        batt_safety_upgrade_params[g_cycle_count_data.reload_condition].float_voltage_uv,
                                        batt_safety_upgrade_params[g_cycle_count_data.reload_condition].vbat_full_mv);
        first_time = false;
        goto out;
    }

    rc = power_supply_get_property(chip->batt_psy, POWER_SUPPLY_PROP_STATUS_QCOM, &prop);
    if (rc < 0) {
        pr_err("Error in getting charging status, rc=%d\n", rc);
        goto out;
    }

    if (prop.intval != POWER_SUPPLY_STATUS_FULL) {
        goto out;
    }

    pre_condition = g_cycle_count_data.reload_condition;
    //~ if (pre_condition >= (BATT_SAFETY_UPGRADE_PARAMS_COUNT -1)) {
        //~ goto out;
    //~ }

    //a.judge battery using total time
    if (g_cycle_count_data.bat_time_accum.item.battery_total_time >= chip->safety_upgrade_cond.condition2_battery_time) {
        g_cycle_count_data.reload_condition = 2;
        goto done;
    } else if (g_cycle_count_data.bat_time_accum.item.battery_total_time >= chip->safety_upgrade_cond.condition1_battery_time &&
        g_cycle_count_data.bat_time_accum.item.battery_total_time < chip->safety_upgrade_cond.condition2_battery_time) {
        g_cycle_count_data.reload_condition = 1;
    }

    //b. judge battery cycle count(not used for now)

    //c. judge high temp and voltage condition(not used for now)
    if (0) {
    if (g_cycle_count_data.bat_time_accum.item.high_temp_vol_time >= chip->safety_upgrade_cond.condition2_temp_vol_time) {
        g_cycle_count_data.reload_condition = 2;
        goto done;
    } else if (g_cycle_count_data.bat_time_accum.item.high_temp_vol_time >= chip->safety_upgrade_cond.condition1_temp_vol_time &&
        g_cycle_count_data.bat_time_accum.item.high_temp_vol_time < chip->safety_upgrade_cond.condition2_temp_vol_time) {
        g_cycle_count_data.reload_condition = 1;
    }
    }

    //d. judge high temp condition
    if (g_cycle_count_data.bat_time_accum.item.high_temp_total_time >= chip->safety_upgrade_cond.condition2_temp_time){
        g_cycle_count_data.reload_condition = 2;
        goto done;
    } else if (g_cycle_count_data.bat_time_accum.item.high_temp_total_time >= chip->safety_upgrade_cond.condition1_temp_time &&
        g_cycle_count_data.bat_time_accum.item.high_temp_total_time < chip->safety_upgrade_cond.condition2_temp_time){
        g_cycle_count_data.reload_condition = 1;
    }

    //e. judge high voltage condition
    if (g_cycle_count_data.bat_time_accum.item.high_vol_total_time >= chip->safety_upgrade_cond.condition2_vol_time){
        g_cycle_count_data.reload_condition = 2;
    } else if (g_cycle_count_data.bat_time_accum.item.high_vol_total_time >= chip->safety_upgrade_cond.condition1_vol_time &&
        g_cycle_count_data.bat_time_accum.item.high_vol_total_time < chip->safety_upgrade_cond.condition2_vol_time){
        g_cycle_count_data.reload_condition = 1;
    }

    // 3. Lower Float Voltage/Vbatt Full if necessary
done:
    if (g_cycle_count_data.reload_condition != pre_condition) {
        new_condition = g_cycle_count_data.reload_condition;
        if (new_condition >= 0 && new_condition <= (BATT_SAFETY_UPGRADE_PARAMS_COUNT - 1)) {
            BAT_DBG("%s total used time:%lu, high vol time:%lu, high temp time:%lu, hight temp vol time:%lu\n",
                __func__, g_cycle_count_data.bat_time_accum.item.battery_total_time,
                g_cycle_count_data.bat_time_accum.item.high_vol_total_time,
                g_cycle_count_data.bat_time_accum.item.high_temp_total_time,
                g_cycle_count_data.bat_time_accum.item.high_temp_vol_time);
            BAT_DBG("%s: condition change(%d -> %d), fv/vf need to update, pre(%d, %d), now(%d, %d)\n",
                __func__, pre_condition, new_condition,
                chip->bp.float_volt_uv / 1000, chip->bp.vbatt_full_mv,
                batt_safety_upgrade_params[new_condition].float_voltage_uv / 1000,
                batt_safety_upgrade_params[new_condition].vbat_full_mv);
            asus_update_fv_vf(chip, batt_safety_upgrade_params[new_condition].float_voltage_uv,
                                                batt_safety_upgrade_params[new_condition].vbat_full_mv);
            if (2 == g_cycle_count_data.reload_condition) {
                BAT_DBG("%s: change to condition 2, reload according profile\n", __func__);
                ASUSEvtlog("[BAT]change to condition 2, reload according profile\n");
                asus_clear_reload_battery_profile(chip);
            }
        }
    }

out:
	if(g_cyclecount_initialized && (init_backup_batinfo_ok < 0) ){
		rc = check_sdcard_file_ok();
		if(rc < 0){
			goto WORK;
		}
		init_backup_batinfo_ok = asus_backup_batinfo_to_sdcard();
	}

WORK:
    if (g_cyclecount_initialized) {
        // Polling every 1 minute when condition data initialized
        schedule_delayed_work(&chip->safety_upgrade_work, msecs_to_jiffies(60000));
    } else {
        schedule_delayed_work(&chip->safety_upgrade_work, msecs_to_jiffies(2000));
    }
}

static void asus_battery_safety_init(struct fg_chip *chip)
{
    BAT_DBG("%s: enter\n", __func__);
    chip->safety_upgrade_cond.condition1_battery_time = BATTERY_USE_TIME_CONDITION1;
    chip->safety_upgrade_cond.condition2_battery_time = BATTERY_USE_TIME_CONDITION2;
    chip->safety_upgrade_cond.condition1_cycle_count = CYCLE_COUNT_CONDITION1;
    chip->safety_upgrade_cond.condition2_cycle_count = CYCLE_COUNT_CONDITION2;
    chip->safety_upgrade_cond.condition1_temp_vol_time = HIGH_TEMP_VOL_TIME_CONDITION1;
    chip->safety_upgrade_cond.condition2_temp_vol_time = HIGH_TEMP_VOL_TIME_CONDITION2;
    chip->safety_upgrade_cond.condition1_temp_time = HIGH_TEMP_TIME_CONDITION1;
    chip->safety_upgrade_cond.condition2_temp_time = HIGH_TEMP_TIME_CONDITION2;
    chip->safety_upgrade_cond.condition1_vol_time = HIGH_VOL_TIME_CONDITION1;
    chip->safety_upgrade_cond.condition2_vol_time = HIGH_VOL_TIME_CONDITION2;

    INIT_DELAYED_WORK(&chip->safety_upgrade_work, asus_batt_safety_upgrade_work);
    schedule_delayed_work(&chip->safety_upgrade_work, 0);
}

static int full_cycle_thresh_proc_show(struct seq_file *buf, void *data)
{
	seq_printf(buf, "%d\n", g_full_cycle_thresh);
	printk("full cycle thresh: current = %d\n", g_full_cycle_thresh);
	return 0;
}
static int full_cycle_thresh_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, full_cycle_thresh_proc_show, NULL);
}

static ssize_t full_cycle_thresh_proc_write(struct file *file,const char __user *buffer,size_t count,loff_t *pos)
{
	int old_thresh, new_thresh;

	old_thresh = g_full_cycle_thresh;
	sscanf(buffer,"%d", &new_thresh);
	if(new_thresh <=0 || new_thresh > 100)
		return count;
	g_full_cycle_thresh = new_thresh;
	printk("full cycle thresh: old = %d --> new = %d\n", old_thresh, g_full_cycle_thresh);
	return count;
}
static const struct file_operations full_cycle_thresh_fops = {
	.owner = THIS_MODULE,
	.open = full_cycle_thresh_proc_open,
	.read = seq_read,
	.write = full_cycle_thresh_proc_write,
	.release = single_release,
};

static int accum_soc_proc_show(struct seq_file *buf, void *data)
{
	seq_printf(buf, "---Accumulated charging Soc---\n");
	seq_printf(buf, "Batt_Soc:%d\n", g_cycle_count_data.charge_cap_accum[0]);
	seq_printf(buf, "Sys_Soc:%d\n", g_cycle_count_data.charge_cap_accum[1]);
	seq_printf(buf, "Mono_Soc:%d\n", g_cycle_count_data.charge_cap_accum[2]);
	return 0;
}
static int accum_soc_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, accum_soc_proc_show, NULL);
}
static const struct file_operations accum_soc_fops = {
	.owner = THIS_MODULE,
	.open = accum_soc_proc_open,
	.read = seq_read,
	.release = single_release,
};

/*
 * input pattern: type condition1_time condition2_time
 * type:
 * 0 - reset to default
 * 1 - total battery use time
 * 2 - cycle count
 * 3 - high temperature & volume time
 * 4 - high temperature time
 * 5 - high volume time
 */
static ssize_t batt_condition_value_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
    int type = 0;
    unsigned long condition1_time = 0;
    unsigned long condition2_time = 0;
    char buf[len];
    char *start = buf;

    if(!g_fgChip){
        pr_err("g_fgChip is NULL!");
        return len;
    }

    if (copy_from_user(buf, buff, len-1)) {
        pr_err("Failed to copy from user\n");
        return -EFAULT;
    }
    buf[len-1] = 0;

    sscanf(start, "%d", &type);
    while (*start++ != ' ');
    sscanf(start, "%lu", &condition1_time);
    while (*start++ != ' ');
    sscanf(start, "%lu", &condition2_time);

    if (type && condition2_time <= condition1_time) {
        pr_info("input type error,please input correct type!\n");
        return len;
    }

    switch(type) {
        case 0: // reset to default
            g_fgChip->safety_upgrade_cond.condition1_battery_time = BATTERY_USE_TIME_CONDITION1;
            g_fgChip->safety_upgrade_cond.condition2_battery_time = BATTERY_USE_TIME_CONDITION2;
            g_fgChip->safety_upgrade_cond.condition1_cycle_count = CYCLE_COUNT_CONDITION1;
            g_fgChip->safety_upgrade_cond.condition2_cycle_count = CYCLE_COUNT_CONDITION2;
            g_fgChip->safety_upgrade_cond.condition1_temp_vol_time = HIGH_TEMP_VOL_TIME_CONDITION1;
            g_fgChip->safety_upgrade_cond.condition2_temp_vol_time = HIGH_TEMP_VOL_TIME_CONDITION2;
            g_fgChip->safety_upgrade_cond.condition1_temp_time = HIGH_TEMP_TIME_CONDITION1;
            g_fgChip->safety_upgrade_cond.condition2_temp_time = HIGH_TEMP_TIME_CONDITION2;
            g_fgChip->safety_upgrade_cond.condition1_vol_time = HIGH_VOL_TIME_CONDITION1;
            g_fgChip->safety_upgrade_cond.condition2_vol_time = HIGH_VOL_TIME_CONDITION2;
            g_cycle_count_data.reload_condition = 0;
        break;
        case 1: // total battery use time
            g_fgChip->safety_upgrade_cond.condition1_battery_time = condition1_time;
            g_fgChip->safety_upgrade_cond.condition2_battery_time = condition2_time;
        break;
        case 2: // cycle count
            g_fgChip->safety_upgrade_cond.condition1_cycle_count = (int)condition1_time;
            g_fgChip->safety_upgrade_cond.condition2_cycle_count = (int)condition2_time;
        break;
        case 3: // high temperature & volume time
            g_fgChip->safety_upgrade_cond.condition1_temp_vol_time = condition1_time;
            g_fgChip->safety_upgrade_cond.condition2_temp_vol_time = condition2_time;
        break;
        case 4: // high temperature time
            g_fgChip->safety_upgrade_cond.condition1_temp_time = condition1_time;
            g_fgChip->safety_upgrade_cond.condition2_temp_time = condition2_time;
        break;
        case 5: // high volume time
            g_fgChip->safety_upgrade_cond.condition1_vol_time = condition1_time;
            g_fgChip->safety_upgrade_cond.condition2_vol_time = condition2_time;
        break;
    }

    pr_info("type=%d;condition1_time=%lu;condition2_time=%lu\n", type, condition1_time, condition2_time);
    return len;
}

static int batt_condition_value_proc_read(struct seq_file *buf, void *v)
{
    if(!g_fgChip){
        pr_err("g_fgChipis NULL!");
        return -1;
    }

    seq_printf(buf, "---condition input pattern---\n");
    seq_printf(buf, "type condition1_time condition2_time\n");
    seq_printf(buf, "eg: set high volume time condition1 to 5mins, condition2 to 10mins:\n");
    seq_printf(buf, "echo 5 300 600 > /proc/driver/batt_condition_value\n");

    seq_printf(buf, "---condition type---\n");
    seq_printf(buf, "0 - reset to default\n");
    seq_printf(buf, "1 - total battery use time\n");
    seq_printf(buf, "2 - cycle count\n");
    seq_printf(buf, "3 - high temperature & volume time\n");
    seq_printf(buf, "4 - high temperature time\n");
    seq_printf(buf, "5 - high volume time\n");

    seq_printf(buf, "---condition value---\n");
    seq_printf(buf, "condition1 battery time %lu\n", g_fgChip->safety_upgrade_cond.condition1_battery_time);
    seq_printf(buf, "condition2 battery time %lu\n", g_fgChip->safety_upgrade_cond.condition2_battery_time);
    seq_printf(buf, "condition1 cycle count %d\n", g_fgChip->safety_upgrade_cond.condition1_cycle_count);
    seq_printf(buf, "condition2 cycle count %d\n", g_fgChip->safety_upgrade_cond.condition2_cycle_count);
    seq_printf(buf, "condition1 temp&vol time %lu\n", g_fgChip->safety_upgrade_cond.condition1_temp_vol_time);
    seq_printf(buf, "condition2 temp&vol time %lu\n", g_fgChip->safety_upgrade_cond.condition2_temp_vol_time);
    seq_printf(buf, "condition1 temp time %lu\n", g_fgChip->safety_upgrade_cond.condition1_temp_time);
    seq_printf(buf, "condition2 temp time %lu\n", g_fgChip->safety_upgrade_cond.condition2_temp_time);
    seq_printf(buf, "condition1 vol time %lu\n", g_fgChip->safety_upgrade_cond.condition1_vol_time);
    seq_printf(buf, "condition2 vol time %lu\n", g_fgChip->safety_upgrade_cond.condition2_vol_time);

    return 0;
}

static int batt_condition_value_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, batt_condition_value_proc_read, NULL);
}

static const struct file_operations batt_condition_value_fops = {
	.owner = THIS_MODULE,
	.open =  batt_condition_value_proc_open,
	.write = batt_condition_value_proc_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 * input pattern: type time
 * type:
 * 1 - total battery use time
 * 2 - battery cycle count
 * 3 - high temperature & volume time
 * 4 - high temperature time
 * 5 - high volume time
 */
static ssize_t batt_time_value_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
    int type = 0;
    unsigned long time = 0;
    char buf[30] = {0};
    char *start = buf;
    size_t buf_size;

    if(!g_fgChip){
        pr_err("g_fgChip is NULL!");
        return len;
    }

    buf_size = min(len, (size_t)(sizeof(buf)-1));
    if (copy_from_user(buf, buff, buf_size)) {
        pr_err("Failed to copy from user\n");
        return -EFAULT;
    }
    buf[buf_size] = 0;

    sscanf(start, "%d", &type);
    while (*start++ != ' ');
    sscanf(start, "%lu", &time);

    switch(type) {
        case 1: // total battery use time
            g_cycle_count_data.bat_time_accum.item.battery_total_time = time;
            break;
        case 2: // total battery cycle count
            g_cycle_count_data.charge_cap_accum[0] = time;
            break;
        case 3: // high temperature & volume time
            g_cycle_count_data.bat_time_accum.item.high_temp_vol_time = time;
            break;
        case 4: // high temperature time
            g_cycle_count_data.bat_time_accum.item.high_temp_total_time = time;
            break;
        case 5: // high volume time
            g_cycle_count_data.bat_time_accum.item.high_vol_total_time = time;
            break;
        default:
            pr_info("input error!Now return\n");
            pr_info("---accumulated time input pattern---\n");
            pr_info("type time\n");
            pr_info("eg: set total high volume time to 1day(60*60*24):\n");
            pr_info("echo 5 86400 > /proc/driver/batt_time_value\n");

            pr_info("---condition type---\n");
            pr_info("1 - total battery use time\n");
            pr_info("2 - total battery cycle count\n");
            pr_info("3 - high temperature & volume time\n");
            pr_info("4 - high temperature time\n");
            pr_info("5 - high volume time\n");
            return len;
    }

    schedule_delayed_work(&g_fgChip->safety_upgrade_work, 0);
    pr_info("type=%d;time=%lu\n", type, time);
    return len;
}

static int batt_time_value_proc_read(struct seq_file *buf, void *v)
{
	int i, temp;
	int cycle_count[3] = {0};

	/* Calculate cycle count */
	for(i = 0; i<3; i++) {
		temp = g_cycle_count_data.charge_cap_accum[i];
		cycle_count[i] = DIV_ROUND_CLOSEST(temp * 100 * 10, 256 * g_full_cycle_thresh);
	}

    seq_printf(buf, "---show battery safety value---\n");
    seq_printf(buf, "battery_total_time:%lu\n", g_cycle_count_data.bat_time_accum.item.battery_total_time);
    if(cycle_count[0]%10 >= 5){
        seq_printf(buf, "battery_cycle_count:%d\n", cycle_count[0]/10+1);
    }else{
        seq_printf(buf, "battery_cycle_count:%d\n", cycle_count[0]/10);
    }
    seq_printf(buf, "high_temp_vol_time:%lu\n", g_cycle_count_data.bat_time_accum.item.high_temp_vol_time);
    seq_printf(buf, "high_temp_total_time:%lu\n", g_cycle_count_data.bat_time_accum.item.high_temp_total_time);
    seq_printf(buf, "high_vol_total_time:%lu\n", g_cycle_count_data.bat_time_accum.item.high_vol_total_time);
    seq_printf(buf, "reload_condition:%d\n", g_cycle_count_data.reload_condition);
    seq_printf(buf, "bat_reload_cond:%d\n", g_bat_reload_cond);

    return 0;
}

static int batt_time_value_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, batt_time_value_proc_read, NULL);
}

static const struct file_operations batt_time_value_fops = {
	.owner = THIS_MODULE,
	.open =  batt_time_value_proc_open,
	.write = batt_time_value_proc_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int cycle_count_proc_show(struct seq_file *buf, void *data)
{
	int i, temp;
	int cycle_count[3] = {0};

	/* Calculate cycle count */
	for(i = 0; i<3; i++) {
		temp = g_cycle_count_data.charge_cap_accum[i];
		cycle_count[i] = DIV_ROUND_CLOSEST(temp * 100 * 10, 256 * g_full_cycle_thresh);
	}

    if(cycle_count[0]%10 >= 5){
        seq_printf(buf, "battery_cycle_count:%d\n", cycle_count[0]/10+1);
    }else{
        seq_printf(buf, "battery_cycle_count:%d\n", cycle_count[0]/10);
    }

	return 0;
}
static int cycle_count_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cycle_count_proc_show, NULL);
}

static const struct file_operations cycle_count_fops = {
	.owner = THIS_MODULE,
	.open = cycle_count_proc_open,
	.read = seq_read,
	.release = single_release,
};

//ASUS_BS battery health upgrade +++
void battery_health_data_reset(void){
	g_bat_health_data.bat_current = 0;
	g_bat_health_data.bat_current_avg = 0;
	g_bat_health_data.accumulate_time = 0;
	g_bat_health_data.accumulate_current = 0;
	g_bat_health_data.start_time = 0;
	g_bat_health_data.end_time = 0;
	g_bathealth_trigger = false;
	g_last_bathealth_trigger = false;
	wake_unlock(&bat_health_lock);
}

static int resotre_bat_health(void)
{
	int i=0, rc = 0;

#if 0
	memset(&g_bat_health_data_backup,0,sizeof(struct BAT_HEALTH_DATA_BACKUP)*BAT_HEALTH_NUMBER_MAX);

	/* Read cycle count data from emmc */
	rc = file_op(CYCLE_COUNT_FILE_NAME, BAT_HEALTH_DATA_OFFSET1,
		(char*)&g_bat_health_data_backup, sizeof(struct BAT_HEALTH_DATA_BACKUP)*BAT_HEALTH_NUMBER_MAX, FILE_OP_READ);
	if(rc < 0) {
		pr_err("Read bat health file failed!\n");
		return -1;
	}
#endif

	rc = batt_health_csc_backup();
	if(rc < 0){
		return rc;
	}

	BAT_DBG("%s: index(%d)\n",__FUNCTION__, g_bat_health_data_backup[0].health);
	for(i=1; i<BAT_HEALTH_NUMBER_MAX;i++){
		if(g_bat_health_data_backup[i].health)
			BAT_DBG("%s %d",g_bat_health_data_backup[i].date, g_bat_health_data_backup[i].health);
	}

	g_health_upgrade_index = g_bat_health_data_backup[0].health;
	g_bathealth_initialized = true;

	return 0;
}

static int backup_bat_health(void)
{
	int bat_health, rc;
	struct timespec ts;
	struct rtc_time tm;
	int health_t;
	int count=0, i=0;
	unsigned long long bat_health_accumulate=0;

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec,&tm);

	bat_health = g_bat_health_data.bat_health;

	if(g_health_upgrade_index == BAT_HEALTH_NUMBER_MAX-1){
		g_health_upgrade_index = 1;
	}else{
		g_health_upgrade_index++;
	}

	sprintf(g_bat_health_data_backup[g_health_upgrade_index].date, "%d-%02d-%02d %02d:%02d:%02d", tm.tm_year+1900,tm.tm_mon+1, tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);
	g_bat_health_data_backup[g_health_upgrade_index].health = bat_health;
	g_bat_health_data_backup[0].health = g_health_upgrade_index;

	if(g_health_debug_enable)
		BAT_DBG("%s ===== Health history ====\n",__FUNCTION__);

	for(i=1;i<BAT_HEALTH_NUMBER_MAX;i++){
		if(g_bat_health_data_backup[i].health!=0){
			count++;
			bat_health_accumulate += g_bat_health_data_backup[i].health;
			if(g_health_debug_enable)
				BAT_DBG("%s %02d:%d\n",__FUNCTION__,i,g_bat_health_data_backup[i].health);
		}
	}

	if(g_health_debug_enable)
		BAT_DBG("%s ========================\n",__FUNCTION__);

	if (count == 0){
		BAT_DBG("%s battery health value is empty\n",__FUNCTION__);
		return -1;
	}

	health_t = bat_health_accumulate*10/count;
	g_bat_health_avg = (int)(health_t + 5)/10;
	g_bat_health_data_backup[g_health_upgrade_index].health = g_bat_health_avg;

	rc = file_op(CYCLE_COUNT_FILE_NAME, BAT_HEALTH_DATA_OFFSET1,
		(char *)&g_bat_health_data_backup, sizeof(struct BAT_HEALTH_DATA_BACKUP)*BAT_HEALTH_NUMBER_MAX, FILE_OP_WRITE);
	if(rc<0){
		pr_err("%s:Write file:%s err!\n", __FUNCTION__, CYCLE_COUNT_FILE_NAME);
	}

	return rc;
}

int batt_health_csc_backup(void){
	int rc=0, i=0;
	char *buf2;
	int len = 0;

	buf2 = kmalloc(sizeof(char)*BAT_HEALTH_NUMBER_MAX*30, GFP_KERNEL);
	if(!buf2){
		BAT_DBG_E("%s kmalloc buf2 fail!\n", __func__);
		return -1;
	}

	memset(&g_bat_health_data_backup,0,sizeof(struct BAT_HEALTH_DATA_BACKUP)*BAT_HEALTH_NUMBER_MAX);
	memset(buf2,0,sizeof(char)*BAT_HEALTH_NUMBER_MAX*30);

	rc = file_op(CYCLE_COUNT_FILE_NAME, BAT_HEALTH_DATA_OFFSET1,
		(char*)&g_bat_health_data_backup, sizeof(struct BAT_HEALTH_DATA)*BAT_HEALTH_NUMBER_MAX, FILE_OP_READ);
	if(rc < 0) {
		BAT_DBG_E("Read bat health file failed!\n");
		goto ERROR;
	}

	for(i=1;i<BAT_HEALTH_NUMBER_MAX;i++){
		if(g_bat_health_data_backup[i].health!=0){
			sprintf(buf2+len, "%s [%d]\n", g_bat_health_data_backup[i].date, g_bat_health_data_backup[i].health);
			len += strlen(buf2+len);
		}
	}

	rc = file_op(BAT_HEALTH_DATA_SD_FILE_NAME, BAT_HEALTH_DATA_OFFSET,
	buf2, strlen(buf2), FILE_OP_WRITE);
	if(rc < 0 ){
		BAT_DBG_E("Write bat health file failed!\n");
		goto ERROR;
	}

	BAT_DBG("%s Done! \n", __func__);
ERROR:
	kfree(buf2);
	buf2 = NULL;
	return rc;
}

extern int fg_get_prop_capacity(struct fg_chip *chip, int *val);
extern int fg_get_battery_current(struct fg_chip *chip, int *val);
static void update_battery_health(struct fg_chip *chip){
	int bat_current, bat_capacity, delta_p;
	unsigned long T;
	int health_t;
	int rc;

	if(g_health_upgrade_enable != true){
		return;
	}

	if(g_bathealth_initialized != true){
		resotre_bat_health();
		return;
	}

	if(!chip->online_status){
		if(g_last_bathealth_trigger == true){
			battery_health_data_reset();
		}
		return;
	}

	fg_get_prop_capacity(chip, &bat_capacity);

	if(bat_capacity == g_health_upgrade_start_level && g_bat_health_data.start_time == 0){
		wake_lock(&bat_health_lock);
		g_bathealth_trigger = true;
		rc = asus_qpnp_rtc_read_time(&g_bat_health_data.start_time);
	    if (rc) {
	        pr_err("read rtc time fail\n");
	        return ;
	    }
	}
	if(bat_capacity > g_health_upgrade_end_level){
		g_bathealth_trigger = false;
	}
	if(g_last_bathealth_trigger == false && g_bathealth_trigger == false){
		return;
	}

	if( g_bathealth_trigger ){
		fg_get_battery_current(chip, &bat_current);

		g_bat_health_data.accumulate_time += g_health_upgrade_upgrade_time;
		g_bat_health_data.bat_current = -bat_current;
		g_bat_health_data.accumulate_current += g_bat_health_data.bat_current;
		g_bat_health_data.bat_current_avg = g_bat_health_data.accumulate_current/g_bat_health_data.accumulate_time;

		if(g_health_debug_enable){
			BAT_DBG("%s accumulate_time(%llu), accumulate_current(%llu), bat_current(%d), bat_current_avg(%llu), bat_capacity(%d)",
				__FUNCTION__, g_bat_health_data.accumulate_time, g_bat_health_data.accumulate_current/1000, g_bat_health_data.bat_current/1000,
				g_bat_health_data.bat_current_avg/1000, bat_capacity);
		}

		if(bat_capacity >= g_health_upgrade_end_level){
			rc = asus_qpnp_rtc_read_time(&g_bat_health_data.end_time);
		    if (rc) {
		        pr_err("read rtc time fail\n");
		        return ;
		    }

			delta_p = g_health_upgrade_end_level - g_health_upgrade_start_level;
			// g_bat_health_data.bat_current_avg; //uA
			// g_bat_health_data.accumulate_time; //second
			T = g_bat_health_data.end_time - g_bat_health_data.start_time;
			health_t = (g_bat_health_data.bat_current_avg*T)*10/(unsigned long long)(ZS620KL_DESIGNED_CAPACITY*delta_p)/(unsigned long long)360;
			g_bat_health_data.bat_health = (int)((health_t + 5)/10);

			//if(g_bat_health_data.bat_health > 100) g_bat_health_data.bat_health = 100;
			//if(g_bat_health_data.bat_health < 0) g_bat_health_data.bat_health = 0;

			backup_bat_health();
			batt_health_csc_backup();
			BAT_DBG("%s battery health = (%d,%d), T(%lu), bat_current_avg(%llu)",__FUNCTION__, g_bat_health_data.bat_health, g_bat_health_avg, T, g_bat_health_data.bat_current_avg/1000);
			battery_health_data_reset();
		}else{
			//do nothing
		}
	}else{
		battery_health_data_reset();
	}
	g_last_bathealth_trigger = g_bathealth_trigger;
}

void battery_health_upgrade_data_polling(int time) {
	cancel_delayed_work(&battery_health_work);
	schedule_delayed_work(&battery_health_work, time * HZ);
}

void battery_health_worker(struct work_struct *work)
{
	update_battery_health(g_fgChip);
	if(g_bathealth_initialized)
		battery_health_upgrade_data_polling(g_health_upgrade_upgrade_time); // update each hour
	else
		battery_health_upgrade_data_polling(60);
}

#if 0
static void update_battery_metadata(struct fg_chip *chip){
	//copy health data to sdcard
	batt_health_csc_backup();
}

void battery_metadata_upgrade_data_polling(int time) {
	cancel_delayed_work(&battery_metadata_work);
	schedule_delayed_work(&battery_metadata_work, time * HZ);
}

void battery_metadata_worker(struct work_struct *work)
{
	update_battery_metadata(g_fgChip);
	battery_metadata_upgrade_data_polling(BATTERY_METADATA_UPGRADE_TIME); // update each hour
}
#endif

static void batt_health_upgrade_debug_enable(bool enable){
	g_health_debug_enable = enable;
	BAT_DBG("%s: %d\n",__FUNCTION__,g_health_debug_enable);
}

static void batt_health_upgrade_enable(bool enable){
	g_health_upgrade_enable = enable;
	BAT_DBG("%s: %d\n",__FUNCTION__,g_health_upgrade_enable);
}

static void batt_health_clear_value(void)
{
	int rc;
	char buf[BAT_HEALTH_NUMBER_MAX*30];

	memset(g_bat_health_data_backup, 0, sizeof(struct BAT_HEALTH_DATA_BACKUP)*BAT_HEALTH_NUMBER_MAX);
	memset(buf, 0, sizeof(char)*BAT_HEALTH_NUMBER_MAX*30);

	rc = file_op(CYCLE_COUNT_FILE_NAME, BAT_HEALTH_DATA_OFFSET1,
			(char *)&g_bat_health_data_backup, sizeof(struct BAT_HEALTH_DATA_BACKUP)*BAT_HEALTH_NUMBER_MAX, FILE_OP_WRITE);
	if(rc < 0 )
		BAT_DBG_E("Write %s file failed!\n", CYCLE_COUNT_FILE_NAME);
	rc = file_op(BAT_HEALTH_DATA_SD_FILE_NAME, BAT_HEALTH_DATA_OFFSET,
			buf, sizeof(char)*BAT_HEALTH_NUMBER_MAX*30, FILE_OP_WRITE);
	if(rc < 0 )
		BAT_DBG_E("Write %s file failed!\n", BAT_HEALTH_DATA_SD_FILE_NAME);

	BAT_DBG("clear batt health value!\n");
}

static int batt_health_config_proc_show(struct seq_file *buf, void *data)
{
	int count=0, i=0;
	unsigned long long bat_health_accumulate=0;

	seq_printf(buf, "start level:%d\n", g_health_upgrade_start_level);
	seq_printf(buf, "end level:%d\n", g_health_upgrade_end_level);
	seq_printf(buf, "upgrade time:%d\n", g_health_upgrade_upgrade_time);
	seq_printf(buf, "enable debug:%d\n", g_health_debug_enable);
	seq_printf(buf, "enable upgrade:%d\n", g_health_upgrade_enable);

	for(i=1;i<BAT_HEALTH_NUMBER_MAX;i++){
		if(g_bat_health_data_backup[i].health!=0){
			count++;
			bat_health_accumulate += g_bat_health_data_backup[i].health;
		}
	}

	g_bat_health_avg = ((bat_health_accumulate*10/count) + 5)/10;

	seq_printf(buf, "health_avg: %d\n", g_bat_health_avg);

	return 0;
}

static int batt_health_config_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, batt_health_config_proc_show, NULL);
}

static ssize_t batt_health_config_write(struct file *file,const char __user *buffer,size_t count,loff_t *pos)
{
	int command=0;
	int value = 0;
	char buf[30] = {0};
	size_t buf_size;
	char *start = buf;

	buf_size = min(count, (size_t)(sizeof(buf)-1));
	if (copy_from_user(buf, buffer, buf_size)) {
		BAT_DBG_E("Failed to copy from user\n");
		return -EFAULT;
	}
	buf[buf_size] = 0;

	sscanf(start, "%d", &command);
	while (*start++ != ' ');
	sscanf(start, "%d", &value);

	switch(command){
		case 1:
			g_health_upgrade_start_level = value;
			BAT_DBG("health upgrade start_level = %d;\n", value);
		break;
		case 2:
			g_health_upgrade_end_level = value;
			BAT_DBG("health upgrade end_level = %d;\n", value);
		break;
		case 3:
			g_health_upgrade_upgrade_time = value;
			BAT_DBG("health upgrade time = %d;\n", value);
		break;

		case 4: //clear battery health value
			batt_health_clear_value();
		break;

		case 5: // disable battery health debug log
			batt_health_upgrade_debug_enable(false);
		break;

		case 6: // enable battery health debug log
			batt_health_upgrade_debug_enable(true);
		break;

		case 7: // disable battery health upgrade
			batt_health_upgrade_enable(false);
			break;

		case 8: // enable battery health upgrade
			batt_health_upgrade_enable(true);
			break;

		default:
			BAT_DBG("input error!Now return\n");
			return count;
	}

	return count;
}

static const struct file_operations batt_health_config_fops = {
	.owner = THIS_MODULE,
	.open = batt_health_config_proc_open,
	.read = seq_read,
	.write = batt_health_config_write,
	.release = single_release,
};

void asus_add_battery_health_fun(void)
{
	INIT_DELAYED_WORK(&battery_health_work, battery_health_worker); //battery_health_work
	wake_lock_init(&bat_health_lock, WAKE_LOCK_SUSPEND, "bat_health_lock");
	battery_health_data_reset();
	schedule_delayed_work(&battery_health_work, 60 * HZ);
}

static void create_batt_health_config_proc_file(void)
{
	struct proc_dir_entry *proc_file = proc_create("driver/batt_health_config", 0644, NULL, &batt_health_config_fops);
	if (!proc_file) {
		BAT_DBG_E("[Proc]%s failed!\n", __func__);
	}
	return;
}
//ASUS_BS battery health upgrade ---

static void create_batt_cycle_count_proc_file(void)
{
	struct proc_dir_entry *asus_batt_cycle_count_dir = proc_mkdir("Batt_Cycle_Count", NULL);
	struct proc_dir_entry *asus_batt_cycle_count_proc_file = proc_create("cycle_count", 0644,
		asus_batt_cycle_count_dir, &cycle_count_fops);
	struct proc_dir_entry *asus_batt_accum_soc_proc_file = proc_create("accum_soc", 0644,
		asus_batt_cycle_count_dir, &accum_soc_fops);
	struct proc_dir_entry *asus_batt_full_cycle_thresh_proc_file = proc_create("full_cycle_thresh", 0644,
		asus_batt_cycle_count_dir, &full_cycle_thresh_fops);
	struct proc_dir_entry *asus_batt_safety_condition_proc_file = proc_create("condition_value", 0644,
		asus_batt_cycle_count_dir, &batt_condition_value_fops);
	struct proc_dir_entry *asus_batt_safety_time_proc_file = proc_create("batt_safety", 0644,
		asus_batt_cycle_count_dir, &batt_time_value_fops);
	if (!asus_batt_cycle_count_dir)
		printk("batt_cycle_count_dir create failed!\n");
	if (!asus_batt_cycle_count_proc_file)
		printk("batt_cycle_count_proc_file create failed!\n");
	if (!asus_batt_accum_soc_proc_file)
		printk("batt_accum_soc_proc_file create failed!\n");
	if (!asus_batt_full_cycle_thresh_proc_file)
		printk("batt_full_cycle_thresh_proc_file create failed!\n");
	if (!asus_batt_safety_condition_proc_file)
		printk(" create asus_batt_safety_condition_proc_file failed!\n");
	if (!asus_batt_safety_time_proc_file)
		printk(" create asus_batt_safety_time_proc_file failed!\n");
}

void asus_procfs_create(void)
{
    BAT_DBG("%s: enter\n", __func__);
	create_water_detection_adc_proc_file();
	create_wp_state_proc_file();
	create_liquid_state_proc_file();
	create_wmsg_state_proc_file();
	create_vadc_enable_proc_file();
	create_vadc_high_thr_1_proc_file();
	create_vadc_high_thr_2_proc_file();
	create_vadc_low_thr_proc_file();
	create_liquid_high_bound_proc_file();
	create_liquid_low_bound_proc_file();
	create_thermal_test_proc_file();
	create_batt_cycle_count_proc_file();
	create_batt_health_config_proc_file(); //battery health upgrade
#ifdef ASUS_FACTORY_BUILD
	create_asus_ATD_gaugeIC_status_interface();
	create_asus_ATD_battery_current_interface();
	create_asus_ATD_battery_voltage_interface();
#endif
}

void asus_fg_init_config(struct fg_chip *chip)
{
    int rc;
    u8	batt_aux_therm_coeffs[3] = {0xB6, 0x2D, 0xE5};

    // Set FG_ADC_RR_AUX_THERM_Cx_COEFF(0x4588/0x4589/0x458A)
    rc = fg_write(chip, chip->rradc_base + 0x88,
        batt_aux_therm_coeffs, 3);
    if (rc < 0) {
        pr_err("Error in writing battery aux thermal coefficients, rc=%d\n",
            rc);
    }
}

/* Write back batt_cyclecount data before restart/shutdown */
static int reboot_shutdown_prep(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	switch(event) {
	case SYS_RESTART:
	case SYS_POWER_OFF:
		/* Write data back to emmc */
		write_back_cycle_count_data();
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}
/*  Call back function for reboot notifier chain  */
static struct notifier_block reboot_blk = {
	.notifier_call	= reboot_shutdown_prep,
};

int asus_fg_porting(struct fg_chip *chip)
{
	if(!chip){
		BAT_DBG_E("struct fg_chip is NULL,Now will return!\n");
		return -1;
	}
BAT_DBG("%s: enter\n", __func__);
	asus_fg_init_config(chip);

	asus_batt_cycle_count_init();
	asus_battery_safety_init(chip);
	register_reboot_notifier(&reboot_blk);

	asus_water_detection_init(chip);
	asus_battery_version_init();
	asus_battery_id_init();

	asus_procfs_create();

	return 0;
}

// 0 for sucess,value passed in to data
//-1 for fail
//-2 magic not exist
int asus_fg_get_record(struct RECORD_DATA *data)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	loff_t pos_lsts = 0;
	struct RECORD_DATA record_data;
	int readlen = 0;

	fp = filp_open(CYCLE_COUNT_FILE_NAME, O_RDONLY , S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_info("%s: open (%s) fail(%ld)\n", __func__, CYCLE_COUNT_FILE_NAME, PTR_ERR(fp));
		return -1;
	}

	/*For purpose that can use read/write system call*/
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	//lseek(fp, sizeof(struct CYCLE_COUNT_DATA), SEEK_SET);

    pos_lsts = data->pos;
	readlen = vfs_read(fp, (uint8_t *)&record_data, sizeof(struct RECORD_DATA), &pos_lsts);
	if (readlen < 0) {
		pr_info("%s read (%s) failed\n",__FUNCTION__,CYCLE_COUNT_FILE_NAME);
		set_fs(old_fs);
		filp_close(fp, NULL);
		return -1;
	}

	set_fs(old_fs);
	filp_close(fp, NULL);

	if(record_data.magic != data->magic || record_data.pos != data->pos)
	{
		printk("[BAT] get record_data.magic is %08x , record_data.pos is %d \n",record_data.magic,record_data.pos);
		return RECORD_NOT_INIT;
	}

	data->value = record_data.value;

	return 0;
}


// 0 for sucess,value passed in to data
//-1 for fail
//-2 magic not exist
int asus_fg_set_record(struct RECORD_DATA *data)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	loff_t pos_lsts = 0;
	int rc;

	fp = filp_open(CYCLE_COUNT_FILE_NAME, O_RDWR , S_IRWXU | S_IRWXG | S_IRWXO);
	if (IS_ERR_OR_NULL(fp)) {
		pr_info("%s: open (%s) fail(%ld)\n", __func__, CYCLE_COUNT_FILE_NAME, PTR_ERR(fp));
		return -1;
	}

	/*For purpose that can use read/write system call*/
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	//lseek(fp, sizeof(struct CYCLE_COUNT_DATA), SEEK_SET);

    pos_lsts = data->pos;
    rc = vfs_write(fp, (uint8_t *)data, sizeof(struct RECORD_DATA), &pos_lsts);
    printk("[BAT] write record_data.magic is %08x , record_data.pos is %d value is %d\n",data->magic,data->pos,data->value);
    if (rc < 0) {
        set_fs(old_fs);
        filp_close(fp, NULL);
		return -1;
    }

	set_fs(old_fs);
	filp_close(fp, NULL);

	return 0;
}

int asus_get_record(int *val,int index)
{
	int ret = 0;
	struct RECORD_DATA record_data;
	record_data.magic = record_magic[index];
	record_data.pos = record_pos[index];

	ret = asus_fg_get_record(&record_data);
	if(ret == 0)
	{
		*val = record_data.value;
	}

	return ret;
}

int asus_set_record(int val,int index)
{
	int ret = 0;
	struct RECORD_DATA record_data;
	record_data.magic = record_magic[index];
	record_data.pos = record_pos[index];
	record_data.value = val;

	ret = asus_fg_set_record(&record_data);

	return ret;
}


int asus_get_RCONN_RECORED(int *val)
{
	return asus_get_record(val,RECORD_RCONN);
}

int asus_set_RCONN_RECORED(int val)
{
	return asus_set_record(val,RECORD_RCONN);
}

void asus_check_rconn(void)
{
	int flag = 1; //need to check
	int rconn = 0;
	int ret = 0;
	if(!g_fgChip)
	{
		pr_err("g_fgChip is NULL !\n");
		flag = 0;
	}

	if(flag == 1)
	{
		ret = asus_get_RCONN_RECORED(&rconn);

		if(ret == RECORD_NOT_INIT)
		{
			//never set
			printk("[BAT] rconn is not set");
			ASUSEvtlog("[BAT] rconn is not set");
			asus_clear_reload_battery_profile(g_fgChip);
		}
		else if(ret == 0)
		{
			if(rconn != g_fgChip->dt.rconn_mohms)
			{
				if (g_fgChip->asus_profile_changed) {
					g_fgChip->asus_profile_changed = 0;
					printk("[BAT] profile reloaded, no need to update rconn again\n");
					ASUSEvtlog("[BAT] profile reloaded, no need to update rconn again");
					asus_set_RCONN_RECORED(g_fgChip->dt.rconn_mohms);
					return;
				}
				printk("[BAT] rconn need to update \n");
				ASUSEvtlog("[BAT] rconn need to update,set rconn");
				asus_clear_reload_battery_profile(g_fgChip);
			}
			else
			{
				ASUSEvtlog("[BAT] rconn is the same,rconn %d\n",rconn);
				printk("[BAT] rconn is the same,rconn %d\n",rconn);
			}
		}
		else
		{
			ASUSEvtlog("[BAT]fail to read rconn,just do it next boot time\n");
			printk("[BAT]fail to read rconn,just do it next boot time\n");
		}
	}
	else
	{
		ASUSEvtlog("[BAT]fail to read rconn,just do it next boot time,g_fgChip/g_fgChip->dt.rconn_mohms\n");
		printk("[BAT]fail to read rconn,just do it next boot time,g_fgChip/g_fgChip->dt.rconn_mohms\n");
	}

}

void asus_check_full_pending(struct fg_chip *chip, int ibatt_now, int msoc)
{
    int pending_state = 0;
    static struct timespec release_start_time = {.tv_sec = 0, .tv_nsec = 0,};
    struct timespec now, debounce_time;

    if (chip == NULL) {
        BAT_DBG_E("chip is null!\n");
        return;
    }

    if (ibatt_now < 0 && chip->last_report_msoc == 99 && msoc == 100) { // 99% to 100% while charging
        pending_state = (ibatt_now <= ASUS_REPORT_FULL_IBAT_THRESH) ? 1 : 0;// report 100% only when charge current above threshold
        //BAT_DBG("ibatt_now:%d, pending_state:%d\n", ibatt_now, pending_state);
        if (pending_state != chip->asus_pending_report_full) {
            now = current_kernel_time();
            if (!pending_state) {
                if (release_start_time.tv_sec == 0 && release_start_time.tv_nsec == 0) {
                    release_start_time.tv_sec = now.tv_sec;
                }
                debounce_time.tv_sec = release_start_time.tv_sec + ASUS_REPORT_FULL_DEBOUNCE_TIME;
            }
            BAT_DBG("full capacity pending state changed(%d -> %d), ibatt_now:%d mA, msoc:%d, last_report_msoc:%d\n",
                chip->asus_pending_report_full, pending_state, ibatt_now/1000, msoc, chip->last_report_msoc);
            if ((!pending_state && now.tv_sec > debounce_time.tv_sec) || pending_state) {
                release_start_time.tv_sec = 0;
                release_start_time.tv_nsec = 0;
                chip->asus_pending_report_full = pending_state;
                asus_set_record(chip->asus_pending_report_full, RECORD_PENDINGFULL);
            }
        } else {
            release_start_time.tv_sec = 0;
            release_start_time.tv_nsec = 0;
        }
    } else {
        release_start_time.tv_sec = 0;
        release_start_time.tv_nsec = 0;
    }
}
