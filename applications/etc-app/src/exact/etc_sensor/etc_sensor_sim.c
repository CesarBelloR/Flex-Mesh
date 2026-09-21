/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Simulated probe readings for hardware-in-the-loop testing (FW-1234).
 * CONFIG_ETC_SENSOR_SIM_SHELL has the rationale.
 */

#include "etc_sensor_sim.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_DECLARE(etc_sensor, CONFIG_ETC_SENSOR_LOG_LEVEL);

static float sim_value[SENSOR_INPUT_MAX];
static uint16_t sim_active;

int etc_sensor_sim_set(enum sensor_input in, float value)
{
	if ((unsigned int)in >= SENSOR_INPUT_MAX) {
		return -EINVAL;
	}

	/* Unlocked: the barrier keeps the value ahead of the mask bit that
	 * publishes it, so a concurrent sample reads either the previous value or
	 * the new one, never the new mask with the old value. */
	sim_value[in] = value;
	compiler_barrier();
	sim_active |= BIT(in);
	return 0;
}

void etc_sensor_sim_clear(uint16_t mask)
{
	sim_active &= ~mask;
}

void etc_sensor_sim_apply(struct sensor_data *data)
{
	uint16_t active = sim_active;

	if (active == 0) {
		return;
	}

	LOG_INF("Sensor sim active mask 0x%03x", active);
	for (unsigned int in = 0; in < SENSOR_INPUT_MAX; in++) {
		if (active & BIT(in)) {
			data->sensor[in] = sim_value[in];
		}
	}
}

uint16_t etc_sensor_sim_active_mask(void)
{
	return sim_active;
}

/* Shell names of the inputs, indexed by enum sensor_input. */
static const char *const sim_input_name[] = {
	"in1", "in2", "in3", "in4", "in5", "in6", "in7", "in8", "ambient", "humid",
};
BUILD_ASSERT(ARRAY_SIZE(sim_input_name) == SENSOR_INPUT_MAX,
	     "sim_input_name needs a name for every enum sensor_input member");

/** @brief Input named on the command line, or -EINVAL. */
static int sim_input_by_name(const char *name)
{
	for (unsigned int in = 0; in < SENSOR_INPUT_MAX; in++) {
		if (strcmp(name, sim_input_name[in]) == 0) {
			return (int)in;
		}
	}
	return -EINVAL;
}

/** @brief Milli-units of a reading, the unit the shell speaks. */
static int sim_milli(float value)
{
	return (int)(value * 1000.0f);
}

static int cmd_sensor_sim_set(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	int in = sim_input_by_name(argv[1]);

	if (in < 0) {
		shell_error(sh, "Unknown input '%s'", argv[1]);
		return -EINVAL;
	}

	float value;

	if (strcmp(argv[2], "nc") == 0) {
		value = (in == SENSOR_INPUT_HUMID) ? (float)SENSOR_HUMID_NO_CONNECTED
						   : (float)SENSOR_TEMP_NO_CONNECTED;
	} else {
		int err = 0;
		long mval = shell_strtol(argv[2], 10, &err);

		if (err) {
			shell_error(sh, "Invalid value '%s', expected milli-units or 'nc'",
				    argv[2]);
			return -EINVAL;
		}
		value = (float)mval / 1000.0f;
	}

	int rc = etc_sensor_sim_set((enum sensor_input)in, value);

	if (rc) {
		shell_error(sh, "Could not simulate %s (err %d)", argv[1], rc);
		return rc;
	}

	shell_print(sh, "Simulating %s at %d milli-units", argv[1], sim_milli(value));
	return 0;
}

static int cmd_sensor_sim_clear(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t mask = BIT_MASK(SENSOR_INPUT_MAX);

	if (argc > 1) {
		int in = sim_input_by_name(argv[1]);

		if (in < 0) {
			shell_error(sh, "Unknown input '%s'", argv[1]);
			return -EINVAL;
		}
		mask = BIT(in);
	}

	etc_sensor_sim_clear(mask);
	shell_print(sh, "Sensor sim active mask 0x%03x", etc_sensor_sim_active_mask());
	return 0;
}

static int cmd_sensor_sim_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint16_t active = etc_sensor_sim_active_mask();

	shell_print(sh, "Sensor sim active mask 0x%03x", active);
	for (unsigned int in = 0; in < SENSOR_INPUT_MAX; in++) {
		if (active & BIT(in)) {
			shell_print(sh, "%s %d", sim_input_name[in], sim_milli(sim_value[in]));
		}
	}
	return 0;
}

/* Root name kept distinct from Zephyr's own CONFIG_SENSOR_SHELL "sensor". */
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_sensor_sim,
	SHELL_CMD_ARG(set, NULL,
		      "Simulate a reading: set <in1..in8|ambient|humid> "
		      "<milli-units|nc>",
		      cmd_sensor_sim_set, 3, 0),
	SHELL_CMD_ARG(clear, NULL, "Stop simulating one input, or all of them: clear [input]",
		      cmd_sensor_sim_clear, 1, 1),
	SHELL_CMD(status, NULL, "Print the simulated inputs and their values.",
		  cmd_sensor_sim_status),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(sensor_sim, &sub_sensor_sim, "Simulated probe readings (FW-1234)", NULL);
