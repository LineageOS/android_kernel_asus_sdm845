#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/pmic-voter.h>
#include <linux/bitops.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/iio/consumer.h>
#include <linux/slab.h>

#include "asus_charger.h"


#define smblib_err(chg, fmt, ...)		\
	pr_err("%s: %s: " fmt, chg->name,	\
		__func__, ##__VA_ARGS__)	\

#define smblib_dbg(chg, reason, fmt, ...)			\
	do {							\
		if (*chg->debug_mask & (reason))		\
			pr_info("%s: %s: " fmt, chg->name,	\
				__func__, ##__VA_ARGS__);	\
		else						\
			pr_debug("%s: %s: " fmt, chg->name,	\
				__func__, ##__VA_ARGS__);	\
	} while (0)

#define HVDCP_PULSE_COUNT_MAX 0x135B

#define ASUS_MONITOR_CYCLE		60000
#define ADC_WAIT_TIME_HVDCP0	3000// 15000
#define ADC_WAIT_TIME_HVDCP23	100// 100

#define USB_ICL_MAX 0xC0 // 192*25mA = 4800mA

#define FCC_OVERRIDE_1 500000
#define FCC_OVERRIDE_2 1000000

// default parameters +++
const int draco_evb_jeita_cfg[]={0,100,200,500,600};
const int draco_evb_jeita_rchg_cfg[]={30,130,230,470,570};
#ifdef ASUS_FACTORY_BUILD
const int draco_evb_jeita_fcc_cfg[]={900000,900000,1500000,2000000,2000000,1500000,1500000,1500000,1500000};
#else
const int draco_evb_jeita_fcc_cfg[]={900000,900000,1500000,3000000,3000000,1500000,1500000,1500000,1500000};
#endif
const int draco_evb_icl_cfg[]={500,900,950,1400,1910};
// default parameters ---

static const unsigned int usb_connector_ext_supported_cable[] = {
	EXTCON_NONE,
};
static const unsigned int qc_stat_ext_supported_cable[] = {
	EXTCON_NONE,
};


static int ASUS_ADAPTER_ID = 0;
bool charger_batt_enable = 1;
bool charger_limit_enable;
bool charger_flag=1;
int charger_limit_setting;
bool asus_flow_done_flag = 0;
bool asus_thermal_keep_chg_flag = 0;
bool asus_get_vbat_in_process_flag = 0;
bool g_legacy_det_done = false;
int ptc_check_fcc_override_1 = FCC_OVERRIDE_1;
int ptc_check_fcc_override_2 = FCC_OVERRIDE_2;
int g_CDP_WA = 0;
bool g_CDP_WA_done_once = false;
bool smartchg_stop_chg_flag = false;

int fcc_override_setting;
int asus_fcc_override_flag = 0;
int vbat_avg_1 = 0;
int vbat_avg_2 = 0;

struct delayed_work charging_limit_work;
struct delayed_work cable_capability_check_work;
struct delayed_work SetJeitaRTCWorker;
struct delayed_work asus_rconn_check_work;
struct delayed_work reset_icl_work;
struct delayed_work reset_icl_with_override_work;
struct wakeup_source adc_check_lock;
struct wakeup_source ChargerModeLock;
struct wakeup_source UsbCable_Lock;
static struct alarm jeita_alarm;
static DEFINE_SPINLOCK(jeita_slock);

bool g_boot_complete = false;
bool g_ready_to_report_2 = false;
bool g_receiver_enable = false;

void reset_icl_for_nonstandard_ac(bool icl_override);

//ASUS BSP FOR ubatterylife +++
bool g_ubatterylife_enable_flag = 0;
int ubatlife_chg_status = UBATLIFE_CHG_THD;

//ASUS_BSP +++
bool is_ubatlife_dischg(void)
{
	return (ubatlife_chg_status == UBATLIFE_DISCHG_THD) && g_ubatterylife_enable_flag ? true: false;
}
//ASUS_BSP ---

//ASUS_BSP +++
#define CHGLimit_PATH "/asdf/CHGLimit_kernel"
static bool check_ultrabatterylife_enable(void)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	loff_t pos_lsts = 0;
	char buf[8] = "";
	int l_result = -1;

	fp = filp_open(CHGLimit_PATH, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(fp)) {
		CHG_DBG_E("%s: open (%s) fail\n", __func__, CHGLimit_PATH);
		return false;	/*No such file or directory*/
	}

	/*For purpose that can use read/write system call*/
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	vfs_read(fp,buf,6,&pos_lsts);

	set_fs(old_fs);
	filp_close(fp, NULL);

	sscanf(buf, "%d", &l_result);
	CHG_DBG("%s: %d",__func__, l_result);

	if(l_result == 1){
		return true;
	}else{
		return false;
	}
}

void update_ubatterylife_info(bool* dismiss){
	bool prev_ubat_flag = g_ubatterylife_enable_flag;
	g_ubatterylife_enable_flag = check_ultrabatterylife_enable();

	if(prev_ubat_flag == g_ubatterylife_enable_flag)
		return;

	// handle ubatterylife dismiss
	if(prev_ubat_flag){
		*dismiss = true;
		CHG_DBG("ultrabatterylife dismiss\n");

	}

	if(g_ubatterylife_enable_flag)
		CHG_DBG("ultrabatterylife triggered\n");
}
//ASUS_BSP ---
//ASUS BSP FOR ubatterylife ---

enum ADAPTER_ID {
	ASUS_750K,
	ASUS_200K,
	PB,
	OTHERS,
	ADC_NOT_READY,
};

static char *asus_id[] = {
	"ASUS_750K",
	"ASUS_200K",
	"PB",
	"OTHERS",
	"ADC_NOT_READY"
};

char *ufp_type[] = {
	"NONE",
	"DEFAULT",
	"MEDIUM",
	"HIGH",
	"OTHERS"
};

struct apsd_result {
	const char * const name;
	const u8 bit;
	const enum power_supply_type pst;
};

extern bool asp1690_ready;

extern const struct apsd_result *smblib_get_apsd_result(struct smb_charger *chg);
extern const struct apsd_result *smblib_update_usb_type(struct smb_charger *chg);
extern int smblib_get_prop_ufp_mode(struct smb_charger *chg);
extern int asp1690E_CHG_TYPE_judge(void);
extern void SwitchTo1D(void);
extern void OpenDpDm(bool open);
extern int usb_therm_trigger(void);
extern void usb_therm_set_thresh_1(void);
extern void usb_therm_set_thresh_2(void);

int smblib_get_prop_batt_temp(struct smb_charger *chg,
			      union power_supply_propval *val)
{
	int rc;

	if (!chg->bms_psy)
		return -EINVAL;

	rc = power_supply_get_property(chg->bms_psy,
				       POWER_SUPPLY_PROP_TEMP, val);
	return rc;
}

int smblib_get_prop_batt_voltage_now(struct smb_charger *chg,
				     union power_supply_propval *val)
{
	int rc;

	if (!chg->bms_psy)
		return -EINVAL;

	rc = power_supply_get_property(chg->bms_psy,
				       POWER_SUPPLY_PROP_VOLTAGE_NOW, val);
	return rc;
}

int smblib_get_prop_batt_current_now(struct smb_charger *chg,
				     union power_supply_propval *val)
{
	int rc;

	if (!chg->bms_psy)
		return -EINVAL;

	rc = power_supply_get_property(chg->bms_psy,
				       POWER_SUPPLY_PROP_CURRENT_NOW, val);
	return rc;
}

int asus_get_prop_batt_temp(struct smb_charger *chg)
{
	union power_supply_propval temp_val = {0, };
	int rc;

	rc = smblib_get_prop_batt_temp(chg, &temp_val);

	return temp_val.intval;
}

int asus_get_prop_batt_volt(struct smb_charger *chg)
{
	union power_supply_propval volt_val = {0, };
	int rc;

	rc = smblib_get_prop_batt_voltage_now(chg, &volt_val);

	return volt_val.intval;
}

int asus_get_prop_batt_current(struct smb_charger *chg)
{
	union power_supply_propval current_val = {0, };
	int rc;

	rc = smblib_get_prop_batt_current_now(chg, &current_val);

	return current_val.intval;
}

int asus_get_prop_batt_capacity(struct smb_charger *chg)
{
	union power_supply_propval capacity_val = {0, };
	int rc;

	rc = smblib_get_prop_batt_capacity(chg, &capacity_val);

	return capacity_val.intval;
}

int asus_get_prop_batt_status(struct smb_charger *chg)
{
	union power_supply_propval status_val = {0, };
	union power_supply_propval default_val = {0, };
	int rc;

	rc = smblib_get_prop_batt_status(chg, &default_val,&status_val);

	return status_val.intval;
}

int asus_check_batt_health(struct smb_charger *chg)
{
	int cur_state;
	int rc;
	u8 stat;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_2_REG, &stat);
	if (rc < 0) {
		CHG_DBG_E("Couldn't read BATTERY_CHARGER_STATUS_2 rc=%d\n", rc);
		return chg->asus_chg->last_batt_health;
	}

	if (stat & BAT_TEMP_STATUS_TOO_COLD_BIT) {
		cur_state = POWER_SUPPLY_HEALTH_COLD;
		if (chg->asus_chg->last_batt_health != cur_state) {
			CHG_DBG_E("JEITA Hard Cold is triggered\n");
			ASUSErclog(ASUS_JEITA_HARD_COLD, "JEITA Hard Cold is triggered");
		}
	}
	else if (stat & BAT_TEMP_STATUS_TOO_HOT_BIT) {
		cur_state = POWER_SUPPLY_HEALTH_OVERHEAT;
		if (chg->asus_chg->last_batt_health != cur_state) {
			CHG_DBG_E("JEITA Hard Hot is triggered\n");
			ASUSErclog(ASUS_JEITA_HARD_HOT, "JEITA Hard Hot is triggered");
		}
	}
	else if (stat & BAT_TEMP_STATUS_COLD_SOFT_LIMIT_BIT) {
		cur_state = POWER_SUPPLY_HEALTH_COOL;
	}
	else if (stat & BAT_TEMP_STATUS_HOT_SOFT_LIMIT_BIT) {
		cur_state = POWER_SUPPLY_HEALTH_WARM;
		if (chg->asus_chg->last_batt_health != cur_state) {
			CHG_DBG_E("JEITA Soft Hot is triggered\n");
			ASUSErclog(ASUS_JEITA_SOFT_HOT, "JEITA Soft Hot is triggered");
		}
	}
	else
		cur_state = POWER_SUPPLY_HEALTH_GOOD;

	chg->asus_chg->last_batt_health = cur_state;

	return cur_state;
}

int asus_set_prop_pd_qc_state(struct smb_charger *chg,
				    const union power_supply_propval *val)
{
	CHG_DBG("%s: target qc_state = %d\n", __func__, val->intval);
	if (val->intval < 0 || val->intval > AC_EQ_10W) {
		return -1;
	}

	chg->asus_chg->quick_charge_ac_flag = val->intval;

	return 0;
}

extern u32 asus_rpm_count[2];
extern uint32_t asus_rpmh_counts[5];
static void asus_dump_sleep_count(void) {
	if(asus_rpm_count[0] == 0xffffffff || asus_rpmh_counts[1] == 0xffffffff){
		printk("%s:rpm or rpmh count is not init!\n", __func__);
		return ;
	}

	ASUSEvtlog("aosd=0x%x;cxsd=0x%x;APSS=0x%x;MPSS=0x%x;ADSP=0x%x;CDSP=0x%x;SLPI=0x%x",
		asus_rpm_count[0], asus_rpm_count[1], asus_rpmh_counts[0],
		asus_rpmh_counts[1], asus_rpmh_counts[2],
		asus_rpmh_counts[3], asus_rpmh_counts[4]);
}

static char *dual_charge_type_str[]={
		[ASUS_2A]="ASUS_2A",
		[TYPEC_3A]="TYPEC_3A",
		[SINGLE]="SINGLE",
		[UNDEFINE]="UNDEFINE",
};
extern void asp1690_enable(bool enable);
static char *asus_charging_type_str[]={
	[PD]	="PD",
	[HVDCP_ASUS_200K_2A]	="HVDCP_ASUS_200K_2A",	
	[HVDCP_OTHERS_1A]	="HVDCP_OTHERS_1A",
	[HVDCP_OTHERS_1P5A]	="HVDCP_OTHERS_1P5A",
	[HVDCP_OTHERS_PB_1A]	="HVDCP_OTHERS_PB_1A",
	[DCP_ASUS_750K_2A]	="DCP_ASUS_750K_2A",
	[DCP_ASUS_200K_2A]	="DCP_ASUS_200K_2A",
	[DCP_PB_2A]	="DCP_PB_2A",
	[TYPEC_1P5A]	="TYPEC_1P5A",
	[TYPEC_3P0A]	="TYPEC_3P0A",
	[DCP_OTHERS_1A]	="DCP_OTHERS_1A",
	[DCP_2A_BR_IN]	="DCP_2A_BR_IN",
	[SDP_0P5A]	="SDP_0P5A",
	[CDP_1P5A]	="CDP_1P5A",
	[FLOATING_0P5A] ="FLOATING_0P5A",
	[OTHERS_1A]	="OTHERS_1A",
	[UNDEFINED]	="UNDEFINED",
	[NONE]	="NONE",
};

static int asus_update_all(struct smb_charger *chip, struct battery_info_reply *batt_info)
{
	int batt_temp;

	batt_info->capacity = asus_get_prop_batt_capacity(chip);
	batt_info->voltage_now = asus_get_prop_batt_volt(chip)/1000;
	batt_info->current_now = asus_get_prop_batt_current(chip)/1000;
	batt_info->fcc = 3300;
	batt_temp = asus_get_prop_batt_temp(chip);
	if (batt_temp >= 10 || batt_temp <= -10) {
		batt_info->temperature10 = batt_temp / 10;
		batt_info->temperature = batt_temp - (batt_info->temperature10 * 10);
		if (batt_info->temperature < 0) {
			batt_info->temperature = -batt_info->temperature;
		}
		batt_info->temperature_negative_sign = "";
	} else {
		batt_info->temperature10 = 0;
		batt_info->temperature = batt_temp < 0 ? -batt_temp : batt_temp;
		if (batt_temp >= 0) {
			batt_info->temperature_negative_sign = "";
		}
	}

	batt_info->status = asus_get_prop_batt_status(chip);

	return 0;
}

static int asus_print_all(void)
{
	u8 reg;
	u8 value[6];
	char battInfo[256];
	u8 val,thd_hot,thd_2hot,therm_sts;
	struct battery_info_reply batt_info;
	char batt_status_str[7][7] = {
		"UNKN",
		"CHRG",
		"DISC",
		"NOTC",
		"FULL",
		"QUICK",
		"NOTQUK"
	};
	static int cnt = 0;

	asus_update_all(&chip_dev->chg, &batt_info);
	smblib_read(&chip_dev->chg, BATTERY_CHARGER_STATUS_1_REG, &reg);

	snprintf(battInfo, sizeof(battInfo), "[BATT]FCC:%dmAh, BMS:%d, V:%dmV, Cur:%dmA, Temp:%s%d.%dC, ",
		batt_info.fcc,
		batt_info.capacity,
		batt_info.voltage_now,
		batt_info.current_now,
		batt_info.temperature_negative_sign,
		batt_info.temperature10,
		batt_info.temperature);
	snprintf(battInfo, sizeof(battInfo), "%sCable:%d(%s), Dual:%s, Status:%s[0x%x], TLevel:%d\n",
		battInfo,
		chip_dev->chg.asus_chg->asus_charging_type,
		asus_charging_type_str[chip_dev->chg.asus_chg->asus_charging_type],
		dual_charge_type_str[chip_dev->chg.asus_chg->dual_charge],
		batt_status_str[batt_info.status],
		reg,
		chip_dev->chg.system_temp_level);

	printk(KERN_INFO "%s", battInfo);
	if(chip_dev->chg.asus_capacity != batt_info.capacity){
		chip_dev->chg.asus_capacity = batt_info.capacity;
		ASUSEvtlog("[BAT][Ser]report Capacity==>%d,FCC:%d,BMS:%d,V:%d,Cur:%d,Temp:%s%d.%dC,Cable:%d(%s),Status:%s\n", 
			batt_info.capacity,
			batt_info.fcc,
			batt_info.capacity,
			batt_info.voltage_now,
			batt_info.current_now,
			batt_info.temperature_negative_sign,
			batt_info.temperature10,
			batt_info.temperature,
			chip_dev->chg.asus_chg->asus_charging_type,
			asus_charging_type_str[chip_dev->chg.asus_chg->asus_charging_type],
			batt_status_str[batt_info.status]);
		asus_dump_sleep_count();
	}

	smblib_read(&chip_dev->chg, 0x4582, &val);
	smblib_read(&chip_dev->chg, 0x4586, &thd_hot);
	smblib_read(&chip_dev->chg, 0x4587, &thd_2hot);
	smblib_read(&chip_dev->chg, TEMP_RANGE_STATUS_REG, &therm_sts);
	CHG_DBG("INOV info: raw:0x%x, thd(%d,%d), trigger(%d,%d), regulation(%d)\n",
               val,(int)(thd_hot>>1)-30,(int)(thd_2hot>>1)-30,
               val&BIT(4)?1:0, val&BIT(5)?1:0, therm_sts&BIT(6)?1:0);

    if(((val&BIT(4)?1:0)||(val&BIT(5)?1:0)) && (therm_sts&BIT(6)?1:0))
    {
        if(NONE == chip_dev->chg.asus_chg->asus_charging_type)
            cnt = 0;
        if(cnt%5 == 0) {
            cnt = 0;
            ASUSEvtlog("INOV Triggered pmi_THERM_STS[4582]=0x%x, TEMP_RANGE_STS[1606]=0x%x\n",val, therm_sts);
        }
        cnt++;
    }

	smblib_read(&chip_dev->chg, FAST_CHARGE_CURRENT_CFG_REG, &value[0]);	       //fcc
	smblib_read(&chip_dev->chg, FLOAT_VOLTAGE_CFG_REG, &value[1]);        //fv
	smblib_read(&chip_dev->chg, USBIN_CURRENT_LIMIT_CFG_REG, &value[2]);  //icl
	smblib_read(&chip_dev->chg, POWER_PATH_STATUS_REG, &value[3]);  //suspend and patch
	smblib_read(&chip_dev->chg, CHARGING_ENABLE_CMD_REG, &value[4]);  //batt charge enable
	smblib_read(&chip_dev->chg, CHGR_CFG2_REG, &value[5]);  //CHG_EN_POLARITY
	printk(KERN_INFO "[BATT]pmi_fcc[1061] =0x%x, pmi_fv[1070] =0x%x, pmi_icl[1370] =0x%x, pmi_suspend[160B] =0x%x, pmi_chg_en[1042] = 0x%x, chg_en_polarity[1051] =0x%x pmi_THERM_STS[4582]=0x%x\n",
		value[0],
		value[1],
		value[2],
		value[3],
		value[4],
		value[5],
		val);

	smblib_read(&chip_dev->chg, HVDCP_PULSE_COUNT_MAX, &value[0]);       //hvdcp_puls_cnt
	smblib_read(&chip_dev->chg, USBIN_ADAPTER_ALLOW_CFG_REG, &value[1]);        //adp_allow_cfg
	smblib_read(&chip_dev->chg, USBIN_OPTIONS_1_CFG_REG, &value[2]);  //usbin_opt1_cfg
	smblib_read(&chip_dev->chg, USBIN_LOAD_CFG_REG, &value[3]);  //usbin_load_cfg
	smblib_read(&chip_dev->chg, USBIN_ICL_OPTIONS_REG, &value[4]);  //usbin_icl_opt
	smblib_read(&chip_dev->chg, TEMP_RANGE_STATUS_REG, &value[5]);  //therm_sts
	printk(KERN_INFO "[BATT]hvdcp_puls_cnt[135B] =0x%x, adp_allow_cfg[1360] =0x%x, usbin_opt1_cfg[1362] =0x%x, usbin_load_cfg[1365] =0x%x, usbin_icl_opt[1366] = 0x%x, therm_sts[1606] = 0x%x\n",
		value[0],
		value[1],
		value[2],
		value[3],
		value[4],
		value[5]);

	return 0;
}

void asus_polling_data(struct work_struct *work)
{
	int ret;
	struct asus_charger *asus_chip = container_of(work,
					struct asus_charger,
					battery_poll_data_work.work);
	int capacity;

	if(!chip_dev || !asus_chip){
		CHG_DBG_E("smb2 or asus_charger is NULL!\n");
		return ;
	}

	capacity=asus_get_prop_batt_capacity(&chip_dev->chg);
	ret = asus_print_all();

	/*BSP david: if report capacity fail, do it after 5s*/
	if (!ret) {
		if(capacity > 15){
			cancel_delayed_work(&asus_chip->battery_poll_data_work);
			schedule_delayed_work(&asus_chip->battery_poll_data_work, msecs_to_jiffies(60000));
		}else{
			cancel_delayed_work(&asus_chip->battery_poll_data_work);
			schedule_delayed_work(&asus_chip->battery_poll_data_work, msecs_to_jiffies(30000));
		}
	}else{
		cancel_delayed_work(&asus_chip->battery_poll_data_work);
		schedule_delayed_work(&asus_chip->battery_poll_data_work, msecs_to_jiffies(30000));
	}
}

void add_asus_charger_log(struct smb2 *chip)
{
	struct asus_charger *asus_chip = chip->chg.asus_chg;

	INIT_DELAYED_WORK(&asus_chip->battery_poll_data_work, asus_polling_data);
	schedule_delayed_work(&asus_chip->battery_poll_data_work, 30 * HZ);
}

bool is_apsd_done(struct smb_charger *chg)
{
	int rc;
	u8 apsd_stat;

	rc = smblib_read(chg, APSD_STATUS_REG, &apsd_stat);
	if (rc < 0) {
		smblib_err(chg, "Couldn't read APSD_STATUS rc=%d\n", rc);
		return false;
	}
    return apsd_stat & APSD_DTC_STATUS_DONE_BIT;
}

int asus_get_prop_usb_present(struct smb_charger *chg)
{
	union power_supply_propval val;
	int rc;

	rc = smblib_get_prop_usb_present(chg, &val);
	if (rc < 0) {
		smblib_err(chg, "Couldn't get usb present rc = %d\n", rc);
		return rc;
	}

	return val.intval;
}

int asus_get_prop_ufp_mode(struct smb_charger *chg)
{
    int ufp_mode;

    ufp_mode = smblib_get_prop_ufp_mode(chg);
    if (ufp_mode == POWER_SUPPLY_TYPEC_NONE)
        return 0;
    else if (ufp_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT)
        return 1;
    else if (ufp_mode == POWER_SUPPLY_TYPEC_SOURCE_MEDIUM)
        return 2;
    else if (ufp_mode == POWER_SUPPLY_TYPEC_SOURCE_HIGH)
        return 3;
    else
        return 4;
}

int asus_smbchg_suspend(struct device *dev)
{
    struct smb2 *chip = dev_get_drvdata(dev);
    int usb_present = asus_get_prop_usb_present(&chip->chg);
    int otg_en = smblib_vbus_regulator_is_enabled(chip->chg.vbus_vreg->rdev);

    pr_info("%s: usb_present = %d, otg_en = %d\n", __func__, usb_present, otg_en);

    if (!usb_present && !otg_en) {
        pr_info("%s: disable asp1690e\n", __func__);
        asp1690_enable(false);
        chip->chg.asus_chg->asp1690_disable_flag = true;
        chip->chg.asus_chg->suspend_flag = true;
    }

	return 0;
}

int asus_smbchg_resume(struct device *dev)
{
    struct smb2 *chip = dev_get_drvdata(dev);

    if (chip->chg.asus_chg->asp1690_disable_flag) {
        pr_info("%s: enable asp1690e\n", __func__);
        asp1690_enable(true);
        chip->chg.asus_chg->asp1690_disable_flag = false;
        chip->chg.asus_chg->suspend_flag = false;
		if (g_boot_complete || g_charger_mode)
			schedule_delayed_work(&chip->chg.asus_chg->set_usb_connector_work, 0);
		}

    return 0;
}

#define charger_inov_enable_PROC_FILE "driver/charger_inov_enable"
static ssize_t charger_inov_enable_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    char messages[256];
    int flag, rc = 0;
    if (len > 256) {
        len = 256;
    }

    memset(messages, 0, sizeof(messages));
    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    sscanf(messages,"%d",&flag);

    if (flag == 0) {
        rc = smblib_write(&chip_dev->chg, THERMREG_SRC_CFG_REG, 0x00);
        if (rc < 0)
            pr_err("Couldn't set THERMREG_SRC_CFG_REG 0x0\n");
        CHG_DBG("%s: Disable INOV function\n", __func__);
    } else if (flag == 1) {
        rc = smblib_write(&chip_dev->chg, THERMREG_SRC_CFG_REG, 0x07);
        if (rc < 0)
            pr_err("Couldn't set THERMREG_SRC_CFG_REG 0x7\n");
        CHG_DBG("%s: Enable INOV function\n", __func__);
    }

    return len;
}
static int charger_inov_enable_proc_read(struct seq_file *buf, void *data)
{
    int rc;
    u8 reg;

    rc = smblib_read(&chip_dev->chg, THERMREG_SRC_CFG_REG, &reg);
    seq_printf(buf, "INOV_reg 0x1670 = 0x%x\n", reg);

    return 0;
}
static int charger_inov_enable_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, charger_inov_enable_proc_read, NULL);
}
static const struct file_operations charger_inov_enable_fops = {
    .owner = THIS_MODULE,
    .open = charger_inov_enable_proc_open,
    .read = seq_read,
    .write = charger_inov_enable_proc_write,
};

