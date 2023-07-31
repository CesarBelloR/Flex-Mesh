/*
 * Copyright (c) 2023 EXACT Technology
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_MAX31888_H_
#define ZEPHYR_DRIVERS_SENSOR_MAX31888_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/w1.h>
#include <zephyr/sys/util_macro.h>

#define MAX31888_CMD_CONVERT_T         		0x44
#define MAX31888_CMD_WRITE_REGISTER    		0xCC
#define MAX31888_CMD_READ_REGISTER     		0x33
#define MAX31888_CMD_FIFO_DATA_REGISTER   	0x08

struct max31888_fifo {
	int16_t temp;
	uint16_t crc;
};

struct max31888_config {
	const struct device *bus;
	uint8_t family;
};

struct max31888_data {
	struct w1_slave_config config;
	struct max31888_fifo fifo;
	bool lazy_loaded;
};

static inline const struct device *max31888_bus(const struct device *dev)
{
	const struct max31888_config *dcp = dev->config;

	return dcp->bus;
}

#endif /* ZEPHYR_DRIVERS_SENSOR_MAX31888_H_ */
