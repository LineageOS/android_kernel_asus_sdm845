/*! \file sx9310.c
 * \brief  SX9310 Driver
 *
 * Driver for the SX9310 
 * Copyright (c) 2011 Semtech Corp
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
//#define DEBUG
#define DRIVER_NAME "sx9310"

#define MAX_WRITE_ARRAY_SIZE 32
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>

#include <linux/input/smtc/misc/sx86xx.h> /* main struct, interrupt,init,pointers */
#include <linux/input/smtc/misc/sx9310_i2c_reg.h>
#include <linux/input/smtc/misc/sx9310_platform_data.h>  /* platform data */

#define IDLE 0
#define ACTIVE 1
#define GPIO_SX9310_NIRQ 24

psx86XX_t sx86xx_info = NULL;
#ifdef ASUS_FACTORY_BUILD
struct proc_dir_entry *sarsensor_entry = NULL;
#endif
static int sx9310_gpio_parse_dt(
				 psx86XX_t this)
{

	struct device *dev = 0;
	int rc = 0;
	dev = this->pdev;
	pr_debug("enter %s\n",__func__);
  
	rc = of_get_named_gpio_flags(dev->of_node, "sx9310,intrpin-gpios",
			0, NULL);
	if (rc < 0) 
	{
		pr_err("Unable to read interrupt pin number\n");
		return rc;
	} 
	else
	{
		this->intr_pin = rc;
 	    pr_debug("sx9310 get intr_pin = %d\n",this->intr_pin);
	}

	return 0;
}



//#define SX9310_NIRQ OMAP_GPIO_IRQ(GPIO_SX9310_NIRQ)
void gpio_init(psx86XX_t this)
{
    int ret;
	struct i2c_client *i2c = 0;
    i2c = this->bus;
	ret = gpio_request(this->intr_pin, "gpio_sx9310_intr");
	if (ret < 0) {
		dev_err(&i2c->dev,"gpio_request error\n");
		return ;
	}

	ret = gpio_direction_input(this->intr_pin);
	if (ret < 0) {
		dev_err(&i2c->dev,"gpio_direction_input error\n");
		return ;
	}
	gpio_export(this->intr_pin, 0);

}


static int sx9310_get_nirq_state(void)
{
	return !gpio_get_value(GPIO_SX9310_NIRQ);
}


static struct smtc_reg_data sx9310_i2c_reg_setup[] = {
	{
		.reg = SX9310_IRQ_ENABLE_REG,
		.val = 0x70,
	},
	{
		.reg = SX9310_IRQFUNC_REG, //0x00 open drain; 0x01 Push-pull
		.val = 0x01,
	},
	{
		.reg = SX9310_CPS_CTRL1_REG,
		.val = 0x00,
	},
	{
		.reg = SX9310_CPS_CTRL2_REG,
		.val = 0x00,
	},
	{
		.reg = SX9310_CPS_CTRL3_REG,
		.val = 0x0C,
	},
	{
		.reg = SX9310_CPS_CTRL4_REG,
		.val = 0x0D,
	},
	{
		.reg = SX9310_CPS_CTRL5_REG,
		.val = 0xC1,
	},
	{
		.reg = SX9310_CPS_CTRL6_REG,
		.val = 0x20,
	},
	{
		.reg = SX9310_CPS_CTRL7_REG,
		.val = 0x4C,
	},
	{
		.reg = SX9310_CPS_CTRL8_REG,
		.val = 0x7E,
	},
	{
		.reg = SX9310_CPS_CTRL9_REG,
		.val = 0x7D,
	},

	/*{
		.reg = SX9310_CPS_CTRL10_REG,
		.val = 0x00,
	},
	
	{
		.reg = SX9310_CPS_CTRL11_REG,
		.val = 0x00,
	},
	{
		.reg = SX9310_CPS_CTRL12_REG,
		.val = 0x00,
	},
	{
		.reg = SX9310_CPS_CTRL13_REG,
		.val = 0x00,
	},
	{
		.reg = SX9310_CPS_CTRL14_REG,
		.val = 0x00,
	},
	{
		.reg = SX9310_CPS_CTRL15_REG,
		.val = 0x00,
	},
	{
		.reg = SX9310_CPS_CTRL16_REG,
		.val = 0x00,
	},
	{
		.reg = SX9310_CPS_CTRL17_REG,
		.val = 0x00,
	},
	{
		.reg = SX9310_CPS_CTRL18_REG,
		.val = 0x00,
	},
	{
		.reg = SX9310_CPS_CTRL19_REG,
		.val = 0x00,
	},*/
	{
		.reg = SX9310_CPS_CTRL0_REG,
		.val = 0x55,  //enable cs0/cs2
	},
};