static void create_asus_charger_inov_enable_proc_file(void)
{
    struct proc_dir_entry *charger_inov_enable = proc_create(charger_inov_enable_PROC_FILE, 0666, NULL, &charger_inov_enable_fops);

    if(!charger_inov_enable)
        pr_err("creat charger_inov_enable proc inode failed!\n");
}

static char *usb_type_str[] = {
    "NONE",
    "SDP",
    "OTHER",
    "CDP",
    "DCP",
};

void asus_log_chg_mode(u8 apsd_result_bit)
{
    int chg_type = 0;

    switch (apsd_result_bit) {
    case SDP_CHARGER_BIT:
        chg_type = 1;
        break;
    case CDP_CHARGER_BIT:
        chg_type = 3;
        break;
    case OCP_CHARGER_BIT:
    case FLOAT_CHARGER_BIT:
        chg_type = 2;
        break;
    case DCP_CHARGER_BIT:
        chg_type = 4;
        break;
    default:
        break;
    }

    ASUSEvtlog("[USB] set_chg_mode:%s\n", usb_type_str[chg_type]);
}

#define SWITCH_QC_NOT_QUICK_CHARGING_10W	4
#define SWITCH_QC_QUICK_CHARGING_10W		3
#define SWITCH_QC_NOT_QUICK_CHARGING	2
#define SWITCH_QC_QUICK_CHARGING		1
#define SWITCH_QC_OTHER	0
void asus_register_qc_stat(struct smb_charger *chg)
{
    struct asus_charger *asus_chg = chg->asus_chg;
    int ret;

    asus_chg->qc_stat_dev.supported_cable = qc_stat_ext_supported_cable;
    asus_chg->qc_stat_dev.name = "quick_charging";
    dev_set_name(&asus_chg->qc_stat_dev.dev, "quick_charging");
    ret = extcon_dev_register(&asus_chg->qc_stat_dev);
    if (ret) {
        CHG_DBG("%s: quick_charging extcon registration failed\n", __func__);
    } else {
        CHG_DBG("%s: quick_charging extcon registration success\n", __func__);
        asus_chg->qc_stat_registed = true;
    }
}

void asus_set_qc_stat(struct smb_charger *chg, union power_supply_propval *val)
{
    struct asus_charger *asus_chg = chg->asus_chg;
    int stat, set;

    stat = val->intval;
    if (0 == asus_chg->quick_charge_ac_flag) {
        set = SWITCH_QC_OTHER;
        //~ CHG_DBG("stat: %d, switch: %d\n",stat, set);
        extcon_set_state_sync_asus(&asus_chg->qc_stat_dev, set);
        return;
    }

    switch(stat) {
        //"qc" stat happends in charger mode only, refer to smblib_get_prop_batt_status
        case POWER_SUPPLY_STATUS_CHARGING:
        case POWER_SUPPLY_STATUS_NOT_CHARGING:
        case POWER_SUPPLY_STATUS_QUICK_CHARGING:
        case POWER_SUPPLY_STATUS_NOT_QUICK_CHARGING:
        case POWER_SUPPLY_STATUS_QUICK_CHARGING_10W:
        case POWER_SUPPLY_STATUS_NOT_QUICK_CHARGING_10W:
            if(asus_get_prop_batt_capacity(chg) <= 70) {
                if (AC_GT_10W == asus_chg->quick_charge_ac_flag)
                    set = SWITCH_QC_QUICK_CHARGING;
                else
                    set = SWITCH_QC_QUICK_CHARGING_10W;
            } else {
                if (AC_GT_10W == asus_chg->quick_charge_ac_flag)
                    set = SWITCH_QC_NOT_QUICK_CHARGING;
                else
                    set = SWITCH_QC_NOT_QUICK_CHARGING_10W;
            }
                    
            extcon_set_state_sync_asus(&asus_chg->qc_stat_dev, set);
            break;

        default:
            set = SWITCH_QC_OTHER;
            extcon_set_state_sync_asus(&asus_chg->qc_stat_dev, set);
            break;
    }

    //~ CHG_DBG("stat: %d, switch: %d\n",stat, set);
    return;
}

void asus_legacy_det_work(struct work_struct *work)
{
    struct smb_charger * chg = &chip_dev->chg;

	cancel_work(&chg->legacy_detection_work);
	schedule_work(&chg->legacy_detection_work);
}

#define	SMARTCHG_STOP_CHG_PROC_FILE "driver/smartchg_stop_chg"
static int smartchg_stop_chg_proc_read(struct seq_file *buf, void *v)
{
    seq_printf(buf, "smartchg_stop_chg_flag = %d\n", smartchg_stop_chg_flag);
    return 0;
}

static ssize_t smartchg_stop_chg_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    char messages[256];
    int flag = 0;

    if (len > 256) {
        len = 256;
    }

    memset(messages, 0, sizeof(messages));
    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    sscanf(messages,"%d",&flag);

    if (0 == flag) {
        smartchg_stop_chg_flag = false;
        vote(chip_dev->chg.chg_disable_votable, SMARTCHG_VOTER, false, 0);
        CHG_DBG("%s: smart charge enable charging\n",__func__);
    } else {
        smartchg_stop_chg_flag = true;
        vote(chip_dev->chg.chg_disable_votable, SMARTCHG_VOTER, true, 0);
        CHG_DBG("%s: smart charge disable charging\n",__func__);
    }

    return len;
}

static int smartchg_stop_chg_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, smartchg_stop_chg_proc_read, NULL);
}

static const struct file_operations smartchg_stop_chg_fops = {
    .owner = THIS_MODULE,
    .open =  smartchg_stop_chg_proc_open,
    .write = smartchg_stop_chg_proc_write,
    .read = seq_read,
    .release = single_release,
};

static void create_smartchg_stop_chg_proc_file(void)
{
    struct proc_dir_entry *smartchg_stop_chg_proc_file = proc_create(SMARTCHG_STOP_CHG_PROC_FILE, 0777, NULL, &smartchg_stop_chg_fops);

    if (smartchg_stop_chg_proc_file) {
    } else{
        pr_info("[SMB][CHG]%s fail\n",__FUNCTION__);
    }

    return;
}

#define VBAT_SAMPLE_COUNT 5

void asus_vbat_avg_work(struct work_struct *work)
{
    struct asus_charger *asus_chg = container_of(work,
                    struct asus_charger,
                    vbat_avg_work.work);
	int rc = 0;
	int vbat = 0;
	int cbat = 0;
	int avg = 0;
	static int sum = 0;
	static int count = 0;

	vbat = asus_get_prop_batt_volt(&chip_dev->chg);
	cbat = asus_get_prop_batt_current(&chip_dev->chg);
	sum += vbat;
	count++;
	CHG_DBG("%s: vbat(%d) = %d, cbat = %d\n", __func__, count, vbat/1000, cbat/1000);

	if (count < VBAT_SAMPLE_COUNT) {
		schedule_delayed_work(&asus_chg->vbat_avg_work, msecs_to_jiffies(1000));
	} else {
		avg = sum / VBAT_SAMPLE_COUNT;
		CHG_DBG("%s: Average vbat in %d seconds = %d mV\n", __func__, VBAT_SAMPLE_COUNT, avg/1000);
		if (1 == asus_fcc_override_flag) {
			vbat_avg_1 = avg;
		} else if (2 == asus_fcc_override_flag) {
			vbat_avg_2 = avg;
		} else {
			CHG_DBG_E("%s: override flag error\n", __func__);
		}
		sum = 0;
		count = 0;
		rc = vote(chip_dev->chg.fcc_votable, FCC_OVERRIDE_VOTER, false, 0);
		if (rc < 0) {
			CHG_DBG_E("%s: Couldn't set fast charge current rc=%d\n", __func__, rc);
		}
		asus_fcc_override_flag = 0;
		asus_get_vbat_in_process_flag = 0;
		cancel_delayed_work(&chip_dev->chg.asus_chg->asus_batt_temp_work);
		schedule_delayed_work(&chip_dev->chg.asus_chg->asus_batt_temp_work,0);
	}
}

void asus_fcc_override_work(struct work_struct *work)
{
    struct asus_charger *asus_chg = container_of(work,
                    struct asus_charger,
                    fcc_override_work.work);
	int rc = 0;

	if (0 == asus_fcc_override_flag)
		return;

	if (fcc_override_setting < 0) {
		CHG_DBG("%s: fcc out of range!\n", __func__);
		return;
	}

    rc = vote(chip_dev->chg.fcc_votable, FCC_OVERRIDE_VOTER, true, fcc_override_setting);
    if (rc < 0) {
        CHG_DBG_E("%s: Couldn't set fast charge current rc=%d\n", __func__, rc);
    }

	schedule_delayed_work(&asus_chg->vbat_avg_work, msecs_to_jiffies(5000));
	CHG_DBG("%s: Start measure vbat in 5s, target fcc=%d mA, override_flag = %d\n", __func__, fcc_override_setting/1000, asus_fcc_override_flag);
}

#define	VBAT_FCC_SETTING_1_PROC_FILE "driver/vbat_fcc_setting_1"
static int vbat_fcc_setting_1_proc_read(struct seq_file *buf, void *v)
{
    seq_printf(buf, "FCC override setting 1 = %d mA\n", ptc_check_fcc_override_1/1000);
    return 0;
}

static ssize_t vbat_fcc_setting_1_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    char messages[256];
    int fcc_setting = 0;

    if (len > 256) {
        len = 256;
    }

    memset(messages, 0, sizeof(messages));
    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    sscanf(messages,"%d",&fcc_setting);

	if (!asus_get_vbat_in_process_flag) {
		if (fcc_setting < 0) {
			CHG_DBG_E("%s: fcc setting 1 cannot below zero\n", __func__);
			return len;
		}
		ptc_check_fcc_override_1 = fcc_setting;
		CHG_DBG("%s: set fcc_override_setting_1 to %d\n",__func__, ptc_check_fcc_override_1);
	}

    return len;
}

static int vbat_fcc_setting_1_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, vbat_fcc_setting_1_proc_read, NULL);
}

static const struct file_operations vbat_fcc_setting_1_fops = {
    .owner = THIS_MODULE,
    .open =  vbat_fcc_setting_1_proc_open,
    .write = vbat_fcc_setting_1_proc_write,
    .read = seq_read,
    .release = single_release,
};

static void create_vbat_fcc_setting_1_proc_file(void)
{
    struct proc_dir_entry *vbat_fcc_setting_1_proc_file = proc_create(VBAT_FCC_SETTING_1_PROC_FILE, 0777, NULL, &vbat_fcc_setting_1_fops);

    if (vbat_fcc_setting_1_proc_file) {
    } else{
        pr_info("[SMB][CHG]%s fail\n",__FUNCTION__);
    }

    return;
}

#define	VBAT_FCC_SETTING_2_PROC_FILE "driver/vbat_fcc_setting_2"
static int vbat_fcc_setting_2_proc_read(struct seq_file *buf, void *v)
{
    seq_printf(buf, "FCC override setting 2 = %d mA\n", ptc_check_fcc_override_2/1000);
    return 0;
}

static ssize_t vbat_fcc_setting_2_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    char messages[256];
    int fcc_setting = 0;

    if (len > 256) {
        len = 256;
    }

    memset(messages, 0, sizeof(messages));
    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    sscanf(messages,"%d",&fcc_setting);

	if (!asus_get_vbat_in_process_flag) {
		if (fcc_setting < 0) {
			CHG_DBG_E("%s: fcc setting 2 cannot below zero\n", __func__);
			return len;
		}
		ptc_check_fcc_override_2 = fcc_setting;
		CHG_DBG("%s: set fcc_override_setting_2 to %d\n",__func__, ptc_check_fcc_override_2);
	}

    return len;
}

static int vbat_fcc_setting_2_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, vbat_fcc_setting_2_proc_read, NULL);
}

static const struct file_operations vbat_fcc_setting_2_fops = {
    .owner = THIS_MODULE,
    .open =  vbat_fcc_setting_2_proc_open,
    .write = vbat_fcc_setting_2_proc_write,
    .read = seq_read,
    .release = single_release,
};

static void create_vbat_fcc_setting_2_proc_file(void)
{
    struct proc_dir_entry *vbat_fcc_setting_2_proc_file = proc_create(VBAT_FCC_SETTING_2_PROC_FILE, 0777, NULL, &vbat_fcc_setting_2_fops);

    if (vbat_fcc_setting_2_proc_file) {
    } else{
        pr_info("[SMB][CHG]%s fail\n",__FUNCTION__);
    }

    return;
}

#define	VBAT_FCC_TEST_PROC_FILE "driver/vbat_fcc_test"
static int vbat_fcc_test_proc_read(struct seq_file *buf, void *v)
{
    seq_printf(buf, "FCC override flag = %d\nFCC override setting = %d mA\nMeasure in process = %d\n",
		asus_fcc_override_flag, fcc_override_setting/1000, asus_get_vbat_in_process_flag);
    return 0;
}

static ssize_t vbat_fcc_test_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    char messages[256];
    int flag = 0;

    if (len > 256) {
        len = 256;
    }

    memset(messages, 0, sizeof(messages));
    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    sscanf(messages,"%d",&flag);

	if (!asus_get_vbat_in_process_flag) {
		if(1 == flag) {
			asus_fcc_override_flag = 1;
			fcc_override_setting = ptc_check_fcc_override_1;
			asus_get_vbat_in_process_flag = true;
			vbat_avg_1 = 0;
		} else if (2 == flag) {
			asus_fcc_override_flag = 2;
			fcc_override_setting = ptc_check_fcc_override_2;
			asus_get_vbat_in_process_flag = true;
			vbat_avg_2 = 0;
		} else {
			asus_fcc_override_flag = 0;
			fcc_override_setting = -1;
		}
		pr_info("[SMB][CHG]%s asus_fcc_override_flag=%d, fcc_override_setting=%d\n",__func__, asus_fcc_override_flag, fcc_override_setting);
		cancel_delayed_work(&chip_dev->chg.asus_chg->fcc_override_work);
		schedule_delayed_work(&chip_dev->chg.asus_chg->fcc_override_work,0*HZ);
	}

    return len;
}

