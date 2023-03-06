#ifndef ETC_SETTINGS_H__
#define ETC_SETTINGS_H__
#include <stdint.h>

#define ETC_SETTINGS_DEVICE_ID_LEN (32)
#define ETC_SETTING_FW_VER_LEN	   (8)
#define ETC_SETTING_HW_VER_LEN	   (8)

int etc_settings_init(void);
void etc_settings_refresh();
void etc_set_hw_version(const char *hw_version);
void etc_set_fw_version(const char *fw_version);
void etc_set_device_id(const char *device_id);
void etc_set_time_measurement_interval(int time_in_sec);
void etc_set_time_transmission_interval(int time_in_sec);
void etc_set_time_last_log(int time);
void etc_set_time_last_tx(int time);
int etc_get_hw_version(char *buf, int buf_len);
int etc_get_fw_version(char *buf, int buf_len);
int etc_get_device_id(char *buf, int buf_len);
int etc_get_time_measurement_interval(void);
int etc_get_time_transmission_interval(void);
int etc_get_time_last_log(void);
int etc_get_time_last_tx(void);
#endif /* ETC_SETTINGS_H__ */