static struct _buttonInfo psmtcButtons[] = {
  {
    .keycode = SX9310_KEY_0,
    .mask = SX9310_TCHCMPSTAT_TCHSTAT0_FLAG,
  },
  {
    .keycode = SX9310_KEY_1,
    .mask = SX9310_TCHCMPSTAT_TCHSTAT1_FLAG,
  },
  {
    .keycode = SX9310_KEY_2,
    .mask = SX9310_TCHCMPSTAT_TCHSTAT2_FLAG,
  },
  {
    .keycode = SX9310_KEY_COMB,   //KEY_COMB,
    .mask = SX9310_TCHCMPSTAT_TCHSTAT3_FLAG,
  },
};


static struct _totalButtonInformation smtcButtonInformation = {
  .buttons = psmtcButtons,
  .buttonSize = ARRAY_SIZE(psmtcButtons),
};


/*! \struct sx9310
 * Specialized struct containing input event data, platform data, and
 * last cap state read if needed.
 */
typedef struct sx9310
{
  pbuttonInformation_t pbuttonInformation;
	psx9310_platform_data_t hw; /* specific platform data settings */
} sx9310_t, *psx9310_t;


/*! \fn static int write_register(psx86XX_t this, u8 address, u8 value)
 * \brief Sends a write register to the device
 * \param this Pointer to main parent struct 
 * \param address 8-bit register address
 * \param value   8-bit register value to write to address
 * \return Value from i2c_master_send
 */
static int write_register(psx86XX_t this, u8 address, u8 value)
{
  struct i2c_client *i2c = 0;
  char buffer[2];
  int returnValue = 0;
  buffer[0] = address;
  buffer[1] = value;
  returnValue = -ENOMEM;
  if (this && this->bus) {
    i2c = this->bus;
    returnValue = i2c_master_send(i2c,buffer,2);
  }
  return returnValue;
}

/*! \fn static int read_register(psx86XX_t this, u8 address, u8 *value) 
* \brief Reads a register's value from the device
* \param this Pointer to main parent struct 
* \param address 8-Bit address to read from
* \param value Pointer to 8-bit value to save register value to 
* \return Value from i2c_smbus_read_byte_data if < 0. else 0
*/
static int read_register(psx86XX_t this, u8 address, u8 *value)
{
  struct i2c_client *i2c = 0;
  s32 returnValue = 0;
  if (this && value && this->bus) {
    i2c = this->bus;
    returnValue = i2c_smbus_read_byte_data(i2c,address);
    if (returnValue >= 0) {
      *value = returnValue;
      return 0;
    } else {
      return returnValue;
    }
  }
  return -ENOMEM;
}
/*! \brief Sends a write register range to the device
 * \param this Pointer to main parent struct 
 * \param reg 8-bit register address (base address)
 * \param data pointer to 8-bit register values
 * \param size size of the data pointer
 * \return Value from i2c_master_send
 */
