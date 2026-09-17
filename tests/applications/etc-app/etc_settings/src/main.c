/*
 * Copyright (c) 2024 EXACT Technology Corporation
 */

#include <stdbool.h>
#include <string.h>
#include "etc_settings.h"
#include "etc_device.h"
#include <zephyr/ztest.h>
#include <math.h>
#include <errno.h>
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

/* Mirror of the private on-flash threshold record, for the raw-record tests */
#define TEST_THRESHOLD_NVS_VERSION 1

struct test_threshold_nvs {
	uint8_t version;
	uint8_t enabled;
	uint8_t value_type;
	uint8_t alert_type;
	float value;
} __packed;

static const uint16_t threshold_ids[ETC_THRESHOLD_SLOT_COUNT] = {
	ETC_SETTING_THRESHOLD_0_ID,
	ETC_SETTING_THRESHOLD_1_ID,
	ETC_SETTING_THRESHOLD_2_ID,
	ETC_SETTING_THRESHOLD_3_ID,
};

/* Drop all stored threshold records and reload the settings */
static void thresholds_reset(void)
{
	for (uint8_t slot = 0; slot < ETC_THRESHOLD_SLOT_COUNT; slot++) {
		etc_device_delete_setting(threshold_ids[slot]);
	}
	etc_settings_init();
}

static void assert_threshold_is_default(const struct etc_threshold *t, uint8_t slot)
{
	zassert_equal(t->enabled, ETC_SETTING_THRESHOLD_ENABLED_DEFAULT,
		      "Slot %u should be disabled", slot);
	zassert_equal(t->value_type, ETC_SETTING_THRESHOLD_VALUE_TYPE_DEFAULT,
		      "Slot %u value type should be default", slot);
	zassert_equal(t->alert_type, ETC_SETTING_THRESHOLD_ALERT_TYPE_DEFAULT,
		      "Slot %u alert type should be default", slot);
	zassert_equal(t->value, ETC_SETTING_THRESHOLD_VALUE_DEFAULT,
		      "Slot %u value should be default", slot);
}

ZTEST(etc_settings_test, test_threshold_defaults)
{
	struct etc_threshold thresholds[ETC_THRESHOLD_SLOT_COUNT];

	thresholds_reset();
	etc_get_thresholds(thresholds);
	for (uint8_t slot = 0; slot < ETC_THRESHOLD_SLOT_COUNT; slot++) {
		assert_threshold_is_default(&thresholds[slot], slot);
	}
}

ZTEST(etc_settings_test, test_threshold_set_get_round_trip)
{
	struct etc_threshold out;

	thresholds_reset();
	for (uint8_t slot = 0; slot < ETC_THRESHOLD_SLOT_COUNT; slot++) {
		struct etc_threshold in = {
			.enabled = true,
			.value_type = (uint8_t)(slot + 1),
			.alert_type = (uint8_t)(slot % 2),
			.value = 10.5f * (slot + 1),
		};

		zassert_ok(etc_set_threshold(slot, &in), "Set slot %u", slot);
		zassert_ok(etc_get_threshold(slot, &out), "Get slot %u", slot);
		zassert_equal(out.enabled, in.enabled, "Slot %u enabled", slot);
		zassert_equal(out.value_type, in.value_type, "Slot %u value type", slot);
		zassert_equal(out.alert_type, in.alert_type, "Slot %u alert type", slot);
		zassert_equal(out.value, in.value, "Slot %u value", slot);
	}
}

ZTEST(etc_settings_test, test_threshold_invalid_slot_and_null)
{
	struct etc_threshold t = {
		.enabled = true,
		.value_type = 1,
		.alert_type = ETC_THRESHOLD_ALERT_EXCEEDS,
		.value = 1.0f,
	};

	zassert_equal(etc_get_threshold(ETC_THRESHOLD_SLOT_COUNT, &t), -EINVAL,
		      "Slot 4 should be rejected");
	zassert_equal(etc_get_threshold(0, NULL), -EINVAL, "NULL should be rejected");
	zassert_equal(etc_set_threshold(ETC_THRESHOLD_SLOT_COUNT, &t), -EINVAL,
		      "Slot 4 should be rejected");
	zassert_equal(etc_set_threshold(0, NULL), -EINVAL, "NULL should be rejected");
	zassert_equal(etc_settings_update_thresholds(NULL), -EINVAL, "NULL should be rejected");
}

