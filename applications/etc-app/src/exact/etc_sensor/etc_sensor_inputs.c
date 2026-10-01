/*
 * Copyright (c) 2026 EXACT Technology
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

#include "etc_sensor.h"
#include "etc_battery.h"
#include "etc_date_time.h"
#include "etc_sensor_sim.h"

LOG_MODULE_REGISTER(etc_sensor_inputs, CONFIG_ETC_SENSOR_LOG_LEVEL);

const char *etc_sensor_type_str(enum sensor_type type)
{
	switch (type) {
	case SENSOR_TYPE_ANALOG:
		return "Analog (NTC)";
	case SENSOR_TYPE_DIGITAL:
		return "Digital (1-Wire)";
	case SENSOR_TYPE_UNDEF:
	default:
		return "None";
	}
}

int etc_sensor_read_all_inputs(struct etc_logger_all_inputs *inputs, bool trigger_fresh_acquisition)
{
	if (!inputs) {
		return -EINVAL;
	}

	memset(inputs, 0, sizeof(*inputs));

	if (trigger_fresh_acquisition) {
		int rc = etc_sensor_run_acquisition();
		if (rc != 0) {
			LOG_WRN("Acquisition failed or busy (err %d)", rc);
			return rc;
		}
	}

	/* Timestamp */
	int utc_timestamp = date_time_now_second();
	inputs->timestamp = utc_timestamp > 0 ? (int64_t)utc_timestamp : (int64_t)(k_uptime_get() / 1000);

	/* Ambient temperature */
	inputs->ambient_temp_c = etc_sensor_get_ambient_temp();
	inputs->ambient_valid = sensor_temperature_is_valid(inputs->ambient_temp_c);

	/* Relative humidity */
	inputs->humidity_percent = etc_sensor_get_probe_humid();
	inputs->humidity_valid = sensor_humidity_is_valid(inputs->humidity_percent);
	inputs->humidity_port = etc_sensor_get_probe_humid_index();

	/* Primary inputs IN1..IN4 (Ports 1..4) */
	for (int i = 0; i < 4; i++) {
		inputs->in[i].temp_c = etc_sensor_get_probe_temp(i);
		inputs->in[i].type = etc_sensor_get_probe_type(i);
		inputs->in[i].connected = sensor_temperature_is_valid(inputs->in[i].temp_c);
	}

	/* Splitter inputs IN5..IN8 (Ports 1.B..4.B) */
	for (int i = 0; i < 4; i++) {
		inputs->splitter[i].temp_c = etc_sensor_get_probe_temp(i + SENSOR_INPUT_IN5);
		inputs->splitter[i].type = etc_sensor_get_probe_type(i + SENSOR_INPUT_IN5);
		inputs->splitter[i].connected = sensor_temperature_is_valid(inputs->splitter[i].temp_c);
	}

	/* Battery inputs */
	inputs->battery_mv = etc_battery_get_voltage_mV();
	inputs->battery_percent = etc_battery_percentage_from_voltage(inputs->battery_mv);
	inputs->battery_status = etc_battery_get_status();

	/* Populate raw_data (matches struct sensor_data used in events and records) */
	inputs->raw_data.timestamp = inputs->timestamp;
	inputs->raw_data.sensor[SENSOR_INPUT_AMBIENT] = inputs->ambient_temp_c;
	inputs->raw_data.sensor[SENSOR_INPUT_HUMID] = inputs->humidity_percent;
	for (int i = 0; i < 4; i++) {
		inputs->raw_data.sensor[i] = inputs->in[i].temp_c;
		inputs->raw_data.sensor[i + SENSOR_INPUT_IN5] = inputs->splitter[i].temp_c;
	}
	inputs->raw_data.battery_mV = inputs->battery_mv;
	inputs->raw_data.battery_status = inputs->battery_status;

	/* Apply simulated overrides if simulation module is active */
	etc_sensor_sim_apply(&inputs->raw_data);

	/* Synchronize any simulation overrides to individual fields */
	inputs->ambient_temp_c = inputs->raw_data.sensor[SENSOR_INPUT_AMBIENT];
	inputs->ambient_valid = sensor_temperature_is_valid(inputs->ambient_temp_c);
	inputs->humidity_percent = inputs->raw_data.sensor[SENSOR_INPUT_HUMID];
	inputs->humidity_valid = sensor_humidity_is_valid(inputs->humidity_percent);
	for (int i = 0; i < 4; i++) {
		inputs->in[i].temp_c = inputs->raw_data.sensor[i];
		inputs->in[i].connected = sensor_temperature_is_valid(inputs->in[i].temp_c);
		inputs->splitter[i].temp_c = inputs->raw_data.sensor[i + SENSOR_INPUT_IN5];
		inputs->splitter[i].connected = sensor_temperature_is_valid(inputs->splitter[i].temp_c);
	}

	return 0;
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>