/*static int write_registerEx(psx86XX_t this, unsigned char reg,
				unsigned char *data, int size)
{
  struct i2c_client *i2c = 0;
	u8 tx[MAX_WRITE_ARRAY_SIZE];
	int ret = 0;

  if (this && (i2c = this->bus) && data && (size <= MAX_WRITE_ARRAY_SIZE))
  {
    dev_dbg(this->pdev, "inside write_registerEx()\n");
    tx[0] = reg;
    dev_dbg(this->pdev, "going to call i2c_master_send(0x%p, 0x%x ",
            (void *)i2c,tx[0]);
    for (ret = 0; ret < size; ret++)
    {
      tx[ret+1] = data[ret];
      dev_dbg(this->pdev, "0x%x, ",tx[ret+1]);
    }
    dev_dbg(this->pdev, "\n");

    ret = i2c_master_send(i2c, tx, size+1 );
	  if (ret < 0)
	  	dev_err(this->pdev, "I2C write error\n");
  }
  dev_dbg(this->pdev, "leaving write_registerEx()\n");


	return ret;
}*/
/*! \brief Reads a group of registers from the device
* \param this Pointer to main parent struct 
* \param reg 8-Bit address to read from (base address)
* \param data Pointer to 8-bit value array to save registers to 
* \param size size of array
* \return Value from i2c_smbus_read_byte_data if < 0. else 0
*/
/*static int read_registerEx(psx86XX_t this, unsigned char reg,
				unsigned char *data, int size)
{
  struct i2c_client *i2c = 0;
	int ret = 0;
	u8 tx[] = {
		reg
	};
  if (this && (i2c = this->bus) && data && (size <= MAX_WRITE_ARRAY_SIZE))
  {
    dev_dbg(this->pdev, "inside read_registerEx()\n");
    dev_dbg(this->pdev,
        "going to call i2c_master_send(0x%p,0x%p,1) Reg: 0x%x\n",
                                                               (void *)i2c,(void *)tx,tx[0]);
  	ret = i2c_master_send(i2c,tx,1);
  	if (ret >= 0) {
      dev_dbg(this->pdev, "going to call i2c_master_recv(0x%p,0x%p,%x)\n",
                                                              (void *)i2c,(void *)data,size);
  		ret = i2c_master_recv(i2c, data, size);
    }
  }
	if (unlikely(ret < 0))
		dev_err(this->pdev, "I2C read error\n");
  dev_dbg(this->pdev, "leaving read_registerEx()\n");
	return ret;
}*/
/*********************************************************************/
/*! \brief Perform a manual offset calibration
* \param this Pointer to main parent struct 
* \return Value return value from the write register
 */

static struct class *sx9310_class;
struct device *sx9310_dev;


static int manual_offset_calibration(psx86XX_t this)
{
  s32 returnValue = 0;
  returnValue = write_register(this,SX9310_IRQSTAT_REG,0xFF);
  return returnValue;
}
/*! \brief sysfs show function for manual calibration which currently just
 * returns register value.
 */
static ssize_t manual_offset_calibration_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
  u8 reg_value = 0;
	psx86XX_t this = dev_get_drvdata(dev);

  dev_dbg(this->pdev, "Reading IRQSTAT_REG\n");
  read_register(this,SX9310_IRQSTAT_REG,&reg_value);
	return sprintf(buf, "%d\n", reg_value);
}

/*! \brief sysfs store function for manual calibration
 */
static ssize_t manual_offset_calibration_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	psx86XX_t this = dev_get_drvdata(dev);
	unsigned long val;
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;
  if (val) {
    dev_info( this->pdev, "Performing manual_offset_calibration()\n");
    manual_offset_calibration(this);
  }
	return count;
}


static ssize_t cstest_value_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
  u8 reg_value = 0;
  int ret = 0;
  int result = 0;
  psx86XX_t this = sx86xx_info;

  ret = read_register(this,SX9310_STAT0_REG,&reg_value);
  pr_info("[sx9310]cstest -- reg_value=%x\n",reg_value);
  if(ret < 0){
  	result = 9; 
	return sprintf(buf, "%d\n", result);
  }else{
	if( (reg_value & SX9310_TCHCMPSTAT_TCHSTAT2_FLAG) == SX9310_TCHCMPSTAT_TCHSTAT2_FLAG ){
		result = 1; 
		return sprintf(buf, "%d\n", result);
	//}else if( (reg_value & SX9310_TCHCMPSTAT_TCHSTAT0_FLAG) == SX9310_TCHCMPSTAT_TCHSTAT0_FLAG ){
	//	result = 1; 
	//	return sprintf(buf, "%d\n", result);
	}else {
		result = 0; 
		return sprintf(buf, "%d\n", result);
	}	
  }
}


static DEVICE_ATTR(calibrate, 0664, manual_offset_calibration_show,
                                manual_offset_calibration_store);
