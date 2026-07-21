/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <zephyr/ztest.h>
#include "etc_device.h"
#include "app_module_helper.h"

static const enum app_upload_reason all_reasons[] = {
	APP_UPLOAD_NONE,
	APP_UPLOAD_NORMAL,
	APP_UPLOAD_LORA_SYNC,
	APP_UPLOAD_MAGNET,
};

/* Rank expected from the merge: MAGNET > LORA_SYNC > NORMAL > NONE. */
static int reason_rank(enum app_upload_reason reason)
{
	switch (reason) {
	case APP_UPLOAD_NONE:
		return 0;
	case APP_UPLOAD_NORMAL:
		return 1;
	case APP_UPLOAD_LORA_SYNC:
		return 2;
	case APP_UPLOAD_MAGNET:
		return 3;
	}
	return -1;
}

ZTEST(upload_reason, test_merge_full_matrix)
{
	for (size_t i = 0; i < ARRAY_SIZE(all_reasons); i++) {
		for (size_t j = 0; j < ARRAY_SIZE(all_reasons); j++) {
			enum app_upload_reason a = all_reasons[i];
			enum app_upload_reason b = all_reasons[j];
			enum app_upload_reason merged = app_module_upload_reason_merge(a, b);
			enum app_upload_reason expected =
				reason_rank(a) >= reason_rank(b) ? a : b;

			zassert_equal(merged, expected,
				      "merge(%d, %d) = %d, expected %d", a, b, merged,
				      expected);
			/* Commutativity */
			zassert_equal(merged, app_module_upload_reason_merge(b, a),
				      "merge(%d, %d) is not commutative", a, b);
		}
	}
}

ZTEST(upload_reason, test_merge_none_none_is_none)
{
	/* The dispatch-NONE-is-a-noop invariant rests on NONE never winning a
	 * merge into a non-NONE request and merge(NONE, NONE) staying NONE. */
	zassert_equal(app_module_upload_reason_merge(APP_UPLOAD_NONE, APP_UPLOAD_NONE),
		      APP_UPLOAD_NONE);
}

ZTEST(upload_reason, test_from_schedule)
{
	zassert_equal(app_module_upload_reason_from_schedule(APP_WAKEUP_TX_INTERVAL_WORK),
		      APP_UPLOAD_NORMAL);
	zassert_equal(app_module_upload_reason_from_schedule(APP_WAKEUP_TX_PROBE_WORK),
		      APP_UPLOAD_NORMAL);
	zassert_equal(
		app_module_upload_reason_from_schedule(APP_WAKEUP_TX_SYNC_CLOUD_FOR_LORA_WORK),
		APP_UPLOAD_LORA_SYNC);
	zassert_equal(
		app_module_upload_reason_from_schedule(APP_WAKEUP_TX_SYNC_CLOUD_FOR_MAGNET_WORK),
		APP_UPLOAD_MAGNET);
}

ZTEST(upload_reason, test_sub_job_for_reason)
{
	zassert_equal(app_module_sub_job_for_reason(APP_UPLOAD_NONE), ETC_TRANSMIT_NORMAL);
	zassert_equal(app_module_sub_job_for_reason(APP_UPLOAD_NORMAL), ETC_TRANSMIT_NORMAL);
	zassert_equal(app_module_sub_job_for_reason(APP_UPLOAD_LORA_SYNC),
		      ETC_TRANSMIT_SYNC_CLOUD_LORA);
	zassert_equal(app_module_sub_job_for_reason(APP_UPLOAD_MAGNET),
		      ETC_TRANSMIT_SYNC_MAGNET);
}

ZTEST_SUITE(upload_reason, NULL, NULL, NULL, NULL, NULL);
