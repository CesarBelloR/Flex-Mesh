/*
 * Copyright (c) 2023 EXACT Technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ETC_SENSOR_H_
#define ETC_SENSOR_H_

#include <zephyr/device.h>
#include "events/sensor_event.h"

void etc_sensor_init(void);
float etc_sensor_get_ambient_temp(void);
float etc_sensor_get_probe_temp(enum sensor_input input);
uint16_t etc_sensor_get_battery(void);
void etc_sensor_run_acquistion(void);

#endif /*  ETC_SENSOR_H_ */
