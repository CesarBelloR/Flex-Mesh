#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef CONFIG_ZTEST
#include <memfault/core/trace_event.h>
#include <memfault/core/data_packetizer_source.h>
#include <memfault/core/event_storage.h>
#include <memfault/core/event_storage_implementation.h>
#include <memfault/core/math.h>
#include <memfault/core/platform/nonvolatile_event_storage.h>
#endif
#include <memfault/util/circular_buffer.h>
#include <memfault/core/batched_events.h>
#include <memfault/core/math.h>
#include "etc_memfault_event.h"
#include <zephyr/logging/log.h>
#ifndef CONFIG_ZTEST
LOG_MODULE_REGISTER(ETC_MFLT_EVENT, CONFIG_MEMFAULT_ETC_LOG_LEVEL);
#else
LOG_MODULE_REGISTER(ETC_MFLT_EVENT, LOG_LEVEL_DBG);
#endif
#include "etc_device.h"

ETC_MFLT_PUT_IN_SECTION(".noinit.mflt_event_info")
static uint8_t s_etc_event_buf[CONFIG_MEMFAULT_ETC_EVENT_NOINIT_SIZE];

// This structure keeps track of the current state of events in storage.
struct s_mflt_etc_event {
	uint16_t flag;
	uint16_t num_events;
	sMemfaultBatchedEventsHeader event_header;
	sMfltCircularBuffer event_storage;
};

struct __attribute__((packed)) s_mflt_etc_header {
	uint16_t total_size;
};

// Initialize the event structure
static struct s_mflt_etc_event *mflt_etc_event = NULL;

// Initialize the event fifo
static sMfltCircularBuffer *p_event_storage = NULL;

// Constant for a specific event flag used for identifying.
#define ETC_MFLT_EVENT_FLAG (0xAABB)

// Defines the maximum buffer size available for events
#define ETC_MFLT_EVENT_MAX_BUF_SIZE                                                                \
	(CONFIG_MEMFAULT_ETC_EVENT_NOINIT_SIZE - sizeof(struct s_mflt_etc_event))

static bool s_nv_storage_enabled = true;

static size_t mflt_etc_last_read = 0;

static bool memfault_etc_event_is_init(void)
{
	return (mflt_etc_event->flag == ETC_MFLT_EVENT_FLAG);
}

static size_t memfault_etc_get_total_event_size(void)
{
	if (mflt_etc_event->num_events == 0) {
		return 0;
	}

	const size_t hdr_overhead_bytes =
		mflt_etc_event->num_events * sizeof(struct s_mflt_etc_header);
	return memfault_circular_buffer_get_read_size(p_event_storage) +
	       mflt_etc_event->event_header.length - hdr_overhead_bytes;
}

ETC_MFLT_EXPORT_FUNC
bool prv_platform_nv_event_storage_enabled(void)
{
	return s_nv_storage_enabled;
}

ETC_MFLT_EXPORT_FUNC
bool prv_platform_nv_event_storage_read_has_event(size_t *event_size)
{
	if (!memfault_etc_event_is_init())
		return false;

	if (mflt_etc_event->num_events == 0) {
		return false;
	}

	*event_size = memfault_etc_get_total_event_size();
	memfault_batched_events_build_header(mflt_etc_event->num_events,
					     &mflt_etc_event->event_header);
	LOG_DBG("Event size %d", *event_size);
	return true;
}

