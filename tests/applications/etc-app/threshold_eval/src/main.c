/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Edge-triggered evaluation of the immediate report thresholds (FW-1179),
 * per the trigger semantics in docs/specs/lwm2m-immediate-report-thresholds.md.
 */

#include <math.h>

#include <zephyr/ztest.h>

#include "threshold_eval.h"

/* The hold-off the evaluator is fed, unless a test passes its own. */
#define INTERVAL_S  900U
#define INTERVAL_MS ((int64_t)INTERVAL_S * MSEC_PER_SEC)

static struct threshold_eval state;
static struct etc_threshold cfg[ETC_THRESHOLD_SLOT_COUNT];

/** @brief A sample with every port disconnected. */
static struct sensor_data no_readings(void)
{
	struct sensor_data data = {0};

	for (int port = 0; port < SENSOR_EVENT_NUM_DEV_MAX; port++) {
		data.sensor[port] = SENSOR_TEMP_NO_CONNECTED;
	}
	data.sensor[SENSOR_INPUT_HUMID] = SENSOR_HUMID_NO_CONNECTED;
	return data;
}

static void arm(uint8_t slot, uint8_t value_type, uint8_t alert_type, float value)
{
	cfg[slot].enabled = true;
	cfg[slot].value_type = value_type;
	cfg[slot].alert_type = alert_type;
	cfg[slot].value = value;
}

static struct threshold_eval_result feed_interval(const struct sensor_data *data, uint8_t changed,
						  int64_t now_ms, uint32_t interval_s)
{
	struct threshold_eval_result out;

	threshold_eval_update(&state, cfg, changed, data, now_ms, interval_s, &out);
	return out;
}

static struct threshold_eval_result feed(const struct sensor_data *data, uint8_t changed,
					 int64_t now_ms)
{
	return feed_interval(data, changed, now_ms, INTERVAL_S);
}

/** @brief Feed one port reading at a time far enough apart to clear the hold-off. */
static struct threshold_eval_result feed_port(enum sensor_input port, float reading,
					      uint8_t changed, int64_t now_ms)
{
	struct sensor_data data = no_readings();

	data.sensor[port] = reading;
	return feed(&data, changed, now_ms);
}

/** @brief Unarmed evaluator, all slots cleared to the temperature value type. */
static void reset_state(void)
{
	threshold_eval_init(&state);
	memset(cfg, 0, sizeof(cfg));
	for (uint8_t slot = 0; slot < ETC_THRESHOLD_SLOT_COUNT; slot++) {
		cfg[slot].value_type = ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE;
	}
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	reset_state();
}

ZTEST_SUITE(threshold_eval, NULL, NULL, before, NULL, NULL);

ZTEST(threshold_eval, test_exceeds_crosses_on_the_rising_edge_only)
{
	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	zassert_equal(feed_port(SENSOR_INPUT_IN1, 10.0f, 0, 0).crossed, 0);
	zassert_equal(feed_port(SENSOR_INPUT_IN1, 20.0f, 0, 1000).crossed, 0);

	struct threshold_eval_result out = feed_port(SENSOR_INPUT_IN1, 25.0f, 0, 2000);

	zassert_equal(out.crossed, BIT(0));
	zassert_true(out.request_upload);

	zassert_equal(feed_port(SENSOR_INPUT_IN1, 30.0f, 0, 3000).crossed, 0);
}

ZTEST(threshold_eval, test_drops_below_crosses_on_the_falling_edge_only)
{
	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_DROPS_BELOW, 0.0f);

	zassert_equal(feed_port(SENSOR_INPUT_IN1, 10.0f, 0, 0).crossed, 0);
	zassert_equal(feed_port(SENSOR_INPUT_IN1, 0.0f, 0, 1000).crossed, 0);

	struct threshold_eval_result out = feed_port(SENSOR_INPUT_IN1, -5.0f, 0, 2000);

	zassert_equal(out.crossed, BIT(0));
	zassert_true(out.request_upload);

	zassert_equal(feed_port(SENSOR_INPUT_IN1, -10.0f, 0, 3000).crossed, 0);
}

