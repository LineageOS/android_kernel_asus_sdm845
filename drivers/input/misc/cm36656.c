/* drivers/input/misc/cm36656.c - cm36656 optical sensors driver
 *
 * Copyright (C) 2016 Vishay Capella Microsystems Limited
 * Author: Frank Hsieh <Frank.Hsieh@vishay.com>
 *                                    
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/lightsensor.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>//asus alex_wang setting  chip power
#include <linux/proc_fs.h>
#include <linux/cm36656.h>
#include <linux/capella_cm3602.h>
#include <asm/setup.h>
#define CONFIG_HAS_WAKELOCK
#include <linux/jiffies.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif

#define D(x...) pr_info(x)

#define I2C_RETRY_COUNT 10
//asus alex wang ALS polling delay 50ms
#define LS_POLLING_DELAY 50
//#define CS_POLLING_DELAY 600

#define REL_RED		REL_X
#define REL_GREEN	REL_Y
#define REL_BLUE	REL_Z
#define REL_IR	        REL_MISC
//asus alex wang ALS sensor porting++++
#define sensitivity_default 125
#define sensitivity_125     125 //cm36656 cs_it =50ms  /0.08 lux/setp
#define sensitivity_250     250 //cm36656 cs_it =100ms  /0.04 lux/setp
#define sensitivity_500     500 //cm36656 cs_it =200ms  /0.02 lux/setp
#define sensitivity_1000    1000 //cm36656 cs_it =400ms  /0.01 lux/setp
//asus alex wang ALS sensor porting----
//asus alex_wang setting  chip power +++++
#define CM36656_VIO_MIN_UV	1650000
#define CM36656_VIO_TYP_UV	1800000
#define CM36656_VIO_MAX_UV	1950000
#define CM36656_VDD_MIN_UV	2500000
#define CM36656_VDD_TYP_UV	3000000
#define CM36656_VDD_MAX_UV	3600000
//asus alex_wang setting  chip power-----

#define NEAR_DELAY_TIME ((100 * HZ) / 1000)

#define CONTROL_INT_ISR_REPORT        0x00
#define CONTROL_ALS                   0x01
#define CONTROL_PS                    0x02
#define CONTROL_ALS_REPORT            0x03
//asus alex_wang rgb porting+++++
#define CONTROL_RGB                   0x04
#define CONTROL_RGB_IT                0x05
//asus alex_wang rgb porting-----
#define ASUS_LIGHTSENSOR_DISABLED_LUX 100000
static void sensor_irq_do_work(struct work_struct *work);
static DECLARE_WORK(sensor_irq_work, sensor_irq_do_work);

struct cm36656_info {
	struct class *cm36656_class;
	struct device *ls_dev;
	struct device *ps_dev;
	struct device *cs_dev;//asus alex_wang porting rgb sensor

	struct input_dev *ls_input_dev;
	struct input_dev *ps_input_dev;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	struct i2c_client *i2c_client;
	struct workqueue_struct *lp_wq;
	struct workqueue_struct *lp_als_wq;
	int intr_pin;
	int als_enable;
	int ps_enable;
	int system_ps_enable;
	int factory_ps_enable;
	int ps_irq_flag;
	int ps_suspend_irq_flag;
	int als_polling_delay;
	int system_ls_enable;
	int camera_ls_enable;

#ifdef CONFIG_PM_SLEEP
	int als_enabled_before_suspend;
        int rgb_enabled_before_suspend;
        int ps_autok_enabled_before_suspend;
#endif

	int irq;
	int (*power)(int, uint8_t); /* power to the chip */

#ifdef CONFIG_HAS_WAKELOCK
	struct wakeup_source wake_src;
#endif
	int psensor_opened;
	int lightsensor_opened;
	uint8_t slave_addr;

	uint16_t ps_close_thd_set;
	uint16_t ps_away_thd_set;
	uint16_t ps_close_thd_set_1cm;
	uint32_t current_lux;
	uint16_t current_adc;
	uint16_t inte_cancel_set;
	uint16_t ps_conf1_val;
	uint16_t ps_conf3_val;
	//asus alex_wang PS calibration++++
	uint16_t ps_crosstalk;
	//asus alex_wang PS calibration-----
	//asus alex_wang psensor autok+++
#ifndef ASUS_FACTORY_BUILD
	struct hrtimer ps_autok_timer;
	uint16_t ps_crosstalk_diff;
	int ps_autok_module_enable;
	int ps_autok_enable;
	int ps_autok_min;
	int ps_autok_max;
#endif
	//asus alex_wang psensor autok---
	//uint16_t ls_cmd;
	//asus alex_wang ALS sensor porting+++++
	uint16_t ls_high_thd_set;
	uint16_t ls_low_thd_set;
	int ls_int_source;
	int last_report_lux;
	//asus alex_wang ALS sensor porting-----
	//asus alex_wang rgb porting+++++
	uint16_t rgb_cmd;
	int rgb_debug;
	int rgb_enable;
	int rgbsensor_opened;
	//asus alex_wang rgb porting-----
	//asus alex_wang setting  chip power+++
	struct regulator *vio;
	struct regulator *vdd;
	//asus alex_wang setting  chip power---
	//rgb timer+++++
#ifndef ASUS_FACTORY_BUILD
	struct hrtimer rgb_setit_timer;
	bool flag_setit_delay;
	int last_r_adc;
	int last_g_adc;
	int last_b_adc;
	int last_ir_adc;
	int last_it;
#endif
        //rgb timer-----
};

/******************************************************************
*  cm36656 function declaration
*
*******************************************************************/
struct cm36656_info *lp_info;
static struct mutex als_control_mutex, als_get_adc_mutex;
static struct mutex ps_control_mutex, ps_get_adc_mutex;
static struct mutex CM36656_control_mutex;
static struct mutex CM36656_i2c_mutex;
static struct mutex CM36656_cctreport_mutex;

static int lightsensor_enable(struct cm36656_info *lpi);
static int lightsensor_disable(struct cm36656_info *lpi);
static int initial_cm36656(struct cm36656_info *lpi);
static void psensor_initial_cmd(struct cm36656_info *lpi);

static int control_and_report(struct cm36656_info *lpi, uint8_t mode, uint16_t param);

//asus alex_wang rgb porting ++++
static struct mutex rgb_control_mutex, rgb_get_adc_mutex;
static int rgbsensor_enable(struct cm36656_info *lpi);
static int rgbsensor_disable(struct cm36656_info *lpi);

//asus alex_wang setting  chip power+++ 
static int cm36656_power_set(struct cm36656_info * info, bool on);
//asus alex_wang setting  chip power---
//asus alex_wang ALS cct workqueue+++
static void cct_value_work_routine(struct work_struct *work);
static DECLARE_DELAYED_WORK(cct_value_work, cct_value_work_routine);
//asus alex_wang ALS cct workqueue----
//asus alex_wang psensor autok+++
#ifndef ASUS_FACTORY_BUILD
static void proximity_autok(struct work_struct *work);
static DECLARE_WORK(proximity_autok_work, proximity_autok);
#define PROXIMITY_AUTOK_POLLING_MS 500
#define PROXIMITY_AUTOK_COUNT 6
#define PROXIMITY_AUTOK_DELAY 10
#endif
//asus alex_wang psensor autok----
#define UNSUPPORT_AUTO_BACKLIGHT

bool focal_psensor_disable_touch = false;
EXPORT_SYMBOL(focal_psensor_disable_touch);

//asus alex_wang ALS porting------
/******************************************************************
*cm36656 variable declaration
*
*******************************************************************/
static uint16_t cm36656_adc_red, cm36656_adc_green, cm36656_adc_blue, cm36656_adc_ir;
static int proximity_state = 1;
static int ps_bypass_near_event = 0;
//asus alex_wang psensor porting+++++
static int cm36656_probe_fail = 0;
//asus alex_wang psensor porting-----
//asus alex wang ALS sensor porting++++++
static int sensitivity=sensitivity_default;
//asus alex wang ALS sensor porting------
//asus alex wang ALS Calibration+++++
bool enLSensorConfig_flag =0 ;   //the flag of lightsensor calibration enable
int LSensor_CALIDATA =1567; //input calibration data . Format : "600 lux -->lux value "
//asus alex wang ALS Calibration------
//asus alex wang PS Calibration+++++
bool enPSensorConfig_flag=0;  //the flag of psensor calibration enable
int PSensor_CALIDATA[4] = {146, 65, 35, 1627}; //input calibration data . Format : "near 3cm:-->value;far 5cm:-->value;crosstalk:-->value"

//asus alex wang PS Calibration+++++
//asus alexwang 20170222 porting factory+++
#ifdef ASUS_FACTORY_BUILD
struct proc_dir_entry *lightsensor_entry = NULL;
struct proc_dir_entry *proximitysensor_entry = NULL;
#endif
//asus alexwang 20170222 porting factory---
//asus alex_wang Debug+++
static int  CM36656DEBUG=0;
#define LOGE(x...)		if(CM36656DEBUG)pr_err(x)
static int g_ls_adc_debug = 0;
//asus alex_wang Debug---

static uint16_t csconf,psconf;
static uint8_t i2crxdata[3];
static int I2C_RxData(uint16_t slaveAddr, uint8_t cmd, uint8_t *rxData, int length)
{
	uint8_t loop_i;
	int val,ret;
	struct cm36656_info *lpi = lp_info;
	struct i2c_msg msgs[] = {
		{
		 .addr = slaveAddr,
		 .flags = 0,
		 .len = 1,
		 .buf = &cmd,
		},
		{
		 .addr = slaveAddr,
		 .flags = I2C_M_RD,
		 .len = length,
		 .buf = rxData,
		},
	};

	for (loop_i = 0; loop_i < I2C_RETRY_COUNT; loop_i++) {
		ret = i2c_transfer(lp_info->i2c_client->adapter, msgs, 2);
		if (ret > 0)
			break;
		else if( -ETIMEDOUT == ret)
			return ret;

		val = gpio_get_value(lpi->intr_pin);
		/*check intr GPIO when i2c error*/
		if (loop_i == 0 || loop_i == I2C_RETRY_COUNT -1)
			D("[PS][CM36656 error] %s, i2c err, slaveAddr 0x%x cmd 0x%x ISR gpio %d  = %d rgbconf=0x%x psconf1=0x%x last csconf=0x%x,psconf=0x%x,rxdata 0x%x 0x%x 0x%x\n",
				__func__, slaveAddr,cmd, lpi->intr_pin, val,lpi->rgb_cmd,lpi->ps_conf1_val,csconf,psconf,i2crxdata[0],i2crxdata[1],i2crxdata[2]);
		msleep(10);
	}

	if (loop_i >= I2C_RETRY_COUNT) {
		printk(KERN_ERR "[PS_ERR][CM36656 error] %s retry over %d\n",
			__func__, I2C_RETRY_COUNT);
		return -EIO;
	}
	i2crxdata[0]=cmd;
	i2crxdata[1]=rxData[0];
	i2crxdata[2]=rxData[1];
	return 0;
}

static uint8_t i2ctxdata[3];
static int I2C_TxData(uint16_t slaveAddr, uint8_t *txData, int length)
{
	uint8_t loop_i;
	int val,ret;
	struct cm36656_info *lpi = lp_info;
	struct i2c_msg msg[] = {
		{
		 .addr = slaveAddr,
		 .flags = 0,
		 .len = length,
		 .buf = txData,
		},
	};

	for (loop_i = 0; loop_i < I2C_RETRY_COUNT; loop_i++) {
		ret =i2c_transfer(lp_info->i2c_client->adapter, msg, 1);
		if (ret > 0)
			break;
		else if( -ETIMEDOUT == ret)
			return ret;

		val = gpio_get_value(lpi->intr_pin);
		/*check intr GPIO when i2c error*/
		if (loop_i == 0 || loop_i == I2C_RETRY_COUNT -1)
			D("[CM36656 error][TX] %s, i2c err, slaveAddr 0x%x, value 0x%x,data[1] 0x%x,data[2] 0x%x ISR gpio%d  = %d rgbconf=0x%x psconf1=0x%x last csconf=0x%x, psconf=0x%x txdata 0x%x 0x%x 0x%x\n",
				__func__, slaveAddr, txData[0],txData[1],txData[2], lpi->intr_pin, val,lpi->rgb_cmd,lpi->ps_conf1_val,csconf,psconf,i2ctxdata[0],i2ctxdata[1],i2ctxdata[2]);

		msleep(10);
	}

	if (loop_i >= I2C_RETRY_COUNT) {
		printk(KERN_ERR "[ALS+PS_ERR][CM36656 error] %s retry over %d\n",
			__func__, I2C_RETRY_COUNT);
		return -EIO;
	}

	if (txData[0] == 0x0)
	{
	csconf = ((uint16_t)txData[2])<<8;
        csconf += (uint16_t)txData[1];
	}

	if (txData[0] == 0x3){
	psconf = ((uint16_t)txData[2])<<8;
        psconf +=(uint16_t)txData[1];
	}
	i2ctxdata[0]=txData[0];
        i2ctxdata[1]=txData[1];
        i2ctxdata[2]=txData[2];
	return 0;
}

static int _cm36656_I2C_Read_Word(uint16_t slaveAddr, uint8_t cmd, uint16_t *pdata)
{
	uint8_t buffer[2];
	int ret = 0;
        mutex_lock(&CM36656_i2c_mutex);
	if (pdata == NULL){
		mutex_unlock(&CM36656_i2c_mutex);
		return -EFAULT;
	}
	ret = I2C_RxData(slaveAddr, cmd, buffer, 2);
	if (ret < 0) {
		pr_err(
			"[ALS+PS_ERR][CM36656 error]%s: I2C_RxData fail [0x%x, 0x%x]\n",
			__func__, slaveAddr, cmd);
		mutex_unlock(&CM36656_i2c_mutex);
		return ret;
	}

	*pdata = (buffer[1]<<8)|buffer[0];
	/* Debug use
	printk(KERN_DEBUG "[CM36656] %s: I2C_RxData[0x%x, 0x%x] = 0x%x\n",
		__func__, slaveAddr, cmd, *pdata);
 	*/
        mutex_unlock(&CM36656_i2c_mutex);
	return ret;
}

static int _cm36656_I2C_Write_Word(uint16_t SlaveAddress, uint8_t cmd, uint16_t data)
{
	char buffer[3];
	int ret = 0;
	mutex_lock(&CM36656_i2c_mutex);
	/* Debug use
	printk(KERN_DEBUG "[CM36656] %s: _cm36656_I2C_Write_Word[0x%x, 0x%x, 0x%x]\n",
		__func__, SlaveAddress, cmd, data);
	*/
	buffer[0] = cmd;
	buffer[1] = (uint8_t)(data&0xff);
	buffer[2] = (uint8_t)((data&0xff00)>>8);
	ret = I2C_TxData(SlaveAddress, buffer, 3);
	if (ret < 0) {
		pr_err("[ALS+PS_ERR][CM36656 error]%s: I2C_TxData fail\n", __func__);
		mutex_unlock(&CM36656_i2c_mutex);
		return -EIO;
	}
	mutex_unlock(&CM36656_i2c_mutex);
	return ret;
}

static int get_ps_adc_value(uint16_t *data)
{
	int ret = 0;
	struct cm36656_info *lpi = lp_info;
	mutex_lock(&ps_get_adc_mutex);
	if (data == NULL){
		mutex_unlock(&ps_get_adc_mutex);
		return -EFAULT;
	}

	ret = _cm36656_I2C_Read_Word(lpi->slave_addr, PS_DATA, data);

	if (ret < 0) {
		pr_err("[PS][CM36656 error]%s: _cm36656_I2C_Read_Word fail\n", __func__);
		mutex_unlock(&ps_get_adc_mutex);
		return -EIO;
	} else {
		//pr_err("[PS][CM36656 OK]%s: _cm36656_I2C_Read_Word OK 0x%04x\n", __func__, *data);
	}
	mutex_unlock(&ps_get_adc_mutex);
	return ret;
}
//asus alex wang 20170526 remove get_stable_ps_adc_value+++
/*
static uint16_t mid_value(uint16_t value[], uint8_t size)
{
	int i = 0, j = 0;
	uint16_t temp = 0;

	if (size < 3)
		return 0;

	for (i = 0; i < (size - 1); i++)
		for (j = (i + 1); j < size; j++)
			if (value[i] > value[j]) {
				temp = value[i];
				value[i] = value[j];
				value[j] = temp;
			}
	return value[((size - 1) / 2)];
}
static int get_stable_ps_adc_value(uint16_t *ps_adc)
{
	uint16_t value[3] = {0, 0, 0}, mid_val = 0;
	int ret = 0;
	int i = 0;
	int wait_count = 0;
	struct cm36656_info *lpi = lp_info;

	for (i = 0; i < 3; i++) {
		//wait interrupt GPIO high
		while (gpio_get_value(lpi->intr_pin) == 0) {
			msleep(10);
			wait_count++;
			if (wait_count > 12) {
				pr_err("[PS_ERR][CM36656 error]%s: interrupt GPIO low,"
					" get_ps_adc_value\n", __func__);
				return -EIO;
			}
		}

		ret = get_ps_adc_value(&value[i]);
		if (ret < 0) {
			pr_err("[PS_ERR][CM36656 error]%s: get_ps_adc_value\n",
				__func__);
			return -EIO;
		}

		if (wait_count < 60/10) {//wait gpio less than 60ms
			msleep(60 - (10*wait_count));
		}
		wait_count = 0;
	}

	//D("Sta_ps: Before sort, value[0, 1, 2] = [0x%x, 0x%x, 0x%x]",
	//	value[0], value[1], value[2]);
	mid_val = mid_value(value, 3);
	D("Sta_ps: After sort, value[0, 1, 2] = [0x%x, 0x%x, 0x%x]",
		value[0], value[1], value[2]);

	return 0;
}
*/
//asus alex wang 20170526 remove get_stable_ps_adc_value---

