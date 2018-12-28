/* include/linux/CM36656.h
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

#ifndef __LINUX_CM36656_H
#define __LINUX_CM36656_H

#define CM36656_I2C_NAME "cm36656"

/* Define Slave Address*/
#define	CM36656_slave_add	0xC0>>1


/*Define Command Code*/
#define		CS_CONF		    0x00
#define		CS_THDH  	    0x01
#define		CS_THDL	      0x02
#define		PS_CONF1      0x03
#define		PS_CONF3      0x04

#define		PS_THDL       0x05
#define		PS_THDH       0x06
#define		PS_CANC       0x07

#define		CS_R_DATA     0xF0
#define		CS_G_DATA     0xF1
#define		CS_B_DATA     0xF2
#define		CS_IR_DATA    0xF3
#define		PS_DATA       0xF4

#define		INT_FLAG  		0xF5
#define		ID_REG    		0xF6

/*cm36656*/
/*for CS CONF command*/
#define CM36656_CS_START 	(1 << 7)
#define CM36656_CS_START_MASK 0xFF7F

#define CM36656_CS_IT_50MS 	(0 << 2)
#define CM36656_CS_IT_100MS 	(1 << 2)
#define CM36656_CS_IT_200MS 	(2 << 2)
#define CM36656_CS_IT_400MS 	(3 << 2)
#define CM36656_CS_IT_MASK     0xFFF3//asus alex wang rgb porting
#define CM36656_CS_PERS_1 		(0 << 10)
#define CM36656_CS_PERS_2 		(1 << 10)
#define CM36656_CS_PERS_4 		(2 << 10)
#define CM36656_CS_PERS_8 		(3 << 10)
#define CM36656_CS_INT_EN	 	(1 << 8) /*enable/disable Interrupt*/
#define CM36656_CS_INT_MASK	0xFEFF
#define CM36656_CS_SD			  (1 << 0) /*enable/disable cs func, 1:disable , 0: enable*/
#define CM36656_CS_SD_MASK		0xFFFE

//asus alex wang support ALS CONF command++++
#define CM36656_ALS_INT_EN        (1 << 8) /*enable/disable als Interrupt 0:disable 1:enable */
#define CM36656_ALS_INT_EN_MASK   0xFEFF
#define CM36656_ALS_INT_SEL        (1 << 9) //set source  1: CS_IR 0:GEERN
#define CM36656_ALS_INT_SEL_MASK 0xFDFF
#define CM36656_ALS_PERS_1 		(0 << 10)
#define CM36656_ALS_PERS_2 		(1 << 10)
#define CM36656_ALS_PERS_4 		(2 << 10)
#define CM36656_ALS_PERS_8 		(3 << 10)
#define CM36656_ALS_HS 		        (1 << 12) //set ALS sensitivity     1: *2 0:*1 
#define CM36656_ALS_HS_MASK      0xEFFF
#define CM36656_ALS_GAIN	        (1 << 12) //set ALS sensitivity    1:/8   0:*1
#define CM36656_ALS_GAIN_MASK  0xDFFF
#define CM36656_ALS_SOURCE_GREEN       0
#define CM36656_ALS_SOURCE_IR       1
//asus alex wang support ALS CONF command-----

/*for PS CONF1 command*/
#define CM36656_PS_START 	(1 << 11)
//asus alex wang Psensor porting+++++
#define CM36656_PS_START_MASK 0xF7FF
#define CM36656_PS_PERIOD_8      (0<< 6)
#define CM36656_PS_PERIOD_16      (1<< 6)
#define CM36656_PS_PERIOD_32      (2<< 6)
#define CM36656_PS_PERIOD_64      (3<< 6)
#define CM36656_PS_MPS_1              (0<<12)
#define CM36656_PS_MPS_2              (1<<12)
#define CM36656_PS_MPS_4              (2<<12)
#define CM36656_PS_MPS_8              (3<<12)
//asus alex wang Psensor porting-----
//#define CM36656_PS_INT_ENABLE	       (2 << 2) /*enable/disable Interrupt*/

#define CM36656_PS_INT_OFF	       (0 << 2) /*enable/disable Interrupt*/

//#define CM36656_PS_INT_IN          (1 << 2)
//#define CM36656_PS_INT_OUT         (2 << 2)
#define CM36656_PS_INT_IN_AND_OUT  (2 << 2)/*enable / disable PS Interrupt 2:enable 0:disable 3: disable*/
#define CM36656_PS_INT_OFF_2	       (3 << 2) 
#define CM36656_PS_INT_LOGIC        (1 << 2)
#define CM36656_PS_INT_MASK        0xFFF3

