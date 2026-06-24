/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */
#include "etc_settings.h"

static enum etc_device_type mock_device_type = ETC_DEVICE_TYPE_AMBIENT;
static int16_t mock_rsrp = -103;
static enum etc_power_mode_e mock_power_mode = ETC_POWER_MODE_INTERVAL;

int16_t etc_get_functional_test_rsrp_value(void)
{
	return mock_rsrp;
}

int etc_set_functional_test_rsrp_value(int16_t value)
{
	mock_rsrp = value;
	return 0;
}

enum etc_device_type etc_get_device_type(void)
{
	return mock_device_type;
}

int etc_set_power_mode(enum etc_power_mode_e power)
{
	mock_power_mode = power;
	return 0;
}

void etc_settings_sync_config(void)
{
}

void mock_settings_set_device_type(enum etc_device_type type)
{
	mock_device_type = type;
}

void mock_settings_set_rsrp(int16_t rsrp)
{
	mock_rsrp = rsrp;
}

enum etc_power_mode_e mock_settings_get_power_mode(void)
{
	return mock_power_mode;
}
