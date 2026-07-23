/*
 * Copyright (c) 2025 EXACT Technology Corporation
 */

#include "etc_relay_reclaim.h"
#include "etc_device.h"
#include "etc_date_time.h"

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_relay_reclaim, CONFIG_ETC_RELAY_RECLAIM_LOG_LEVEL);

static struct etc_device_reclaim_request list_reclaim_request[ETC_RECLAIM_RELAY_MAX_ELEMENT];
K_MUTEX_DEFINE(reclaim_request_mtx);

static const struct device *reclaim_ram_dev = DEVICE_DT_GET(DT_ALIAS(reclaim_request_ram));

#define ETC_RECLAIM_RAM_MAGIC	      0xDEADC0DEU
#define ETC_RECLAIM_STALE_TIMEOUT_SEC (5 * 24 * 3600)
#define RECLAIM_RAM_MAGIC_OFFSET      0
#define RECLAIM_RAM_DATA_OFFSET	      sizeof(uint32_t)
#define RECLAIM_RAM_CRC_OFFSET                                                                     \
	(RECLAIM_RAM_DATA_OFFSET +                                                                 \
	 sizeof(struct etc_reclaim_request_ram) * ETC_RECLAIM_RELAY_MAX_ELEMENT)

#pragma pack(push, 1)
struct etc_reclaim_request_ram {
	char logger_id[ETC_DEVICE_LORA_LOGGER_ID_SIZE]; /* 17 */
	int32_t start_time;				/*  4 */
	int32_t stop_time;				/*  4 */
	int32_t created_at;				/*  4 */
	uint8_t active;					/*  1 */
	uint8_t _pad[2];				/*  2 */
}; /* = 32 bytes */
#pragma pack(pop)

/* The ReclaimRetainedMem region was shrunk to carve out LteSyncRetainedMem from
 * its tail; ensure the reclaim payload still fits what remains. */
BUILD_ASSERT(RECLAIM_RAM_CRC_OFFSET + sizeof(uint32_t) <=
		     DT_REG_SIZE(DT_PARENT(DT_ALIAS(reclaim_request_ram))),
	     "Reclaim retained data exceeds its retained-RAM region");

static void reclaim_list_zero(void)
{
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		atomic_set(&list_reclaim_request[i].flag_set, false);
		list_reclaim_request[i].start_time = 0;
		list_reclaim_request[i].stop_time = 0;
		list_reclaim_request[i].created_at = 0;
		list_reclaim_request[i].logger_id[0] = '\0';
	}
}

static void reclaim_persist(void)
{
	struct etc_reclaim_request_ram store[ETC_RECLAIM_RELAY_MAX_ELEMENT];
	uint32_t magic = ETC_RECLAIM_RAM_MAGIC;

	memset(store, 0, sizeof(store));
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		store[i].active = (uint8_t)atomic_get(&req->flag_set);
		store[i].start_time = req->start_time;
		store[i].stop_time = req->stop_time;
		store[i].created_at = req->created_at;
		memcpy(store[i].logger_id, req->logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE);
	}

	uint32_t crc = crc32_ieee((uint8_t *)store, sizeof(store));

	/* Write data first, then CRC, then magic — magic written last so a
	 * reboot mid-write leaves magic absent and the load path discards. */
	int rc = retained_mem_write(reclaim_ram_dev, RECLAIM_RAM_DATA_OFFSET, (uint8_t *)store,
				    sizeof(store));
	if (rc) {
		LOG_ERR("Failed to write reclaim data to retained RAM: %d", rc);
		return;
	}
	rc = retained_mem_write(reclaim_ram_dev, RECLAIM_RAM_CRC_OFFSET, (uint8_t *)&crc,
				sizeof(crc));
	if (rc) {
		LOG_ERR("Failed to write reclaim CRC to retained RAM: %d", rc);
		return;
	}
	rc = retained_mem_write(reclaim_ram_dev, RECLAIM_RAM_MAGIC_OFFSET, (uint8_t *)&magic,
				sizeof(magic));
	if (rc) {
		LOG_ERR("Failed to write reclaim magic to retained RAM: %d", rc);
	}
}

