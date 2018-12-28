#ifndef __ASUS_CHARGER_H
#define __ASUS_CHARGER_H

#include <linux/power_supply.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/extcon.h>
#include "smb-lib.h"
#include "smb-reg.h"
#include "linux/asusdebug.h"
#include <linux/rtc.h>
#include <linux/ktime.h>
#include <linux/alarmtimer.h>

enum asus_charging_type {
	PD,
	HVDCP_ASUS_200K_2A,
	HVDCP_OTHERS_1A,
	HVDCP_OTHERS_1P5A,
	HVDCP_OTHERS_PB_1A,
	DCP_ASUS_750K_2A,
	DCP_ASUS_200K_2A,
	DCP_PB_2A,
	TYPEC_1P5A,
	TYPEC_3P0A,
	DCP_OTHERS_1A,
	DCP_2A_BR_IN,
	SDP_0P5A,
	CDP_1P5A,
	FLOATING_0P5A,
	OTHERS_1A,
	UNDEFINED,
	NONE,
};

enum dual_charge_type{
	ASUS_2A,
	TYPEC_3A,
	SINGLE,
	UNDEFINE,
};

enum JEITA_state_for_all {
	JEITA_STATE_INITIAL,	// Intitial state shall not be changed
	JEITA_STATE_RANGE_01,	// It denotes either below 10 or below 15, depends on the project
	JEITA_STATE_RANGE_02,	// It denotes either between 10 to 100 or beteen 15 to 100, depends on the project
	JEITA_STATE_RANGE_03,	// It denotes between 100 to 200
	JEITA_STATE_RANGE_04,	// It denotes between 200 to 500
	JEITA_STATE_RANGE_05,	// It denotes between 500 to 600
	JEITA_STATE_RANGE_06,	// It denotes beyond 600
};

enum usb_thermal_alert_type{
	OTP_NOT_TRIGGER,
	OTP_TRIGGER_WITHOUT_AC,
	OTP_TRIGGER_WITH_AC,
};

struct asus_version_tables{
	//pre_config
	int pcc_cfg;
	int fcc_cfg;
	int fv_cfg;
	int rchg_cfg;
	int tcc_cfg;
	
	const int *icl_config_table;
	int icl_config_len;

	int jeita_fv_cfg;
	const int *jeita_fcc_cfg;
	const int *jeita_config_table;
	int jeita_config_len;
	const int *jeita_rechg_table;
	int jeita_rechg_len;
	const int *asus_adapter_adc_table;
	
	int adc_read_addr;
};

struct asus_charger{
	struct delayed_work battery_poll_data_work;
	struct delayed_work asus_handle_usb_insertion_work;
	struct delayed_work asus_adapter_adc_work;
	struct delayed_work asus_batt_temp_work;
	struct delayed_work set_usb_connector_work;
	struct delayed_work check_usb_connector_work;
	struct delayed_work asus_handle_otg_insertion_work;
	struct delayed_work fcc_override_work;
	struct delayed_work vbat_avg_work;
	struct delayed_work legacy_det_work;
	struct iio_channel *aux_therm_chan;
	enum asus_charging_type asus_charging_type;
	enum dual_charge_type dual_charge;
	struct asus_version_tables asus_tb;
	int asus_qc_flag;
	int ufp_mode;
	bool legacy_cable_flag;
	bool boot_completed_flag;
	bool suspend_flag;
	bool qc_stat_registed;
	bool asus_adapter_detecting_flag;
	bool asus_flow_processing_flag;
	bool pon_cable_det_flag;
	bool cdp_high_wa_flag;
	int quick_charge_ac_flag;
	bool vbus_rising_flag;
	bool asp1690_disable_flag;
	bool thermal_alert_release_pending_flag;
	int BR_countrycode_flag;
	bool BR_countrycode_read_pending;
	int soft_jeita_state;
	int usb_thermal_alert_irq;
	int usb_connector_event;
	struct extcon_dev usb_thermal_dev;
	struct extcon_dev qc_stat_dev;
	bool jeita_stop_charge_flag;
	bool legacy_detecting_flag;
	int last_batt_health;
	bool bat_ovp_flag;
	u8 last_icl_cfg;
};

