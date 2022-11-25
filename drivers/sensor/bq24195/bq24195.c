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
#include <kernel.h>
#include <logging/log.h>
#include <sys/util.h>
#include <drivers/sensor.h>
#include <zephyr/init.h>
#include "bq24195.h"

#define DT_DRV_COMPAT ti_bq24195

LOG_MODULE_REGISTER(BQ24195, CONFIG_BQ24195_LOG_LEVEL);

/* Execute f until it returns 0 or until the number of
 * retries n is exceeded. Log an error if failed after n retries.
 * c needs to be passed an integer counter variable and r is an integer
 * that the return value is saved to.
 * 
*/
#define RETRY_IF_FAIL(f, n, c, r)             			\
	(c) = 0;						\
	while (((c) < (n)) && ((r) = (f) != 0)) {  		\
		(c)++;						\
		k_msleep(50);					\
	}							\
	if ((r) != 0) {						\
		LOG_ERR(#f " failed after %u retries", (n));	\
	}

#define INIT_RETRIES	5
#define	CB_RETRIES	2


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

enum BQ24195_CHG_TIMER_MASK {
	CHARGE_TIMER_5HR = 0x00,
	CHARGE_TIMER_8HR,
	CHARGE_TIMER_12HR,
	CHARGE_TIMER_20HR
};

#ifndef round
#define round(x)     ((x)>=0?(long)((x)+0.5):(long)((x)-0.5))
#endif

static int read_register(const struct device *dev,
			uint8_t addr, uint8_t *val)
{
	const struct bq24195_dev_config *cfg = dev->config;

	int rc = i2c_write_read(cfg->i2c.bus, cfg->i2c.addr,
				&addr, sizeof(addr),
				val, 1);

	return rc;
}

static int write_register(const struct device *dev,
			uint8_t addr, uint8_t value)
{
	const struct bq24195_dev_config *cfg = dev->config;
	int rc = 0;

	uint8_t tx_buf[2] = {addr, value};

	rc = i2c_write(cfg->i2c.bus, tx_buf, sizeof(tx_buf), cfg->i2c.addr);

	return rc;
}

static int read_all_registers(const struct device *dev,
			uint8_t *buf, uint8_t buf_size) 
{
	const struct bq24195_dev_config *cfg = dev->config;
	int rc = 0;
	uint8_t read_size = buf_size <= (BQ24195_PMIC_VERSION_REG + 1) ?
			buf_size : BQ24195_PMIC_VERSION_REG + 1;
	const uint8_t read_addr = 0x00;

	rc = i2c_write_read(cfg->i2c.bus, cfg->i2c.addr, &read_addr, 1,
			buf, read_size);
	
	return rc;
}

static int bq24195_get_fault_reg(const struct device *dev, uint8_t *reg) 
{
	int ret;
	uint8_t fault;

	ret = read_register(dev, BQ24195_FAULT_REG, &fault);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ24195_FAULT_REG error %d", ret);
		return ret;
	}

	if (fault != 0) {
		LOG_WRN("Fault 0x%02x", fault);
	}

	if (reg != NULL) {
		*reg = fault;
	}

	return 0;
}

void bq24195_print_all_registers(const struct device *dev)
{
	uint8_t buf[BQ24195_PMIC_VERSION_REG + 1];
	int rc;

	rc = read_all_registers(dev, buf, sizeof(buf));
	if (rc == 0) {
		LOG_HEXDUMP_DBG(buf, sizeof(buf), "REG");
	} else {
		LOG_ERR("Could not read registers");
	}
}

static void bq24195_work_fn(struct k_work *work)
{
	struct bq24195_data *drv_data =
		CONTAINER_OF(work, struct bq24195_data, work);
	const struct device *dev = drv_data->dev;
	const struct bq24195_dev_config *cfg = dev->config;
	uint16_t curr_lim = 0;
	int count;
	int ret;

	ret = bq24195_is_power_good(drv_data->dev);
	if (ret != 1) {
		return;
	}

	ret = bq24195_get_input_current_limit(drv_data->dev, &curr_lim);
	if (curr_lim != cfg->max_current) {
		LOG_DBG("set cur lim %u to %u", curr_lim, cfg->max_current);
		RETRY_IF_FAIL(
			bq24195_set_input_current_limit(dev, cfg->max_current),
			CB_RETRIES, count, ret);
	}

	bq24195_get_fault_reg(dev, NULL);
}

