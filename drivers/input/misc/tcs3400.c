/*
 * Device driver for monitoring ambient light intensity in (lux)
 * proximity detection (prox), and Beam functionality within the
 * AMS-TAOS TCS family of devices.
 *
 * Copyright (c) 2016, AMS-TAOS USA, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/tcs3400.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>//asus alex_wang setting  chip power

#define TCS3400_CMD_ALS_INT_CLR  0xE6
#define TCS3400_CMD_ALL_INT_CLR	0xE7

#define INTEGRATION_CYCLE 278

#define I2C_ADDR_OFFSET	0X80

//asus alex_wang setting  chip power +++++
#define TCS3400_VIO_TYP_UV	1800000
#define TCS3400_VDD_TYP_UV	3000000
//asus alex_wang setting  chip power-----

enum tcs3400_regs {
	TCS3400_CONTROL,
	TCS3400_ALS_TIME,                  // 0x81
	TCS3400_RESV_1,
	TCS3400_WAIT_TIME,               // 0x83
	TCS3400_ALS_MINTHRESHLO,   // 0x84
	TCS3400_ALS_MINTHRESHHI,   // 0x85
	TCS3400_ALS_MAXTHRESHLO,  // 0x86
	TCS3400_ALS_MAXTHRESHHI,  // 0x87
	TCS3400_RESV_2,                     // 0x88
	TCS3400_PRX_MINTHRESHLO,  // 0x89 -> Not used for TCS3400 

	TCS3400_RESV_3,                    // 0x8A 
	TCS3400_PRX_MAXTHRESHHI, // 0x8B  -> Not used for TCS3400 
	TCS3400_PERSISTENCE,          // 0x8C   
	TCS3400_CONFIG,                    // 0x8D
	TCS3400_PRX_PULSE_COUNT,  // 0x8E  -> Not used for TCS3400
	TCS3400_GAIN,                        // 0x8F  : Gain Control Register  
	TCS3400_AUX,                          // 0x90  
	TCS3400_REVID,
	TCS3400_CHIPID,
	TCS3400_STATUS,                    // 0x93

	TCS3400_CLR_CHANLO,            // 0x94
	TCS3400_CLR_CHANHI,            // 0x95
	TCS3400_RED_CHANLO,           // 0x96
	TCS3400_RED_CHANHI,           // 0x97
	TCS3400_GRN_CHANLO,           // 0x98
	TCS3400_GRN_CHANHI,           // 0x99 
	TCS3400_BLU_CHANLO,           // 0x9A
	TCS3400_BLU_CHANHI,           // 0x9B
	TCS3400_PRX_HI,                    // 0x9C
	TCS3400_PRX_LO,                    // 0x9D

	TCS3400_PRX_OFFSET,            // 0x9E
	TCS3400_RESV_4,                    // 0x9F
	TCS3400_IRBEAM_CFG,            // 0xA0  
	TCS3400_IRBEAM_CARR,          // 0xA1   
	TCS3400_IRBEAM_NS,              // 0xA2 
	TCS3400_IRBEAM_ISD,            // 0xA3 
	TCS3400_IRBEAM_NP,              // 0xA4
	TCS3400_IRBEAM_IPD,            // 0xA5
	TCS3400_IRBEAM_DIV,            // 0xA6
	TCS3400_IRBEAM_LEN,            // 0xA7 

	TCS3400_IRBEAM_STAT,         // 0xA8
	TCS3400_REG_MAX,

};

enum tcs3400_en_reg {
	TCS3400_EN_PWR_ON   = (1 << 0),
	TCS3400_EN_ALS      = (1 << 1),
	TCS3400_EN_PRX      = (1 << 2),
	TCS3400_EN_WAIT     = (1 << 3),
	TCS3400_EN_ALS_IRQ  = (1 << 4),
	TCS3400_EN_PRX_IRQ  = (1 << 5),
	TCS3400_EN_IRQ_PWRDN = (1 << 6),
	TCS3400_EN_BEAM     = (1 << 7),
};

enum tcs3400_status {
	TCS3400_ST_ALS_VALID  = (1 << 0),
	TCS3400_ST_PRX_VALID  = (1 << 1),
	TCS3400_ST_BEAM_IRQ   = (1 << 3),
	TCS3400_ST_ALS_IRQ    = (1 << 4),
	TCS3400_ST_PRX_IRQ    = (1 << 5),
	TCS3400_ST_PRX_SAT    = (1 << 6),
};

enum {
	TCS3400_ALS_GAIN_MASK = (3 << 0),
	TCS3400_PRX_GAIN_MASK = (3 << 2),
	TCS3400_ALS_AGL_MASK  = (1 << 2),
	TCS3400_ALS_AGL_SHIFT = 2,
	TCS3400_ATIME_PER_100 = 273,
	TCS3400_ATIME_DEFAULT_MS = 50,
	SCALE_SHIFT = 11,
	RATIO_SHIFT = 10,
	MAX_ALS_VALUE = 0xffff,
	MIN_ALS_VALUE = 10,
	GAIN_SWITCH_LEVEL = 100,
	GAIN_AUTO_INIT_VALUE = AGAIN_16,
};

static u8 const tcs3400_ids[] = {
	0x90,		//tcs34001&tcs34005
	0x93,		//tcs34003&tcs34007
};

static char const *tcs3400_names[] = {
	"tcs34001",
	"tcs34003",
};

static u8 const restorable_regs[] = {
	TCS3400_ALS_TIME,
	TCS3400_PERSISTENCE,
	TCS3400_PRX_PULSE_COUNT,
	TCS3400_GAIN,
	TCS3400_PRX_OFFSET,
};

static u8 const als_gains[] = {
	1,
	4,
	16,
	64
};

struct tcs3400_als_info {
	u32 cpl;
	u32 saturation;
	u16 clear_raw;
	u16 red_raw;
	u16 green_raw;
	u16 blue_raw;
	u16 lux;
	u16 cct;
	s16 ir;
};

static struct lux_segment segment_default[] = {
	{
		.d_factor = D_Factor1,
		.r_coef = R_Coef1,
		.g_coef = G_Coef1,
		.b_coef = B_Coef1,
		.ct_coef = CT_Coef1,
		.ct_offset = CT_Offset1,
	},
};

struct tcs3400_chip {
	struct class *tcs3400_class;
	struct device *rgb_dev;
	struct mutex lock;
	struct i2c_client *client;
	struct tcs3400_als_info als_inf;
	struct tcs3400_parameters params;
	struct tcs3400_i2c_platform_data *pdata;
	u8 shadow[42];

       struct input_dev *a_idev;
	int in_suspend;
	int wake_irq;
	int irq_pending;
	bool unpowered;
	bool als_enabled;

       struct lux_segment *segment;
	int segment_num;
	int seg_num_max;
	bool als_gain_auto;
	u8 device_index;
	u8 bc_symbol_table[128];
	u16 bc_nibbles;
	u16 hop_count;
	u8 hop_next_slot;
	u8 hop_index;

	struct workqueue_struct *chip_workqueue;
	//asus alex_wang setting  chip power+++
	struct regulator *vio;
	struct regulator *vdd;
	//asus alex_wang setting  chip power---
};

struct tcs3400_chip *chip_info;

static int tcs3400_i2c_read(struct tcs3400_chip *chip, u8 reg, u8 *val)
{
	int ret;

	s32 read;
	struct i2c_client *client = chip->client;

	reg += I2C_ADDR_OFFSET;
	ret = i2c_smbus_write_byte(client, reg);
	if (ret < 0) {
		mdelay(3);
		ret = i2c_smbus_write_byte(client, reg);
		if (ret < 0) {
			dev_err(&client->dev, "%s: failed 2x to write register %x\n",
				__func__, reg);
			return ret;
		}
	}

	read = i2c_smbus_read_byte(client);
	if (read < 0) {
		mdelay(3);
		read = i2c_smbus_read_byte(client);
		if (read < 0) {
			dev_err(&client->dev, "%s: failed read from register %x\n",
				__func__, reg);
		}
		return ret;
	}

	*val = (u8)read;
	return 0;
}

static int tcs3400_i2c_write(struct tcs3400_chip *chip, u8 reg, u8 val)
{
	int ret;
	struct i2c_client *client = chip->client;

	reg += I2C_ADDR_OFFSET;
	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		mdelay(3);
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (ret < 0) {
			dev_err(&client->dev, "%s: failed to write register %x err= %d\n",
				__func__, reg, ret);
		}
	}
	return ret;
}

static int tcs3400_i2c_reg_blk_write(struct tcs3400_chip *chip,
		u8 reg, u8 *val, int size)
{
	s32 ret;
	struct i2c_client *client = chip->client;

	reg += I2C_ADDR_OFFSET;
	ret =  i2c_smbus_write_i2c_block_data(client,
			reg, size, val);
	if (ret < 0) {
		dev_err(&client->dev, "%s: failed 2X at address %x (%d bytes)\n",
				__func__, reg, size);
	}

	return ret;
}


static int tcs3400_flush_regs(struct tcs3400_chip *chip)
{
	unsigned i;
	int rc;
	u8 reg;

	dev_info(&chip->client->dev, "%s\n", __func__);


	for (i = 0; i < ARRAY_SIZE(restorable_regs); i++) {
		reg = restorable_regs[i];
		rc = tcs3400_i2c_write(chip, reg, chip->shadow[reg]);
		if (rc) {
			dev_err(&chip->client->dev, "%s: err on reg 0x%02x\n",
					__func__, reg);
			break;
		}
	}


	return rc;
}

static int tcs3400_update_enable_reg(struct tcs3400_chip *chip)
{
	return	tcs3400_i2c_write(chip, TCS3400_CONTROL,
			chip->shadow[TCS3400_CONTROL]);
}


static void tcs3400_calc_cpl(struct tcs3400_chip *chip)
{
	u32 cpl;
	u32 sat;
	u8 atime = chip->shadow[TCS3400_ALS_TIME];

	cpl = 256 - chip->shadow[TCS3400_ALS_TIME];
	cpl *= INTEGRATION_CYCLE;
	cpl /= 100;
	cpl *= als_gains[chip->params.als_gain];

	sat = min_t(u32, MAX_ALS_VALUE, (u32)(256 - atime) << 10);
	sat = sat * 8 / 10;
	chip->als_inf.cpl = cpl;
	chip->als_inf.saturation = sat;
}

static int tcs3400_set_als_gain(struct tcs3400_chip *chip, int gain)
{
	int rc;
	u8 ctrl_reg  = chip->shadow[TCS3400_GAIN] & ~TCS3400_ALS_GAIN_MASK;

	switch (gain) {
	case 1:
		ctrl_reg |= AGAIN_1;
		break;
	case 4:
		ctrl_reg |= AGAIN_4;
		break;
	case 16:
		ctrl_reg |= AGAIN_16;
		break;
	case 64:
		ctrl_reg |= AGAIN_64;
		break;
	default:
		dev_err(&chip->client->dev, "%s: wrong als gain %d\n",
				__func__, gain);
		return -EINVAL;
	}

	rc = tcs3400_i2c_write(chip, TCS3400_GAIN, ctrl_reg);
	if (!rc) {
		chip->shadow[TCS3400_GAIN] = ctrl_reg;
		chip->params.als_gain = ctrl_reg & TCS3400_ALS_GAIN_MASK;
		dev_info(&chip->client->dev, "%s: new als gain %d\n",
				__func__, gain);
	}
	return rc;
}

static int tcs3400_set_segment_table(struct tcs3400_chip *chip,
		struct lux_segment *segment, int seg_num)
{
	int i;
	struct device *dev = &chip->client->dev;

	chip->seg_num_max = chip->pdata->segment_num ?
			chip->pdata->segment_num : ARRAY_SIZE(segment_default);

	if (!chip->segment) {
		dev_info(dev, "%s: allocating segment table\n", __func__);
		chip->segment = kzalloc(sizeof(*chip->segment) *
				chip->seg_num_max, GFP_KERNEL);
		if (!chip->segment) {
			dev_err(dev, "%s: no memory!\n", __func__);
			return -ENOMEM;
		}
	}
	if (seg_num > chip->seg_num_max) {
		dev_warn(dev, "%s: %d segment requested, %d applied\n",
				__func__, seg_num, chip->seg_num_max);
		chip->segment_num = chip->seg_num_max;
	} else {
		chip->segment_num = seg_num;
	}
	memcpy(chip->segment, segment,
			chip->segment_num * sizeof(*chip->segment));
	dev_info(dev, "%s: %d segment requested, %d applied\n", __func__,
			seg_num, chip->seg_num_max);
	for (i = 0; i < chip->segment_num; i++)
		dev_info(dev,
		"seg %d: d_factor %d, r_coef %d, g_coef %d, b_coef %d, ct_coef %d ct_offset %d\n",
		i, chip->segment[i].d_factor, chip->segment[i].r_coef,
		chip->segment[i].g_coef, chip->segment[i].b_coef,
		chip->segment[i].ct_coef, chip->segment[i].ct_offset);
	return 0;
}


static int tcs3400_get_lux(struct tcs3400_chip *chip)
{
	u32 rp1, gp1, bp1, cp1;
	u32 lux = 0;
	u32 cct;
	int ret;
	u32 sat = chip->als_inf.saturation;
	u32 sf;

	/* use time in ms get scaling factor */
	tcs3400_calc_cpl(chip);

	if (!chip->als_gain_auto) {
		if (chip->als_inf.clear_raw <= MIN_ALS_VALUE) {
			dev_info(&chip->client->dev,
				"%s: darkness\n", __func__);
			lux = 0;
			goto exit;
		} else if (chip->als_inf.clear_raw >= sat) {
			dev_info(&chip->client->dev,
				"%s: saturation, keep lux & cct\n", __func__);
			lux = chip->als_inf.lux;
			goto exit;
		}
	} else {
		u8 gain = als_gains[chip->params.als_gain];
		//u8 gain = chip->params.als_gain;
		int rc = -EIO;

		if (gain == 16 && chip->als_inf.clear_raw >= sat) {
				rc = tcs3400_set_als_gain(chip, 1);
		} else if (gain == 16 &&
				chip->als_inf.clear_raw < GAIN_SWITCH_LEVEL) {
				rc = tcs3400_set_als_gain(chip, 64);
		} else if ((gain == 64 &&
			chip->als_inf.clear_raw >= (sat - GAIN_SWITCH_LEVEL)) ||
			(gain == 1 &&
				chip->als_inf.clear_raw < GAIN_SWITCH_LEVEL)) {
				rc = tcs3400_set_als_gain(chip, 16);
		}
		if (!rc) {
			dev_info(&chip->client->dev, "%s: gain adjusted, skip\n",
					__func__);
			tcs3400_calc_cpl(chip);
			ret = -EAGAIN;
			lux = chip->als_inf.lux;
			goto exit;
		}

		if (chip->als_inf.clear_raw <= MIN_ALS_VALUE) {
			dev_info(&chip->client->dev,
					"%s: darkness\n", __func__);
			lux = 0;
			goto exit;
		} else if (chip->als_inf.clear_raw >= sat) {
			dev_info(&chip->client->dev, "%s: saturation, keep lux\n",
					__func__);
			lux = chip->als_inf.lux;
			goto exit;
		}
	}

	/* remove ir from counts*/
	rp1 = chip->als_inf.red_raw - chip->als_inf.ir;
	gp1 = chip->als_inf.green_raw - chip->als_inf.ir;
	bp1 = chip->als_inf.blue_raw - chip->als_inf.ir;
	cp1 = chip->als_inf.clear_raw - chip->als_inf.ir;

	if (!chip->als_inf.cpl) {
		dev_info(&chip->client->dev, "%s: zero cpl. Setting to 1\n",
				__func__);
		chip->als_inf.cpl = 1;
	}

	if (chip->als_inf.red_raw > chip->als_inf.ir)
		lux += chip->segment[chip->device_index].r_coef * rp1;
	else
		dev_err(&chip->client->dev, "%s: lux rp1 = %d\n",
			__func__,
			(chip->segment[chip->device_index].r_coef * rp1));

	if (chip->als_inf.green_raw > chip->als_inf.ir)
		lux += chip->segment[chip->device_index].g_coef * gp1;
	else
		dev_err(&chip->client->dev, "%s: lux gp1 = %d\n",
			__func__,
			(chip->segment[chip->device_index].g_coef * rp1));

	if (chip->als_inf.blue_raw > chip->als_inf.ir)
		lux -= chip->segment[chip->device_index].b_coef * bp1;
	else
		dev_err(&chip->client->dev, "%s: lux bp1 = %d\n",
			__func__,
			(chip->segment[chip->device_index].b_coef * rp1));

	sf = chip->als_inf.cpl;

	if (sf > 131072)
		goto error;

	lux /= sf;
	lux *= chip->segment[chip->device_index].d_factor;
	lux += 512;
	lux >>= 10;
	chip->als_inf.lux = (u16) lux;

	cct = ((chip->segment[chip->device_index].ct_coef * bp1) / rp1) +
		chip->segment[chip->device_index].ct_offset;

	chip->als_inf.cct = (u16) cct;

