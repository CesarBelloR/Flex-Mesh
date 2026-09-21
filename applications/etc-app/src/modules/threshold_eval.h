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

/** @brief @ref threshold_eval.last_report_ms while no upload has been requested. */
#define THRESHOLD_NO_REPORT_YET INT64_MIN

/** @brief Edge-triggered evaluation state of the immediate report thresholds. */
struct threshold_eval {
	/** Per slot: ports (bit = @ref enum sensor_input) currently beyond the threshold. */
	uint16_t beyond[ETC_THRESHOLD_SLOT_COUNT];
	/** Uptime of the last upload request, @ref THRESHOLD_NO_REPORT_YET until the first. */
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
 * @brief Reset the evaluation state; every slot starts unarmed, hold-off open.
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
 * @param interval_s Hold-off between upload requests, all slots combined. Read
 * per sample, so a change takes effect on the next one.
 * @param out Crossed slots and whether an immediate upload may be requested.
 */
void threshold_eval_update(struct threshold_eval *state,
			   const struct etc_threshold cfg[ETC_THRESHOLD_SLOT_COUNT],
			   uint8_t changed, const struct sensor_data *data, int64_t now_ms,
			   uint32_t interval_s, struct threshold_eval_result *out);

#ifdef __cplusplus
}
#endif

#endif /* THRESHOLD_EVAL_H_ */
