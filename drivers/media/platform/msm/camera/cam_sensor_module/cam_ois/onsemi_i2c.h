#ifndef ASUS_OIS_ONSEMI_I2C_H
#define ASUS_OIS_ONSEMI_I2C_H
#include <linux/types.h>
#include "cam_ois_dev.h"

int onsemi_read_byte(struct cam_ois_ctrl_t * ctrl,uint32_t reg_addr, uint32_t* reg_data);
int onsemi_read_word(struct cam_ois_ctrl_t * ctrl,uint32_t reg_addr, uint32_t* reg_data);
int onsemi_read_dword(struct cam_ois_ctrl_t * ctrl,uint32_t reg_addr, uint32_t* reg_data);
int onsemi_read_seq_bytes(struct cam_ois_ctrl_t * ctrl,uint32_t reg_addr, uint8_t* reg_data, uint32_t size);

int onsemi_poll_byte(struct cam_ois_ctrl_t * ctrl,uint32_t reg_addr, uint16_t reg_data, uint32_t delay_ms);
int onsemi_poll_word(struct cam_ois_ctrl_t * ctrl,uint32_t reg_addr, uint16_t reg_data, uint32_t delay_ms);

int onsemi_write_byte(struct cam_ois_ctrl_t * ctrl,uint32_t reg_addr, uint32_t reg_data);
int onsemi_write_word(struct cam_ois_ctrl_t * ctrl,uint32_t reg_addr, uint32_t reg_data);
int onsemi_write_dword(struct cam_ois_ctrl_t * ctrl,uint32_t reg_addr, uint32_t reg_data);
int onsemi_write_seq_bytes(struct cam_ois_ctrl_t * ctrl,uint32_t reg_addr, uint8_t* reg_data,uint32_t size);
#endif