static void bq24195_gpio_callback(const struct device *dev,
				struct gpio_callback *cb, uint32_t pins)
{
	struct bq24195_data *drv_data = 
		CONTAINER_OF(cb, struct bq24195_data, gpio_cb);

	k_work_submit(&drv_data->work);
}

static int bq24195_init_interrupt (const struct device *dev) 
{
	const struct bq24195_dev_config *cfg = dev->config;
	struct bq24195_data *drv_data = dev->data;
	int ret;

	if (!device_is_ready(cfg->interrupt.port)) {
		LOG_ERR("GPIO port %s not ready", cfg->interrupt.port->name);
		return -EINVAL;
	}

	ret = gpio_pin_configure_dt(&cfg->interrupt, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&drv_data->gpio_cb,
			   bq24195_gpio_callback,
			   BIT(cfg->interrupt.pin));

	ret = gpio_add_callback(cfg->interrupt.port, &drv_data->gpio_cb);
	if (ret < 0) {
		LOG_ERR("Failed to set gpio callback!");
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					      GPIO_INT_EDGE_TO_ACTIVE);

	drv_data->dev = dev;

	k_work_init(&drv_data->work, bq24195_work_fn);

	return ret;
}

static int bq24195_init(const struct device *dev)
{
	int ret = 0;
	uint8_t reg = 0x00;
	int count;
	const struct bq24195_dev_config *cfg = dev->config;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("I2C bus %s not ready!", cfg->i2c.bus->name);
		return -EINVAL;
	}

	ret = read_register(dev, BQ24195_PMIC_VERSION_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ24195_PMIC_VERSION_REG error %d", ret);
		return ret;
	}

	if (reg != BQ24195_VERSION_ID) {
		return -ENOTSUP;
	}

	ret = read_register(dev, BQ24195_SYSTEM_STATUS_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ24195_SYSTEM_STATUS_REG error %d", ret);
		return ret;
	}

	bq24195_get_fault_reg(dev, NULL);

	bq24195_disable_watchdog(dev);

	RETRY_IF_FAIL(
		bq24195_set_charge_current(dev, 
					cfg->charge_current_limit),
		INIT_RETRIES, count, ret);
	RETRY_IF_FAIL(
		bq24195_set_charge_voltage(dev,
					cfg->charge_voltage_limit),
		INIT_RETRIES, count, ret);
	RETRY_IF_FAIL(
		bq24195_set_input_current_limit(dev,
					cfg->max_current),
		INIT_RETRIES, count, ret);
	RETRY_IF_FAIL(
		bq24195_set_input_voltage_limit(dev,
					cfg->min_voltage),
		INIT_RETRIES, count, ret);

	bq24195_enable_disable_charge_timer(dev, cfg->charge_timer_en);
	if (cfg->charge_timer_en) {
		bq24195_set_charge_timer_value(dev, cfg->charge_timer_val);
	}

	bq24195_init_interrupt(dev);

#if (CONFIG_BQ24195_LOG_LEVEL == LOG_LEVEL_DBG)
	bq24195_print_all_registers(dev);
#endif
	return 0;
}

int bq24195_enable_charge(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	
	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0) {
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	uint8_t mask = reg & 0xFD;

	return write_register(dev, BQ24195_MISC_CONTROL_REG, mask | 0x02);
}

