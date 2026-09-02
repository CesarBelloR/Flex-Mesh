/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Per-channel send bookkeeping in data_module.c (FW-1194): the cloud and BLE
 * channels each own an in-flight record, so an ACK on one channel can neither
 * acknowledge nor clear the other's record.
 */

#include <zephyr/ztest.h>

#include "data_send_state.h"

static struct data_send_state cloud;
static struct data_send_state ble;

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&cloud, 0, sizeof(cloud));
	memset(&ble, 0, sizeof(ble));
}

ZTEST_SUITE(data_send_state, NULL, NULL, before, NULL, NULL);

ZTEST(data_send_state, test_finish_returns_record_and_clears)
{
	cloud.record_id = 42;
	data_send_state_begin(&cloud);
	zassert_true(cloud.active);

	zassert_equal(data_send_state_finish(&cloud), 42);
	zassert_false(cloud.active);
	zassert_equal(cloud.record_id, 0);
	zassert_equal(data_send_state_finish(&cloud), 0);
}

/* The FW-1194 interleaving: a BLE ACK arriving while a cloud record is in
 * flight must settle only the BLE record. */
ZTEST(data_send_state, test_ble_ack_leaves_cloud_send_in_flight)
{
	cloud.record_id = 10;
	data_send_state_begin(&cloud);
	ble.record_id = 11;
	data_send_state_begin(&ble);

	zassert_equal(data_send_state_finish(&ble), 11);

	zassert_true(cloud.active, "cloud send lost its in-flight state");
	zassert_equal(cloud.record_id, 10, "cloud record id clobbered");
	zassert_equal(data_send_state_finish(&cloud), 10);
}

ZTEST(data_send_state, test_cloud_ack_leaves_ble_send_in_flight)
{
	ble.record_id = 11;
	data_send_state_begin(&ble);
	cloud.record_id = 10;
	data_send_state_begin(&cloud);

	zassert_equal(data_send_state_finish(&cloud), 10);

	zassert_true(ble.active, "BLE send lost its in-flight state");
	zassert_equal(ble.record_id, 11, "BLE record id clobbered");
}

/* Cloud RX-off / disconnect resets clear only the cloud channel. */
ZTEST(data_send_state, test_cloud_reset_does_not_clear_ble)
{
	ble.record_id = 11;
	data_send_state_begin(&ble);

	data_send_state_finish(&cloud);

	zassert_true(ble.active);
	zassert_equal(ble.record_id, 11);
}