//asus alex wang psensor porting+++++
static int set_ps_threshold(void)
{
	int ret = 0;
	struct cm36656_info *lpi = lp_info;
        if(lpi->ps_away_thd_set >=  lpi->ps_close_thd_set )
	{
		pr_err("[PS][CM36656]%s error: ps_close_thd_set = 0x%04x, ps_away_thd_set = 0x%04x\n", __func__, lpi->ps_close_thd_set, lpi->ps_away_thd_set);
		return -1;
	}
	ret =  _cm36656_I2C_Write_Word(lpi->slave_addr, PS_THDL,  lpi->ps_away_thd_set);
	if(ret < 0){
		return -EIO;
	}
	ret = _cm36656_I2C_Write_Word(lpi->slave_addr, PS_THDH,  lpi->ps_close_thd_set);
	if(ret < 0){
		return -EIO;
	}
	LOGE("[PS][CM36656]%s: ps_close_thd_set = 0x%04x, ps_away_thd_set = 0x%04x\n", __func__, lpi->ps_close_thd_set, lpi->ps_away_thd_set);
        return ret;
}
//asus alex wang psensor porting+++++
static void sensor_irq_do_work(struct work_struct *work)
{
	struct cm36656_info *lpi = lp_info;
	//asus alex wang ALS porting++++
	uint16_t intFlag;
	_cm36656_I2C_Read_Word(lpi->slave_addr, INT_FLAG, &intFlag);
	control_and_report(lpi, CONTROL_INT_ISR_REPORT, intFlag);
	enable_irq(lpi->irq);
	//asus alex wang ALS porting++++
	lpi->ps_irq_flag =0;
	__pm_relax(&lpi->wake_src);
}

static irqreturn_t cm36656_irq_handler(int irq, void *data)
{
	struct cm36656_info *lpi = data;
	__pm_wakeup_event(&lpi->wake_src, 1 * HZ);
	lpi->ps_irq_flag =1;
	disable_irq_nosync(lpi->irq);
	queue_work(lpi->lp_wq, &sensor_irq_work);
	return IRQ_HANDLED;
}
//asus alex wang ALS sensor porting+++++
static void read_ls_adc_value(void)
{
	struct cm36656_info *lpi = lp_info;
	mutex_lock(&als_get_adc_mutex);
	_cm36656_I2C_Read_Word(lpi->slave_addr, CS_R_DATA, &cm36656_adc_red);
	_cm36656_I2C_Read_Word(lpi->slave_addr, CS_G_DATA, &cm36656_adc_green);
	_cm36656_I2C_Read_Word(lpi->slave_addr, CS_B_DATA, &cm36656_adc_blue);
	_cm36656_I2C_Read_Word(lpi->slave_addr, CS_IR_DATA, &cm36656_adc_ir);
	//asus rgb timer+++
	#ifndef ASUS_FACTORY_BUILD
	lpi->last_r_adc = cm36656_adc_red;
	lpi->last_g_adc = cm36656_adc_green;
	lpi->last_b_adc = cm36656_adc_blue;
	lpi->last_ir_adc = cm36656_adc_ir;
	#endif
	//asus rgb timer----
	LOGE("[LS][CM36656] %s raw data R=%x G=%x B=%x IR=%x\n", __func__, cm36656_adc_red, cm36656_adc_green, cm36656_adc_blue, cm36656_adc_ir);
	mutex_unlock(&als_get_adc_mutex);
}

static int  set_lsensor_threshold(void)
{
	int ret = 0;
	struct cm36656_info  *lpi =lp_info;
	ret = _cm36656_I2C_Write_Word(lpi->slave_addr, CS_THDH, lpi->ls_high_thd_set);
	if(ret < 0){
		return -EIO;
	}
	ret = _cm36656_I2C_Write_Word(lpi->slave_addr, CS_THDL, lpi->ls_low_thd_set);
	if(ret < 0){
		return -EIO;
	}
	return ret;
}

//asus alex wang ALS sensor porting-----
//asus alex wang rgb porting ++++
static void read_cs_adc_value(void)
{
	struct cm36656_info *lpi = lp_info;
	mutex_lock(&als_get_adc_mutex);
	_cm36656_I2C_Read_Word(lpi->slave_addr, CS_R_DATA, &cm36656_adc_red);
	_cm36656_I2C_Read_Word(lpi->slave_addr, CS_G_DATA, &cm36656_adc_green);
	_cm36656_I2C_Read_Word(lpi->slave_addr, CS_B_DATA, &cm36656_adc_blue);
	_cm36656_I2C_Read_Word(lpi->slave_addr, CS_IR_DATA, &cm36656_adc_ir);
	//asus rgb timer+++
	#ifndef ASUS_FACTORY_BUILD
	lpi->last_r_adc = cm36656_adc_red;
	lpi->last_g_adc = cm36656_adc_green;
	lpi->last_b_adc = cm36656_adc_blue;
	lpi->last_ir_adc = cm36656_adc_ir;
	#endif
	//asus rgb timer----
	LOGE("[CS][CM36656] %s %x %x %x %x\n", __func__, cm36656_adc_red, cm36656_adc_green, cm36656_adc_blue, cm36656_adc_ir);
	mutex_unlock(&als_get_adc_mutex);
}

static uint16_t get_ls_intsource_adc(void)
{
	struct cm36656_info *lpi = lp_info;
	if(lpi->ls_int_source == CM36656_ALS_SOURCE_GREEN ){
		    return cm36656_adc_green;
        }else if(lpi->ls_int_source == CM36656_ALS_SOURCE_IR){
		    return cm36656_adc_ir;
        }else{
		 return 0x00;
	}
}

static void set_cs_rgbir_it(uint16_t time,int val)
{
	struct cm36656_info *lpi = lp_info;
#ifndef ASUS_FACTORY_BUILD
	ktime_t setit_delay;
	int delaytime;
	lpi->last_it=lpi->rgb_cmd&0xc; //rgb timer++++
#endif
	if((lpi->rgb_cmd&0xc)!=(time<<2)){
		control_and_report(lpi, CONTROL_RGB_IT,time);
	}
	//rgb timer++++
#ifndef ASUS_FACTORY_BUILD
	delaytime=((time+1)*50*125/100);//delay 1.25*CS_IT
	setit_delay = ns_to_ktime(delaytime * NSEC_PER_MSEC);
	hrtimer_start(&lpi->rgb_setit_timer, setit_delay, HRTIMER_MODE_REL);
	lpi->flag_setit_delay = true;
#endif
	//rgb timer++++
}
//asus alex wang rgb porting ----
//asus rgb timer++++
#ifndef ASUS_FACTORY_BUILD
static enum hrtimer_restart rgb_setit_timer_function(struct hrtimer *timer)
{
	struct  cm36656_info *lpi = lp_info;
	lpi->flag_setit_delay = false;
	D("[CS][CM36656] RGB set IT delay complete\n");
	return HRTIMER_NORESTART;
}
#endif
//asus rgb timer----

//asus alex_wang ALS Calibration+++++
/*******************************************
//     Calibration Formula:
//     y = f(x)
//  -> ax - by = constant_k
//     a is f(x2) - f(x1) , b is x2 - x1
**********************************************/
int calibration_max_lux(int64_t x_big, int64_t x_small)
{
	int report_lux = 0xffffffff;
	if (report_lux == 0xffffffff) { // only calculate it the first time
		int64_t y_big = 1000 * sensitivity / 10; //lux ->adc
		int64_t y_small = 200 * sensitivity / 10;
		int64_t constant_k = (y_big - y_small) * x_small - (x_big - x_small) * y_small;
		if (65535 * (y_big - y_small) < constant_k){
			report_lux = 0;
		}
		else {
			report_lux = (((65535 * (y_big - y_small) - constant_k) * 100 + (x_big - x_small) * sensitivity * 5) /
				((x_big - x_small) * sensitivity * 10));
		}
	}
	return report_lux;
}

int64_t static calibration_light(int64_t x_big, int64_t x_small, int64_t report_lux)
{
	/*int64_t y_big = 1000 * sensitivity / 10; //lux ->adc
	int64_t y_small = 200 * sensitivity/ 10;
	int64_t constant_k;

	if (report_lux <= x_small) {
		y_big = y_small;
		x_big = x_small;
		y_small = 0;
		x_small = 0;
	}
	else  if ((report_lux > x_big) && (calibration_max_lux(x_big, x_small) < 20000)) {
		y_small = y_big;
		x_small = x_big;
		y_big = 20000 * sensitivity / 10;
		x_big = 65535;
	}

	constant_k= (y_big - y_small) * x_small - (x_big - x_small) * y_small;
	if (report_lux * (y_big - y_small) < constant_k){
		return 0;
	}
	else {
		return (((report_lux * (y_big - y_small) - constant_k) * 100 + (x_big - x_small) * sensitivity * 5) /
			((x_big - x_small) * sensitivity * 10));
	}*/
	report_lux = 1000 * report_lux / LSensor_CALIDATA;
	return report_lux;
}
//dark_adc->200lux calibration adc  bright_adc->1000lux calibration adc
void cal_ls_thd_sel(uint16_t adc_value, uint16_t *ls_low_thd, uint16_t *ls_high_thd, uint16_t low_bound, uint16_t high_bound,int dark_adc,int bright_adc)
{
	uint32_t temp_adc;
	int current_lux = 0, temp_lux;

	if (enLSensorConfig_flag == 1) {
		current_lux = calibration_light(bright_adc, dark_adc, (int)adc_value);
	}

	temp_adc = adc_value * low_bound / 100; //low_bound%
	if (enLSensorConfig_flag == 1) {
		if (temp_adc == adc_value) temp_adc--;
		do {
			temp_lux = calibration_light(bright_adc, dark_adc, (int)temp_adc);
			//LOGE("[LS][CM36656][Calibration][%s] temp_lux = %d\n",__func__,temp_lux);
			if (temp_lux < current_lux) {
				temp_adc++;
				break;
			}
			if (temp_adc == 0) {
				break;
			}
			else {
				temp_adc--;
			}			
		}
		while (temp_adc >= 0);
	}
	*ls_low_thd = temp_adc;

	temp_adc = adc_value * high_bound / 100; //high_bound%
	if (enLSensorConfig_flag == 1) {
		if (temp_adc == adc_value) temp_adc++;
		do {
			temp_lux = calibration_light(bright_adc, dark_adc, (int)temp_adc);
			if (temp_lux > current_lux) {
				temp_adc--;
				break;
			}
			temp_adc++;
		}
		while (temp_adc < 65535);
		if(temp_adc > 65535)
		 temp_adc=65535;
	}
	*ls_high_thd = (uint16_t)temp_adc;
}

void cal_ls_thd(uint16_t adc_value, uint16_t *ls_low_thd, uint16_t *ls_high_thd,int dark_adc,int bright_adc)
{
	if (adc_value == 0) {
		*ls_low_thd = 0;
		*ls_high_thd = 0;
	}
	else if (adc_value <= dark_adc) { //200lux -> adc
		cal_ls_thd_sel(adc_value, ls_low_thd, ls_high_thd, 98, 102,dark_adc,bright_adc);
	}
	else if (adc_value <= bright_adc) { //1000lux -> adc
		cal_ls_thd_sel(adc_value, ls_low_thd, ls_high_thd, 95, 105,dark_adc,bright_adc);
	}
	else {
		cal_ls_thd_sel(adc_value, ls_low_thd, ls_high_thd, 90, 110,dark_adc,bright_adc);
	}
}
/*
uint16_t  read_ls_calibration_value(uint16_t report_lux,int dark_adc,int bright_adc)
{
	struct cm36656_info *lpi = lp_info;
	uint16_t ls_low_thd, ls_high_thd;
	cal_ls_thd(report_lux, &ls_low_thd, &ls_high_thd,dark_adc,bright_adc);
	lpi->ls_high_thd_set=ls_high_thd;
	lpi->ls_low_thd_set=ls_low_thd;
	LOGE("[LS][CM36656][%s] before set_lsensor_threshold report_lux is 0x%04x, Hi/Low THD ls_high_thd_set = 0x%04x, ls_low_thd_set = 0x%04x\n",__func__,report_lux,ls_high_thd,ls_low_thd);
	set_lsensor_threshold();
	switch(lpi->rgb_cmd&0xc)
	{
		case CM36656_CS_IT_50MS:
		    sensitivity=sensitivity_125;
		break;
		case CM36656_CS_IT_100MS:
		   sensitivity=sensitivity_250;
		break;
		case CM36656_CS_IT_200MS:
		   sensitivity=sensitivity_500;
		break;
		case CM36656_CS_IT_400MS:
		   sensitivity=sensitivity_1000;
		break;
		default:
		   sensitivity=sensitivity_default;
		break;
	}
	if(enLSensorConfig_flag == 1 ){//calibration enable 
		    if( dark_adc > 0&&bright_adc > 0 &&bright_adc > dark_adc ){ //in case of zero divisor error
                         if (report_lux > 0) {
                           report_lux = calibration_light(bright_adc, dark_adc, report_lux);
			   LOGE("[LS][CM36656][%s] after calibration report_lux is 0x%04x, Hi/Low THD ls_high_thd_set = 0x%04x, ls_low_thd_set = 0x%04x\n",__func__,report_lux,lpi->ls_high_thd_set,lpi->ls_low_thd_set);
			         if (report_lux == 0) report_lux = 1;
                          }else {
			     report_lux = 0;
			  }
                     }else{
                          pr_err("[LS][CM36656]%s:ASUS input LSensor_CALIDATA was invalid .error !!!!!\n",__func__);
                     }
		}else{
		    report_lux = report_lux*10/sensitivity*(200/15);
		    LOGE("[LS][CM36656] not ALS calbiration,report_lux=%d\n ",report_lux);
	        }
	return report_lux;
}
*/
//cct report
uint16_t  read_cct_calibration_value(uint16_t report_lux,int dark_adc,int bright_adc)
{
	struct cm36656_info *lpi = lp_info;
	switch(lpi->rgb_cmd&0xc)
	{
		case CM36656_CS_IT_50MS:
			sensitivity=sensitivity_125;
			break;
		case CM36656_CS_IT_100MS:
			sensitivity=sensitivity_250;
			break;
		case CM36656_CS_IT_200MS:
			sensitivity=sensitivity_500;
			break;
		case CM36656_CS_IT_400MS:
			sensitivity=sensitivity_1000;
			break;
		default:
			sensitivity=sensitivity_default;
		break;
	}
	if(enLSensorConfig_flag == 1 ){//calibration enable
		if( dark_adc > 0&&bright_adc > 0 &&bright_adc > dark_adc ){ //in case of zero divisor error
			if (report_lux > 0) {
				report_lux = calibration_light(bright_adc, dark_adc, report_lux);
				LOGE("[LS][CM36656][%s] after calibration report_lux is 0x%04x, Hi/Low THD ls_high_thd_set = 0x%04x, ls_low_thd_set = 0x%04x\n",__func__,report_lux,lpi->ls_high_thd_set,lpi->ls_low_thd_set);
				if (report_lux == 0) report_lux = 1;
			}else {
				report_lux = 0;
			}
		}else{
			pr_err("[LS][CM36656]%s:ASUS input LSensor_CALIDATA was invalid .error !!!!!\n",__func__);
		}
	}else{
		report_lux = report_lux*10/sensitivity*(200/15);
		LOGE("[LS][CM36656] not ALS calbiration,report_lux=%d\n ",report_lux);
	}
	return report_lux;
}
//cct report

//asus alex_wang ALS Calibration-----

static void report_psensor_input_event(struct cm36656_info *lpi, int value)
{
	input_report_abs(lpi->ps_input_dev, ABS_DISTANCE, value);
	input_sync(lpi->ps_input_dev);
}
static void report_ls_input_event(int report_lux,int report_golden)
{
	struct cm36656_info *lpi = lp_info;
	input_report_abs(lpi->ls_input_dev, ABS_X,report_lux);
	input_report_abs(lpi->ls_input_dev, ABS_Y,report_golden);
	input_report_abs(lpi->ls_input_dev, ABS_Z,cm36656_adc_red);
	input_report_abs(lpi->ls_input_dev, ABS_RX,cm36656_adc_green);
	input_report_abs(lpi->ls_input_dev, ABS_RY,cm36656_adc_blue);
	input_report_abs(lpi->ls_input_dev, ABS_RZ,cm36656_adc_ir);
	input_sync(lpi->ls_input_dev);
	lpi->last_report_lux = report_lux;
}

