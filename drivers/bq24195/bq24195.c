/***************************************************************************/
/*!
\file       bq24195.c
\brief      BQ24195 Power Management IC

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include <device.h>
#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <kernel.h>
#include <logging/log.h>
#include <sys/util.h>
#include "bq24195.h"

LOG_MODULE_REGISTER(BQ24195, CONFIG_BQ24195_LOG_LEVEL);

#define BQ24195_I2C_7BIT_ADDR (0x6B)
#define BQ24195_VERSION_ID (0x23)

/* BQ24195 registers */
enum {
    BQ24195_INPUT_SOURCE_REG = 0x00,
    BQ24195_POWERON_CONFIG_REG = 0x01,
    BQ24195_CHARGE_CURRENT_CONTROL_REG = 0x02,
    BQ24195_PRECHARGE_CURRENT_CONTROL_REG = 0x03,
    BQ24195_CHARGE_VOLTAGE_CONTROL_REG = 0x04,
    BQ24195_CHARGE_TIMER_CONTROL_REG = 0x05,
    BQ24195_THERMAL_REG_CONTROL_REG = 0x06,
    BQ24195_MISC_CONTROL_REG = 0x07,
    BQ24195_SYSTEM_STATUS_REG = 0x08,
    BQ24195_FAULT_REG = 0x009,
    BQ24195_PMIC_VERSION_REG = 0x0A,
};

enum BQ24195_LIMIT_MASK {
    CURRENT_LIM_100 = 0x00,
    CURRENT_LIM_150,
    CURRENT_LIM_500,
    CURRENT_LIM_900,
    CURRENT_LIM_1200,
    CURRENT_LIM_1500,
    CURRENT_LIM_2000,
    CURRENT_LIM_3000,
};

struct bq24195_config {
	const struct device *i2c_dev;
	uint8_t addr;
};

static struct bq24195_config m_bq24195_config;

#ifndef round
#define round(x)     ((x)>=0?(long)((x)+0.5):(long)((x)-0.5))
#endif

static int read_register(uint8_t addr, uint8_t *val)
{
	const struct bq24195_config *cfg = &m_bq24195_config;

	int rc = i2c_write_read(cfg->i2c_dev, cfg->addr,
				&addr, sizeof(addr),
				val, 1);

	return rc;
}

static int write_register(uint8_t addr, uint8_t value)
{
	const struct bq24195_config *cfg = &m_bq24195_config;
	int rc = 0;

	uint8_t tx_buf[2] = {addr, value};

	rc = i2c_write(cfg->i2c_dev, tx_buf, sizeof(tx_buf), cfg->addr);

	return rc;
}

int bq24195_init(void)
{
    int ret = 0;
    uint8_t reg = 0x00;

    struct bq24195_config *cfg = &m_bq24195_config;

    cfg->i2c_dev = (struct device*)device_get_binding("I2C_0");
    if (cfg->i2c_dev == NULL) {
        LOG_ERR("Failed to get device_get_binding I2C_0");
        return -EINVAL;
    }

    cfg->addr = BQ24195_I2C_7BIT_ADDR;

    ret = read_register(BQ24195_PMIC_VERSION_REG, &reg);
    if (ret != 0) {
        LOG_ERR("Failed to read reg BQ24195_PMIC_VERSION_REG error %d", ret);
        return ret;
    }

    if (reg != BQ24195_VERSION_ID) {
        return -ENOTSUP;
    }

    return 0;
}

int bq24195_enable_charge(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    
    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0) {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0xFD;

    return write_register(BQ24195_MISC_CONTROL_REG, mask | 0x02);
}