exit:
return 0;

error:
	dev_err(&chip->client->dev, "ERROR Scale factor = %d", sf);

return 1;

}

static int tcs3400_pltf_power_on(struct tcs3400_chip *chip)
{
	int rc = 0;
	if (chip->pdata->platform_power) {
		rc = chip->pdata->platform_power(&chip->client->dev,
			POWER_ON);
		mdelay(10);
	}
	chip->unpowered = rc != 0;
	return rc;
}

static int tcs3400_pltf_power_off(struct tcs3400_chip *chip)
{
	int rc = 0;
	if (chip->pdata->platform_power) {
		rc = chip->pdata->platform_power(&chip->client->dev,
			POWER_OFF);
		chip->unpowered = rc == 0;
	} else {
		chip->unpowered = false;
	}
	return rc;
}

static int tcs3400_irq_clr(struct tcs3400_chip *chip, u8 int2clr)
{
	int ret, ret2;

	ret = i2c_smbus_write_byte(chip->client, int2clr);
	if (ret < 0) {
		mdelay(3);
		ret2 = i2c_smbus_write_byte(chip->client, int2clr);
		if (ret2 < 0) {
			dev_err(&chip->client->dev, "%s: failed 2x, int to clr %02x\n",
					__func__, int2clr);
		}
		return ret2;
	}

	return ret;
}

