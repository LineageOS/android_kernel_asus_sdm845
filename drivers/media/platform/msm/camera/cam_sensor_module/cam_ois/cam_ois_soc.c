/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <cam_sensor_cmn_header.h>
#include <cam_sensor_util.h>
#include <cam_sensor_io.h>
#include <cam_req_mgr_util.h>

#include "cam_ois_soc.h"
#include "cam_debug_util.h"


//ASUS_BSP +++ Zhengwei "read id register when probe"
void dump_regulator_name(struct cam_ois_ctrl_t *o_ctrl,const char * tag)
{
	int i;
	struct cam_hw_soc_info   *soc_info = &o_ctrl->soc_info;
	for (i = 0; i < soc_info->num_rgltr; i++)
	{
		if (soc_info->rgltr_name[i] == NULL) {
			CAM_ERR(CAM_OIS, "%s: can't find regulator name for index %d",tag,i);
			continue;
		}
		CAM_INFO(CAM_OIS,"%s: regulator[%d] = %s",tag,i,soc_info->rgltr_name[i]);
	}
}

static int cam_ois_get_dt_i2c_info(struct cam_ois_ctrl_t *o_ctrl)
{
	int                             rc = 0;
	struct cam_hw_soc_info         *soc_info = &o_ctrl->soc_info;
	struct cam_ois_soc_private     *soc_private = (struct cam_ois_soc_private *)soc_info->soc_private;
	struct cam_ois_i2c_info_t      *i2c_info = &soc_private->i2c_info;
	struct device_node             *of_node = NULL;
	uint32_t id_info[3];

	if (!soc_info->dev) {
		CAM_ERR(CAM_OIS, "soc_info is not initialized");
		return -EINVAL;
	}

	of_node = soc_info->dev->of_node;
	if (!of_node) {
		CAM_ERR(CAM_OIS, "dev.of_node NULL");
		return -EINVAL;
	}

	rc = of_property_read_u32_array(of_node, "qcom,slave-id",
		id_info, 3);
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "read slave id failed, rc %d",rc);
		return rc;
	}
	rc = of_property_read_u8(of_node, "qcom,i2c-freq-mode",&i2c_info->i2c_freq_mode);
	if (rc < 0) {
		CAM_ERR(CAM_OIS,"i2c-freq-mode read fail, rc %d. Setting to 0\n",rc);
		i2c_info->i2c_freq_mode = 0;
	}
	i2c_info->slave_addr = id_info[0];
	i2c_info->id_register = id_info[1];
	i2c_info->chip_id = id_info[2];
	CAM_INFO(CAM_OIS,"Get from DT, slave addr 0x%x, id_reg 0x%x, chip_id 0x%x",
					 i2c_info->slave_addr,
					 i2c_info->id_register,
					 i2c_info->chip_id
					);
	return rc;
}
//ASUS_BSP --- Zhengwei "read id register when probe"

/**
 * @e_ctrl: ctrl structure
 *
 * Parses ois dt
 */
static int cam_ois_get_dt_data(struct cam_ois_ctrl_t *o_ctrl)
{
	int                             i, rc = 0;
	struct cam_hw_soc_info         *soc_info = &o_ctrl->soc_info;
	struct cam_ois_soc_private     *soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t *power_info = &soc_private->power_info;
	struct device_node             *of_node = NULL;

	of_node = soc_info->dev->of_node;

	if (!of_node) {
		CAM_ERR(CAM_OIS, "of_node is NULL, device type %d",
			o_ctrl->ois_device_type);
		return -EINVAL;
	}
	rc = cam_soc_util_get_dt_properties(soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_OIS, "cam_soc_util_get_dt_properties rc %d",
			rc);
		return rc;
	}

	//ASUS_BSP +++ Zhengwei "read id register when probe"
	power_info->dev = o_ctrl->soc_info.dev;
	/* Initialize default parameters */
	for (i = 0; i < soc_info->num_clk; i++) {
		soc_info->clk[i] = devm_clk_get(soc_info->dev,
					soc_info->clk_name[i]);
		if (!soc_info->clk[i]) {
			CAM_ERR(CAM_OIS, "get failed for %s",
				 soc_info->clk_name[i]);
			rc = -ENOENT;
			return rc;
		}
	}
	rc = cam_ois_get_dt_i2c_info(o_ctrl);
	if(rc < 0)
	{
		CAM_ERR(CAM_OIS, "Get i2c info failed!");
		return -EINVAL;
	}
	rc = cam_get_dt_power_setting_data(of_node,soc_info,power_info);
	if(rc < 0 || power_info->power_setting_size <= 0)
	{
		CAM_ERR(CAM_OIS, "Get power setting failed!");
		return -EINVAL;
	}
	rc = msm_camera_fill_vreg_params(
		soc_info,
		power_info->power_setting,
		power_info->power_setting_size);
	if (rc) {
		CAM_ERR(CAM_OIS,
			"failed to fill vreg params for power up rc:%d", rc);
		return rc;
	}
	rc = msm_camera_fill_vreg_params(
		soc_info,
		power_info->power_down_setting,
		power_info->power_down_setting_size);
	if (rc) {
		CAM_ERR(CAM_OIS,
			"failed to fill vreg params power down rc:%d", rc);
		return rc;
	}
	//ASUS_BSP --- Zhengwei "read id register when probe"

	if (!soc_info->gpio_data) {
		CAM_INFO(CAM_OIS, "No GPIO found");
		return 0;
	}

	if (!soc_info->gpio_data->cam_gpio_common_tbl_size) {
		CAM_INFO(CAM_OIS, "No GPIO found");
		return -EINVAL;
	}

	rc = cam_sensor_util_init_gpio_pin_tbl(soc_info,
		&power_info->gpio_num_info);
	if ((rc < 0) || (!power_info->gpio_num_info)) {
		CAM_ERR(CAM_OIS, "No/Error OIS GPIOs");
		return -EINVAL;
	}

	for (i = 0; i < soc_info->num_clk; i++) {
		soc_info->clk[i] = devm_clk_get(soc_info->dev,
			soc_info->clk_name[i]);
		if (!soc_info->clk[i]) {
			CAM_ERR(CAM_SENSOR, "get failed for %s",
				soc_info->clk_name[i]);
			rc = -ENOENT;
			return rc;
		}
	}

	return rc;
}
/**
 * @o_ctrl: ctrl structure
 *
 * This function is called from cam_ois_platform/i2c_driver_probe, it parses
 * the ois dt node.
 */
int cam_ois_driver_soc_init(struct cam_ois_ctrl_t *o_ctrl)
{
	int                             rc = 0;
	struct cam_hw_soc_info         *soc_info = &o_ctrl->soc_info;
	struct device_node             *of_node = NULL;

	if (!soc_info->dev) {
		CAM_ERR(CAM_OIS, "soc_info is not initialized");
		return -EINVAL;
	}

	of_node = soc_info->dev->of_node;
	if (!of_node) {
		CAM_ERR(CAM_OIS, "dev.of_node NULL");
		return -EINVAL;
	}

	if (o_ctrl->ois_device_type == MSM_CAMERA_PLATFORM_DEVICE) {
		rc = of_property_read_u32(of_node, "cci-master",
			&o_ctrl->cci_i2c_master);
		if (rc < 0) {
			CAM_DBG(CAM_OIS, "failed rc %d", rc);
			return rc;
		}
	}

	rc = cam_ois_get_dt_data(o_ctrl);
	if (rc < 0)
		CAM_DBG(CAM_OIS, "failed: ois get dt data rc %d", rc);

	return rc;
}
