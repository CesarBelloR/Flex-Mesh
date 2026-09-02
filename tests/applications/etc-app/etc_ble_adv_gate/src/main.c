/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * The gate that keeps advertising requests off the Bluetooth stack until
 * etc_ble_init() has finished enabling it (FW-1194).
 */

#include <zephyr/ztest.h>

#include "etc_ble_adv_gate.h"

static struct etc_ble_adv_gate gate;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&gate, 0, sizeof(gate));
}

ZTEST_SUITE(etc_ble_adv_gate, NULL, NULL, before, NULL, NULL);

/* The FW-1194 crash: a magnet swipe before the stack is up must not run. */
ZTEST(etc_ble_adv_gate, test_request_before_open_is_latched)
{
	zassert_false(etc_ble_adv_gate_request(&gate, ETC_BLE_ADV_START_TIMEOUT));
	zassert_equal(etc_ble_adv_gate_open(&gate), ETC_BLE_ADV_START_TIMEOUT);
}

ZTEST(etc_ble_adv_gate, test_open_without_request_replays_nothing)
{
	zassert_equal(etc_ble_adv_gate_open(&gate), ETC_BLE_ADV_NONE);
}

ZTEST(etc_ble_adv_gate, test_request_after_open_runs_immediately)
{
	etc_ble_adv_gate_open(&gate);

	zassert_true(etc_ble_adv_gate_request(&gate, ETC_BLE_ADV_START_TIMEOUT));
	zassert_true(etc_ble_adv_gate_request(&gate, ETC_BLE_ADV_START));
}

/* A latched request is replayed once, not on every later open. */
ZTEST(etc_ble_adv_gate, test_latched_request_is_consumed_by_open)
{
	etc_ble_adv_gate_request(&gate, ETC_BLE_ADV_START);

	zassert_equal(etc_ble_adv_gate_open(&gate), ETC_BLE_ADV_START);
	zassert_equal(etc_ble_adv_gate_open(&gate), ETC_BLE_ADV_NONE);
}

/* Two swipes during boot collapse into one advertising start. */
ZTEST(etc_ble_adv_gate, test_last_request_wins_while_closed)
{
	etc_ble_adv_gate_request(&gate, ETC_BLE_ADV_START_TIMEOUT);
	etc_ble_adv_gate_request(&gate, ETC_BLE_ADV_START);

	zassert_equal(etc_ble_adv_gate_open(&gate), ETC_BLE_ADV_START);
}
