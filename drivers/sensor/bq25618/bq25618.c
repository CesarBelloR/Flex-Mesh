/***************************************************************************/
/*!
\file       bq25618.c
\brief      BQ25618 Power Management IC

\product    General purpose
\processor  ARM Cortex M

\author     EXACT Technology, Andi Gerl
 */
/***************************************************************************/
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/init.h>
#include "bq25618.h"

#define DT_DRV_COMPAT ti_bq25618

LOG_MODULE_REGISTER(BQ25618, CONFIG_BQ25618_LOG_LEVEL);

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


#define BQ25618_I2C_7BIT_ADDR (0x6A)
#define BQ25618_VERSION_ID (0x44)

/* BQ25618 registers */
enum {
	BQ25618_INPUT_CURRENT_LIMIT_REG = 0x00,
	BQ25618_CHARGER_CONTROL0_REG = 0x01,
	BQ25618_CHARGE_CURRENT_LIMIT_REG = 0x02,
	BQ25618_PRECHARGE_CURRENT_LIMIT_REG = 0x03,
	BQ25618_BATTERY_VOLTAGE_LIMIT_REG = 0x04,
	BQ25618_CHARGER_CONTROL1_REG = 0x05,
	BQ25618_CHARGER_CONTROL2_REG = 0x06,
	BQ25618_CHARGER_CONTROL3_REG = 0x07,
	BQ25618_CHARGER_STATUS0_REG = 0x08,
	BQ25618_CHARGER_STATUS1_REG = 0x09,
	BQ25618_CHARGER_STATUS2_REG = 0x0A,
	BQ25618_PART_INFORMATION_REG = 0x0B,
	BQ25618_CHARGER_CONTROL4_REG = 0x0C,
	BQ25618_REG_MAX
};

enum bq25618_chg_timer_mask {
	CHARGE_TIMER_20HR = 0x00,
	CHARGE_TIMER_10HR
};

#ifndef round
#define round(x)     ((x)>=0?(long)((x)+0.5):(long)((x)-0.5))
#endif

static bq25618_evt_handler_t bq25618_evt_cb = NULL;

static int read_register(const struct device *dev,
			uint8_t addr, uint8_t *val)
{
	const struct bq25618_dev_config *cfg = dev->config;

	int rc = i2c_write_read(cfg->i2c.bus, cfg->i2c.addr,
				&addr, sizeof(addr),
				val, 1);

	return rc;
}

static int write_register(const struct device *dev,
			uint8_t addr, uint8_t value)
{
	const struct bq25618_dev_config *cfg = dev->config;
	int rc = 0;

	uint8_t tx_buf[2] = {addr, value};

	rc = i2c_write(cfg->i2c.bus, tx_buf, sizeof(tx_buf), cfg->i2c.addr);

	return rc;
}

static int read_all_registers(const struct device *dev,
			uint8_t *buf, uint8_t buf_size) 
{
	const struct bq25618_dev_config *cfg = dev->config;
	int rc = 0;
	uint8_t read_size = buf_size <= (BQ25618_REG_MAX) ?
			buf_size : BQ25618_REG_MAX;
	const uint8_t read_addr = 0x00;

	pm_device_runtime_get(cfg->i2c.bus);

	rc = i2c_write_read(cfg->i2c.bus, cfg->i2c.addr, &read_addr, 1,
			buf, read_size);

	pm_device_runtime_put(cfg->i2c.bus);
	
	return rc;
}

static uint16_t round_int(uint16_t value, uint16_t divisor) 
{
	uint16_t remainder = value % divisor;
	uint16_t quotient = value / divisor;

	if (remainder >= divisor / 2U) {
		quotient++;
	}
	return quotient * divisor;
}

static uint16_t ceil_int(uint16_t value, uint16_t divisor) 
{
	uint16_t remainder = value % divisor;
	uint16_t quotient = value / divisor;

	if (remainder != 0) {
		quotient++;
	}
	
	return quotient * divisor;
}