int bq24195_enable_boost_mode(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_POWERON_CONFIG_REG, &reg);
	if (ret != 0) {
	LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	uint8_t mask = reg & 0xCF;
	// Enable PMIC Boost Mode
	ret = write_register(dev, BQ24195_POWERON_CONFIG_REG, mask | 0x20);
	if (ret != 0) {
	LOG_ERR("Failed to write reg BQ24195_POWERON_CONFIG_REG error %d", ret);
	return ret;
	}

	ret = read_register(dev, BQ24195_CHARGE_TIMER_CONTROL_REG, &reg);
	if (ret != 0) {
	LOG_ERR("Failed to read reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
	return ret;
	}
	
	if (reg == -1) {
	return 0;
	}

	mask = reg & 0x7F;
	ret = write_register(dev, BQ24195_CHARGE_TIMER_CONTROL_REG, mask);
	if (ret != 0) {
	LOG_ERR("Failed to write reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
	return ret;
	}

	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0) {
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	mask = reg & 0xFC;

	ret = write_register(dev, BQ24195_MISC_CONTROL_REG, mask | 0x03);
	if (ret != 0) {
	LOG_ERR("Failed to write reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	k_sleep(K_MSEC(500));
	return 0;
}

int bq24195_disable_charge(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_POWERON_CONFIG_REG, &reg);
	if (ret != 0) {
	LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	uint8_t mask = reg & 0xCF;

	ret = write_register(dev, BQ24195_POWERON_CONFIG_REG, mask);
	if (ret != 0) {
	LOG_ERR("Failed to write reg BQ24195_POWERON_CONFIG_REG error %d", ret);
	return ret;
	}

	ret = read_register(dev, BQ24195_CHARGE_TIMER_CONTROL_REG, &reg);
	if (ret != 0) {
	LOG_ERR("Failed to read reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
	return ret;
	}
	
	if (reg == -1) {
	return 0;
	}

	mask = reg & 0x7F;
	ret = write_register(dev, BQ24195_CHARGE_TIMER_CONTROL_REG, mask);
	if (ret != 0) {
	LOG_ERR("Failed to write reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
	return ret;
	}

	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0) {
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	mask = reg & 0xFC;

	return write_register(dev, BQ24195_MISC_CONTROL_REG, mask); 
}

int bq24195_disable_boost_mode(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_POWERON_CONFIG_REG, &reg);
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

	return write_register(dev, BQ24195_POWERON_CONFIG_REG, mask | 0x10);
}

int bq24195_enable_buck(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_INPUT_SOURCE_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_INPUT_SOURCE_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	return write_register(dev, BQ24195_INPUT_SOURCE_REG, (reg & 0b01111111));
}

int bq24195_disable_buck(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_INPUT_SOURCE_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_INPUT_SOURCE_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	return write_register(dev, BQ24195_INPUT_SOURCE_REG, (reg & 0b10000000));
}

int bq24195_set_input_current_limit(const struct device *dev, uint16_t current) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_INPUT_SOURCE_REG, &reg);
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

	if (current > 150) {
		current_val = CURRENT_LIM_150;
	}
	if (current >= 500) {
		current_val = CURRENT_LIM_500;
	}
	if (current >= 900) {
		current_val = CURRENT_LIM_900;
	}
	if (current >= 1200) {
		current_val = CURRENT_LIM_1200;
	}
	if (current >= 1500) {
		current_val = CURRENT_LIM_1500;
	}
	if (current >= 2000) {
		current_val = CURRENT_LIM_2000;
	}
	if (current >= 3000) {
		current_val = CURRENT_LIM_3000;
	}

	return write_register(dev, BQ24195_INPUT_SOURCE_REG, (mask | current_val));
}

int bq24195_get_input_current_limit(const struct device *dev, uint16_t* current) {
	int ret = 0;
	uint8_t reg = 0x00;
	*current = -1;
	ret = read_register(dev, BQ24195_INPUT_SOURCE_REG, &reg);
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
		*current = 100;
		break;
	case CURRENT_LIM_150:
		*current = 150;
		break;
	case CURRENT_LIM_500:
		*current = 500;
		break;
	case CURRENT_LIM_900:
		*current = 900;
		break;
	case CURRENT_LIM_1200:
		*current = 1200;
		break;
	case CURRENT_LIM_1500:
		*current = 1500;
		break;
	case CURRENT_LIM_2000:
		*current = 2000;
		break;
	case CURRENT_LIM_3000:
		*current = 3000;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int bq24195_set_input_voltage_limit(const struct device *dev, uint16_t voltage) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_INPUT_SOURCE_REG, &reg);
	if (ret != 0)
	{
		LOG_ERR("Failed to read reg BQ24195_INPUT_SOURCE_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	uint8_t mask = reg & 0x87;
	if (voltage > 5080) {
		voltage = 5080;
	} else if (voltage < 3880) {
		voltage = 3880;
	}

	reg = mask | ((((voltage - 3880) / 80) << 3 & 0x78));
	return write_register(dev, BQ24195_INPUT_SOURCE_REG, 
			reg);
}

int bq24195_get_input_voltage_limit(const struct device *dev, float* voltage) {
	int ret = 0;
	uint8_t reg = 0x00;
	*voltage = -1;
	ret = read_register(dev, BQ24195_INPUT_SOURCE_REG, &reg);
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

int bq24195_reset_watchdog(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_POWERON_CONFIG_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	return write_register(dev, BQ24195_POWERON_CONFIG_REG, (reg | 0b01000000));
}

int bq24195_set_minimum_system_voltage(const struct device *dev, float voltage) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_POWERON_CONFIG_REG, &reg);
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

	return write_register(dev, BQ24195_POWERON_CONFIG_REG, (mask | ((round((voltage - 3.0) * 10) * 2) + 1)));
}

int bq24195_get_minimum_system_voltage(const struct device *dev, float* voltage) {
	int ret = 0;
	uint8_t reg = 0x00;
	*voltage = -1;
	ret = read_register(dev, BQ24195_POWERON_CONFIG_REG, &reg);
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

int bq24195_set_charge_current(const struct device *dev, uint16_t current) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_CHARGE_CURRENT_CONTROL_REG, &reg);
	if (ret != 0)
	{
		LOG_ERR("Failed to read reg BQ24195_CHARGE_CURRENT_CONTROL_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	uint8_t mask = reg & 0x01;

	if (current > 4544) {
		current = 4544;
	} else if (current < 512) {
		current = 512;
	}

	return write_register(dev, BQ24195_CHARGE_CURRENT_CONTROL_REG, (((current - 512) / 16) & 0xFC) | mask);
}

int bq24195_get_charge_current(const struct device *dev, float* current) {
	int ret = 0;
	uint8_t reg = 0x00;
	*current = -1;
	ret = read_register(dev, BQ24195_CHARGE_CURRENT_CONTROL_REG, &reg);
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

int bq24195_set_precharge_current(const struct device *dev, float current) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_PRECHARGE_CURRENT_CONTROL_REG, &reg);
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
	return write_register(dev, BQ24195_PRECHARGE_CURRENT_CONTROL_REG, mask | (round((current - 0.128f) / 0.008f) & 0xF0));
}

int bq24195_get_precharge_current(const struct device *dev, float* current) {
	int ret = 0;
	uint8_t reg = 0x00;
	*current = -1;
	ret = read_register(dev, BQ24195_PRECHARGE_CURRENT_CONTROL_REG, &reg);
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

int bq24195_set_termcharge_current(const struct device *dev, float current) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_PRECHARGE_CURRENT_CONTROL_REG, &reg);
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
	return write_register(dev, BQ24195_PRECHARGE_CURRENT_CONTROL_REG, mask | (round((current - 0.128) / 0.128) & 0x0F));
}

int bq24195_get_termcharge_current(const struct device *dev, float* current) {
	int ret = 0;
	uint8_t reg = 0x00;
	*current = -1;
	ret = read_register(dev, BQ24195_PRECHARGE_CURRENT_CONTROL_REG, &reg);
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

int bq24195_set_charge_voltage(const struct device *dev, uint16_t voltage) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_CHARGE_VOLTAGE_CONTROL_REG, &reg);
	if (ret != 0)
	{
		LOG_ERR("Failed to read reg BQ24195_CHARGE_VOLTAGE_CONTROL_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	uint8_t mask = reg & 0x03;
	if (voltage > 4512) {
		voltage = 4512;
	} else if (voltage < 3504) {
		voltage = 3504;
	}
	return write_register(dev, BQ24195_CHARGE_VOLTAGE_CONTROL_REG, (((voltage - 3504) / 4) & 0xFC) | mask);
}

int bq24195_get_charge_voltage(const struct device *dev, float* voltage) {
	int ret = 0;
	uint8_t reg = 0x00;
	*voltage = 0.0;
	ret = read_register(dev, BQ24195_CHARGE_VOLTAGE_CONTROL_REG, &reg);
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

int bq24195_disable_watchdog(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_CHARGE_TIMER_CONTROL_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	return write_register(dev, BQ24195_CHARGE_TIMER_CONTROL_REG, (reg & 0b11001110));
}

int bq24195_enable_dpdm(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	return write_register(dev, BQ24195_MISC_CONTROL_REG, (reg | 0b10000000));
}

int bq24195_disable_dpdm(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	return write_register(dev, BQ24195_MISC_CONTROL_REG, (reg | 0b01111111));
}

int bq24195_enable_batfet(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	return write_register(dev, BQ24195_MISC_CONTROL_REG, (reg | 0b11011111));
}

int bq24195_disable_batfet(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	return write_register(dev, BQ24195_MISC_CONTROL_REG, (reg | 0b00100000));
}

int bq24915_enable_charge_fault_interrupt(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	uint8_t mask = reg & 0xFD;

	return write_register(dev, BQ24195_MISC_CONTROL_REG, mask | 0x02);
}

int bq24195_disable_charge_fault_interrupt(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	uint8_t mask = reg & 0xFD;

	return write_register(dev, BQ24195_MISC_CONTROL_REG, mask);
}

int bq24195_enable_bat_fault_interrupt(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	uint8_t mask = reg & 0xFE;

	return write_register(dev, BQ24195_MISC_CONTROL_REG, mask | 0x01);
}

int bq24195_disable_bat_fault_interrupt(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_MISC_CONTROL_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_MISC_CONTROL_REG error %d", ret);
	return ret;
	}

	if (reg == -1) {
	return 0;
	}

	uint8_t mask = reg & 0xFE;

	return write_register(dev, BQ24195_MISC_CONTROL_REG, mask);
}

int bq24195_usb_mode(const struct device *dev, int* status) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_SYSTEM_STATUS_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_SYSTEM_STATUS_REG error %d", ret);
	return ret;
	}

	uint8_t mask = reg & 0xC0;
	*status = mask;
	return 0;
}

int bq24195_charge_status(const struct device *dev, int* status) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_SYSTEM_STATUS_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_SYSTEM_STATUS_REG error %d", ret);
	return ret;
	}

	uint8_t mask = reg & 0x30;
	*status = mask;
	return 0;
}