ZTEST(threshold_eval, test_arming_while_already_beyond_triggers_once)
{
	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	zassert_equal(feed_port(SENSOR_INPUT_IN1, 25.0f, 0, 0).crossed, BIT(0));
	zassert_equal(feed_port(SENSOR_INPUT_IN1, 25.0f, 0, 1000).crossed, 0);
}

ZTEST(threshold_eval, test_rearm_requires_returning_past_the_threshold)
{
	struct threshold_eval_result out;

	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	zassert_equal(feed_port(SENSOR_INPUT_IN1, 25.0f, 0, 0).crossed, BIT(0));
	zassert_equal(feed_port(SENSOR_INPUT_IN1, 26.0f, 0, 1000).crossed, 0);
	zassert_equal(feed_port(SENSOR_INPUT_IN1, 15.0f, 0, 2000).crossed, 0);

	out = feed_port(SENSOR_INPUT_IN1, 25.0f, 0, INTERVAL_MS);
	zassert_equal(out.crossed, BIT(0));
}

ZTEST(threshold_eval, test_ports_are_evaluated_independently)
{
	struct sensor_data data = no_readings();

	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	data.sensor[SENSOR_INPUT_IN1] = 25.0f;
	data.sensor[SENSOR_INPUT_IN3] = 10.0f;
	zassert_equal(feed(&data, 0, 0).crossed, BIT(0));

	/* IN1 stays beyond, so the second report comes from IN3 alone. */
	data.sensor[SENSOR_INPUT_IN3] = 25.0f;
	zassert_equal(feed(&data, 0, INTERVAL_MS).crossed, BIT(0));

	/* The splitter sub-ports carry their own state too. */
	data.sensor[SENSOR_INPUT_IN5] = 25.0f;
	zassert_equal(feed(&data, 0, 2 * INTERVAL_MS).crossed, BIT(0));

	data.sensor[SENSOR_INPUT_IN8] = 25.0f;
	zassert_equal(feed(&data, 0, 3 * INTERVAL_MS).crossed, BIT(0));

	zassert_equal(feed(&data, 0, 4 * INTERVAL_MS).crossed, 0);
}

ZTEST(threshold_eval, test_value_type_selects_the_ports)
{
	struct sensor_data data = no_readings();

	arm(0, ETC_THRESHOLD_VALUE_TYPE_HUMIDITY, ETC_THRESHOLD_ALERT_EXCEEDS, 50.0f);
	arm(1, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 50.0f);

	/* A temperature port beyond the humidity value does not reach slot 0. */
	data.sensor[SENSOR_INPUT_IN1] = 60.0f;
	zassert_equal(feed(&data, 0, 0).crossed, BIT(1));

	/* Humidity beyond the temperature value does not reach slot 1. */
	data = no_readings();
	data.sensor[SENSOR_INPUT_HUMID] = 60.0f;
	zassert_equal(feed(&data, 0, INTERVAL_MS).crossed, BIT(0));
}

ZTEST(threshold_eval, test_ambient_is_not_an_input_port)
{
	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	zassert_equal(feed_port(SENSOR_INPUT_AMBIENT, 25.0f, 0, 0).crossed, 0);
}

ZTEST(threshold_eval, test_invalid_reading_drops_the_port_state)
{
	struct threshold_eval_result out;

	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	zassert_equal(feed_port(SENSOR_INPUT_IN1, 25.0f, 0, 0).crossed, BIT(0));
	zassert_equal(feed_port(SENSOR_INPUT_IN1, SENSOR_TEMP_NO_CONNECTED, 0, 1000).crossed, 0);

	out = feed_port(SENSOR_INPUT_IN1, 25.0f, 0, INTERVAL_MS);
	zassert_equal(out.crossed, BIT(0));
}

ZTEST(threshold_eval, test_configuration_change_rearms_the_slot)
{
	struct threshold_eval_result out;

	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);
	zassert_equal(feed_port(SENSOR_INPUT_IN1, 25.0f, 0, 0).crossed, BIT(0));

	/* Still beyond, but against a new value: the slot arms and crosses again. */
	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 21.0f);
	out = feed_port(SENSOR_INPUT_IN1, 25.0f, BIT(0), INTERVAL_MS);
	zassert_equal(out.crossed, BIT(0));

	cfg[0].enabled = false;
	out = feed_port(SENSOR_INPUT_IN1, 25.0f, BIT(0), 2 * INTERVAL_MS);
	zassert_equal(out.crossed, 0);

	cfg[0].enabled = true;
	out = feed_port(SENSOR_INPUT_IN1, 25.0f, BIT(0), 3 * INTERVAL_MS);
	zassert_equal(out.crossed, BIT(0));
}