static int bq25618_get_fault_reg(const struct device *dev, uint8_t *reg) 
{
	int ret;
	uint8_t fault;

	ret = read_register(dev, BQ25618_CHARGER_STATUS1_REG, &fault);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_STATUS1_REG error %d", ret);
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

void bq25618_print_all_registers(const struct device *dev)
{
	uint8_t buf[BQ25618_REG_MAX];
	int rc;

	rc = read_all_registers(dev, buf, sizeof(buf));
	if (rc == 0) {
		LOG_HEXDUMP_DBG(buf, sizeof(buf), "REG");
	} else {
		LOG_ERR("Could not read registers");
	}
}

void bq25618_register_callback(bq25618_evt_handler_t evt) {
	bq25618_evt_cb = evt;
}

static void bq25618_work_fn(struct k_work *work)
{
	struct bq25618_data *drv_data =
		CONTAINER_OF(work, struct bq25618_data, work);
	const struct device *dev = drv_data->dev;
	const struct bq25618_dev_config *cfg = dev->config;
	uint16_t curr_lim = 0;
	int count;
	int ret;

	/* Poll PMIC status - Notice: Call it before pm get/put. */
	bq25618_poll_status(dev);

	pm_device_runtime_get(cfg->i2c.bus);

	ret = bq25618_get_input_current_limit(drv_data->dev, &curr_lim);
	if (curr_lim != cfg->max_current) {
		LOG_DBG("set cur lim %u to %u", curr_lim, cfg->max_current);
		RETRY_IF_FAIL(
			bq25618_set_input_current_limit(dev, cfg->max_current),
			CB_RETRIES, count, ret);
	}

	bq25618_get_fault_reg(dev, NULL);

	pm_device_runtime_put(cfg->i2c.bus);
}

static void bq25618_gpio_callback(const struct device *dev,
				struct gpio_callback *cb, uint32_t pins)
{
	struct bq25618_data *drv_data = 
		CONTAINER_OF(cb, struct bq25618_data, gpio_cb);
	/* Wait 10ms before running status work */
	k_work_reschedule(&drv_data->work, K_MSEC(10));
}

static int bq25618_init_interrupt(const struct device *dev) 
{
	const struct bq25618_dev_config *cfg = dev->config;
	struct bq25618_data *drv_data = dev->data;
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
			   bq25618_gpio_callback,
			   BIT(cfg->interrupt.pin));

	ret = gpio_add_callback(cfg->interrupt.port, &drv_data->gpio_cb);
	if (ret < 0) {
		LOG_ERR("Failed to set gpio callback!");
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&cfg->interrupt,
					      GPIO_INT_EDGE_TO_ACTIVE);

	drv_data->dev = dev;

	k_work_init_delayable(&drv_data->work, bq25618_work_fn);
	k_mutex_init(&drv_data->status_lock);
	return ret;
}

static int bq25618_init(const struct device *dev)
{
	int ret = 0;
	int retval = 0;
	uint8_t reg = 0x00;
	int count;
	const struct bq25618_dev_config *cfg = dev->config;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("I2C bus %s not ready!", cfg->i2c.bus->name);
		return -EINVAL;
	}

	pm_device_runtime_get(cfg->i2c.bus);

	ret = read_register(dev, BQ25618_PART_INFORMATION_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ24195_PMIC_VERSION_REG error %d", ret);
		return ret;
	}

	if (reg != BQ25618_VERSION_ID) {
		LOG_ERR("Invalid version ID: 0x%02X", reg);
		retval = -ENOTSUP;
		goto exit;
	}

	bq25618_get_fault_reg(dev, NULL);

	bq25618_disable_watchdog(dev);

	RETRY_IF_FAIL(
		bq25618_set_charge_current(dev, 
					cfg->charge_current_limit),
		INIT_RETRIES, count, ret);
	RETRY_IF_FAIL(
		bq25618_set_charge_voltage(dev,
					cfg->charge_voltage_limit),
		INIT_RETRIES, count, ret);
	RETRY_IF_FAIL(
		bq25618_set_input_current_limit(dev,
					cfg->max_current),
		INIT_RETRIES, count, ret);
	RETRY_IF_FAIL(
		bq25618_set_input_voltage_limit(dev,
					cfg->min_voltage),
		INIT_RETRIES, count, ret);
	if (cfg->precharge_current != 0) {
		RETRY_IF_FAIL(
			bq25618_set_precharge_current(dev,
						cfg->precharge_current),
			INIT_RETRIES, count, ret);
	}

	bq25618_enable_disable_charge_timer(dev, cfg->charge_timer_en);
	if (cfg->charge_timer_en) {
		bq25618_set_charge_timer_value(dev, cfg->charge_timer_val);
	}

	bq25618_init_interrupt(dev);

