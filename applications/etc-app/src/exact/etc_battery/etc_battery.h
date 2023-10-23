#ifndef ETC_BATTERY_H_
#define ETC_BATTERY_H_

#include <stdint.h>

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
 * @brief Get the battery in mV 
 * 
 * @return battery status @ref enum battery_status
 */
uint16_t etc_battery_get_voltage_mV(void);

#endif /* ETC_BATTERY_H_ */