static int vbat_fcc_test_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, vbat_fcc_test_proc_read, NULL);
}

static const struct file_operations vbat_fcc_test_fops = {
    .owner = THIS_MODULE,
    .open =  vbat_fcc_test_proc_open,
    .write = vbat_fcc_test_proc_write,
    .read = seq_read,
    .release = single_release,
};

static void create_vbat_fcc_test_proc_file(void)
{
    struct proc_dir_entry *vbat_fcc_test_proc_file = proc_create(VBAT_FCC_TEST_PROC_FILE, 0777, NULL, &vbat_fcc_test_fops);

    if (vbat_fcc_test_proc_file) {
    } else{
        pr_info("[SMB][CHG]%s fail\n",__FUNCTION__);
    }

    return;
}

#define VBAT_AVG_1_PROC_FILE "driver/vbat_avg_1"
static int vbat_avg_1_proc_read(struct seq_file *buf, void *data)
{
	int cnt = 0;
	if (1 == asus_fcc_override_flag) {
		while (asus_get_vbat_in_process_flag && (11 != cnt++) ) {
			msleep(1000);
		}
	}
	seq_printf(buf, "%d\n", vbat_avg_1);
	return 0;
}
static int vbat_avg_1_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, vbat_avg_1_proc_read, NULL);
}
static const struct file_operations vbat_avg_1_fops = {
	.owner = THIS_MODULE,
	.open = vbat_avg_1_proc_open,
	.read = seq_read,
};

static void create_vbat_avg_1_proc_file(void)
{
	struct proc_dir_entry *vbat_avg_1 = proc_create(VBAT_AVG_1_PROC_FILE, 0444, NULL, &vbat_avg_1_fops);

	if(!vbat_avg_1)
		pr_err("creat vbat_avg_1 proc inode failed!\n");
}

#define VBAT_AVG_2_PROC_FILE "driver/vbat_avg_2"
static int vbat_avg_2_proc_read(struct seq_file *buf, void *data)
{
	int cnt = 0;
	if (2 == asus_fcc_override_flag) {
		while (asus_get_vbat_in_process_flag && (11 != cnt++) ) {
			msleep(1000);
		}
	}
	seq_printf(buf, "%d\n", vbat_avg_2);
	return 0;
}
static int vbat_avg_2_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, vbat_avg_2_proc_read, NULL);
}
static const struct file_operations vbat_avg_2_fops = {
	.owner = THIS_MODULE,
	.open = vbat_avg_2_proc_open,
	.read = seq_read,
};

static void create_vbat_avg_2_proc_file(void)
{
	struct proc_dir_entry *vbat_avg_2 = proc_create(VBAT_AVG_2_PROC_FILE, 0444, NULL, &vbat_avg_2_fops);

	if(!vbat_avg_2)
		pr_err("creat vbat_avg_2 proc inode failed!\n");
}

#define	THERMAL_TEST_CHG_PROC_FILE "driver/thermal_test_chg"
static int thermal_test_chg_proc_read(struct seq_file *buf, void *v)
{
    seq_printf(buf, "thermal test keep charging = %d\n",asus_thermal_keep_chg_flag);
    return 0;
}

static ssize_t thermal_test_chg_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    char messages[256];
    int flag = 0;

    if (len > 256) {
        len = 256;
    }

    memset(messages, 0, sizeof(messages));
    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    sscanf(messages,"%d",&flag);

    if(0 == flag)
        asus_thermal_keep_chg_flag = false;
    else
        asus_thermal_keep_chg_flag = true;

    pr_info("[Charger]%s asus_thermal_keep_chg_flag=%d\n",__FUNCTION__,asus_thermal_keep_chg_flag);
    cancel_delayed_work(&chip_dev->chg.asus_chg->asus_batt_temp_work);
    schedule_delayed_work(&chip_dev->chg.asus_chg->asus_batt_temp_work,0*HZ);

    return len;
}

static int thermal_test_chg_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, thermal_test_chg_proc_read, NULL);
}

static const struct file_operations thermal_test_chg_fops = {
    .owner = THIS_MODULE,
    .open =  thermal_test_chg_proc_open,
    .write = thermal_test_chg_proc_write,
    .read = seq_read,
    .release = single_release,
};

static void create_thermal_test_chg_proc_file(void)
{
    struct proc_dir_entry *thermal_test_chg_proc_file = proc_create(THERMAL_TEST_CHG_PROC_FILE, 0777, NULL, &thermal_test_chg_fops);

    if (thermal_test_chg_proc_file) {
    } else{
        pr_info("[Charger]%s fail\n",__FUNCTION__);
    }

    return;
}

#define	FAKE_BATT_CAPACITY_PROC_FILE "driver/fake_batt_capacity"
static int fake_batt_capacity_proc_read(struct seq_file *buf, void *v)
{
    seq_printf(buf, "fake battery capacity = %d\n", chip_dev->chg.fake_capacity);
    return 0;
}

static ssize_t fake_batt_capacity_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    char messages[256];
    int capacity = 0;
	union power_supply_propval temp_val = {0, };

    if (len > 256) {
        len = 256;
    }

    memset(messages, 0, sizeof(messages));
    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    sscanf(messages,"%d",&capacity);

    if(capacity >= 0 && capacity <= 100) {
        temp_val.intval = capacity;
	}
    else {
        temp_val.intval = -EINVAL;
	}

	smblib_set_prop_batt_capacity(&chip_dev->chg, &temp_val);

    pr_info("[Charger]%s fake battery capacity = %d\n",__FUNCTION__, temp_val.intval);

    return len;
}

static int fake_batt_capacity_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, fake_batt_capacity_proc_read, NULL);
}

static const struct file_operations fake_batt_capacity_fops = {
    .owner = THIS_MODULE,
    .open =  fake_batt_capacity_proc_open,
    .write = fake_batt_capacity_proc_write,
    .read = seq_read,
    .release = single_release,
};

static void create_fake_batt_capacity_proc_file(void)
{
    struct proc_dir_entry *fake_batt_capacity_proc_file = proc_create(FAKE_BATT_CAPACITY_PROC_FILE, 0777, NULL, &fake_batt_capacity_fops);

    if (fake_batt_capacity_proc_file) {
    } else{
        pr_info("[Charger]%s fail\n",__FUNCTION__);
    }

    return;
}

#define charger_ntc_PROC_FILE "driver/charger_ntc"
static int charger_ntc_proc_read(struct seq_file *buf, void *data)
{
	int temp_val;
	int rc = -1;

	if(!chip_dev){
		pr_err("chip is NULL !\n");
		seq_printf(buf, "0\n");
		return rc;
	}

	if (!chip_dev->chg.asus_chg->aux_therm_chan ||
		PTR_ERR(chip_dev->chg.asus_chg->aux_therm_chan) == -EPROBE_DEFER)
		chip_dev->chg.asus_chg->aux_therm_chan = iio_channel_get(chip_dev->chg.dev, "aux_therm");

	if (IS_ERR(chip_dev->chg.asus_chg->aux_therm_chan))
		return PTR_ERR(chip_dev->chg.asus_chg->aux_therm_chan);

	rc = iio_read_channel_processed(chip_dev->chg.asus_chg->aux_therm_chan, &temp_val);
	if (rc < 0) {
		seq_printf(buf, "1\n");
		pr_err("proc read aux_therm vaule failed!\n");
		return rc;
	}

	seq_printf(buf, "%d\n", temp_val);
	return 0;
}
static int charger_ntc_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, charger_ntc_proc_read, NULL);
}
static const struct file_operations charger_ntc_fops = {
	.owner = THIS_MODULE,
	.open = charger_ntc_proc_open,
	.read = seq_read,
};

static void create_asus_charger_ntc_proc_file(void)
{
	struct proc_dir_entry *charger_ntc = proc_create(charger_ntc_PROC_FILE, 0444, NULL, &charger_ntc_fops);

	if(!charger_ntc)
		pr_err("creat charger_ntc proc inode failed!\n");
}

#define chargerIC_status_PROC_FILE "driver/chargerIC_status"
static int chargerIC_status_proc_read(struct seq_file *buf, void *data)
{
	u8 reg;
	int rc = -1;

	if(!chip_dev){
		pr_err("chip is NULL !\n");
		seq_printf(buf, "0\n");
		return rc;
	}

	rc = smblib_read(&chip_dev->chg, BATTERY_CHARGER_STATUS_1_REG, &reg);
	if (rc < 0) {
		seq_printf(buf, "0\n");
		pr_err("proc read CHGR_STS vaule failed!\n");
		return rc;
	}

	seq_printf(buf, "1\n");
	return 0;
}
static int chargerIC_status_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, chargerIC_status_proc_read, NULL);
}
static const struct file_operations chargerIC_status_fops = {
	.owner = THIS_MODULE,
	.open = chargerIC_status_proc_open,
	.read = seq_read,
};

static void create_asus_chargerIC_status_proc_file(void)
{
	struct proc_dir_entry *chargerIC_status = proc_create(chargerIC_status_PROC_FILE, 0444, NULL, &chargerIC_status_fops);

	if(!chargerIC_status)
		pr_err("creat chargerIC_status proc inode failed!\n");
}

#define ASUS_BATT_CHARGE_ENABLE_PROC_FILE "driver/batt_charge_enable"
static int asus_batt_charge_enable_proc_read(struct seq_file *buf, void *data)
{
    seq_printf(buf, "%d\n",charger_batt_enable);
    return 0;
}
static ssize_t asus_batt_charge_enable_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	char messages[256];
    int flag;
	if (len > 256) {
		len = 256;
	}

    memset(messages, 0, sizeof(messages));
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}

	sscanf(messages,"%d",&flag);

	if (flag == 1) {
		charger_batt_enable=1;
		printk("[BAT][CHG][SMB][Proc]batt charger_enable");
	} else if(flag == 0) {
		charger_batt_enable=0;
		printk("[BAT][CHG][SMB][Proc]batt charger_disable");
	}else{
	    printk("[BAT][CHG][SMB][Proc]input error");
	}
	//~ vote(the_chip->usb_suspend_votable, ATD_CMD_VOTER, !charger_batt_enable, 0);
    smblib_set_usb_suspend(&chip_dev->chg, !charger_batt_enable);
	cancel_delayed_work(&chip_dev->chg.asus_chg->asus_batt_temp_work);
	schedule_delayed_work(&chip_dev->chg.asus_chg->asus_batt_temp_work,0);
	return len;
}
static int asus_batt_charge_enable_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, asus_batt_charge_enable_proc_read, NULL);
}
static const struct file_operations asus_batt_charge_enable_fops = {
	.owner = THIS_MODULE,
	.open = asus_batt_charge_enable_proc_open,
	.write = asus_batt_charge_enable_proc_write,
	.read = seq_read,
	.release = single_release,
};

static void create_asus_batt_charge_enable_proc_file(void)
{
	struct proc_dir_entry *asus_batt_charge_enable_proc_file = proc_create(ASUS_BATT_CHARGE_ENABLE_PROC_FILE, 0777, NULL, &asus_batt_charge_enable_fops);

	if (asus_batt_charge_enable_proc_file) {
		printk("[BAT][CHG][SMB][Proc]ASUS_BATT_CHARGE_TYPE_PROC_FILE create ok!\n");
	} else{
		printk("[BAT][CHG][SMB][Proc]ASUS_BATT_CHARGE_TYPE_PROC_FILE create failed!\n");
	}
}


#define	CHARGING_LIMIT_PROC_FILE "driver/charging_limit"

void charger_limit_update_work(int time)
{
    cancel_delayed_work(&charging_limit_work);
    schedule_delayed_work(&charging_limit_work, time * HZ);
}

static int charger_limit_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n",charger_limit_setting);
	return 0;
}

static ssize_t charger_limit_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	char messages[256];

	if (len > 256) {
		len = 256;
	}

    memset(messages, 0, sizeof(messages));
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}

	sscanf(messages,"%d",&charger_limit_setting);

	if(charger_limit_setting<10)
		charger_limit_setting=10;
	else if(charger_limit_setting>100)
		charger_limit_setting=100;
	
	pr_info("[Charger]%s charger_limit_setting=%d charger_limit_enable=%d\n",__FUNCTION__,charger_limit_setting,charger_limit_enable);
	if (charger_limit_enable)
		charger_limit_update_work(0);
	else {
		cancel_delayed_work(&chip_dev->chg.asus_chg->asus_batt_temp_work);
		schedule_delayed_work(&chip_dev->chg.asus_chg->asus_batt_temp_work,0*HZ);
	}

	return len;
}

static int charger_limit_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, charger_limit_proc_read, NULL);
}

static const struct file_operations charger_limit_fops = {
	.owner = THIS_MODULE,
	.open =  charger_limit_proc_open,
	.write = charger_limit_proc_write,
	.read = seq_read,
	.release = single_release,
};

static void create_charger_limit_proc_file(void)
{
	struct proc_dir_entry *charger_limit_proc_file = proc_create(CHARGING_LIMIT_PROC_FILE, 0777, NULL, &charger_limit_fops);

	if (charger_limit_proc_file) {
	} else{
			pr_info("[Charger]%s fail\n",__FUNCTION__);
	}

	return;
}

#ifndef ASUS_FACTORY_BUILD
#ifndef ASUS_SKU_CN
//<asus demonapp>
#define ADF_PATH "/vendor/ADF/ADF"
static mm_segment_t oldfs;
static void initKernelEnv(void)
{
	oldfs = get_fs();
	set_fs(KERNEL_DS);
}

static void deinitKernelEnv(void)
{
	set_fs(oldfs);
}

static bool Check_ADF_Value(void)
{
	uint8_t buf[32] = {0};
	int ret = 0,i=0;
	off_t fsize;
	loff_t pos;
	struct inode *inode;
	struct file *fd;

	initKernelEnv();
	fd = filp_open(ADF_PATH, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(fd)) {
		pr_info("%s open (%s) failed\n",__FUNCTION__,ADF_PATH);
		deinitKernelEnv();
		return false;
	}

	inode = fd->f_path.dentry->d_inode;
	fsize = inode->i_size;
	pos = 0;
	pr_err("%s fsize = %d\n",__FUNCTION__,(int)fsize);
	ret = vfs_read(fd, buf, fsize, &pos);
	if (ret < 0) {
		pr_info("%s read (%s) failed\n",__FUNCTION__,ADF_PATH);
		deinitKernelEnv();
		filp_close(fd, NULL);
		//kfree(buf);
		return false;
	}
	deinitKernelEnv();
	filp_close(fd,NULL);
	pr_err("%s filp_close\n",__FUNCTION__);
	for(i=0;i<fsize;i++)
		pr_info("%s,buf[%d]:%d\n",__FUNCTION__,i,buf[i]);

	if (buf[3] == 0x1 || buf[3] == 0x2)
		return true;
	else
		return false;
}
//<asus demonapp>
#endif
#endif

#define ASUS_CHARGE_LIMIT_ENABLE_PROC_FILE "driver/charger_limit_enable"

static int asus_charge_limit_enable_proc_read(struct seq_file *buf, void *data)
{
    seq_printf(buf, "%d\n",charger_limit_enable);
    return 0;
}
static ssize_t asus_charge_limit_enable_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	char messages[256];
    int flag;
    //~ int rc;
	if (len > 256) {
		len = 256;
	}

	memset(messages, 0, sizeof(messages));
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}

	sscanf(messages,"%d",&flag);
	if (flag == 1) {
#ifdef ASUS_FACTORY_BUILD
		charger_limit_enable = true;
		charger_flag = true;
		// turn on charging limit in factory mode
		printk("[BAT][CHG][SMB][Proc]charger_enable");
	} else if(flag == 0) {
		charger_limit_enable = false;
		charger_flag = true;
		// turn off charging limit in factory mode
		printk("[BAT][CHG][SMB][Proc]charger_disable");
	}else{
	    printk("[BAT][CHG][SMB][Proc]input error");
	}
	charger_limit_update_work(0);
#else
    #ifndef ASUS_SKU_CN
        if(Check_ADF_Value()){
			pr_err("%s check ADF return true\n",__FUNCTION__);
            charger_limit_enable = true;
            charger_flag= true;
////      ASUSErclog(ASUS_DEMO_APP, "start WW Demo APP");
            pr_info("charge_limit_enable\n");
        }else {
			pr_err("%s check ADF return false\n",__FUNCTION__);
            charger_limit_enable = false;
            charger_flag = true;
            cancel_delayed_work(&charging_limit_work);
////      ASUSErclog(ASUS_DEMO_APP, "start WW Demo APP Error for check ADF");
            pr_info("charge_limit_disable_1\n");
        }
    #else
		charger_limit_enable = true;
		charger_flag= true;
////	ASUSErclog(ASUS_DEMO_APP, "start CN Demo APP");
		pr_info("charge_limit_enable\n");
    #endif
	} else if(flag == 0) {
		charger_limit_enable = false;
		charger_flag = true;
		cancel_delayed_work(&charging_limit_work);
////	ASUSErclog(ASUS_DEMO_APP, "stop Demo APP");
		pr_info("charge_limit_disable_0\n");
	} else {
		pr_info("%s input error",__FUNCTION__);
	}
	if(charger_limit_enable == 0){
        vote(chip_dev->chg.chg_disable_votable, DEMO_APP_VOTER, false, 0);
        smblib_set_usb_suspend(&chip_dev->chg, false);
	}
#endif
	return len;
}
static int asus_charge_limit_enable_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, asus_charge_limit_enable_proc_read, NULL);
}
static const struct file_operations asus_charge_limit_enable_fops = {
	.owner = THIS_MODULE,
	.open = asus_charge_limit_enable_proc_open,
	.write = asus_charge_limit_enable_proc_write,
	.read = seq_read,
	.release = single_release,
};

static void create_asus_charge_limit_enable_proc_file(void)
{
	struct proc_dir_entry *asus_charge_limit_enable_proc_file = proc_create(ASUS_CHARGE_LIMIT_ENABLE_PROC_FILE, 0777, NULL, &asus_charge_limit_enable_fops);

	if (asus_charge_limit_enable_proc_file) {
	} else{
		pr_info("%s fail",__FUNCTION__);
	}
}

void asus_battery_charging_limit(struct work_struct *dat)
{
    int percentage;
    int rc;
    static bool enable_usb_suspend = false;
    printk("[%s],charger_limit_enable = %d,charger_limit_setting=%d\n",__FUNCTION__,charger_limit_enable,charger_limit_setting);
    percentage = asus_get_prop_batt_capacity(&chip_dev->chg);
    if (charger_limit_enable) {
        if (percentage < charger_limit_setting - 5) {
            charger_flag = true;
        }else if(percentage > charger_limit_setting){//if percentage>60 usb suspend
            enable_usb_suspend = true;
            charger_flag = false;
            goto USB_SUSPEND;
        }else if((percentage >= charger_limit_setting - 2) && (percentage <= charger_limit_setting)){//58% ~ 60%
            charger_flag = false;
        }
    }

	enable_usb_suspend = false;

	rc = vote(chip_dev->chg.chg_disable_votable, DEMO_APP_VOTER, !charger_flag, 0);
    //~ rc = smblib_write(&chip_dev->chg, CHARGING_ENABLE_CMD_REG, charger_flag ? 0 : 1);
    
	if(rc < 0)
	{
		pr_info("charger batt_suspend disable failed\n");
	}
USB_SUSPEND:
	if(enable_usb_suspend)
        smblib_set_usb_suspend(&chip_dev->chg, true);
		//~ vote(the_chip->usb_suspend_votable, DEMO_APP_USB_VOTER, 1, 0);
	else if (!enable_usb_suspend && charger_batt_enable)
        smblib_set_usb_suspend(&chip_dev->chg, false);
		//~ vote(the_chip->usb_suspend_votable, DEMO_APP_USB_VOTER, 0, 0);

	printk("charger_flag=%d;enable_usb_suspend=%d;percentage=%d\n",
		charger_flag, enable_usb_suspend, percentage);

	charger_limit_update_work(60);
}