ETC_MFLT_EXPORT_FUNC
bool prv_platform_nv_event_storage_read(uint32_t offset, void *buf, size_t buf_len)
{
	if (!memfault_etc_event_is_init())
		return false;
	if (mflt_etc_event->num_events == 0) {
		return false;
	}

	const size_t total_event_size = memfault_etc_get_total_event_size();
	LOG_DBG("Offset %d - Len %d - Total %d", offset, buf_len, total_event_size);
	if ((offset + buf_len) > total_event_size) {
		return false;
	}

	uint8_t *bufp = (uint8_t *)buf;
	if (offset < mflt_etc_event->event_header.length) {
		const size_t bytes_to_copy =
			MEMFAULT_MIN(buf_len, mflt_etc_event->event_header.length - offset);
		memcpy(bufp, &mflt_etc_event->event_header.data[offset], bytes_to_copy);
		buf_len -= bytes_to_copy;
		offset = 0;
		bufp += bytes_to_copy;
	} else {
		offset -= mflt_etc_event->event_header.length;
	}

	uint32_t curr_offset = 0;
	uint32_t read_offset = 0;

	while (buf_len > 0) {
		struct s_mflt_etc_header hdr = {0};
		const bool success = memfault_circular_buffer_read(p_event_storage, read_offset,
								   &hdr, sizeof(hdr));
		if (!success) {
			return false;
		}

		read_offset += sizeof(hdr);
		const size_t event_size = hdr.total_size;
		if ((curr_offset + event_size) < offset) {
			// we haven't reached the offset we were trying to read from
			curr_offset += event_size;
			read_offset += event_size;
			continue;
		}

		const size_t evt_start_offset = offset - curr_offset;
		LOG_DBG("Event start offset: %d %d", read_offset, evt_start_offset);
		const size_t bytes_to_read = MEMFAULT_MIN(event_size - evt_start_offset, buf_len);
		if (!memfault_circular_buffer_read(p_event_storage, read_offset + evt_start_offset,
						   bufp, bytes_to_read)) {
			return false;
		}

		bufp += bytes_to_read;
		curr_offset += event_size;
		read_offset += event_size;
		buf_len -= bytes_to_read;
		offset += bytes_to_read;
	}

	mflt_etc_last_read = read_offset;
	return true;
}

ETC_MFLT_EXPORT_FUNC
void prv_platform_nv_event_storage_consume(void)
{
	if (!memfault_etc_event_is_init())
		return;
	mflt_etc_event->num_events = 0;
	memfault_circular_buffer_consume(p_event_storage, mflt_etc_last_read);
}

ETC_MFLT_EXPORT_FUNC
bool prv_platform_nv_event_storage_write(MemfaultEventReadCallback event_read_cb, size_t total_size)
{
	if (!memfault_etc_event_is_init())
		return false;
	bool rc = true;
	struct s_mflt_etc_header hdr = {.total_size = total_size};
	rc = memfault_circular_buffer_write(p_event_storage, &hdr, sizeof(hdr));
	if (rc) {
		uint8_t write_buf[total_size];
		for (size_t i = 0; i < total_size; i++) {
			event_read_cb(i, &write_buf[i], 1);
		}
		rc = memfault_circular_buffer_write(p_event_storage, write_buf, total_size);
	}
	if (rc) {
		mflt_etc_event->num_events += 1;
	}
	LOG_DBG("Write data [%d]: %d %d (%d,%d)", rc, total_size, mflt_etc_event->num_events,
		memfault_circular_buffer_get_read_size(p_event_storage),
		memfault_circular_buffer_get_write_size(p_event_storage));
	return rc;
}

#ifndef CONFIG_ZTEST
const sMemfaultNonVolatileEventStorageImpl g_memfault_platform_nv_event_storage_impl = {
	.enabled = prv_platform_nv_event_storage_enabled,
	.has_event = prv_platform_nv_event_storage_read_has_event,
	.read = prv_platform_nv_event_storage_read,
	.consume = prv_platform_nv_event_storage_consume,
	.write = prv_platform_nv_event_storage_write,
};
#endif

#ifndef CONFIG_ZTEST
void memfault_event_storage_request_persist_callback(
	const sMemfaultEventStoragePersistCbStatus *status)
{
	memfault_event_storage_persist();
}
#endif

void memfault_etc_event_init(void)
{
	mflt_etc_event = (struct s_mflt_etc_event *)&s_etc_event_buf[0];
	if (memfault_etc_event_is_init()) {
		LOG_DBG("Memfault Event has initialized");
	} else {
		LOG_DBG("Memfault Event didn't initialize");
		/* Initialize storage */
		memset(s_etc_event_buf, 0, sizeof(s_etc_event_buf));
		mflt_etc_event->flag = ETC_MFLT_EVENT_FLAG;
		mflt_etc_event->num_events = 0;
		memset(&mflt_etc_event->event_header, 0, sizeof(mflt_etc_event->event_header));
		memfault_circular_buffer_init(&mflt_etc_event->event_storage,
					      &s_etc_event_buf[sizeof(struct s_mflt_etc_event)],
					      ETC_MFLT_EVENT_MAX_BUF_SIZE);
	}
	/* Set buffer circular to no-init RAM buffer */
	p_event_storage = &mflt_etc_event->event_storage;
}
