/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include "etc_lte_sync_store.h"

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_lte_sync_store, CONFIG_ETC_LTE_SYNC_STORE_LOG_LEVEL);

/* Failures beyond this add nothing: the backoff window already saturates at its
 * ceiling, and it bounds the doubling shift in app_module_lte_sync_backoff_s(). */
#define ETC_LTE_SYNC_FAILURES_MAX 31U

#define ETC_LTE_SYNC_RAM_MAGIC 0x17E5C0DEU
#define LTE_SYNC_RAM_MAGIC_OFFSET 0
#define LTE_SYNC_RAM_DATA_OFFSET sizeof(uint32_t)

#pragma pack(push, 1)
struct etc_lte_sync_ram {
	int64_t last_attempt;          /* 8 */
	uint32_t consecutive_failures; /* 4 */
}; /* = 12 bytes */
#pragma pack(pop)

#define LTE_SYNC_RAM_CRC_OFFSET (LTE_SYNC_RAM_DATA_OFFSET + sizeof(struct etc_lte_sync_ram))

static const struct device *lte_sync_ram_dev = DEVICE_DT_GET(DT_ALIAS(lte_sync_ram));
K_MUTEX_DEFINE(lte_sync_mtx);

static struct etc_lte_sync_ram state;

static void lte_sync_persist(void)
{
	uint32_t magic = ETC_LTE_SYNC_RAM_MAGIC;
	uint32_t crc = crc32_ieee((uint8_t *)&state, sizeof(state));

	/* Write data first, then CRC, then magic — magic written last so a reboot
	 * mid-write leaves magic absent and the load path discards. */
	int rc = retained_mem_write(lte_sync_ram_dev, LTE_SYNC_RAM_DATA_OFFSET, (uint8_t *)&state,
				    sizeof(state));
	if (rc) {
		LOG_ERR("Failed to write LTE-sync data to retained RAM: %d", rc);
		return;
	}
	rc = retained_mem_write(lte_sync_ram_dev, LTE_SYNC_RAM_CRC_OFFSET, (uint8_t *)&crc,
				sizeof(crc));
	if (rc) {
		LOG_ERR("Failed to write LTE-sync CRC to retained RAM: %d", rc);
		return;
	}
	rc = retained_mem_write(lte_sync_ram_dev, LTE_SYNC_RAM_MAGIC_OFFSET, (uint8_t *)&magic,
				sizeof(magic));
	if (rc) {
		LOG_ERR("Failed to write LTE-sync magic to retained RAM: %d", rc);
	}
}

static void lte_sync_load(void)
{
	uint32_t magic = 0;
	uint32_t stored_crc = 0;

	memset(&state, 0, sizeof(state));

	if (!device_is_ready(lte_sync_ram_dev)) {
		LOG_ERR("LTE-sync retained-mem device not ready");
		return;
	}

	int rc = retained_mem_read(lte_sync_ram_dev, LTE_SYNC_RAM_MAGIC_OFFSET, (uint8_t *)&magic,
				   sizeof(magic));
	if (rc || magic != ETC_LTE_SYNC_RAM_MAGIC) {
		LOG_INF("LTE-sync retained RAM: no valid data (magic=0x%08x), starting fresh",
			magic);
		lte_sync_persist();
		return;
	}

	rc = retained_mem_read(lte_sync_ram_dev, LTE_SYNC_RAM_DATA_OFFSET, (uint8_t *)&state,
			       sizeof(state));
	if (rc) {
		LOG_ERR("Failed to read LTE-sync data from retained RAM: %d", rc);
		memset(&state, 0, sizeof(state));
		return;
	}

	rc = retained_mem_read(lte_sync_ram_dev, LTE_SYNC_RAM_CRC_OFFSET, (uint8_t *)&stored_crc,
			       sizeof(stored_crc));
	if (rc || stored_crc != crc32_ieee((uint8_t *)&state, sizeof(state))) {
		LOG_WRN("LTE-sync retained RAM: CRC mismatch, discarding");
		memset(&state, 0, sizeof(state));
		lte_sync_persist();
		return;
	}

	if (state.consecutive_failures > ETC_LTE_SYNC_FAILURES_MAX) {
		state.consecutive_failures = ETC_LTE_SYNC_FAILURES_MAX;
	}
	LOG_DBG("LTE-sync state restored: last=%lld failures=%u", (long long)state.last_attempt,
		state.consecutive_failures);
}

void etc_lte_sync_store_init(void)
{
	k_mutex_lock(&lte_sync_mtx, K_FOREVER);
	lte_sync_load();
	k_mutex_unlock(&lte_sync_mtx);
}

time_t etc_lte_sync_get_last_attempt(void)
{
	time_t last;

	k_mutex_lock(&lte_sync_mtx, K_FOREVER);
	last = (time_t)state.last_attempt;
	k_mutex_unlock(&lte_sync_mtx);
	return last;
}

uint32_t etc_lte_sync_get_failures(void)
{
	uint32_t failures;

	k_mutex_lock(&lte_sync_mtx, K_FOREVER);
	failures = state.consecutive_failures;
	k_mutex_unlock(&lte_sync_mtx);
	return failures;
}

void etc_lte_sync_record_attempt(time_t now)
{
	k_mutex_lock(&lte_sync_mtx, K_FOREVER);
	state.last_attempt = (int64_t)now;
	lte_sync_persist();
	k_mutex_unlock(&lte_sync_mtx);
}

void etc_lte_sync_record_success(void)
{
	k_mutex_lock(&lte_sync_mtx, K_FOREVER);
	if (state.consecutive_failures != 0) {
		state.consecutive_failures = 0;
		lte_sync_persist();
	}
	k_mutex_unlock(&lte_sync_mtx);
}

void etc_lte_sync_record_failure(void)
{
	k_mutex_lock(&lte_sync_mtx, K_FOREVER);
	if (state.consecutive_failures < ETC_LTE_SYNC_FAILURES_MAX) {
		state.consecutive_failures++;
		lte_sync_persist();
	}
	k_mutex_unlock(&lte_sync_mtx);
}