#define CM36656_PS_PERS_1 	   (0 << 4)
#define CM36656_PS_PERS_2 	   (1 << 4)
#define CM36656_PS_PERS_3 	   (2 << 4)
#define CM36656_PS_PERS_4 	   (3 << 4)
#define CM36656_PS_IT_1T 	     (0 << 14)
#define CM36656_PS_IT_2T 	     (1 << 14)
#define CM36656_PS_IT_4T 	     (2 << 14)
#define CM36656_PS_IT_8T 	     (3 << 14)
#define CM36656_PS_SMART_PERS  (1 << 1)/*enable / disable Smart Persistence 1:enable,0:disable*/
#define CM36656_PS_SMART_PERS_MASK 0xFFFD
#define CM36656_PS_SD	         (1 << 0)/*enable/disable PS func, 1:disable , 0: enable*/
#define CM36656_PS_SD_MASK     0xFFFE

/*for PS CONF3 command*/
#define CM36656_LED_I_70              (0 << 8)
#define CM36656_LED_I_95              (1 << 8)
#define CM36656_LED_I_110             (2 << 8)
#define CM36656_LED_I_130             (3 << 8)
#define CM36656_LED_I_170             (4 << 8)
#define CM36656_LED_I_200             (5 << 8)
#define CM36656_LED_I_220             (6 << 8)
#define CM36656_LED_I_240             (7 << 8)
#define CM36656_PS_ACTIVE_FORCE_MODE  (1 << 6)
#define CM36656_PS_ACTIVE_FORCE_TRIG  (1 << 5)
#define CM36656_PS_RESERVED_BIT_1     (1 << 3)
//asus alex wang Psensor porting+++++
#define CM36656_PS_AF_AUTO              (0<<6)
#define CM36656_PS_AF_FORCE             (1<<6)
#define CM36656_PS_FORCENUM_ONE  (0<<4)
#define CM36656_PS_FORCENUM_TWO  (1<<4)
#define CM36656_PS_MUST_SETTING     (1<<3)
//asus alex wang Psensor porting-----

/*for INT FLAG*/
#define INT_FLAG_PS_SPFLAG           (1 << 14)
#define INT_FLAG_ALS_IF_L            (1 << 11)//alex wang porting+++
#define INT_FLAG_ALS_IF_H            (1 << 10)//alex wang porting+++
#define INT_FLAG_PS_IF_CLOSE         (1 << 9)
#define INT_FLAG_PS_IF_AWAY          (1 << 8)  

extern unsigned int ps_kparam1;
extern unsigned int ps_kparam2;

struct cm36656_platform_data {
	int intr;
	int (*power)(int, uint8_t); /* power to the chip */
	uint8_t slave_addr;
	uint16_t ps_close_thd_set;
	uint16_t ps_away_thd_set;	
	uint16_t ls_cmd;
	uint16_t ps_conf1_val;
	uint16_t ps_conf3_val;	
};
//alex wang rgb ++++
#define ASUS_RGB_SENSOR_DATA_SIZE	5
#define ASUS_RGB_SENSOR_NAME_SIZE	32
#define ASUS_RGB_SENSOR_IOC_MAGIC                      ('C')		///< RGB sensor ioctl magic number 
#define ASUS_RGB_SENSOR_IOCTL_DATA_READ           	 _IOR(ASUS_RGB_SENSOR_IOC_MAGIC, 1, int[ASUS_RGB_SENSOR_DATA_SIZE])	///< RGB sensor ioctl command - Read data RGBW
#define ASUS_RGB_SENSOR_IOCTL_IT_SET          		 _IOW(ASUS_RGB_SENSOR_IOC_MAGIC, 2, int)	///< RGB sensor ioctl command - Set integration time
#define ASUS_RGB_SENSOR_IOCTL_DEBUG_MODE           _IOW(ASUS_RGB_SENSOR_IOC_MAGIC, 3, int)	///< RGB sensor ioctl command - Get debug mode
#define ASUS_RGB_SENSOR_IOCTL_MODULE_NAME         _IOR(ASUS_RGB_SENSOR_IOC_MAGIC, 4, char[ASUS_RGB_SENSOR_NAME_SIZE])	///< RGB sensor ioctl command - Get module name
#define ASUS_RGB_SENSOR_IOCTL_GET_ENABLED		 _IOR(ASUS_RGB_SENSOR_IOC_MAGIC, 5, int *)
#define ASUS_RGB_SENSOR_IOCTL_ENABLE 			 _IOW(ASUS_RGB_SENSOR_IOC_MAGIC, 7, int)
//alex wang rgb -----

#define LBUFF_SIZE						16	/* Rx buffer size */
#define CAMERA_LIGHTSENSOR_IOCTL_GET_ENABLED			_IOR(LIGHTSENSOR_IOCTL_MAGIC, 8, int *)
#define CAMERA_LIGHTSENSOR_IOCTL_ENABLE				_IOW(LIGHTSENSOR_IOCTL_MAGIC, 9, int)
#define ASUS_LIGHTSENSOR_IOCTL_GETLUX				_IOR(LIGHTSENSOR_IOCTL_MAGIC, 10, char[LBUFF_SIZE+1])

#endif
