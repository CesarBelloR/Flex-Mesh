/*
 * Copyright (c) 2024 EXACT Technology Corporation
 */

#include <stdbool.h>
#include <string.h>
#include "etc_settings.h"
#include "etc_device.h"
#include <zephyr/ztest.h>
#include <math.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(etc_settings_test, CONFIG_ETC_APP_LOG_LEVEL);

static void *test_setup(void)
{
	etc_device_nvs_init();
	etc_settings_init();

	return NULL;
}

static void test_teardown(void *)
{
}

ZTEST(etc_settings_test, test_get_device_type_relay)
{
	char device_id[ETC_SETTINGS_DEVICE_ID_LEN];
	enum etc_device_type type;
	etc_set_device_id("11000000");
	etc_get_device_id(device_id, sizeof(device_id));
	LOG_HEXDUMP_INF(device_id, sizeof(device_id), "Device ID");
	type = etc_get_device_type();
	zassert_equal(type, ETC_DEVICE_TYPE_RELAY, "Device type should be relay");
}

ZTEST(etc_settings_test, test_get_device_type_embeddable)
{
	enum etc_device_type type;
	etc_set_device_id("12000000");
	type = etc_get_device_type();
	zassert_equal(type, ETC_DEVICE_TYPE_EMBEDDABLE, "Device type should be embeddable");
}

ZTEST(etc_settings_test, test_get_device_type_ambient)
{
	enum etc_device_type type;
	etc_set_device_id("13000000");
	type = etc_get_device_type();
	zassert_equal(type, ETC_DEVICE_TYPE_AMBIENT, "Device type should be ambient");
}

ZTEST(etc_settings_test, test_get_device_type_unknown)
{
	enum etc_device_type type;
	etc_set_device_id("99000000");
	type = etc_get_device_type();
	zassert_equal(type, ETC_DEVICE_TYPE_LOGGER, "Device type should be logger");
}

ZTEST_SUITE(etc_settings_test, NULL, test_setup, NULL, NULL, test_teardown);