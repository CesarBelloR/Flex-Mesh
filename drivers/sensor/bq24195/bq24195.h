/***************************************************************************/
/*!
\file       bq24195.h
\brief      BQ24195 Power Management IC

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef BQ24195_H_
#define BQ24195_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <stdint.h>
#include <zephyr/sys/timeutil.h>

/***************************************************************************/
/* Definitions                                                             */
// Charge Fault Status
#define NO_CHARGE_FAULT 0x00
#define INPUT_OVER_VOLTAGE 0x10
#define THERMAL_SHUTDOWN 0x20
#define CHARGE_SAFETY_TIME_EXPIRED 0x30

// Termal Fault Status
#define NO_TEMPERATURE_FAULT 0x00
#define LOWER_THRESHOLD_TEMPERATURE_FAULT 0x05
#define HIGHER_THRESHOLD_TEMPERATURE_FAULT 0x06

// Charging status
#define NOT_CHARGING 0x00
#define PRE_CHARGING 0x10
#define FAST_CHARGING 0x20
#define CHARGE_TERMINATION_DONE 0x30

// Voltage BUS status
#define UNKNOWN_MODE 0x00
#define USB_HOST_MODE 0x40
#define ADAPTER_PORT_MODE 0x80
#define BOOST_MODE 0xC0

struct bq24195_data {
        struct gpio_callback gpio_cb;
	const struct device *dev;

        struct k_work work;
};

struct bq24195_dev_config {
        struct i2c_dt_spec i2c;
        struct gpio_dt_spec interrupt;

        uint16_t charge_current_limit;
        uint16_t charge_voltage_limit;
        uint16_t max_current;
        uint16_t min_voltage;
        bool charge_timer_en;
        uint8_t charge_timer_val;
};

/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/

/** @brief Enables PMIC charge mode
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_enable_charge(const struct device *dev);

/** @brief Enables PMIC boost mode, allow to generate 5V from battery
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_enable_boost_mode(const struct device *dev);

/** @brief Disable the battery charger
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_disable_charge(const struct device *dev);

/** @brief Disable the boost mode
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_disable_boost_mode(const struct device *dev);

/** @brief Enable the Buck Regulator
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_enable_buck(const struct device *dev);

/** @brief Disable the Buck Regulator
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_disable_buck(const struct device *dev);

/** @brief Sets the upper input current limit for PMIC
 *
 * @param current input current limit expressed in Ampere
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_set_input_current_limit(const struct device *dev, uint16_t current);

/** @brief Get the PMIC and return the input current limit
 *
 * @param current pointer to where store the return value
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_get_input_current_limit(const struct device *dev, uint16_t* current);

/** @brief Set the lower input voltage limit, the PMIC set a base voltage of 
 * 3.88V plus a combination of bits 3 to 6 of input source control register 
 * where the LSB is 80mV
 *
 * @param voltage input voltage limit expressed in Volt
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_set_input_voltage_limit(const struct device *dev, uint16_t voltage);

/** @brief Get the PMIC and return the input voltage limit
 *
 * @param voltage pointer to where store the return value
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_get_input_voltage_limit(const struct device *dev, float* voltage);

/** @brief Reset the I2C Watchdog Timer
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_reset_watchdog(const struct device *dev);

/** @brief Set the mimium acceptable votlage to feed the onboard MCU and module
 *
 * @param voltage mimium system voltage in Volt
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_set_minimum_system_voltage(const struct device *dev, float voltage);

/** @brief Get the minium system voltage in Volt
 *
 * @param voltage pointer to where store the return value
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_get_minimum_system_voltage(const struct device *dev, float* voltage);

/** @brief Set charge current
 *
 * @param current the current expressed in Ampere
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_set_charge_current(const struct device *dev, uint16_t current);

/** @brief Get the PMIC and return the charge current value in Ampere
 *
 * @param current pointer to where store the return value
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_get_charge_current(const struct device *dev, float* current);

/** @brief Set PMIC precharge current limit
 *
 * @param current the current expressed in Ampere
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_set_precharge_current(const struct device *dev, float current);

/** @brief Get the PMIC and return the precharge current value in Ampere
 *
 * @param current pointer to where store the return value
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_get_precharge_current(const struct device *dev, float* current);

/** @brief Set PMIC termination charge current limit
 *
 * @param current the current expressed in Ampere
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_set_termcharge_current(const struct device *dev, float current);

/** @brief Get the PMIC and return the termination charge current value in Ampere
 *
 * @param current pointer to where store the return value
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_get_termcharge_current(const struct device *dev, float* current);

/** @brief Set the PMIC charge voltage limit
 *
 * @param voltage charge voltage in Volt
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_set_charge_voltage(const struct device *dev, uint16_t voltage);

/** @brief Get the PMIC and return the charge voltage limit in Volt
 *
 * @param current pointer to where store the return value
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_get_charge_voltage(const struct device *dev, float* voltage);

/** @brief Disable Watchdog Timer
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_disable_watchdog(const struct device *dev);

/** @brief Enable the DPDM detection
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_enable_dpdm(const struct device *dev);

/** @brief Disable the DPDM detection
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_disable_dpdm(const struct device *dev);

/** @brief Enable the BATFET (Turn ON)
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_enable_batfet(const struct device *dev);

/** @brief Disable the BATFET (Turn OFF)
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_disable_batfet(const struct device *dev);

/** @brief Enable interrupt during charge fault
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24915_enable_charge_fault_interrupt(const struct device *dev);

/** @brief Disable interrupt during charge fault
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_disable_charge_fault_interrupt(const struct device *dev);

/** @brief Enable interrupt during battery fault
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_enable_bat_fault_interrupt(const struct device *dev);

/** @brief Disable interrupt during battery fault
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_disable_bat_fault_interrupt(const struct device *dev);

/** @brief Query the PMIC and return the voltage bus status
 *
 * @param status the voltage bus status
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_usb_mode(const struct device *dev, int* status);

/** @brief Query the PMIC and return the charging status
 *
 * @param status the charging status
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_charge_status(const struct device *dev, int* status);

/** @brief Check if a battery is connected
 *
 * @retval return 0 battery connected, otherwise return none-zero status
 */