ZTEST(etc_settings_test, test_threshold_invalid_values_rejected)
{
	const struct etc_threshold stored = {
		.enabled = true,
		.value_type = 2,
		.alert_type = ETC_THRESHOLD_ALERT_DROPS_BELOW,
		.value = 42.0f,
	};
	struct etc_threshold bad;
	struct etc_threshold out;

	thresholds_reset();
	zassert_ok(etc_set_threshold(0, &stored), "Set slot 0");

	bad = stored;
	bad.value_type = ETC_SETTING_THRESHOLD_VALUE_TYPE_MIN - 1;
	zassert_equal(etc_set_threshold(0, &bad), -EINVAL, "Value type 0 should be rejected");

	bad = stored;
	bad.value_type = ETC_SETTING_THRESHOLD_VALUE_TYPE_MAX + 1;
	zassert_equal(etc_set_threshold(0, &bad), -EINVAL, "Value type 13 should be rejected");

	bad = stored;
	bad.alert_type = ETC_THRESHOLD_ALERT_DROPS_BELOW + 1;
	zassert_equal(etc_set_threshold(0, &bad), -EINVAL, "Alert type 2 should be rejected");

	bad = stored;
	bad.value = NAN;
	zassert_equal(etc_set_threshold(0, &bad), -EINVAL, "NaN should be rejected");

	bad = stored;
	bad.value = INFINITY;
	zassert_equal(etc_set_threshold(0, &bad), -EINVAL, "Infinity should be rejected");

	zassert_ok(etc_get_threshold(0, &out), "Get slot 0");
	zassert_equal(out.enabled, stored.enabled, "Enabled unchanged");
	zassert_equal(out.value_type, stored.value_type, "Value type unchanged");
	zassert_equal(out.alert_type, stored.alert_type, "Alert type unchanged");
	zassert_equal(out.value, stored.value, "Value unchanged");
}

ZTEST(etc_settings_test, test_threshold_persistence)
{
	const struct etc_threshold in = {
		.enabled = true,
		.value_type = 12,
		.alert_type = ETC_THRESHOLD_ALERT_DROPS_BELOW,
		.value = -12.25f,
	};
	struct etc_threshold out;

	thresholds_reset();
	zassert_ok(etc_set_threshold(2, &in), "Set slot 2");

	/* Reload as after a reboot */
	etc_settings_init();

	zassert_ok(etc_get_threshold(2, &out), "Get slot 2");
	zassert_equal(out.enabled, in.enabled, "Enabled persisted");
	zassert_equal(out.value_type, in.value_type, "Value type persisted");
	zassert_equal(out.alert_type, in.alert_type, "Alert type persisted");
	zassert_equal(out.value, in.value, "Value persisted");
}

ZTEST(etc_settings_test, test_threshold_update_all_slots)
{
	struct etc_threshold in[ETC_THRESHOLD_SLOT_COUNT];
	struct etc_threshold applied[ETC_THRESHOLD_SLOT_COUNT];
	struct etc_threshold out[ETC_THRESHOLD_SLOT_COUNT];

	thresholds_reset();
	for (uint8_t slot = 0; slot < ETC_THRESHOLD_SLOT_COUNT; slot++) {
		in[slot].enabled = (slot % 2) == 0;
		in[slot].value_type = (uint8_t)(ETC_SETTING_THRESHOLD_VALUE_TYPE_MAX - slot);
		in[slot].alert_type = (uint8_t)(slot % 2);
		in[slot].value = 5.0f + slot;
	}

	zassert_ok(etc_settings_update_thresholds(in), "Update all slots");
	etc_get_thresholds(out);
	for (uint8_t slot = 0; slot < ETC_THRESHOLD_SLOT_COUNT; slot++) {
		zassert_equal(out[slot].enabled, in[slot].enabled, "Slot %u enabled", slot);
		zassert_equal(out[slot].value_type, in[slot].value_type, "Slot %u value type",
			      slot);
		zassert_equal(out[slot].alert_type, in[slot].alert_type, "Slot %u alert type",
			      slot);
		zassert_equal(out[slot].value, in[slot].value, "Slot %u value", slot);
	}

	memcpy(applied, in, sizeof(applied));

	/* A rejected slot 0 is reported but must not hold back the later slots */
	in[0].value_type = ETC_SETTING_THRESHOLD_VALUE_TYPE_MAX + 1;
	for (uint8_t slot = 1; slot < ETC_THRESHOLD_SLOT_COUNT; slot++) {
		in[slot].enabled = !in[slot].enabled;
		in[slot].value = 100.0f + slot;
	}

	zassert_equal(etc_settings_update_thresholds(in), -EINVAL, "Invalid slot should fail");

	etc_get_thresholds(out);
	zassert_equal(out[0].value_type, applied[0].value_type, "Slot 0 value type unchanged");
	zassert_equal(out[0].value, applied[0].value, "Slot 0 value unchanged");
	for (uint8_t slot = 1; slot < ETC_THRESHOLD_SLOT_COUNT; slot++) {
		zassert_equal(out[slot].enabled, in[slot].enabled, "Slot %u enabled applied", slot);
		zassert_equal(out[slot].value, in[slot].value, "Slot %u value applied", slot);
	}
}

