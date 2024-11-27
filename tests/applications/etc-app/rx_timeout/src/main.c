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

LOG_MODULE_REGISTER(rx_timeout, CONFIG_ETC_APP_LOG_LEVEL);

static void *test_setup(void)
{
	etc_settings_init();
	return NULL;
}

ZTEST(rx_timeout_test, test_01_on_boot_up)
{
	zassert_equal(etc_get_device_mode(), ETC_SETTING_DEVICE_MODE_DEFAULT);
	zassert_equal(etc_get_rx_timeout_secs(), ETC_SETTING_RX_TIMEOUT_SECS_DEFAULT);
}

ZTEST_SUITE(rx_timeout_test, NULL, test_setup, NULL, NULL, NULL);