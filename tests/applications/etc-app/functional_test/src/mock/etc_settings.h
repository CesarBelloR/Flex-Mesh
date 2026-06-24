/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Minimal mock of etc_settings.h for the functional test unit test.
 */
#ifndef ETC_SETTINGS_MOCK_H_
#define ETC_SETTINGS_MOCK_H_

#include <stdint.h>

enum etc_device_type {
	ETC_DEVICE_TYPE_LOGGER,
	ETC_DEVICE_TYPE_RELAY,
	ETC_DEVICE_TYPE_EMBEDDABLE,
	ETC_DEVICE_TYPE_AMBIENT,
};

enum etc_power_mode_e {
	ETC_POWER_MODE_ALWAYS_ON = 0x00,
	ETC_POWER_MODE_INTERVAL = 0x01,
	ETC_POWER_MODE_PROBE = 0x02,
};

int16_t etc_get_functional_test_rsrp_value(void);
int etc_set_functional_test_rsrp_value(int16_t value);
enum etc_device_type etc_get_device_type(void);
int etc_set_power_mode(enum etc_power_mode_e power);
void etc_settings_sync_config(void);

/* Test-only controls. */
void mock_settings_set_device_type(enum etc_device_type type);
void mock_settings_set_rsrp(int16_t rsrp);
enum etc_power_mode_e mock_settings_get_power_mode(void);

#endif /* ETC_SETTINGS_MOCK_H_ */