//cct work queue
static int cct_debug_log_count=0;
static void cct_value_work_routine(struct work_struct *work){
	uint16_t adc_value=0;
	int report_lux=0,report_golden = 0;
        int dark_adc=0,bright_adc=0;
	struct cm36656_info *lpi = lp_info;
	mutex_lock(&CM36656_cctreport_mutex);
	LOGE("[LS][CM36656][cct_wq] start lightsensor wq\n");
        read_ls_adc_value();
	adc_value=get_ls_intsource_adc();
	//asus alex wang als clabration+++++
        dark_adc=LSensor_CALIDATA/3;
        bright_adc=LSensor_CALIDATA*5/3;
	report_lux = read_cct_calibration_value(adc_value,dark_adc,bright_adc);
	report_golden=read_cct_calibration_value(adc_value,400,2000);
	if((g_ls_adc_debug==1)||(cct_debug_log_count==0)) {
		pr_err("[LS][CM36656] %s report lux = %x report_golden = %x R=%x G=%x B=%x IR=%x\n", __func__, report_lux,report_golden,cm36656_adc_red, cm36656_adc_green, cm36656_adc_blue, cm36656_adc_ir);
		cct_debug_log_count=99;
	}else{
		cct_debug_log_count--;
	}
	//asus alex wang als clabration-----
	if (lpi->als_enable == 1) {
		report_ls_input_event(report_lux,report_golden);
		cancel_delayed_work(&cct_value_work);
		queue_delayed_work(lpi->lp_als_wq, &cct_value_work, msecs_to_jiffies(300));
	} else {
		report_ls_input_event(ASUS_LIGHTSENSOR_DISABLED_LUX,ASUS_LIGHTSENSOR_DISABLED_LUX);
	}
	LOGE("[LS][CM36656]%s",__func__);
	mutex_unlock(&CM36656_cctreport_mutex);
}

static int als_power(int enable)
{
	struct cm36656_info *lpi = lp_info;
	if (lpi->power)
		lpi->power(LS_PWR_ON, 1);
	return 0;
}

//asus alex_wang setting  chip power+++
static int cm36656_power_set(struct cm36656_info * info, bool on)
{
	int rc;
	if(on){
		info->vio = regulator_get(&info->i2c_client->dev, "vio");
		if (IS_ERR(info->vio)) {
			rc = PTR_ERR(info->vio);
			dev_err(&info->i2c_client->dev,"Regulator get failed vio rc=%d\n", rc);
			goto err_vio_get;
		}
		if (regulator_count_voltages(info->vio) > 0) {
			rc = regulator_set_voltage(info->vio,CM36656_VIO_TYP_UV, CM36656_VIO_TYP_UV);
			if (rc) {
				dev_err(&info->i2c_client->dev,"Regulator set failed vio rc=%d\n", rc);
				goto err_vio_set_vtg;
			}
		}
		rc = regulator_enable(info->vio);
		if (rc) {
			dev_err(&info->i2c_client->dev,"Regulator vio enable failed rc=%d\n", rc);
			goto err_vio_ena;
		}
		//VDD ->LEDA
		info->vdd = regulator_get(&info->i2c_client->dev, "vdd");
		if (IS_ERR(info->vdd)) {
			rc = PTR_ERR(info->vdd);
			dev_err(&info->i2c_client->dev,"Regulator get failed vdd rc=%d\n", rc);
			goto err_vdd_get;
		}
		if (regulator_count_voltages(info->vdd) > 0) {
			rc = regulator_set_voltage(info->vdd,CM36656_VDD_TYP_UV, CM36656_VDD_TYP_UV);
			if (rc) {
				dev_err(&info->i2c_client->dev,"Regulator set failed vdd rc=%d\n", rc);
				goto err_vdd_set_vtg;
			}
		}
		rc = regulator_enable(info->vdd);
		if (rc) {
			dev_err(&info->i2c_client->dev,"Regulator vdd enable failed rc=%d\n", rc);
			goto err_vdd_ena;
		}
	} else {
		rc = regulator_disable(info->vio);
		if (rc) {
			dev_err(&info->i2c_client->dev,"Regulator vio disable failed rc=%d\n", rc);
			return rc;
		}
		if (regulator_count_voltages(info->vio) > 0)
			regulator_set_voltage(info->vio, 0,CM36656_VIO_TYP_UV);
		regulator_put(info->vio);
		//VDD ->LEDA
		rc = regulator_disable(info->vdd);
		if (rc) {
			dev_err(&info->i2c_client->dev,"Regulator vdd disable failed rc=%d\n", rc);
			return rc;
		}
		if (regulator_count_voltages(info->vdd) > 0)
			regulator_set_voltage(info->vdd, 0,CM36656_VDD_TYP_UV);

		regulator_put(info->vdd);
	}
	dev_err(&info->i2c_client->dev,"CM36656 set power ok");
	return 0;
err_vio_ena:
	if (regulator_count_voltages(info->vio) > 0)
		regulator_set_voltage(info->vio, 0, CM36656_VIO_TYP_UV);
err_vio_set_vtg:
	regulator_put(info->vio);
err_vio_get:
	return rc;
err_vdd_ena:
	if (regulator_count_voltages(info->vdd) > 0)
		regulator_set_voltage(info->vdd, 0, CM36656_VDD_TYP_UV);
err_vdd_set_vtg:
	regulator_put(info->vio);
err_vdd_get:
      return rc;
}
//asus alex_wang setting  chip power---

static void ls_initial_cmd(struct cm36656_info *lpi)
{
	/*must disable l-sensor interrupt befrore IST create*//*disable ALS func*/
	//asus alex_wang ALSsensor porting++++
	lpi->rgb_cmd &= CM36656_ALS_INT_EN_MASK;//disable interrupt
	lpi->rgb_cmd |= CM36656_CS_SD ;//disable ALS and RGB
	//asus alex_wang ALSsensor porting-----
	//asus alex_wang Debug+++
	LOGE("[CM36656 DEBUG]%s: ls initial rgb_cmd = 0x%x \n",__func__,lpi->rgb_cmd);
	//asus alex_wang Debug+++
	_cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
}
//asus alexwang rgb porting++++
static void cs_initial_cmd(struct cm36656_info *lpi)
{
	/*must disable rgb sensor interrupt befrore IST create*//*disable cs func*/
	lpi->rgb_cmd |= CM36656_CS_SD ;
	lpi->rgb_cmd &=CM36656_CS_START_MASK;
	lpi->rgb_cmd |=CM36656_CS_START;
        //asus alex_wang Debug+++
	LOGE("[CM36656 DEBUG]%s: rgb_cmd = 0x%x \n",__func__,lpi->rgb_cmd);
	//asus alex_wang Debug+++
	_cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
}

//asus alexwang rgb porting-----
static void psensor_initial_cmd(struct cm36656_info *lpi)
{
	/*must disable p-sensor interrupt befrore IST create*//*disable PS func*/
	lpi->ps_conf1_val |= CM36656_PS_SD;
	lpi->ps_conf1_val &= CM36656_PS_INT_MASK;
	//asus alex wang psensor porting++++
	lpi->ps_conf1_val &= CM36656_PS_START_MASK;
	//asus alex wang psensor porting----
	_cm36656_I2C_Write_Word(lpi->slave_addr, PS_CONF1, lpi->ps_conf1_val);
	_cm36656_I2C_Write_Word(lpi->slave_addr, PS_CONF3, lpi->ps_conf3_val);
        set_ps_threshold();
}
//asus alex_wang psensor check status+++
bool proximity_check_status(void){
	struct cm36656_info *lpi = lp_info;
	uint16_t ps_conf1_val,ps_adc_value,temp_ps_conf1;
	int ret,status;
	mutex_lock(&ps_control_mutex);
	if(cm36656_probe_fail){
		pr_err("[PS][CM36656]%s,probe error!\n",__func__);
		goto check_status_error;
	}

	ret = _cm36656_I2C_Read_Word(lpi->slave_addr, PS_CONF1, &temp_ps_conf1);
	if(ret<0)
		goto check_status_error;

	ps_conf1_val=temp_ps_conf1;
	if(temp_ps_conf1&CM36656_PS_SD){
		D("[PS][CM36656]%s,psensor need start\n",__func__);
		ps_conf1_val &= CM36656_PS_SD_MASK;
		ps_conf1_val |= CM36656_PS_START;
		ret=_cm36656_I2C_Write_Word(lpi->slave_addr, PS_CONF1, ps_conf1_val);
		if(ret<0)
			goto check_status_error;
		msleep(10);
	}
	ret = get_ps_adc_value(&ps_adc_value);
	if(!ret){
		if(ps_adc_value > lpi->ps_close_thd_set){
			D("[PS][CM36656] proximity_check_status  NEAR\n");
			status = 1;
		}else{
			D("[PS][CM36656] proximity_check_status FAR\n");
			status = 0;
		}
	}else{
		goto check_status_error;
	}

	if (temp_ps_conf1 & CM36656_PS_SD) {
		ps_conf1_val = ps_conf1_val | CM36656_PS_SD; //disable = 1
		ps_conf1_val&= CM36656_PS_START_MASK;
		ret = _cm36656_I2C_Write_Word(lpi->slave_addr, PS_CONF1, ps_conf1_val);
		if (ret < 0)
			goto check_status_error;
	}

	mutex_unlock(&ps_control_mutex);
	return status;

check_status_error:
	mutex_unlock(&ps_control_mutex);
	return -1;
}

EXPORT_SYMBOL(proximity_check_status);
//asus alex_wang psensor check status ---

static int psensor_enable(struct cm36656_info *lpi)
{
	int ret = -EIO;
	mutex_lock(&ps_control_mutex);
	D("[PS][CM36656] %s\n", __func__);

	if ( lpi->ps_enable ) {
		D("[PS][CM36656] %s: already enabled\n", __func__);
		ret = 0;
	} else {
		ret = control_and_report(lpi, CONTROL_PS, 1);
		//asus alex wang psensor porting+++++++
		if (ret!=0){
			D("[PS][CM36656] psensor_enable--fail!!!\n");
		}else{
			D("[PS][[CM36656] psensor_enable--success!!!\n");
		}
		//asus alex wang psensor porting-------
		enable_irq_wake(lpi->irq);
	}
	mutex_unlock(&ps_control_mutex);
	LOGE("[PS][CM36656] %s\n", __func__);
	return ret;
}

static int psensor_disable(struct cm36656_info *lpi)
{
	int ret = -EIO;

	mutex_lock(&ps_control_mutex);
	D("[PS][CM36656] %s\n", __func__);

	if ( lpi->ps_enable == 0 ) {
		D("[PS][CM36656] %s: already disabled\n", __func__);
		ret = 0;
	} else {
		ret = control_and_report(lpi, CONTROL_PS,0);

		disable_irq_wake(lpi->irq);
	}
	mutex_unlock(&ps_control_mutex);//For next time event be guaranteed to be sent!
	//asus alex wang psensor porting+++++++
	input_report_abs(lpi->ps_input_dev, ABS_DISTANCE, 4);
	input_sync(lpi->ps_input_dev);
	//asus alex wang psensor porting-------
	ps_bypass_near_event = 0;
	focal_psensor_disable_touch = false;
	LOGE("[PS][CM36656] %s\n", __func__);
	return ret;
}

static int psensor_open(struct inode *inode, struct file *file)
{
	struct cm36656_info *lpi = lp_info;

	//if (lpi->psensor_opened)
	//	return -EBUSY;

	lpi->psensor_opened = 1;

	return 0;
}

static int psensor_release(struct inode *inode, struct file *file)
{
	struct cm36656_info *lpi = lp_info;

	lpi->psensor_opened = 0;
	//asus alex wang psensor porting++++ why disable the code only return 0
	LOGE("[PS][CM36656] %s\n", __func__);
	//return psensor_disable(lpi);
	return 0;
	//asus alex wang psensor porting---- why disable the code only return 0
}

static long psensor_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	int val;
	//asua alex_wang psensor porting+++
	int rc;
	char enPcalibration_flag=0;//asus alex wang calibration
	void __user *argp = (void __user *)arg;
	//asua alex_wang psensor porting----
	struct cm36656_info *lpi = lp_info;

	D("[PS][CM36656] %s cmd %d\n", __func__, _IOC_NR(cmd));

	switch (cmd) {
	case CAPELLA_CM3602_IOCTL_ENABLE:
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
                lpi->system_ps_enable = val ?  1 : 0;
		if (lpi->system_ps_enable || lpi->factory_ps_enable)
			return psensor_enable(lpi);
		else
			return psensor_disable(lpi);
		break;
	case CAPELLA_CM3602_IOCTL_ENABLE_FAC:
		if (get_user(val, (unsigned long __user *)arg))
			return -EFAULT;
                lpi->factory_ps_enable = val ?  1 : 0;
		if (lpi->system_ps_enable || lpi->factory_ps_enable)
			return psensor_enable(lpi);
		else
			return psensor_disable(lpi);
		break;
	case CAPELLA_CM3602_IOCTL_GET_ENABLED:
		return put_user(lpi->ps_enable, (unsigned long __user *)arg);
		break;
	case ASUS_PSENSOR_IOCTL_GETDATA:
	{
		uint16_t ps_adc_value = 0;
		int ret=0;
		rc = 0 ;
		LOGE("[PS][CM36656]%s:ASUS ASUS_PSENSOR_IOCTL_GETDATA \n", __func__);
		ret = get_ps_adc_value(&ps_adc_value);
		if (ret < 0) {
			printk("[PS][CM36656]%s:ASUS failed to get_ps_adc_value. \n",__func__);
			rc = -EIO;
			goto pend;
		}
		if ( copy_to_user(argp, &ps_adc_value, sizeof(ps_adc_value) ) ) {
			printk("[PS][CM36656]%s:ASUS failed to copy psense data to user space.\n",__func__);
			rc = -EFAULT;
			goto pend;
		}
		printk("[PS][CM36656]%s:ASUS_PSENSOR_IOCTL_GETDATA end ps_adc_value=%d \n", __func__,ps_adc_value);
	}
		break;
	case ASUS_PSENSOR_SETCALI_DATA:
		LOGE("[PS][CM36656]%s,ASUS_PSENSOR_SETCALI_DATA\n",__func__);
		//asus alex wang PS Calibration++++
		rc = 0 ;
		memset(PSensor_CALIDATA, 0, 4*sizeof(int)); //<asus-wx20160804>
		if (copy_from_user(PSensor_CALIDATA, argp, sizeof(PSensor_CALIDATA))){
			rc = -EFAULT;
			goto pend;
		}
		pr_err("[PS][CM36656]%s:ASUS_PSENSOR SETCALI_DATA : PSensor_CALIDATA[0] : %d, PSensor_CALIDATA[1]: %d, PSensor_CALIDATA[2]: %d, PSensor_CALIDATA[3]: %d\n",
			__func__, PSensor_CALIDATA[0], PSensor_CALIDATA[1], PSensor_CALIDATA[2], PSensor_CALIDATA[3]); //<asus-wx20160804>
		if(PSensor_CALIDATA[3] <= 0||PSensor_CALIDATA[0] <= 0  ||PSensor_CALIDATA[3] <= PSensor_CALIDATA[0] )
			rc =  -EINVAL;
		lpi->ps_crosstalk = PSensor_CALIDATA[2];
		lpi->ps_away_thd_set = PSensor_CALIDATA[1];
		lpi->ps_close_thd_set = PSensor_CALIDATA[0];
		lpi->ps_close_thd_set_1cm = PSensor_CALIDATA[3];
#ifndef ASUS_FACTORY_BUILD
		//asus20170420 alex wang psensor autok++++
		lpi->ps_autok_module_enable = true;
		lpi->ps_crosstalk_diff = 0;
		//asus20170517 alex wang for psensor CSC autok+++++
		if(lpi->ps_away_thd_set == 10 || lpi->ps_close_thd_set == 999){
			PSensor_CALIDATA[1] = lpi->ps_crosstalk+30;
			PSensor_CALIDATA[0] = lpi->ps_crosstalk+100;
			lpi->ps_away_thd_set = PSensor_CALIDATA[1];
			lpi->ps_close_thd_set = PSensor_CALIDATA[0];
		}
		//asus20170517 alex wang for psensor CSC autok-----
		lpi->ps_autok_min = 3; //decided by EE
		lpi->ps_autok_max = 150; //decided by EE
		//asus20170420 alex wang psensor autok++++
#endif
		LOGE("[PS][CM36656]%s:enPSensorConfig_flag is 1, lpi->ps_crosstalk =0x%x,ps_close_thd_set = 0x%x, ps_away_thd_set = 0x%x\n",__func__,lpi->ps_crosstalk,lpi->ps_close_thd_set, lpi->ps_away_thd_set);
		set_ps_threshold();
		//asus alex wang PS Calibration----
		break;
	case  ASUS_PSENSOR_EN_CALIBRATION:
		//asus alex wang PS Calibration++++
		LOGE("[PS][CM36656]%s:ASUS ASUS_PSENSOR_EN_CALIBRATION \n", __func__);
		rc = 0 ;
		if (copy_from_user(&enPcalibration_flag , argp, sizeof(enPcalibration_flag ))){
			rc = -EFAULT;
			goto pend;
		}
		enPSensorConfig_flag =  enPcalibration_flag ;
		//asus alex wang PS Calibration----
		pr_err("[PS][CM36656]%s: ASUS_PSENSOR_EN_CALIBRATION : enPSensorConfig_flag is : %d  \n",__func__,enPSensorConfig_flag); 
		break;
	case  ASUS_PSENSOR_BYPASS_NEAR_EVENT:
		LOGE("[PS][CM36656]%s:ASUS ASUS_PSENSOR_BYPASS_NEAR_EVENT \n", __func__);
		rc = 0 ;
		if (copy_from_user(&ps_bypass_near_event , argp, sizeof(ps_bypass_near_event))){
			rc = -EFAULT;
			goto pend;
		}
		if(ps_bypass_near_event) {
			focal_psensor_disable_touch = false;
		}
		pr_err("[PS][CM36656]%s: ASUS_PSENSOR_BYPASS_NEAR_EVENT : ASUS_PSENSOR_BYPASS_NEAR_EVENT is : %d  \n",__func__,ps_bypass_near_event);
		break;
	default:
		pr_err("[PS][CM36656 error]%s: invalid cmd %d\n",
			__func__, _IOC_NR(cmd));
		return -EINVAL;
	}
