/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <stddef.h>

#include "data_codec_signal.h"

bool data_codec_signal_is_reportable(int err, const struct modem_signal_sample *sample,
				     int64_t max_age_ms)
{
	if ((err != 0) || (sample == NULL)) {
		return false;
	}

	if ((sample->age_ms < 0) || (sample->age_ms > max_age_ms)) {
		return false;
	}

	if ((sample->rsrp < RSRP_MIN_RANGE_VALUE) || (sample->rsrp > RSRP_MAX_RANGE_VALUE)) {
		return false;
	}

	if ((sample->rsrq < RSRQ_MIN_RANGE_VALUE) || (sample->rsrq > RSRQ_MAX_RANGE_VALUE)) {
		return false;
	}

	return true;
}
