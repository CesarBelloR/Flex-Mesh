/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Alert-flag bookkeeping for the EXACT Threshold slots (FW-1179): a slot's
 * Alert flag is cleared only once the send carrying it has been delivered.
 */

#include <zephyr/ztest.h>

#include "threshold_alert_state.h"

#define SLOT(n) ((uint8_t)BIT(n))

static struct threshold_alert_state state;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&state, 0, sizeof(state));
}

ZTEST_SUITE(threshold_alert_state, NULL, NULL, before, NULL, NULL);

ZTEST(threshold_alert_state, test_take_carries_pending_slots)
{
	threshold_alert_set(&state, SLOT(2));

	zassert_equal(threshold_alert_take_for_send(&state), SLOT(2));
	zassert_equal(state.pending, 0, "pending not emptied by take");
	zassert_equal(state.in_flight, SLOT(2));
}

ZTEST(threshold_alert_state, test_ack_clears_in_flight_slots)
{
	threshold_alert_set(&state, SLOT(0) | SLOT(3));
	threshold_alert_take_for_send(&state);

	zassert_equal(threshold_alert_send_acked(&state), SLOT(0) | SLOT(3));
	zassert_equal(state.in_flight, 0);
	zassert_equal(state.pending, 0);
}

/* The failed-send retry: the next send must still carry the alert. */
ZTEST(threshold_alert_state, test_failed_send_returns_slots_to_pending)
{
	threshold_alert_set(&state, SLOT(1));
	threshold_alert_take_for_send(&state);

	threshold_alert_send_failed(&state);
	zassert_equal(state.in_flight, 0);
	zassert_equal(state.pending, SLOT(1));

	zassert_equal(threshold_alert_take_for_send(&state), SLOT(1));
	zassert_equal(threshold_alert_send_acked(&state), SLOT(1));
}

/* A slot that triggers again mid-flight keeps its flag past the ack. */
ZTEST(threshold_alert_state, test_retrigger_during_flight_survives_ack)
{
	threshold_alert_set(&state, SLOT(1));
	threshold_alert_take_for_send(&state);
	threshold_alert_set(&state, SLOT(1));

	zassert_equal(threshold_alert_send_acked(&state), 0, "re-triggered slot was cleared");
	zassert_equal(state.pending, SLOT(1));
	zassert_equal(threshold_alert_take_for_send(&state), SLOT(1));
}

/* A send started before the previous one settled must carry both. */
ZTEST(threshold_alert_state, test_take_without_settle_keeps_earlier_slots_in_flight)
{
	threshold_alert_set(&state, SLOT(0));
	zassert_equal(threshold_alert_take_for_send(&state), SLOT(0));

	threshold_alert_set(&state, SLOT(3));
	zassert_equal(threshold_alert_take_for_send(&state), SLOT(0) | SLOT(3));
	zassert_equal(threshold_alert_send_acked(&state), SLOT(0) | SLOT(3));
}
