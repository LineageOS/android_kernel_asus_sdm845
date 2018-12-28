#ifndef ASUS_CAM_SENSOR_UTIL_H
#define ASUS_CAM_SENSOR_UTIL_H

#include <linux/types.h>
#include <linux/time.h>
#include "cam_sensor_cmn_header.h"

void asus_util_delay_ms(uint32_t time);
void asus_util_delay_us(uint32_t time);
int64_t asus_util_diff_time_us(struct timeval *t1, struct timeval *t2);

int asus_util_fs_read_text_uint8(char *filename, uint8_t *value);
int asus_util_fs_read_text_uint32(char *filename, uint32_t *value);
int asus_util_fs_read_text_byte_seq_hex(char *filename, uint8_t *value, uint32_t size);
int asus_util_fs_read_text_word_seq_hex(char *filename, uint16_t *value, uint32_t size);
int asus_util_fs_read_text_dword_seq_hex(char *filename, uint32_t *value, uint32_t size);
int asus_util_fs_write_byte_seq_text(char *filename, uint8_t *value, uint32_t size);
int asus_util_fs_write_word_seq_text(char *filename, uint16_t *value, uint32_t size);
int asus_util_fs_write_word_seq_text_change_line(char *filename, uint16_t *value, uint32_t size, uint32_t number);
int asus_util_fs_write_dword_seq_text_change_line(char *filename, uint32_t *value, uint32_t size, uint32_t number, bool full_format);

int32_t asus_util_fs_get_file_size(const char *filename, uint64_t* size);
int asus_util_fs_read_file_into_buffer(const char *filename, uint8_t* data, uint32_t size);

int asus_util_format_hex_string(char *output_buf, int len, uint8_t* data, int count);

bool asus_util_i2c_setting_contain_address(struct cam_sensor_i2c_reg_setting * setting, uint32_t addr, uint32_t* data, uint16_t* index);
void asus_util_dump_cam_power_setting(struct cam_sensor_power_ctrl_t *ctrl);
const char * asus_util_cam_op_to_string(uint32_t op_code);

#endif
