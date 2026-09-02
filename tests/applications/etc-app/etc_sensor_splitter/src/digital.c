/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Digital (SHT31 RH probe) sampling in etc_sensor.c, and the functional-test
 * detection it shares a loop with.
 */

#include <zephyr/ztest.h>

#include "mock/mock_deps.h"

#include "etc_sensor.h"
#include "events/sensor_event.h"

#define RH_TEMP	 23.5f
#define RH_HUMID 45.25f
#define ADC_NTC	 2200

static void acquire(void)
{
	zassert_ok(etc_sensor_run_acquisition(), "acquisition failed");
}

static void expect_rh_on_input(enum sensor_input input)
{
	float humid = etc_sensor_get_probe_humid();
	float temp = etc_sensor_get_probe_temp(input);

	zassert_true(sensor_humidity_is_valid(humid), "humidity absent, expected %.2f %%",
		     (double)RH_HUMID);
	zassert_within(humid, RH_HUMID, 0.01f, "humidity %.2f, expected %.2f", (double)humid,
		       (double)RH_HUMID);
	zassert_equal(etc_sensor_get_probe_humid_index(), (int)input,
		      "humidity attributed to input %d, expected %d",
		      etc_sensor_get_probe_humid_index() + 1, (int)input + 1);
	zassert_true(sensor_temperature_is_valid(temp), "input %d has no RH temperature",
		     (int)input + 1);
	zassert_within(temp, RH_TEMP, 0.01f, "input %d temperature %.2f, expected %.2f",
		       (int)input + 1, (double)temp, (double)RH_TEMP);
}

static void expect_no_rh(void)
{
	zassert_false(sensor_humidity_is_valid(etc_sensor_get_probe_humid()),
		      "humidity reported with no readable RH probe");
	zassert_equal(etc_sensor_get_probe_humid_index(), -1, "humidity attributed to a port");
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	mock_reset();
	etc_sensor_init(NULL);
}

ZTEST_SUITE(etc_sensor_digital, NULL, NULL, before, NULL, NULL);

/* Regression for FW-1195: the first port whose 1-Wire line idled high cleared
 * the functional-test flag and was never read, so an RH probe on port 1 was
 * dead until it was moved to another port. */
ZTEST(etc_sensor_digital, test_rh_probe_on_port_one_is_read)
{
	mock_attach_rh_probe(0, RH_TEMP, RH_HUMID);

	acquire();

	expect_rh_on_input(SENSOR_INPUT_IN1);
	zassert_equal(mock.sht31.fetches, 1, "SHT31 fetched %d times", mock.sht31.fetches);
	zassert_false(etc_sensor_get_enter_functional_test(), "functional test entered");
}

ZTEST(etc_sensor_digital, test_rh_probe_on_port_two_with_port_one_open)
{
	mock_attach_rh_probe(1, RH_TEMP, RH_HUMID);

	acquire();

	expect_rh_on_input(SENSOR_INPUT_IN2);
	zassert_equal(mock.sht31.fetches, 1, "SHT31 fetched %d times", mock.sht31.fetches);
}

/* A grounded port must neither be read nor stop the following ports from
 * being read. */
ZTEST(etc_sensor_digital, test_rh_probe_on_port_two_with_port_one_grounded)
{
	mock.port[0].line_low = true;
	mock_attach_rh_probe(1, RH_TEMP, RH_HUMID);

	acquire();

	expect_rh_on_input(SENSOR_INPUT_IN2);
	zassert_equal(mock.sht31.fetches, 1, "SHT31 fetched %d times", mock.sht31.fetches);
	zassert_false(etc_sensor_get_enter_functional_test(), "functional test entered");
}

/* FW-588: a sensor is never read through a grounded line, which hangs the bus. */
ZTEST(etc_sensor_digital, test_rh_probe_on_grounded_port_is_not_read)
{
	mock_attach_rh_probe(0, RH_TEMP, RH_HUMID);
	mock.port[0].line_low = true;
	mock_attach_probe(1, ADC_NTC);

	acquire();

	expect_no_rh();
	zassert_equal(mock.sht31.fetches, 0, "SHT31 read through a grounded line");
	zassert_false(etc_sensor_get_enter_functional_test(), "functional test entered");
}

/* The functional-test jig grounds every port. */
ZTEST(etc_sensor_digital, test_all_ports_grounded_enters_functional_test)
{
	for (int i = 0; i < MOCK_NUM_PORTS; i++) {
		mock.port[i].line_low = true;
	}
	mock_attach_rh_probe(0, RH_TEMP, RH_HUMID);

	acquire();

	expect_no_rh();
	zassert_equal(mock.sht31.fetches, 0, "SHT31 read through a grounded line");
	zassert_true(etc_sensor_get_enter_functional_test(), "functional test not entered");
}

/* An NTC probe on port 1 idles high too, so it must not shadow the RH probe. */
ZTEST(etc_sensor_digital, test_rh_probe_after_ntc_probe_is_read)
{
	mock_attach_probe(0, ADC_NTC);
	mock_attach_rh_probe(2, RH_TEMP, RH_HUMID);

	acquire();

	expect_rh_on_input(SENSOR_INPUT_IN3);
	zassert_true(sensor_temperature_is_valid(etc_sensor_get_probe_temp(SENSOR_INPUT_IN1)),
		     "NTC probe on port 1 lost");
}

ZTEST(etc_sensor_digital, test_lite_reads_rh_probe_on_port_one)
{
	mock.device_type = ETC_DEVICE_TYPE_AMBIENT;
	mock_attach_rh_probe(0, RH_TEMP, RH_HUMID);

	acquire();

	expect_rh_on_input(SENSOR_INPUT_IN1);
	zassert_false(etc_sensor_get_enter_functional_test(), "functional test entered");
}

ZTEST(etc_sensor_digital, test_embeddable_reads_rh_probe_on_port_two)
{
	mock.device_type = ETC_DEVICE_TYPE_EMBEDDABLE;
	mock_attach_rh_probe(1, RH_TEMP, RH_HUMID);

	acquire();

	expect_rh_on_input(SENSOR_INPUT_IN2);
	zassert_false(etc_sensor_get_enter_functional_test(), "functional test entered");
}