#if (CONFIG_BQ25618_LOG_LEVEL == LOG_LEVEL_DBG)
	bq25618_print_all_registers(dev);
#endif

exit:
	pm_device_runtime_put(cfg->i2c.bus);

	return retval;
}

void bq25618_poll_status(const struct device *dev) 
{
	struct bq25618_data *drv_data = dev->data;
	const struct bq25618_dev_config *cfg = dev->config;

	k_mutex_lock(&drv_data->status_lock, K_FOREVER);
	pm_device_runtime_get(cfg->i2c.bus);

	/* Need to sync immediately */
	uint8_t power_status = 0;
	uint8_t battery_status = 0;
	uint8_t bus_status = 0;
	uint8_t reg = 0x00;
	int ret = read_register(dev, BQ25618_CHARGER_STATUS0_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_STATUS0_REG error %d", ret);
		goto exit;
	}

	LOG_DBG("Reg 0x%02x", reg);
	power_status = (reg >> 2) & 0x01;
	battery_status = (reg >> 3) & 0x03;
	bus_status = (reg >> 5) & 0x07;

	if (bq25618_evt_cb) {
		bq25618_evt_cb(bus_status, battery_status, power_status);
	}
exit:
	pm_device_runtime_put(cfg->i2c.bus);
	k_mutex_unlock(&drv_data->status_lock);
}

int bq25618_enable_buck(const struct device *dev) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ25618_INPUT_CURRENT_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_INPUT_CURRENT_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	return write_register(dev, BQ25618_INPUT_CURRENT_LIMIT_REG, (reg & ~0x80));
}

int bq25618_disable_buck(const struct device *dev) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ25618_INPUT_CURRENT_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_INPUT_CURRENT_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	return write_register(dev, BQ25618_INPUT_CURRENT_LIMIT_REG, (reg | 0x80));
}

int bq25618_set_input_current_limit(const struct device *dev, uint16_t current_ma) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	uint8_t current_val;
	ret = read_register(dev, BQ25618_INPUT_CURRENT_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_INPUT_CURRENT_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	if (current_ma < 100) {
		current_ma = 100;
	} else if (current_ma > 3200) {
		current_ma = 3200;
	}

	uint8_t mask = reg & ~0x1F;
	/* Round to the nearest 100 mA and subtract offset */
	current_val = (round_int(current_ma, 100) - 100U) / 100U;

	if (current_val > 0x1F) {
		current_val = 0x1F;
	}

	return write_register(dev, BQ25618_INPUT_CURRENT_LIMIT_REG, (mask | current_val));
}

int bq25618_get_input_current_limit(const struct device *dev, uint16_t* current_ma) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	*current_ma = -1;
	ret = read_register(dev, BQ25618_INPUT_CURRENT_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_INPUT_CURRENT_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return -1;
	}

	uint8_t mask = reg & 0x1F;
	*current_ma = (mask + 1) * 100U;

	return 0;
}

int bq25618_set_input_voltage_limit(const struct device *dev, uint16_t voltage_mv) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ25618_CHARGER_CONTROL2_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_CONTROL2_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	uint8_t mask = reg & ~0x0F;
	if (voltage_mv > 5400) {
		voltage_mv = 5400;
	} else if (voltage_mv < 3900) {
		voltage_mv = 3900;
	}

	/* Round to the nearest 100 mA */
	voltage_mv = round_int(voltage_mv, 100);

	reg = mask | ((voltage_mv - 3900) / 100);
	return write_register(dev, BQ25618_CHARGER_CONTROL2_REG, 
			      reg);
}

int bq25618_get_input_voltage_limit(const struct device *dev, uint16_t *voltage_mv) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	*voltage_mv = 0;
	ret = read_register(dev, BQ25618_CHARGER_CONTROL2_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_CONTROL2_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return -1;
	}

	uint8_t mask = reg & 0x0F;
	*voltage_mv = (mask * 100U) + 3900U;
	
	return 0;
}