static DEVICE_ATTR(cstest, 0444, cstest_value_show,NULL);

static struct attribute *sx9310_attributes[] = {
	&dev_attr_calibrate.attr,
	&dev_attr_cstest.attr,
	NULL,
};
static struct attribute_group sx9310_attr_group = {
	.attrs = sx9310_attributes,
};
/*********************************************************************/
#ifdef ASUS_FACTORY_BUILD
static int SAR_sensor_proc_show(struct seq_file *m, void *v) {
	 uint8_t data = 0;
	 int ret;
	 psx86XX_t this = sx86xx_info;
	 ret = read_register(this,SX9310_Who_Am_I,&data);
     if(ret<0){
		seq_printf(m,"0 \n");
	 }else{
	 	if(data == 0x01){
			pr_info("[SX9310] Read Who AM I reg is 0x%x\n",data);
			seq_printf(m,"1 \n");
		}else{
			pr_info("[SX9310] Read Who AM I reg is 0x%x not 0x1\n",data);
			seq_printf(m,"0 \n");
		}

        }
        return ret;
}

static int SAR_sensor_proc_open(struct inode *inode, struct  file *file) {
     return single_open(file, SAR_sensor_proc_show, NULL);
}

static const struct file_operations SAR_sensor_proc_fops = {
   .owner = THIS_MODULE,
   .open = SAR_sensor_proc_open,
   .read = seq_read,
   .llseek = seq_lseek,
   .release = single_release,
};


int create_SAR_Sensor_status_entry( void )
{
   sarsensor_entry = proc_create("SAR_Sensor_status", S_IRUGO, NULL,&SAR_sensor_proc_fops);
    if (!sarsensor_entry)
        return -ENOMEM;
    	return 0;
}
#endif

/*! \fn static int read_regStat(psx86XX_t this)
 * \brief Shortcut to read what caused interrupt.
 * \details This is to keep the drivers a unified
 * function that will read whatever register(s) 
 * provide information on why the interrupt was caused.
 * \param this Pointer to main parent struct 
 * \return If successful, Value of bit(s) that cause interrupt, else 0
 */
static int read_regStat(psx86XX_t this)
{
  u8 data = 0;
  if (this) {
    if (read_register(this,SX9310_IRQSTAT_REG,&data) == 0)
      return (data & 0x00FF);
  }
  return 0;
}

/*! \brief  Initialize I2C config from platform data
 * \param this Pointer to main parent struct 
 */
 //#ifdef xxx23
static void hw_init(psx86XX_t this)
{
  psx9310_t pDevice = 0;
  psx9310_platform_data_t pdata = 0;
	int i = 0;
	/* configure device */
  dev_dbg(this->pdev, "Going to Setup I2C Registers\n");
  if (this && (pDevice = this->pDevice) && (pdata = pDevice->hw))
  {
    while ( i < pdata->i2c_reg_num) {
      /* Write all registers/values contained in i2c_reg */
      dev_dbg(this->pdev, "Going to Write Reg: 0x%x Value: 0x%x\n",
                pdata->pi2c_reg[i].reg,pdata->pi2c_reg[i].val);
//      msleep(3);        
      write_register(this, pdata->pi2c_reg[i].reg,pdata->pi2c_reg[i].val);
      i++;
    }
  } else {
  dev_err(this->pdev, "ERROR! platform data 0x%p\n",pDevice->hw);
  }
}
/*********************************************************************/




/*! \fn static int initialize(psx86XX_t this)
 * \brief Performs all initialization needed to configure the device
 * \param this Pointer to main parent struct 
 * \return Last used command's return value (negative if error)
 */