static void tcs3400_get_als(struct tcs3400_chip *chip)
{
	u8 *buf = &chip->shadow[TCS3400_CLR_CHANLO];

	/* extract raw channel data */
	chip->als_inf.clear_raw = le16_to_cpup((const __le16 *)&buf[0]);
	chip->als_inf.red_raw = le16_to_cpup((const __le16 *)&buf[2]);
	chip->als_inf.green_raw = le16_to_cpup((const __le16 *)&buf[4]);
	chip->als_inf.blue_raw = le16_to_cpup((const __le16 *)&buf[6]);
	chip->als_inf.ir =
		(chip->als_inf.red_raw + chip->als_inf.green_raw +
		chip->als_inf.blue_raw - chip->als_inf.clear_raw + 1) >> 1;
	if (chip->als_inf.ir < 0)
		chip->als_inf.ir = 0;
}

static int tcs3400_read_all(struct tcs3400_chip *chip)
{
	int ret = 0;

	tcs3400_i2c_read(chip, TCS3400_STATUS,
			&chip->shadow[TCS3400_STATUS]);

	tcs3400_i2c_read(chip, TCS3400_CLR_CHANLO,
			&chip->shadow[TCS3400_CLR_CHANLO]);
	tcs3400_i2c_read(chip, TCS3400_CLR_CHANHI,
			&chip->shadow[TCS3400_CLR_CHANHI]);

	tcs3400_i2c_read(chip, TCS3400_RED_CHANLO,
			&chip->shadow[TCS3400_RED_CHANLO]);
	tcs3400_i2c_read(chip, TCS3400_RED_CHANHI,
			&chip->shadow[TCS3400_RED_CHANHI]);

	tcs3400_i2c_read(chip, TCS3400_GRN_CHANLO,
			&chip->shadow[TCS3400_GRN_CHANLO]);
	tcs3400_i2c_read(chip, TCS3400_GRN_CHANHI,
			&chip->shadow[TCS3400_GRN_CHANHI]);

	tcs3400_i2c_read(chip, TCS3400_BLU_CHANLO,
			&chip->shadow[TCS3400_BLU_CHANLO]);
	ret = tcs3400_i2c_read(chip, TCS3400_BLU_CHANHI,
			&chip->shadow[TCS3400_BLU_CHANHI]);

	return (ret < 0) ? ret : 0;
}