int bq24195_is_battery_connected(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_SYSTEM_STATUS_REG, &reg);
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

int bq24195_is_power_good(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_SYSTEM_STATUS_REG, &reg);
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

int bq24195_is_hot(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_SYSTEM_STATUS_REG, &reg);
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

int bq24195_can_run_on_battery(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_SYSTEM_STATUS_REG, &reg);
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

int bq24195_is_watchdog_expired(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_FAULT_REG, &reg);
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

int bq24195_get_charge_fault(const struct device *dev, int *fault) {
	int ret = 0;
	uint8_t reg = 0x00;
	*fault = 0x00;
	ret = read_register(dev, BQ24195_FAULT_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_FAULT_REG error %d", ret);
	return ret;
	}

	*fault = reg & 0x30;
	return 0;
}

int bq24195_is_battery_is_over_voltage(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_FAULT_REG, &reg);
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

int bq24195_has_battery_temperature_fault(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_FAULT_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_FAULT_REG error %d", ret);
	return ret;
	}

	ret = bq24195_disable_otg(dev);
	if (ret != 0) 
	{
	LOG_ERR("Failed to read reg bq24195_disable_OTG error %d", ret);
	return ret;
	}

	return reg & 0x07;
}

int bq24195_set_thermal_regulation_temp(const struct device *dev, int degree) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_THERMAL_REG_CONTROL_REG, &reg);
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
	return write_register(dev, BQ24195_THERMAL_REG_CONTROL_REG, (mask | (round((degree - 60) / 20) & 0x03)));
}

