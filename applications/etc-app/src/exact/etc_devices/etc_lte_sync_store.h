/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef ETC_LTE_SYNC_STORE_H_
#define ETC_LTE_SYNC_STORE_H_

#include <stdint.h>
#include <time.h>

/**
 * @brief Restore the opportunistic-LTE backoff state from retained RAM.
 *
 * Called once during device init. Loads the last-attempt timestamp and
 * consecutive-failure count, or zeroes them if the retained region is absent
 * or fails its CRC. Survives warm reboots so a crash loop cannot defeat the
 * FW-789 battery guard.
 */
void etc_lte_sync_store_init(void);

/** @return Unix seconds of the last LTE bring-up, or 0 if none since cold boot. */
time_t etc_lte_sync_get_last_attempt(void);

/** @return Number of consecutive failed LTE bring-ups since the last success. */
uint32_t etc_lte_sync_get_failures(void);

/**
 * @brief Record that an LTE bring-up was attempted (FW-789: stamp on attempt).
 *
 * @param now Unix seconds when the bring-up started.
 */
void etc_lte_sync_record_attempt(time_t now);

/** @brief Record a successful LTE connection; clears the failure count. */
void etc_lte_sync_record_success(void);

/** @brief Record a failed LTE bring-up; grows the failure count (saturating). */
void etc_lte_sync_record_failure(void);

/** @brief Clear the backoff state (last attempt and failures). For HIL tests. */
void etc_lte_sync_store_reset(void);

#endif /* ETC_LTE_SYNC_STORE_H_ */