int bq24195_enable_boost_mode(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_POWERON_CONFIG_REG, &reg);
    if (ret != 0) {
        LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0xCF;
    // Enable PMIC Boost Mode
    ret = write_register(BQ24195_POWERON_CONFIG_REG, mask | 0x20);
    if (ret != 0) {
        LOG_ERR("Failed to write reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    ret = read_register(BQ24195_CHARGE_TIMER_CONTROL_REG, &reg);
    if (ret != 0) {
        LOG_ERR("Failed to read reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
        return ret;
    }
    
    if (reg == -1) {
        return 0;
    }

    mask = reg & 0x7F;
    ret = write_register(BQ24195_CHARGE_TIMER_CONTROL_REG, mask);
    if (ret != 0) {
        LOG_ERR("Failed to write reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
        return ret;
    }

    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0) {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    mask = reg & 0xFC;

    ret = write_register(BQ24195_MISC_CONTROL_REG, mask | 0x03);
    if (ret != 0) {
        LOG_ERR("Failed to write reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    k_sleep(K_MSEC(500));
    return 0;
}

int bq24195_disable_charge(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_POWERON_CONFIG_REG, &reg);
    if (ret != 0) {
        LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0xCF;

    ret = write_register(BQ24195_POWERON_CONFIG_REG, mask);
    if (ret != 0) {
        LOG_ERR("Failed to write reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    ret = read_register(BQ24195_CHARGE_TIMER_CONTROL_REG, &reg);
    if (ret != 0) {
        LOG_ERR("Failed to read reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
        return ret;
    }
    
    if (reg == -1) {
        return 0;
    }

    mask = reg & 0x7F;
    ret = write_register(BQ24195_CHARGE_TIMER_CONTROL_REG, mask);
    if (ret != 0) {
        LOG_ERR("Failed to write reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
        return ret;
    }

    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0) {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    mask = reg & 0xFC;

    return write_register(BQ24195_MISC_CONTROL_REG, mask); 
}

int bq24195_disable_boost_mode(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_POWERON_CONFIG_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    if (reg == -1)
    {
        return 0;
    }

    uint8_t mask = reg & 0xCF;

    return write_register(BQ24195_POWERON_CONFIG_REG, mask | 0x10);
}

int bq24195_enable_buck(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_INPUT_SOURCE_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_INPUT_SOURCE_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    return write_register(BQ24195_INPUT_SOURCE_REG, (reg & 0b01111111));
}

int bq24195_disable_buck(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_INPUT_SOURCE_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_INPUT_SOURCE_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    return write_register(BQ24195_INPUT_SOURCE_REG, (reg & 0b10000000));
}

int bq24195_set_input_current_limit(float current) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_INPUT_SOURCE_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_INPUT_SOURCE_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0xF8;
    uint8_t current_val = CURRENT_LIM_100;

    if (current > 0.015) {
        current_val = CURRENT_LIM_150;
    }
    if (current >= 0.5) {
        current_val = CURRENT_LIM_500;
    }
    if (current >= CURRENT_LIM_900) {
        current_val = CURRENT_LIM_900;
    }
    if (current >= 1.2) {
        current_val = CURRENT_LIM_1200;
    }
    if (current >= 1.5) {
        current_val = CURRENT_LIM_1500;
    }
    if (current >= 2.0) {
        current_val = CURRENT_LIM_2000;
    }
    if (current >= 3.0) {
        current_val = CURRENT_LIM_3000;
    }

    return write_register(BQ24195_INPUT_SOURCE_REG, (mask | current_val));
}

int bq24195_get_input_current_limit(float* current) {
    int ret = 0;
    uint8_t reg = 0x00;
    *current = -1;
    ret = read_register(BQ24195_INPUT_SOURCE_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_INPUT_SOURCE_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return -1;
    }

    uint8_t mask = reg & 0x07;
    
    switch (mask) {
        case CURRENT_LIM_100:
            *current = 0.1;
            break;
        case CURRENT_LIM_150:
            *current = 0.015;
            break;
        case CURRENT_LIM_500:
            *current = 0.5;
            break;
        case CURRENT_LIM_900:
            *current = 0.9;
            break;
        case CURRENT_LIM_1200:
            *current = 1.2;
            break;
        case CURRENT_LIM_1500:
            *current = 1.5;
            break;
        case CURRENT_LIM_2000:
            *current = 2.0;
            break;
        case CURRENT_LIM_3000:
            *current = 3.0;
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

int bq24195_set_input_voltage_limit(float voltage) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_INPUT_SOURCE_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_INPUT_SOURCE_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0x87;
    if (voltage > 5.08) {
        voltage = 5.08;
    } else if (voltage < 3.88) {
        voltage = 3.88;
    }
    return write_register(BQ24195_INPUT_SOURCE_REG, (mask | (round((voltage - 3.88) * 100) & 0x78)));
}

int bq24195_get_input_voltage_limit(float* voltage) {
    int ret = 0;
    uint8_t reg = 0x00;
    *voltage = -1;
    ret = read_register(BQ24195_INPUT_SOURCE_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_INPUT_SOURCE_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return -1;
    }

    uint8_t mask = reg & 0x78;
    *voltage = (mask / 100.0f) + 3.88;
    return 0;
}

int bq24195_reset_watchdog(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_POWERON_CONFIG_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    return write_register(BQ24195_POWERON_CONFIG_REG, (reg | 0b01000000));
}

int bq24195_set_minimum_system_voltage(float voltage) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_POWERON_CONFIG_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0xF0;
    if (voltage > 3.7f) {
        voltage = 3.7f;
    } else if (voltage < 3.0f) {
        voltage = 3.0f;
    }

    return write_register(BQ24195_POWERON_CONFIG_REG, (mask | ((round((voltage - 3.0) * 10) * 2) + 1)));
}

int bq24195_get_minimum_system_voltage(float* voltage) {
    int ret = 0;
    uint8_t reg = 0x00;
    *voltage = -1;
    ret = read_register(BQ24195_POWERON_CONFIG_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return -1;
    }

    uint8_t mask = reg & 0x0E;
    *voltage = (mask  / 20.0f) + 3.0f;
    return 0;
}

int bq24195_set_charge_current(float current) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_CHARGE_CURRENT_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_CHARGE_CURRENT_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0x01;

    if (current > 4.544) {
        current = 4.544;
    } else if (current < 0.512) {
        current = 0.512;
    }

    return write_register(BQ24195_CHARGE_CURRENT_CONTROL_REG, (round(((current - 0.512) / 0.016)) & 0xFC) | mask);
}

int bq24195_get_charge_current(float* current) {
    int ret = 0;
    uint8_t reg = 0x00;
    *current = -1;
    ret = read_register(BQ24195_CHARGE_CURRENT_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_CHARGE_CURRENT_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return -1;
    }

    uint8_t mask = reg & 0xFC;
    *current = ((mask * 0.016f) + 0.512f);
    return 0;
}

int bq24195_set_precharge_current(float current) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_PRECHARGE_CURRENT_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_PRECHARGE_CURRENT_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0x0F;
    if (current > 2.048) {
        current = 2.048;
    } else if (current < 0.128) {
        current = 0.128;
    }
    return write_register(BQ24195_PRECHARGE_CURRENT_CONTROL_REG, mask | (round((current - 0.128f) / 0.008f) & 0xF0));
}

int bq24195_get_precharge_current(float* current) {
    int ret = 0;
    uint8_t reg = 0x00;
    *current = -1;
    ret = read_register(BQ24195_PRECHARGE_CURRENT_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_PRECHARGE_CURRENT_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return -1;
    }

    uint8_t mask = reg & 0xF0;
    *current = (mask * 0.008f) + 0.128f;
    return 0;
}

int bq24195_set_termcharge_current(float current) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_PRECHARGE_CURRENT_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_PRECHARGE_CURRENT_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0x0F;
    if (current > 2.048) {
        current = 2.048;
    } else if (current < 0.128) {
        current = 0.128;
    }
    return write_register(BQ24195_PRECHARGE_CURRENT_CONTROL_REG, mask | (round((current - 0.128) / 0.128) & 0x0F));
}

int bq24195_get_termcharge_current(float* current) {
    int ret = 0;
    uint8_t reg = 0x00;
    *current = -1;
    ret = read_register(BQ24195_PRECHARGE_CURRENT_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_PRECHARGE_CURRENT_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return -1;
    }

    uint8_t mask = reg & 0xF0;
    *current = (mask * 0.128f) + 0.128f;
    return 0;
}

int bq24195_set_charge_voltage(float voltage) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_CHARGE_VOLTAGE_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_CHARGE_VOLTAGE_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0x03;
    if (voltage > 4.512) {
        voltage = 4.512;
    } else if (voltage < 3.504) {
        voltage = 3.504;
    }
    return write_register(BQ24195_CHARGE_VOLTAGE_CONTROL_REG, (round(((voltage - 3.504) / 0.004)) & 0xFC) | mask);
}

int bq24195_get_charge_voltage(float* voltage) {
    int ret = 0;
    uint8_t reg = 0x00;
    *voltage = 0.0;
    ret = read_register(BQ24195_CHARGE_VOLTAGE_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_CHARGE_VOLTAGE_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return -1;
    }

    uint8_t mask = reg & 0xFC;
    *voltage = ((mask * 0.004f) + 3.504f);
    return 0;
}

int bq24195_disable_watchdog(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_CHARGE_TIMER_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    return write_register(BQ24195_CHARGE_TIMER_CONTROL_REG, (reg & 0b11001110));
}

int bq24195_enable_dpdm(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    return write_register(BQ24195_MISC_CONTROL_REG, (reg | 0b10000000));
}

int bq24195_disable_dpdm(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    return write_register(BQ24195_MISC_CONTROL_REG, (reg | 0b01111111));
}

int bq24195_enable_batfet(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    return write_register(BQ24195_MISC_CONTROL_REG, (reg | 0b11011111));
}

int bq24195_disable_batfet(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    return write_register(BQ24195_MISC_CONTROL_REG, (reg | 0b00100000));
}

int bq24915_enable_charge_fault_interrupt(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0xFD;

    return write_register(BQ24195_MISC_CONTROL_REG, mask | 0x02);
}

int bq24195_disable_charge_fault_interrupt(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0xFD;

    return write_register(BQ24195_MISC_CONTROL_REG, mask);
}

int bq24195_enable_bat_fault_interrupt(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0xFE;

    return write_register(BQ24195_MISC_CONTROL_REG, mask | 0x01);
}

int bq24195_disable_bat_fault_interrupt(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_MISC_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
        return ret;
    }

    if (reg == -1) {
        return 0;
    }

    uint8_t mask = reg & 0xFE;

    return write_register(BQ24195_MISC_CONTROL_REG, mask);
}

int bq24195_usb_mode(int* status) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_SYSTEM_STATUS_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_SYSTEM_STATUS_REG error %d", ret);
        return ret;
    }

    uint8_t mask = reg & 0xC0;
    *status = mask;
    return 0;
}

int bq24195_charge_status(int* status) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_SYSTEM_STATUS_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_SYSTEM_STATUS_REG error %d", ret);
        return ret;
    }

    uint8_t mask = reg & 0x30;
    *status = mask;
    return 0;
}