/* The change mask etc_take_thresholds() returns drives the evaluator's re-arming,
 * so it must name exactly the slots a write actually changed.
 */
static struct etc_threshold threshold_of(uint8_t value_type, float value)
{
	struct etc_threshold t = {
		.enabled = true,
		.value_type = value_type,
		.alert_type = ETC_THRESHOLD_ALERT_EXCEEDS,
		.value = value,
	};

	return t;
}

/* Drop the mask left behind by earlier tests. */
static void thresholds_take_changed(void)
{
	struct etc_threshold out[ETC_THRESHOLD_SLOT_COUNT];

	etc_take_thresholds(out);
}

ZTEST(etc_settings_test, test_threshold_change_mask_reports_the_changed_slot)
{
	struct etc_threshold out[ETC_THRESHOLD_SLOT_COUNT];
	const struct etc_threshold in = threshold_of(1, 7.5f);

	thresholds_reset();
	thresholds_take_changed();

	zassert_ok(etc_set_threshold(1, &in), "Set slot 1");
	zassert_equal(etc_take_thresholds(out), BIT(1), "Slot 1 should be reported as changed");
	zassert_equal(etc_take_thresholds(out), 0, "The mask must be cleared when it is read");
}

ZTEST(etc_settings_test, test_threshold_change_mask_ignores_an_identical_write)
{
	struct etc_threshold out[ETC_THRESHOLD_SLOT_COUNT];
	const struct etc_threshold in = threshold_of(2, -3.5f);

	thresholds_reset();
	thresholds_take_changed();

	zassert_ok(etc_set_threshold(0, &in), "Set slot 0");
	zassert_equal(etc_take_thresholds(out), BIT(0), "Slot 0 should be reported as changed");

	zassert_ok(etc_set_threshold(0, &in), "Rewrite slot 0 with the same values");
	zassert_equal(etc_take_thresholds(out), 0, "An unchanged slot must not be reported");
}

ZTEST(etc_settings_test, test_threshold_change_mask_reports_every_changed_slot)
{
	struct etc_threshold out[ETC_THRESHOLD_SLOT_COUNT];
	const struct etc_threshold in = threshold_of(1, 42.0f);

	thresholds_reset();
	thresholds_take_changed();

	zassert_ok(etc_set_threshold(0, &in), "Set slot 0");
	zassert_ok(etc_set_threshold(3, &in), "Set slot 3");
	zassert_equal(etc_take_thresholds(out), BIT(0) | BIT(3), "Both slots should be reported");
}

/* A runtime factory reset reloads every slot, which must re-arm the evaluator
 * the same way a server write does.
 */
ZTEST(etc_settings_test, test_threshold_change_mask_reports_a_reload)
{
	struct etc_threshold out[ETC_THRESHOLD_SLOT_COUNT];

	thresholds_reset();
	thresholds_take_changed();

	etc_settings_init();
	zassert_equal(etc_take_thresholds(out), BIT_MASK(ETC_THRESHOLD_SLOT_COUNT),
		      "A reload should report every slot as changed");
}

ZTEST(etc_settings_test, test_threshold_unknown_nvs_version)
{
	struct test_threshold_nvs record = {
		.version = 0xFF,
		.enabled = 1,
		.value_type = 7,
		.alert_type = ETC_THRESHOLD_ALERT_DROPS_BELOW,
		.value = 99.0f,
	};
	struct etc_threshold out;

	thresholds_reset();
	zassert_ok(etc_device_write_setting(threshold_ids[3], &record, sizeof(record)),
		   "Write raw record");

	etc_settings_init();

	zassert_ok(etc_get_threshold(3, &out), "Get slot 3");
	assert_threshold_is_default(&out, 3);
}

ZTEST(etc_settings_test, test_threshold_stored_value_type_out_of_range)
{
	struct test_threshold_nvs record = {
		.version = TEST_THRESHOLD_NVS_VERSION,
		.enabled = 1,
		.value_type = ETC_SETTING_THRESHOLD_VALUE_TYPE_MAX + 1,
		.alert_type = ETC_THRESHOLD_ALERT_EXCEEDS,
		.value = 5.0,
	};
	struct etc_threshold out;

	thresholds_reset();
	zassert_ok(etc_device_write_setting(threshold_ids[1], &record, sizeof(record)),
		   "Write raw record");

	etc_settings_init();

	zassert_ok(etc_get_threshold(1, &out), "Get slot 1");
	assert_threshold_is_default(&out, 1);
}

ZTEST_SUITE(etc_settings_test, NULL, test_setup, NULL, NULL, test_teardown);