ZTEST(threshold_eval, test_disabled_slots_and_unsupported_value_types_never_trigger)
{
	struct sensor_data data = no_readings();

	/* Slot 0 is configured but disabled. */
	cfg[0].value_type = ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE;
	cfg[0].alert_type = ETC_THRESHOLD_ALERT_EXCEEDS;
	cfg[0].value = 20.0f;

	data.sensor[SENSOR_INPUT_IN1] = 25.0f;
	data.sensor[SENSOR_INPUT_HUMID] = 80.0f;
	zassert_equal(feed(&data, 0, 0).crossed, 0);

	for (uint8_t value_type = 3; value_type <= 12; value_type++) {
		reset_state();
		arm(1, value_type, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);
		zassert_equal(feed(&data, 0, 0).crossed, 0, "value type %u triggered", value_type);
	}
}

ZTEST(threshold_eval, test_rate_limit_holds_off_uploads_across_slots)
{
	struct sensor_data data = no_readings();
	struct threshold_eval_result out;

	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);
	arm(1, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_DROPS_BELOW, -10.0f);

	data.sensor[SENSOR_INPUT_IN1] = 25.0f;
	out = feed(&data, 0, 0);
	zassert_equal(out.crossed, BIT(0));
	zassert_true(out.request_upload);

	/* IN1 is unchanged, so only slot 1 crosses inside the window: flagged,
	 * but no second upload. */
	data.sensor[SENSOR_INPUT_IN2] = -20.0f;
	out = feed(&data, 0, 1000);
	zassert_equal(out.crossed, BIT(1));
	zassert_false(out.request_upload);

	data.sensor[SENSOR_INPUT_IN2] = 0.0f;
	out = feed(&data, 0, 2000);
	zassert_equal(out.crossed, 0);

	data.sensor[SENSOR_INPUT_IN2] = -20.0f;
	out = feed(&data, 0, INTERVAL_MS);
	zassert_equal(out.crossed, BIT(1));
	zassert_true(out.request_upload);
}

ZTEST(threshold_eval, test_two_slots_of_the_same_value_type_are_independent)
{
	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_DROPS_BELOW, 10.0f);
	arm(1, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	zassert_equal(feed_port(SENSOR_INPUT_IN1, 15.0f, 0, 0).crossed, 0);
	zassert_equal(feed_port(SENSOR_INPUT_IN1, 25.0f, 0, 1000).crossed, BIT(1));
	zassert_equal(feed_port(SENSOR_INPUT_IN1, 5.0f, 0, 2000).crossed, BIT(0));
	zassert_equal(feed_port(SENSOR_INPUT_IN1, 25.0f, 0, 3000).crossed, BIT(1));
}

ZTEST(threshold_eval, test_out_of_range_and_nan_readings_rearm_the_port)
{
	struct threshold_eval_result out;

	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	zassert_equal(feed_port(SENSOR_INPUT_IN1, 25.0f, 0, 0).crossed, BIT(0));
	zassert_equal(feed_port(SENSOR_INPUT_IN1, SENSOR_TEMP_C_MAX + 1.0f, 0, 1000).crossed, 0);

	out = feed_port(SENSOR_INPUT_IN1, 25.0f, 0, INTERVAL_MS);
	zassert_equal(out.crossed, BIT(0));

	out = feed_port(SENSOR_INPUT_IN1, NAN, 0, 2 * INTERVAL_MS);
	zassert_equal(out.crossed, 0);

	out = feed_port(SENSOR_INPUT_IN1, 25.0f, 0, 3 * INTERVAL_MS);
	zassert_equal(out.crossed, BIT(0));
}

