#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
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

#ifdef CONFIG_BATTERY_MODULE_SHELL
static bool is_battery_shell_active = false;
static uint8_t battery_percent_on_set = 0;
#endif

struct battery_lookup_entry {
	uint16_t start_voltage_mv;
	uint16_t end_voltage_mv;
	float scale;
	float offset;
};

/** 
 * Lookup table that is used to convert a battery voltage in mV to a percentage.
 * The lookup table contains parameters for linear equations.
 * 
 * For details on how this lookup table was derived, refer to:
 * @ref https://docs.google.com/spreadsheets/d/1JUHKDNdC5E_rDLhdtyLvtSG8GyZHbXeV7vxVv2MGPFk/edit#gid=1074662725
*/
const struct battery_lookup_entry lookup_table[] = {
	{.start_voltage_mv = 4200, .end_voltage_mv = 4100, .scale = 0.03, .offset = -26},
	{.start_voltage_mv = 4100, .end_voltage_mv = 4030, .scale = 0.2, .offset = -723},
	{.start_voltage_mv = 4030, .end_voltage_mv = 3790, .scale = 0.05, .offset = -118.5},
	{.start_voltage_mv = 3790, .end_voltage_mv = 3440, .scale = 0.13714, .offset = -448.76},
	{.start_voltage_mv = 3440, .end_voltage_mv = 2950, .scale = 0.04694, .offset = -138.47}
};

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

	if (etc_battery_cb != NULL) {
		last_battery_status = current_status;
		etc_battery_cb(current_status);
	}
}

uint16_t etc_battery_get_voltage_mV(void)
{
	return etc_sensor_get_battery();
}

uint8_t etc_battery_percentage_from_voltage(uint16_t voltage_mv)
{	
#ifdef CONFIG_BATTERY_MODULE_SHELL
	if (is_battery_shell_active) {
		return battery_percent_on_set;
	}
#endif
	for (int i = 0; i < ARRAY_SIZE(lookup_table); i++) {
		struct battery_lookup_entry *entry = (struct battery_lookup_entry *)&lookup_table[i];

		if (voltage_mv <= entry->start_voltage_mv &&
		    voltage_mv >= entry->end_voltage_mv) {
			return voltage_mv * entry->scale + entry->offset;
		}
	}

	if (voltage_mv > lookup_table[0].start_voltage_mv) {
		return 100;
	}

	return 0;
}

#ifdef CONFIG_BATTERY_MODULE_SHELL
#include <zephyr/shell/shell.h>

static int cmd_battery_enable(const struct shell *shell, size_t argc, char **argv)
{
	is_battery_shell_active = true;
	return 0;
}

static int cmd_battery_disable(const struct shell *shell, size_t argc, char **argv)
{
	is_battery_shell_active = false;
	return 0;
}

static int cmd_battery_set(const struct shell *shell, size_t argc, char **argv)
{
	battery_percent_on_set = atoi(argv[1]);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_battery,
	SHELL_CMD(enable, NULL, "Enable the battery shell", cmd_battery_enable),
	SHELL_CMD(disable, NULL, "Disable the battery shell", cmd_battery_disable),
	SHELL_CMD(set, NULL, "Set a percentage battery", cmd_battery_set),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(battery, &sub_battery, "Command line to test battery", NULL);

#endif