#define ASUS_CHARGE_TYPE_PROC_FILE "driver/asus_charge_type"

static int asus_charge_type_proc_read(struct seq_file *buf, void *data)
{
    seq_printf(buf, "%s\n", asus_charging_type_str[chip_dev->chg.asus_chg->asus_charging_type]);

	return 0;
}
static int asus_charge_type_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, asus_charge_type_proc_read, NULL);
}
static const struct file_operations asus_charge_type_fops = {
	.owner = THIS_MODULE,
	.open = asus_charge_type_proc_open,
	.read = seq_read,
};
static void create_asus_charge_type_proc_file(void)
{
	struct proc_dir_entry *asus_charge_type_proc_file = proc_create(ASUS_CHARGE_TYPE_PROC_FILE, 0666, NULL, &asus_charge_type_fops);

	if (asus_charge_type_proc_file) {
		printk("[BAT][CHG][SMB][Proc]ASUS_CHARGE_TYPE_PROC_FILE create ok!\n");
	} else{
		printk("[BAT][CHG][SMB][Proc]ASUS_CHARGE_TYPE_PROC_FILE create failed!\n");
	}
}

#define QC3_WA_WAIT_TIME 6000// ms, wait for pd hard reset completion
void asus_hvdcp3_wa(struct smb_charger *chg)
{
    int rc = 0;

    if (ASUS_ADAPTER_ID == ASUS_200K && chg->asus_chg->asus_charging_type != HVDCP_ASUS_200K_2A) {
        CHG_DBG("%s: Fix HVDCP3 sign from ASUS_200K\n", __func__);
        chg->asus_chg->asus_charging_type = HVDCP_ASUS_200K_2A;
        chg->asus_chg->dual_charge = ASUS_2A;
        chg->asus_chg->quick_charge_ac_flag = AC_GT_10W;
        rc = smblib_masked_write(chg, USBIN_CURRENT_LIMIT_CFG_REG,
            USBIN_CURRENT_LIMIT_MASK, 1650/25);
        if (rc < 0)
            CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);
        chg->asus_chg->last_icl_cfg = 0x42;
        power_supply_changed(chg->batt_psy);
    } else if (!chg->pd_active && asus_flow_done_flag && !chg->asus_chg->adc_redet_flag && ((ASUS_ADAPTER_ID == ADC_NOT_READY) ||
                    !((chg->asus_chg->asus_charging_type == HVDCP_ASUS_200K_2A) ||
                    (chg->asus_chg->asus_charging_type == HVDCP_OTHERS_1P5A)))) { // rerun adc detection for QC3 if ASUS_ID not correctly set
        CHG_DBG_E("%s: rerun adc detection for QC3 which ASUS_ID not correctly set\n", __func__);
        chg->asus_chg->adc_redet_flag = true;
        chg->asus_chg->asus_charging_type = UNDEFINED;
        chg->asus_chg->dual_charge = UNDEFINE;
        ASUS_ADAPTER_ID = ADC_NOT_READY;
        chg->asus_chg->asus_qc_flag = 3;
        //~ asus_adapter_adc_det(chg, true);
        schedule_delayed_work(&chg->asus_chg->asus_qc3_wa_adc_det_work, msecs_to_jiffies(QC3_WA_WAIT_TIME));
    }
}

void Jeita_SetRTC(struct work_struct *dat)
{
	unsigned long jeita_flags;
	struct timespec new_jeita_Alarm_time;
	struct timespec mtNow;
	int RTCSetInterval = 60;

	mtNow = current_kernel_time();
	new_jeita_Alarm_time.tv_sec = 0;
	new_jeita_Alarm_time.tv_nsec = 0;

	RTCSetInterval = 60;

	new_jeita_Alarm_time.tv_sec = mtNow.tv_sec + RTCSetInterval;
	//printk("%s: alarm start after %ds\n", __FUNCTION__, RTCSetInterval);
	spin_lock_irqsave(&jeita_slock, jeita_flags);
	alarm_start(&jeita_alarm,
		timespec_to_ktime(new_jeita_Alarm_time));
	spin_unlock_irqrestore(&jeita_slock, jeita_flags);
}

static enum alarmtimer_restart Jeita_Alarm_handler(struct alarm *alarm, ktime_t now)
{
//	printk("[BAT][alarm]Jeita_Alarm triggered\n");
	if (!chip_dev) {
		CHG_DBG("%s: driver not ready yet!\n", __func__);
		return ALARMTIMER_NORESTART;
	}

	if (asus_get_prop_usb_present(&chip_dev->chg) == true) {
		__pm_wakeup_event(&UsbCable_Lock, jiffies_to_msecs(2 * HZ));
		cancel_delayed_work(&chip_dev->chg.asus_chg->asus_batt_temp_work);
		schedule_delayed_work(&chip_dev->chg.asus_chg->asus_batt_temp_work, 0);
	}

	return ALARMTIMER_NORESTART;
}

static int asus_jeita_judge_state(struct asus_charger *chip, int batt_tempr)
{
	int old_state = chip->soft_jeita_state;
	int result_state = 0;

	//decide value to set each reg (Vchg, Charging enable, Fast charge current)
	if (batt_tempr < chip->asus_tb.jeita_config_table[0]) {
		result_state = JEITA_STATE_RANGE_01;
	} else if (batt_tempr < chip->asus_tb.jeita_config_table[1]) {
		result_state = JEITA_STATE_RANGE_02;
	} else if (batt_tempr < chip->asus_tb.jeita_config_table[2]) {
		result_state = JEITA_STATE_RANGE_03;
	} else if (batt_tempr < chip->asus_tb.jeita_config_table[3]) {
		result_state = JEITA_STATE_RANGE_04;
	} else if (batt_tempr < chip->asus_tb.jeita_config_table[4]) {
		result_state = JEITA_STATE_RANGE_05;
	} else{
		result_state = JEITA_STATE_RANGE_06;
	}

	if (old_state == JEITA_STATE_RANGE_01 && result_state == JEITA_STATE_RANGE_02) {
		if (batt_tempr <= chip->asus_tb.jeita_rechg_table[0]) {
			result_state = old_state;
		}
	}
	if (old_state == JEITA_STATE_RANGE_02 && result_state == JEITA_STATE_RANGE_03) {
		if (batt_tempr <= chip->asus_tb.jeita_rechg_table[1]) {	//130
			result_state = old_state;
		}
	}
	if (old_state == JEITA_STATE_RANGE_03 && result_state == JEITA_STATE_RANGE_04) {
		if (batt_tempr <= chip->asus_tb.jeita_rechg_table[2]) {	//230
			result_state = old_state;
		}
	}
	if (old_state == JEITA_STATE_RANGE_05 && result_state == JEITA_STATE_RANGE_04) {
		if (batt_tempr >= chip->asus_tb.jeita_rechg_table[3]) {	// 470
			result_state = old_state;
		}
	}
	if (old_state == JEITA_STATE_RANGE_06 && result_state == JEITA_STATE_RANGE_05) {
		if (batt_tempr >= chip->asus_tb.jeita_rechg_table[4]) {	// 570
			result_state = old_state;
		}
	}
	chip->soft_jeita_state = result_state;
	return result_state;
}

static void asus_soft_jeita_config(struct asus_charger *chip, int batt_volt, u8 fv_reg,
    bool *charging_enable, int *fcc_value, int *float_volt)
{
	switch (chip->soft_jeita_state) {
		case JEITA_STATE_RANGE_01:
			*charging_enable = false;
			*fcc_value = chip->asus_tb.jeita_fcc_cfg[0];
			*float_volt = chip->asus_tb.fv_cfg;
			CHG_DBG("%s: temperature < %d@range1\n", __FUNCTION__, chip->asus_tb.jeita_config_table[0]);
			break;
		case JEITA_STATE_RANGE_02:
			*charging_enable = true;
			*fcc_value = chip->asus_tb.jeita_fcc_cfg[1];
			*float_volt = chip->asus_tb.fv_cfg;
			CHG_DBG("%s: %d< temperature < %d@range2\n", __FUNCTION__, chip->asus_tb.jeita_config_table[0],
				chip->asus_tb.jeita_config_table[1]);
			break;
		case JEITA_STATE_RANGE_03:
			*charging_enable = true;
			*fcc_value = chip->asus_tb.jeita_fcc_cfg[2];
			*float_volt = chip->asus_tb.fv_cfg;
			CHG_DBG("%s: %d< temperature < %d@range3\n", __FUNCTION__, chip->asus_tb.jeita_config_table[1],
				chip->asus_tb.jeita_config_table[2]);
			break;
		case JEITA_STATE_RANGE_04:
			if(batt_volt > 4250000){
				*fcc_value = chip->asus_tb.jeita_fcc_cfg[5];
			}else if(batt_volt <= 4100000){
				*fcc_value = chip->asus_tb.jeita_fcc_cfg[3];
			}else{
				*fcc_value = chip->asus_tb.jeita_fcc_cfg[4];
			}
			*charging_enable = true;
			*float_volt = chip->asus_tb.fv_cfg;
			CHG_DBG("%s: %d< temperature < %d@range4\n", __FUNCTION__, chip->asus_tb.jeita_config_table[2],
				chip->asus_tb.jeita_config_table[3]);
			break;
		case JEITA_STATE_RANGE_05:
			if((batt_volt >= 4100000) && (fv_reg == 0x74)){
				*charging_enable = false;
				*float_volt = chip->asus_tb.fv_cfg;
				*fcc_value = chip->asus_tb.jeita_fcc_cfg[6];
			}else{
				*charging_enable = true;
				*float_volt = ASUS_FlOAT_4V057;
				*fcc_value = chip->asus_tb.jeita_fcc_cfg[7];
			}
			CHG_DBG("%s: %d < temperature < %d@range5\n", __FUNCTION__, chip->asus_tb.jeita_config_table[3],
				chip->asus_tb.jeita_config_table[4]);
			break;
		case JEITA_STATE_RANGE_06:
			*charging_enable = false;
			*fcc_value = chip->asus_tb.jeita_fcc_cfg[8];
			*float_volt = chip->asus_tb.fv_cfg;
			CHG_DBG("%s: temperature > %d@range6\n", __FUNCTION__, chip->asus_tb.jeita_config_table[4]);
			break;
		default :
			*charging_enable = true;
			*fcc_value = chip->asus_tb.jeita_fcc_cfg[0];
			*float_volt = chip->asus_tb.fv_cfg;
			CHG_DBG("wrong state\n");
		}
}

static void asus_soft_jeita_recharge(struct smb_charger *chg)
{
	int capacity;
	u8 termination_reg, stat;
	bool termination_done = 0;
	int rc;

	rc = smblib_read(chg, BATTERY_CHARGER_STATUS_1_REG, &termination_reg);
	if (rc < 0)
		CHG_DBG_E("%s: Couldn't read BATTERY_CHARGER_STATUS_1_REG\n", __func__);
	if ((termination_reg & BATTERY_CHARGER_STATUS_MASK) == 0x05)
		termination_done = 1;

	capacity = asus_get_prop_batt_capacity(chg);

	CHG_DBG("%s: capacity = %d, termination_reg = 0x%x\n", __func__, capacity, termination_reg);

	if (capacity <= 98 && termination_done) {
        rc = smblib_read(chg, CHGR_CFG2_REG, &stat);
        if (rc < 0) {
            CHG_DBG_E("Couldn't read CHGR_CFG2_REG rc=%d\n", rc);
            return;
        }
        rc = vote(chg->chg_disable_votable, SOFT_JEITA_RECHARGE_VOTER, true, 0);
        if(rc < 0) {
            CHG_DBG_E("%s: charger batt_suspend disable failed, rc=%d\n", __func__, rc);
            return;
        }
        rc = vote(chg->chg_disable_votable, SOFT_JEITA_RECHARGE_VOTER, false, 0);
        if(rc < 0) {
            CHG_DBG_E("%s: charger batt_suspend disable failed, rc=%d\n", __func__, rc);
            return;
        }
	}
}

#define AICL_LOW_RETRY_CNT_THD 5// Retry count threshold for set icl_override(1341[1]) after aicl continuous low
#define FLOAT_CHG_CHECK_CNT_THD 2// Float charger check count threshold for rerun apsd
static int asus_do_soft_jeita(struct smb_charger *chg)
{
	int ret = 0;
	int batt_temp, batt_volt;
	bool charging_enable;
	int fcc_value,float_volt;
	u8 fv_reg, aicl_stat = 0, icl_ret = 0;
	int aicl_result;
	bool aicl_done = false;
	int bat_capacity;
	bool ubatlife_stop_charging_flag = 0;
	bool suspend_dismiss = 0;
	u8 icl_max = 0;
	bool aicl_fail = false;
	u8 reg = 0, icl_opt_reg = 0;
	bool usb_100_wa = false;
	bool aicl_low = false;
	bool inov_triggered = false;
	static int aicl_low_cnt = 0;
	bool icl_override_flag = false;
	bool flt_chg_rerun_apsd = false;

	batt_temp = asus_get_prop_batt_temp(chg);
	batt_volt = asus_get_prop_batt_volt(chg);
	ret = smblib_read(chg, FLOAT_VOLTAGE_CFG_REG, &fv_reg);
	if (ret < 0)
		CHG_DBG_E("%s: Couldn't read FLOAT_VOLTAGE_CFG_REG\n", __func__);

    // Enable jeita hard limit pause changing, disable fv & chg current compensation
	ret = smblib_write(chg, JEITA_EN_CFG_REG, 0x10);
	if (ret < 0)
		CHG_DBG_E("%s: Failed to set JEITA_EN_CFG_REG\n", __func__);

	//update jeita state according  to batt_temp
	asus_jeita_judge_state(chg->asus_chg, batt_temp);

	//check if new aicl result get

	ret = smblib_read(chg, AICL_STATUS_REG, &aicl_stat);
	if (!ret) {
		aicl_done = (bool)(aicl_stat & AICL_DONE_BIT);
        aicl_fail = (bool)(aicl_stat & AICL_FAIL_BIT);
	}
	smblib_read(chg, ICL_STATUS_REG, &icl_ret);

	aicl_result = icl_ret * 25; // steps 25mA
	CHG_DBG("asus_charging_type=%d, aicl_done=%d, aicl_result=%d, aicl_fail=%d, aicl_stat=0x%02x\n",
		chg->asus_chg->asus_charging_type, aicl_done, aicl_result, aicl_fail, aicl_stat);

	if((chg->asus_chg->asus_charging_type == DCP_2A_BR_IN) && (aicl_done==1) && (aicl_result <= 1600)){
		CHG_DBG("BR/IN country DCP_2A_BR_IN change to DCP_OTHERS_1A");
		chg->asus_chg->asus_charging_type = DCP_OTHERS_1A;
		chg->asus_chg->dual_charge = SINGLE;
		chg->asus_chg->quick_charge_ac_flag = 0;
        ret = smblib_masked_write(chg, USBIN_CURRENT_LIMIT_CFG_REG,
            USBIN_CURRENT_LIMIT_MASK, 0x28); // 1000mA
        if (ret < 0)
            CHG_DBG_E("%s: Failed to set 1000mA icl, ret = %d\n", __func__, ret);
	}

	CHG_DBG("soft_jeita_state = %d, voltage = %dmV, temp = %d\n", chg->asus_chg->soft_jeita_state
			, batt_volt/1000, batt_temp);

	asus_soft_jeita_config(chg->asus_chg, batt_volt, fv_reg, &charging_enable, &fcc_value, &float_volt);

	CHG_DBG("jeita set fcc =%d, float_volt = %d, charging_en = %d\n", fcc_value/1000, float_volt/1000, charging_enable);

    // set fcc
    if (0 == asus_fcc_override_flag) {
		ret = vote(chg->fcc_votable, BATT_PROFILE_VOTER, true, fcc_value);
		if (ret < 0) {
			dev_err(chg->dev, "Couldn't set fast charge current rc=%d\n", ret);
			return ret;
		}
	} else {
		CHG_DBG("%s: skip jeita fcc due to override(%d)\n", __func__, asus_fcc_override_flag);
	}

    // set float volt
    ret = vote(chg->fv_votable, BATT_PROFILE_VOTER, true, float_volt);
    if (ret < 0) {
        dev_err(chg->dev, "Couldn't set float voltage rc=%d\n", ret);
        return ret;
    }


	if(charger_batt_enable){

		update_ubatterylife_info(&suspend_dismiss);

		//~ vote(chip->usb_suspend_votable, ATD_CMD_VOTER, false, 0);
        if (charger_flag && (OTP_NOT_TRIGGER == chg->asus_chg->usb_connector_event))
		{
			if (g_ubatterylife_enable_flag) {
				bat_capacity = asus_get_prop_batt_capacity(chg);

				if(bat_capacity >= UBATLIFE_DISCHG_THD)	// >60%
					ubatlife_chg_status = UBATLIFE_DISCHG_THD;
				else if(bat_capacity < UBATLIFE_CHG_THD)	// <58%
					ubatlife_chg_status = UBATLIFE_CHG_THD;
				else;

				if(ubatlife_chg_status == UBATLIFE_DISCHG_THD)
				{
					CHG_DBG("not charge for ubatlife_chg_status == UBATLIFE_DISCHG_THD\n");
					smblib_set_usb_suspend(chg, true);
				}
				else
				{
					CHG_DBG("enable charge for ubatlife_chg_status == UBATLIFE_CHG_THD\n");
					smblib_set_usb_suspend(chg, false);
				}

				if(ubatlife_chg_status == UBATLIFE_CHG_THD)
					ubatlife_stop_charging_flag = false;
				else
					ubatlife_stop_charging_flag = true;
			}
			else
			{
				smblib_set_usb_suspend(chg, false);
			}
		}
	}else{
		//~ vote(chip->usb_suspend_votable, ATD_CMD_VOTER, true, 0);
        smblib_set_usb_suspend(chg, true);
	}

	// set charging enable
    if (asus_thermal_keep_chg_flag) {
        CHG_DBG("skip jeita stop charging due to thermal test\n");
        vote(chg->chg_disable_votable, BATTCHG_JEITA_EN_VOTER, false, 0);
    } else {
        ret = vote(chg->chg_disable_votable, BATTCHG_JEITA_EN_VOTER, (!charging_enable)||ubatlife_stop_charging_flag, 0);
        chg->asus_chg->jeita_stop_charge_flag = !charging_enable;
        CHG_DBG("%s: Stop charging,BATTCHG_JEITA = %d, ubat = %d\n", __func__, charging_enable, ubatlife_stop_charging_flag);
        if(ret < 0) {
            pr_err("charger batt_suspend disable failed\n");
            return ret;
        }
    }

    // sw recharge
	if((chg->asus_chg->soft_jeita_state >= JEITA_STATE_RANGE_02) && (chg->asus_chg->soft_jeita_state <= JEITA_STATE_RANGE_04)){
		asus_soft_jeita_recharge(chg);
	}

    ret = smblib_read(chg, USBIN_CURRENT_LIMIT_CFG_REG, &icl_max);

    smblib_read(chg, TEMP_RANGE_STATUS_REG, &reg);
    if (reg & THERM_REG_ACTIVE_BIT) {
        inov_triggered = true;
    }

    // re-run apsd and reset icl_cfg when aicl result lower than target threshold
    switch(chg->asus_chg->asus_charging_type) {
    case HVDCP_ASUS_200K_2A:
    case HVDCP_OTHERS_1P5A:
    case HVDCP_OTHERS_1A:
    case DCP_ASUS_750K_2A:
    case DCP_PB_2A:
    case DCP_2A_BR_IN:
    case DCP_OTHERS_1A:
    case OTHERS_1A:
        if ((chg->asus_chg->dual_charge != TYPEC_3A)  &&
            aicl_result < DCP_LIKE_AICL_RERUN_THRESH && 
            !inov_triggered) {
            aicl_low = true;
            aicl_low_cnt++;
        }
        break;
    case FLOATING_0P5A:
    case UNDEFINED:
        if (chg->asus_chg->flt_chg_wa_en && (++(chg->asus_chg->flt_chg_chk_cnt) > FLOAT_CHG_CHECK_CNT_THD)) {
            chg->asus_chg->flt_chg_wa_en = false;
            chg->asus_chg->flt_chg_chk_cnt = 0;
            flt_chg_rerun_apsd = true;
        }
    case SDP_0P5A:
        if (aicl_result < SDP_LIKE_AICL_RERUN_THRESH) {
            aicl_low = true;
            aicl_low_cnt++;
        }
        break;
    default:
        break;
    }

    if (ret == 0) {
        ret = smblib_read(chg, USBIN_ICL_OPTIONS_REG, &icl_opt_reg);
        if (((chg->asus_chg->asus_charging_type == FLOATING_0P5A) ||
            (chg->asus_chg->asus_charging_type == SDP_0P5A) ||
            (chg->asus_chg->asus_charging_type == UNDEFINED)) &&
            !(icl_opt_reg & USB51_MODE_BIT)) {
            usb_100_wa = true;
            CHG_DBG("%s: run usb 100 wa, icl_cfg=0x%02x, 0x1366=0x%02x\n", __func__, icl_max, icl_opt_reg);
        }
    }

    CHG_DBG("aicl_result = %d, inov_triggered = %d, aicl_low = %d, usb_100_wa = %d, aicl_low_cnt = %d, flt_chg_rerun_apsd = %d, flt_chg_chk_cnt = %d\n", 
        aicl_result, inov_triggered, aicl_low, usb_100_wa, aicl_low_cnt, flt_chg_rerun_apsd, chg->asus_chg->flt_chg_chk_cnt);
    if (aicl_done && aicl_low) {
        ASUSEvtlog("aicl done & low, aicl_result=%d\n", aicl_result);
    }
    if (aicl_fail) {
        ASUSEvtlog("aicl fail, aicl_stat=0x%02x\n", aicl_stat);
    }

    // re-run apsd when aicl fail
    if (aicl_fail || usb_100_wa || aicl_low || flt_chg_rerun_apsd) {
        // Rerun APSD
        smblib_read(chg, USBIN_SOURCE_CHANGE_INTRPT_ENB_REG, &reg);
        CHG_DBG("JEITA re-running APSD after aicl fail or usb 100 wa or aicl low, pmi[0x1369]=%02X\n",reg);
        ret = smblib_masked_write(chg, CMD_APSD_REG, APSD_RERUN_BIT, 1);
        if (ret < 0)
            CHG_DBG_E("%s: Failed to set CMD_APSD_REG\n", __func__);
    }

    if (aicl_low_cnt >= AICL_LOW_RETRY_CNT_THD) {
        CHG_DBG("%s: aicl_low_cnt = %d, larger than AICL_LOW_RETRY_CNT_THD = %d\n", __func__, aicl_low_cnt, AICL_LOW_RETRY_CNT_THD);
        icl_override_flag = true;
        aicl_low_cnt = 0;
    }

    if (aicl_fail || usb_100_wa || aicl_low) {
        // usbin icl options(USB2.0/500mA current mode)
        ret = smblib_masked_write(chg, USBIN_ICL_OPTIONS_REG,
                    CFG_USB3P0_SEL_BIT|USB51_MODE_BIT|USBIN_MODE_CHG_BIT, 0x02);
        if (ret < 0) {
            CHG_DBG_E("Couldn't set usbin icl options rc=%d\n", ret);
        }
        if (!icl_override_flag) {
            cancel_delayed_work(&reset_icl_work);
            schedule_delayed_work(&reset_icl_work, msecs_to_jiffies(2000));// add delay for apsd complete
        } else {
            cancel_delayed_work(&reset_icl_with_override_work);
            schedule_delayed_work(&reset_icl_with_override_work, msecs_to_jiffies(2000));// add delay for apsd complete
        }
    }

	return ret;
}

