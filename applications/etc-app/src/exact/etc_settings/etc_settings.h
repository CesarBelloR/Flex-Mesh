#ifndef ETC_SETTINGS_H__
#define ETC_SETTINGS_H__
#include <stdint.h>

void etc_settings_refresh();
void etc_set_hw_version(const char* hw_version);
void etc_set_fw_version(const char* fw_version);
void etc_set_device_id(const char* device_id);
char* etc_get_hw_version(void);
char* etc_get_fw_version(void);
char* etc_get_device_id(void);

#endif /* ETC_SETTINGS_H__ */