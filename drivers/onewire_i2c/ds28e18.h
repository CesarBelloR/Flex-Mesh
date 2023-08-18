/*
 * Copyright (c) 2023 EXACT Technology
 */

#ifndef ZEPHYR_DRIVERS_I2C_DS28E18_H_
#define ZEPHYR_DRIVERS_I2C_DS28E18_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/w1.h>
#include <zephyr/sys/util_macro.h>

#define DS28E18_CMD_START (0x66)
#define DS28E18_CMD_WRITE_SEQ (0x11)
#define DS28E18_CMD_READ_SEQ (0x22)
#define DS28E18_CMD_RUN_SEQ (0x33)
#define DS28E18_CMD_WRITE_CONFIG (0x55)
#define DS28E18_CMD_READ_CONFIG (0x6A)
#define DS28E18_CMD_WRITE_IO_CONFIG (0x83)
#define DS28E18_CMD_READ_IO_CONFIG (0x7C)
#define DS28E18_CMD_DEV_STATUS (0x7A)

#define DS28E18_CMD_I2C_START_CMD (0x02)
#define DS28E18_CMD_I2C_STOP_CMD (0x03)
#define DS28E18_CMD_I2C_WRITE_DATA_CMD (0xE3)
#define DS28E18_CMD_I2C_READ_DATA_CMD (0xD4)
#define DS28E18_CMD_I2C_READ_DATA_NACK_CMD (0xD3)

#define DS28E18_CMD_RELEASE_BYTE (0xAA)

#define DS28E18_CMD_LEN (128)
#define DS28E18_CMD_BUF_LEN (255)

#define DS28E18_RETURN_VALID (0xAA)
#define DS28E18_RETURN_INVALID (0x77)
#define DS28E18_RETURN_POR (0x44)
#define DS28E18_RETURN_EXE_ERROR (0x55)
#define DS28E18_RETURN_NACK_OCCURRED_ON_DATA (0x88)
#define DS28E18_RETURN_BYTE_POS (2)


struct ds28e18_i2c_data {
	struct w1_slave_config config;
	uint8_t tx_buf[DS28E18_CMD_LEN];
    uint8_t rx_buf[DS28E18_CMD_LEN];
};

struct ds28e18_i2c_config {
	const struct device *bus;
    uint32_t frequency;
	uint8_t family;
	uint16_t concat_buf_size;
	uint16_t flash_buf_max_size;
};

static inline const struct device *ds28e18_bus(const struct device *dev)
{
	const struct ds28e18_i2c_config *dcp = dev->config;

	return dcp->bus;
}

struct ds28e18_config_reg {
    uint8_t spd : 2;
    uint8_t inack : 1;
    uint8_t prot : 1;
    uint8_t spi_mode : 2;
    uint8_t rfu : 2;
};

struct ds28e18_read_reg {
    uint8_t addr_hi : 1;
    uint8_t slen :7;
};

struct ds28e18_rfu_reg {
    uint8_t slen_hi : 2;
    uint8_t rfu : 5;
};


#endif /* ZEPHYR_DRIVERS_I2C_DS28E18_H_ */