pend:
	return rc;
}

static const struct file_operations psensor_fops = {
	.owner = THIS_MODULE,
	.open = psensor_open,
	.release = psensor_release,
	.unlocked_ioctl = psensor_ioctl
};

struct miscdevice psensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "proximitySensor",
	.fops = &psensor_fops
};

static int lightsensor_enable(struct cm36656_info *lpi)
{
	int ret = -EIO;
	mutex_lock(&als_control_mutex);
	D("[LS][CM36656] %s\n", __func__);

	if (lpi->als_enable) {
		D("[LS][CM36656] %s: already enabled\n", __func__);
		ret = 0;
	} else {
		ret = control_and_report(lpi, CONTROL_ALS, 1);
		mutex_lock(&CM36656_cctreport_mutex);
		cancel_delayed_work(&cct_value_work);
		cct_debug_log_count = 0;
		queue_delayed_work(lpi->lp_als_wq, &cct_value_work, lpi->als_polling_delay*125/100);
		mutex_unlock(&CM36656_cctreport_mutex);
	}
	mutex_unlock(&als_control_mutex);
	return ret;
}

static int lightsensor_disable(struct cm36656_info *lpi)
{
	int ret = -EIO;
	mutex_lock(&als_control_mutex);
	D("[LS][CM36656] %s\n", __func__);

	if ( lpi->als_enable == 0 ) {
		D("[LS][CM36656] %s: already disabled\n", __func__);
		ret = 0;
	} else{
		ret = control_and_report(lpi, CONTROL_ALS, 0);
		mutex_lock(&CM36656_cctreport_mutex);
		cancel_delayed_work(&cct_value_work);
		report_ls_input_event(ASUS_LIGHTSENSOR_DISABLED_LUX,ASUS_LIGHTSENSOR_DISABLED_LUX);
		mutex_unlock(&CM36656_cctreport_mutex);
	}
	mutex_unlock(&als_control_mutex);
	return ret;
}

static int lightsensor_open(struct inode *inode, struct file *file)
{
	//struct cm36656_info *lpi = lp_info;
	int rc = 0;
	//asus alexwang debug+++++
	LOGE("[LS][CM36656] %s\n", __func__);
	//asus alexwang debug-----
	///if (lpi->lightsensor_opened) {
	//	pr_err("[LS][CM36656 error]%s: already opened\n", __func__);
	//	rc = -EBUSY;
	//}
	//lpi->lightsensor_opened = 1;
	return rc;
}

static int lightsensor_release(struct inode *inode, struct file *file)
{
	//struct cm36656_info *lpi = lp_info;
	//lpi->lightsensor_opened = 0;
	LOGE("[LS][CM36656] %s\n", __func__);
	return 0;
}

static long lightsensor_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int rc, val;
	struct cm36656_info *lpi = lp_info;
	char encalibration_flag = 0 ;//asus alex wang ALS Calibration
	//asus alex_wang ALS sensor porting++++
	void __user *argp = (void __user *)arg;
	//asus alex_wang ALS sensor porting-----
	LOGE("[LS][CM36656] %s cmd: 0x%x\n", __func__, _IOC_NR(cmd));
	switch (cmd) {
	case LIGHTSENSOR_IOCTL_ENABLE:
		if (get_user(val, (unsigned long __user *)arg)) {
			rc = -EFAULT;
			break;
		}
		D("[LS][CM36656] %s LIGHTSENSOR_IOCTL_ENABLE, value = %d\n",
			__func__, val);
		LOGE("[LS][CM36656] %s LIGHTSENSOR_IOCTL_ENABLE, value = %d\n",
			__func__, val);
		lpi->system_ls_enable = val ?  1 : 0;
		rc = (lpi->system_ls_enable || lpi->camera_ls_enable) ? lightsensor_enable(lpi) : lightsensor_disable(lpi);
		break;
	case CAMERA_LIGHTSENSOR_IOCTL_ENABLE:// hal enable cmd
		if (get_user(val, (unsigned long __user *)arg)) {
			rc = -EFAULT;
			break;
		}
		D("[LS][CM36656] %s CAMERA_LIGHTSENSOR_SENSOR_IOCTL_ENABLE, value = %d\n",
			__func__, val);
		LOGE("[LS][CM36656] %s CAMERA_LIGHTSENSOR_SENSOR_IOCTL_ENABLE, value = %d\n",
			__func__, val);
		lpi->camera_ls_enable = val ?  1 : 0;
		rc = (lpi->system_ls_enable || lpi->camera_ls_enable) ? lightsensor_enable(lpi) : lightsensor_disable(lpi);
		break;
	case CAMERA_LIGHTSENSOR_IOCTL_GET_ENABLED:
	case LIGHTSENSOR_IOCTL_GET_ENABLED:
		val = lpi->als_enable;
		D("[LS][CM36656] %s LIGHTSENSOR_IOCTL_GET_ENABLED, enabled %d\n",
			__func__, val);
		rc = put_user(val, (unsigned long __user *)arg);
		break;
	//asus alex wang ALS sensor porting+++++
	case ASUS_LIGHTSENSOR_IOCTL_GETLUX:
	{
		uint16_t report_lux;
		rc = 0 ;
		//msleep(lpi->als_polling_delay*10*125/100);
		LOGE("[LS][CM36656]%s:ASUS ASUS_LIGHTSENSOR_IOCTL_GETDATA \n", __func__);
		read_ls_adc_value();
		report_lux = get_ls_intsource_adc();
                report_lux = calibration_light(0, 0, report_lux);
		if ( copy_to_user(argp, &report_lux, sizeof(report_lux) ) ) {
			D("[LS][CM36656]%s:ASUS failed to copy lightsense data to user space.\n",__func__);
			rc = -EFAULT;
			goto end;
		}
	}
                break;
	case ASUS_LIGHTSENSOR_IOCTL_GETDATA:
	{
		uint16_t report_lux;
		rc = 0 ;
		msleep(lpi->als_polling_delay*10*125/100);
		LOGE("[LS][CM36656]%s:ASUS ASUS_LIGHTSENSOR_IOCTL_GETDATA \n", __func__);
		read_ls_adc_value();
		report_lux = get_ls_intsource_adc();
		if ( copy_to_user(argp, &report_lux, sizeof(report_lux) ) ) {
			D("[LS][CM36656]%s:ASUS failed to copy lightsense data to user space.\n",__func__);
			rc = -EFAULT;
			goto end;
		}
		//pr_err("[LS][CM36656]%s:ASUS_LIGHTSENSOR_IOCTL_GETDATA end,report_lux=%d,R=%d,G=%d,B=%d,IR=%d\n", __func__,report_lux[0],report_lux[1],report_lux[2],report_lux[3],report_lux[4]);
	}
		break;
	case ASUS_LIGHTSENSOR_SETCALI_DATA:
		LOGE("[LS][CM36656]%s:ASUS ASUS_LIGHTSENSOR_SETCALI_DATA \n", __func__);
		//asus alex wang ALS Calibration+++++
		rc = 0 ;
		//memset(LSensor_CALIDATA, 0, 2*sizeof(int));
		if (copy_from_user(&LSensor_CALIDATA, argp, sizeof(LSensor_CALIDATA)))
		{
			rc = -EFAULT;
			goto end;
		}
		pr_err("[LS][CM36656]%s:ASUS_LIGHTSENSOR SETCALI_DATA : LSensor_CALIDATA :  %d  \n",
			__func__, LSensor_CALIDATA);
		if(LSensor_CALIDATA<0 )
			rc =  -EINVAL;
		//asus alex wang ALS Calibration------
		break;
	case ASUS_LIGHTSENSOR_EN_CALIBRATION:
		LOGE("[LS][CM36656]%s:ASUS ASUS_LIGHTSENSOR_EN_CALIBRATION \n", __func__);
		//asus alex wang ALS Calibration+++++
		rc = 0 ;
		if (copy_from_user(&encalibration_flag , argp, sizeof(encalibration_flag )))
		{
			rc = -EFAULT;
			goto end;
		}
		enLSensorConfig_flag =  encalibration_flag ;
		pr_err("[LS][CM36656]%s: ASUS_LIGHTSENSOR_EN_CALIBRATION : enLSensorConfig_flag is : %d  \n",__func__,enLSensorConfig_flag); 
		//asus alex wang ALS Calibration------
		break;
		//asus alex wang ALS sensor porting------
	default:
		pr_err("[LS][CM36656 error]%s: invalid cmd %d\n",
		__func__, _IOC_NR(cmd));
		rc = -EINVAL;
	}

//asus alex wang ALS sensor porting+++++
end:
//asus alex wang ALS sensor porting------
	return rc;
}

static const struct file_operations lightsensor_fops = {
	.owner = THIS_MODULE,
	.open = lightsensor_open,
	.release = lightsensor_release,
	.unlocked_ioctl = lightsensor_ioctl,
	.compat_ioctl = lightsensor_ioctl
};

static struct miscdevice lightsensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "lightsensor",
	.fops = &lightsensor_fops
};
/****************************************************
*  RGB sensor kernel porting
*  author: alex wang
*******************************************************/

//alex_wang porting RGB sensor+++++
static int rgbsensor_enable(struct cm36656_info *lpi)
{
	int ret = -EIO;
	mutex_lock(&rgb_control_mutex);
	LOGE("[CS][CM36656] %s\n", __func__);
	if (lpi->rgb_enable) {
		D("[CS][CM36656] %s: already enabled\n", __func__);
		ret = 0;
	} else

	ret = control_and_report(lpi, CONTROL_RGB, 1);
	mutex_unlock(&rgb_control_mutex);
	return ret;
}

static int rgbsensor_disable(struct cm36656_info *lpi)
{
	int ret = -EIO;
	mutex_lock(&rgb_control_mutex);
	D("[CS][CM36656] %s\n", __func__);

	if ( lpi->rgb_enable == 0 ) {
		D("[CS][CM36656] %s: already disabled\n", __func__);
		ret = 0;
	} else
		ret = control_and_report(lpi, CONTROL_RGB, 0);

	//asus rgb timer++++
#ifndef ASUS_FACTORY_BUILD
	if (lpi->flag_setit_delay) {
		hrtimer_cancel(&lpi->rgb_setit_timer);
		lpi->flag_setit_delay =false;
	}
#endif
	mutex_unlock(&rgb_control_mutex);
	//asus rgb timer----
	return ret;
}

static int rgbsensor_open(struct inode *inode, struct file *file)
{
	struct cm36656_info *lpi = lp_info;
	int rc = 0;

	if (lpi->rgbsensor_opened) {
		pr_err("[CS][CM36656 error]%s: already opened\n", __func__);
		rc = -EBUSY;
	}
	lpi->rgbsensor_opened = 1;
	return rc;
}

static int rgbsensor_release(struct inode *inode, struct file *file)
{
	struct cm36656_info *lpi = lp_info;

	lpi->rgbsensor_opened = 0;
	return 0;
}

static long rgbsensor_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int rc, val;
	uint16_t time;
	struct cm36656_info *lpi = lp_info;
	int rgb_data[5] = {0};
	void __user *argp = (void __user *)arg;
	char modelName[ASUS_RGB_SENSOR_NAME_SIZE];
	LOGE("[CM36656] %s cmd %d\n", __func__, _IOC_NR(cmd));
	switch (cmd) {
	case ASUS_RGB_SENSOR_IOCTL_ENABLE:// hal enable cmd
		if (get_user(val, (unsigned long __user *)arg)) {
			rc = -EFAULT;
			break;
		}
		D("[CS][CM36656] %s ASUS_RGB_SENSOR_IOCTL_ENABLE, value = %d\n",
			__func__, val);

		LOGE("[CS][CM36656][DEBUG] %s ASUS_RGB_SENSOR_IOCTL_ENABLE, value = %d\n",
			__func__, val);
		rc = val ? rgbsensor_enable(lpi) : rgbsensor_disable(lpi);
		break;
	case ASUS_RGB_SENSOR_IOCTL_GET_ENABLED:
		val = lpi->rgb_enable;
		D("[CS][CM36656] %s ASUS_RGB1_SENSOR_IOCTL_GET_ENABLED, enabled %d\n",
			__func__, val);
		rc = put_user(val, (unsigned long __user *)arg);
		break;
	case ASUS_RGB_SENSOR_IOCTL_IT_SET://setting rgbir_it
		if (get_user(val, (unsigned long __user *)arg)) {
			rc = -EFAULT;
			break;
		}
		if((val>= 0)&&(val<=50))
			time=0;
		else if ((val>50)&&(val<=100))
			time=1;
		else if ((val>100)&&(val<=200))
			time=2;
		else if ((val>200)&&(val<=400))
			time=3;
		else if(val > 400)
			time=3;
		else
			time=-1;
		set_cs_rgbir_it(time,val);
		D("%s:ASUS_RGB_SENSOR_IOCTL_IT_SET end val=%d\n", __func__,val);
		rc=0;
		break;
	case ASUS_RGB_SENSOR_IOCTL_DEBUG_MODE:
		val=lpi->rgb_debug;
		if ( copy_to_user(argp, &val, sizeof(val) ) ) {
			pr_err("%s:ASUS failed to copy RBG debug mode to user space.\n",__func__);
			rc = -EFAULT;
			break;
		}
		rc = 0;
		D("%s:ASUS_RGB_SENSOR_IOCTL_DEBUG_MODE end val=%d\n", __func__,val);
		break;
	case ASUS_RGB_SENSOR_IOCTL_MODULE_NAME:
		/*switch(asus_project_id)
		{
#ifdef ZD552KL_PHOENIX
		case ASUS_ZD552KL_PHOENIX://ASUS_ZD552KL_PHOENIX
			strcpy(modelName,"ASUS_ZD552KL_PHOENIX");
			break;
		case ASUS_ZE553KL://ASUS_ZE553KL
			strcpy(modelName,"ASUS_ZE553KL");
			break;
#endif
		default:
			strcpy(modelName,"ZS550KL");
			break;
		}*/
		strcpy(modelName,"ZS620KL");
		if ( copy_to_user(argp, &modelName, sizeof(modelName) ) ) {
			pr_err("%s:ASUS failed to copy model name  to user space.\n",__func__);
			rc = -EFAULT;
			break;
		}
		rc = 0;
		break;
	case ASUS_RGB_SENSOR_IOCTL_DATA_READ:
		//asus rgbtimer++++
#ifndef ASUS_FACTORY_BUILD
		if (lpi->flag_setit_delay) {
			int index=0;
			int now_it=0;
			cm36656_adc_red = lpi->last_r_adc;
			cm36656_adc_green = lpi->last_g_adc;
			cm36656_adc_blue = lpi->last_b_adc;
			cm36656_adc_ir = lpi->last_ir_adc;
			now_it = lpi->rgb_cmd&0xc;
			D("[CS][CM36656] last_r_adc = %d,last_g_adc = %d,last_b_adc = %d,last_ir_adc = %d,last_it = %d,now_it = %d\n",
				lpi->last_r_adc,lpi->last_g_adc,lpi->last_b_adc,lpi->last_ir_adc,lpi->last_it,now_it);
			if (now_it > lpi->last_it) {
				for (index = lpi->last_it;index < now_it;index++) {
					cm36656_adc_red *= 2;
					cm36656_adc_green *= 2;
					cm36656_adc_blue *= 2;
					cm36656_adc_ir *= 2;
				}
				if (cm36656_adc_red > 0xffff) cm36656_adc_red = 0xffff;
				if (cm36656_adc_green > 0xffff) cm36656_adc_green = 0xffff;
				if (cm36656_adc_blue > 0xffff) cm36656_adc_blue = 0xffff;
				if (cm36656_adc_ir > 0xffff) cm36656_adc_ir = 0xffff;
			}
			else if (now_it < lpi->last_it) {
				for (index = lpi->last_it;index > now_it;index--) {
					cm36656_adc_red /= 2;
					cm36656_adc_green /= 2;
					cm36656_adc_blue /= 2;
					cm36656_adc_ir /= 2;
				}
			}
		}else{
#endif
			//asus rgbtimer----
#ifdef ASUS_FACTORY_BUILD
			msleep(lpi->als_polling_delay*10*125/100);
#endif
			read_cs_adc_value();
#ifndef ASUS_FACTORY_BUILD
		}
#endif
		LOGE("[CS][CM36656] %s cm36656_adc_red=0x%x cm36656_adc_green= 0x%x cm36656_adc_blue=0x%x cm36656_adc_ir=0x%x\n", __func__, cm36656_adc_red, cm36656_adc_green, cm36656_adc_blue, cm36656_adc_ir);
		rgb_data[0]= cm36656_adc_red;
		rgb_data[1]= cm36656_adc_green;
		rgb_data[2]= cm36656_adc_blue;
		rgb_data[3]= cm36656_adc_ir;
		rgb_data[4]= 0;
		if ( copy_to_user(argp, &rgb_data, sizeof(rgb_data) ) ) {
			pr_err("%s:ASUS failed to copy RBG data to user space.\n",__func__);
			rc = -EFAULT;
			break;
		}

		rc = 0;
		break;
	default:
		pr_err("[CS][CM36656 error]%s: invalid cmd %d\n",
			__func__, _IOC_NR(cmd));
		rc = -EINVAL;
	}
	return rc;
}

