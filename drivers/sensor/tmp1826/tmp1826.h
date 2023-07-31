/*
 * Copyright (c) 2023 EXACT Technology
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_TMP1826_H_
#define ZEPHYR_DRIVERS_SENSOR_TMP1826_H_

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/w1.h>
#include <zephyr/sys/util_macro.h>

#define TMP1826_CMD_CONVERT_T         0x44
#define TMP1826_CMD_WRITE_SCRATCHPAD  0x4E
#define TMP1826_CMD_READ_SCRATCHPAD   0xBE
#define TMP1826_CMD_COPY_SCRATCHPAD   0x48
#define TMP1826_CMD_RECALL_EEPROM     0xB8
#define TMP1826_CMD_READ_POWER_SUPPLY 0xB4

/**
 * @brief  temperature calculation values.
 * @details Specified temperature calculation values of  Click driver.
 */
#define TMP1826_TEMP_RES_16BIT                 0.0078125f
#define TMP1826_TEMP_RES_12BIT                 0.0625f

/**
 * @brief Status register settings.
 * @details Specified status register settings of  Click driver.
 */
#define TMP1826_STATUS_ALERT_HIGH              0x80
#define TMP1826_STATUS_ALERT_LOW               0x40
#define TMP1826_STATUS_RESERVED                0x30
#define TMP1826_STATUS_DATA_VALID              0x08
#define TMP1826_STATUS_POWER_MODE              0x04
#define TMP1826_STATUS_ARB_DONE                0x02
#define TMP1826_STATUS_LOCK_STATUS             0x01

/**
 * @brief Config 1 register settings.
 * @details Specified config 1 register settings of  Click driver.
 */
#define TMP1826_CFG1_TEMP_FMT_12_BIT           0x00
#define TMP1826_CFG1_TEMP_FMT_16_BIT           0x80
#define TMP1826_CFG1_TEMP_FMT_MASK             0x80
#define TMP1826_CFG1_RESERVED                  0x40
#define TMP1826_CFG1_CONV_TIME_SEL_3MS         0x00
#define TMP1826_CFG1_CONV_TIME_SEL_5p5MS       0x20
#define TMP1826_CFG1_CONV_TIME_SEL_MASK        0x20
#define TMP1826_CFG1_ALERT_MODE_ALERT          0x00
#define TMP1826_CFG1_ALERT_MODE_COMPARATOR     0x10
#define TMP1826_CFG1_ALERT_MODE_MASK           0x10
#define TMP1826_CFG1_AVG_SEL_NO_AVG            0x00
#define TMP1826_CFG1_AVG_SEL_8_B2B_CONV        0x08
#define TMP1826_CFG1_AVG_SEL_MASK              0x08
#define TMP1826_CFG1_CONV_MODE_SEL_ONE_SHOT    0x00
#define TMP1826_CFG1_CONV_MODE_SEL_STACKED     0x01
#define TMP1826_CFG1_CONV_MODE_SEL_AUTO        0x02
#define TMP1826_CFG1_CONV_MODE_SEL_MASK        0x07

/**
 * @brief Config 2 register settings.
 * @details Specified config 2 register settings of  Click driver.
 */
#define TMP1826_CFG2_OD_DIS                    0x00
#define TMP1826_CFG2_OD_EN                     0x80
#define TMP1826_CFG2_OD_MASK                   0x80
#define TMP1826_CFG2_FLEX_ADDR_MODE_HOST       0x00
#define TMP1826_CFG2_FLEX_ADDR_MODE_IO         0x20
#define TMP1826_CFG2_FLEX_ADDR_MODE_RES        0x40
#define TMP1826_CFG2_FLEX_ADDR_MODE_IO_RES     0x60
#define TMP1826_CFG2_FLEX_ADDR_MODE_MASK       0x60
#define TMP1826_CFG2_ARB_MODE_DIS              0x00
#define TMP1826_CFG2_ARB_MODE_SOFT_EN          0x10
#define TMP1826_CFG2_ARB_MODE_FAST_EN          0x18
#define TMP1826_CFG2_ARB_MODE_MASK             0x18
#define TMP1826_CFG2_HYSTERESIS_5_C            0x00
#define TMP1826_CFG2_HYSTERESIS_10_C           0x02
#define TMP1826_CFG2_HYSTERESIS_15_C           0x04
#define TMP1826_CFG2_HYSTERESIS_20_C           0x06
#define TMP1826_CFG2_HYSTERESIS_MASK           0x06
#define TMP1826_CFG2_LOCK_DIS                  0x00
#define TMP1826_CFG2_LOCK_EN                   0x01
#define TMP1826_CFG2_LOCK_MASK                 0x01

/**
 * @brief Default temperature alert and offset values.
 * @details Specified default temperature alert and offset values of  Click driver.
 */
#define TMP1826_TEMP_ALERT_LOW                 5.0f
#define TMP1826_TEMP_ALERT_HIGH                40.0f
#define TMP1826_TEMP_OFFSET                    0.0f


struct tmp1826_scratchpad {
    int16_t temperature;        /**< Raw temperature value (Read only). */
    uint8_t status;             /**< Status value (Read only). */
    uint8_t config_1;           /**< Config 1 setting (Read/Write). */
    uint8_t config_2;           /**< Config 2 setting (Read/Write). */
    uint8_t short_addr;         /**< Short address setting (Read/Write). */
    int16_t temp_limit_low;     /**< Temperature alert limit low setting (Read/Write). */
    int16_t temp_limit_high;    /**< Temperature alert limit high setting (Read/Write). */
    int16_t temp_offset;        /**< Temperature offset setting (Read/Write). */
};

struct tmp1826_config {
	const struct device *bus;
	uint8_t family;
	uint8_t resolution;
};

struct tmp1826_data {
	struct w1_slave_config config;
	struct tmp1826_scratchpad scratchpad;
	bool lazy_loaded;
};

static inline const struct device *tmp1826_bus(const struct device *dev)
{
	const struct tmp1826_config *dcp = dev->config;

	return dcp->bus;
}

#endif /* ZEPHYR_DRIVERS_SENSOR_TMP1826_H_ */
