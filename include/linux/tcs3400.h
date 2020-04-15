/*
 * Device driver for monitoring ambient light intensity in (lux)
 * proximity detection (prox), and Beam functionality within the
 * AMS-TAOS TCS3400 family of devices.
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

#ifndef __TCS3400_H
#define __TCS3400_H

#include <linux/types.h>

#define TCS3400_I2C_NAME "tcs3400"

/* Max number of segments allowable in LUX table */
#define TCS3400_MAX_LUX_TABLE_SIZE		9
#define MAX_DEFAULT_TABLE_BYTES (sizeof(int) * TCS3400_MAX_LUX_TABLE_SIZE)

/* Default LUX and Color coefficients */

#define D_Factor	241
#define R_Coef		144
#define G_Coef		1000
#define B_Coef		400
#define CT_Coef		(3972)
#define CT_Offset	(1672)

#define D_Factor1	241
#define R_Coef1		144
#define G_Coef1		1000
#define B_Coef1		400
#define CT_Coef1	(3972)
#define CT_Offset1	(1672)

struct device;

enum tcs3400_pwr_state {
	POWER_ON,
	POWER_OFF,
	POWER_STANDBY,
};

enum tcs3400_ctrl_reg {
	AGAIN_1        = (0 << 0),
	AGAIN_4        = (1 << 0),
	AGAIN_16       = (2 << 0),
	AGAIN_64       = (3 << 0),
};

#define ALS_PERSIST(p) (((p) & 0xf) << 3)

struct tcs3400_parameters {
	u8 als_time;
	u16 als_deltaP;
	u8 als_gain;
	u8 persist;
};

struct lux_segment {
	int d_factor;
	int r_coef;
	int g_coef;
	int b_coef;
	int ct_coef;
	int ct_offset;
};

/*struct tcs3400_data = {
	.platform_power = board_tcs3400_power,
	.platform_init = board_tcs3400_init,
	.platform_teardown = board_tcs3400_teardown,
	.als_name = "taos_als",
	.parameters = {
		.persist = ALS_PERSIST(0),
		.als_deltaP = 10,
		.als_time = 0x6B,
	.als_gain = AGAIN_16,
	},
	.als_can_wake = false,
	.segment = (struct lux_segment *) tcs3400_segment,
	.segment_num = ARRAY_SIZE(tcs3400_segment),
};
*/

struct tcs3400_i2c_platform_data {
	/* The following callback for power events received and handled by
	   the driver.  Currently only for SUSPEND and RESUME */
	int (*platform_power)(struct device *dev, enum tcs3400_pwr_state state);
	int (*platform_init)(void);
	void (*platform_teardown)(struct device *dev);
	char const *als_name;
	struct tcs3400_parameters parameters;
	bool als_can_wake;
	struct lux_segment *segment;
	int segment_num;
	//struct tcs3400_data data;
};

#endif /* __TCS3400_H */