static const struct file_operations rgbsensor_fops = {
	.owner = THIS_MODULE,
	.open = rgbsensor_open,
	.release = rgbsensor_release,
	.unlocked_ioctl = rgbsensor_ioctl,
	.compat_ioctl = rgbsensor_ioctl,
};

static struct miscdevice rgbsensor_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "asusRgbSensor",
	.fops = &rgbsensor_fops
};
//alex_wang porting RGB sensor------


static ssize_t ps_adc_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	uint16_t value;
	int ret;
	struct cm36656_info *lpi = lp_info;
	int intr_val;
	// uint16_t ps_data=0;
	intr_val = gpio_get_value(lpi->intr_pin);

	get_ps_adc_value(&value);
	// get_stable_ps_adc_value(&ps_data);
	ret = sprintf(buf, "adc = %d, ENABLE = %d,intr_pin = %d\n", value, lpi->ps_enable,intr_val);
	return ret;
}
//asus alexwang psensor porting++++
static ssize_t ps_adc_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	//nop
	return count;
}
//asus alexwang psensor porting----

static ssize_t ps_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm36656_info *lpi = lp_info;
	ret = sprintf(buf, "Proximity sensor Enable = %d\n", lpi->ps_enable);
	return ret;
}

static ssize_t ps_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ps_en;
	struct cm36656_info *lpi = lp_info;

	ps_en = -1;
	sscanf(buf, "%d", &ps_en);

	if (ps_en != 0 && ps_en != 1)
		return -EINVAL;
	
	D("[PS][CM36656] %s: ps_en=%d\n", __func__, ps_en);
	
	if (ps_en)
		psensor_enable(lpi);
	else
		psensor_disable(lpi);

	return count;
}

static ssize_t ps_conf_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct cm36656_info *lpi = lp_info;
	return sprintf(buf, "PS_CONF1 = 0x%04x, PS_CONF3 = 0x%04x\n", lpi->ps_conf1_val, lpi->ps_conf3_val);
}
static ssize_t ps_conf_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int code1, code2;
	struct cm36656_info *lpi = lp_info;

	sscanf(buf, "0x%x 0x%x", &code1, &code2);

	D("[PS]%s: store value PS conf1 reg = 0x%04x PS conf3 reg = 0x%04x\n", __func__, code1, code2);

	lpi->ps_conf1_val = code1;
	lpi->ps_conf3_val = code2;

	_cm36656_I2C_Write_Word(lpi->slave_addr, PS_CONF3, lpi->ps_conf3_val );  
	_cm36656_I2C_Write_Word(lpi->slave_addr, PS_CONF1, lpi->ps_conf1_val );

	return count;
}

static ssize_t ps_thd_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	struct cm36656_info *lpi = lp_info;
	ret = sprintf(buf, "[PS][CM36656]PS Hi/Low THD ps_close_thd_set = 0x%04x(%d), ps_away_thd_set = 0x%04x(%d)\n", lpi->ps_close_thd_set, lpi->ps_close_thd_set, lpi->ps_away_thd_set, lpi->ps_away_thd_set);
	return ret;	
}
static ssize_t ps_thd_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int code1, code2;
	int ps_en;
	struct cm36656_info *lpi = lp_info;

	sscanf(buf, "0x%x 0x%x", &code1, &code2);

	lpi->ps_close_thd_set = code1;
	lpi->ps_away_thd_set = code2;
	ps_en = lpi->ps_enable;

	if(ps_en)
		psensor_disable(lpi);
	//asus alex wang porting psensor++++
	set_ps_threshold();
	/*
	_cm36656_I2C_Write_Word(lpi->slave_addr, PS_THDH, lpi->ps_close_thd_set );
	_cm36656_I2C_Write_Word(lpi->slave_addr, PS_THDL, lpi->ps_away_thd_set );
       */
	//asus alex wang porting psensor-----
	if(ps_en)
		psensor_enable(lpi);

	D("[PS][CM36656]%s: ps_close_thd_set = 0x%04x(%d), ps_away_thd_set = 0x%04x(%d)\n", __func__, lpi->ps_close_thd_set, lpi->ps_close_thd_set, lpi->ps_away_thd_set, lpi->ps_away_thd_set);

	return count;
}

static ssize_t ps_canc_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm36656_info *lpi = lp_info;

	ret = sprintf(buf, "[PS][CM36656]PS_CANC = 0x%04x(%d)\n", lpi->inte_cancel_set,lpi->inte_cancel_set);

	return ret;
}
static ssize_t ps_canc_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int code;
	int ps_en;
	struct cm36656_info *lpi = lp_info;

	sscanf(buf, "0x%x", &code);

	D("[PS][CM36656]PS_CANC: store value = 0x%04x(%d)\n", code,code);
	
	lpi->inte_cancel_set = code;	
	ps_en = lpi->ps_enable;

	if(ps_en)
		psensor_disable(lpi);

	_cm36656_I2C_Write_Word(lpi->slave_addr, PS_CANC, lpi->inte_cancel_set );

	if(ps_en)
		psensor_enable(lpi);
	
	return count;
}

static ssize_t ls_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{

	int ret = 0;
	struct cm36656_info *lpi = lp_info;

	ret = sprintf(buf, "Light sensor Enable = %d\n",
			lpi->als_enable);

	return ret;
}

static ssize_t ls_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret = 0;
	int ls_auto;
	struct cm36656_info *lpi = lp_info;

	ls_auto = -1;
	sscanf(buf, "%d", &ls_auto);

	if (ls_auto != 0 && ls_auto != 1)
		return -EINVAL;

	if (ls_auto) {
		ret = lightsensor_enable(lpi);
	} else {
		ret = lightsensor_disable(lpi);
	}

	D("[LS][CM36656] %s: lpi->als_enable = %d, ls_auto=%d\n",
		__func__, lpi->als_enable, ls_auto);

	if (ret < 0)
		pr_err("[LS][CM36656 error]%s: set auto light sensor fail\n",
			__func__);

	return count;
}

static ssize_t ls_conf_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct cm36656_info *lpi = lp_info;
	return sprintf(buf, "CS_CONF = %x\n", lpi->rgb_cmd);
}
static ssize_t ls_conf_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct cm36656_info *lpi = lp_info;
	int value = 0;
	sscanf(buf, "0x%x", &value);

	lpi->rgb_cmd = value;
	printk(KERN_INFO "[LS]set CS_CONF = %x\n", lpi->rgb_cmd);
	
	_cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
	return count;
}
//asus rgbratio++++
static  char CSRratio[256];

static ssize_t ls_rgbratio_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "CSratio =%s\n", CSRratio);
}
static ssize_t ls_rgbratio_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	sscanf(buf, "%s", CSRratio);
	return count;
}
//asus rgbratio----

//asus alex wang ALS sensor porting++++
/*
static ssize_t ls_red_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x\n", cm36656_adc_red);
}


static ssize_t ls_green_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x\n", cm36656_adc_green);
}

static ssize_t ls_blue_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x\n", cm36656_adc_blue);
}

static ssize_t ls_ir_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x\n", cm36656_adc_ir);
}
*/
static ssize_t ls_pers_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct cm36656_info *lpi = lp_info;
	uint16_t value;
	int result;
	_cm36656_I2C_Read_Word(lpi->slave_addr, CS_CONF, &value);
	switch(value&0x0c00){
	case 0x0:
		result=1;
		break;
	case 0x400:
		result=2;
		break;
	case 0x800:
		result=4;
		break;
	case 0xc00:
		result=8;
		break;
	default:
		result=-1;
		break;
	}
	return sprintf(buf, "cs_pers = %d\n", result);
}

static ssize_t ls_pers_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int value = 0;
	uint16_t temp_conf;
	struct cm36656_info *lpi = lp_info;
	_cm36656_I2C_Read_Word(lpi->slave_addr, CS_CONF, &temp_conf);
	sscanf(buf, "%d", &value);
	pr_err("[LS]value=%d\n",value);
	switch(value){
	case 1:
		temp_conf&=0xf3ff;
		break;
	case 2:
		temp_conf&=0xf3ff;
		temp_conf|=CM36656_ALS_PERS_2;
		break;
	case 4:
		temp_conf&=0xf3ff;
		temp_conf|=CM36656_ALS_PERS_4;
		break;
	case 8:
		temp_conf&=0xf3ff;
		temp_conf|=CM36656_ALS_PERS_8;
		break;
	default:
		temp_conf=0x0;
		break;
	}
	if(temp_conf){
		lpi->rgb_cmd=temp_conf;
		_cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
	}
	return count;
}

static ssize_t ls_adc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	uint16_t als_step;
	struct cm36656_info *lpi = lp_info;
	lightsensor_enable(lpi);
	read_ls_adc_value();
	als_step=get_ls_intsource_adc();
	return sprintf(buf, "ADC lux=0x%x,R=0x%x,G=0x%x,B=0x%x,IR=0x%x\n", als_step,cm36656_adc_red,cm36656_adc_green,cm36656_adc_blue,cm36656_adc_ir);
}

static ssize_t ls_adc_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	//nop
	return count;
}

static ssize_t ls_int_source_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct cm36656_info *lpi = lp_info;
	lightsensor_enable(lpi); 
	D("[LS][CM36283] %s: ls_int_source = 0x%x !!!\n",	__func__, lp_info->ls_int_source);
	return sprintf(buf, "ls_int_source=%d\n", lp_info->ls_int_source);
}
static ssize_t ls_int_source_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	//nop
	return count;
}


static ssize_t ls_thd_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	struct cm36656_info *lpi = lp_info;
	ret = sprintf(buf, "[LS][CM36656] Hi/Low THD ls_high_thd_set = 0x%04x(%d), ls_low_thd_set = 0x%04x(%d)\n",
		lpi->ls_high_thd_set, lpi->ls_high_thd_set, lpi->ls_low_thd_set, lpi->ls_low_thd_set);
	return ret;
}
static ssize_t ls_thd_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int code1, code2;
	int ls_en;
	struct cm36656_info *lpi = lp_info;

	sscanf(buf, "0x%x 0x%x", &code1, &code2);

	lpi->ls_high_thd_set = code1;
	lpi->ls_low_thd_set = code2;
	ls_en = lpi->als_enable;

	if(ls_en)
		lightsensor_disable(lpi);
	set_lsensor_threshold();
	//bsp grace
	if(ls_en)
		lightsensor_enable(lpi);

	D("[LS][CM36656]%s: ls_high_thd_set = 0x%04x(%d), ls_low_thd_set = 0x%04x(%d)\n", __func__, lpi->ps_close_thd_set, lpi->ps_close_thd_set, lpi->ps_away_thd_set, lpi->ps_away_thd_set);
	return count;
}

static ssize_t ls_debug_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "CM36656DEBUG=%d\n", CM36656DEBUG);
}
static ssize_t ls_debug_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int value;
	value = -1;
	sscanf(buf, "%d", &value);

	if (value != 0 && value != 1)
		return -EINVAL;

        CM36656DEBUG=value;
	LOGE("[CM36656][DEBUG]CM36656DEBUG=%d\n",CM36656DEBUG);
	return count;
}

static ssize_t ls_adc_debug_show(struct device *dev,
                              struct device_attribute *attr,
                              char *buf)
{
	return sprintf(buf, "g_ls_adc_debug = %d\n", g_ls_adc_debug);
}

static ssize_t ls_adc_debug_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf, size_t count)
{
	int value=0;

	sscanf(buf, "%d", &value);
	if (value != 0) value = 1;

	g_ls_adc_debug = value;
	pr_err("[LS][CM36656][DEBUG] set g_ls_adc_debug = %d\n", g_ls_adc_debug);

	return count;
}

//asus alex wang ALS sensor porting----
//asus alex_wang rgb porting+++++
static ssize_t cs_rgbir_it_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{

	int ret = 0;
	struct cm36656_info *lpi = lp_info;
	switch(lpi->rgb_cmd&0xc)
	{
		case CM36656_CS_IT_50MS:
			ret = sprintf(buf, "RGB sensor CS_IT=50MS\n");
			break;
		case CM36656_CS_IT_100MS:
			ret = sprintf(buf, "RGB sensor CS_IT=100MS\n");
			break;
		case CM36656_CS_IT_200MS:
			ret = sprintf(buf, "RGB sensor CS_IT=200MS\n");
			break;
		case CM36656_CS_IT_400MS:
			ret = sprintf(buf, "RGB sensor CS_IT=400MS\n");
			break;
		default:
			ret = sprintf(buf, "RGB sensor CS_IT error\n");
			break;
	}
	return ret;
}

static ssize_t cs_rgbir_it_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	uint16_t time;
	int val;
	sscanf(buf, "%d", &val);
	if((val>= 0)&&(val<=50))
		time=0;
	else if ((val>50)&&(val<=100))
		time=1;
	else if ((val>100)&&(val<=200))
		time=2;
	else if ((val>200)&&(val<=400))
		time=3;
	else if(val > 400)
		time=4;
	else
		time=-1;
	set_cs_rgbir_it(time,val);
	return count;
}
static ssize_t cs_rgb_debug_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm36656_info *lpi = lp_info;
	ret = sprintf(buf, "RGB sensor debug enable = %d\n",lpi->rgb_debug);
	return ret;
}

static ssize_t cs_rgb_debug_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int cs_auto = -1;
	struct cm36656_info *lpi = lp_info;
	sscanf(buf, "%d", &cs_auto);

	if (cs_auto != 0 && cs_auto != 1)
		return -EINVAL;
	lpi->rgb_debug = cs_auto;
	return count;
}
static ssize_t cs_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct cm36656_info *lpi = lp_info;
	ret = sprintf(buf, "RGB sensor Enable = %d\n",
			lpi->rgb_enable);
	return ret;
}

static ssize_t cs_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret = 0;
	int val;
	struct cm36656_info *lpi = lp_info;

	val = -1;
	sscanf(buf, "%d", &val);

	if (val != 0 && val != 1)
		return -EINVAL;

	if (val) {
		ret = rgbsensor_enable(lpi);
	} else {
		ret = rgbsensor_disable(lpi);
	}

	D("[CS][CM36656] %s: lpi->rgb_enable =0x%x, val=%d\n",
		__func__, lpi->rgb_enable, val);

	if (ret < 0)
		pr_err("[cS][CM36656 error]%s: set RGB sensor enable fail\n",
			__func__);

	return count;
}

static ssize_t cs_conf_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct cm36656_info *lpi = lp_info;
	return sprintf(buf, "CS_CONF = 0x%x\n", lpi->rgb_cmd);
}

static ssize_t cs_conf_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct cm36656_info *lpi = lp_info;
	int value = 0;
	sscanf(buf, "0x%x", &value);

	lpi->rgb_cmd = value;
	printk(KERN_INFO "[CS]set CS_CONF = %x\n", lpi->rgb_cmd);

	_cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
	return count;
}

static ssize_t cs_red_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x\n", cm36656_adc_red);
}

static ssize_t cs_red_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
       //nop
	return count;
}


static ssize_t cs_green_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x\n", cm36656_adc_green);
}

static ssize_t cs_green_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
       //nop
	return count;
}

static ssize_t cs_blue_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x\n", cm36656_adc_blue);
}

static ssize_t cs_blue_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
       //nop
	return count;
}

static ssize_t cs_ir_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x\n", cm36656_adc_ir);
}
static ssize_t cs_ir_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
       //nop
	return count;
}

//<asus alex_wang 20170307> add factory +++++
#ifdef ASUS_FACTORY_BUILD
static ssize_t cs_status_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	uint16_t idReg;//asus alex wang cm36656 who am i
	int ret; //asus alex wang cm36656 who am i
	int value;
	struct cm36656_info *lpi = lp_info;
	ret =_cm36656_I2C_Read_Word(lpi->slave_addr, ID_REG, &idReg);
	if(ret<0){
		value=0;
	}else{
		value=1;
	}
	return sprintf(buf, "%d\n", value);
}
static ssize_t cs_status_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
       //nop
	return count;
}
static struct device_attribute dev_attr_cs_status =
__ATTR(cs_status, 0664, cs_status_show, cs_status_store);
#endif
//<asus alex_wang 20170307> add factory ----