int bq25618_reset_watchdog(const struct device *dev) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ25618_CHARGER_CONTROL0_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_CONTROL0_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	return write_register(dev, BQ25618_CHARGER_CONTROL0_REG, (reg | 0x40));
}

int bq25618_set_minimum_system_voltage(const struct device *dev, uint16_t voltage_mv) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	uint8_t temp;
	ret = read_register(dev, BQ25618_CHARGER_CONTROL0_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_CONTROL0_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	if (voltage_mv > 3700) {
		voltage_mv = 3700;
	} else if (voltage_mv < 2600) {
		voltage_mv = 2600;
	}

	uint8_t mask = reg & ~0x0E;
	/* System minimum voltage setting:
	 * 000 – 2.6 V
	 * 001 – 2.8 V
	 * 010 – 3 V
	 * 011 – 3.2 V
	 * 100 – 3.4 V
	 * 101 – 3.5 V (default)
	 * 110 – 3.6 V
	 * 111 – 3.7 V
	*/
	/* Match the input to a valid value. Round up to ensure that the minimum
	 * voltage condition is always fulfilled.
	 */
	if (voltage_mv >= 3400) {
		voltage_mv = ceil_int(voltage_mv, 100);
		temp = 0x04 | ((voltage_mv - 3400U) / 100U);
	} else {
		voltage_mv = ceil_int(voltage_mv, 200);
		temp = (voltage_mv - 2600U) / 200U;
	}

	reg = (mask | (temp << 1));
	return write_register(dev, BQ25618_CHARGER_CONTROL0_REG, reg);
}

int bq25618_get_minimum_system_voltage(const struct device *dev, uint16_t *voltage_mv) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	*voltage_mv = 0;
	ret = read_register(dev, BQ25618_CHARGER_CONTROL0_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_CONTROL0_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return -1;
	}

	uint8_t mask = (reg & 0x0E) >> 1;
	if (mask & 0x04) {
		*voltage_mv = 3400U + (mask & 0x03) * 100U;
	} else {
		*voltage_mv = 2600U + mask * 200U;
	}
	
	return 0;
}

int bq25618_set_charge_current(const struct device *dev, uint16_t current_ma) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	uint16_t temp;
	ret = read_register(dev, BQ25618_CHARGE_CURRENT_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGE_CURRENT_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	uint8_t mask = reg & ~0x3F;

	if (current_ma > 1500) {
		current_ma = 1500;
	}
	/* Value 1235 is picked to round correctly (average between 1180 and 1290)*/
	if (current_ma < 1235) {
		if (current_ma > 1180) {
			current_ma = 1180;
		}
		temp = round_int(current_ma, 20);
		temp = temp / 20U;
	} else {
		if (current_ma < 1290) {
			current_ma = 1290;
		}
		temp = round_int(current_ma - 1290, 70);
		temp = 0x3C | (temp / 70U);
	}

	mask |= (uint8_t)temp;

	return write_register(dev, BQ25618_CHARGE_CURRENT_LIMIT_REG, mask);
}

int bq25618_get_charge_current(const struct device *dev, uint16_t *current_ma) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	*current_ma = 0;
	ret = read_register(dev, BQ25618_CHARGE_CURRENT_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGE_CURRENT_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return -1;
	}

	uint8_t mask = reg & 0x3F;

	if (mask < 0x3C) {
		*current_ma = mask * 20U;
	} else {
		*current_ma = 1290U + (mask & 0x03) * 70U;
	}

	return 0;
}

int bq25618_set_precharge_current(const struct device *dev, uint16_t current_ma) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	uint16_t temp;
	ret = read_register(dev, BQ25618_PRECHARGE_CURRENT_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_PRECHARGE_CURRENT_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	if (current_ma < 20) {
		current_ma = 20;
	} else if (current_ma > 260) {
		current_ma = 260;
	}

	temp = round_int(current_ma, 20);

	uint8_t mask = reg & ~0xF0;
	mask |= ((temp - 20U) / 20U) << 4;
	
	return write_register(dev, BQ25618_PRECHARGE_CURRENT_LIMIT_REG, mask);
}

