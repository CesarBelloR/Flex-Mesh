/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Splitter detection and branch sampling in etc_sensor.c. Inputs 1..4 are the A
 * branches of the four front-end ports, inputs 5..8 the B branches, so input 7
 * is what the portal shows as port 3.B.
 */

#include <zephyr/ztest.h>

#include "mock/mock_deps.h"

#include "etc_sensor.h"
#include "events/sensor_event.h"

/* Raw ADC counts chosen so every probe in a test converts to a distinct
 * temperature. */
#define ADC_P1_A 1800
#define ADC_P1_B 1900
#define ADC_P2_A 2000
#define ADC_P2_B 2100
#define ADC_P3	 2200
#define ADC_P4	 2300

static void expect_absent(enum sensor_input input)
{
	float temp = etc_sensor_get_probe_temp(input);

	zassert_false(sensor_temperature_is_valid(temp), "input %d reported %.2f C, expected absent",
		      (int)input + 1, (double)temp);
}

static void expect_probe(enum sensor_input input, int raw_adc)
{
	float temp = etc_sensor_get_probe_temp(input);

	zassert_true(sensor_temperature_is_valid(temp), "input %d absent, expected %.2f C",
		     (int)input + 1, (double)mock_temperature(raw_adc));
	zassert_within(temp, mock_temperature(raw_adc), 0.1f, "input %d reported %.2f C, expected %.2f C",
		       (int)input + 1, (double)temp, (double)mock_temperature(raw_adc));
}

static void acquire(void)
{
	zassert_ok(etc_sensor_run_acquisition(), "acquisition failed");
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	mock_reset();
	etc_sensor_init(NULL);
}

ZTEST_SUITE(etc_sensor_splitter, NULL, NULL, before, NULL, NULL);

/* The configuration from FW-1071: splitters on ports 1 and 2, plain probes
 * wired directly to ports 3 and 4. Ports 3 and 4 have no B branch. */
ZTEST(etc_sensor_splitter, test_probe_without_splitter_has_no_b_branch)
{
	mock_attach_splitter(0, ADC_P1_A, ADC_P1_B);
	mock_attach_splitter(1, ADC_P2_A, ADC_P2_B);
	mock_attach_probe(2, ADC_P3);
	mock_attach_probe(3, ADC_P4);

	acquire();

	expect_probe(SENSOR_INPUT_IN1, ADC_P1_A);
	expect_probe(SENSOR_INPUT_IN2, ADC_P2_A);
	expect_probe(SENSOR_INPUT_IN3, ADC_P3);
	expect_probe(SENSOR_INPUT_IN4, ADC_P4);
	expect_probe(SENSOR_INPUT_IN5, ADC_P1_B);
	expect_probe(SENSOR_INPUT_IN6, ADC_P2_B);
	expect_absent(SENSOR_INPUT_IN7);
	expect_absent(SENSOR_INPUT_IN8);
}

/* Regression for FW-1071: the B branch used to be classified once and never
 * cleared, so a port kept publishing input 5..8 after its splitter was removed,
 * duplicating the A branch reading. */
ZTEST(etc_sensor_splitter, test_removing_a_splitter_clears_its_b_branch)
{
	mock_attach_splitter(2, ADC_P3, ADC_P3 + 400);
	acquire();
	expect_probe(SENSOR_INPUT_IN3, ADC_P3);
	expect_probe(SENSOR_INPUT_IN7, ADC_P3 + 400);

	/* Splitter swapped for a plain probe, without a reboot. */
	mock_attach_probe(2, ADC_P3);
	acquire();

	expect_probe(SENSOR_INPUT_IN3, ADC_P3);
	expect_absent(SENSOR_INPUT_IN7);
}

/* A branch that cannot be selected must be reported absent, never as whichever
 * branch the splitter happened to be pointing at. */
ZTEST(etc_sensor_splitter, test_failed_switch_to_b_does_not_publish_a)
{
	mock_attach_splitter(0, ADC_P1_A, ADC_P1_B);
	mock.port[0].switch_fail_branch = MOCK_BRANCH_B;

	acquire();

	expect_probe(SENSOR_INPUT_IN1, ADC_P1_A);
	expect_absent(SENSOR_INPUT_IN5);
}

/* A splitter that answers but will not actuate leaves the branch position
 * unknown. Neither branch may be published, and in particular the port must not
 * fall back to the no-splitter path, which samples without switching. */
ZTEST(etc_sensor_splitter, test_unreachable_splitter_publishes_neither_branch)
{
	mock_attach_splitter(0, ADC_P1_A, ADC_P1_B);
	acquire();
	expect_probe(SENSOR_INPUT_IN1, ADC_P1_A);

	mock.port[0].switch_fail_branch = MOCK_BRANCH_A;
	acquire();

	expect_absent(SENSOR_INPUT_IN1);
	expect_absent(SENSOR_INPUT_IN5);
}

/* Both branches of a splitter are sampled independently. */
ZTEST(etc_sensor_splitter, test_splitter_branches_report_distinct_values)
{
	mock_attach_splitter(0, ADC_P1_A, ADC_P1_B);
	mock_attach_splitter(1, ADC_P2_A, ADC_P2_B);

	acquire();

	expect_probe(SENSOR_INPUT_IN1, ADC_P1_A);
	expect_probe(SENSOR_INPUT_IN2, ADC_P2_A);
	expect_probe(SENSOR_INPUT_IN5, ADC_P1_B);
	expect_probe(SENSOR_INPUT_IN6, ADC_P2_B);
	expect_absent(SENSOR_INPUT_IN3);
	expect_absent(SENSOR_INPUT_IN4);
	expect_absent(SENSOR_INPUT_IN7);
	expect_absent(SENSOR_INPUT_IN8);
}

/* A splitter with only its A branch populated reports nothing on B. */
ZTEST(etc_sensor_splitter, test_splitter_with_empty_b_branch_reports_absent)
{
	mock_attach_splitter(3, ADC_P4, MOCK_ADC_OPEN);

	acquire();

	expect_probe(SENSOR_INPUT_IN4, ADC_P4);
	expect_absent(SENSOR_INPUT_IN8);
}

/* Nothing plugged in anywhere. */
ZTEST(etc_sensor_splitter, test_all_ports_open_report_absent)
{
	acquire();

	for (enum sensor_input i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN8; i++) {
		expect_absent(i);
	}
}