ZTEST(threshold_eval, test_value_type_change_rearms_and_switches_ports)
{
	struct sensor_data data = no_readings();

	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 50.0f);

	data.sensor[SENSOR_INPUT_IN1] = 60.0f;
	data.sensor[SENSOR_INPUT_HUMID] = 60.0f;
	zassert_equal(feed(&data, 0, 0).crossed, BIT(0));

	/* The slot now watches humidity, so the already beyond HUMID port crosses
	 * on the re-armed slot. */
	arm(0, ETC_THRESHOLD_VALUE_TYPE_HUMIDITY, ETC_THRESHOLD_ALERT_EXCEEDS, 50.0f);
	zassert_equal(feed(&data, BIT(0), INTERVAL_MS).crossed, BIT(0));

	/* IN1 is no longer part of the slot: re-crossing it produces nothing. */
	data.sensor[SENSOR_INPUT_IN1] = 10.0f;
	zassert_equal(feed(&data, 0, 2 * INTERVAL_MS).crossed, 0);
	data.sensor[SENSOR_INPUT_IN1] = 60.0f;
	zassert_equal(feed(&data, 0, 3 * INTERVAL_MS).crossed, 0);
}

/* FW-1225: the hold-off is an input, so a value written between samples decides
 * the very next one. */
ZTEST(threshold_eval, test_a_shortened_interval_takes_effect_on_the_next_sample)
{
	struct sensor_data data = no_readings();
	struct threshold_eval_result out;

	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	data.sensor[SENSOR_INPUT_IN1] = 25.0f;
	out = feed_interval(&data, 0, 0, INTERVAL_S);
	zassert_true(out.request_upload);

	/* Still inside the long window: flagged only. */
	data.sensor[SENSOR_INPUT_IN1] = 10.0f;
	(void)feed_interval(&data, 0, 60 * MSEC_PER_SEC, INTERVAL_S);
	data.sensor[SENSOR_INPUT_IN1] = 25.0f;
	out = feed_interval(&data, 0, 61 * MSEC_PER_SEC, INTERVAL_S);
	zassert_equal(out.crossed, BIT(0));
	zassert_false(out.request_upload);

	/* A 60 s hold-off written now opens the window the next sample evaluates
	 * against, measured from the last upload request rather than this write. */
	data.sensor[SENSOR_INPUT_IN1] = 10.0f;
	(void)feed_interval(&data, 0, 120 * MSEC_PER_SEC, 60);
	data.sensor[SENSOR_INPUT_IN1] = 25.0f;
	out = feed_interval(&data, 0, 121 * MSEC_PER_SEC, 60);
	zassert_equal(out.crossed, BIT(0));
	zassert_true(out.request_upload);
}

/* A lengthened hold-off applies from the last request, not from the write. */
ZTEST(threshold_eval, test_a_lengthened_interval_holds_off_an_earlier_request)
{
	struct sensor_data data = no_readings();
	struct threshold_eval_result out;

	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	data.sensor[SENSOR_INPUT_IN1] = 25.0f;
	zassert_true(feed_interval(&data, 0, 0, 60).request_upload);

	data.sensor[SENSOR_INPUT_IN1] = 10.0f;
	(void)feed_interval(&data, 0, 100 * MSEC_PER_SEC, INTERVAL_S);
	data.sensor[SENSOR_INPUT_IN1] = 25.0f;
	out = feed_interval(&data, 0, 101 * MSEC_PER_SEC, INTERVAL_S);
	zassert_equal(out.crossed, BIT(0));
	zassert_false(out.request_upload);
}

/* The first crossing after boot is never held off, however long the hold-off is
 * and however late in the uptime it lands. */
ZTEST(threshold_eval, test_the_first_crossing_is_never_held_off)
{
	const uint32_t longest_s = 86400; /* ETC_SETTING_..._REPORT_INTERVAL_SECS_MAX */
	struct sensor_data data = no_readings();

	arm(0, ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE, ETC_THRESHOLD_ALERT_EXCEEDS, 20.0f);

	data.sensor[SENSOR_INPUT_IN1] = 25.0f;
	zassert_true(feed_interval(&data, 0, (int64_t)7 * 86400 * MSEC_PER_SEC, longest_s)
			     .request_upload);
}