int bq24195_is_battery_connected(const struct device *dev);

/** @brief Check if the power good
 *
 * @retval return 1 if power good, 1 if not good,
 *  otherwise error
 */
int bq24195_is_power_good(const struct device *dev);

/** @brief Check if the thermal is in thermal regulation range
 *
 * @retval return 0 on normal state, 1 on thermal regulation state
 */
int bq24195_is_hot(const struct device *dev);

/** @brief Check if the battery voltge i lower than the input system
 * voltage limit
 *
 * @retval return 0 if V_BAT > V_SYS_MIN, 1 if V_BAT < V_SYS_MIN
 */
int bq24195_can_run_on_battery(const struct device *dev);

/** @brief Query the PMIC and return if watch time expires
 *
 * @retval return 0 on normal state and 1 if watchdog timer expired
 */
int bq24195_is_watchdog_expired(const struct device *dev);

/** @brief Query the PMIC and return charge fault status
 *
 * @param fault -1 on reading error, 0 if no fault and a fault bits
 * combination on PMIC charge fault.
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_get_charge_fault(const struct device *dev, int *fault);

/** @brief Query the PMIC and return if battery over-voltage fault occurs.
 *
 * @retval return 0 on normal state, 1 if battery over voltage fault occurs
 */
int bq24195_is_battery_is_over_voltage(const struct device *dev);

/** @brief Query the PMIC and if the battery temperature is out of the 
 * required safety charge temperature range.
 *
 * @retval return -1 on read error, 0 on normal state, 
 * a NTC fault bit combination on NTC fault.
 */
int bq24195_has_battery_temperature_fault(const struct device *dev);

/** @brief Set the thermal regulation threshold
 * 
 * @param degree threshold degree in C
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_set_thermal_regulation_temp(const struct device *dev, int degree);

/** @brief Get the thermal regulation threshold
 * 
 * @param degree pointer to store threshold degree in C
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_get_thermal_regulation_temp(const struct device *dev, int* degree);

/** @brief Enable the battery charger
 * 
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_enable_charging(const struct device *dev);

/** @brief Disable the battery charger
 * 
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_disable_charging(const struct device *dev);

/** @brief Enable the boost mode
 * 
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_enable_otg(const struct device *dev);

/** @brief Disable the boost mode
 * 
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int bq24195_disable_otg(const struct device *dev);

/** @brief Enable or disable the charge timer.
 * 
 * @param enable If true, enable the timer. If false, disable the timer.
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
*/
int bq24195_enable_disable_charge_timer(const struct device *dev, bool enable);

/** @brief Set the charge timer value in hours.
 * Set to the closest equal or lower value allowed by the hardware.
 * Valid values are: 5, 8, 12 and 20 hrs.
 * 
 * @param hrs Charge timer value in hours.
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
*/
int bq24195_set_charge_timer_value(const struct device *dev, uint8_t hrs);

/** @brief Print all registers as HEXDUMP, if LOG_LEVEL_DEBUG or lower.
 * 
*/
void bq24195_print_all_registers(const struct device *dev);

#endif /* BQ24195_H_ */