void reset_icl_for_nonstandard_ac(bool icl_override)
{
    u8 set_icl = 0;
    int usb_max_current = 0;
    int rc;
    int sdp_like = 0, dcp_like = 0;
    const struct apsd_result *apsd_result;
    struct smb_charger *chg = &chip_dev->chg;
    struct asus_charger *asus_chg = chg->asus_chg;

    apsd_result = smblib_update_usb_type(chg);

    switch (apsd_result->bit) {
        case SDP_CHARGER_BIT:
        case FLOAT_CHARGER_BIT:
            if ((3 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
                asus_chg->asus_charging_type = TYPEC_3P0A;
                usb_max_current = 3000;
                asus_chg->quick_charge_ac_flag = AC_GT_10W;
            }
            else if ((2 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
                asus_chg->asus_charging_type = TYPEC_1P5A;
                usb_max_current = 1500;
                asus_chg->quick_charge_ac_flag = 0;
            } else {
                if (FLOAT_CHARGER_BIT == apsd_result->bit) {
                    asus_chg->asus_charging_type = FLOATING_0P5A;
                } else {
                    asus_chg->asus_charging_type = SDP_0P5A;
                }
                usb_max_current = 500;
                asus_chg->quick_charge_ac_flag = 0;
            }

			if(g_CDP_WA_done_once) {
				if(g_charger_mode)
					usb_max_current = 500;
				else
					usb_max_current = 1500;
				CHG_DBG("WA for CDP in jeita, icl: %d mA, WA: %d\n", usb_max_current, g_CDP_WA);
				g_CDP_WA_done_once = false;
			}

            asus_chg->dual_charge = SINGLE;
            sdp_like =1;
            break;
        case CDP_CHARGER_BIT:
            if ((3 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
                asus_chg->asus_charging_type = TYPEC_3P0A;
                usb_max_current = 3000;
                asus_chg->quick_charge_ac_flag = AC_GT_10W;
            } else {
                if ((2 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
                    asus_chg->asus_charging_type = TYPEC_1P5A;
                } else {
                    asus_chg->asus_charging_type = CDP_1P5A;
                }
                usb_max_current = 1500;
                asus_chg->quick_charge_ac_flag = 0;
                // 500mA icl in charger mode with CDP
                if (asus_chg->legacy_cable_flag && g_charger_mode)
                    usb_max_current = 500;
            }

            asus_chg->dual_charge = SINGLE;
            sdp_like =1;
            break;
        case OCP_CHARGER_BIT:
            if ((3 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
                asus_chg->asus_charging_type = TYPEC_3P0A;
                usb_max_current = 3000;
                asus_chg->quick_charge_ac_flag = AC_GT_10W;
            } else if ((2 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
                asus_chg->asus_charging_type = TYPEC_1P5A;
                usb_max_current = 1500;
                asus_chg->quick_charge_ac_flag = 0;
            } else {
                asus_chg->asus_charging_type = OTHERS_1A;
                usb_max_current = 1000;
                asus_chg->quick_charge_ac_flag = 0;
            }

            asus_chg->dual_charge = SINGLE;
            sdp_like =1;
            break;
        case DCP_CHARGER_BIT | QC_3P0_BIT:
        case DCP_CHARGER_BIT | QC_2P0_BIT:
        case DCP_CHARGER_BIT:
            sdp_like = 0;
            dcp_like = 1;
        default:
            usb_max_current = 500;
            asus_chg->asus_charging_type = UNDEFINED;
            asus_chg->dual_charge = SINGLE;
            asus_chg->quick_charge_ac_flag = 0;
            break;
    }

    if(dcp_like && !sdp_like) {
        switch (ASUS_ADAPTER_ID) {
            case ASUS_750K:
            case PB:
                if (0 == asus_chg->asus_qc_flag) {// non HVDCP
                    if (ASUS_750K == ASUS_ADAPTER_ID) {
                        asus_chg->asus_charging_type = DCP_ASUS_750K_2A;
                    } else {
                        asus_chg->asus_charging_type = DCP_PB_2A;
                    }
                    if (3 == asus_chg->ufp_mode && !asus_chg->legacy_cable_flag) {
                        asus_chg->dual_charge = TYPEC_3A;
                        usb_max_current = 3000;
                        asus_chg->quick_charge_ac_flag = AC_GT_10W;
                    } else {
                        asus_chg->dual_charge = ASUS_2A;
                        usb_max_current = 2000;
                        asus_chg->quick_charge_ac_flag = AC_EQ_10W;
                    }
                } else if (3 == asus_chg->asus_qc_flag) { // HVDCP3
                    asus_chg->asus_charging_type = HVDCP_OTHERS_1P5A;
                    asus_chg->dual_charge = SINGLE;
                    usb_max_current = 1500;
                    asus_chg->quick_charge_ac_flag = AC_GT_10W;
                } else { // HVDCP2
                    asus_chg->asus_charging_type = HVDCP_OTHERS_1A;
                    asus_chg->dual_charge = SINGLE;
                    usb_max_current = 1000;
                    asus_chg->quick_charge_ac_flag = 0;
                }
                break;
            case ASUS_200K:
                if (3 == asus_chg->asus_qc_flag) {// HVDCP3
                    asus_chg->asus_charging_type = HVDCP_ASUS_200K_2A;
                    asus_chg->dual_charge = ASUS_2A;
                    usb_max_current = 1650;
                    asus_chg->quick_charge_ac_flag = AC_GT_10W;
                    break;
                } else if (2 == asus_chg->asus_qc_flag) {// HVDCP2
                    asus_chg->asus_charging_type = HVDCP_OTHERS_1A;
                    asus_chg->dual_charge = SINGLE;
                    usb_max_current = 1000;
                    asus_chg->quick_charge_ac_flag = 0;
                    break;
                } else {
                    // Non-QC falls to others
                }
            case OTHERS:
            default:
                if (3 == asus_chg->ufp_mode && !asus_chg->legacy_cable_flag && 0 == asus_chg->asus_qc_flag) {
                    asus_chg->asus_charging_type = TYPEC_3P0A;
                    usb_max_current = 3000;
                    asus_chg->dual_charge = SINGLE;
                    asus_chg->quick_charge_ac_flag = AC_GT_10W;
                } else if (2 == asus_chg->ufp_mode && !asus_chg->legacy_cable_flag && 0 == asus_chg->asus_qc_flag) {
                    asus_chg->asus_charging_type = TYPEC_1P5A;
                    usb_max_current = 1500;
                    asus_chg->dual_charge = SINGLE;
                    asus_chg->quick_charge_ac_flag = 0;
                } else {
                    // TODO: BR/IN country code set icl to 2000mA

                    asus_chg->asus_charging_type = DCP_OTHERS_1A;
                    usb_max_current = 1000;
                    asus_chg->dual_charge = SINGLE;
                    asus_chg->quick_charge_ac_flag = 0;
                    if (3 == asus_chg->asus_qc_flag) {
                        asus_chg->asus_charging_type = HVDCP_OTHERS_1P5A;
                        usb_max_current = 1500;
                        asus_chg->quick_charge_ac_flag = AC_GT_10W;
                    } else if (2 == asus_chg->asus_qc_flag) {
                        asus_chg->asus_charging_type = HVDCP_OTHERS_1A;
                    }
                }
                break;
            //~ default:
                //~ usb_max_current = 500;
                //~ asus_chg->asus_charging_type = UNDEFINED;
                //~ asus_chg->dual_charge = SINGLE;
                //~ asus_chg->quick_charge_ac_flag = 0;
                //~ break;
        }
    }

	// config ICL_OVERRIDE_AFTER_APSD(sw control icl)
	rc = smblib_masked_write(chg, USBIN_LOAD_CFG_REG,
				ICL_OVERRIDE_AFTER_APSD_BIT, ICL_OVERRIDE_AFTER_APSD_BIT);
	if (rc < 0) {
		CHG_DBG_E("%s: Couldn't set ICL_OVERRIDE_AFTER_APSD rc=%d\n", __func__, rc);
	}

    // Set ICL by table
    set_icl = usb_max_current / 25;
    if (set_icl > USB_ICL_MAX) {
        set_icl = USB_ICL_MAX;
    }
	CHG_DBG("%s: legacy:%d, ufp_mode:%d sdp_like:%d, dcp_like:%d, setting mA = %d, icl_override = %d\n", __func__, asus_chg->legacy_cable_flag, asus_chg->ufp_mode, sdp_like, dcp_like, usb_max_current, icl_override);
    //~ vote(chg->usb_icl_votable, USB_PSY_VOTER, true, usb_max_current * 1000);
    rc = smblib_masked_write(chg, USBIN_CURRENT_LIMIT_CFG_REG,
        USBIN_CURRENT_LIMIT_MASK, set_icl);
    if (rc < 0)
        CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);

    // reset ICL_OVERRIDE(0x1341[1] = 1)
    if (icl_override) {
        rc = smblib_masked_write(chg, CMD_APSD_REG,
                    ICL_OVERRIDE_BIT, ICL_OVERRIDE_BIT);
        CHG_DBG("%s: Set icl override after set icl by table\n", __func__);
    }
    // config usb usbin adapter allowance(5V to 9V) 0x1360 = 0x08
    rc = smblib_masked_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG,
                USBIN_ADAPTER_ALLOW_MASK, 0x08);
    if (rc < 0) {
        CHG_DBG_E("%s: Couldn't set usbin adapter allowance rc=%d\n", __func__, rc);
    }

    chg->asus_chg->last_icl_cfg = set_icl;
}

void asus_reset_icl_work(struct work_struct *work)
{
    struct smb_charger *chg = &chip_dev->chg;

    if (!is_apsd_done(chg)) {
        CHG_DBG("APSD not done, delay 2s\n");
        msleep(2000); // TODO: adjust wait time
    }
    reset_icl_for_nonstandard_ac(false);
}

void asus_reset_icl_with_override_work(struct work_struct *work)
{
    struct smb_charger *chg = &chip_dev->chg;

    if (!is_apsd_done(chg)) {
        CHG_DBG("APSD not done, delay 2s\n");
        msleep(2000); // TODO: adjust wait time
    }
    reset_icl_for_nonstandard_ac(true);
}

#define	TYPE_C_1P5A_ICL_THD	0x36 // 1350mA / 25mA = 54 = 0x36
#define	TYPE_C_3A_ICL_THD		0x50 // 2000mA / 25mA = 80 = 0x50

void check_legacy_for_nonstandard_ac(void)
{
    u8 stat=0, icl_ret=0;
    int icl_thd, prev_legacy, prev_ufp;
    bool reset= false;
    struct smb_charger *chg = &chip_dev->chg;
    struct asus_charger *asus_chg = chg->asus_chg;

    prev_legacy = asus_chg->legacy_cable_flag;
    prev_ufp = asus_chg->ufp_mode;
    asus_chg->ufp_mode = asus_get_prop_ufp_mode(chg);

    if(asus_chg->ufp_mode != 2 && asus_chg->ufp_mode != 3){
        CHG_DBG("end legacy check for AC is not in checking list\n");
        return;
    }

    if(2 == asus_chg->ufp_mode) // TypeC 1.5A
        icl_thd = TYPE_C_1P5A_ICL_THD;
    else // TypeC 3A
        icl_thd = TYPE_C_3A_ICL_THD;

    smblib_read(chg, AICL_STATUS_REG, &stat);
    smblib_read(chg, ICL_STATUS_REG, &icl_ret);

    if((stat & AICL_DONE_BIT) && (int)icl_ret <= icl_thd){
        reset = true;
        asus_chg->legacy_cable_flag = true; //legacy type
        CHG_DBG("end legacy check for reseting ICL as legacy, prev:%d, now:%d, aicl_ret = %d mA, thd = %d mA\n",
            prev_legacy, asus_chg->legacy_cable_flag, (int)icl_ret * 25, icl_thd * 25);
    }else {}
        //~ CHG_DBG("legacy check no error\n");

    CHG_DBG("prev ufp = %d, now = %d\n", prev_ufp, asus_chg->ufp_mode);
    if (asus_chg->ufp_mode > prev_ufp) {
        reset = true;
    }

    if(!reset)
        return;

    reset_icl_for_nonstandard_ac(false);
}

void asus_cable_cap_check_work(struct work_struct *work)
{
    check_legacy_for_nonstandard_ac();
}

void asus_batt_temp_work(struct work_struct *work)
{
	struct asus_charger *asus_chg = container_of(work,
					struct asus_charger,
					asus_batt_temp_work.work);
    struct smb_charger *chg = &chip_dev->chg;
	int ret;
	int usb_present;
	int type;
	union power_supply_propval pval = {0, };

	type = chg->real_charger_type;
	usb_present = asus_get_prop_usb_present(chg);
	CHG_DBG("asus_batt_temp_work: usb_present=%d, usb_type=%d\n", usb_present, type);

	// Checking battery OVP event
	smblib_get_prop_batt_health(chg, &pval);

    if (usb_present) {
        //do asus adpter check, country code not BR, now we get it, update!!
        if((asus_chg->BR_countrycode_read_pending == 1) &&
            ((asus_chg->BR_countrycode_flag == COUNTRY_BR) || (asus_chg->BR_countrycode_flag == COUNTRY_IN))){
            asus_chg->asus_charging_type = DCP_2A_BR_IN;
            asus_chg->dual_charge = ASUS_2A;
            asus_chg->quick_charge_ac_flag = AC_EQ_10W;
            ret = smblib_masked_write(chg, USBIN_CURRENT_LIMIT_CFG_REG,
                USBIN_CURRENT_LIMIT_MASK, 0x50); // 2000mA
            if (ret < 0)
                CHG_DBG_E("%s: Failed to set icl to 2000mA for BR/IN adapter\n", __func__);
            asus_chg->BR_countrycode_read_pending = 0;
            CHG_DBG("%s: change type to DCP_2A since BR country code, do jeita 5s later\n", __func__);
            cancel_delayed_work(&asus_chg->asus_batt_temp_work);
            schedule_delayed_work(&asus_chg->asus_batt_temp_work,
                    msecs_to_jiffies(5000));
            return;
        }

        check_legacy_for_nonstandard_ac();
        if ((true == g_legacy_det_done) || (true == g_CDP_WA_done_once)) {
			CHG_DBG("%s reset icl after legacy det done\n",__func__);
			reset_icl_for_nonstandard_ac(false);
			g_legacy_det_done = false;
		}
		ret = asus_do_soft_jeita(chg);
		if (!ret) {
//            CHG_DBG("%s: do soft jeita after 60s\n", __func__);
//            cancel_delayed_work(&asus_chg->asus_batt_temp_work);
//			schedule_delayed_work(&asus_chg->asus_batt_temp_work,
//					msecs_to_jiffies(60000));
			schedule_delayed_work(&SetJeitaRTCWorker, 0);
		} else {
			CHG_DBG("%s do soft jeita after 5s\n",__func__);
            cancel_delayed_work(&asus_chg->asus_batt_temp_work);
			schedule_delayed_work(&asus_chg->asus_batt_temp_work, 
                    msecs_to_jiffies(5000));
		}
    } else {
		//~ CHG_DBG("%s do soft jeita after 60s\n",__func__);
		cancel_delayed_work(&asus_chg->asus_batt_temp_work);
		schedule_delayed_work(&asus_chg->asus_batt_temp_work,
					msecs_to_jiffies(60000));
    }
}

void asus_adapter_adc_process(struct asus_charger *asus_chg, bool is_rerun)
{
    struct smb_charger *chg = &chip_dev->chg;
	int rc;
	int usb_max_current;
	u8 set_icl;
	bool typec_wa_flag = false;

    CHG_DBG("enter %s, is_rerun=%d\n", __func__, is_rerun);
    asus_chg->BR_countrycode_read_pending = 0;

    // set usb icl 25mA
    rc = smblib_masked_write(chg, USBIN_CURRENT_LIMIT_CFG_REG, USBIN_CURRENT_LIMIT_MASK, 0x01);
    if (rc < 0)
        CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT_CFG_REG\n", __func__);

	if (asp1690_ready != 1) {
		ASUS_ADAPTER_ID = ADC_NOT_READY;
		goto set_current;
	}

    msleep(5);
    // start adc detect
	ASUS_ADAPTER_ID = asp1690E_CHG_TYPE_judge();

    // check legacy cable
//    rc = smblib_read(chg, TYPE_C_STATUS_5_REG, &reg);
//    if (rc < 0)
//        CHG_DBG_E("%s: Couldn't read TYPE_C_STATUS_5_REG\n", __func__);
//    CHG_DBG("%s:ufp_mode = %d, pre legacy flag = %d, 0x130F now = 0x%x\n", __func__, asus_chg->ufp_mode, asus_chg->legacy_cable_flag, reg);
//
//	if((0 == asus_chg->asus_qc_flag) && (3 == asus_chg->ufp_mode || 2 == asus_chg->ufp_mode)){
//		rc = smblib_read(chg, TYPE_C_STATUS_5_REG, &reg);
//		if (rc < 0)
//			CHG_DBG_E("%s: redo in charger mode but Couldn't read TYPE_C_STATUS_5_REG\n", __func__);
//		asus_chg->legacy_cable_flag = reg & TYPEC_LEGACY_CABLE_STATUS_BIT;
//		CHG_DBG(" rerun legacy for non-QC TypeC 1.5A/3A, result %d\n", asus_chg->legacy_cable_flag);
//	}

set_current:
    switch (ASUS_ADAPTER_ID) {
    case ASUS_750K:
    case PB:
        if (0 == asus_chg->asus_qc_flag) {// non HVDCP
            if (ASUS_750K == ASUS_ADAPTER_ID) {
                asus_chg->asus_charging_type = DCP_ASUS_750K_2A;
            } else {
                asus_chg->asus_charging_type = DCP_PB_2A;
            }
            if (3 == asus_chg->ufp_mode && !asus_chg->legacy_cable_flag) {
                asus_chg->dual_charge = TYPEC_3A;
                usb_max_current = 3000;
                asus_chg->quick_charge_ac_flag = AC_GT_10W;
            } else {
                asus_chg->dual_charge = ASUS_2A;
                usb_max_current = 2000;
                asus_chg->quick_charge_ac_flag = AC_EQ_10W;
                if (chg->asus_chg->pon_cable_det_flag) {
                    typec_wa_flag = true;
                }
            }
        } else if (3 == asus_chg->asus_qc_flag) { // HVDCP3
            asus_chg->asus_charging_type = HVDCP_OTHERS_1P5A;
            asus_chg->dual_charge = SINGLE;
            usb_max_current = 1500;
            asus_chg->quick_charge_ac_flag = AC_GT_10W;
        } else { // HVDCP2
            asus_chg->asus_charging_type = DCP_OTHERS_1A;
            asus_chg->dual_charge = SINGLE;
            usb_max_current = 1000;
        }
        goto post_proc;
        break;
    case ASUS_200K:
        if (3 == asus_chg->asus_qc_flag) {
            asus_chg->asus_charging_type = HVDCP_ASUS_200K_2A;
            asus_chg->dual_charge = ASUS_2A;
            usb_max_current = 1650;
            asus_chg->quick_charge_ac_flag = AC_GT_10W;
            goto post_proc;
        } else if (2 == asus_chg->asus_qc_flag) {
            asus_chg->asus_charging_type = HVDCP_OTHERS_1A;
            asus_chg->dual_charge = SINGLE;
            usb_max_current = 1000;
            goto post_proc;
        } else {
            break;
        }
    case OTHERS:
    case ADC_NOT_READY:
    default:
        break;
    }
    if (3 == asus_chg->ufp_mode && !asus_chg->legacy_cable_flag && 0 == asus_chg->asus_qc_flag) {
        asus_chg->asus_charging_type = TYPEC_3P0A;
        usb_max_current = 3000;
        asus_chg->dual_charge = SINGLE;
        asus_chg->quick_charge_ac_flag = AC_GT_10W;
        typec_wa_flag = true;
    } else if (2 == asus_chg->ufp_mode && !asus_chg->legacy_cable_flag && 0 == asus_chg->asus_qc_flag) {
        asus_chg->asus_charging_type = TYPEC_1P5A;
        usb_max_current = 1500;
        asus_chg->dual_charge = SINGLE;
        typec_wa_flag = true;
    } else {
        // TODO: BR/IN country code set icl to 2000mA
		//this if-else is for BR country adapter, no id pin
		if (((asus_chg->BR_countrycode_flag == COUNTRY_BR) || 
			(asus_chg->BR_countrycode_flag == COUNTRY_IN)) && (0 == asus_chg->asus_qc_flag)) {
			CHG_DBG("change BR/IN to ASUS_2A \n");
			asus_chg->asus_charging_type = DCP_2A_BR_IN;
			asus_chg->dual_charge = ASUS_2A;
			asus_chg->quick_charge_ac_flag = AC_EQ_10W;
			usb_max_current = 2000;
		} else {
			asus_chg->asus_charging_type = DCP_OTHERS_1A;
			asus_chg->dual_charge = SINGLE;
			if (0 == asus_chg->asus_qc_flag) {
				asus_chg->BR_countrycode_read_pending = 1;
			}
			usb_max_current = 1000;
		}

        if (3 == asus_chg->asus_qc_flag) {
            asus_chg->asus_charging_type = HVDCP_OTHERS_1P5A;
            usb_max_current = 1500;
            asus_chg->quick_charge_ac_flag = AC_GT_10W;
        } else if (2 == asus_chg->asus_qc_flag) {
            asus_chg->asus_charging_type = HVDCP_OTHERS_1A;
        }
    }

	if ((2 == asus_chg->ufp_mode || 3 == asus_chg->ufp_mode) && g_charger_mode)
        typec_wa_flag = true;

post_proc:
    // Set HVDCP_PULSE_COUNT_MAX 
	rc = smblib_write(chg, HVDCP_PULSE_COUNT_MAX, 0x54);// change from 0x54 to 0x4C
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set HVDCP_PULSE_COUNT_MAX\n", __func__);

    // Enable BC1.2 & Enable HVDCP
	rc = smblib_write(chg, USBIN_OPTIONS_1_CFG_REG, 0x7D);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set USBIN_OPTIONS_1_CFG_REG\n", __func__);

    // Rerun APSD
	CHG_DBG("%s: Rerun APSD 2nd\n", __func__);
	rc = smblib_masked_write(chg, CMD_APSD_REG, APSD_RERUN_BIT, APSD_RERUN_BIT);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to set CMD_APSD_REG\n", __func__);

	if(typec_wa_flag) {
		// reset ICL_OVERRIDE(0x1341[1] = 1)
		rc = smblib_masked_write(chg, CMD_APSD_REG,
					ICL_OVERRIDE_BIT, ICL_OVERRIDE_BIT);
		CHG_DBG("%s: Set icl override for typec 1.5A/3A WA\n", __func__);
	}

    // Set ICL by table
    set_icl = usb_max_current / 25;
    if (set_icl > USB_ICL_MAX) {
        set_icl = USB_ICL_MAX;
    }
	CHG_DBG("%s: ASUS_ADAPTER_ID = %s, qc_flag = %d, ufp_mode = %d, setting mA = %d, set_icl = 0x%x\n", __func__, asus_id[ASUS_ADAPTER_ID], asus_chg->asus_qc_flag, asus_chg->ufp_mode, usb_max_current, set_icl);
    //~ vote(chg->usb_icl_votable, USB_PSY_VOTER, true, usb_max_current * 1000);
    rc = smblib_masked_write(chg, USBIN_CURRENT_LIMIT_CFG_REG,
        USBIN_CURRENT_LIMIT_MASK, set_icl);
    if (rc < 0)
        CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);

    if (!is_rerun) {// skip reset ICL_OVERRIDE in adc det rerun
        // reset ICL_OVERRIDE(0x1341[1] = 1)
        rc = smblib_masked_write(chg, CMD_APSD_REG,
                    ICL_OVERRIDE_BIT, ICL_OVERRIDE_BIT);
        CHG_DBG("%s: Set icl override after set icl by table\n", __func__);
    } else {
        CHG_DBG("%s: Skip reset icl override after set icl by table in adc det rerun\n", __func__);
    }
    // config usb usbin adapter allowance(5V to 9V) 0x1360 = 0x08
    rc = smblib_masked_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG,
                USBIN_ADAPTER_ALLOW_MASK, 0x08);
    if (rc < 0) {
        CHG_DBG_E("%s: Couldn't set usbin adapter allowance rc=%d\n", __func__, rc);
    }

    chg->asus_chg->last_icl_cfg = set_icl;

    // usb suspend when icl <= 25mA
    rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
            SUSPEND_ON_COLLAPSE_USBIN_BIT, 1);
    if (rc < 0) {
        CHG_DBG_E("%s: Couldn't set SUSPEND_ON_COLLAPSE_USBIN_BIT rc=%d\n", __func__, rc);
    }

    // start cable capability check 5s later
    cancel_delayed_work(&cable_capability_check_work);
    schedule_delayed_work(&cable_capability_check_work, msecs_to_jiffies(5000));

    // start soft jeita
    if (!chg->asus_chg->adc_redet_flag) {
        cancel_delayed_work(&chg->asus_chg->asus_batt_temp_work);
        schedule_delayed_work(&chg->asus_chg->asus_batt_temp_work, msecs_to_jiffies(15000));
    }

	// start legacy cable detection when not HVDCP
	if ((0 == asus_chg->asus_qc_flag) && typec_wa_flag) {
		CHG_DBG("%s: DCP detected, asus run legacy wa in 5s\n", __func__);
		schedule_delayed_work(&asus_chg->legacy_det_work, msecs_to_jiffies(5000));
	} else {
		asus_chg->asus_adapter_detecting_flag = false;
	}
    asus_flow_done_flag = 1;
    asus_chg->adc_redet_flag = false;

    if(adc_check_lock.active){
		__pm_relax(&adc_check_lock);
    }
}

void asus_adapter_adc_normal_work(struct work_struct *work)
{
    struct asus_charger *asus_chg = container_of(work,
                    struct asus_charger,
                    asus_adapter_adc_normal_work.work);
    asus_adapter_adc_process(asus_chg, false);
}

void asus_adapter_adc_rerun_work(struct work_struct *work)
{
    struct asus_charger *asus_chg = container_of(work,
                    struct asus_charger,
                    asus_adapter_adc_rerun_work.work);
    asus_adapter_adc_process(asus_chg, true);
}

void asus_adapter_adc_det(struct smb_charger *chg, bool is_rerun)
{
    int rc;
    u8 reg=0;

    // Disable BC1.2 & Disable HVDCP
    rc = smblib_write(chg, USBIN_OPTIONS_1_CFG_REG,  0x71);
    if (rc < 0)
        CHG_DBG_E("%s: Failed to set USBIN_OPTIONS_1_CFG_REG\n", __func__);
    // Rerun APSD
    smblib_read(chg, USBIN_SOURCE_CHANGE_INTRPT_ENB_REG, &reg);
    CHG_DBG("asus_adapter_adc_det re-running APSD pmi[0x1369]=%02X\n",reg);
    CHG_DBG("%s: Rerun APSD 1st\n", __func__);
    rc = smblib_masked_write(chg, CMD_APSD_REG, APSD_RERUN_BIT, 1);
    if (rc < 0)
        CHG_DBG_E("%s: Failed to set CMD_APSD_REG\n", __func__);

    // usb not suspend when icl <= 25mA
    rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
            SUSPEND_ON_COLLAPSE_USBIN_BIT, 0);
    if (rc < 0) {
        CHG_DBG_E("%s: Couldn't clear SUSPEND_ON_COLLAPSE_USBIN_BIT rc=%d\n", __func__, rc);
    }

    OpenDpDm(true);
    if(!adc_check_lock.active){
		__pm_stay_awake(&adc_check_lock);
    }
    if (chg->asus_chg->asus_qc_flag != 0) {
        if (!is_rerun)
            schedule_delayed_work(&chg->asus_chg->asus_adapter_adc_normal_work, msecs_to_jiffies(ADC_WAIT_TIME_HVDCP23));
        else
            schedule_delayed_work(&chg->asus_chg->asus_adapter_adc_rerun_work, msecs_to_jiffies(ADC_WAIT_TIME_HVDCP23));
    } else {
        if (!is_rerun)
            schedule_delayed_work(&chg->asus_chg->asus_adapter_adc_normal_work, msecs_to_jiffies(ADC_WAIT_TIME_HVDCP0));
        else
            schedule_delayed_work(&chg->asus_chg->asus_adapter_adc_rerun_work, msecs_to_jiffies(ADC_WAIT_TIME_HVDCP0));
    }
}