static void reclaim_load(void)
{
	struct etc_reclaim_request_ram store[ETC_RECLAIM_RELAY_MAX_ELEMENT];
	uint32_t magic = 0;
	uint32_t stored_crc = 0;

	if (!device_is_ready(reclaim_ram_dev)) {
		LOG_ERR("Reclaim retained-mem device not ready");
		reclaim_list_zero();
		return;
	}

	int rc = retained_mem_read(reclaim_ram_dev, RECLAIM_RAM_MAGIC_OFFSET, (uint8_t *)&magic,
				   sizeof(magic));
	if (rc || magic != ETC_RECLAIM_RAM_MAGIC) {
		LOG_INF("Reclaim retained RAM: no valid data (magic=0x%08x), starting empty",
			magic);
		reclaim_list_zero();
		reclaim_persist();
		return;
	}

	rc = retained_mem_read(reclaim_ram_dev, RECLAIM_RAM_DATA_OFFSET, (uint8_t *)store,
			       sizeof(store));
	if (rc) {
		LOG_ERR("Failed to read reclaim data from retained RAM: %d", rc);
		reclaim_list_zero();
		return;
	}

	rc = retained_mem_read(reclaim_ram_dev, RECLAIM_RAM_CRC_OFFSET, (uint8_t *)&stored_crc,
			       sizeof(stored_crc));
	if (rc || stored_crc != crc32_ieee((uint8_t *)store, sizeof(store))) {
		LOG_WRN("Reclaim retained RAM: CRC mismatch, discarding");
		reclaim_list_zero();
		reclaim_persist();
		return;
	}

	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		atomic_set(&req->flag_set, store[i].active);
		req->start_time = store[i].start_time;
		req->stop_time = store[i].stop_time;
		req->created_at = store[i].created_at;
		memcpy(req->logger_id, store[i].logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE);
	}
	LOG_DBG("Reclaim request list restored from retained RAM");
}

void etc_relay_reclaim_init(void)
{
	reclaim_load();
}

static void reclaim_slot_assign(struct etc_device_reclaim_request *slot, const char *logger_id,
				int start_time, int stop_time, int32_t created_at)
{
	strncpy(slot->logger_id, logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE - 1);
	slot->logger_id[ETC_DEVICE_LORA_LOGGER_ID_SIZE - 1] = '\0';
	slot->start_time = start_time;
	slot->stop_time = stop_time;
	slot->created_at = created_at;
	atomic_set(&slot->flag_set, true);
}

int etc_relay_reclaim_set(char *logger_id, int start_time, int stop_time)
{
	if (!etc_device_is_relay()) {
		return -EINVAL;
	}

	if (start_time > stop_time) {
		return -EINVAL;
	}

	int now = date_time_now_second();
	int32_t created = (now >= 0) ? (int32_t)now : 0;

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);

	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *request = &list_reclaim_request[i];
		if (atomic_get(&request->flag_set)) {
			continue;
		}

		reclaim_slot_assign(request, logger_id, start_time, stop_time, created);
		reclaim_persist();
		k_mutex_unlock(&reclaim_request_mtx);
		LOG_DBG("Added reclaim request for %s [%d - %d]", logger_id, start_time, stop_time);
		return 0;
	}

	/* No free slot — evict the oldest stale entry if it is old enough */
	if (now < 0) {
		k_mutex_unlock(&reclaim_request_mtx);
		LOG_WRN("RTC not synced, cannot evict stale reclaim entry");
		return -ENOMEM;
	}

	int evict_idx = -1;
	int32_t oldest_created = INT32_MAX;
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		if (req->created_at > 0 &&
		    (now - req->created_at) >= ETC_RECLAIM_STALE_TIMEOUT_SEC) {
			if (req->created_at < oldest_created) {
				oldest_created = req->created_at;
				evict_idx = i;
			}
		}
	}

	if (evict_idx < 0) {
		k_mutex_unlock(&reclaim_request_mtx);
		LOG_WRN("Reclaim buffer full, no stale entry to evict");
		return -ENOMEM;
	}

	struct etc_device_reclaim_request *slot = &list_reclaim_request[evict_idx];
	LOG_INF("Evicting stale reclaim entry for %s (created=%d)", slot->logger_id,
		slot->created_at);
	reclaim_slot_assign(slot, logger_id, start_time, stop_time, created);
	reclaim_persist();
	k_mutex_unlock(&reclaim_request_mtx);
	LOG_DBG("Added reclaim request for %s [%d - %d] (evicted stale)", logger_id, start_time,
		stop_time);
	return 1;
}