int bq24195_get_thermal_regulation_temp(const struct device *dev, int* degree) {
	int ret = 0;
	uint8_t reg = 0x00;
	*degree = 0;
	ret = read_register(dev, BQ24195_THERMAL_REG_CONTROL_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_THERMAL_REG_CONTROL_REG error %d", ret);
	return ret;
	}

	uint8_t mask = reg & 0x03;
	*degree = ((mask * 20) + 60);
	return 0;
}

int bq24195_enable_charging(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_POWERON_CONFIG_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
	return ret;
	}

	reg = reg & 0b11001111;
	reg = reg | 0b00010000;
	return write_register(dev, BQ24195_POWERON_CONFIG_REG, reg);
}

int bq24195_disable_charging(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_POWERON_CONFIG_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
	return ret;
	}

	return write_register(dev, BQ24195_POWERON_CONFIG_REG, reg & 0xCF);
}

int bq24195_enable_otg(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_POWERON_CONFIG_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
	return ret;
	}

	reg = reg & 0b11001111;
	reg = reg | 0b00100000;
	ret = write_register(dev, BQ24195_POWERON_CONFIG_REG, reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to write reg BQ24195_POWERON_CONFIG_REG error %d", ret);
	return ret;
	}

	k_sleep(K_MSEC(220));
	return 0;
}