void asus_qc3_wa_adc_det_work(struct work_struct *work)
{
    struct smb_charger *chg = &chip_dev->chg;
    asus_adapter_adc_det(chg, true);
}

void asus_charger_pre_config(struct smb_charger *chg)
{
    struct asus_charger *asus_chg = chg->asus_chg;
    int rc;
    u8 stat;

    // 1.config pre charge current
    rc = smblib_masked_write(chg, PRE_CHARGE_CURRENT_CFG_REG,
                PRE_CHARGE_CURRENT_SETTING_MASK, 0x0A);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set pre charge current rc=%d\n", rc);
    }
    // 2.config fast charge current
    rc = vote(chg->fcc_votable, BATT_PROFILE_VOTER, true, asus_chg->asus_tb.fcc_cfg);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set fast charge current rc=%d\n", rc);
    }
    // 3.config float voltage
    rc = vote(chg->fv_votable, BATT_PROFILE_VOTER, true, asus_chg->asus_tb.fv_cfg);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set float voltage rc=%d\n", rc);
    }
    // 4.config recharge threshold(v)
    rc = smblib_masked_write(chg, FVC_RECHARGE_THRESHOLD_CFG_REG,
                FVC_RECHARGE_THRESHOLD_MASK, asus_chg->asus_tb.rchg_cfg);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set recharge threshold rc=%d\n", rc);
    }
    // 5.usbin icl options(USB2.0/500mA current mode)
    rc = smblib_masked_write(chg, USBIN_ICL_OPTIONS_REG,
                CFG_USB3P0_SEL_BIT|USB51_MODE_BIT|USBIN_MODE_CHG_BIT, 0x02);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set usbin icl options rc=%d\n", rc);
    }
    // 6.config termination charge current
//    rc = smblib_masked_write(chg, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
//                TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, asus_chg->asus_tb.tcc_cfg);
//    if (rc < 0) {
//        dev_err(chg->dev, "Couldn't set termination charge current rc=%d\n", rc);
//    }
    // 7.config usb usbin adapter allowance(5V to 9V)
    rc = smblib_masked_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG,
                USBIN_ADAPTER_ALLOW_MASK, 0x08);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set usbin adapter allowance rc=%d\n", rc);
    }
    // 8.config dc dcin adapter allowance(5V)

    // Enable BC1.2 && HVDCP
    rc = smblib_write(chg, USBIN_OPTIONS_1_CFG_REG, 0x7D);
    if (rc < 0)
        CHG_DBG_E("%s: Failed to set USBIN_OPTIONS_1_CFG_REG\n", __func__);

    // 9.set chareger en=reg control && disable charge inhibit
    rc = smblib_write(chg, CHGR_CFG2_REG, 0x40);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set reg control charger enable && disable charge inhibit rc=%d\n", rc);
    }
    // 10.disable charger
    rc = smblib_read(chg, CHGR_CFG2_REG, &stat);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't read CHGR_CFG2_REG rc=%d\n", rc);
    }
    rc = vote(chg->chg_disable_votable, USBIN_PRE_CONFIG_VOTER, true, 0);
    if(rc < 0) {
        dev_err(chg->dev, "Couldn't disable charging rc=%d\n", rc);
    }
    // 11.enable charger
    rc = vote(chg->chg_disable_votable, USBIN_PRE_CONFIG_VOTER, false, 0);
    if(rc < 0) {
        dev_err(chg->dev, "Couldn't enable charging rc=%d\n", rc);
    }
    // 12.config minimum system voltage(3.6V)
    rc = smblib_masked_write(chg, VSYS_MIN_SEL_CFG_REG,
                VSYS_MIN_SEL_MASK, 0x02);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set minimum system voltage rc=%d\n", rc);
    }
    // 13.hvdcp = force hvdcp 5V
    
    // 14.hvdcp pulse count max = qc2_5v & qc3 pulse count = 0
    
    // 15.config ICL_OVERRIDE_AFTER_APSD(sw control icl)
    rc = smblib_masked_write(chg, USBIN_LOAD_CFG_REG,
                ICL_OVERRIDE_AFTER_APSD_BIT, ICL_OVERRIDE_AFTER_APSD_BIT);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set ICL_OVERRIDE_AFTER_APSD rc=%d\n", rc);
    }
    // 16.usb cmd apsd = ICL_OVERRIDE
    rc = smblib_masked_write(chg, CMD_APSD_REG,
                ICL_OVERRIDE_BIT, ICL_OVERRIDE_BIT);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set ICL_OVERRIDE rc=%d\n", rc);
    }
}

void asus_handle_otg_insertion(struct work_struct *work)
{
    struct smb_charger *chg = &chip_dev->chg;
    int rc;

	rc = smblib_masked_write(chg, OTG_CURRENT_LIMIT_CFG_REG,
			OTG_CURRENT_LIMIT_MASK, 0x02);//set to 750mA after 5s
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set otg icl rc=%d\n", rc);
    }

}

