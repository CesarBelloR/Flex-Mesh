#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_battery, CONFIG_ETC_BATTERY_LOG_LEVEL);

#include "etc_battery.h"
#include "etc_sensor.h"
#include "bq25618.h"

const struct device* battery_dev = DEVICE_DT_GET_ANY(ti_bq25618);
static enum battery_status last_battery_status = BATTERY_UNKNOWN;
static etc_battery_evt_handler_t etc_battery_cb = NULL;
static enum battery_status etc_battery_poll_status(void);
static void etc_battery_work_handler(struct k_work* work);
K_WORK_DELAYABLE_DEFINE(etc_battery_work, etc_battery_work_handler);

void etc_battery_init(etc_battery_evt_handler_t handler) {
	static bool is_work_running = false;
	if (!device_is_ready(battery_dev)) {
		LOG_ERR("Battery device is not ready");
		return;
	}

	etc_battery_cb = handler;
	if (!is_work_running) {
		is_work_running = true;
		k_work_schedule(&etc_battery_work, K_SECONDS(CONFIG_BATTERY_POLL_TIME_SEC));
	}
}

enum battery_status etc_battery_get_status(void) {
	return last_battery_status;
}

static enum battery_status etc_battery_poll_status(void)
{
	uint8_t battery_status = 0;
	uint8_t bus_status = 0;
	uint16_t battery_mV = etc_sensor_get_battery();
	int rc = bq25618_charge_status(battery_dev, &battery_status);
	if (rc) {
		LOG_ERR("Error in get charge status (err %d)", rc);
		return BATTERY_UNKNOWN;
	}

	rc = bq25618_voltage_bus_status(battery_dev, &bus_status);
	if (rc) {
		LOG_ERR("Error in get voltage bus status (err %d)", rc);
		return BATTERY_UNKNOWN;
	}

	LOG_DBG("Bus Status %d - Battery Status %d - Battery Voltage %d", bus_status, 
		battery_status, battery_mV);

	if (battery_mV < CONFIG_BATTERY_NOT_INSTALL_MV) {
		return BATTERY_NO_INSTALLED;
	}

	if (bus_status == USB_HOST_MODE) {
		if (battery_status == CHARGE_TERMINATION_DONE)  {
			return BATTERY_CHARGE_COMPLETE;
		} else if (battery_status == FAST_CHARGING || battery_status == PRE_CHARGING) {
			return BATTERY_CHARGE_IN_PROCESS;
		} else {
			/* Unknown case */
		}
	} else if (bus_status == NO_INPUT) {
		if (battery_mV > CONFIG_BATTERY_NOT_INSTALL_MV 
			&& battery_mV < CONFIG_BATTERY_LOW_VOLTAGE_MV) {
			return BATTERY_LOW;
		} else if (battery_mV > CONFIG_BATTERY_LOW_VOLTAGE_MV) {
			return BATTERY_NORMAL;
		} else {
			/* Unknown case */
		}
	}

	LOG_WRN("Unknown case (bus %d - bat %d - mV %d)", bus_status, battery_status, battery_mV);
	return BATTERY_UNKNOWN;
}

uint16_t etc_battery_get_voltage_mV(void)
{
	return etc_sensor_get_battery();
}

static void etc_battery_work_handler(struct k_work* work) {
	enum battery_status current_status = etc_battery_poll_status();
	if (current_status != last_battery_status) {
		last_battery_status = current_status;
		if (etc_battery_cb != NULL) {
			etc_battery_cb(current_status);
		}
	}
	k_work_reschedule(&etc_battery_work, K_SECONDS(CONFIG_BATTERY_POLL_TIME_SEC));
}