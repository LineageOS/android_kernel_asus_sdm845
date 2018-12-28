/*
ADC asp1690 Driver
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/of.h>

#include <linux/gpio.h>

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/spmi.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/semaphore.h>
#include <linux/device.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/sched.h>
#include <linux/spinlock_types.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/clk.h>
#include <linux/proc_fs.h>

//ASUS BSP Austin_T : global asp1690_READY +++
bool asp1690_ready;
EXPORT_SYMBOL(asp1690_ready);

//Define register addresses of asp1690 0X38
#define asp1690_raddr 0x39
#define CHIP_ID 0x43

#define DRACO_750K_MIN		0xC2
#define DRACO_750K_MAX		0xDE
#define DRACO_200K_MIN		0x2F
#define DRACO_200K_MAX		0x41

#define USB_THERM_ALERT_HIGH_THRESH_1  0xFF // min temp
#define USB_THERM_ALERT_LOW_THRESH_1 0x56 // 0.774V = deg 70
#define USB_THERM_ALERT_HIGH_THRESH_2  0x69 // 0.945V = deg 60
#define USB_THERM_ALERT_LOW_THRESH_2 0x00// max temp
#define USB_THERM_ALERT_LOW_TRIGGER 1
#define USB_THERM_ALERT_HIGH_TRIGGER 2
#define USB_THERM_ALERT_NOT_TRIGGER 0

enum ADAPTER_ID {
	ASUS_750K,
	ASUS_200K,
	PB,
	OTHERS,
	ADC_NOT_READY,
};

struct asp1690 {
	struct i2c_client	*client;
	struct device		*dev;
	int adc_en_gpio;
	u8 thermal_low_thresh;
	u8 thermal_high_thresh;
};

struct asp1690 *asp1690_chip;
//~ struct i2c_client *asp1690_client;

bool asp1690_adc_enable = 0;

/*
asp1690_write_reg():	write 8 bits reg function
slave_addr:	SMBus address (7 bits)
cmd_reg   :	cmd register for programming
write_val  :	the value will be written
*/
int asp1690_write_reg(uint8_t slave_addr, uint8_t cmd_reg, uint8_t write_val)
{
	int ret = 0;

	//~ printk("[BAT][CHG] asp1690_write_reg 0x%x start\n",cmd_reg);
	asp1690_chip->client->addr = slave_addr; //real SMBus address (8 bits)
	ret = i2c_smbus_write_byte_data(asp1690_chip->client, cmd_reg, write_val);
	if (ret < 0) {
		printk("%s: failed to write i2c addr=%x\n",
			__func__, slave_addr);
	}
	return ret;
}

/*
asp1690_read_reg():	read 8 bits reg function
slave_addr:	SMBus address (7 bits)
cmd_reg   :	cmd register for programming
store_read_val  :	value be read will store here

*/
int asp1690_read_reg(uint8_t slave_addr, uint8_t cmd_reg, uint8_t *store_read_val)
{
	int ret = 0;

	asp1690_chip->client->addr = slave_addr;
	ret = i2c_smbus_read_byte_data(asp1690_chip->client, cmd_reg);
	if (ret < 0) {
		printk("%s: failed to read i2c addr=%x\n",	__func__, slave_addr);
	}

	*store_read_val = (uint8_t) ret;

	return ret;
}

u8 asp1690_adapter_value(void)
{
	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, 0x46, &my_read_value);
	printk("[BAT][CHG] asp1690_value = 0x%xh\n", my_read_value);

	return my_read_value;
}
EXPORT_SYMBOL(asp1690_adapter_value);

u8 asp1690_thermal_value(void)
{
	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, 0x03, &my_read_value);
	printk("[BAT][CHG] asp1690_value = 0x%xh\n", my_read_value);

	return my_read_value;
}
EXPORT_SYMBOL(asp1690_thermal_value);

static ssize_t adapter_value_show(struct device *dev, struct device_attribute *da,
	char *buf)
{
	u8 val;

	val = asp1690_adapter_value();

	return sprintf(buf, "0x%xh\n", val);
}

static ssize_t thermal_value_show(struct device *dev, struct device_attribute *da,
	char *buf)
{
	u16 val;

	val = asp1690_thermal_value();

	return sprintf(buf, "0x%xh\n", val);
}

static ssize_t adc_ack_show(struct device *dev, struct device_attribute *da,
	char *buf)
{
	u8 val = 0;
	int ret = 0;
	bool ack = 1;

	ret = asp1690_read_reg(asp1690_raddr, 0x46, &val);
	if (ret < 0)
		ack = 0;
	else
		ack = 1;

	return sprintf(buf, "%d\n", ack);
}