/* Count active reclaim slots. Caller must hold reclaim_request_mtx. */
static int reclaim_active_count_locked(void)
{
	int count = 0;

	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		if (atomic_get(&list_reclaim_request[i].flag_set)) {
			count++;
		}
	}
	return count;
}

int etc_relay_reclaim_count(void)
{
	if (!etc_device_is_relay()) {
		return -EINVAL;
	}

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	int count = reclaim_active_count_locked();
	k_mutex_unlock(&reclaim_request_mtx);
	return count;
}

int etc_relay_reclaim_get_by_index(int idx, struct etc_device_reclaim_request *out)
{
	if (out == NULL || idx < 0 || idx >= ETC_RECLAIM_RELAY_MAX_ELEMENT) {
		return -EINVAL;
	}

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	struct etc_device_reclaim_request *req = &list_reclaim_request[idx];
	if (!atomic_get(&req->flag_set)) {
		k_mutex_unlock(&reclaim_request_mtx);
		return -ENOENT;
	}
	memcpy(out, req, sizeof(*out));
	k_mutex_unlock(&reclaim_request_mtx);
	return 0;
}

int etc_relay_reclaim_clear_all(void)
{
	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	int prior = reclaim_active_count_locked();
	reclaim_list_zero();
	reclaim_persist();
	k_mutex_unlock(&reclaim_request_mtx);
	LOG_INF("Cleared %d reclaim request(s)", prior);
	return prior;
}

int etc_relay_reclaim_for_each(etc_relay_reclaim_visitor_fn fn, void *ctx)
{
	if (fn == NULL) {
		return -EINVAL;
	}

	int visited = 0;

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		if (!atomic_get(&req->flag_set)) {
			continue;
		}
		fn(i, req, ctx);
		visited++;
	}
	k_mutex_unlock(&reclaim_request_mtx);
	return visited;
}

int etc_relay_reclaim_get_by_logger_id(const char *logger_id,
				       struct etc_device_reclaim_request *reclaim_request)
{
	if (logger_id == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *request = &list_reclaim_request[i];
		if (!atomic_get(&request->flag_set)) {
			continue;
		}
		if (strncmp(logger_id, request->logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE) == 0) {
			memcpy(reclaim_request, request, sizeof(*reclaim_request));
			k_mutex_unlock(&reclaim_request_mtx);
			return 0;
		}
	}
	k_mutex_unlock(&reclaim_request_mtx);
	return -ENOENT;
}