static struct device_attribute dev_attr_cs_rgbir_it =
__ATTR(cs_rgbir_it, 0664, cs_rgbir_it_show, cs_rgbir_it_store);

static struct device_attribute dev_attr_cs_rgb_debug =
__ATTR(cs_rgb_debug, 0664, cs_rgb_debug_show, cs_rgb_debug_store);

static struct device_attribute dev_attr_cs_enable =
__ATTR(cs_enable, 0664, cs_enable_show, cs_enable_store);

static struct device_attribute dev_attr_cs_conf =
__ATTR(cs_conf, 0664, cs_conf_show, cs_conf_store);

static struct device_attribute dev_attr_color_red =
__ATTR(cs_intensity_red, 0664, cs_red_show, cs_red_store);

static struct device_attribute dev_attr_color_green =
__ATTR(cs_intensity_green, 0664, cs_green_show, cs_green_store);

static struct device_attribute dev_attr_color_blue =
__ATTR(cs_intensity_blue, 0664, cs_blue_show, cs_blue_store);

static struct device_attribute dev_attr_color_ir =
__ATTR(cs_intensity_ir, 0664, cs_ir_show, cs_ir_store);

static struct attribute *rgb_sysfs_attrs[] = {
#ifdef ASUS_FACTORY_BUILD
       &dev_attr_cs_status.attr,
#endif
	&dev_attr_cs_enable.attr,
	&dev_attr_cs_conf.attr,
	&dev_attr_color_red.attr,
	&dev_attr_color_green.attr,
	&dev_attr_color_blue.attr,
	&dev_attr_color_ir.attr,
	&dev_attr_cs_rgb_debug.attr,
	&dev_attr_cs_rgbir_it.attr,  
	NULL
};

static struct attribute_group rgb_attribute_group = {
	.attrs = rgb_sysfs_attrs,
};
//asus alex_wang rgb porting-----

static struct device_attribute dev_attr_ps_adc =
__ATTR(ps_adc, 0444, ps_adc_show, ps_adc_store);//asus alex wang psensor porting

static struct device_attribute dev_attr_ps_enable =
__ATTR(ps_enable, 0664, ps_enable_show, ps_enable_store);	

static struct device_attribute dev_attr_ps_conf =	
__ATTR(ps_conf, 0664, ps_conf_show, ps_conf_store);

static struct device_attribute dev_attr_ps_thd =	
__ATTR(ps_thd, 0664, ps_thd_show, ps_thd_store);

static struct device_attribute dev_attr_ps_canc = 	
__ATTR(ps_canc, 0664, ps_canc_show, ps_canc_store);

static struct attribute *proximity_sysfs_attrs[] = {
	&dev_attr_ps_adc.attr,
	&dev_attr_ps_enable.attr,
	&dev_attr_ps_conf.attr,
	&dev_attr_ps_thd.attr,
	&dev_attr_ps_canc.attr,
	NULL
};

static struct attribute_group proximity_attribute_group = {
	.attrs = proximity_sysfs_attrs,
};

static struct device_attribute dev_attr_ls_enable =
__ATTR(ls_enable, 0664, ls_enable_show, ls_enable_store);

static struct device_attribute dev_attr_ls_conf =
__ATTR(ls_conf, 0664, ls_conf_show, ls_conf_store);

//asus alex wang ALS sensor porting+++++
/*
static struct device_attribute dev_attr_light_red =
__ATTR(in_intensity_red, 0664, ls_red_show, NULL);

static struct device_attribute dev_attr_light_green =
__ATTR(in_intensity_green, 0664, ls_green_show, NULL);

static struct device_attribute dev_attr_light_blue =
__ATTR(in_intensity_blue, 0664, ls_blue_show, NULL);

static struct device_attribute dev_attr_light_ir =
__ATTR(in_intensity_ir, 0664, ls_ir_show, NULL);
*/

static struct device_attribute dev_attr_light_pers =
	__ATTR(ls_pers, 0664,ls_pers_show, ls_pers_store);
static struct device_attribute dev_attr_light_adc =
__ATTR(ls_adc, 0664, ls_adc_show, ls_adc_store);
static struct device_attribute dev_attr_light_intsource =
__ATTR(ls_int_source, 0664, ls_int_source_show, ls_int_source_store);
static struct device_attribute dev_attr_light_thd =
__ATTR(ls_thd, 0664, ls_thd_show, ls_thd_store);
static struct device_attribute dev_attr_light_debug =
	__ATTR(ls_debug, 0664, ls_debug_show, ls_debug_store);
static struct device_attribute dev_attr_light_adc_debug =
	__ATTR(ls_adc_debug, 0664, ls_adc_debug_show, ls_adc_debug_store);
static struct device_attribute dev_attr_light_rgbratio =
	__ATTR(ls_rgbratio, 0664, ls_rgbratio_show, ls_rgbratio_store);

//asus alex wang ALS sensor porting-----
static struct attribute *light_sysfs_attrs[] = {
	&dev_attr_ls_enable.attr,
	&dev_attr_ls_conf.attr,
	&dev_attr_light_adc.attr,
	&dev_attr_light_intsource.attr,
	&dev_attr_light_thd.attr,
	&dev_attr_light_pers.attr,
	&dev_attr_light_debug.attr,
	&dev_attr_light_adc_debug.attr,
	&dev_attr_light_rgbratio.attr,
	/*
	&dev_attr_light_red.attr,
	&dev_attr_light_green.attr,
	&dev_attr_light_blue.attr,
	&dev_attr_light_ir.attr,  
	*/
	NULL
};

static struct attribute_group light_attribute_group = {
	.attrs = light_sysfs_attrs,
};

static int lightsensor_setup(struct cm36656_info *lpi)
{
	int ret;

	lpi->ls_input_dev = input_allocate_device();
	if (!lpi->ls_input_dev) {
		pr_err("[LS][CM36656 error]%s: could not allocate ls input device\n", __func__);
		return -ENOMEM;
	}
	lpi->ls_input_dev->name = "lightsensor";
	//asus alex_wang porting ALS sensor++++
	/*
	input_set_capability(lpi->ls_input_dev, EV_REL, REL_RED);
	input_set_capability(lpi->ls_input_dev, EV_REL, REL_GREEN);
	input_set_capability(lpi->ls_input_dev, EV_REL, REL_BLUE);
	input_set_capability(lpi->ls_input_dev, EV_REL, REL_IR);
	*/
	set_bit(EV_ABS, lpi->ls_input_dev->evbit);
	input_set_abs_params(lpi->ls_input_dev, ABS_X, 0, 9, 0, 0);
	input_set_abs_params(lpi->ls_input_dev, ABS_Y, 0, 9, 0, 0);
	input_set_abs_params(lpi->ls_input_dev, ABS_Z, 0, 9, 0, 0);
	input_set_abs_params(lpi->ls_input_dev, ABS_RX, 0, 9, 0, 0);
	input_set_abs_params(lpi->ls_input_dev, ABS_RY, 0, 9, 0, 0);
	input_set_abs_params(lpi->ls_input_dev, ABS_RZ, 0, 9, 0, 0);
	//asus alex_wang porting ALS sensor-----
	ret = input_register_device(lpi->ls_input_dev);
	if (ret < 0) {
		pr_err("[LS][CM36656 error]%s: can not register ls input device\n",
				__func__);
		goto err_free_ls_input_device;
	}

	ret = misc_register(&lightsensor_misc);
	if (ret < 0) {
		pr_err("[LS][CM36656 error]%s: can not register ls misc device\n",
				__func__);
		goto err_unregister_ls_input_device;
	}
	return ret;

err_unregister_ls_input_device:
	input_unregister_device(lpi->ls_input_dev);
	return ret;

err_free_ls_input_device:
	input_free_device(lpi->ls_input_dev);
	return ret;
}

