/*
 * Copyright (c) 2024 EXACT Technology Corporation
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "etc_device.h"
#include "etc_device_record.h"
#include "etc_settings.h"
#include "etc_util.h"
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(etc_always_on, CONFIG_ETC_APP_LOG_LEVEL);

static void *test_setup(void)
{
	etc_settings_init();
	return NULL;
}

ZTEST(etc_always_on_test, test_01_on_boot_up)
{
	zassert_equal(etc_get_device_mode(), ETC_SETTING_DEVICE_MODE_DEFAULT);
	zassert_equal(etc_get_power_mode(), ETC_SETTING_POWER_MODE_DEFAULT);
}

ZTEST(etc_always_on_test, test_02_on_not_relay_mode)
{
	enum etc_power_mode_e previous_power_mode = etc_get_power_mode();
	/* Not Relay Mode */
	zassert_not_equal(etc_get_device_mode(), ETC_DEVICE_MODE_RELAY);
	etc_set_power_mode(ETC_POWER_MODE_ALWAYS_ON);
	zassert_equal(previous_power_mode, etc_get_power_mode());
}

ZTEST(etc_always_on_test, test_03_on_relay_mode)
{
	/* Set Relay Mode */
	etc_set_device_mode(ETC_DEVICE_MODE_RELAY);
	etc_set_power_mode(ETC_POWER_MODE_ALWAYS_ON);
	zassert_equal(etc_get_power_mode(), ETC_POWER_MODE_ALWAYS_ON);
}

ZTEST(etc_always_on_test, test_04_on_change_mode)
{
	/* Set Relay Mode & Power mode*/
	etc_set_device_mode(ETC_DEVICE_MODE_RELAY);
	etc_set_power_mode(ETC_POWER_MODE_ALWAYS_ON);
	zassert_equal(etc_get_power_mode(), ETC_POWER_MODE_ALWAYS_ON);
	etc_set_power_mode(ETC_POWER_MODE_INTERVAL);
	zassert_equal(etc_get_power_mode(), ETC_POWER_MODE_INTERVAL);
}

ZTEST(etc_always_on_test, test_05_on_switch_mode)
{
	/* Set Relay Mode & Power mode*/
	etc_set_device_mode(ETC_DEVICE_MODE_RELAY);
	etc_set_power_mode(ETC_POWER_MODE_ALWAYS_ON);
	zassert_equal(etc_get_power_mode(), ETC_POWER_MODE_ALWAYS_ON);
	etc_set_device_mode(ETC_DEVICE_MODE_LTE_LOGGER);
	zassert_equal(etc_get_power_mode(), ETC_POWER_MODE_INTERVAL);
}

#if CONFIG_BOARD_ETC
ZTEST(etc_always_on_test, test_06_lora_rx_active)
{
	zassert_equal(etc_get_power_mode(), ETC_POWER_MODE_INTERVAL);
}
#endif

ZTEST_SUITE(etc_always_on_test, NULL, test_setup, NULL, NULL, NULL);