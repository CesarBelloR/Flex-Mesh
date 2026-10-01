#ifndef ETC_BATTERY_H_
#define ETC_BATTERY_H_

#include <stdint.h>
#include <stdbool.h>

enum battery_level {
	/* Battery low level from 0 to < 6 */
	BATTERY_MIN_LOW = 0,
	/* Battery medium level from 6 to < 21 */
	BATTERY_MIN_MED = 6,
	/* Battery full level from 21 to 100 */
	BATTERY_MIN_FULL = 21
};

enum battery_status {
	/* The battery is operating normally and not on power */
	BATTERY_NORMAL = 0,
	/* The battery is currently charging */
	BATTERY_CHARGE_IN_PROCESS,
	/* The battery is fully charged and still on power */
	BATTERY_CHARGE_COMPLETE,
	/* The battery has some problem */
	BATTERY_DAMAGED,
	/* The battery is low on charge */
	BATTERY_LOW,
	/* The battery is not installed */
	BATTERY_NO_INSTALLED,
	/* The battery information is not available */
	BATTERY_UNKNOWN
};

typedef void(*etc_battery_evt_handler_t)(enum battery_status status);

/**
 * @brief Initialize the battery management
 */
void etc_battery_init(etc_battery_evt_handler_t handler);

/** @brief Poll the status from PMIC
 *
 */
void etc_battery_poll_status(void);
/**
 * @brief Get the battery status based on LwM2M spec
 * 
 * @return battery status @ref enum battery_status
 */
enum battery_status etc_battery_get_status(void);

/**
 * @brief Convert battery status enum to human-readable string.
 *
 * @param status Battery status.
 * @return String description ("Normal", "Charging", "Charge Complete", etc.).
 */
const char *etc_battery_status_str(enum battery_status status);

/**
 * @brief Get the battery in mV 
 * 
 * @return battery status @ref enum battery_status
 */
uint16_t etc_battery_get_voltage_mV(void);

/**
 * Convert a battery voltage into a percentage.
 * 
 * @param voltage_mv Battery voltage in mV
 * 
 * @return Percentage value from 0 to 100.
*/
uint8_t etc_battery_percentage_from_voltage(uint16_t voltage_mv);

/**
 * @brief Check whether a battery is physically connected.
 *
 * Performs an active measurement: charging is briefly disabled so the BAT pin
 * is no longer held at the charge voltage, the battery voltage is sampled, and
 * charging is re-enabled. A voltage that collapses below
 * CONFIG_BATTERY_NOT_INSTALL_MV indicates no battery is installed. This is
 * required because, while a charger is attached, the PMIC holds the BAT pin
 * near the charge voltage and a missing battery cannot be detected passively.
 *
 * @retval true A battery is connected.
 * @retval false No battery is connected (or the PMIC is not ready).
 */
bool etc_battery_is_connected(void);

#endif /* ETC_BATTERY_H_ */