int etc_relay_reclaim_clear_if_satisfied(const char *logger_id, int timestamp)
{
	if (logger_id == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		if (!atomic_get(&req->flag_set)) {
			continue;
		}
		if (strncmp(logger_id, req->logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE) != 0) {
			continue;
		}
		int32_t start = req->start_time;
		int32_t stop = req->stop_time;
		if (timestamp >= start && timestamp <= stop) {
			atomic_set(&req->flag_set, false);
			req->start_time = 0;
			req->stop_time = 0;
			req->created_at = 0;
			req->logger_id[0] = '\0';
			reclaim_persist();
			k_mutex_unlock(&reclaim_request_mtx);
			LOG_INF("Reclaim request for %s satisfied by ts=%d [%d-%d]", logger_id,
				timestamp, start, stop);
			return 0;
		}
	}
	k_mutex_unlock(&reclaim_request_mtx);
	return -ENOENT;
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#include <stdlib.h>

static int cmd_relay_reclaim_set(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 4 || argc > 5) {
		shell_error(shell,
			    "Usage: relay_reclaim set <logger_id> <start> <stop> [created_at]");
		return -EINVAL;
	}
	char *logger_id = argv[1];
	int start = (int)strtol(argv[2], NULL, 10);
	int stop = (int)strtol(argv[3], NULL, 10);
	int rc = etc_relay_reclaim_set(logger_id, start, stop);
	if (rc < 0) {
		shell_error(shell, "Failed to set reclaim request: %d", rc);
		return rc;
	}
	/* Optional: override created_at for testing stale eviction */
	if (argc == 5) {
		int32_t created = (int32_t)strtol(argv[4], NULL, 10);
		k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
		for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
			struct etc_device_reclaim_request *req = &list_reclaim_request[i];
			if (atomic_get(&req->flag_set) &&
			    strncmp(req->logger_id, logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE) ==
				    0 &&
			    req->start_time == start && req->stop_time == stop) {
				req->created_at = created;
				break;
			}
		}
		reclaim_persist();
		k_mutex_unlock(&reclaim_request_mtx);
	}
	shell_print(shell, "Reclaim request set for %s [%d - %d]", logger_id, start, stop);
	return 0;
}

static int cmd_relay_reclaim_list(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int count = 0;

	k_mutex_lock(&reclaim_request_mtx, K_FOREVER);
	for (int i = 0; i < ETC_RECLAIM_RELAY_MAX_ELEMENT; i++) {
		struct etc_device_reclaim_request *req = &list_reclaim_request[i];
		if (atomic_get(&req->flag_set)) {
			shell_print(shell, "[%d] %s start=%d stop=%d", i, req->logger_id,
				    req->start_time, req->stop_time);
			count++;
		}
	}
	k_mutex_unlock(&reclaim_request_mtx);
	if (count == 0) {
		shell_print(shell, "No active reclaim requests");
	}
	return 0;
}

static int cmd_relay_reclaim_clear_all(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	etc_relay_reclaim_clear_all();
	shell_print(shell, "All reclaim requests cleared");
	return 0;
}

static int cmd_relay_reclaim_satisfy(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 3) {
		shell_error(shell, "Usage: relay_reclaim satisfy <logger_id> <timestamp>");
		return -EINVAL;
	}
	char *logger_id = argv[1];
	int timestamp = (int)strtol(argv[2], NULL, 10);
	int rc = etc_relay_reclaim_clear_if_satisfied(logger_id, timestamp);
	if (rc == 0) {
		shell_print(shell, "Reclaim satisfied for %s at ts=%d", logger_id, timestamp);
	} else {
		shell_print(shell, "No matching reclaim request for %s at ts=%d", logger_id,
			    timestamp);
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_relay_reclaim,
	SHELL_CMD_ARG(set, NULL, "Set reclaim request: <logger_id> <start> <stop> [created_at]",
		      cmd_relay_reclaim_set, 4, 1),
	SHELL_CMD(list, NULL, "List active reclaim requests", cmd_relay_reclaim_list),
	SHELL_CMD(clear_all, NULL, "Clear all reclaim requests", cmd_relay_reclaim_clear_all),
	SHELL_CMD_ARG(satisfy, NULL, "Satisfy reclaim request: <logger_id> <timestamp>",
		      cmd_relay_reclaim_satisfy, 3, 0),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(relay_reclaim, &sub_relay_reclaim, "Relay reclaim request management", NULL);

#endif
