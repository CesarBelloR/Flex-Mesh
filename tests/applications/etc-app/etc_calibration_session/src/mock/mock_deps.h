/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef MOCK_DEPS_H_
#define MOCK_DEPS_H_

#include <stdbool.h>
#include <stdint.h>

#include "etc_sensor.h"

struct mock_state {
	/* Tracks whether the calibration hardware is currently powered, so a test
	 * can assert that teardown happened before the front-end lock was
	 * released. */
	bool hw_powered;
	/* Front-end ownership, mirroring etc_sensor's hw_owner, so a test can
	 * assert a failed check releases it without powering the rail down. */
	enum etc_sensor_hw_owner hw_owner;
	int enter_calls;
	int exit_calls;
	int timeout_notifications;
	/* Selects which switch the ADC/temperature stubs answer for. */
	int8_t last_gpio_mask;
	/* Stubbed return values. */
	int scan_result;
	int read_sn_result;
	uint16_t battery_mv;
	/* Calibrator TMP1826 ambient, and the device's own onboard ambient. Kept
	 * separate so a test can fault one without disturbing the other. */
	float ambient_temp;
	float device_ambient_temp;
	/* Fault injection: non-zero makes the calibrator switch refuse to actuate;
	 * adc_fails makes the sensor ADC report a hard error on every port;
	 * write_setting_result fails the NVS store of the new coefficients. */
	int gpio_mask_result;
	bool adc_fails;
	int write_setting_result;
	/* Value the offset switch reads back. Defaults to MOCK_ADC_OFFSET; set it
	 * negative to reproduce a real board, which reads slightly below zero
	 * there. */
	int adc_offset_value;
};

extern struct mock_state mock;

void mock_reset(void);

#endif /* MOCK_DEPS_H_ */
