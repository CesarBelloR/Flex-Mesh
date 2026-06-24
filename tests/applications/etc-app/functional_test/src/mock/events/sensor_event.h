/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Minimal mock of events/sensor_event.h for the functional test unit test.
 * Provides only the types referenced by etc_functional_test.c, without
 * pulling in the application event manager.
 */
#ifndef _SENSOR_EVENT_MOCK_H_
#define _SENSOR_EVENT_MOCK_H_

#include <stdint.h>

enum sensor_input {
	SENSOR_INPUT_IN1 = 0,
	SENSOR_INPUT_IN2,
	SENSOR_INPUT_IN3,
	SENSOR_INPUT_IN4,
	SENSOR_INPUT_IN5,
	SENSOR_INPUT_IN6,
	SENSOR_INPUT_IN7,
	SENSOR_INPUT_IN8,
	SENSOR_INPUT_AMBIENT,
	SENSOR_INPUT_HUMID,
	SENSOR_INPUT_MAX
};

#define SENSOR_EVENT_NUM_DEV_MAX SENSOR_INPUT_MAX

struct sensor_data {
	int64_t timestamp;
	float sensor[SENSOR_EVENT_NUM_DEV_MAX];
	uint16_t battery_mV;
	uint8_t battery_status;
};

#endif /* _SENSOR_EVENT_MOCK_H_ */