int bq24195_is_battery_connected(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_SYSTEM_STATUS_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_SYSTEM_STATUS_REG error %d", ret);
        return ret;
    }

    if ((reg & 0x08) == 0x08) {
        return 1;
    }

    return 0;
}

int bq24195_is_power_good(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_SYSTEM_STATUS_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_SYSTEM_STATUS_REG error %d", ret);
        return ret;
    }

    if (reg & 0b00000100) {
        return 1;
    }

    return 0;
}

int bq24195_is_hot(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_SYSTEM_STATUS_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_SYSTEM_STATUS_REG error %d", ret);
        return ret;
    }

    if (reg & 0b00000010) {
        return 1;
    }

    return 0;
}

int bq24195_can_run_on_battery(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_SYSTEM_STATUS_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_SYSTEM_STATUS_REG error %d", ret);
        return ret;
    }

    if (reg & 0x01) {
        return 1;
    }

    return 0;
}

int bq24195_is_watchdog_expired(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_FAULT_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_FAULT_REG error %d", ret);
        return ret;
    }

    if (reg & 0x80) {
        return 1;
    }

    return 0;
}

int bq24195_get_charge_fault(int *fault) {
    int ret = 0;
    uint8_t reg = 0x00;
    *fault = 0x00;
    ret = read_register(BQ24195_FAULT_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_FAULT_REG error %d", ret);
        return ret;
    }

    *fault = reg & 0x30;
    return 0;
}

