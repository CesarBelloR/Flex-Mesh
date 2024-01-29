/*
 * Copyright (c) 2023 EXACT Technology
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ETC_SENSOR_H_
#define ETC_SENSOR_H_

#include <zephyr/device.h>
#include "events/sensor_event.h"

enum etc_sensor_status {
	SENSOR_NO_CONNECTION,
	SENSOR_CONNECTED,
	SENSOR_NA,
};

/* Define a maximum probe sensor in hardware */
#define ETC_SENSOR_NUM_PROBE_SENSOR (4)

typedef void(*etc_sensor_evt_handler_t)(enum etc_sensor_status status);

/**
 * @brief Initialize the sensor system.
 * 
 * This function is responsible for setting up and initializing the sensor system 
 * and setting default values.
 */
void etc_sensor_init(etc_sensor_evt_handler_t handler);

/**
 * @brief Get the ambient temperature.
 * 
 * This function retrieves the temperature of the surrounding environment.
 * 
 * @return the ambient temperature
 */
float etc_sensor_get_ambient_temp(void);

/**
 * @brief Get the probe's temperature based on a specific input.
 * 
 * @param input Specifies which sensor input to read the temperature from.
 * @return the temperature of the specified probe.
 */
float etc_sensor_get_probe_temp(enum sensor_input input);

/**
 * @brief Get the probe's humidity level.
 * 
 * This function retrieves the relative humidity percentage measured by the probe.
 * 
 * @return the humidity level of the probe in percentage.
 */
float etc_sensor_get_probe_humid(void);

/**
 * @brief Get the port probe's humidity index.
 * 
 * This function retrieves an index with the probe's humidity measurement.
 * 
 * @return the humidity index.
 */
int8_t etc_sensor_get_probe_humid_index(void);

/**
 * @brief Get the battery mV.
 * 
 * This function provides information regarding mV of battery.
 * 
 * @return the battery level in mV
 */
uint16_t etc_sensor_get_battery(void);

/**
 * @brief Run or start a new data acquisition cycle.
 * 
 * This function initiates a new round of data collection from the sensors.
 */
void etc_sensor_run_acquisition(void);

/**
 * @brief Get the type of probe based on a specific input.
 * 
 * @param input Specifies which sensor input to determine the probe type from.
 * @return the type of the specified probe.
 */
enum sensor_type etc_sensor_get_probe_type(enum sensor_input input);

/**
 * @brief Get the current probe sensor status 
 * 
 * @return the status of probed sensor (connected or no connect)
 */
enum etc_sensor_status etc_sensor_get_status(void);

/** Get the enter functional test status. This will return true if all four
 * sensor ports report a connected analog sensor and a low level on the 1-wire
 * sensor line.
 * 
*/
bool etc_sensor_get_enter_functional_test(void);

/** Enter the functional test. Turn on sensor power supplies. 
 * 
*/
void etc_sensor_enter_functional_test(void);

/** Exit the functional test. Turn off sensor power supplies.
 * 
*/
void etc_sensor_exit_functional_test(void);
#endif /*  ETC_SENSOR_H_ */