static int psensor_setup(struct cm36656_info *lpi)
{
	int ret;

	lpi->ps_input_dev = input_allocate_device();
	if (!lpi->ps_input_dev) {
		pr_err(
			"[PS][CM36656 error]%s: could not allocate ps input device\n",
			__func__);
		 return -ENOMEM;
	}

	lpi->ps_input_dev->name = "proximity";
	set_bit(EV_ABS, lpi->ps_input_dev->evbit);
	input_set_abs_params(lpi->ps_input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	ret = input_register_device(lpi->ps_input_dev);
	if (ret < 0) {
		pr_err(
			"[PS][CM36656 error]%s: could not register ps input device\n",
			__func__);
		goto err_free_ps_input_device;
	}

	ret = misc_register(&psensor_misc);
	if (ret < 0) {
		pr_err(
			"[PS][CM36656 error]%s: could not register ps misc device\n",
			__func__);
		goto err_unregister_ps_input_device;
	}

	return ret;

err_unregister_ps_input_device:
	input_unregister_device(lpi->ps_input_dev);
	return ret;

err_free_ps_input_device:
	input_free_device(lpi->ps_input_dev);
	return ret;
}
//asus alex_wang porting rgb sensor+++++
static int rgbsensor_setup(struct cm36656_info *lpi)
{
	int ret;
	ret = misc_register(&rgbsensor_misc);
	if (ret < 0) {
		pr_err("[CS][CM36656 error]%s: can not register cs misc device\n",
				__func__);
	}
	return ret;
}
//asus alex_wang porting rgb sensor+++++

static int initial_cm36656(struct cm36656_info *lpi)
{
	int val;
	uint16_t idReg;//asus alex wang cm36656 who am i
	int ret; //asus alex wang cm36656 who am i
	val = gpio_get_value(lpi->intr_pin);
	D("[PS][CM36656] %s, INTERRUPT GPIO val = %d\n", __func__, val);
	//asus alex wang cm36656 who am i+++++
	ret =_cm36656_I2C_Read_Word(lpi->slave_addr, ID_REG, &idReg);
	idReg = idReg&0x00FF;
	LOGE("CM36656: idReg : %d \n", idReg);
	if(ret < 0 || idReg != 0x0057){
		pr_err("[CM36656]%s:inital error ret = %d,idreg=%d\n",__func__,ret,idReg);
		return -ENOMEM;
	}
	D("[PS][CM36656]%s: init ok\n", __func__);
	//asus alex wang cm36656 who am i-----
	return 0;
}

static int cm36656_setup(struct cm36656_info *lpi)
{
	int ret = 0;

	als_power(1);
	msleep(5);
	ret = gpio_request(lpi->intr_pin, "gpio_cm36656_intr");
	if (ret < 0) {
		pr_err("[PS][CM36656 error]%s: gpio %d request failed (%d)\n",
			__func__, lpi->intr_pin, ret);
		return ret;
	}

	ret = gpio_direction_input(lpi->intr_pin);
	if (ret < 0) {
		pr_err(
			"[PS][CM36656 error]%s: fail to set gpio %d as input (%d)\n",
			__func__, lpi->intr_pin, ret);
		goto fail_free_intr_pin;
	}


	ret = initial_cm36656(lpi);
	if (ret < 0) {
		pr_err(
			"[PS_ERR][CM36656 error]%s: fail to initial cm36656 (%d)\n",
			__func__, ret);
		goto fail_free_intr_pin;
	}

	/*Default disable P sensor and L sensor*/
	ls_initial_cmd(lpi);
	psensor_initial_cmd(lpi);
	//asus alex_wang rgb porting+++
        cs_initial_cmd(lpi);
	//asus alex_wang rgb porting----
	ret = request_any_context_irq(lpi->irq,
			cm36656_irq_handler,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT,
			"cm36656",
			lpi);
	if (ret < 0) {
		pr_err(
			"[PS][CM36656 error]%s: req_irq(%d) fail for gpio %d (%d)\n",
			__func__, lpi->irq,
			lpi->intr_pin, ret);
		goto fail_free_intr_pin;
	}

	return ret;

fail_free_intr_pin:
	gpio_free(lpi->intr_pin);
	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cm36656_early_suspend(struct early_suspend *h)
{
	struct cm36656_info *lpi = lp_info;

	D("[LS][CM36656] %s\n", __func__);

	if (lpi->als_enable)
		lightsensor_disable(lpi);
	
	if (lpi->ps_enable)
		psensor_disable(lpi);
	//asus alex_wang porting rgbsensor++++
        if (lpi->rgb_enable)
		rgbsensor_disable(lpi);
	//asus alex_wang porting rgbsensor-----
}

static void cm36656_late_resume(struct early_suspend *h)
{
	struct cm36656_info *lpi = lp_info;

	D("[LS][CM36656] %s\n", __func__);

	if (!lpi->als_enable)
		lightsensor_enable(lpi);
	if (!lpi->ps_enable)
		psensor_enable(lpi);
	//asus alex_wang porting rgbsensor++++
	if (lpi->rgb_enable)
		rgbsensor_enable(lpi);
	//asus alex_wang porting rgbsensor-----
}
#endif

#ifdef CONFIG_OF
static int cm36656_parse_dt(struct device *dev,
				struct cm36656_info *lpi)
{
	struct device_node *np = dev->of_node;
	u32 temp_val;
	int rc;
	D("[LS][CM36656] %s\n", __func__);

	rc = of_get_named_gpio_flags(np, "capella,intrpin-gpios", 0, NULL);
	if (rc < 0)
	{
		dev_err(dev, "Unable to read interrupt pin number\n");
		return rc;
	}
	else
	{
		lpi->intr_pin = rc;
		D("[LS][CM36656]%s GET INTR PIN \n", __func__);
	}

	rc = of_property_read_u32(np, "capella,slave_address", &temp_val);
	if (rc)
	{
		dev_err(dev, "Unable to read slave_address\n");
		return rc;
	}
	else
	{
		lpi->slave_addr = (uint8_t)temp_val;
	}

	D("[PS][CM36656]%s PARSE OK \n", __func__);

	return 0;
}
#endif

//<asus alex_wang 20170222> add factory +++++
#ifdef ASUS_FACTORY_BUILD
static int lightsensor_proc_show(struct seq_file *m, void *v)
{
        uint16_t idReg;//asus alex wang cm36656 who am i
	int ret; //asus alex wang cm36656 who am i
	struct cm36656_info *lpi = lp_info;
	ret =_cm36656_I2C_Read_Word(lpi->slave_addr, ID_REG, &idReg);
	if(ret<0){
		seq_printf(m,"0 \n");
	}else{
		seq_printf(m,"1 \n");
	}
	return ret;
}

static int Proximitysensor_proc_show(struct seq_file *m, void *v) {
	uint16_t idReg;//asus alex wang cm36656 who am i
	int ret; //asus alex wang cm36656 who am i
	struct cm36656_info *lpi = lp_info;
	ret =_cm36656_I2C_Read_Word(lpi->slave_addr, ID_REG, &idReg);
	if(ret<0){
		seq_printf(m,"0 \n");
	}else{
		seq_printf(m,"1 \n");
	}
	return ret;
}

static int lightsensor_proc_open(struct inode *inode, struct  file *file) {
	return single_open(file, lightsensor_proc_show, NULL);
}

static const struct file_operations lightsensor_proc_fops = {
	.owner = THIS_MODULE,
	.open = lightsensor_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int Proximitysensor_proc_open(struct inode *inode, struct  file *file) {
	return single_open(file, Proximitysensor_proc_show, NULL);
}

static const struct file_operations Proximitysensor_proc_fops = {
	.owner = THIS_MODULE,
	.open = Proximitysensor_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

int create_asusproc_lightsensor_status_entry(void){
	lightsensor_entry = proc_create("lightsensor_status", S_IWUGO| S_IRUGO, NULL,&lightsensor_proc_fops);
	if (!lightsensor_entry)
		return -ENOMEM;
    	return 0;
}

int create_asusproc_Proximitysensor_status_entry(void){
	proximitysensor_entry = proc_create("Proximitysensor_status", S_IWUGO| S_IRUGO, NULL,&Proximitysensor_proc_fops);
	if (!proximitysensor_entry)
       		 return -ENOMEM;
    	return 0;
}


#endif
//<asus alex_wang 20170222> add factory -----
//<asus alex_wang 20170222>psensor autok+++++
#ifndef ASUS_FACTORY_BUILD
static int psensor_check_minCT(void)
{
	struct cm36656_info *lpi = lp_info;
	uint16_t adc_value = 0;
	int ret;
	int crosstalk_min = 4096;
	int crosstalk_diff;
	int i;
	D("[PS][CM36656] %s,lpi->ps_autok_min=%d, lpi->ps_autok_max=%d lpi->ps_away_thd_set =%d lpi->ps_close_thd_set=%d\n", __func__,lpi->ps_autok_min,lpi->ps_autok_max,lpi->ps_away_thd_set,lpi->ps_close_thd_set);
	/*update the min crosstalk value*/
	for (i = 0;i < PROXIMITY_AUTOK_COUNT;i++) {
		ret = get_ps_adc_value(&adc_value);
		if (ret < 0) {
			pr_err("[PS][CM36283]psensor_check_minCT : get_ps_adc_value FAIL\n");
			return ret;
		}

		if (adc_value < crosstalk_min) {
			crosstalk_min = adc_value;
		}

		if (i != PROXIMITY_AUTOK_COUNT - 1) {
			mdelay(PROXIMITY_AUTOK_DELAY);
		}
	}

	/*update the diff crosstalk value*/
	crosstalk_diff = crosstalk_min - lpi->ps_crosstalk;
	pr_err("[PS][CM36656]psensor_check_minCT : ps_crosstalk = %d, crosstalk_min = %d\n", lpi->ps_crosstalk, crosstalk_min);

	if ((crosstalk_diff > lpi->ps_autok_min) && (crosstalk_diff < lpi->ps_autok_max)) {
		lpi->ps_crosstalk_diff = crosstalk_diff;
		pr_err("[PS][CM36656]psensor_check_minCT : update ps_crosstalk_diff = %d\n", lpi->ps_crosstalk_diff);

		lpi->ps_away_thd_set = PSensor_CALIDATA[1] + lpi->ps_crosstalk_diff;
		lpi->ps_close_thd_set = PSensor_CALIDATA[0] + lpi->ps_crosstalk_diff;
		set_ps_threshold();
	}
	else {
		lpi->ps_away_thd_set = PSensor_CALIDATA[1];
		lpi->ps_close_thd_set = PSensor_CALIDATA[0];
		set_ps_threshold();

		if (crosstalk_diff <= lpi->ps_autok_min) {
			pr_err("[PS][CM36656]psensor_check_minCT : crosstalk_diff = %d, too small\n", crosstalk_diff);
			return -1;
		}
		else {
			lpi->ps_crosstalk_diff = crosstalk_diff;
			pr_err("[PS][CM36656]psensor_check_minCT : crosstalk_diff = %d, too big\n", crosstalk_diff);
		}
	}

	return 0;
}

static enum hrtimer_restart proximity_timer_function(struct hrtimer *timer)
{
	struct cm36656_info *lpi = lp_info;
	ktime_t autok_delay;

	queue_work(lpi->lp_wq, &proximity_autok_work);

	if (0 == lpi->ps_crosstalk_diff) {
		lpi->ps_autok_enable=false;
		return HRTIMER_NORESTART;
	}

	else {
		autok_delay = ns_to_ktime(PROXIMITY_AUTOK_POLLING_MS * NSEC_PER_MSEC);
		hrtimer_forward_now(&lpi->ps_autok_timer, autok_delay);
	}

	return HRTIMER_RESTART;
}

static void proximity_autok(struct work_struct *work)
{
	struct cm36656_info *lpi = lp_info;
	uint16_t adc_value = 0;
	int ret;
	int crosstalk_diff;

	ret = get_ps_adc_value(&adc_value);
	if (ret < 0) {
		pr_err("[PS][CM36656]proximity_autok : get_ps_adc_value FAIL\n");
		lpi->ps_crosstalk_diff = 0;
		return;
	}
	//D("[PS][CM36656]proximity_autok : adc_value = %d\n", adc_value);

	crosstalk_diff = adc_value - lpi->ps_crosstalk;
	if ((crosstalk_diff < lpi->ps_crosstalk_diff) && (lpi->ps_crosstalk_diff != 0)) {
		/*last diff of crosstalk does not set to HW, should reset the value to 0.*/
		if (lpi->ps_crosstalk_diff >= lpi->ps_autok_max) {
			lpi->ps_crosstalk_diff = 0;
		}

		if (crosstalk_diff <= lpi->ps_autok_min) {
			lpi->ps_away_thd_set = PSensor_CALIDATA[1];
			lpi->ps_close_thd_set = PSensor_CALIDATA[0];
			set_ps_threshold();

			lpi->ps_crosstalk_diff = 0;
			pr_err("[PS][CM36656]%s,adc_value=%d,ps_crosstalk=%d,crosstalk_diff=%d\n",__func__,adc_value,lpi->ps_crosstalk,crosstalk_diff);
			pr_err("[PS][CM36656]PSensor_CALIDATA[0]=%d,PSensor_CALIDATA[1]=%d,PSensor_CALIDATA[2]=%d,PSensor_CALIDATA[3]=%d\n",PSensor_CALIDATA[0],PSensor_CALIDATA[1],PSensor_CALIDATA[2],PSensor_CALIDATA[3]);
			pr_err("[PS][CM36656]proximity_autok : crosstalk is normal, stop poll psensor autok\n");
		}
		else if ((crosstalk_diff > lpi->ps_autok_min) && (crosstalk_diff < lpi->ps_autok_max)) {
			lpi->ps_crosstalk_diff = crosstalk_diff;
			pr_err("[PS][CM36656]proximity_autok : update ps_crosstalk_diff = %d\n", lpi->ps_crosstalk_diff);

			lpi->ps_away_thd_set = PSensor_CALIDATA[1] + lpi->ps_crosstalk_diff;
			lpi->ps_close_thd_set = PSensor_CALIDATA[0] + lpi->ps_crosstalk_diff;
			set_ps_threshold();
		}
		else {
			lpi->ps_crosstalk_diff = crosstalk_diff;
			pr_err("[PS][CM36656]proximity_autok : crosstalk_diff = %d, too big\n", crosstalk_diff);
		}
	}
}
#endif
//<asus alex_wang 20170222>psensor autok+++++

static int cm36656_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	struct cm36656_info *lpi;
#ifndef CONFIG_OF  
	struct cm36656_platform_data *pdata;
#endif

	D("[ALS+PS][CM36656] %s\n", __func__);

	lpi = kzalloc(sizeof(struct cm36656_info), GFP_KERNEL);
	if (!lpi)
		return -ENOMEM;

	lpi->i2c_client = client;
	lpi->irq = client->irq;
	i2c_set_clientdata(client, lpi);

	lpi->als_polling_delay = msecs_to_jiffies(LS_POLLING_DELAY);
	//asus alex_wang rgb porting++++
	lpi->rgb_debug = 0;
	//asus alex_wang rgb porting----
#ifndef CONFIG_OF
	pdata = client->dev.platform_data;
	if (!pdata) {
		pr_err("[ALS+PS][CM36656 error]%s: Assign platform_data error!!\n",
			__func__);
		ret = -EBUSY;
		goto err_platform_data_null;
	}

	lpi->intr_pin = pdata->intr;
	lpi->power = pdata->power;
	lpi->slave_addr = pdata->slave_addr;
	lpi->ps_away_thd_set = pdata->ps_away_thd_set;
	lpi->ps_close_thd_set = pdata->ps_close_thd_set;
	lpi->ps_conf1_val = pdata->ps_conf1_val;
	lpi->ps_conf3_val = pdata->ps_conf3_val;
#else
	if( cm36656_parse_dt(&client->dev, lpi) < 0 )
	{
		ret = -EBUSY;
		goto err_platform_data_null;
	}
	lpi->ps_irq_flag=0;
	lpi->ps_suspend_irq_flag=0;
	lpi->ps_away_thd_set = 0x7;
	lpi->ps_close_thd_set = 0xF;
	lpi->ps_conf1_val = CM36656_PS_PERS_1 | CM36656_PS_START | CM36656_PS_PERIOD_8;
	lpi->ps_conf1_val = lpi->ps_conf1_val | CM36656_PS_IT_2T;
	lpi->ps_conf3_val = CM36656_LED_I_110 | CM36656_PS_RESERVED_BIT_1;
	//asus alex_wang ALS sensor porting++++
	lpi->ls_low_thd_set=0x0;
	lpi->ls_high_thd_set=0x3e8;
	//lpi->rgb_cmd = CM36656_ALS_INT_EN;
	//asus alex_wang ALS sensor porting----
	lpi->power = NULL;
#endif
	//asus alex_wang rgb porting++++
	lpi->rgb_cmd = CM36656_CS_START;
	lpi->rgb_cmd &= CM36656_CS_IT_50MS | CM36656_CS_PERS_1;//setting default RGB IT 50MS
#ifndef ASUS_FACTORY_BUILD
	lpi->last_it=CM36656_CS_IT_50MS;//asus rgb timer
#endif
	sensitivity=sensitivity_default;
	//asus alex_wang rgb porting----
	//asus alex_wang ALS sensor porting default set intrrupent source Green+++
	lpi->ls_int_source = CM36656_ALS_SOURCE_GREEN;
	//asus alex_wang ALS sensor porting default set intrrupent source Green---
	//asus alex_wang setting  chip power +++++
	ret = cm36656_power_set(lpi,true);
	if(ret < 0){
		pr_err("[CM36656 error]%s: chip set power error\n", __func__);
		goto err_power_on;
	}

	//asus alex_wang setting  chip power -----
	LOGE("[PS][CM36656] %s: rgb_cmd 0x%x\n",__func__, lpi->rgb_cmd);
	lp_info = lpi;
	mutex_init(&CM36656_cctreport_mutex);
	mutex_init(&CM36656_i2c_mutex);
	mutex_init(&CM36656_control_mutex);
	//asus alex_wang rgb porting++++
	mutex_init(&rgb_control_mutex);
	mutex_init(&rgb_get_adc_mutex);
	ret = rgbsensor_setup(lpi);
	if (ret < 0) {
		pr_err("[rgb][CM36656 error]%s: rgbsensor_setup error!!\n",
			__func__);
		goto err_rgbsensor_setup;
	}
	//asus alex_wang rgb porting----

	lpi->system_ls_enable = 0;
	lpi->camera_ls_enable = 0;
	lpi->system_ps_enable = 0;
	lpi->factory_ps_enable = 0;
	mutex_init(&als_control_mutex);
	mutex_init(&als_get_adc_mutex);
        lpi->lp_als_wq = create_singlethread_workqueue("cm36656_als_wq");
	if (!lpi->lp_als_wq) {
		pr_err("[CM36656 error]%s: can't create workqueue\n", __func__);
		ret = -ENOMEM;
		goto err_create_singlethread_als_workqueue;
	}
	ret = lightsensor_setup(lpi);
	if (ret < 0) {
		pr_err("[LS][CM36656 error]%s: lightsensor_setup error!!\n",
			__func__);
		goto err_lightsensor_setup;
	}

	mutex_init(&ps_control_mutex);
	mutex_init(&ps_get_adc_mutex);

	ret = psensor_setup(lpi);
	if (ret < 0) {
		pr_err("[PS][CM36656 error]%s: psensor_setup error!!\n",
			__func__);
		goto err_psensor_setup;
	}


	lpi->lp_wq = create_singlethread_workqueue("cm36656_wq");
	if (!lpi->lp_wq) {
		pr_err("[PS][CM36656 error]%s: can't create workqueue\n", __func__);
		ret = -ENOMEM;
		goto err_create_singlethread_workqueue;
	}

#ifdef CONFIG_HAS_WAKELOCK
	wakeup_source_init(&lpi->wake_src, "proximity_irq");
#endif
	ret = cm36656_setup(lpi);
	if (ret < 0) {
		pr_err("[PS_ERR][CM36656 error]%s: cm36656_setup error!\n", __func__);
		goto err_cm36656_setup;
	}
	lpi->cm36656_class = class_create(THIS_MODULE, "capella_sensors");
	if (IS_ERR(lpi->cm36656_class)) {
		ret = PTR_ERR(lpi->cm36656_class);
		lpi->cm36656_class = NULL;
		goto err_create_class;
	}
	//asus alex_wang rgb porting++++
	lpi->cs_dev = device_create(lpi->cm36656_class,
				NULL, 0, "%s", "rgbsensor");
	if (unlikely(IS_ERR(lpi->cs_dev))) {
		ret = PTR_ERR(lpi->cs_dev);
		lpi->cs_dev = NULL;
		goto err_create_cs_device;
	}
	/* register the attributes */
	ret = sysfs_create_group(&lpi->ls_input_dev->dev.kobj, &rgb_attribute_group);
	if (ret)
		goto err_sysfs_create_group_rgb;

	//asus alex_wang rgb porting----
	lpi->ls_dev = device_create(lpi->cm36656_class,
				NULL, 0, "%s", "lightsensor");
	if (unlikely(IS_ERR(lpi->ls_dev))) {
		ret = PTR_ERR(lpi->ls_dev);
		lpi->ls_dev = NULL;
		goto err_create_ls_device;
	}
	/* register the attributes */
	ret = sysfs_create_group(&lpi->ls_input_dev->dev.kobj, &light_attribute_group);
	if (ret)
		goto err_sysfs_create_group_light;

	lpi->ps_dev = device_create(lpi->cm36656_class,
				NULL, 0, "%s", "proximity");
	if (unlikely(IS_ERR(lpi->ps_dev))) {
		ret = PTR_ERR(lpi->ps_dev);
		lpi->ps_dev = NULL;
		goto err_create_ps_device;
	}
	/* register the attributes */
	ret = sysfs_create_group(&lpi->ps_input_dev->dev.kobj, &proximity_attribute_group);
	if (ret)
		goto err_sysfs_create_group_proximity;
	//asus alex_wang device create file to debug+++++
	//rgb
	ret = device_create_file(lpi->cs_dev, &dev_attr_cs_enable);
	if (ret)
		goto err_create_cs_device_file;

	ret = device_create_file(lpi->cs_dev, &dev_attr_cs_conf);
	if (ret)
		goto err_create_cs_device_file;

	ret = device_create_file(lpi->cs_dev, &dev_attr_color_red);
	if (ret)
		goto err_create_cs_device_file;

	ret = device_create_file(lpi->cs_dev, &dev_attr_color_green);
	if (ret)
		goto err_create_cs_device_file;

	ret = device_create_file(lpi->cs_dev, &dev_attr_color_blue);
	if (ret)
		goto err_create_cs_device_file;

	ret = device_create_file(lpi->cs_dev, &dev_attr_color_ir);
	if (ret)
		goto err_create_cs_device_file;	
	ret = device_create_file(lpi->cs_dev, &dev_attr_cs_rgb_debug);
	if (ret)
		goto err_create_cs_device_file;
	ret = device_create_file(lpi->cs_dev, &dev_attr_cs_rgbir_it);
	if (ret)
		goto err_create_cs_device_file;
#ifdef ASUS_FACTORY_BUILD
		ret = device_create_file(lpi->cs_dev, &dev_attr_cs_status);
	if (ret)
		goto err_create_cs_device_file;
#endif
	//light
	ret = device_create_file(lpi->ls_dev, &dev_attr_ls_enable);
	if (ret)
		goto err_create_ls_device_file;
	ret = device_create_file(lpi->ls_dev, &dev_attr_ls_conf);
	if (ret)
		goto err_create_ls_device_file;
	ret = device_create_file(lpi->ls_dev, &dev_attr_light_adc);
	if (ret)
		goto err_create_ls_device_file;
	ret = device_create_file(lpi->ls_dev, &dev_attr_light_intsource);
	if (ret)
		goto err_create_ls_device_file;
	ret = device_create_file(lpi->ls_dev, &dev_attr_light_thd);
	if (ret)
		goto err_create_ls_device_file;
	ret = device_create_file(lpi->ls_dev, &dev_attr_light_pers);
	if (ret)
		goto err_create_ls_device_file;
	ret = device_create_file(lpi->ls_dev, &dev_attr_light_debug);
	if (ret)
		goto err_create_ls_device_file;
	ret = device_create_file(lpi->ls_dev, &dev_attr_light_adc_debug);
	if (ret)
		goto err_create_ls_device_file;
	ret = device_create_file(lpi->ls_dev, &dev_attr_light_rgbratio);
	if (ret)
		goto err_create_ls_device_file;

	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_adc);
	if (ret)
		goto err_create_ps_device_file;
	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_enable);
	if (ret)
		goto err_create_ps_device_file;
	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_conf);
	if (ret)
		goto err_create_ps_device_file;
	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_thd);
	if (ret)
		goto err_create_ps_device_file;
	ret = device_create_file(lpi->ps_dev, &dev_attr_ps_canc);
	if (ret)
		goto err_create_ps_device_file;
	//asus alex_wang device create file to debug------

#ifdef CONFIG_HAS_EARLYSUSPEND
	lpi->early_suspend.level =
			EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	lpi->early_suspend.suspend = cm36656_early_suspend;
	lpi->early_suspend.resume = cm36656_late_resume;
	register_early_suspend(&lpi->early_suspend);
#endif

	//<asus alex_wang 20170222> add factory++++
#ifdef ASUS_FACTORY_BUILD
	ret = create_asusproc_lightsensor_status_entry( );
        if(ret){
	pr_err("[%s] : ERROR to create lightsensor proc entry\n",__func__);}

	ret =create_asusproc_Proximitysensor_status_entry( );
	if(ret){
	pr_err("[%s] : ERROR to create Proximitysensor proc entry\n",__func__);}
#endif
	//<asus alex_wang 20170222> add factory -----
