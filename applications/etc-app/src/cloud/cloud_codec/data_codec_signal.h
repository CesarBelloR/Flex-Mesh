/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef DATA_CODEC_SIGNAL_H
#define DATA_CODEC_SIGNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "modem_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reportable ranges. Outside these the modem has no usable reading, rather
 * than a weak one.
 */
/* Below -130 dBm the modem reports its floor, not a signal level. */
#define RSRP_MIN_RANGE_VALUE (-130)
#define RSRP_MAX_RANGE_VALUE (-40)
#define RSRQ_MIN_RANGE_VALUE (-34)
/* RSRQ is negative by definition; 0 means the modem has not measured it. */
#define RSRQ_MAX_RANGE_VALUE (-1)

/**
 * @brief Decide whether a modem signal sample may be reported to the cloud.
 *
 * @param err Return code from quectel_bg95_get_signal().
 * @param sample Sample filled by quectel_bg95_get_signal(). Ignored when
 *               @p err is non-zero.
 * @param max_age_ms Oldest sample age still worth reporting, inclusive.
 *
 * @return true if the sample exists, is in range and is recent enough.
 */
bool data_codec_signal_is_reportable(int err, const struct modem_signal_sample *sample,
				     int64_t max_age_ms);

#ifdef __cplusplus
}
#endif

#endif /* DATA_CODEC_SIGNAL_H */
