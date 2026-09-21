/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Simulated probe readings substituted into a sample (FW-1234).
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "etc_sensor_sim.h"

/* An arbitrary reading the physical getters are taken to have produced. */
#define PHYSICAL_C 21.5f

/** @brief Fill @p data as the physical getters leave it: every slot the same. */
static void physical_sample(struct sensor_data *data)
{
	memset(data, 0, sizeof(*data));
	for (unsigned int in = 0; in < SENSOR_INPUT_MAX; in++) {
		data->sensor[in] = PHYSICAL_C;
	}
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	etc_sensor_sim_clear(BIT_MASK(SENSOR_INPUT_MAX));
}

ZTEST_SUITE(etc_sensor_sim, NULL, NULL, before, NULL, NULL);

ZTEST(etc_sensor_sim, test_nothing_simulated_leaves_the_sample_alone)
{
	struct sensor_data data;

	physical_sample(&data);

	zassert_equal(etc_sensor_sim_active_mask(), 0);

	etc_sensor_sim_apply(&data);
	for (unsigned int in = 0; in < SENSOR_INPUT_MAX; in++) {
		zassert_equal(data.sensor[in], PHYSICAL_C, "input %u was overwritten", in);
	}
}

ZTEST(etc_sensor_sim, test_set_overwrites_only_its_own_slot)
{
	struct sensor_data data;

	physical_sample(&data);

	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_IN1, 30.0f));
	zassert_equal(etc_sensor_sim_active_mask(), BIT(SENSOR_INPUT_IN1));

	etc_sensor_sim_apply(&data);
	zassert_equal(data.sensor[SENSOR_INPUT_IN1], 30.0f);
	for (unsigned int in = SENSOR_INPUT_IN2; in < SENSOR_INPUT_MAX; in++) {
		zassert_equal(data.sensor[in], PHYSICAL_C, "input %u was overwritten", in);
	}
}

ZTEST(etc_sensor_sim, test_several_inputs_accumulate_in_the_mask)
{
	struct sensor_data data;

	physical_sample(&data);

	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_IN3, -5.5f));
	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_HUMID, 40.0f));
	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_AMBIENT, 18.25f));
	zassert_equal(etc_sensor_sim_active_mask(),
		      BIT(SENSOR_INPUT_IN3) | BIT(SENSOR_INPUT_HUMID) | BIT(SENSOR_INPUT_AMBIENT));

	etc_sensor_sim_apply(&data);
	zassert_equal(data.sensor[SENSOR_INPUT_IN3], -5.5f);
	zassert_equal(data.sensor[SENSOR_INPUT_HUMID], 40.0f);
	zassert_equal(data.sensor[SENSOR_INPUT_AMBIENT], 18.25f);
	zassert_equal(data.sensor[SENSOR_INPUT_IN4], PHYSICAL_C);
}

ZTEST(etc_sensor_sim, test_set_twice_keeps_the_latest_value)
{
	struct sensor_data data;

	physical_sample(&data);

	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_IN1, 20.0f));
	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_IN1, 30.0f));
	zassert_equal(etc_sensor_sim_active_mask(), BIT(SENSOR_INPUT_IN1));

	etc_sensor_sim_apply(&data);
	zassert_equal(data.sensor[SENSOR_INPUT_IN1], 30.0f);
}

ZTEST(etc_sensor_sim, test_clear_releases_only_the_masked_inputs)
{
	struct sensor_data data;

	physical_sample(&data);

	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_IN1, 30.0f));
	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_IN2, 31.0f));

	etc_sensor_sim_clear(BIT(SENSOR_INPUT_IN1));
	zassert_equal(etc_sensor_sim_active_mask(), BIT(SENSOR_INPUT_IN2));

	etc_sensor_sim_apply(&data);
	zassert_equal(data.sensor[SENSOR_INPUT_IN1], PHYSICAL_C,
		      "a released input must fall back to the physical reading");
	zassert_equal(data.sensor[SENSOR_INPUT_IN2], 31.0f);
}

ZTEST(etc_sensor_sim, test_clear_all_releases_every_input)
{
	struct sensor_data data;

	physical_sample(&data);

	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_IN8, 30.0f));
	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_HUMID, 40.0f));

	etc_sensor_sim_clear(BIT_MASK(SENSOR_INPUT_MAX));
	zassert_equal(etc_sensor_sim_active_mask(), 0);

	etc_sensor_sim_apply(&data);
	zassert_equal(data.sensor[SENSOR_INPUT_IN8], PHYSICAL_C);
	zassert_equal(data.sensor[SENSOR_INPUT_HUMID], PHYSICAL_C);
}

/* An unplugged probe is simulated by the sentinel the physical getters use. */
ZTEST(etc_sensor_sim, test_no_connected_sentinels_are_accepted)
{
	struct sensor_data data;

	physical_sample(&data);

	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_IN1, (float)SENSOR_TEMP_NO_CONNECTED));
	zassert_ok(etc_sensor_sim_set(SENSOR_INPUT_HUMID, (float)SENSOR_HUMID_NO_CONNECTED));

	etc_sensor_sim_apply(&data);
	zassert_false(sensor_temperature_is_valid(data.sensor[SENSOR_INPUT_IN1]));
	zassert_false(sensor_humidity_is_valid(data.sensor[SENSOR_INPUT_HUMID]));
}

ZTEST(etc_sensor_sim, test_input_out_of_range_is_rejected)
{
	zassert_equal(etc_sensor_sim_set(SENSOR_INPUT_MAX, 30.0f), -EINVAL);
	zassert_equal(etc_sensor_sim_set((enum sensor_input)(-1), 30.0f), -EINVAL);
	zassert_equal(etc_sensor_sim_active_mask(), 0);
}
