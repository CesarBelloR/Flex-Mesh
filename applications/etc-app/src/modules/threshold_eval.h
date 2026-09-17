/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef THRESHOLD_EVAL_H_
#define THRESHOLD_EVAL_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys_clock.h>

#include "etc_device.h"
#include "events/sensor_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Minimum spacing between threshold-triggered uploads, all slots combined. */
#define THRESHOLD_MIN_REPORT_INTERVAL_MS                                                           \
	((int64_t)CONFIG_ETC_APP_THRESHOLD_MIN_REPORT_INTERVAL_S * MSEC_PER_SEC)

/** @brief Edge-triggered evaluation state of the immediate report thresholds. */
struct threshold_eval {
	/** Per slot: ports (bit = @ref enum sensor_input) currently beyond the threshold. */
	uint16_t beyond[ETC_THRESHOLD_SLOT_COUNT];
	/** Uptime of the last upload request. */
	int64_t last_report_ms;
};

/** @brief Outcome of evaluating one sample. */
struct threshold_eval_result {
	/** Slots (bit = slot index) that crossed on this sample. */
	uint8_t crossed;
	/** An immediate upload may be requested for this sample. */
	bool request_upload;
};

/**
 * @brief Reset the evaluation state; every slot starts unarmed, rate limit open.
 *
 * @param state State to reset.
 */
void threshold_eval_init(struct threshold_eval *state);

/**
 * @brief Evaluate one sample against the threshold configuration.
 *
 * Slots named in @p changed are re-armed before evaluation, so a reading already
 * beyond a freshly configured threshold crosses. A port re-arms once its reading
 * returns to the non-triggering side or becomes invalid.
 *
 * @param state Evaluation state, carried between samples.
 * @param cfg Current configuration of all slots.
 * @param changed Slots (bit = slot index) reconfigured since the last sample.
 * @param data Sample to evaluate.
 * @param now_ms Uptime of the sample, used for the rate limit.
 * @param out Crossed slots and whether an immediate upload may be requested.
 */
void threshold_eval_update(struct threshold_eval *state,
			   const struct etc_threshold cfg[ETC_THRESHOLD_SLOT_COUNT],
			   uint8_t changed, const struct sensor_data *data, int64_t now_ms,
			   struct threshold_eval_result *out);

#ifdef __cplusplus
}
#endif

#endif /* THRESHOLD_EVAL_H_ */