struct smb_dt_props {
	int	usb_icl_ua;
	int	dc_icl_ua;
	int	boost_threshold_ua;
	int	wipower_max_uw;
	int	min_freq_khz;
	int	max_freq_khz;
	struct	device_node *revid_dev_node;
	int	float_option;
	int	chg_inhibit_thr_mv;
	bool	no_battery;
	bool	hvdcp_disable;
	bool	auto_recharge_soc;
	int	wd_bark_time;
};

struct smb2 {
	struct smb_charger	chg;
	struct dentry		*dfs_root;
	struct smb_dt_props	dt;
	bool			bad_part;
};

struct battery_info_reply {
	u32 capacity;
	int rawsoc;
	int Rsoc;
	u32 voltage_now;
	int current_now;
	char *temperature_negative_sign;
	int temperature;
	int temperature10;
	u32 cable_status;
	int status;
	int fcc;
	bool charging_toggle;
	bool otg_toggle;
	int usb_supply_type;
};

#define CHARGER_TAG "[SMB][CHG]"
#define ERROR_TAG "[ERR]"
#define CHG_DBG(...)  printk(KERN_INFO CHARGER_TAG __VA_ARGS__)
#define CHG_DBG_E(...)  printk(KERN_ERR CHARGER_TAG ERROR_TAG __VA_ARGS__)

// default parameters +++
#define PCC_250MA 		BIT(1)|BIT(0)
#define FCC_1500MA 		1500000
#define ASUS_FlOAT_4V357 		4357500
#define ASUS_FlOAT_4V057 		4057500
#define RCHG_4V252 		0x66
#define RCHG_4V147 		0x58
#define RCHG_4V094 		0x51
#define RCHG_4V049 		0x4B
#define TCC_150MA 		0x03
#define TCC_100MA 		0x02
// default parameters ---

#define USB_THERM_ALERT_LOW_TRIGGER 1
#define USB_THERM_ALERT_HIGH_TRIGGER 2
#define USB_THERM_ALERT_NOT_TRIGGER 0

#define AC_GT_10W 1
#define AC_EQ_10W 2

#define COUNTRY_OTHER 0
#define COUNTRY_BR 1
#define COUNTRY_IN 2

#define	UBATLIFE_DISCHG_THD		60
#define	UBATLIFE_CHG_THD		58

#define DCP_LIKE_AICL_RERUN_THRESH 800 // mA
#define SDP_LIKE_AICL_RERUN_THRESH 400

extern struct smb2 *chip_dev;
extern bool asus_flow_done_flag;
extern int g_asus_hw_id;
extern bool g_charger_mode;
extern bool g_boot_complete;
extern int g_CDP_WA;
extern bool g_legacy_det_done;
extern int g_asus_prj_id;
int asus_charger_porting(struct smb2 *chip, struct platform_device *pdev);
void asus_charger_pre_config(struct smb_charger *chg);
void asus_handle_usb_removal(struct smb_charger *chg);
void asus_adapter_adc_det(struct smb_charger *chg);
void asus_hvdcp3_wa(struct smb_charger *chg);
int asus_smbchg_suspend(struct device *dev);
int asus_smbchg_resume(struct device *dev);
void asus_set_qc_stat(struct smb_charger *chg, union power_supply_propval *val);
int asus_get_prop_batt_capacity(struct smb_charger *chg);
void asus_log_chg_mode(u8 apsd_result_bit);
void asus_usb_plugin_flow(struct smb_charger *chg, bool vbus_rising);
void asus_update_rechg_volt_by_fv(struct smb_charger *chg, int fv_uv);

int asus_set_prop_pd_qc_state(struct smb_charger *chg,
				    const union power_supply_propval *val);
int asus_check_batt_health(struct smb_charger *chg);

#endif