static DEVICE_ATTR(adapter_value, 0664, adapter_value_show, NULL);
static DEVICE_ATTR(thermal_value, 0664, thermal_value_show, NULL);
static DEVICE_ATTR(adc_ack, 0664, adc_ack_show, NULL);

static struct attribute *dump_reg_attrs[] = {
	&dev_attr_adapter_value.attr,
	&dev_attr_thermal_value.attr,
	&dev_attr_adc_ack.attr,
	NULL
};


static const struct attribute_group dump_reg_attr_group = {
	.attrs = dump_reg_attrs,
};


void SwitchTo3D(void)
{
	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, 0x30, &my_read_value);
	//~ printk("[BAT][CHG] %s asp1690_30 = 0x%xh\n",__func__, my_read_value);

	my_read_value = (my_read_value & 0x1F)|0x20;

	asp1690_write_reg(asp1690_raddr, 0x30, my_read_value);
	my_read_value = 0;
	asp1690_read_reg(asp1690_raddr, 0x30, &my_read_value);

	//~ printk("[BAT][CHG] %s asp1690_30 = 0x%xh\n",__func__,my_read_value);
}

void SwitchTo1D(void)
{

	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, 0x30, &my_read_value);
	//~ printk("[BAT][CHG] asp1690_reg30 = 0x%xh\n", my_read_value);

	my_read_value = (my_read_value & 0x1F)|0x80;

	asp1690_write_reg(asp1690_raddr, 0x30, my_read_value);
	my_read_value = 0;
	asp1690_read_reg(asp1690_raddr, 0x30, &my_read_value);

	printk("[BAT][CHG] asp1690_reg30 = 0x%xh\n", my_read_value);
}


void MeasureFunc(u8 reg, u8 flag, int delay_ms)
{
	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, reg, &my_read_value);
	//~ printk("[BAT][CHG] asp1690_%x = 0x%xh\n",reg ,my_read_value);

	//if(my_read_value & 0x1F)

	my_read_value = my_read_value |flag;

	asp1690_write_reg(asp1690_raddr, reg, my_read_value);

	my_read_value = 0;
	asp1690_read_reg(asp1690_raddr, reg, &my_read_value);

	printk("[BAT][CHG] asp1690_%x = 0x%xh\n",reg, my_read_value);

	if(delay_ms)
		msleep(delay_ms);
}


void MeasureDMresist(void)
{
	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, 0x32, &my_read_value);
	//~ printk("[BAT][CHG] asp1690_DM = 0x%xh\n", my_read_value);

	my_read_value = my_read_value |0x20;

	asp1690_write_reg(asp1690_raddr, 0x32, my_read_value);
	my_read_value = 0;
	asp1690_read_reg(asp1690_raddr, 0x32, &my_read_value);

	printk("[BAT][CHG] asp1690_DM_resistance = 0x%xh\n", my_read_value);
}

void MeasureDPresist(void)
{
	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, 0x32, &my_read_value);
	//~ printk("[BAT][CHG] asp1690_DP = 0x%xh\n", my_read_value);

	my_read_value = my_read_value |0x80;

	asp1690_write_reg(asp1690_raddr, 0x32, my_read_value);
	my_read_value = 0;
	asp1690_read_reg(asp1690_raddr, 0x32, &my_read_value);

	printk("[BAT][CHG] asp1690_DP_resistance = 0x%xh\n", my_read_value);
}

u8 DM_read(void)
{
	u8 my_read_value = 0;
	//MeasureDMresist();
	//msleep(240);

	asp1690_read_reg(asp1690_raddr, 0x46, &my_read_value);
	printk("[BAT][CHG] asp1690_DM_R = 0x%xh\n", my_read_value);

	return my_read_value;
}
u8 DP_read(void)
{
	u8 my_read_value = 0;

	MeasureDPresist();
	msleep(130);

	asp1690_read_reg(asp1690_raddr, 0x44, &my_read_value);
	printk("[BAT][CHG] asp1690_DP_R = 0x%xh\n", my_read_value);
	return my_read_value;
}

void OpenDpDm(bool open)
{
	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, 0x30, &my_read_value);
	my_read_value = (my_read_value & 0xEF)|(open ? 0x10 : 0x00);
	asp1690_write_reg(asp1690_raddr, 0x30, my_read_value);

	//~ MeasureFunc(0x30,0x10,0);
}

