#ifndef ETC_SETTINGS_H__
#define ETC_SETTINGS_H__
#include <stdint.h>

void etc_settings_refresh();
void etc_set_hw_version(const char* hw_version);
void etc_set_fw_version(const char* fw_version);
void etc_set_device_id(const char* device_id);
void etc_set_time_measurement_interval(int time_in_sec);
void etc_set_time_transmission_interval(int time_in_sec);
char* etc_get_hw_version(void);
char* etc_get_fw_version(void);
char* etc_get_device_id(void);
int etc_get_time_measurement_interval(void);
int etc_get_time_transmission_interval(void);

#endif /* ETC_SETTINGS_H__ */