void asus_handle_usb_insertion(struct work_struct *work)
{
	struct asus_charger *asus_chg = container_of(work,
					struct asus_charger,
					asus_handle_usb_insertion_work.work);
    struct smb_charger *chg = &chip_dev->chg;
    int icl_cfg;
    int rc;
    u8 reg, set_icl;
    const struct apsd_result *apsd_result;
	bool typec_wa_flag = false;

	if(g_charger_mode){
		CHG_DBG("Keep ChargerModeLock for 90s\n");
		__pm_wakeup_event(&ChargerModeLock, jiffies_to_msecs(90 * HZ));
	}

    if (!asus_get_prop_usb_present(chg)) {
        asus_handle_usb_removal(chg);
        return;
    }

//    asus_chg->asus_adapter_detecting_flag = true;

    // set fcc again to avoid fg override after batt profile loaded
    rc = vote(chg->fcc_votable, BATT_PROFILE_VOTER, true, asus_chg->asus_tb.fcc_cfg);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set fast charge current rc=%d\n", rc);
    }
    // set fv again to avoid fg override after batt profile loaded
    rc = vote(chg->fv_votable, BATT_PROFILE_VOTER, true, asus_chg->asus_tb.fv_cfg);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set float voltage rc=%d\n", rc);
    }
    // wait for apsd done
    if (!is_apsd_done(chg)) {
        CHG_DBG("APSD not done, delay 1s\n");
        msleep(1000); // TODO: adjust wait time
    }
    apsd_result = smblib_update_usb_type(chg);

    // get ufp mode(high/medium/low)
    asus_chg->ufp_mode = asus_get_prop_ufp_mode(chg);

    // check legacy cable
    rc = smblib_read(chg, TYPE_C_STATUS_5_REG, &reg);
    if (rc < 0)
        CHG_DBG_E("%s: Couldn't read TYPE_C_STATUS_5_REG\n", __func__);
    asus_chg->legacy_cable_flag = (bool)(reg & TYPEC_LEGACY_CABLE_STATUS_BIT);

    CHG_DBG("triggered, usb inserted, type = %s, typec mode = %s, legacy_cable_flag = %d\n",
        apsd_result->name, ufp_type[asus_chg->ufp_mode], asus_chg->legacy_cable_flag);

    // get HVDCP type
    if ((DCP_CHARGER_BIT | QC_3P0_BIT) == apsd_result->bit) {
        asus_chg->asus_qc_flag = 3;
    } else if ((DCP_CHARGER_BIT | QC_2P0_BIT) == apsd_result->bit) {
        asus_chg->asus_qc_flag = 2;
    } else {
        asus_chg->asus_qc_flag = 0;
    }

    if (chg->pd_active) {
        CHG_DBG("%s: PD_active\n", __func__);
        asus_chg->asus_charging_type = PD;
        asus_chg->asus_adapter_detecting_flag = false;
        asus_flow_done_flag = true;
        asus_chg->asus_flow_processing_flag = false;
        cancel_delayed_work(&asus_chg->asus_batt_temp_work);
        schedule_delayed_work(&asus_chg->asus_batt_temp_work, 0);
        return;
    }

    asus_chg->dual_charge = UNDEFINE;
    asus_chg->quick_charge_ac_flag = 0;

    // set icl by table
    switch (apsd_result->bit) {
	case SDP_CHARGER_BIT:
    case FLOAT_CHARGER_BIT:
        if ((3 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
            asus_chg->asus_charging_type = TYPEC_3P0A;
            icl_cfg = 3000;
            asus_chg->dual_charge = SINGLE;
            asus_chg->quick_charge_ac_flag = AC_GT_10W;
        } else if ((2 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
            asus_chg->asus_charging_type = TYPEC_1P5A;
            icl_cfg = 1500;
            asus_chg->dual_charge = SINGLE;
        } else {
			if (FLOAT_CHARGER_BIT == apsd_result->bit)
				asus_chg->asus_charging_type = FLOATING_0P5A;
			else
				asus_chg->asus_charging_type = SDP_0P5A;
            icl_cfg = 500;
            asus_chg->dual_charge = SINGLE;
        }

        if (g_CDP_WA >= 2) {
            if(g_charger_mode)
                icl_cfg = 500;
            else
                icl_cfg = 1500;
            CHG_DBG("WA for CDP, icl: %d mA, WA: %d\n", icl_cfg, g_CDP_WA);
            g_CDP_WA = 0;
            g_CDP_WA_done_once = true;
			// config ICL_OVERRIDE_AFTER_APSD(sw control icl)
			rc = smblib_masked_write(chg, USBIN_LOAD_CFG_REG,
						ICL_OVERRIDE_AFTER_APSD_BIT, ICL_OVERRIDE_AFTER_APSD_BIT);
			if (rc < 0) {
				CHG_DBG_E("%s: Couldn't set ICL_OVERRIDE_AFTER_APSD rc=%d\n", __func__, rc);
			}
		}
        break;
    case CDP_CHARGER_BIT:
        if ((3 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
            asus_chg->asus_charging_type = TYPEC_3P0A;
            icl_cfg = 3000;
            asus_chg->dual_charge = SINGLE;
            asus_chg->quick_charge_ac_flag = AC_GT_10W;
        } else {
            if ((2 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
                asus_chg->asus_charging_type = TYPEC_1P5A;
            } else {
                asus_chg->asus_charging_type = CDP_1P5A;
            }
            icl_cfg = 1500;
            // 500mA icl in charger mode with CDP
            if (asus_chg->legacy_cable_flag && g_charger_mode)
                icl_cfg = 500;
            asus_chg->dual_charge = SINGLE;
        }
        break;
    case DCP_CHARGER_BIT | QC_3P0_BIT:
    case DCP_CHARGER_BIT | QC_2P0_BIT:
    case DCP_CHARGER_BIT:
        // wait for apsd done
        if (!is_apsd_done(chg)) {
            CHG_DBG("APSD not done, delay 1s\n");
            msleep(1000); // TODO: adjust wait time
        }
        asus_chg->asus_charging_type = UNDEFINED;
        asus_chg->dual_charge = UNDEFINE;
        asus_adapter_adc_det(chg, false);
        return;
        break;
    case OCP_CHARGER_BIT:
        if ((3 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
            asus_chg->asus_charging_type = TYPEC_3P0A;
            icl_cfg = 3000;
            asus_chg->dual_charge = SINGLE;
            asus_chg->quick_charge_ac_flag = AC_GT_10W;
            typec_wa_flag = true;
        } else if ((2 == asus_chg->ufp_mode) && !asus_chg->legacy_cable_flag) {
            asus_chg->asus_charging_type = TYPEC_1P5A;
            icl_cfg = 1500;
            asus_chg->dual_charge = SINGLE;
            typec_wa_flag = true;
        } else {
            asus_chg->asus_charging_type = OTHERS_1A;
            icl_cfg = 1000;
            asus_chg->dual_charge = SINGLE;
        }
        break;
    default:
        icl_cfg = 500;
        asus_chg->asus_charging_type = UNDEFINED;
        asus_chg->dual_charge = SINGLE;
        asus_chg->asus_flow_processing_flag = false;
        break;
    }

	if(typec_wa_flag) {
		// reset ICL_OVERRIDE(0x1341[1] = 1)
		rc = smblib_masked_write(chg, CMD_APSD_REG,
					ICL_OVERRIDE_BIT, ICL_OVERRIDE_BIT);
		CHG_DBG("%s: Set icl override for typec 1.5A/3A WA\n", __func__);
	}

    set_icl = icl_cfg / 25;
    if (set_icl > USB_ICL_MAX) {
        set_icl = USB_ICL_MAX;
    }
    if (set_icl < 0) {
        set_icl = 0;
    }
    rc = smblib_masked_write(chg, USBIN_CURRENT_LIMIT_CFG_REG,
        USBIN_CURRENT_LIMIT_MASK, set_icl);
    if (rc < 0)
        CHG_DBG_E("%s: Failed to set USBIN_CURRENT_LIMIT\n", __func__);
    //~ vote(chg->usb_icl_votable, USB_PSY_VOTER, true, icl_cfg * 1000);
    smblib_read(chg, USBIN_CURRENT_LIMIT_CFG_REG, &reg);
    CHG_DBG("icl_cfg = %d mA, set_icl = 0x%x, [0x1370] = 0x%x\n", icl_cfg, set_icl, reg);
    // reset ICL_OVERRIDE(0x1341[1] = 1)
    rc = smblib_masked_write(chg, CMD_APSD_REG,
                ICL_OVERRIDE_BIT, ICL_OVERRIDE_BIT);
    CHG_DBG("%s: Set icl override after set icl by table\n", __func__);
    // config usb usbin adapter allowance(5V to 9V) 0x1360 = 0x08
    rc = smblib_masked_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG,
                USBIN_ADAPTER_ALLOW_MASK, 0x08);
    if (rc < 0) {
        CHG_DBG_E("%s: Couldn't set usbin adapter allowance rc=%d\n", __func__, rc);
    }

    chg->asus_chg->last_icl_cfg = set_icl;

    asus_chg->asus_adapter_detecting_flag = false;

    CHG_DBG("do cable capability check in 5s\n");
    cancel_delayed_work(&cable_capability_check_work);
    schedule_delayed_work(&cable_capability_check_work, msecs_to_jiffies(5000));

    CHG_DBG("do soft jeita now\n");
    cancel_delayed_work(&asus_chg->asus_batt_temp_work);
    schedule_delayed_work(&asus_chg->asus_batt_temp_work, msecs_to_jiffies(15000));
    asus_flow_done_flag = 1;
}

void asus_handle_usb_removal(struct smb_charger *chg)
{
    int rc;

    CHG_DBG("triggered, usb removed\n");
    ASUSEvtlog("[USB] set_chg_mode:%s\n", usb_type_str[0]);

    cancel_delayed_work(&chg->asus_chg->asus_handle_usb_insertion_work);
    cancel_delayed_work(&chg->asus_chg->asus_adapter_adc_normal_work);
    cancel_delayed_work(&chg->asus_chg->asus_adapter_adc_rerun_work);
    cancel_delayed_work(&chg->asus_chg->asus_qc3_wa_adc_det_work);
    cancel_delayed_work(&chg->asus_chg->asus_batt_temp_work);
    cancel_delayed_work(&reset_icl_work);
    cancel_delayed_work(&reset_icl_with_override_work);

    chg->asus_chg->asus_charging_type = NONE;
    chg->asus_chg->dual_charge = UNDEFINE;
    chg->asus_chg->asus_qc_flag = 0;
    chg->asus_chg->ufp_mode = 0;
    chg->asus_chg->legacy_cable_flag = true;
    chg->asus_chg->asus_adapter_detecting_flag = false;
    chg->asus_chg->asus_flow_processing_flag = false;
    chg->asus_chg->quick_charge_ac_flag = 0;
    chg->asus_chg->BR_countrycode_read_pending = false;
    asus_flow_done_flag = 0;
    ASUS_ADAPTER_ID = ADC_NOT_READY;
    g_legacy_det_done = false;
    chg->asus_chg->adc_redet_flag = false;
    chg->asus_chg->flt_chg_wa_en = true;
    chg->asus_chg->flt_chg_chk_cnt = 0;
    if(g_CDP_WA)
        --g_CDP_WA;

    if (chg->asus_chg->thermal_alert_release_pending_flag) {
        chg->asus_chg->thermal_alert_release_pending_flag = false;
        // usb alert = 0, clear state
        chg->asus_chg->usb_connector_event = OTP_NOT_TRIGGER;
        extcon_set_state_sync_asus(&chg->asus_chg->usb_thermal_dev, OTP_NOT_TRIGGER);// not trigger
        // Enable usb charging
        if (g_asus_hw_id > 2) { // do thermal alert action after ER
            // enable USB
            if (charger_batt_enable)
                smblib_set_usb_suspend(chg, false);
        }
    }

	rc = smblib_masked_write(chg, OTG_CURRENT_LIMIT_CFG_REG,
                OTG_CURRENT_LIMIT_MASK, 0x05);//set to 1.5A after usb remove
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set otg icl rc=%d\n", rc);
    }

    rc = smblib_masked_write(chg, USBIN_CURRENT_LIMIT_CFG_REG,
        USBIN_CURRENT_LIMIT_MASK, 0x14);// icl = 500mA
    //~ vote(chg->usb_icl_votable, USB_PSY_VOTER, true, 500000);
    rc = smblib_write(chg, USBIN_ICL_OPTIONS_REG, 0x02);
    if (rc < 0) {
        CHG_DBG_E("%s: Couldn't set usbin icl options rc=%d\n", __func__, rc);
    }
    // Enable BC1.2 && HVDCP
    rc = smblib_write(chg, USBIN_OPTIONS_1_CFG_REG, 0x7D);
    if (rc < 0)
        CHG_DBG_E("%s: Failed to set USBIN_OPTIONS_1_CFG_REG\n", __func__);

    if(asp1690_ready) {
        SwitchTo1D();
        CHG_DBG("switch to  1D\n");
    }

    // usb suspend when icl <= 25mA
    rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
            SUSPEND_ON_COLLAPSE_USBIN_BIT, 1);
    if (rc < 0) {
        CHG_DBG_E("%s: Couldn't set SUSPEND_ON_COLLAPSE_USBIN_BIT rc=%d\n", __func__, rc);
    }

    if(adc_check_lock.active){
		__pm_relax(&adc_check_lock);
    }
 
     chg->asus_chg->last_icl_cfg = 0x14;
   
    alarm_cancel(&jeita_alarm);
}

void asus_usb_plugin_flow(struct smb_charger *chg, bool vbus_rising)
{
    CHG_DBG("%s: start, vbus_rising = %d, flow_processing = %d\n", __func__, vbus_rising, chg->asus_chg->asus_flow_processing_flag);
    if (vbus_rising) { // usb plugin
		if(!chg->asus_chg->asus_flow_processing_flag) {
			chg->asus_chg->asus_flow_processing_flag = true;
			CHG_DBG("triggered, usb inserted\n");
			chg->asus_chg->vbus_rising_flag = true;
			schedule_delayed_work(&chg->asus_chg->set_usb_connector_work, msecs_to_jiffies(1000));
			asus_charger_pre_config(chg);
			chg->asus_chg->asus_adapter_detecting_flag = true;
			cancel_delayed_work(&chg->asus_chg->asus_handle_usb_insertion_work);
			if (!g_charger_mode) {
				schedule_delayed_work(&chg->asus_chg->asus_handle_usb_insertion_work, msecs_to_jiffies(4500));// TODO: adjust delay time
			} else {
				schedule_delayed_work(&chg->asus_chg->asus_handle_usb_insertion_work, msecs_to_jiffies(5000));// TODO: adjust delay time
			}
		}
    } else { // usb removal
        asus_handle_usb_removal(chg);
    }
}

#define BR_COUNTRYCODE_PROC_FILE	"br_countrycode_prop"
static struct proc_dir_entry *br_countrycode_proc_file;
static int br_countrycode_proc_read(struct seq_file *buf, void *v)
{
    seq_printf(buf, "br_countrycode:<%d>\n", chip_dev->chg.asus_chg->BR_countrycode_flag);
    return 0;
}

static int br_countrycode_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, br_countrycode_proc_read, NULL);
}
static ssize_t br_countrycode_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    int val = 0;

    char messages[256] = { '\0' };

    if (len > 256) {
        len = 256;
    }

    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    val = (int)simple_strtol(messages, NULL, 10);

    printk("[BAT][Proc][Prop]br_countrycode_prop: %d\n", val);
    if((COUNTRY_BR == val) || (COUNTRY_IN == val) || (COUNTRY_OTHER == val)) {
        chip_dev->chg.asus_chg->BR_countrycode_flag = val;
    }
    return len;
}

static const struct file_operations br_countrycode_fops = {
	.owner = THIS_MODULE,
	.open = br_countrycode_proc_open,
	.write = br_countrycode_proc_write,
	.read = seq_read,
	.release = single_release,
};
static void create_br_countrycode_proc_file(void)
{
	br_countrycode_proc_file = proc_create(BR_COUNTRYCODE_PROC_FILE, 0644, NULL, &br_countrycode_fops);

	if (br_countrycode_proc_file) {
		printk("[BAT][Proc][Prop]br_countrycode_prop create sucessed!\n");
	} else{
		printk("[BAT][Proc][Prop]br_countrycode_prop file create failed!\n");
	}
}

#define boot_completed_PROC_FILE	"boot_completed_prop"
static struct proc_dir_entry *boot_completed_proc_file;
static int boot_completed_proc_read(struct seq_file *buf, void *v)
{
    seq_printf(buf, "boot_completed:<%d>\n", chip_dev->chg.asus_chg->boot_completed_flag);
    return 0;
}

static int boot_completed_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, boot_completed_proc_read, NULL);
}

extern void asus_check_rconn(void);
void asus_rconn_check_work_func(struct work_struct *work)
{
    asus_check_rconn();
}

static ssize_t boot_completed_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    int val = 0;

    char messages[256] = { '\0' };

    if (len > 256) {
        len = 256;
    }

    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    val = (int)simple_strtol(messages, NULL, 10);

    printk("[BAT][Proc][Prop]boot_completed_prop: %d\n", val);
    if(val){
        printk("[BAT][Proc][Prop]set boot_completed to 1 \n");
        chip_dev->chg.asus_chg->boot_completed_flag = 1;
        g_boot_complete = true;
    }

    schedule_delayed_work(&asus_rconn_check_work, 0);
    return len;
}

static const struct file_operations boot_completed_fops = {
	.owner = THIS_MODULE,
	.open = boot_completed_proc_open,
	.write = boot_completed_proc_write,
	.read = seq_read,
	.release = single_release,
};
static void create_boot_completed_proc_file(void)
{
	boot_completed_proc_file = proc_create(boot_completed_PROC_FILE, 0644, NULL, &boot_completed_fops);

	if (boot_completed_proc_file) {
		printk("[BAT][Proc][Prop]boot_completed_prop create sucessed!\n");
	} else{
		printk("[BAT][Proc][Prop]boot_completed_prop file create failed!\n");
	}
}

void asus_set_usb_connector_work ( struct work_struct *work)
{
    struct asus_charger *asus_chg = container_of(work,
                    struct asus_charger,
                    set_usb_connector_work.work);
    struct smb_charger *chg = &chip_dev->chg;
    int status;
    int usb_present;
    //~ int rc;
    int trigger = USB_THERM_ALERT_NOT_TRIGGER;
    int otg_en = smblib_vbus_regulator_is_enabled(chg->vbus_vreg->rdev);

    if (asp1690_ready && !asus_chg->suspend_flag) {
        status = gpio_get_value(asus_chg->usb_thermal_alert_irq);
        usb_present = asus_get_prop_usb_present(chg);
        trigger = usb_therm_trigger();
        if (trigger < 0) {
            printk("%s: asp1690 not up yet.\n", __func__);
        }
        printk("%s: interrupt pin status = %d, usb_present = %d, otg_en = %d, trigger = %d\n", __func__, status, usb_present, otg_en, trigger);

        switch(trigger) {
        case USB_THERM_ALERT_LOW_TRIGGER: // temp over 70 deg
            printk("%s: USB_THERM_ALERT_LOW_TRIGGER\n", __func__);
            // set thresh to 60~max deg
            usb_therm_set_thresh_2();
            asus_chg->thermal_alert_release_pending_flag = false;
            // usb alert = 1
            if (usb_present) { // usb in
                // TODO: judge whether in charger mode
                asus_chg->usb_connector_event = OTP_TRIGGER_WITH_AC;
                ASUSErclog(ASUS_USB_THERMAL_ALERT, "Thermal Alert is triggered, cable 1 otg 0");
                if (g_ready_to_report_2 || g_charger_mode)
					extcon_set_state_sync_asus(&asus_chg->usb_thermal_dev, OTP_TRIGGER_WITH_AC);// with AC
                if (g_asus_hw_id > 2) { // do thermal alert action after ER
                    // USB suspend
                    smblib_set_usb_suspend(&chip_dev->chg, true);
                    //~ // Set as DFP
                    //~ rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
                                 //~ 0x06, 0x02);
                    //~ if (rc < 0) {
                        //~ pr_err("Couldn't set DFP mode rc=%d\n", rc);
                    //~ }
                    // Disable otg
                    smblib_vbus_regulator_disable(chg->vbus_vreg->rdev);
                }
            } else if(otg_en) { // otg en
                printk("%s: usb not present, but otg enabled\n", __func__);
                asus_chg->usb_connector_event = OTP_TRIGGER_WITH_AC;
                ASUSErclog(ASUS_USB_THERMAL_ALERT, "Thermal Alert is triggered, cable 1 otg 1");
                if (g_ready_to_report_2 || g_charger_mode)
					extcon_set_state_sync_asus(&asus_chg->usb_thermal_dev, OTP_TRIGGER_WITH_AC);// with AC
                if (g_asus_hw_id > 2) { // do thermal alert action after ER
                    // Disable otg
                    smblib_vbus_regulator_disable(chg->vbus_vreg->rdev);
                }
            } else {
                printk("%s: no cable connected\n", __func__);
                asus_chg->usb_connector_event = OTP_TRIGGER_WITHOUT_AC;
                ASUSErclog(ASUS_USB_THERMAL_ALERT, "Thermal Alert is triggered, cable 0 otg 0");
                if (g_ready_to_report_2 || g_charger_mode)
					extcon_set_state_sync_asus(&asus_chg->usb_thermal_dev, OTP_TRIGGER_WITHOUT_AC);// without AC
                if (g_asus_hw_id > 2) { // do thermal alert action after ER
                    // USB suspend
                    smblib_set_usb_suspend(&chip_dev->chg, true);
                }
            }
            break;
        case USB_THERM_ALERT_HIGH_TRIGGER:// temp below 60 deg
            printk("%s: USB_THERM_ALERT_HIGH_TRIGGER\n", __func__);
            // set thresh to min~70 deg
            usb_therm_set_thresh_1();
            ASUSErclog(ASUS_USB_THERMAL_ALERT, "Thermal Alert is dismissed");
            extcon_set_state_sync_asus(&asus_chg->usb_thermal_dev, OTP_NOT_TRIGGER);// not trigger
            // release alert and enable charging only when cable not in
            if (!usb_present) {
                // usb alert = 0, clear state
                asus_chg->usb_connector_event = OTP_NOT_TRIGGER;
                //extcon_set_state_sync_asus(&asus_chg->usb_thermal_dev, OTP_NOT_TRIGGER);// not trigger
                // Enable usb charging
                if (g_asus_hw_id > 2) { // do thermal alert action after ER
                    // enable USB
                    if (charger_batt_enable)
                        smblib_set_usb_suspend(&chip_dev->chg, false);
                }
            } else {
                asus_chg->thermal_alert_release_pending_flag = true;
            }
            break;
        case USB_THERM_ALERT_NOT_TRIGGER:
        default:
            printk("%s: USB_THERM_ALERT_NOT_TRIGGER\n", __func__);
            break;
        }
        printk("%s: usb_connector_event = %d, release_pending = %d\n", __func__, asus_chg->usb_connector_event, asus_chg->thermal_alert_release_pending_flag);
    }
}

