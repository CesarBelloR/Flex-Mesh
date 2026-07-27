/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <zephyr/ztest.h>
#include <errno.h>

#include "data_codec_signal.h"

/* Matches DATA_SIGNAL_MAX_AGE_MS in data_module.c. */
#define MAX_AGE_MS (6 * 60 * 60 * (int64_t)MSEC_PER_SEC)

#define GOOD_RSRP (-85)
#define GOOD_RSRQ (-10)

static struct modem_signal_sample good_sample(void)
{
	struct modem_signal_sample sample = {
		.rssi = -70,
		.rsrp = GOOD_RSRP,
		.rsrq = GOOD_RSRQ,
		.age_ms = 0,
	};

	return sample;
}

ZTEST(signal_freshness, test_driver_error_is_not_reportable)
{
	struct modem_signal_sample sample = good_sample();

	/* Registered before QCSQ ever populated a measurement. */
	zassert_false(data_codec_signal_is_reportable(-ENODATA, &sample, MAX_AGE_MS));
	zassert_false(data_codec_signal_is_reportable(-EINVAL, &sample, MAX_AGE_MS));
}

ZTEST(signal_freshness, test_null_sample_is_not_reportable)
{
	zassert_false(data_codec_signal_is_reportable(0, NULL, MAX_AGE_MS));
}

ZTEST(signal_freshness, test_fresh_valid_sample_is_reportable)
{
	struct modem_signal_sample sample = good_sample();

	zassert_true(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));
}

ZTEST(signal_freshness, test_age_boundary_is_inclusive)
{
	struct modem_signal_sample sample = good_sample();

	sample.age_ms = MAX_AGE_MS;
	zassert_true(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));

	sample.age_ms = MAX_AGE_MS + 1;
	zassert_false(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));
}

ZTEST(signal_freshness, test_negative_age_is_not_reportable)
{
	struct modem_signal_sample sample = good_sample();

	sample.age_ms = -1;
	zassert_false(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));
}

/* FW-779: the modem's floor reading must never be published as a signal
 * level. FW-1005: neither must its zeroed no-measurement reply.
 */
ZTEST(signal_freshness, test_modem_placeholder_values_are_not_reportable)
{
	struct modem_signal_sample sample = good_sample();

	sample.rsrp = -140;
	zassert_false(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));

	sample = good_sample();
	sample.rsrp = 0;
	sample.rsrq = 0;
	zassert_false(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));
}

ZTEST(signal_freshness, test_rsrp_range_boundaries)
{
	struct modem_signal_sample sample = good_sample();

	sample.rsrp = RSRP_MIN_RANGE_VALUE;
	zassert_true(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));

	sample.rsrp = RSRP_MIN_RANGE_VALUE - 1;
	zassert_false(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));

	sample.rsrp = RSRP_MAX_RANGE_VALUE;
	zassert_true(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));

	sample.rsrp = RSRP_MAX_RANGE_VALUE + 1;
	zassert_false(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));
}

ZTEST(signal_freshness, test_rsrq_range_boundaries)
{
	struct modem_signal_sample sample = good_sample();

	sample.rsrq = RSRQ_MIN_RANGE_VALUE;
	zassert_true(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));

	sample.rsrq = RSRQ_MIN_RANGE_VALUE - 1;
	zassert_false(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));

	sample.rsrq = RSRQ_MAX_RANGE_VALUE;
	zassert_true(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));

	sample.rsrq = RSRQ_MAX_RANGE_VALUE + 1;
	zassert_false(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));
}

/* The floor reading is rejected on range, not on age, so it stays rejected
 * even when it was just measured.
 */
ZTEST(signal_freshness, test_range_check_applies_to_fresh_samples)
{
	struct modem_signal_sample sample = good_sample();

	sample.rsrp = -140;
	sample.age_ms = 0;
	zassert_false(data_codec_signal_is_reportable(0, &sample, MAX_AGE_MS));
}

ZTEST_SUITE(signal_freshness, NULL, NULL, NULL, NULL, NULL);
