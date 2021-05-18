#ifndef __ASUS_FG_H
#define __ASUS_FG_H

#define BATTERY_TAG "[BAT][BMS]"
#define ERROR_TAG "[ERR]"
#define BAT_DBG(...)  printk(KERN_INFO BATTERY_TAG __VA_ARGS__)
#define BAT_DBG_L(level, ...)  printk(level BATTERY_TAG __VA_ARGS__)
#define BAT_DBG_E(...)  printk(KERN_ERR BATTERY_TAG ERROR_TAG __VA_ARGS__)

#define FAKE_TEMP_INIT	180

int asus_fg_porting(struct fg_chip *chip);
void asus_set_battery_version(void);
void asus_check_batt_id(struct fg_chip *chip);

extern struct fg_chip *g_fgChip;
extern bool g_charger_mode;
extern int fake_temp;
extern int g_bat_reload_cond;

#define CYCLE_COUNT_DATA_MAGIC  0x620E
#define CYCLE_COUNT_FILE_NAME   "/dev/block/bootdevice/by-name/batinfo"
#define BACKUP_BATINFO_NAME   "/data/local/tmp/.bs"
#define BACKUP_BATINFO_SIZE   (1024*16)
#define BAT_SAFETY_FILE_NAME   "/data/local/tmp/bat_safety"
#define Charging		0
#define DisCharging		1

#define BAT_CONDITION_DATA_COUNT 4
#define BAT_TIME_TYPE_COUNT (BAT_CONDITION_DATA_COUNT-1)
#define BAT_TIME_TYPE_OFFSET 1
#define BAT_TIME_TYPE_HIGH_VOL 0
#define BAT_TIME_TYPE_HIGH_TEMP 1
#define BAT_TIME_TYPE_HIGH_TEMP_VOL 2
#define BAT_TIME_WRITEBACK_THRESH 60 // min

union BAT_COND_DATA {
    unsigned long total_time[BAT_CONDITION_DATA_COUNT];
    struct BAT_CONDITON {
        unsigned long battery_total_time;
        unsigned long high_vol_total_time;
        unsigned long high_temp_total_time;
        unsigned long high_temp_vol_time;
    } item;
};

/* Cycle Count Date Structure saved in emmc
 * magic - magic number for data verification
 * charge_cap_accum - Accumulated charging capacity
 * charge_last_soc - last saved soc before reset/shutdown
 * [0]:battery_soc [1]:system_soc [2]:monotonic_soc
 */
struct CYCLE_COUNT_DATA{
	int magic;
	int charge_cap_accum[3];
	int charge_last_soc[3];
	union BAT_COND_DATA bat_time_accum;
	u32 reload_condition;
};

#define CYCLE_FULL_THRESH_DEFAULT   95 // 95% as one full cycle
#define CYCLE_SOC_FULL  0xFF
#define CYCLE_COUNT_WRITEBACK_THRESH  	25 // 9.8%
#define CYCLE_BATT_SOC_REG			91
#define CYCLE_BATT_SOC_OFFSET			0
#define CYCLE_SYSTEM_SOC_REG		94
#define CYCLE_SYSTEM_SOC_OFFSET	0
#define CYCLE_MONO_SOC_REG		94
#define CYCLE_MONO_SOC_OFFSET	2
#define FILE_OP_READ   0
#define FILE_OP_WRITE   1
#define CYCLE_COUNT_DATA_OFFSET  0x0

#define RECORD_NOT_INIT -2000

enum inde_of_record{
	RECORD_RCONN,
	RECORD_CHARGEFULL,
	RECORD_PENDINGFULL,
};

struct RECORD_DATA{
	int magic;
	int pos;
	int value;
};

#define HIGH_TEMP   350
#define HIGHER_TEMP 450
#define FULL_CAPACITY_VALUE 100
#define BATTERY_USE_TIME_CONDITION1  (12*30*24*60*60) //12Months
#define BATTERY_USE_TIME_CONDITION2  (18*30*24*60*60) //18Months
#define CYCLE_COUNT_CONDITION1  100
#define CYCLE_COUNT_CONDITION2  400
#define HIGH_TEMP_VOL_TIME_CONDITION1 (15*24*60*60)  //15Days
#define HIGH_TEMP_VOL_TIME_CONDITION2 (30*24*60*60)  //30Days
#define HIGH_TEMP_TIME_CONDITION1     (6*30*24*60*60) //6Months
#define HIGH_TEMP_TIME_CONDITION2     (12*30*24*60*60) //12Months
#define HIGH_VOL_TIME_CONDITION1     (6*30*24*60*60) //6Months
#define HIGH_VOL_TIME_CONDITION2     (12*30*24*60*60) //12Months

#define ASUS_REPORT_FULL_IBAT_THRESH    (-400000)// 400mA
#define ASUS_REPORT_FULL_DEBOUNCE_TIME  20// sec

#define BATT_SAFETY_UPGRADE_PARAMS_COUNT 3
struct BATT_SAFETY_UPGRADE_PARAMS{
	int float_voltage_uv;
	int vbat_full_mv;
};

//ASUS_BS battery health upgrade +++
#define BAT_HEALTH_NUMBER_MAX 21
struct BAT_HEALTH_DATA{
	int magic;
	int bat_current;
	unsigned long long bat_current_avg;
	unsigned long long accumulate_time; //second
	unsigned long long accumulate_current; //uA
	int bat_health;
	unsigned long start_time;
	unsigned long end_time;
};

struct BAT_HEALTH_DATA_BACKUP{
    char date[20];
    int health;
};
extern void asus_add_battery_health_fun(void);
//ASUS_BSP battery health upgrade ---

extern struct BATT_SAFETY_UPGRADE_PARAMS batt_safety_upgrade_params[];
extern struct CYCLE_COUNT_DATA g_cycle_count_data;
extern int g_cyclecount_initialized;
extern int asus_qpnp_rtc_read_time(unsigned long * secs);
extern int asus_update_cycle_count(struct fg_chip *chip);
extern void asus_check_full_pending(struct fg_chip *chip, int ibatt_now, int msoc);

int asus_batt_cycle_count_init(void);

int asus_fg_get_record(struct RECORD_DATA *data);
int asus_fg_set_record(struct RECORD_DATA *data);
int asus_set_RCONN_RECORED(int val);
int asus_get_RCONN_RECORED(int *val);
int asus_get_record(int *val,int index);
int asus_set_record(int val,int index);

#endif