static int initialize(psx86XX_t this)
{
  int result;
  dev_info(this->pdev, "enter initialize\n");
  if (this) {
    /* prepare reset by disabling any irq handling */
    this->irq_disabled = 1;
    disable_irq(this->irq);
    /* perform a reset */
    write_register(this,SX9310_SOFTRESET_REG,SX9310_SOFTRESET);
    /* wait until the reset has finished by monitoring NIRQ */
    dev_info(this->pdev, "Sent Software Reset. Waiting until device is back from reset to continue.\n");
    /* just sleep for awhile instead of using a loop with reading irq status */
    //msleep(300);
//    while(this->get_nirq_low && this->get_nirq_low()) { read_regStat(this); }
    dev_info(this->pdev, "Device is back from the reset, continuing. NIRQ = %d\n",this->get_nirq_low());
    hw_init(this);
    msleep(300); /* make sure everything is running */
    manual_offset_calibration(this);
    
    /* re-enable interrupt handling */
    enable_irq(this->irq);
    this->irq_disabled = 0;
    msleep(200);
	//read_regStat(this);
	gpio_init(this);
    //result = gpio_get_value(GPIO_SX9310_NIRQ);
	//dev_err(this->pdev,"gpio value =%d\n",result);
    /* make sure no interrupts are pending since enabling irq will only
     * work on next falling edge */
    result = read_regStat(this);
    dev_info(this->pdev, "Exiting initialize(). NIRQ = %d,result=0x%x\n",this->get_nirq_low(),result);
    return 0;
  }
  return -ENOMEM;
}
//#endif


/*! 
 * \brief Handle what to do when a touch occurs
 * \param this Pointer to main parent struct 
 */
static void touchProcess(psx86XX_t this)
{
  int counter = 0;
  u8 i = 0;
  int numberOfButtons = 0;
  psx9310_t pDevice = NULL;
  struct _buttonInfo *buttons = NULL;
  struct input_dev *input = NULL;
  
  struct _buttonInfo *pCurrentButton  = NULL;


  if (this && (pDevice = this->pDevice))
  {
    dev_dbg(this->pdev, "Inside touchProcess()\n");
    read_register(this, SX9310_STAT0_REG, &i);

    buttons = pDevice->pbuttonInformation->buttons;
    input = pDevice->pbuttonInformation->input;
    numberOfButtons = pDevice->pbuttonInformation->buttonSize;
    
    if (unlikely( (buttons==NULL) || (input==NULL) )) {
      dev_err(this->pdev, "ERROR!! buttons or input NULL!!!\n");
      return;
    }

    for (counter = 0; counter < numberOfButtons; counter++) {
      pCurrentButton = &buttons[counter];
      if (pCurrentButton==NULL) {
        dev_err(this->pdev,"ERROR!! current button at index: %d NULL!!!\n",
                                                                      counter);
        return; // ERRORR!!!!
      }
      switch (pCurrentButton->state) {
        case IDLE: /* Button is not being touched! */
          if (((i & pCurrentButton->mask) == pCurrentButton->mask)) {
            /* User pressed button */
            dev_info(this->pdev, "cap button %d touched\n", counter);
            input_report_key(input, pCurrentButton->keycode, 1);
            pCurrentButton->state = ACTIVE;
          } else {
            dev_dbg(this->pdev, "Button %d already released.\n",counter);
          }
          break;
        case ACTIVE: /* Button is being touched! */ 
          if (((i & pCurrentButton->mask) != pCurrentButton->mask)) {
            /* User released button */
            dev_info(this->pdev, "cap button %d released\n",counter);
            input_report_key(input, pCurrentButton->keycode, 0);
            pCurrentButton->state = IDLE;
          } else {
            dev_dbg(this->pdev, "Button %d still touched.\n",counter);
          }
          break;
        default: /* Shouldn't be here, device only allowed ACTIVE or IDLE */
          break;
      };
    }
    input_sync(input);

	  dev_dbg(this->pdev, "Leaving touchProcess()\n");
  }
}

/*static int sx9310_set_power(struct psx9310_t *this, bool on  )
{
	
}*/
/*! \fn static int sx9310_probe(struct i2c_client *client, const struct i2c_device_id *id)
 * \brief Probe function
 * \param client pointer to i2c_client
 * \param id pointer to i2c_device_id
 * \return Whether probe was successful
 */
static int sx9310_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
  int i = 0;
  int ret = 0;
  psx86XX_t this = 0;
  psx9310_t pDevice = 0;
  psx9310_platform_data_t pplatData = 0;
  struct input_dev *input = NULL;

  dev_info(&client->dev, "sx9310_probe()\n");

