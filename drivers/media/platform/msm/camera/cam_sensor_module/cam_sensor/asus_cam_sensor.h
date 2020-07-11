#ifndef ASUS_CAM_SENSOR_H
#define ASUS_CAM_SENSOR_H

#include "cam_sensor_dev.h"

#define PROJECT_DRACO

void asus_cam_sensor_init(struct cam_sensor_ctrl_t *s_ctrl);

#ifdef PROJECT_DRACO
int is_OV8856_R1B(uint32_t id);
int verify_imx363_id(struct cam_sensor_ctrl_t *s_ctrl);
void set_vcm_lens_pos_from_ois_writing(uint32_t dac_value);
int get_current_lens_position(uint32_t *dac_value);
#endif

#endif
