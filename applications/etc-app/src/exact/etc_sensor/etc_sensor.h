/*
 * Copyright (c) 2023 EXACT Technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ETC_SENSOR_H_
#define ETC_SENSOR_H_

#include <zephyr/device.h>

void etc_sensor_init(void);
float etc_sensor_get_ambient_temp(void);
float etc_sensor_get_probe_temp(void);

#endif /*  ETC_SENSOR_H_ */
