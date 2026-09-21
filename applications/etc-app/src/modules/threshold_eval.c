/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <string.h>

#include <zephyr/kernel.h>

#include "threshold_eval.h"

/* Input ports carrying a temperature reading; the ambient sensor is not one. */
#define THRESHOLD_TEMPERATURE_PORTS GENMASK(SENSOR_INPUT_IN8, SENSOR_INPUT_IN1)
/* Input ports carrying a humidity reading. */
#define THRESHOLD_HUMIDITY_PORTS    BIT(SENSOR_INPUT_HUMID)

/** @brief Ports a slot evaluates, 0 for a value type the device cannot read yet. */
static uint16_t slot_port_mask(const struct etc_threshold *cfg)
{
	switch (cfg->value_type) {
	case ETC_THRESHOLD_VALUE_TYPE_TEMPERATURE:
		return THRESHOLD_TEMPERATURE_PORTS;
	case ETC_THRESHOLD_VALUE_TYPE_HUMIDITY:
		return THRESHOLD_HUMIDITY_PORTS;
	default:
		return 0;
	}
}

static bool reading_is_valid(const struct etc_threshold *cfg, float reading)
{
	if (cfg->value_type == ETC_THRESHOLD_VALUE_TYPE_HUMIDITY) {
		return sensor_humidity_is_valid(reading);
	}
	return sensor_temperature_is_valid(reading);
}

static bool reading_is_beyond(const struct etc_threshold *cfg, float reading)
{
	if (cfg->alert_type == ETC_THRESHOLD_ALERT_DROPS_BELOW) {
		return reading < cfg->value;
	}
	return reading > cfg->value;
}

void threshold_eval_init(struct threshold_eval *state)
{
	memset(state, 0, sizeof(*state));
	state->last_report_ms = THRESHOLD_NO_REPORT_YET;
}

void threshold_eval_update(struct threshold_eval *state,
			   const struct etc_threshold cfg[ETC_THRESHOLD_SLOT_COUNT],
			   uint8_t changed, const struct sensor_data *data, int64_t now_ms,
			   uint32_t interval_s, struct threshold_eval_result *out)
{
	out->crossed = 0;
	out->request_upload = false;

	for (uint8_t slot = 0; slot < ETC_THRESHOLD_SLOT_COUNT; slot++) {
		const struct etc_threshold *slot_cfg = &cfg[slot];
		uint16_t ports;

		if ((changed & BIT(slot)) != 0) {
			state->beyond[slot] = 0;
		}

		ports = slot_cfg->enabled ? slot_port_mask(slot_cfg) : 0;
		if (ports == 0) {
			state->beyond[slot] = 0;
			continue;
		}

		for (uint8_t port = 0; port < SENSOR_INPUT_MAX; port++) {
			float reading;

			if ((ports & BIT(port)) == 0) {
				continue;
			}

			reading = data->sensor[port];
			if (!reading_is_valid(slot_cfg, reading) ||
			    !reading_is_beyond(slot_cfg, reading)) {
				state->beyond[slot] &= (uint16_t)~BIT(port);
				continue;
			}

			/* Beyond for the first time since the port was armed. */
			if ((state->beyond[slot] & BIT(port)) == 0) {
				state->beyond[slot] |= (uint16_t)BIT(port);
				out->crossed |= (uint8_t)BIT(slot);
			}
		}
	}

	if (out->crossed == 0) {
		return;
	}

	/* The sentinel is never subtracted from, so no interval can underflow. */
	if (state->last_report_ms != THRESHOLD_NO_REPORT_YET &&
	    (now_ms - state->last_report_ms) < (int64_t)interval_s * MSEC_PER_SEC) {
		return;
	}

	state->last_report_ms = now_ms;
	out->request_upload = true;
}