static int tcs3400_update_als_thres(struct tcs3400_chip *chip, bool on_enable)
{
	s32 ret;
	u8 *buf = &chip->shadow[TCS3400_ALS_MINTHRESHLO];
	u16 deltaP = chip->params.als_deltaP;
	u16 from, to, cur;
	u16 saturation = chip->als_inf.saturation;

	cur = chip->als_inf.clear_raw;

	if (on_enable) {
		/* move deltaP far away from current position to force an irq */
		from = to = cur > saturation / 2 ? 0 : saturation;
	} else {
		deltaP = cur * deltaP / 100;
		if (!deltaP)
			deltaP = 1;

		if (cur > deltaP)
			from = cur - deltaP;
		else
			from = 0;

		if (cur < (saturation - deltaP))
			to = cur + deltaP;
		else
			to = saturation;

	}

	*buf++ = from & 0xff;
	*buf++ = from >> 8;
	*buf++ = to & 0xff;
	*buf++ = to >> 8;
	ret = tcs3400_i2c_reg_blk_write(chip, TCS3400_ALS_MINTHRESHLO,
			&chip->shadow[TCS3400_ALS_MINTHRESHLO],
			TCS3400_ALS_MAXTHRESHHI - TCS3400_ALS_MINTHRESHLO + 1);

	return (ret < 0) ? ret : 0;
}


static void tcs3400_report_als(struct tcs3400_chip *chip)
{
	if (chip->a_idev) {
		int rc = tcs3400_get_lux(chip);
		if (!rc) {
			int lux = chip->als_inf.lux;
			input_report_abs(chip->a_idev, ABS_MISC, lux);
			input_sync(chip->a_idev);
			tcs3400_update_als_thres(chip, 0);
		} else {
			tcs3400_update_als_thres(chip, 1);
		}
	}
}

static int tcs3400_check_and_report(struct tcs3400_chip *chip)
{
	u8 status;
	u8 saturation;

	int ret = tcs3400_read_all(chip);
	if (ret)
		goto exit_clr;

	status = chip->shadow[TCS3400_STATUS];

	saturation = chip->als_inf.saturation;
	
	if ((status & (TCS3400_ST_ALS_VALID | TCS3400_ST_ALS_IRQ)) ==
			(TCS3400_ST_ALS_VALID | TCS3400_ST_ALS_IRQ)) {
		tcs3400_get_als(chip);
		tcs3400_report_als(chip);
		tcs3400_irq_clr(chip, TCS3400_CMD_ALS_INT_CLR);
	}

exit_clr:
	tcs3400_irq_clr(chip, TCS3400_CMD_ALL_INT_CLR);

	return ret;
}

static irqreturn_t tcs3400_irq(int irq, void *handle)
{
	struct tcs3400_chip *chip = handle;
	struct device *dev = &chip->client->dev;

	mutex_lock(&chip->lock);
	if (chip->in_suspend) {
		dev_info(dev, "%s: in suspend\n", __func__);
		chip->irq_pending = 1;
		disable_irq_nosync(chip->client->irq);
		goto bypass;
	}
	(void)tcs3400_check_and_report(chip);
bypass:
	mutex_unlock(&chip->lock);
	return IRQ_HANDLED;
}

static void tcs3400_set_defaults(struct tcs3400_chip *chip)
{
	u8 *sh = chip->shadow;
	struct device *dev = &chip->client->dev;

	if (chip->pdata) {
		dev_info(dev, "%s: Loading pltform data\n", __func__);
		chip->params.als_time = chip->pdata->parameters.als_time;
		chip->params.als_deltaP = chip->pdata->parameters.als_deltaP;
	
		chip->params.persist = chip->pdata->parameters.persist;
		
		chip->params.als_gain = chip->pdata->parameters.als_gain;
	
	} else {
		dev_info(dev, "%s: use defaults\n", __func__);
		sh[TCS3400_ALS_TIME] = 0x6B; /* 405ms */
		sh[TCS3400_PERSISTENCE] = ALS_PERSIST(0);
		sh[TCS3400_PRX_PULSE_COUNT] = 8;
		sh[TCS3400_GAIN] = AGAIN_16;
	}

	chip->als_gain_auto = true;

	
	chip->shadow[TCS3400_PERSISTENCE]     = chip->params.persist;
	chip->shadow[TCS3400_ALS_TIME]        = chip->params.als_time;
	
	chip->shadow[TCS3400_GAIN]            = chip->params.als_gain; 
	
	tcs3400_flush_regs(chip);

}


static int tcs3400_als_enable(struct tcs3400_chip *chip, int on)
{
	int rc;

	dev_info(&chip->client->dev, "%s: on = %d\n", __func__, on);
	if (on) {
		tcs3400_irq_clr(chip, TCS3400_CMD_ALS_INT_CLR);
		tcs3400_update_als_thres(chip, 1);
		chip->shadow[TCS3400_CONTROL] |=
				(TCS3400_EN_PWR_ON | TCS3400_EN_ALS |
				TCS3400_EN_ALS_IRQ);

		rc = tcs3400_update_enable_reg(chip);
		if (rc)
			return rc;
		mdelay(3);
	} else {
		chip->shadow[TCS3400_CONTROL] &=
			~(TCS3400_EN_ALS_IRQ);

		if (!(chip->shadow[TCS3400_CONTROL] & TCS3400_EN_PRX))
			chip->shadow[TCS3400_CONTROL] &= ~TCS3400_EN_PWR_ON;
		rc = tcs3400_update_enable_reg(chip);
		if (rc)
			return rc;
		tcs3400_irq_clr(chip, TCS3400_CMD_ALS_INT_CLR);
	}
	if (!rc)
		chip->als_enabled = on;

	return rc;
}



static ssize_t tcs3400_device_als_lux(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	tcs3400_read_all(chip);
	tcs3400_get_als(chip);
	tcs3400_get_lux(chip);
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->als_inf.lux);
}

static ssize_t tcs3400_lux_table_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	struct lux_segment *s = chip->segment;
	int i, k;

	for (i = k = 0; i < chip->segment_num; i++)
		k += snprintf(buf + k, PAGE_SIZE - k,
				"%d:%d,%d,%d,%d,%d,%d\n", i,
				s[i].d_factor,
				s[i].r_coef,
				s[i].g_coef,
				s[i].b_coef,
				s[i].ct_coef,
				s[i].ct_offset
				);
	return k;
}



static ssize_t tcs3400_lux_table_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int i;
	struct tcs3400_chip *chip = chip_info;
	u32 d_factor, r_coef, g_coef, b_coef, ct_coef, ct_offset;

	if (7 != sscanf(buf, "%10d:%10d,%10d,%10d,%10d,%10d,%10d",
		&i, &d_factor, &r_coef, &g_coef, &b_coef, &ct_coef, &ct_offset))
		return -EINVAL;
	if (i >= chip->segment_num)
		return -EINVAL;
	mutex_lock(&chip->lock);
	chip->segment[i].d_factor = d_factor;
	chip->segment[i].r_coef = r_coef;
	chip->segment[i].g_coef = g_coef;
	chip->segment[i].b_coef = b_coef;
	chip->segment[i].b_coef = ct_coef;
	chip->segment[i].b_coef = ct_offset;
	mutex_unlock(&chip->lock);
	return size;
}

static ssize_t tcs3400_als_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->als_enabled);
}

static ssize_t tcs3400_als_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct tcs3400_chip *chip = chip_info;
	bool value;

	if (strtobool(buf, &value))
		return -EINVAL;

	if (value)
		tcs3400_als_enable(chip, 1);
	else
		tcs3400_als_enable(chip, 0);

	return size;
}