#ifndef CONFIG_OF
  pplatData = client->dev.platform_data;
	if (!pplatData) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}
#else
  pplatData = kzalloc(sizeof(psx9310_platform_data_t), GFP_KERNEL); /* create memory for main struct */
  if(!pplatData)
  	return -ENOMEM;
  pplatData ->get_is_nirq_low = sx9310_get_nirq_state;
  pplatData ->init_platform_hw = NULL;
  pplatData ->exit_platform_hw = NULL;
  pplatData ->pi2c_reg = sx9310_i2c_reg_setup;
  pplatData ->i2c_reg_num = ARRAY_SIZE(sx9310_i2c_reg_setup);
  pplatData ->pbuttonInformation = &smtcButtonInformation;
#endif

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;

  this = kzalloc(sizeof(sx86XX_t), GFP_KERNEL); /* create memory for main struct */
  dev_dbg(&client->dev, "\t Initialized Main Memory: 0x%p\n",this);
  //sx9310_set_power(this,true);
  if (this)
  {
    /* In case we need to reinitialize data 
     * (e.q. if suspend reset device) */
    this->init = initialize;
    /* shortcut to read status of interrupt */
    this->refreshStatus = read_regStat;
    /* pointer to function from platform data to get pendown 
     * (1->NIRQ=0, 0->NIRQ=1) */
    this->get_nirq_low = pplatData->get_is_nirq_low;
    /* save irq in case we need to reference it */
    this->irq = client->irq;
	dev_info(&client->dev, "sx9310_probe,client->irq=%d \n",client->irq);
    /* do we need to create an irq timer after interrupt ? */
    this->useIrqTimer = 0;
    /* Setup function to call on corresponding reg irq source bit */
    if (MAX_NUM_STATUS_BITS>= 8)
    {
      this->statusFunc[0] = 0; /* TXEN_STAT */
      this->statusFunc[1] = 0; /* UNUSED */
      this->statusFunc[2] = 0; /* UNUSED */
      this->statusFunc[3] = 0; /* CONV_STAT */
      this->statusFunc[4] = 0; /* COMP_STAT */
      this->statusFunc[5] = touchProcess; /* RELEASE_STAT */
      this->statusFunc[6] = touchProcess; /* TOUCH_STAT  */
      this->statusFunc[7] = 0; /* RESET_STAT */
    }

    /* setup i2c communication */
	this->bus = client;
	i2c_set_clientdata(client, this);

    /* record device struct */
    this->pdev = &client->dev;
	
	ret =sx9310_gpio_parse_dt(this);
	if(ret<0){
		dev_err(&client->dev,"sx9310 gpio parse error, use default gpio!\n");
		this->intr_pin = GPIO_SX9310_NIRQ;
	} 
	
    /* create memory for device specific struct */
    this->pDevice = pDevice = kzalloc(sizeof(sx9310_t), GFP_KERNEL);
	dev_dbg(&client->dev, "\t Initialized Device Specific Memory: 0x%p\n",pDevice);

    if (pDevice)
    {
      /* for accessing items in user data (e.g. calibrate) */
      ret = sysfs_create_group(&client->dev.kobj, &sx9310_attr_group);
	  if (ret){
		 dev_info(&client->dev, "create sysfs group error!\n");
	  }
	  
      /* Check if we hava a platform initialization function to call*/
      if (pplatData->init_platform_hw)
        pplatData->init_platform_hw();

      /* Add Pointer to main platform data struct */
      pDevice->hw = pplatData;
      
      /* Initialize the button information initialized with keycodes */
      pDevice->pbuttonInformation = pplatData->pbuttonInformation;

      /* Create the input device */
      input = input_allocate_device();
      if (!input) {
        return -ENOMEM;
      }
      
      /* Set all the keycodes */
      __set_bit(EV_KEY, input->evbit);
      for (i = 0; i < pDevice->pbuttonInformation->buttonSize; i++) {
        __set_bit(pDevice->pbuttonInformation->buttons[i].keycode, 
                                                        input->keybit);
        pDevice->pbuttonInformation->buttons[i].state = IDLE;
      }
      /* save the input pointer and finish initialization */
      pDevice->pbuttonInformation->input = input;
      input->name = "SX9310 Cap Touch";
      input->id.bustype = BUS_I2C;
//      input->id.product = sx863x->product;
//      input->id.version = sx863x->version;
      if(input_register_device(input))
        return -ENOMEM;
  
    }
        sx86xx_info = this;
#ifdef ASUS_FACTORY_BUILD
	ret = create_SAR_Sensor_status_entry( );
    if(ret) pr_err("[%s] : ERROR to create SAR proc entry\n",__func__);
#endif
    sx86XX_init(this);
    sx9310_class = class_create(THIS_MODULE, "sx9310_sensors");
    if (IS_ERR(sx9310_class)) {
	ret = PTR_ERR(sx9310_class);
	pr_err("[sx9310] Create Sx9310 class fail!\n");
	return 0;
    };

    sx9310_dev = device_create(sx9310_class,NULL, 0, "%s", "capsensor");
    if (unlikely(IS_ERR(sx9310_dev))) {
	ret = PTR_ERR(sx9310_dev);	
    };	
    ret = device_create_file(sx9310_dev, &dev_attr_cstest);
    if (ret)
	pr_err("[sx9310] Create cstest sysfile error!\n");
    return  0;
  }
  return -1;
}