void asus_usb_alert_detect_work(struct work_struct *work)
{
    struct asus_charger *asus_chg = container_of(work,
                    struct asus_charger,
                    check_usb_connector_work.work);
	static int first_time = true;
    if (asus_chg->boot_completed_flag) {
		if (first_time) {
			printk("[BAT][CHG] USB_alert boot completed first time, usb_thermal status = %d, report UI 5s later\n", asus_chg->usb_connector_event);
			first_time = false;
			schedule_delayed_work(&asus_chg->check_usb_connector_work, msecs_to_jiffies(5000));
			return;
		}

        printk("[BAT][CHG] USB_alert boot completed, usb_thermal status = %d\n", asus_chg->usb_connector_event);
        g_ready_to_report_2 = true;
        if(!g_charger_mode) {
            // report the event to UI when ims ready
            extcon_set_state_sync_asus(&asus_chg->usb_thermal_dev, asus_chg->usb_connector_event);
        }
    } else {
        schedule_delayed_work(&asus_chg->check_usb_connector_work, msecs_to_jiffies(5000));
        printk("[BAT][CHG] USB_alert boot NOT completed yet, retry 5s\n");
    }
}

static irqreturn_t usb_thermal_alert_handler(int irq, void *_chg)
{
    struct smb_charger *chg = _chg;
    int status;

    status = gpio_get_value(chg->asus_chg->usb_thermal_alert_irq);
    printk("%s: usb_thermal_irq pin status =%d\n",__func__, status);
    if (g_boot_complete || g_charger_mode)
		schedule_delayed_work(&chg->asus_chg->set_usb_connector_work,0);

    return IRQ_HANDLED;
}

void asus_usb_thermal_alert_gpio_init(struct smb_charger *chg)
{
    int rc;
    int irq_num;
    struct asus_charger *asus_chg = chg->asus_chg;
    struct device_node *node = chg->dev->of_node;
    // gpio init
    asus_chg->usb_thermal_alert_irq = of_get_named_gpio(node,"asp1690_int",0);
    if ((!gpio_is_valid(asus_chg->usb_thermal_alert_irq))) {
        pr_err("%s: usb_thermal_alert_irq is not valid!\n", __FUNCTION__);
        return;
    }
    rc = gpio_request(asus_chg->usb_thermal_alert_irq,"asp1690_int");
    if (rc < 0) {
        pr_err("%s: request asp1690_int gpio fail!\n", __FUNCTION__);
        goto error_out;
    }
    rc = gpio_direction_input(asus_chg->usb_thermal_alert_irq);
    if (rc < 0) {
        pr_err("%s: set direction of asp1690_int fail!\n", __FUNCTION__);
        goto error_out;
    }
    irq_num = gpio_to_irq(asus_chg->usb_thermal_alert_irq);
    if (irq_num < 0) {
        pr_err("gpio(%d)_to_irq failed\n", asus_chg->usb_thermal_alert_irq);
        goto error_out;
    }
    rc = request_threaded_irq(irq_num, NULL, usb_thermal_alert_handler,
        IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
        "usb_low_impedance", chg);
    if (rc < 0) {
        pr_err("%s: Failed to request usb_thermal_alert_irq\n", __FUNCTION__);
        goto error_out;
    }
    // extcon dev register
    asus_chg->usb_thermal_dev.supported_cable = usb_connector_ext_supported_cable;
    asus_chg->usb_thermal_dev.name = "usb_connector";
    dev_set_name(&asus_chg->usb_thermal_dev.dev, "usb_connector");
    rc = extcon_dev_register(&asus_chg->usb_thermal_dev);
    if (rc) {
        pr_err("usb_connector extcon registration failed");
        goto error_out;
    }
    if(g_charger_mode == 0)
        schedule_delayed_work(&asus_chg->check_usb_connector_work, msecs_to_jiffies(5000));

    return;

error_out:
    gpio_free(asus_chg->usb_thermal_alert_irq);
}

void asus_charger_gpio_init(struct smb_charger *chg)
{
    asus_usb_thermal_alert_gpio_init(chg);
}

void asus_bob_regulator_init(struct smb_charger *chg)
{
	int rc = 0;

	/* fetch the BOB regulator */
	if (!chg->bob_vreg && of_get_property(chg->dev->of_node,
				"bob-supply", NULL)) {
		chg->bob_vreg = devm_regulator_get(chg->dev, "bob");
		if (IS_ERR(chg->bob_vreg)) {
			rc = PTR_ERR(chg->bob_vreg);
			smblib_err(chg, "Couldn't get bob regulator rc=%d\n",
					rc);
			chg->bob_vreg = NULL;
		}
	}

	/* fetch the BOB_AO regulator */
	if (!chg->bob_ao_vreg && of_get_property(chg->dev->of_node,
				"bob_ao-supply", NULL)) {
		chg->bob_ao_vreg = devm_regulator_get(chg->dev, "bob_ao");
		if (IS_ERR(chg->bob_ao_vreg)) {
			rc = PTR_ERR(chg->bob_ao_vreg);
			smblib_err(chg, "Couldn't get bob_ao regulator rc=%d\n",
					rc);
			chg->bob_ao_vreg = NULL;
		}
	}
}

void asus_bob_mode_switch(struct smb_charger *chg, bool incall)
{
    int rc;

    if (chg->bob_vreg == NULL || chg->bob_ao_vreg == NULL) {
        pr_err("BOB or BOB_AO regulator not init yet\n");
        return ;
    }

    if (incall) {
        rc = regulator_set_mode(chg->bob_vreg, REGULATOR_MODE_FAST);
        if (rc < 0) {
            pr_err("BOB regulator_set_mode failed. rc=%d\n", rc);
            return ;
        }
        pr_info("set BOB to pwm in sleep set\n");
        rc = regulator_set_mode(chg->bob_ao_vreg, REGULATOR_MODE_FAST);
        if (rc < 0) {
            pr_err("BOB_AO regulator_set_mode failed. rc=%d\n", rc);
            return ;
        }
        pr_info("set BOB_AO to pwm in active set\n");
    } else {
        rc = regulator_set_mode(chg->bob_vreg, REGULATOR_MODE_STANDBY);
        if (rc < 0) {
            pr_err("BOB regulator_set_mode failed. rc=%d\n", rc);
            return ;
        }
        pr_info("set BOB to pass in sleep set\n");
        rc = regulator_set_mode(chg->bob_ao_vreg, REGULATOR_MODE_NORMAL);
        if (rc < 0) {
            pr_err("BOB_AO regulator_set_mode failed. rc=%d\n", rc);
            return ;
        }
        pr_info("set BOB_AO to auto in active set\n");
    }
}

#define bob_mode_switch_PROC_FILE	"bob_mode_switch"
static struct proc_dir_entry *bob_mode_switch_proc_file;
static int bob_mode_switch_proc_read(struct seq_file *buf, void *v)
{
    seq_printf(buf, "g_receiver_enable:<%d>\n", g_receiver_enable);
    return 0;
}

static int bob_mode_switch_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, bob_mode_switch_proc_read, NULL);
}

static ssize_t bob_mode_switch_proc_write(struct file *filp, const char __user *buff,
        size_t len, loff_t *data)
{
    int val = 0;

    char messages[256] = { '\0' };

    if (len > 256) {
        len = 256;
    }

    if (copy_from_user(messages, buff, len)) {
        return -EFAULT;
    }

    if (g_asus_prj_id == 0) {
        val = (int)simple_strtol(messages, NULL, 10);

        printk("%s: [BAT][Proc] receiver enable: %d\n", __func__, val);
        if (chip_dev == NULL) {
            printk("%s: [BAT][Proc] charger driver not ready yet\n", __func__);
            return len;
        }
        if (val) {
            printk("[BAT][Proc] set bob mode to force PWM mode\n");
            asus_bob_mode_switch(&chip_dev->chg, 1);
            g_receiver_enable = true;
        } else {
            printk("[BAT][Proc] set bob mode to bypass mode\n");
            asus_bob_mode_switch(&chip_dev->chg, 0);
            g_receiver_enable = false;
        }
    } else {
        printk("[BAT][Proc] skip bob mode switch in ZS620KL2\n");
    }

    return len;
}

static const struct file_operations bob_mode_switch_fops = {
	.owner = THIS_MODULE,
	.open = bob_mode_switch_proc_open,
	.write = bob_mode_switch_proc_write,
	.read = seq_read,
	.release = single_release,
};
static void create_bob_mode_switch_proc_file(void)
{
	bob_mode_switch_proc_file = proc_create(bob_mode_switch_PROC_FILE, 0644, NULL, &bob_mode_switch_fops);

	if (bob_mode_switch_proc_file) {
		printk("[BAT][Proc] %s sucessed!\n", __func__);
	} else{
		printk("[BAT][Proc] %s failed!\n", __func__);
	}
}

void asus_update_rechg_volt_by_fv(struct smb_charger *chg, int fv_uv)
{
    int rc = 0;
    u8 rechg_volt = RCHG_4V147;
    switch(fv_uv) {
    case 4357500:
        rechg_volt = RCHG_4V147;
        break;
    case 4300000:
        rechg_volt = RCHG_4V094;
        break;
    case 4250000:
        rechg_volt = RCHG_4V049;
        break;
    default:
        break;
    }
    chg->asus_chg->asus_tb.rchg_cfg = rechg_volt;
    rc = smblib_masked_write(chg, FVC_RECHARGE_THRESHOLD_CFG_REG,
                FVC_RECHARGE_THRESHOLD_MASK, chg->asus_chg->asus_tb.rchg_cfg);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set recharge threshold rc=%d\n", rc);
    } else {
        CHG_DBG("%s: recharge voltage update(0x1081=0x%02x)\n", __func__, chg->asus_chg->asus_tb.rchg_cfg);
    }
}

void asus_charger_init_config(struct smb_charger *chg)
{
    int rc;

    chg->asus_chg->asus_tb.pcc_cfg = PCC_250MA;
    chg->asus_chg->asus_tb.fcc_cfg = FCC_1500MA;
    chg->asus_chg->asus_tb.fv_cfg = ASUS_FlOAT_4V357;
    chg->asus_chg->asus_tb.rchg_cfg = RCHG_4V147;
    chg->asus_chg->asus_tb.tcc_cfg = TCC_100MA;
    chg->asus_chg->asus_tb.jeita_fcc_cfg = draco_evb_jeita_fcc_cfg;
    chg->asus_chg->asus_tb.jeita_fv_cfg = ASUS_FlOAT_4V057;

    chg->asus_chg->asus_tb.icl_config_table = draco_evb_icl_cfg;
    chg->asus_chg->asus_tb.icl_config_len = ARRAY_SIZE(draco_evb_icl_cfg);

    chg->asus_chg->asus_tb.jeita_config_table = draco_evb_jeita_cfg;
    chg->asus_chg->asus_tb.jeita_config_len = ARRAY_SIZE(draco_evb_jeita_cfg);
    chg->asus_chg->asus_tb.jeita_rechg_len = ARRAY_SIZE(draco_evb_jeita_rchg_cfg);
    chg->asus_chg->asus_tb.jeita_rechg_table = draco_evb_jeita_rchg_cfg;
    chg->asus_chg->soft_jeita_state = JEITA_STATE_INITIAL;

    chg->asus_chg->asus_charging_type = NONE;
    chg->asus_chg->dual_charge = UNDEFINE;
    chg->asus_chg->asus_qc_flag = 0;
    chg->asus_chg->ufp_mode = 0;
    chg->asus_chg->legacy_cable_flag = true;
    chg->asus_chg->suspend_flag = false;

    chg->asus_chg->boot_completed_flag = 0;
    chg->asus_chg->usb_connector_event = OTP_NOT_TRIGGER;
    chg->asus_chg->qc_stat_registed = false;
    chg->asus_chg->quick_charge_ac_flag = 0;
    chg->asus_chg->vbus_rising_flag = false;
    chg->asus_chg->asp1690_disable_flag = false;
    chg->asus_chg->thermal_alert_release_pending_flag = false;
    chg->asus_chg->asus_flow_processing_flag = false;
    chg->asus_chg->BR_countrycode_read_pending = false;
    chg->asus_chg->BR_countrycode_flag=0;
    chg->asus_chg->pon_cable_det_flag = true;
    chg->asus_chg->cdp_high_wa_flag = false;
    chg->asus_chg->jeita_stop_charge_flag = false;
    chg->asus_chg->legacy_detecting_flag = false;
    chg->asus_chg->last_batt_health = POWER_SUPPLY_HEALTH_GOOD;
    chg->asus_chg->bat_ovp_flag = false;
    chg->asus_chg->last_icl_cfg = 0;
    chg->asus_chg->adc_redet_flag = false;
    chg->asus_chg->flt_chg_wa_en = true;
    chg->asus_chg->flt_chg_chk_cnt = 0;

#ifdef ASUS_FACTORY_BUILD
    charger_limit_setting = 70;
#else
    charger_limit_setting = 60;
#endif
    asus_flow_done_flag = 0;
	fcc_override_setting = -1;
	asus_fcc_override_flag = 0;
	vbat_avg_1 = 0;
	vbat_avg_2 = 0;

    // 2.config float voltage
    rc = vote(chg->fv_votable, BATT_PROFILE_VOTER, true, chg->asus_chg->asus_tb.fv_cfg);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set float voltage rc=%d\n", rc);
    }
    // 3.config termination charge current
    rc = smblib_masked_write(chg, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
                TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, chg->asus_chg->asus_tb.tcc_cfg);
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set termination charge current rc=%d\n", rc);
    }
    // TODO: config system tcc
    // 4. config type-c 80uA current source
    rc = smblib_masked_write(chg, TYPE_C_CFG_2_REG,
                EN_80UA_180UA_CUR_SOURCE_BIT, 0);// 80uA
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set type-c 80uA current source rc=%d\n", rc);
    }
    // 5. config OTG input current limit
    rc = smblib_masked_write(chg, OTG_CURRENT_LIMIT_CFG_REG,
                OTG_CURRENT_LIMIT_MASK, 0x05);// init 1.5A //750mA
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set otg icl rc=%d\n", rc);
    }
    // 12. config charger jeita en cfg
    rc = smblib_write(chg, JEITA_EN_CFG_REG, 0x10);// JEITA_EN_HARDLIMIT
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set charger jeita en cfg rc=%d\n", rc);
    }
    // TODO: config MAX_PD_INPUT_VOLTAGE_CFG

    // USBIN_IN_COLLAPSE_GF_SEL
    rc = smblib_masked_write(chg, USBIN_LOAD_CFG_REG,
            GENMASK(1, 0), 0x3);// bit[1:0] = 11 (30us)
    if (rc < 0) {
        dev_err(chg->dev, "Couldn't set USBIN_IN_COLLAPSE_GF_SEL rc=%d\n", rc);
    }

#ifdef ASUS_FACTORY_BUILD
    // disable inov in factory build
    rc = smblib_write(&chip_dev->chg, THERMREG_SRC_CFG_REG, 0x00);
    if (rc < 0)
        pr_err("Couldn't set THERMREG_SRC_CFG_REG 0x0\n");
#endif

    INIT_DELAYED_WORK(&chg->asus_chg->asus_handle_usb_insertion_work, asus_handle_usb_insertion);
    INIT_DELAYED_WORK(&chg->asus_chg->asus_adapter_adc_normal_work, asus_adapter_adc_normal_work);
    INIT_DELAYED_WORK(&chg->asus_chg->asus_adapter_adc_rerun_work, asus_adapter_adc_rerun_work);
    INIT_DELAYED_WORK(&chg->asus_chg->asus_qc3_wa_adc_det_work, asus_qc3_wa_adc_det_work);
    INIT_DELAYED_WORK(&chg->asus_chg->asus_batt_temp_work, asus_batt_temp_work);
    INIT_DELAYED_WORK(&charging_limit_work,asus_battery_charging_limit);
    INIT_DELAYED_WORK(&chg->asus_chg->set_usb_connector_work, asus_set_usb_connector_work);
    INIT_DELAYED_WORK(&chg->asus_chg->check_usb_connector_work, asus_usb_alert_detect_work);
    INIT_DELAYED_WORK(&cable_capability_check_work, asus_cable_cap_check_work);
    INIT_DELAYED_WORK(&chg->asus_chg->asus_handle_otg_insertion_work, asus_handle_otg_insertion);
    INIT_DELAYED_WORK(&chg->asus_chg->fcc_override_work, asus_fcc_override_work);
    INIT_DELAYED_WORK(&chg->asus_chg->vbat_avg_work, asus_vbat_avg_work);
    INIT_DELAYED_WORK(&SetJeitaRTCWorker, Jeita_SetRTC);
    INIT_DELAYED_WORK(&chg->asus_chg->legacy_det_work, asus_legacy_det_work);
    INIT_DELAYED_WORK(&asus_rconn_check_work, asus_rconn_check_work_func);
    INIT_DELAYED_WORK(&reset_icl_work, asus_reset_icl_work);
    INIT_DELAYED_WORK(&reset_icl_with_override_work, asus_reset_icl_with_override_work);

    wakeup_source_init(&adc_check_lock, "adc_check_Lock");
    wakeup_source_init(&UsbCable_Lock, "UsbCable_Lock_Wake");
    wakeup_source_init(&ChargerModeLock, "ChargerModeLock");
    alarm_init(&jeita_alarm, ALARM_REALTIME, Jeita_Alarm_handler);

    create_asus_batt_charge_enable_proc_file();
    create_charger_limit_proc_file();
    create_asus_charge_limit_enable_proc_file();
    create_asus_charge_type_proc_file();
    create_asus_chargerIC_status_proc_file();
    create_boot_completed_proc_file();
    create_asus_charger_ntc_proc_file();
    create_thermal_test_chg_proc_file();
    create_asus_charger_inov_enable_proc_file();
    create_vbat_fcc_test_proc_file();
    create_vbat_avg_1_proc_file();
    create_vbat_avg_2_proc_file();
    create_vbat_fcc_setting_1_proc_file();
    create_vbat_fcc_setting_2_proc_file();
    create_fake_batt_capacity_proc_file();
    create_br_countrycode_proc_file();
    create_smartchg_stop_chg_proc_file();
    create_bob_mode_switch_proc_file();

    asus_charger_gpio_init(chg);
    asus_register_qc_stat(chg);
    asus_bob_regulator_init(chg);
}

int asus_charger_porting(struct smb2 *chip, struct platform_device *pdev)
{
	if(!chip){
		CHG_DBG_E("struct smb2 is NULL,Now will return!\n");
		return -1;
	}
	chip->chg.asus_chg = devm_kzalloc(&pdev->dev, sizeof(*chip->chg.asus_chg), GFP_KERNEL);
	if (!chip->chg.asus_chg){
		CHG_DBG_E("asus_chg is NULL!\n");
		return -1;
	}

	chip->chg.asus_capacity = -1;

	asus_charger_init_config(&chip->chg);

	add_asus_charger_log(chip);

	return 0;
}