static ssize_t tcs3400_auto_gain_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	return snprintf(buf, PAGE_SIZE, "%s\n",
				chip->als_gain_auto ? "auto" : "manual");
}

static ssize_t tcs3400_auto_gain_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct tcs3400_chip *chip = chip_info;
	bool value;

	if (strtobool(buf, &value))
		return -EINVAL;

	if (value)
		chip->als_gain_auto = true;
	else
		chip->als_gain_auto = false;

	return size;
}

static ssize_t tcs3400_als_gain_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	return snprintf(buf, PAGE_SIZE, "%d (%s)\n",
			als_gains[chip->params.als_gain],
			chip->als_gain_auto ? "auto" : "manual");
}



static ssize_t tcs3400_als_red_show(struct device *dev,
	struct device_attribute *attr, char *buf)
		{
	struct tcs3400_chip *chip = chip_info;
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->als_inf.red_raw);
}

static ssize_t tcs3400_als_green_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->als_inf.green_raw);
}

static ssize_t tcs3400_als_blue_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->als_inf.blue_raw);
}

static ssize_t tcs3400_als_clear_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->als_inf.clear_raw);
}

static ssize_t tcs3400_als_cct_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	tcs3400_read_all(chip);
	tcs3400_get_als(chip);
	tcs3400_get_lux(chip);
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->als_inf.cct);
}

static ssize_t tcs3400_als_gain_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	unsigned long gain;
	int i = 0;
	int rc;
	struct tcs3400_chip *chip = chip_info;

	rc = kstrtoul(buf, 10, &gain);

	if (rc)
		return -EINVAL;
	if (gain != 0 && gain != 1 && gain != 4 && gain != 16 && gain != 64)
		return -EINVAL;

	while (i < sizeof(als_gains)) {
		if (gain == als_gains[i])
			break;
		i++;
	}

	if (i > 3) {
		dev_err(&chip->client->dev, "%s: wrong als gain %d\n",
				__func__, (int)gain);
		return -EINVAL;
	}

	mutex_lock(&chip->lock);
	if (gain) {
		chip->als_gain_auto = false;
		rc = tcs3400_set_als_gain(chip, als_gains[i]);
		if (!rc)
			tcs3400_calc_cpl(chip);
	} else {
		chip->als_gain_auto = true;
	}
	tcs3400_flush_regs(chip);
	mutex_unlock(&chip->lock);
	return rc ? rc : size;
}



static ssize_t tcs3400_als_persist_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	return snprintf(buf, PAGE_SIZE, "%d\n",
			(((chip->shadow[TCS3400_PERSISTENCE]) & 0x0f)));
}

static ssize_t tcs3400_als_persist_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	long persist;
	int rc;
	struct tcs3400_chip *chip = chip_info;

	rc = kstrtoul(buf, 10, &persist);
	if (rc)
		return -EINVAL;

	mutex_lock(&chip->lock);
	chip->shadow[TCS3400_PERSISTENCE] &= 0xF0;
	chip->shadow[TCS3400_PERSISTENCE] |= ((u8)persist & 0x0F);

	tcs3400_flush_regs(chip);

	mutex_unlock(&chip->lock);
	return size;
}


static ssize_t tcs3400_als_itime_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	int t;
	t = 256 - chip->shadow[TCS3400_ALS_TIME];
	t *= INTEGRATION_CYCLE;
	t /= 100;
	return snprintf(buf, PAGE_SIZE, "%d (in ms)\n", t);
}

static ssize_t tcs3400_als_itime_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	long itime;
	int rc;
	struct tcs3400_chip *chip = chip_info;

	rc = kstrtoul(buf, 10, &itime);
	if (rc)
		return -EINVAL;
	itime *= 100;
	itime /= INTEGRATION_CYCLE;
	itime = (256 - itime);
	mutex_lock(&chip->lock);
	chip->shadow[TCS3400_ALS_TIME] = (u8)itime;
	tcs3400_flush_regs(chip);

	mutex_unlock(&chip->lock);
	return size;
}



static ssize_t tcs3400_als_deltaP_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tcs3400_chip *chip = chip_info;
	return snprintf(buf, PAGE_SIZE,
			"%d (in %%)\n", chip->params.als_deltaP);
}

static ssize_t tcs3400_als_deltaP_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	unsigned long deltaP;
	int rc;
	struct tcs3400_chip *chip = chip_info;

	rc = kstrtoul(buf, 10, &deltaP);
	if (rc || deltaP > 100)
		return -EINVAL;
	mutex_lock(&chip->lock);
	chip->params.als_deltaP = deltaP;
	mutex_unlock(&chip->lock);
	return size;
}


//<asus alex_wang 20170307> add factory +++++
#ifdef ASUS_FACTORY_BUILD
static ssize_t tcs3400_als_status_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int value;
	int ret;
	struct tcs3400_chip *chip = chip_info;

	ret = tcs3400_i2c_read(chip, TCS3400_STATUS,
			&chip->shadow[TCS3400_STATUS]);

	if (ret) {
		value = 0;
	} else {
		value = 1;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", value);

}

static struct device_attribute dev_attr_als_status =
__ATTR(rgb_status, 0664, tcs3400_als_status_show, NULL);
#endif
//<asus alex_wang 20170307> add factory ----


static struct device_attribute dev_attr_als_Itime =
__ATTR(als_Itime, 0664, tcs3400_als_itime_show,
		tcs3400_als_itime_store);

static struct device_attribute dev_attr_als_lux =
__ATTR(als_lux, S_IRUGO, tcs3400_device_als_lux, NULL);

static struct device_attribute dev_attr_als_red =
__ATTR(als_red, S_IRUGO, tcs3400_als_red_show, NULL);

static struct device_attribute dev_attr_als_green =
__ATTR(als_green, S_IRUGO, tcs3400_als_green_show, NULL);

static struct device_attribute dev_attr_als_blue =
__ATTR(als_blue, S_IRUGO, tcs3400_als_blue_show, NULL);

static struct device_attribute dev_attr_als_clear =
__ATTR(als_clear, S_IRUGO, tcs3400_als_clear_show, NULL);

static struct device_attribute dev_attr_als_cct =
__ATTR(als_cct, S_IRUGO, tcs3400_als_cct_show, NULL);

static struct device_attribute dev_attr_als_gain =
__ATTR(als_gain, 0664, tcs3400_als_gain_show,
			tcs3400_als_gain_store);

static struct device_attribute dev_attr_als_thresh_deltaP =
__ATTR(als_thresh_deltaP, 0664, tcs3400_als_deltaP_show,
			tcs3400_als_deltaP_store);

static struct device_attribute dev_attr_als_auto_gain =
__ATTR(als_auto_gain, 0664, tcs3400_auto_gain_enable_show,
			tcs3400_auto_gain_enable_store);

static struct device_attribute dev_attr_als_lux_table =
__ATTR(lux_table, 0664, tcs3400_lux_table_show,
			tcs3400_lux_table_store);

static struct device_attribute dev_attr_als_power_state =
__ATTR(als_power_state, 0664, tcs3400_als_enable_show,
			tcs3400_als_enable_store);

static struct device_attribute dev_attr_als_persist =
__ATTR(als_persist, 0664, tcs3400_als_persist_show,
			tcs3400_als_persist_store);