int bq25618_get_precharge_current(const struct device *dev, uint16_t *current_ma) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	*current_ma = 0;
	ret = read_register(dev, BQ25618_PRECHARGE_CURRENT_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_PRECHARGE_CURRENT_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return -1;
	}

	uint8_t mask = reg & 0xF0;
	*current_ma = (mask >> 4) * 20U + 20U;

	return 0;
}

int bq25618_set_termcharge_current(const struct device *dev, uint16_t current_ma) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	uint16_t temp;
	ret = read_register(dev, BQ25618_PRECHARGE_CURRENT_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_PRECHARGE_CURRENT_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	if (current_ma < 20) {
		current_ma = 20;
	} else if (current_ma > 260) {
		current_ma = 260;
	}

	temp = round_int(current_ma, 20);

	uint8_t mask = reg & ~0x0F;
	mask |= ((temp - 20U) / 20U);
	
	return write_register(dev, BQ25618_PRECHARGE_CURRENT_LIMIT_REG, mask);
}

int bq25618_get_termcharge_current(const struct device *dev, uint16_t *current_ma) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	*current_ma = 0;
	ret = read_register(dev, BQ25618_PRECHARGE_CURRENT_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_PRECHARGE_CURRENT_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return -1;
	}

	uint8_t mask = reg & 0x0F;
	*current_ma = mask * 20U + 20U;
	
	return 0;
}

int bq25618_set_charge_voltage(const struct device *dev, uint16_t voltage_mv) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	uint8_t temp;
	ret = read_register(dev, BQ25618_BATTERY_VOLTAGE_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_BATTERY_VOLTAGE_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	uint8_t mask = reg & ~0xF8;
	if (voltage_mv > 4520) {
		voltage_mv = 4520;
	} else if (voltage_mv < 3500) {
		voltage_mv = 3500;
	}
	
	if (voltage_mv < 4050) {
		temp = round_int(voltage_mv, 100U);
		temp = (temp - 3500U) / 100U;
	} else if ((voltage_mv >= 4050) && (voltage_mv < 4250)) {
		if (voltage_mv < 4100) {
			voltage_mv = 4100;
		}
		temp = round_int(voltage_mv, 50U);
		temp = 0x06 + (temp - 4100U) / 50U;
	} else {
		if (voltage_mv < 4300) {
			voltage_mv = 4300;
		}
		temp = round_int(voltage_mv, 10U);
		temp = 0x09 + (temp - 4300) / 10U;
	}

	mask |= temp << 3;

	return write_register(dev, BQ25618_BATTERY_VOLTAGE_LIMIT_REG, mask);
}

int bq25618_get_charge_voltage(const struct device *dev, uint16_t *voltage_mv) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	*voltage_mv = 0;
	ret = read_register(dev, BQ25618_BATTERY_VOLTAGE_LIMIT_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_BATTERY_VOLTAGE_LIMIT_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return -1;
	}

	uint8_t mask = reg & 0xF8;
	if (mask < 0x06) {
		*voltage_mv = 3500U + mask * 100U;
	} else if (mask >= 0x06 && mask < 0x09) {
		*voltage_mv = 4100U + (mask - 0x06) * 50U;
	} else {
		*voltage_mv = 4300U + (mask - 0x09) * 10U;
	}
	
	return 0;
}

int bq25618_disable_watchdog(const struct device *dev) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ25618_CHARGER_CONTROL1_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_CONTROL1_REG error %d", ret);
		return ret;
	}

	if (reg == -1) {
		return 0;
	}

	reg &= ~(0x03 << 4);

	return write_register(dev, BQ25618_CHARGER_CONTROL1_REG, reg);
}

int bq25618_charge_status(const struct device *dev, uint8_t *status) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ25618_CHARGER_STATUS0_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_STATUS0_REG error %d", ret);
		return ret;
	}

	*status = (reg >> 3) & 0x03;
	
	return 0;
}

int bq25618_voltage_bus_status(const struct device *dev, uint8_t *status) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ25618_CHARGER_STATUS0_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_STATUS0_REG error %d", ret);
		return ret;
	}

	*status = (reg >> 5) & 0x07;
	
	return 0;
}

