#ifndef ASUS_OIS_ONSEMI_INTERFACE_H
#define ASUS_OIS_ONSEMI_INTERFACE_H

#include "cam_ois_dev.h"

#define	LITEON_VERNUM_VCM_0  0x03130204
#define	LITEON_VERNUM_VCM_1  0x03130204
#define	LITEON_VERNUM_VCM_2  0x03130206

#define	PRIMAX_VERNUM_VCM_1  0x05130204
#define	PRIMAX_VERNUM_VCM_2  0x05130206

#define LITEON_VERNUM_BASE 	0x03130201
#define PRIMAX_VERNUM_BASE 	0x05130201

#define VENDOR_ID_LITEON    0x03
#define VENDOR_ID_PRIMAX    0x05


typedef	int8_t		INT_8;
typedef	int16_t		INT_16;
typedef	int32_t     INT_32;
typedef	int64_t     INT_64;
typedef	uint8_t     UINT_8;
typedef	uint16_t    UINT_16;
typedef	uint32_t    UINT_32;
typedef	uint64_t	UINT_64;

typedef struct STRECALIB {
	INT_16	SsFctryOffX ;
	INT_16	SsFctryOffY ;
	INT_16	SsRecalOffX ;
	INT_16	SsRecalOffY ;
	INT_16	SsDiffX ;
	INT_16	SsDiffY ;
} stReCalib ;

int onsemi_is_ois_on(struct cam_ois_ctrl_t * ctrl);
int onsemi_ois_go_on(struct cam_ois_ctrl_t * ctrl);
int onsemi_ois_go_off(struct cam_ois_ctrl_t * ctrl);
int onsemi_get_ois_state(struct cam_ois_ctrl_t * ctrl, uint32_t *state);
int onsemi_restore_ois_state(struct cam_ois_ctrl_t * ctrl, uint32_t state);
int onsemi_is_servo_on(struct cam_ois_ctrl_t * ctrl);
int onsemi_ssc_go_on(struct cam_ois_ctrl_t *ctrl);

void get_module_name_from_fw_id(uint32_t fw_id, char * module_name);

void onsemi_dump_state(struct cam_ois_ctrl_t * ctrl, char * state_buf, uint32_t size);
void onsemi_check_sequence_read(struct cam_ois_ctrl_t * ctrl);

int onsemi_switch_mode(struct cam_ois_ctrl_t *ctrl, uint8_t mode);

void onsemi_shift_dword_data(uint32_t* data, uint32_t size);
//ASUS_BSP Lucien +++: Save one Gyro data after doing OIS calibration
int onsemi_gyro_read_xy(struct cam_ois_ctrl_t * ctrl, uint32_t *x_value, uint32_t *y_value);
//ASUS_BSP Lucien ---: Save one Gyro data after doing OIS calibration
int onsemi_af_dac_setting(struct cam_ois_ctrl_t *ctrl, uint32_t val);
int onsemi_read_pair_sensor_data(struct cam_ois_ctrl_t * ctrl,
								 uint32_t reg_addr_x,uint32_t reg_addr_y,
								 uint32_t *value_x,uint32_t *value_y);

uint8_t onsemi_gyro_calibration(struct cam_ois_ctrl_t * ctrl, stReCalib *pReCalib);

uint8_t f40_need_update_fw(uint32_t current_fw_version, uint32_t actuator_version, uint8_t force_update);
int32_t f40_update_fw(struct cam_ois_ctrl_t *ctrl, uint32_t mode, uint8_t module_vendor, uint8_t vcm, uint32_t* updated_version);

uint8_t F40_IORead32A(struct cam_ois_ctrl_t *ctrl, uint32_t IOadrs, uint32_t*IOdata);
uint8_t F40_WaitProcess(struct cam_ois_ctrl_t *ctrl, uint32_t sleep_us, const char * func);
uint8_t	F40_FlashDownload( struct cam_ois_ctrl_t *ctrl, uint8_t, uint8_t, uint8_t ) ;

int32_t onsemi_handle_i2c_dword_write(struct cam_ois_ctrl_t * ctrl,struct cam_sensor_i2c_reg_setting * setting);
int32_t onsemi_get_50cm_to_10cm_lens_shift(uint32_t* shift_value);
int32_t onsemi_get_10cm_lens_shift(uint32_t* shift_value);
int32_t onsemi_lens_shift_to_distance(uint32_t shift_value, uint32_t* distance_cm);
int32_t onsemi_config_ssc_gain(struct cam_ois_ctrl_t * ctrl, uint32_t distance_cm);

int32_t onsemi_get_calibration_data(struct cam_ois_ctrl_t * ctrl, uint32_t* cal_data, uint32_t size);

#endif
