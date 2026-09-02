/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef ETC_BLE_ADV_GATE_H_
#define ETC_BLE_ADV_GATE_H_

#include <stdbool.h>
#include <zephyr/spinlock.h>

/** Advertising request kinds a caller can make before the stack is ready. */
enum etc_ble_adv_request {
	ETC_BLE_ADV_NONE,
	/** Advertise until stopped (BLE mode). */
	ETC_BLE_ADV_START,
	/** Advertise for the magnet timeout (LTE/LoRa mode). */
	ETC_BLE_ADV_START_TIMEOUT,
};

/**
 * Gate between advertising requests and Bluetooth initialisation. A magnet
 * swipe can land while the stack is still enabling; the request is latched
 * and replayed once the gate opens instead of touching the stack early.
 */
struct etc_ble_adv_gate {
	struct k_spinlock lock;
	bool ready;
	enum etc_ble_adv_request pending;
};

/**
 * @brief Ask to advertise.
 *
 * @param gate The gate.
 * @param request What to start.
 * @return true if the caller may start now; false if the request was latched.
 */
static inline bool etc_ble_adv_gate_request(struct etc_ble_adv_gate *gate,
					    enum etc_ble_adv_request request)
{
	k_spinlock_key_t key = k_spin_lock(&gate->lock);
	bool ready = gate->ready;

	if (!ready) {
		gate->pending = request;
	}
	k_spin_unlock(&gate->lock, key);
	return ready;
}

/**
 * @brief Open the gate once the stack is ready.
 *
 * @param gate The gate.
 * @return The request latched while closed, ETC_BLE_ADV_NONE if none.
 */
static inline enum etc_ble_adv_request etc_ble_adv_gate_open(struct etc_ble_adv_gate *gate)
{
	k_spinlock_key_t key = k_spin_lock(&gate->lock);
	enum etc_ble_adv_request pending = gate->pending;

	gate->ready = true;
	gate->pending = ETC_BLE_ADV_NONE;
	k_spin_unlock(&gate->lock, key);
	return pending;
}

#endif /* ETC_BLE_ADV_GATE_H_ */