void MeasureAll(void)
{
	u8 my_read_value = 0;

	SwitchTo3D();
	OpenDpDm(false);

	//~ MeasureFunc(0x32,0x80,130
	MeasureFunc(0x32,0x80,15);
	asp1690_read_reg(asp1690_raddr, 0x41, &my_read_value);
	printk("[BAT][CHG] asp1690_41 = 0x%xh\n", my_read_value);	
	//~ MeasureFunc(0x32,0x40,130);
	MeasureFunc(0x32,0x40,15);
	asp1690_read_reg(asp1690_raddr, 0x41, &my_read_value);
	printk("[BAT][CHG] asp1690_41 = 0x%xh\n", my_read_value);	
	//~ MeasureFunc(0x32,0x20,240);
	MeasureFunc(0x32,0x20,200);
	asp1690_read_reg(asp1690_raddr, 0x41, &my_read_value);
	printk("[BAT][CHG] asp1690_41 = 0x%xh\n", my_read_value);
}

#define VDM_LOW_THD		0x2C	// 13.5mV per level,
#define VDP_LOW_THD		0X2C
#define VDM_HIGH_THD		0x78
#define VDP_HIGH_THD		0X78

#define BELOW_LOW_THRESH 0
#define ABOVE_HIGH_THRESH 1
// dir: 0 - check low thresh
//        1 - check high thresh
bool CheckDMvDPv(u8 dir)
{
	u8 dpv=0,dmv=0;

	asp1690_read_reg(asp1690_raddr, 0x44, &dpv);
	asp1690_read_reg(asp1690_raddr, 0x45, &dmv);
	printk("[BAT][CHG] asp1690_44 = 0x%xh (Vdp)\n", dpv);
	printk("[BAT][CHG] asp1690_45 = 0x%xh (Vdm)\n", dmv);

    if (dir) { // above high thresh
        return (dpv > VDP_HIGH_THD && dmv > VDM_HIGH_THD);
    } else { // below low thresh
        return (dpv < VDP_LOW_THD && dmv < VDM_LOW_THD );
    }
}

int asp1690E_CHG_TYPE_judge(void)
{
	u8 adc_result;
    int asus_id = OTHERS;

	MeasureAll();	
	if(CheckDMvDPv(BELOW_LOW_THRESH)){ // ASUS_ID or OTHERS
		adc_result = DM_read();
		if(adc_result >= DRACO_750K_MIN && adc_result <= DRACO_750K_MAX){
            asus_id = ASUS_750K;
		}
		else if(adc_result >= DRACO_200K_MIN && adc_result <= DRACO_200K_MAX){
            asus_id = ASUS_200K;
		}
		else{
            asus_id = OTHERS;
		}
	} else { // PB or OTHERS
		if(CheckDMvDPv(ABOVE_HIGH_THRESH)) {
            asus_id = PB;
        }
		else {
            asus_id = OTHERS;
        }
	}
	SwitchTo1D();
    return asus_id;
}

void usb_therm_set_2DM(void)
{
	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, 0x31, &my_read_value);
	printk("[BAT][CHG] asp1690_2DM = 0x%xh\n", my_read_value);

	my_read_value = my_read_value |0x40;

	asp1690_write_reg(asp1690_raddr, 0x31, my_read_value);
	my_read_value = 0;
	asp1690_read_reg(asp1690_raddr, 0x31, &my_read_value);

	printk("[BAT][CHG] asp1690_2DM = 0x%xh\n", my_read_value);
}

void usb_therm_set_2DM_high_thresh(u8 high_thresh)
{
	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, 0x3E, &my_read_value);
	printk("[BAT][CHG] asp1690_2DM_high_thresh_before = 0x%xh\n", my_read_value);

	my_read_value = high_thresh;
	asp1690_write_reg(asp1690_raddr, 0x3E, my_read_value);
	my_read_value = 0;
	asp1690_read_reg(asp1690_raddr, 0x3E, &my_read_value);
	printk("[BAT][CHG] asp1690_2DM_high_thresh_after = 0x%xh\n", my_read_value);
    asp1690_chip->thermal_high_thresh = high_thresh;
}

void usb_therm_set_2DM_low_thresh(u8 low_thresh)
{
	u8 my_read_value = 0;

	asp1690_read_reg(asp1690_raddr, 0x3F, &my_read_value);
	printk("[BAT][CHG] asp1690_2DM_low_thresh_before = 0x%xh\n", my_read_value);

	my_read_value = low_thresh;
	asp1690_write_reg(asp1690_raddr, 0x3F, my_read_value);
	my_read_value = 0;
	asp1690_read_reg(asp1690_raddr, 0x3F, &my_read_value);
	printk("[BAT][CHG] asp1690_2DM_low_thresh_after = 0x%xh\n", my_read_value);
    asp1690_chip->thermal_low_thresh = low_thresh;
}