int bq24195_is_battery_is_over_voltage(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_FAULT_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_FAULT_REG error %d", ret);
        return ret;
    }

    if (reg & 0x08) {
        return 1;
    }

    return 0;
}

int bq24195_has_battery_temperature_fault(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_FAULT_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_FAULT_REG error %d", ret);
        return ret;
    }

    ret = bq24195_disable_otg();
    if (ret != 0) 
    {
        LOG_ERR("Failed to read reg bq24195_disable_OTG error %d", ret);
        return ret;
    }

    return reg & 0x07;
}

int bq24195_set_thermal_regulation_temp(int degree) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_THERMAL_REG_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_THERMAL_REG_CONTROL_REG error %d", ret);
        return ret;
    }

    uint8_t mask = reg & 0xFC;
    if (degree > 120) {
        degree = 120;
    }  else if (degree < 60) {
        degree = 60;
    }
    return write_register(BQ24195_THERMAL_REG_CONTROL_REG, (mask | (round((degree - 60) / 20) & 0x03)));
}

int bq24195_get_thermal_regulation_temp(int* degree) {
    int ret = 0;
    uint8_t reg = 0x00;
    *degree = 0;
    ret = read_register(BQ24195_THERMAL_REG_CONTROL_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_THERMAL_REG_CONTROL_REG error %d", ret);
        return ret;
    }

    uint8_t mask = reg & 0x03;
    *degree = ((mask * 20) + 60);
    return 0;
}

int bq24195_enable_charging(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_POWERON_CONFIG_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    reg = reg & 0b11001111;
    reg = reg | 0b00010000;
    return write_register(BQ24195_POWERON_CONFIG_REG, reg);
}

int bq24195_disable_charging(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_POWERON_CONFIG_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    return write_register(BQ24195_POWERON_CONFIG_REG, reg & 0xCF);
}

int bq24195_enable_otg(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_POWERON_CONFIG_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    reg = reg & 0b11001111;
    reg = reg | 0b00100000;
    ret = write_register(BQ24195_POWERON_CONFIG_REG, reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to write reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    k_sleep(K_MSEC(220));
    return 0;
}

int bq24195_disable_otg(void) {
    int ret = 0;
    uint8_t reg = 0x00;
    ret = read_register(BQ24195_POWERON_CONFIG_REG, &reg);
    if (ret != 0)
    {
        LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
        return ret;
    }

    reg = reg & 0b11001111;
    reg = reg | 0b00010000;
    return write_register(BQ24195_POWERON_CONFIG_REG, reg);
}