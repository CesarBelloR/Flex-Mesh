/***************************************************************************/
/*!
\file       bq25618.h
\brief      BQ25618 Power Management IC

\product    General purpose
\processor  ARM Cortex M

\author     EXACT Technology, Andi Gerl
 */
/***************************************************************************/
#ifndef BQ25618_H_
#define BQ25618_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <stdint.h>
#include <zephyr/sys/timeutil.h>

/***************************************************************************/
/* Definitions                                                             */
// Charge Fault Status
#define NO_CHARGE_FAULT 0x00
#define INPUT_OVER_VOLTAGE 0x01
#define THERMAL_SHUTDOWN 0x02
#define CHARGE_SAFETY_TIME_EXPIRED 0x03

// Termal Fault Status
#define NO_TEMPERATURE_FAULT 0x00
#define LOWER_THRESHOLD_TEMPERATURE_FAULT 0x05
#define HIGHER_THRESHOLD_TEMPERATURE_FAULT 0x06

// Charging status
#define NOT_CHARGING 0x00
#define PRE_CHARGING 0x01
#define FAST_CHARGING 0x02
#define CHARGE_TERMINATION_DONE 0x03

// Voltage BUS status
#define NO_INPUT 0x00
#define USB_HOST_MODE 0x01
#define ADAPTER_PORT_MODE 0x03
#define BOOST_MODE 0x07

struct bq25618_data {
	struct gpio_callback gpio_cb;
	const struct device *dev;
	struct k_mutex status_lock;
	struct k_work_delayable work;
};

struct bq25618_dev_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec interrupt;

	uint16_t charge_current_limit;
	uint16_t charge_voltage_limit;
	uint16_t max_current;
	uint16_t min_voltage;
	uint16_t precharge_current;
	bool charge_timer_en;
	uint8_t charge_timer_val;
};

/** @brief Callback function for BQ25618
 * 
*/
typedef void(*bq25618_evt_handler_t)(uint8_t bus_status, 
	uint8_t battery_status, uint8_t power_status);

/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Run the poll job for PMIC
 * 
 * Notice: Call this API before get PM
 */
void bq25618_poll_status(const struct device *dev);
/** @brief Enable the Buck Regulator
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_enable_buck(const struct device *dev);

/** @brief Disable the Buck Regulator
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_disable_buck(const struct device *dev);

/** @brief Sets the upper input current limit for PMIC
 *
 * @param current input current limit expressed in mA
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_set_input_current_limit(const struct device *dev, uint16_t current_ma);

/** @brief Get the PMIC and return the input current limit
 *
 * @param current pointer to where store the return value (current in mA)
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_get_input_current_limit(const struct device *dev, uint16_t *current_ma);

/** @brief Set the lower input voltage limit, the PMIC set a base voltage of 
 * 3.88V plus a combination of bits 3 to 6 of input source control register 
 * where the LSB is 80mV
 *
 * @param voltage input voltage limit expressed in mV
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_set_input_voltage_limit(const struct device *dev, uint16_t voltage_mv);

/** @brief Get the PMIC and return the input voltage limit
 *
 * @param voltage pointer to where store the return value (voltage in mV)
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_get_input_voltage_limit(const struct device *dev, uint16_t *voltage_mv);

/** @brief Reset the I2C Watchdog Timer
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_reset_watchdog(const struct device *dev);

/** @brief Set the mimium acceptable votlage to feed the onboard MCU and module
 *
 * @param voltage mimium system voltage in mV
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_set_minimum_system_voltage(const struct device *dev, uint16_t voltage_mv);

/** @brief Get the minium system voltage in Volt
 *
 * @param voltage pointer to where store the return value (voltage in mV)
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_get_minimum_system_voltage(const struct device *dev, uint16_t *voltage_mv);

/** @brief Set charge current
 *
 * @param current the current expressed in mA
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_set_charge_current(const struct device *dev, uint16_t current_ma);

/** @brief Get the PMIC and return the charge current value in Ampere
 *
 * @param current pointer to where store the return value (current in mA)
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_get_charge_current(const struct device *dev, uint16_t *current_ma);

/** @brief Set PMIC precharge current limit
 *
 * @param current the current expressed in mA
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_set_precharge_current(const struct device *dev, uint16_t current_ma);

/** @brief Get the PMIC and return the precharge current value in Ampere
 *
 * @param current pointer to where store the return value (current in mA)
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_get_precharge_current(const struct device *dev, uint16_t *current_ma);

/** @brief Set PMIC termination charge current limit
 *
 * @param current the current expressed in mA
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_set_termcharge_current(const struct device *dev, uint16_t current_ma);

/** @brief Get the PMIC and return the termination charge current value in Ampere
 *
 * @param current pointer to where store the return value (current in mA)
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_get_termcharge_current(const struct device *dev, uint16_t *current_ma);

/** @brief Set the PMIC charge voltage limit
 *
 * @param voltage charge voltage in mV
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_set_charge_voltage(const struct device *dev, uint16_t voltage_mv);

/** @brief Get the PMIC and return the charge voltage limit in Volt
 *
 * @param current pointer to where store the return value (voltage in mV)
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_get_charge_voltage(const struct device *dev, uint16_t *voltage_mv);

/** @brief Disable Watchdog Timer
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_disable_watchdog(const struct device *dev);

/** @brief Query the PMIC and return the charging status
 *
 * @param status the charging status
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_charge_status(const struct device *dev, uint8_t *status);

/** @brief Query the PMIC and return the voltage bus (VBUS) status
 *
 * @param status the charging status
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_voltage_bus_status(const struct device *dev, uint8_t *status);

/** @brief Check if the power is good
 *
 * @param status 1 if power good, 0 if not good
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_is_power_good(const struct device *dev, uint8_t* status) ;

/** @brief Query the PMIC and return charge fault status
 *
 * @param fault -1 on reading error, 0 if no fault and a fault bits
 * combination on PMIC charge fault.
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_get_charge_fault(const struct device *dev, uint8_t *fault);

/** @brief Enable the battery charger
 * 
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_enable_charging(const struct device *dev);

/** @brief Disable the battery charger
 * 
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq25618_disable_charging(const struct device *dev);

/** @brief Enable or disable the charge timer.
 * 
 * @param enable If true, enable the timer. If false, disable the timer.
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
*/
int bq25618_enable_disable_charge_timer(const struct device *dev, bool enable);

/** @brief Set the charge timer value in hours.
 * Set to the closest equal or lower value allowed by the hardware.
 * Valid values are: 5, 8, 12 and 20 hrs.
 * 
 * @param hrs Charge timer value in hours.
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
*/
int bq25618_set_charge_timer_value(const struct device *dev, uint8_t hrs);

/** @brief Print all registers as HEXDUMP, if LOG_LEVEL_DEBUG or lower.
 * 
*/
void bq25618_print_all_registers(const struct device *dev);

/** @brief Register a callback to update the power charger IC
 * 
*/
void bq25618_register_callback(bq25618_evt_handler_t evt);
#endif /* BQ25618_H_ */