void usb_therm_set_thresh_1(void)
{
	u8 my_read_value = 0;

    asp1690_read_reg(asp1690_raddr, 0x48, &my_read_value);
    printk("[BAT][CHG] asp1690_0x48: 0x%xh\n", my_read_value);

    usb_therm_set_2DM_high_thresh(USB_THERM_ALERT_HIGH_THRESH_1);
    usb_therm_set_2DM_low_thresh(USB_THERM_ALERT_LOW_THRESH_1);
}

void usb_therm_set_thresh_2(void)
{
	u8 my_read_value = 0;

    asp1690_read_reg(asp1690_raddr, 0x48, &my_read_value);
    printk("[BAT][CHG] asp1690_0x48: 0x%xh\n", my_read_value);

    usb_therm_set_2DM_high_thresh(USB_THERM_ALERT_HIGH_THRESH_2);
    usb_therm_set_2DM_low_thresh(USB_THERM_ALERT_LOW_THRESH_2);
}

void save_thermal_thresh(void)
{
	u8 my_read_value = 0;
	int ret = 0;

	ret = asp1690_read_reg(asp1690_raddr, 0x3E, &my_read_value);
	printk("[BAT][CHG] asp1690_2DM_high_thresh_before = 0x%xh\n", my_read_value);
	if (ret >= 0)
		asp1690_chip->thermal_high_thresh = my_read_value;

	asp1690_read_reg(asp1690_raddr, 0x3F, &my_read_value);
	printk("[BAT][CHG] asp1690_2DM_low_thresh_before = 0x%xh\n", my_read_value);
	if (ret >= 0)
		asp1690_chip->thermal_low_thresh = my_read_value;
}

void restore_thermal_thresh(void)
{
    u8 my_read_value = 0;

    usb_therm_set_2DM();

    asp1690_read_reg(asp1690_raddr, 0x48, &my_read_value);
    printk("[BAT][CHG] asp1690_0x48: 0x%xh\n", my_read_value);

    usb_therm_set_2DM_high_thresh(asp1690_chip->thermal_high_thresh);
    usb_therm_set_2DM_low_thresh(asp1690_chip->thermal_low_thresh);
}

int usb_therm_trigger(void)
{
    u8 my_read_value_1 = 0;
    u8 my_read_value_2 = 0;
    int ret = 0;

    ret = asp1690_read_reg(asp1690_raddr, 0x42, &my_read_value_1);
    if (ret < 0) {
        return -1;
    }
    printk("[BAT][CHG] asp1690_0x42: 0x%xh\n", my_read_value_1);

    ret = asp1690_read_reg(asp1690_raddr, 0x48, &my_read_value_2);
    if (ret < 0) {
        return -1;
    }
    printk("[BAT][CHG] asp1690_0x48: 0x%xh\n", my_read_value_2);

    if (my_read_value_1 & ((u8)1 << 5)) { // F_2DMH = 1
        return USB_THERM_ALERT_HIGH_TRIGGER;
    } else if (my_read_value_1 & ((u8)1 << 4)) { // F_2DML = 1
        return USB_THERM_ALERT_LOW_TRIGGER;
    } else {
        return USB_THERM_ALERT_NOT_TRIGGER;
    }
}

void asp1690_enable(bool enable)
{
    int rc = 0;

    printk("%s: asp1690_enable = %d\n", __FUNCTION__, enable);
    if (asp1690_ready) {
        if (enable) {
            // enable asp1690 first
            rc = gpio_direction_output(asp1690_chip->adc_en_gpio, 0);
            if (rc < 0) {
                printk("%s: set direction of asp1690_adc_en(%d) fail!\n", __FUNCTION__, enable);
                return;
            }
            // restore thermal alert threshold
            restore_thermal_thresh();
        } else {
            // save thermal alert threshold first
            save_thermal_thresh();
            // disable asp1690
            rc = gpio_direction_output(asp1690_chip->adc_en_gpio, 1);
            if (rc < 0) {
                printk("%s: set direction of asp1690_adc_en(%d) fail!\n", __FUNCTION__, enable);
                return;
            }
        }
    }
}

#define ASUS_ASP1690_PROC_FILE "driver/asp1690_Status"

