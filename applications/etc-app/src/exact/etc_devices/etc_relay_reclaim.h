/*
 * Copyright (c) 2025 EXACT Technology Corporation
 */

#ifndef ETC_RELAY_RECLAIM_H_
#define ETC_RELAY_RECLAIM_H_

#include "etc_device.h"
#include <stdbool.h>
#include <zephyr/sys/atomic.h>

/* Maximum reclaim relay supports */
#define ETC_RECLAIM_RELAY_MAX_ELEMENT (20)

/* Structure for request reclaim over cloud or BLE */
struct etc_device_reclaim_request {
	char logger_id[ETC_DEVICE_LORA_LOGGER_ID_SIZE];
	int start_time;
	int stop_time;
	int32_t created_at;
	atomic_t flag_set;
};

/**
 * @brief Restore the reclaim request list from retained RAM.
 *
 * Called once during device init.
 */
void etc_relay_reclaim_init(void);

/**
 * Set the reclaim request for Relay over cloud
 *
 * @param logger_id 	The logger id that relay want to reclaim
 * @param start_time	The start time of the reclaim range.
 * @param stop_time	The stop time of the reclaim range.
 * @return 0 on success (free slot used), 1 on success with a stale entry evicted,
 *         <0 on error (-EINVAL on bad args, -ENOMEM when buffer is full and no
 *         stale entry could be evicted).
 */
int etc_relay_reclaim_set(char *logger_id, int start_time, int stop_time);

/**
 * Visitor function for etc_relay_reclaim_for_each(). Invoked while the reclaim
 * mutex is held — do not block or call back into the reclaim API.
 */
typedef void (*etc_relay_reclaim_visitor_fn)(int idx, const struct etc_device_reclaim_request *req,
					     void *ctx);

/**
 * @brief Return the number of currently-active reclaim slots.
 *
 * @return >=0 active slot count, -EINVAL if the device is not in relay mode.
 */
int etc_relay_reclaim_count(void);

/**
 * @brief Copy out the reclaim entry at a specific slot index.
 *
 * @param idx Slot index in [0, ETC_RECLAIM_RELAY_MAX_ELEMENT).
 * @param out Output buffer; populated on success.
 * @return 0 on success, -EINVAL on bad args, -ENOENT if the slot is not active.
 */
int etc_relay_reclaim_get_by_index(int idx, struct etc_device_reclaim_request *out);

/**
 * @brief Clear all reclaim entries and persist the empty list.
 *
 * @return Number of active entries that were cleared.
 */
int etc_relay_reclaim_clear_all(void);

/**
 * @brief Iterate over all active reclaim entries.
 *
 * @param fn  Visitor invoked once per active slot while the mutex is held.
 * @param ctx Opaque pointer forwarded to `fn`.
 * @return Number of entries visited, or -EINVAL on bad args.
 */
int etc_relay_reclaim_for_each(etc_relay_reclaim_visitor_fn fn, void *ctx);

/**
 * Retrieve the reclaim request for a specific logger ID
 *
 * @param logger_id 	The logger id to check if any reclaim request
 * @param request 	The output to store the reclaim request
 * @return 0 on success, <0 on error.
 */
int etc_relay_reclaim_get_by_logger_id(const char *logger_id,
				       struct etc_device_reclaim_request *request);

/**
 * Clear the reclaim request for a logger if the supplied timestamp falls within the
 * requested window [start_time, stop_time]. The cleared entry is immediately
 * persisted to retained RAM so it survives a subsequent reset.
 *
 * @param logger_id  Logger whose reclaim entry should be checked.
 * @param timestamp  Timestamp of the arriving reclaim record.
 *
 * @retval 0       Request found and cleared.
 * @retval -ENOENT No matching active request found.
 */
int etc_relay_reclaim_clear_if_satisfied(const char *logger_id, int timestamp);

#endif /* ETC_RELAY_RECLAIM_H_ */