int bq25618_is_power_good(const struct device *dev, uint8_t* status) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	*status = 0;
	ret = read_register(dev, BQ25618_CHARGER_STATUS0_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_STATUS0_REG error %d", ret);
		return ret;
	}

	if (reg & 0x04) {
		*status = 1;
	}

	return 0;
}

int bq25618_get_charge_fault(const struct device *dev, uint8_t *fault) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	*fault = 0x00;
	ret = read_register(dev, BQ25618_CHARGER_STATUS1_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_STATUS1_REG error %d", ret);
		return ret;
	}

	*fault = (reg >> 4) & 0x03;

	return 0;
}

int bq25618_enable_charging(const struct device *dev) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ25618_CHARGER_CONTROL0_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_CONTROL0_REG error %d", ret);
		return ret;
	}

	reg |= 0x10;

	return write_register(dev, BQ25618_CHARGER_CONTROL0_REG, reg);
}

int bq24195_disable_charging(const struct device *dev) 
{
	int ret = 0;
	uint8_t reg = 0x00;
	ret = read_register(dev, BQ25618_CHARGER_CONTROL0_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ24195_POWERON_CONFIG_REG error %d", ret);
		return ret;
	}

	reg &= ~0x10;

	return write_register(dev, BQ25618_CHARGER_CONTROL0_REG, reg);
}

int bq25618_enable_disable_charge_timer(const struct device *dev, bool enable) 
{
	int ret;
	uint8_t reg;

	ret = read_register(dev, BQ25618_CHARGER_CONTROL1_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_CONTROL1_REG error %d", ret);
		return ret;
	}

	if (enable) {
		reg |= 0x08;
	} else {
		reg &= ~0x08;
	}

	ret = write_register(dev, BQ25618_CHARGER_CONTROL1_REG, reg);
	if (ret != 0) {
		LOG_ERR("Failed to write reg BQ25618_CHARGER_CONTROL1_REG error %d", ret);
		return ret;
	}

	return 0;
}

int bq25618_set_charge_timer_value(const struct device *dev, uint8_t hrs)
{
	int ret;
	uint8_t reg;
	enum bq25618_chg_timer_mask timer_mask = 0x00;


	if (hrs <= 10) {
		timer_mask = CHARGE_TIMER_10HR;
	} else {
		timer_mask = CHARGE_TIMER_20HR;
	}

	ret = read_register(dev, BQ25618_CHARGER_CONTROL1_REG, &reg);
	if (ret != 0) {
		LOG_ERR("Failed to read reg BQ25618_CHARGER_CONTROL1_REG error %d", ret);
		return ret;
	}

	reg &= ~(CHARGE_TIMER_10HR << 2);
	reg |= timer_mask << 2;

	ret = write_register(dev, BQ25618_CHARGER_CONTROL1_REG, reg);
	if (ret != 0) {
		LOG_ERR("Failed to write reg BQ25618_CHARGER_CONTROL1_REG error %d", ret);
		return ret;
	}

	return 0;
}

static const struct sensor_driver_api bq25618_api = {
};

struct bq25618_data bq25618_data;

struct bq25618_dev_config bq25618_config = {
	.i2c = I2C_DT_SPEC_INST_GET(0),
	.interrupt = GPIO_DT_SPEC_INST_GET(0, int_gpios),

	.charge_current_limit = DT_INST_PROP(0, charge_current),
	.charge_voltage_limit = DT_INST_PROP(0, charge_voltage),
	.max_current = DT_INST_PROP(0, max_current),
	.min_voltage = DT_INST_PROP(0, min_voltage),
	.charge_timer_en = DT_INST_PROP(0, has_charge_timer),
	.charge_timer_val = DT_INST_PROP(0, charge_timer_val),
	.precharge_current = DT_INST_PROP_OR(0, precharge_current, 0),
};

DEVICE_DT_INST_DEFINE(0, bq25618_init, NULL, &bq25618_data,
		&bq25618_config, POST_KERNEL,
		CONFIG_SENSOR_INIT_PRIORITY, &bq25618_api);