static int asp1690_proc_read(struct seq_file *buf, void *data)
{
	u8 reg, ret;	

	ret = asp1690_read_reg(asp1690_raddr, CHIP_ID, &reg);
	if(ret < 0)
		ret = 0;
	else
		ret = 1;
	seq_printf(buf, "%d\n", ret);
	return 0;
}
static int asp1690_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, asp1690_proc_read, NULL);
}
static const struct file_operations asp1690_fops = {
	.owner = THIS_MODULE,
	.open = asp1690_proc_open,
	.read = seq_read,
	.release = single_release,
};
static void create_asp1690_proc_file(void)
{
	struct proc_dir_entry *asus_asp1690_proc_file = proc_create(ASUS_ASP1690_PROC_FILE, 0666, NULL, &asp1690_fops);

	if (asus_asp1690_proc_file) {
		//printk("create_asp1690_proc_file create ok!\n");
	} else{
		printk("create_asp1690_proc_file create failed!\n");
	}
}

static int asp1690_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct asp1690 *chip;
	int rc;
    u8 reg = 0;

	printk("[BAT][CHG] %s asp1690e start\n", __FUNCTION__);

	asp1690_ready = 0;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk("[BAT][CHG] %s: i2c bus does not support the asp1690\n", __FUNCTION__);
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);

	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->dev = &client->dev;
	asp1690_chip = chip;

	rc = sysfs_create_group(&client->dev.kobj, &dump_reg_attr_group);
	if (rc)
		goto exit_remove;

	//request enable gpio
	chip->adc_en_gpio = of_get_named_gpio(client->dev.of_node, "asp1690_adc_en", 0);
	//~ pr_debug("asp1690_adc_en_gpio = %d\n", chip->adc_en_gpio);

	if ((!gpio_is_valid(chip->adc_en_gpio))) {
		printk("%s: adc_pwr_en_gpio is not valid!\n", __FUNCTION__);
		return -EINVAL;
	}
	rc = gpio_request(chip->adc_en_gpio,"asp1690_adc_en");
	if (rc < 0) {
		printk("%s: request asp1690_adc_en gpio fail!\n", __FUNCTION__);
		return -EINVAL;
	}

	rc = gpio_direction_output(chip->adc_en_gpio, 0);
	if (rc < 0) {
		printk("%s: set direction of asp1690_adc_en fail!\n", __FUNCTION__);
		return -EINVAL;
	}

	/* probe the device to check if its actually connected */
	rc = asp1690_read_reg(asp1690_raddr, CHIP_ID, &reg);
	if (rc < 0) {
		pr_err("Failed to detect asp1690, device may be absent. rc=%d\n", rc);
		return -ENODEV;
	}
	pr_info("asp1690 chip revision is %x\n", reg);

    create_asp1690_proc_file();

    usb_therm_set_2DM();
    usb_therm_set_thresh_1();

	asp1690_ready = 1;

    //~ if (!asp1690_adc_enable) {
        //~ gpio_direction_output(chip->adc_en_gpio, 1);
    //~ }

	printk("[BAT][CHG] %s asp1690e end\n", __FUNCTION__);

	return 0;

exit_remove:
    sysfs_remove_group(&client->dev.kobj, &dump_reg_attr_group);
	return rc;
}

static int asp1690_remove(struct i2c_client *client)
{
	struct asp1690 *chip = i2c_get_clientdata(client);

	if(!chip)
		pr_err("%s::chip is NULL!\n", __func__);

	if (gpio_is_valid(chip->adc_en_gpio)) {
		gpio_free(chip->adc_en_gpio);
	}

	return 0;
}

static struct of_device_id asp1690_match_table[] = {
	{ .compatible = "asp1690-adc",},
	{ },
};

static const struct i2c_device_id asp1690_id[] = {
	{ "asp1690", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, asp1690_id);

static struct i2c_driver asp1690_driver = {
	.driver = {
		.name = "asp1690",
		.owner		= THIS_MODULE,
		.of_match_table	= asp1690_match_table,
	},
	.probe = asp1690_probe,
	.remove = asp1690_remove,
	.id_table = asp1690_id,
};

static int  asp1690_init(void){ 
	s32 ret;

	ret = i2c_add_driver(&asp1690_driver);
	pr_debug("asp1690_init!\n");
	return ret;        
}
static void  asp1690_exit(void) {
	i2c_del_driver(&asp1690_driver);       
}
module_init(asp1690_init);
module_exit(asp1690_exit);