/*! \fn static int sx9310_remove(struct i2c_client *client)
 * \brief Called when device is to be removed
 * \param client Pointer to i2c_client struct
 * \return Value from sx86XX_remove()
 */
static int sx9310_remove(struct i2c_client *client)
{
	psx9310_platform_data_t pplatData =0;
  psx9310_t pDevice = 0;
	psx86XX_t this = i2c_get_clientdata(client);
  if (this && (pDevice = this->pDevice))
  {
    input_unregister_device(pDevice->pbuttonInformation->input);

    sysfs_remove_group(&client->dev.kobj, &sx9310_attr_group);
#ifndef  CONFIG_OF
	pplatData = client->dev.platform_data;
    if (pplatData && pplatData->exit_platform_hw)
      pplatData->exit_platform_hw();
    kfree(this->pDevice);
#else
	pplatData = pDevice->hw;
    if(pplatData){
		kfree(pplatData);	
	}
    kfree(this->pDevice);
#endif
  }
	return sx86XX_remove(this);
}

#if defined(USE_KERNEL_SUSPEND)
/*====================================================*/
/***** Kernel Suspend *****/
static int sx9310_suspend(struct i2c_client *client)
{
  psx86XX_t this = i2c_get_clientdata(client);
  sx86XX_suspend(this);
 return 0;
}
/***** Kernel Resume *****/
static int sx9310_resume(struct i2c_client *client)
{
  psx86XX_t this = i2c_get_clientdata(client);
  sx86XX_resume(this);
  return 0;
}
#endif
/*====================================================*/
static struct i2c_device_id sx9310_idtable[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

#ifdef CONFIG_OF
static struct of_device_id cm36656_match_table[] = {
	{ .compatible = "semtech,sx9310",},
	{ },
};
#else
#define cm36656_match_table NULL
#endif


MODULE_DEVICE_TABLE(i2c, sx9310_idtable);
static struct i2c_driver sx9310_driver = {
	.driver = {
		.owner  = THIS_MODULE,
		.name   = DRIVER_NAME,
		.of_match_table = cm36656_match_table,
	},
	.id_table = sx9310_idtable,
	.probe	  = sx9310_probe,
	.remove	  = sx9310_remove,
#if defined(USE_KERNEL_SUSPEND)
  .suspend  = sx9310_suspend,
  .resume   = sx9310_resume,
#endif
};
static int __init sx9310_init(void)
{
	return i2c_add_driver(&sx9310_driver);
}
static void __exit sx9310_exit(void)
{
#ifdef ASUS_FACTORY_BUILD
	if(sarsensor_entry)
		remove_proc_entry("SAR_Sensor_status", NULL);
#endif
	i2c_del_driver(&sx9310_driver);
}

module_init(sx9310_init);
module_exit(sx9310_exit);

MODULE_AUTHOR("Semtech Corp. (http://www.semtech.com/)");
MODULE_DESCRIPTION("SX9310 Capacitive Touch Controller Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