int bq24195_disable_otg(const struct device *dev) {
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ24195_POWERON_CONFIG_REG, &reg);
	if (ret != 0)
	{
	LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
	return ret;
	}

	reg = reg & 0b11001111;
	reg = reg | 0b00010000;
	return write_register(dev, BQ24195_POWERON_CONFIG_REG, reg);
}

int bq24195_enable_disable_charge_timer(const struct device *dev, bool enable) {
	int ret;
	uint8_t reg;

	ret = read_register(dev, BQ24195_CHARGE_TIMER_CONTROL_REG, &reg);
	if (ret != 0) {
	LOG_ERR("Failed to read reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
	return ret;
	}

	if (enable) {
	reg |= 0x08;
	} else {
	reg &= ~0x08;
	}

	ret = write_register(dev, BQ24195_CHARGE_TIMER_CONTROL_REG, reg);
	if (ret != 0) {
	LOG_ERR("Failed to write reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
	return ret;
	}

	return 0;
}

int bq24195_set_charge_timer_value(const struct device *dev, uint8_t hrs)
{
	int ret;
	uint8_t reg;
	enum BQ24195_CHG_TIMER_MASK timer_mask = 0x00;


	if (hrs <= 5) {
	timer_mask = CHARGE_TIMER_5HR;
	} else if (hrs <= 8) {
	timer_mask = CHARGE_TIMER_8HR;
	} else if (hrs <= 12) {
	timer_mask = CHARGE_TIMER_12HR;
	} else if (hrs <= 20) {
	timer_mask = CHARGE_TIMER_20HR;
	} else {
	LOG_ERR("Invalid charge timer value: %u hrs", hrs);
	return -EINVAL;
	}

	ret = read_register(dev, BQ24195_CHARGE_TIMER_CONTROL_REG, &reg);
	if (ret != 0) {
	LOG_ERR("Failed to read reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
	return ret;
	}

	reg &= ~(CHARGE_TIMER_20HR << 1);
	reg |= timer_mask << 1;

	ret = write_register(dev, BQ24195_CHARGE_TIMER_CONTROL_REG, reg);
	if (ret != 0) {
	LOG_ERR("Failed to write reg BQ24195_CHARGE_TIMER_CONTROL_REG error %d", ret);
	return ret;
	}

	return 0;
}

static const struct sensor_driver_api bq24195_api = {
};

struct bq24195_data bq24195_data;

struct bq24195_dev_config bq24195_config = {
	.i2c = I2C_DT_SPEC_INST_GET(0),
	.interrupt = GPIO_DT_SPEC_INST_GET(0, int_gpios),

	.charge_current_limit = DT_INST_PROP(0, charge_current),
        .charge_voltage_limit = DT_INST_PROP(0, charge_voltage),
        .max_current = DT_INST_PROP(0, max_current),
        .min_voltage = DT_INST_PROP(0, min_voltage),
        .charge_timer_en = DT_INST_PROP(0, has_charge_timer),
        .charge_timer_val = DT_INST_PROP(0, charge_timer_val),
};

DEVICE_DT_INST_DEFINE(0, bq24195_init, NULL, &bq24195_data,
		&bq24195_config, POST_KERNEL,
		CONFIG_SENSOR_INIT_PRIORITY, &bq24195_api);