#ifndef ASUS_FACTORY_BUILD
	//<asus alex wang 20170222>psensor autok+++
	hrtimer_init(&lpi->ps_autok_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	lpi->ps_autok_timer.function = proximity_timer_function;
	//<asus alex wang 20170222>psensor autok---
	//rgb timer++++
	hrtimer_init(&lpi->rgb_setit_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	lpi->rgb_setit_timer.function = rgb_setit_timer_function;
	lpi->flag_setit_delay = false;
#endif
	//rgb timer----

	D("[PS][CM36656] %s: Probe success!\n", __func__);
	return ret;

err_create_ps_device_file://asus alex_wang device create file to debug
err_create_ls_device_file:
err_create_cs_device_file://asus alex_wang device create file to debug
	sysfs_remove_group(&lpi->ps_input_dev->dev.kobj, &proximity_attribute_group);
err_sysfs_create_group_proximity:
	device_destroy(lpi->cm36656_class, lpi->ps_dev->devt);
err_create_ps_device:
	sysfs_remove_group(&lpi->ls_input_dev->dev.kobj, &light_attribute_group);
err_sysfs_create_group_light:
	device_destroy(lpi->cm36656_class, lpi->ls_dev->devt);
err_create_ls_device:
	sysfs_remove_group(&lpi->ls_input_dev->dev.kobj, &rgb_attribute_group);
	//asus alex wang rgb porting++++
err_sysfs_create_group_rgb:
	device_destroy(lpi->cm36656_class, lpi->cs_dev->devt);
err_create_cs_device:
	class_destroy(lpi->cm36656_class);
	//asus alex wang rgb porting----
err_create_class:
	gpio_free(lpi->intr_pin); //cm36656_setup
err_cm36656_setup:
#ifdef CONFIG_HAS_WAKELOCK
	wakeup_source_trash(&lpi->wake_src);
#endif
	destroy_workqueue(lpi->lp_wq);
err_create_singlethread_workqueue:
	input_unregister_device(lpi->ps_input_dev);
	misc_deregister(&psensor_misc); //psensor_setup
err_psensor_setup:
	mutex_destroy(&ps_control_mutex);
	mutex_destroy(&ps_get_adc_mutex);
	input_unregister_device(lpi->ls_input_dev);
	misc_deregister(&lightsensor_misc); //lightsensor_setup
err_lightsensor_setup:
	destroy_workqueue(lpi->lp_als_wq);
err_create_singlethread_als_workqueue:
	mutex_destroy(&als_control_mutex);
	mutex_destroy(&als_get_adc_mutex);
	misc_deregister(&rgbsensor_misc); //rgbsensor_setup
	//asus alex_wang rgb porting++++
err_rgbsensor_setup:
        mutex_destroy(&rgb_control_mutex);
	mutex_destroy(&rgb_get_adc_mutex);
	mutex_destroy(&CM36656_control_mutex);
	mutex_destroy(&CM36656_i2c_mutex);
	mutex_destroy(&CM36656_cctreport_mutex);
	//asus alex_wang rgb porting----
	//asus alex_wang setting  chip power +++++
err_power_on:
	//asus alex_wang setting  chip power -----
err_platform_data_null:
	cm36656_probe_fail=1;//asus alex wang porting psensor
	kfree(lpi);
	return ret;
}

//static int debug_log_count=0;
static int control_and_report( struct cm36656_info *lpi, uint8_t mode, uint16_t param ) {
	int ret=0;
	uint16_t ps_data = 0;
	int val;
	
	mutex_lock(&CM36656_control_mutex);
	if( mode == CONTROL_ALS ){
		LOGE("[LS][CM36656]control\n");
		if(param){
			lpi->rgb_cmd &= CM36656_CS_SD_MASK;
			lpi->rgb_cmd &=CM36656_CS_IT_MASK;
			lpi->rgb_cmd |= CM36656_CS_START;
			lpi->rgb_cmd |= CM36656_ALS_PERS_1;
			//asua alex wang ALS sensor porting++++ set interrupt source
			if(lpi->ls_int_source == CM36656_ALS_SOURCE_GREEN){
				lpi->rgb_cmd &= CM36656_ALS_INT_SEL_MASK;
			}else if(lpi->ls_int_source == CM36656_ALS_SOURCE_IR){
			     lpi->rgb_cmd |= CM36656_ALS_INT_SEL;
			}
			//asua alex wang ALS sensor porting---- set interrupt source
		} else {
			//asus alex wang ALS sensor porting+++++
			//when rgb enable light sensor can't be disable
			param=1;
			lpi->als_enable = 0;
			// lpi->rgb_cmd &=CM36656_ALS_INT_EN_MASK;
			if(lpi->rgb_enable == 0){
				lpi->rgb_cmd |= CM36656_CS_SD;
				param=0;
			}
			if(param == 1)
				goto als_pass;
			//asus alex wang ALS sensor porting-----
		}
		LOGE("[LS][CM36656]write CS_CONF lpi->rgb_cmd=0x%4x \n",lpi->rgb_cmd);
		ret=_cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
		if(!ret){
			lpi->als_enable=param;
			if (!lpi->als_enable) {
				pr_err("[LS][CM36656]light sensor last report lux = %d\n", lpi->last_report_lux);
			}
		}

	} else if( mode == CONTROL_PS ){
		if(param){
			lpi->ps_conf1_val &= CM36656_PS_SD_MASK;
			lpi->ps_conf1_val |= CM36656_PS_INT_IN_AND_OUT;
			lpi->ps_conf1_val |= CM36656_PS_START;
		} else {
			lpi->ps_conf1_val |= CM36656_PS_SD;
			lpi->ps_conf1_val &= CM36656_PS_INT_MASK;
			lpi->ps_conf1_val &=CM36656_PS_START_MASK;
		}
		LOGE("[PS][CM36656]write PS_CONF1 lpi->ps_conf1_val=0x%4x \n",lpi->ps_conf1_val);
		ret = _cm36656_I2C_Write_Word(lpi->slave_addr, PS_CONF1, lpi->ps_conf1_val);
		if(!ret){
			lpi->ps_enable=param;}
		}

	else if( mode == CONTROL_RGB ){
		if(param){
			lpi->rgb_cmd &= CM36656_CS_SD_MASK;
		        lpi->rgb_cmd |= CM36656_CS_START;
		} else {
			param = 1;
			lpi->rgb_enable=0;
			if(lpi->als_enable == 0){
				//lpi->rgb_cmd &= CM36656_CS_START_MASK;
				lpi->rgb_cmd |= CM36656_CS_SD;
				param = 0;
			}
			if(param == 1)
				goto als_pass;
		}
		_cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
		lpi->rgb_enable=param;
	}

	else if(mode == CONTROL_RGB_IT){
		uint16_t rgb_temp_cmd = lpi->rgb_cmd & CM36656_CS_IT_MASK;
		if(param == -1)
			goto als_pass;
		switch(param){
		case 0:
			rgb_temp_cmd = rgb_temp_cmd | CM36656_CS_IT_50MS;
			lpi->rgb_cmd = rgb_temp_cmd;
			lpi->als_polling_delay = msecs_to_jiffies(50);
		        sensitivity=sensitivity_125;
			break;
		case 1:
			rgb_temp_cmd = rgb_temp_cmd | CM36656_CS_IT_100MS;
			lpi->rgb_cmd = rgb_temp_cmd;
			lpi->als_polling_delay = msecs_to_jiffies(100);
			sensitivity=sensitivity_250;
			break;
		case 2:
			rgb_temp_cmd = rgb_temp_cmd | CM36656_CS_IT_200MS;
			lpi->rgb_cmd = rgb_temp_cmd;
			lpi->als_polling_delay = msecs_to_jiffies(200);
			sensitivity=sensitivity_500;
			break;
		case 3:
			rgb_temp_cmd = rgb_temp_cmd | CM36656_CS_IT_400MS;
			lpi->rgb_cmd = rgb_temp_cmd;
			lpi->als_polling_delay = msecs_to_jiffies(400);
			sensitivity=sensitivity_1000;
			break;
		default:
			pr_err("[CS][CM36656][RGBIT]setting RGBIT error\n");
			break;
		}
		LOGE("[CS][CM36656][RGBIT]lpi->als_polling_delay=%d,sensitivity=%d\n",lpi->als_polling_delay,sensitivity);
		_cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
		//msleep((param+1)*50*125/100);//delay 1.25*CS_IT
	}

als_pass:
	//asus alex wang ALS sensor porting
	//asus alex_wang porting rgb----
	//<asus alex wang 20170222>psensor autok++++
#ifndef ASUS_FACTORY_BUILD
	if(mode == CONTROL_PS){
		if( param==1 ){
			msleep(PROXIMITY_AUTOK_DELAY);
			lpi->ps_autok_enable = true;
			if (lpi->ps_autok_module_enable) {
				int error;
				/*Stage 1 : check first 6 adc which spend about 50ms~100ms*/
				error = psensor_check_minCT();
				if (error < 0) {
					pr_err("[PS][CM36656]proximity_check_minCT ERROR\n");
					lpi->ps_autok_enable = false;
				}
			}
			/*Stage 2 : start polling proximity adc(500ms) to check min value*/
			if (lpi->ps_autok_enable && lpi->ps_crosstalk_diff != 0) {
				ktime_t autok_delay;
				autok_delay = ns_to_ktime(PROXIMITY_AUTOK_POLLING_MS * NSEC_PER_MSEC);
				hrtimer_start(&lpi->ps_autok_timer, autok_delay, HRTIMER_MODE_REL);
			}
		} else {
			if (lpi->ps_autok_enable) {
				hrtimer_cancel(&lpi->ps_autok_timer);
				lpi->ps_autok_enable = false;
			}
		}
	}
#endif
	//<asus alex wang 20170222>psensor autok++++
	//asus alex wang ALS porting++++
	/*
		if(lpi->als_enable){
			//uint16_t adc_value;
			//int report_lux, report_golden;
			// int dark_adc=0,bright_adc=0;
			if(mode == CONTROL_ALS && lpi->rgb_enable == 0){
				// lpi->rgb_cmd &=CM36656_ALS_INT_EN_MASK;
				// _cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
				// pr_err("[LS][CM36656]first enable\n");
				//    queue_delayed_work(lpi->lp_als_wq, &lightsensor_value_work, lpi->als_polling_delay*125/100);
				// lpi->rgb_cmd |=CM36656_ALS_INT_EN;
				//   ret = _cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
			}

			if( (mode == CONTROL_ALS &&lpi->rgb_enable==1) ||( mode == CONTROL_INT_ISR_REPORT && ((param&INT_FLAG_ALS_IF_L)||(param&INT_FLAG_ALS_IF_H)))){
				lpi->rgb_cmd &=CM36656_ALS_INT_EN_MASK;
				_cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
				LOGE("[LS][CM36656][%s] disable int finish\n",__func__);
				read_ls_adc_value();
				adc_value=get_ls_intsource_adc();
				//asus alex wang als clabration+++++
				dark_adc=LSensor_CALIDATA/3;
				bright_adc=LSensor_CALIDATA*5/3;
				report_lux = read_ls_calibration_value(adc_value,dark_adc,bright_adc);
				report_golden=read_cct_calibration_value(adc_value,400,2000);//godlen
				//asus alex wang als calibration---
				//asus alex wang debug log+++
				if((g_ls_adc_debug==1)||(debug_log_count>10)) {
					pr_err("[LS][CM36656] %s report lux = %x report_golden = %x R=%x G=%x B=%x IR=%x\n", __func__, report_lux,report_golden,cm36656_adc_red, cm36656_adc_green, cm36656_adc_blue, cm36656_adc_ir);
					debug_log_count=0;
				}else{
					debug_log_count++;
				}
				//asus alex wang debug log ----
				report_ls_input_event(report_lux,report_golden);
				lpi->rgb_cmd |=CM36656_ALS_INT_EN;
				ret = _cm36656_I2C_Write_Word(lpi->slave_addr, CS_CONF, lpi->rgb_cmd);
				LOGE("[LS][CM36656] enable int finish\n");
   		   	}
		}*/
//asus alex wang ALS porting----

#define PS_CLOSE 1
#define PS_AWAY  (1<<1)
#define PS_CLOSE_AND_AWAY PS_CLOSE+PS_AWAY
		if(lpi->ps_enable){
			int ps_status = 0;
			if( mode == CONTROL_PS )
				ps_status = PS_CLOSE_AND_AWAY;
			else if(mode == CONTROL_INT_ISR_REPORT ){
				if ( param & INT_FLAG_PS_IF_CLOSE )
					ps_status |= PS_CLOSE;
				if ( param & INT_FLAG_PS_IF_AWAY )
					ps_status |= PS_AWAY;
			}

		if (ps_status!=0){
			switch(ps_status){
			case PS_CLOSE_AND_AWAY:
				// get_stable_ps_adc_value(&ps_data);
				get_ps_adc_value(&ps_data);
				val = (ps_data >= lpi->ps_close_thd_set) ? 0 : 10;
				D("[PS][CM36656]proximity detected object PS_CLOSE_AND_AWAY val=%d\n",val);
				break;
			case PS_AWAY:
				val = 10;
				D("[PS][CM36656] proximity detected object away\n");
				break;
			case PS_CLOSE:
				get_ps_adc_value(&ps_data);
				val = (ps_data >= lpi->ps_close_thd_set_1cm) ? 0 : 2;
				D("[PS][CM36656] proximity detected object close ps_data=%d, lpi->ps_close_thd_set_1cm=%d\n",ps_data,lpi->ps_close_thd_set_1cm);
				break;
			};

			proximity_state = val;
			if((proximity_state == 0 || proximity_state == 2))
			{
				if(ps_bypass_near_event)
				{
					focal_psensor_disable_touch = false;
				}
				else
				{
					focal_psensor_disable_touch = true;
				}
				printk("[PS][CM36656] psensor is close\n");
			}
			else
			{
				ps_bypass_near_event = 0;
				focal_psensor_disable_touch = false;
				printk("[PS][CM36656] psensor is away\n");
			}
			report_psensor_input_event(lpi, val);
		}
	}

	mutex_unlock(&CM36656_control_mutex);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int cm36656_suspend(struct device *dev)
{
	struct cm36656_info *lpi;
	lpi = dev_get_drvdata(dev);
	pr_err("CM36656: suspend ps:%d\n",lpi->ps_enable);
	/*
	  * Save sensor state and disable them,
	  * this is to ensure internal state flags are set correctly.
	  * device will power off after both sensors are disabled.
	  * P sensor will not be disabled because it  is a wakeup sensor.
	*/
	
	lpi->als_enabled_before_suspend = lpi->als_enable;  
	lpi->rgb_enabled_before_suspend = lpi->rgb_enable;
#ifndef ASUS_FACTORY_BUILD
	lpi->ps_autok_enabled_before_suspend= lpi->ps_autok_enable;
#endif

#ifdef UNSUPPORT_AUTO_BACKLIGHT
	if (lpi->als_enable == 1)
		lightsensor_disable(lpi);
	if (lpi->rgb_enable)
		rgbsensor_disable(lpi);
#ifndef ASUS_FACTORY_BUILD
	if(lpi->ps_autok_enable)
		hrtimer_cancel(&lpi->ps_autok_timer);
#endif
#endif
	return 0;
}

static int cm36656_resume(struct device *dev)
{
	struct cm36656_info *lpi;
	lpi = dev_get_drvdata(dev);
        pr_err("CM36656:  resume ps:%d\n",lpi->ps_enable);

	/* Don't disable light at phone calling
	  * while the automatic backlight is on.
	  */
#ifdef UNSUPPORT_AUTO_BACKLIGHT
	if (lpi->als_enabled_before_suspend)
		lightsensor_enable(lpi);
        if (lpi->rgb_enabled_before_suspend)
		rgbsensor_enable(lpi);
#ifndef ASUS_FACTORY_BUILD
	if(lpi->ps_autok_enabled_before_suspend&& lpi->ps_crosstalk_diff != 0){
                 ktime_t autok_delay;
		 autok_delay = ns_to_ktime(PROXIMITY_AUTOK_POLLING_MS * NSEC_PER_MSEC);
	         hrtimer_start(&lpi->ps_autok_timer, autok_delay, HRTIMER_MODE_REL);
	}
#endif
#endif	
      
	return 0;
}
#endif

static UNIVERSAL_DEV_PM_OPS(cm36656_pm, cm36656_suspend, cm36656_resume, NULL);


static const struct i2c_device_id cm36656_i2c_id[] = {
	{CM36656_I2C_NAME, 0},
	{}
};

#ifdef CONFIG_OF
static struct of_device_id cm36656_match_table[] = {
	{ .compatible = "capella,cm36656",},
	{ },
};
#else
#define cm36656_match_table NULL
#endif

static struct i2c_driver cm36656_driver = {
	.id_table = cm36656_i2c_id,
	.probe = cm36656_probe,
	.driver = {
		.name = CM36656_I2C_NAME,
		.owner = THIS_MODULE,
		.pm = &cm36656_pm,    
               .of_match_table = cm36656_match_table,     
	},
};

static int __init cm36656_init(void)
{
	return i2c_add_driver(&cm36656_driver);
}

static void __exit cm36656_exit(void)
{
	i2c_del_driver(&cm36656_driver);
}

module_init(cm36656_init);
module_exit(cm36656_exit);

MODULE_AUTHOR("Frank Hsieh <Frank.Hsieh@vishay.com>");
MODULE_DESCRIPTION("CM36656 Optical Sensor Driver");
MODULE_LICENSE("GPL v2");
