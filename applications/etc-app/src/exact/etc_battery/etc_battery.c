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
static void etc_battery_charger_handler(uint8_t bus_status, 
	uint8_t battery_status, uint8_t power_status);


void etc_battery_init(etc_battery_evt_handler_t handler) {
	static bool is_work_running = false;
	if (!device_is_ready(battery_dev)) {
		LOG_ERR("Battery device is not ready");
		return;
	}

	etc_battery_cb = handler;
	bq25618_register_callback(etc_battery_charger_handler);
}

void etc_battery_poll_status(void) {
	bq25618_poll_status(battery_dev);
}

enum battery_status etc_battery_get_status(void) {
	return last_battery_status;
}

static void etc_battery_charger_handler(uint8_t bus_status, 
	uint8_t battery_status, uint8_t power_status) {
	uint16_t battery_mV = etc_sensor_get_battery();
	LOG_DBG("Bus Status %d - Battery Status %d - Power Status %d - Battery Voltage %d", 
		bus_status, battery_status, power_status, battery_mV);
	
	enum battery_status current_status = BATTERY_UNKNOWN;

	if (battery_mV < CONFIG_BATTERY_NOT_INSTALL_MV) {
		current_status = BATTERY_NO_INSTALLED;
	} else {
		if (bus_status == USB_HOST_MODE || bus_status == ADAPTER_PORT_MODE) {
			if (battery_status == CHARGE_TERMINATION_DONE)  {
				current_status = BATTERY_CHARGE_COMPLETE;
			} else if (battery_status == FAST_CHARGING || battery_status == PRE_CHARGING) {
				current_status = BATTERY_CHARGE_IN_PROCESS;
			} else {
				/* Unknown case */
			}
		} else if (bus_status == NO_INPUT) {
			if (battery_mV > CONFIG_BATTERY_NOT_INSTALL_MV 
				&& battery_mV < CONFIG_BATTERY_LOW_VOLTAGE_MV) {
				current_status = BATTERY_LOW;
			} else if (battery_mV > CONFIG_BATTERY_LOW_VOLTAGE_MV) {
				current_status = BATTERY_NORMAL;
			} else {
				/* Unknown case */
			}
		}
	}

	if (last_battery_status != current_status) {
		last_battery_status = current_status;
		if (etc_battery_cb != NULL) {
			etc_battery_cb(current_status);
		}
	}
}

uint16_t etc_battery_get_voltage_mV(void)
{
	return etc_sensor_get_battery();
}