static struct attribute *rgb_back_sysfs_attrs[] = {
#ifdef ASUS_FACTORY_BUILD
	&dev_attr_als_status.attr,
#endif
	&dev_attr_als_Itime.attr,
	&dev_attr_als_lux.attr,
	&dev_attr_als_red.attr,
	&dev_attr_als_green.attr,
	&dev_attr_als_blue.attr,
	&dev_attr_als_clear.attr,
	&dev_attr_als_cct.attr,
	&dev_attr_als_gain.attr,
	&dev_attr_als_thresh_deltaP.attr,
	&dev_attr_als_auto_gain.attr,
	&dev_attr_als_lux_table.attr,
	&dev_attr_als_power_state.attr,
	&dev_attr_als_persist.attr,
	NULL
};

static struct attribute_group rgb_back_attribute_group = {
	.attrs = rgb_back_sysfs_attrs,
};


/*
static int tcs3400_add_sysfs_interfaces(struct device *dev,
	struct device_attribute *a, int size)
{
	int i;
	for (i = 0; i < size; i++)
		if (device_create_file(dev, a + i))
			goto undo;
	return 0;
undo:
	for (; i >= 0 ; i--)
		device_remove_file(dev, a + i);
	dev_err(dev, "%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}
*/
/*
static void tcs3400_remove_sysfs_interfaces(struct device *dev,
	struct device_attribute *a, int size)
{
	int i;
	for (i = 0; i < size; i++)
		device_remove_file(dev, a + i);
}
*/

static int tcs3400_get_id(struct tcs3400_chip *chip, u8 *id, u8 *rev)
{
	tcs3400_i2c_read(chip, TCS3400_REVID, rev);
	return tcs3400_i2c_read(chip, TCS3400_CHIPID, id);
}

static int tcs3400_power_on(struct tcs3400_chip *chip)
{
	int rc;
	rc = tcs3400_pltf_power_on(chip);
	if (rc)
		return rc;
	dev_info(&chip->client->dev, "%s: chip was off, restoring regs\n",
			__func__);
	return tcs3400_flush_regs(chip);
}


static int tcs3400_als_idev_open(struct input_dev *idev)
{
	struct tcs3400_chip *chip = chip_info;
	int rc;
	//bool prox = chip->p_idev && chip->p_idev->users;

	dev_info(&idev->dev, "%s\n", __func__);
	mutex_lock(&chip->lock);
	if (chip->unpowered) {
		rc = tcs3400_power_on(chip);
		if (rc)
			goto chip_on_err;
	}
	rc = tcs3400_als_enable(chip, 1);
	//if (rc && !prox)
	//	tcs3400_pltf_power_off(chip);
chip_on_err:
	mutex_unlock(&chip->lock);
	return rc;
}

static void tcs3400_als_idev_close(struct input_dev *idev)
{
	struct tcs3400_chip *chip = chip_info;
	dev_info(&idev->dev, "%s\n", __func__);
	mutex_lock(&chip->lock);
	tcs3400_als_enable(chip, 0);

	//if (!chip->p_idev || !chip->p_idev->users)
	tcs3400_pltf_power_off(chip);
	mutex_unlock(&chip->lock);
}

//joyce++
/*static int tcs3400_parse_dt(struct device *dev,
				struct tcs3400_i2c_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	u32 temp_val;
	int rc;
	//D("[LS][CM36656] %s\n", __func__);
  
	rc = of_get_named_gpio_flags(np, "ams,intrpin-gpios",
			0, NULL);
	if (rc < 0) 
	{
		dev_err(dev, "Unable to read interrupt pin number\n");
		return rc;
	} 
	else
	{
		//pdata->intr_pin = rc;
		printk("joyce: [RGB][TCS3400] %s GET INTR PIN \n",__func__); 
	}

	rc = of_property_read_u32(np, "ams,slave_address", &temp_val);
	if (rc)
	{
		dev_err(dev, "Unable to read slave_address\n");
		return rc;
	} 
	else
	{
		//pdata->slave_addr = (uint8_t)temp_val;
		printk("joyce: [RGB][TCS3400] %s GET slave address \n",__func__); 
	}
  
	printk("joyce: [RGB][TCS3400] %s PARSE OK \n",__func__); 

	return 0;
}
*/

//asus alex_wang setting chip power+++ 
static int tcs3400_power_set(struct tcs3400_chip *chip, bool on)
{
    int rc;  
	if(on){
           chip->vio = regulator_get(&chip->client->dev, "vio");
	       if (IS_ERR(chip->vio)) {
			    rc = PTR_ERR(chip->vio);
		        dev_err(&chip->client->dev,"Regulator get failed vio rc=%d\n", rc);
			    goto err_vio_get;
	       }
           if (regulator_count_voltages(chip->vio) > 0) {
			   rc = regulator_set_voltage(chip->vio,TCS3400_VIO_TYP_UV, TCS3400_VIO_TYP_UV);
		       if (rc) {
				   dev_err(&chip->client->dev,"Regulator set failed vio rc=%d\n", rc);
				   goto err_vio_set_vtg;
			   }
	       }
       	   rc = regulator_enable(chip->vio);
	       if (rc) {
			   dev_err(&chip->client->dev,"Regulator vio enable failed rc=%d\n", rc);
			   goto err_vio_ena;
	       }
	//VDD ->LEDA	  
         chip->vdd = regulator_get(&chip->client->dev, "vdd");
	       if (IS_ERR(chip->vdd)) {
			    rc = PTR_ERR(chip->vdd);
		        dev_err(&chip->client->dev,"Regulator get failed vdd rc=%d\n", rc);
			    goto err_vdd_get;
	       }
           if (regulator_count_voltages(chip->vdd) > 0) {
			   rc = regulator_set_voltage(chip->vdd,TCS3400_VDD_TYP_UV, TCS3400_VDD_TYP_UV);
		       if (rc) {
				   dev_err(&chip->client->dev,"Regulator set failed vdd rc=%d\n", rc);
				   goto err_vdd_set_vtg;
			   }
	       }
       	   rc = regulator_enable(chip->vdd);
	       if (rc) {
			   dev_err(&chip->client->dev,"Regulator vdd enable failed rc=%d\n", rc);
			   goto err_vdd_ena;
	       }
	} else {
			rc = regulator_disable(chip->vio);
		    if (rc) {
			     dev_err(&chip->client->dev,"Regulator vio disable failed rc=%d\n", rc);
			     return rc;
		     }
		    if (regulator_count_voltages(chip->vio) > 0)
			  regulator_set_voltage(chip->vio, 0,TCS3400_VIO_TYP_UV);

		    regulator_put(chip->vio);
//VDD ->LEDA
		  rc = regulator_disable(chip->vdd);
		    if (rc) {
			     dev_err(&chip->client->dev,"Regulator vdd disable failed rc=%d\n", rc);
			     return rc;
		     }
		    if (regulator_count_voltages(chip->vdd) > 0)
			  regulator_set_voltage(chip->vdd, 0,TCS3400_VDD_TYP_UV);

		    regulator_put(chip->vdd);
	}
	dev_err(&chip->client->dev,"TCS3400 set power ok");
	return 0;
err_vio_ena:
	if (regulator_count_voltages(chip->vio) > 0)
		regulator_set_voltage(chip->vio, 0, TCS3400_VIO_TYP_UV);
err_vio_set_vtg:
	regulator_put(chip->vio);
err_vio_get:
	return rc;
err_vdd_ena:
	if (regulator_count_voltages(chip->vdd) > 0)
		regulator_set_voltage(chip->vdd, 0, TCS3400_VDD_TYP_UV);
err_vdd_set_vtg:
	regulator_put(chip->vio);
err_vdd_get:
      return rc;
}
//asus alex_wang setting  chip power---