static void print_logger_inputs(const struct shell *shell, const struct etc_logger_all_inputs *inputs)
{
	shell_print(shell, "=================== Logger Inputs ===================");
	if (inputs->timestamp > 1577836800) { /* > Year 2020: valid UTC */
		shell_print(shell, "Timestamp    : %lld (UTC)", inputs->timestamp);
	} else {
		shell_print(shell, "Timestamp    : %lld s (uptime)", inputs->timestamp);
	}

	shell_print(shell, "Battery      : %u mV (%u%%, %s)",
		    inputs->battery_mv, inputs->battery_percent,
		    etc_battery_status_str(inputs->battery_status));

	if (inputs->ambient_valid) {
		shell_print(shell, "Ambient Temp : %.2f C", (double)inputs->ambient_temp_c);
	} else {
		shell_print(shell, "Ambient Temp : Not available");
	}

	if (inputs->humidity_valid) {
		if (inputs->humidity_port >= 0) {
			shell_print(shell, "Humidity     : %.2f %% (Port %d)",
				    (double)inputs->humidity_percent, inputs->humidity_port + 1);
		} else {
			shell_print(shell, "Humidity     : %.2f %%", (double)inputs->humidity_percent);
		}
	} else {
		shell_print(shell, "Humidity     : Not connected");
	}

	shell_print(shell, "--- Primary Inputs (Ports 1-4) ---");
	for (int i = 0; i < 4; i++) {
		if (inputs->in[i].connected) {
			shell_print(shell, "  IN%d (Port %d)   : %6.2f C  [%s]",
				    i + 1, i + 1, (double)inputs->in[i].temp_c,
				    etc_sensor_type_str(inputs->in[i].type));
		} else {
			shell_print(shell, "  IN%d (Port %d)   : --        [%s]",
				    i + 1, i + 1,
				    etc_sensor_type_str(inputs->in[i].type));
		}
	}

	shell_print(shell, "--- Splitter Inputs (Ports 1.B-4.B) ---");
	for (int i = 0; i < 4; i++) {
		if (inputs->splitter[i].connected) {
			shell_print(shell, "  IN%d (Port %d.B) : %6.2f C  [%s]",
				    i + 5, i + 1, (double)inputs->splitter[i].temp_c,
				    etc_sensor_type_str(inputs->splitter[i].type));
		} else {
			shell_print(shell, "  IN%d (Port %d.B) : --        [%s]",
				    i + 5, i + 1,
				    etc_sensor_type_str(inputs->splitter[i].type));
		}
	}
	shell_print(shell, "=====================================================");
}

static int cmd_inputs_read(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct etc_logger_all_inputs inputs;
	int rc = etc_sensor_read_all_inputs(&inputs, false);
	if (rc != 0) {
		shell_error(shell, "Failed to read inputs (err %d)", rc);
		return rc;
	}
	print_logger_inputs(shell, &inputs);
	return 0;
}

static int cmd_inputs_sample(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct etc_logger_all_inputs inputs;
	shell_print(shell, "Sampling logger inputs (running hardware acquisition)...");
	int rc = etc_sensor_read_all_inputs(&inputs, true);
	if (rc != 0) {
		shell_error(shell, "Failed to sample inputs (err %d)", rc);
		return rc;
	}
	print_logger_inputs(shell, &inputs);
	return 0;
}

static int cmd_inputs_default(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 1) {
		return cmd_inputs_read(shell, argc, argv);
	}
	shell_help(shell);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_inputs,
	SHELL_CMD(read, NULL, "Read latest cached inputs without waking hardware", cmd_inputs_read),
	SHELL_CMD(sample, NULL, "Trigger fresh hardware acquisition and print inputs", cmd_inputs_sample),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(inputs, &sub_inputs, "Read all logger inputs (temperature, humidity, battery)", cmd_inputs_default);
#endif /* CONFIG_SHELL */