/*
* @pin_mux - single module pin-mux structure which defines pin-mux
*			details for all its pins.
*/
/*static void setup_pin_mux(struct pinmux_config *pin_mux)
{
	int i;

	for (i = 0; pin_mux->string_name != NULL; pin_mux++)
		omap_mux_init_signal(pin_mux->string_name, pin_mux->val);

}

static int board_tcs3400_init(void)
{
	printk(KERN_INFO "board_tcs3400_init CALLED\n");
	setup_pin_mux(tcs3400_pin_mux);
	return 0;
}
*/

static int board_tcs3400_power(struct device *dev, enum tcs3400_pwr_state state)
{
	printk(KERN_INFO "board_tcs3400_power CALLED\n");
	return 0;
}

static void board_tcs3400_teardown(struct device *dev)
{
	printk(KERN_INFO "board_tcs3400_teardow CALLED\n");
	dev_dbg(dev, "%s\n", __func__);
}

static const struct lux_segment tcs3400_segment[] = {
	{
		.d_factor = D_Factor1,
		.r_coef = R_Coef1,
		.g_coef = G_Coef1,
		.b_coef = B_Coef1,
		.ct_coef = CT_Coef1,
		.ct_offset = CT_Offset1,
	},
	{
		.d_factor = D_Factor1,
		.r_coef = R_Coef1,
		.g_coef = G_Coef1,
		.b_coef = B_Coef1,
		.ct_coef = CT_Coef1,
		.ct_offset = CT_Offset1,
	},
};

struct tcs3400_i2c_platform_data tcs3400_data = {
        .platform_power = board_tcs3400_power,
        //.platform_init = board_tcs3400_init,
        .platform_teardown = board_tcs3400_teardown,
        .als_name = "taos_als",
	.parameters = {
		.persist = ALS_PERSIST(0),
		.als_deltaP = 10,
		.als_time = 0x6B, /* 5.6ms */
		.als_gain = AGAIN_16,
	},
	.als_can_wake = false,
	.segment = (struct lux_segment *) tcs3400_segment,
	.segment_num = ARRAY_SIZE(tcs3400_segment),

};

static int tcs3400_probe(struct i2c_client *client,
	const struct i2c_device_id *idp)
{


	int i, ret;
	u8 id, rev;
	struct device *dev = &client->dev;
	static struct tcs3400_chip *chip;
	//struct tcs3400_i2c_platform_data *pdata = dev->platform_data;
	struct tcs3400_i2c_platform_data *pdata = &tcs3400_data;

	bool powered = 0;

	dev_info(dev, "%s: client->irq = %d\n", __func__, client->irq);
	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "%s: i2c smbus byte data unsupported\n", __func__);
		ret = -EOPNOTSUPP;
		goto init_failed;
	}
	if (!pdata) {
		dev_err(dev, "%s: platform data required\n", __func__);
		ret = -EINVAL;
		goto init_failed;
	}

	if (!(pdata->als_name) || client->irq < 0) {
		dev_err(dev, "%s: no reason to run.\n", __func__);
		ret = -EINVAL;
		goto init_failed;
	}

	/*if (pdata->platform_init) {
		ret = pdata->platform_init();
		if (ret)
			goto init_failed;
	}
	*/

	if (pdata->platform_power) {
		ret = pdata->platform_power(dev, POWER_ON);
		if (ret) {
			dev_err(dev, "%s: pltf power on failed\n", __func__);
			goto pon_failed;
		}
		powered = true;
		mdelay(10);
	}

	chip = kzalloc(sizeof(struct tcs3400_chip), GFP_KERNEL);
	if (!chip) {
		ret = -ENOMEM;
		goto malloc_failed;
	}
	chip->client = client;
	chip->pdata = pdata;
	i2c_set_clientdata(client, chip);

    ret = tcs3400_power_set(chip,true);
	if(ret < 0){
		dev_err(dev, "%s: chip set power error\n", __func__);
		goto init_failed;
	}

	chip_info = chip;
	chip->chip_workqueue = create_singlethread_workqueue("tcs3400_wq");
    if (NULL == chip->chip_workqueue) {
        dev_err(dev, "%s: failed to create chip workqueue\n", __func__);
		goto init_failed;
    }

	chip->seg_num_max = chip->pdata->segment_num ?
			chip->pdata->segment_num : ARRAY_SIZE(segment_default);
	if (chip->pdata->segment)
		ret = tcs3400_set_segment_table(chip, chip->pdata->segment,
			chip->pdata->segment_num);
	else
		ret =  tcs3400_set_segment_table(chip, segment_default,
			ARRAY_SIZE(segment_default));
	if (ret)
		goto set_segment_failed;

	ret = tcs3400_get_id(chip, &id, &rev);

	dev_info(dev, "%s: device id:%02x device rev:%02x\n", __func__,
				id, rev);

	for (i = 0; i < ARRAY_SIZE(tcs3400_ids); i++) {
		if (id == tcs3400_ids[i])
			break;
	}

	if (i < ARRAY_SIZE(tcs3400_names)) {
		dev_info(dev, "%s: '%s rev. %d' detected\n", __func__,
			tcs3400_names[i], rev);
		chip->device_index = i;
	} else {
		dev_err(dev, "%s: not supported chip id\n", __func__);
		ret = -EOPNOTSUPP;
		goto id_failed;
	}

	mutex_init(&chip->lock);
	tcs3400_set_defaults(chip);
	ret = tcs3400_flush_regs(chip);
	if (ret)
		goto flush_regs_failed;

	if (pdata->platform_power) {
		pdata->platform_power(dev, POWER_OFF);
		powered = false;
		chip->unpowered = true;
	}

	if (!pdata->als_name)
		goto bypass_als_idev;

	chip->a_idev = input_allocate_device();
	if (!chip->a_idev) {
		dev_err(dev, "%s: no memory for input_dev '%s'\n",
				__func__, pdata->als_name);
		ret = -ENODEV;
		goto input_a_alloc_failed;
	}

	chip->a_idev->name = pdata->als_name;
	chip->a_idev->id.bustype = BUS_I2C;
	set_bit(EV_ABS, chip->a_idev->evbit);
	set_bit(ABS_MISC, chip->a_idev->absbit);
	input_set_abs_params(chip->a_idev, ABS_MISC, 0, 65535, 0, 0);
	chip->a_idev->open = tcs3400_als_idev_open;
	chip->a_idev->close = tcs3400_als_idev_close;
	ret = input_register_device(chip->a_idev);
	if (ret) {
		input_free_device(chip->a_idev);
		goto input_a_alloc_failed;
	}

	//rgb add +++
	chip->tcs3400_class = class_create(THIS_MODULE, "rgb_sensors");
	if (IS_ERR(chip->tcs3400_class)) {
		ret = PTR_ERR(chip->tcs3400_class);
		chip->tcs3400_class = NULL;
		goto err_create_class;
	}

	chip->rgb_dev = device_create(chip->tcs3400_class,
				NULL, 0, "%s", "rgbsensor");
	if (unlikely(IS_ERR(chip->rgb_dev))) {
		ret = PTR_ERR(chip->rgb_dev);
		chip->rgb_dev = NULL;
		goto err_create_rgb_device;
	}

	/* register the attributes */
	ret = sysfs_create_group(&chip->a_idev->dev.kobj, &rgb_back_attribute_group);
	if (ret)
		goto err_sysfs_create_group_input_rgb;

	ret = sysfs_create_group(&chip->rgb_dev->kobj, &rgb_back_attribute_group);
	if (ret)
		goto err_sysfs_create_group_rgb;
	//rgb add ---

	//ret = tcs3400_add_sysfs_interfaces(&chip->a_idev->dev,
			//als_attrs, ARRAY_SIZE(als_attrs));
	//if (ret)
		//goto input_a_sysfs_failed;

bypass_als_idev:
	dev_info(dev, "enter bypass_als_idev.\n");
	ret = request_threaded_irq(client->irq, NULL, &tcs3400_irq,
		      IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		      dev_name(dev), chip);
	if (ret) {
		dev_info(dev, "Failed to request irq %d\n", client->irq);
		goto irq_register_fail;
	}

	chip->shadow[TCS3400_CONTROL] =
			(TCS3400_EN_PWR_ON | TCS3400_EN_ALS);

	tcs3400_update_enable_reg(chip);

	dev_info(dev, "Probe ok.\n");
	return 0;

irq_register_fail:
	sysfs_remove_group(&chip->rgb_dev->kobj, &rgb_back_attribute_group);
	//if (chip->a_idev) {
		//tcs3400_remove_sysfs_interfaces(&chip->a_idev->dev,
			//als_attrs, ARRAY_SIZE(als_attrs));
//input_a_sysfs_failed:
		//input_unregister_device(chip->a_idev);
	//}
err_sysfs_create_group_rgb:
    sysfs_remove_group(&chip->a_idev->dev.kobj, &rgb_back_attribute_group);
err_sysfs_create_group_input_rgb:
	device_destroy(chip->tcs3400_class, chip->rgb_dev->devt);
err_create_rgb_device:
	class_destroy(chip->tcs3400_class);
err_create_class:
	input_unregister_device(chip->a_idev);
input_a_alloc_failed:
	dev_err(dev, "input_a_alloc_failed.\n");
flush_regs_failed:
	dev_err(dev, "flush_regs_failed.\n");
id_failed:
	kfree(chip->segment);
set_segment_failed:
	i2c_set_clientdata(client, NULL);
	kfree(chip);
malloc_failed:
	if (powered && pdata->platform_power)
		pdata->platform_power(dev, POWER_OFF);
pon_failed:
	if (pdata->platform_teardown)
		pdata->platform_teardown(dev);
init_failed:
	dev_err(dev, "Probe failed.\n");
	return ret;
}

static int tcs3400_suspend(struct device *dev)
{
	struct tcs3400_chip *chip = chip_info;
	struct tcs3400_i2c_platform_data *pdata = &tcs3400_data;

	dev_info(dev, "%s\n", __func__);
	mutex_lock(&chip->lock);
	chip->in_suspend = 1;

	if (chip->a_idev && chip->a_idev->users) {
		if (pdata->als_can_wake) {
			dev_info(dev, "set wake on als\n");
			chip->wake_irq = 1;
		} else {
			dev_info(dev, "als off\n");
			tcs3400_als_enable(chip, 0);
		}
	}
	if (chip->wake_irq) {
		irq_set_irq_wake(chip->client->irq, 1);
	} else if (!chip->unpowered) {
		dev_info(dev, "powering off\n");
		tcs3400_pltf_power_off(chip);
	}
	mutex_unlock(&chip->lock);

	return 0;
}

static int tcs3400_resume(struct device *dev)
{
	struct tcs3400_chip *chip = chip_info;
	bool als_on;
	int rc = 0;
	mutex_lock(&chip->lock);
	als_on = chip->a_idev && chip->a_idev->users;
	chip->in_suspend = 0;

	dev_info(dev, "%s: powerd %d, als: needed %d  enabled %d",
			__func__, !chip->unpowered, als_on,
			chip->als_enabled);

	if (chip->wake_irq) {
		irq_set_irq_wake(chip->client->irq, 0);
		chip->wake_irq = 0;
	}
	if (chip->unpowered && als_on) {
		dev_info(dev, "powering on\n");
		rc = tcs3400_power_on(chip);
		if (rc)
			goto err_power;
	}
	if (als_on && !chip->als_enabled)
		(void)tcs3400_als_enable(chip, 1);
	if (chip->irq_pending) {
		dev_info(dev, "%s: pending interrupt\n", __func__);
		chip->irq_pending = 0;
		(void)tcs3400_check_and_report(chip);
		enable_irq(chip->client->irq);
	}
err_power:
	mutex_unlock(&chip->lock);

	return 0;
}

static int tcs3400_remove(struct i2c_client *client)
{
	struct tcs3400_chip *chip = i2c_get_clientdata(client);
	free_irq(client->irq, chip);
	if (chip->a_idev) {
		//tcs3400_remove_sysfs_interfaces(&chip->a_idev->dev,
			//als_attrs, ARRAY_SIZE(als_attrs));
		input_unregister_device(chip->a_idev);
	}

	if (chip->pdata->platform_teardown)
		chip->pdata->platform_teardown(&client->dev);
	i2c_set_clientdata(client, NULL);
	kfree(chip->segment);
	kfree(chip);
	return 0;
}

static const struct dev_pm_ops tcs3400_pm_ops = {
	.suspend = tcs3400_suspend,
	.resume  = tcs3400_resume,
};

static const struct i2c_device_id tcs3400_i2c_id[] = {
	{TCS3400_I2C_NAME, 0},
	{}
};

static struct of_device_id tcs3400_match_table[] = {
	{ .compatible = "ams,tcs3400",},
	{ },
};

static struct i2c_driver tcs3400_driver = {
	.driver = {
		.name = TCS3400_I2C_NAME,
		.owner = THIS_MODULE,
		.pm = &tcs3400_pm_ops,
		.of_match_table = tcs3400_match_table, 
	},
	.id_table = tcs3400_i2c_id,
	.probe = tcs3400_probe,
	.remove = tcs3400_remove,
};

static int __init tcs3400_init(void)
{
	return i2c_add_driver(&tcs3400_driver);
}

static void __exit tcs3400_exit(void)
{
	i2c_del_driver(&tcs3400_driver);
}

module_init(tcs3400_init);
module_exit(tcs3400_exit);

MODULE_DESCRIPTION("AMS-TAOS tcs3400 Color sensor driver");
MODULE_LICENSE("GPL");
