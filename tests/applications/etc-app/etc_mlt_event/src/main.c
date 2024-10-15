/*
 * Copyright (c) 2024 EXACT Technology Corporation
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "etc_device.h"
#include "etc_device_record.h"
#include "etc_memfault_event.h"
#include "etc_util.h"
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(etc_mlf_event_test, CONFIG_ETC_APP_LOG_LEVEL);

extern bool prv_platform_nv_event_storage_enabled(void);
extern bool prv_platform_nv_event_storage_read_has_event(size_t *event_size);
extern bool prv_platform_nv_event_storage_read(uint32_t offset, void *buf, size_t buf_len);
extern void prv_platform_nv_event_storage_consume(void);
extern bool prv_platform_nv_event_storage_write(MemfaultEventReadCallback event_read_cb,
						size_t total_size);

static int select_event = 0;

typedef struct {
	const uint8_t *data;
	size_t written;
	size_t len;
} sFakePersistedEvent;

const uint8_t test_event0[] = {
	0xa7, 0x02, 0x01, 0x03, 0x01, 0x07, 0x69, 'D', 'A', 'A',  'B',	'B', 'C',
	'C',  'D',  'D',  0x0a, 0x64, 'm',  'a',  'i', 'n', 0x09, 0x65, '1', '.',
	'2',  '.',  '3',  0x06, 0x66, 'e',  'v',  't', '_', '2',  '4',
};

const uint8_t test_event1[] = {
	0xa6, 0x02, 0x01, 0x03, 0x01, 0x0a, 0x64, 'm', 'a', 'i', 'n', 0x09, 0x65,
	'1',  '.',  '2',  '.',	'3',  0x06, 0x66, 'e', 'v', 't', '_', '2',  '4',
};

static sFakePersistedEvent s_fake_persisted_events[] = {
	{.data = &test_event0[0], .len = sizeof(test_event0), .written = 0},
	{.data = &test_event1[0], .len = sizeof(test_event1), .written = 0},
};

static bool MemfaultEventReadCallbackHandler(uint32_t offset, void *buf, size_t buf_len)
{
	sFakePersistedEvent *event = &s_fake_persisted_events[select_event];
	memcpy(buf, &event->data[event->written], buf_len);
	event->written += buf_len;
	return true;
}

static void MemfaultEventReset(void)
{
	sFakePersistedEvent *event = &s_fake_persisted_events[select_event];
	event->written = 0;
}

static size_t MemfaultEventReadEventLen(void)
{
	sFakePersistedEvent *event = &s_fake_persisted_events[select_event];
	return event->len;
}

static uint8_t *MemfaultEventReadEventData(void)
{
	sFakePersistedEvent *event = &s_fake_persisted_events[select_event];
	return (uint8_t *)event->data;
}

void memfault_sdk_assert_func(void)
{
}

static void *test_setup(void)
{
	memfault_etc_event_init();
	return NULL;
}

static void test_teardown(void *)
{
	/* Allow ack/nack status to be saved to flash */
	k_sleep(K_SECONDS(CONFIG_ETC_DEVICE_SAVE_RECORD_SEC + 1));
}

ZTEST(etc_mlf_event_test, test_mutliples_events)
{
	select_event = 0;
	bool rc = true;
	size_t event_size = 0;
	uint8_t buf[128] = {0x00};
	MemfaultEventReset();
	rc = prv_platform_nv_event_storage_write(MemfaultEventReadCallbackHandler,
						 MemfaultEventReadEventLen());
	zassert_true(rc);
	rc = prv_platform_nv_event_storage_read_has_event(&event_size);
	zassert_true(rc);
	rc = prv_platform_nv_event_storage_read(0, buf, event_size);
	zassert_true(rc);
	zassert_equal(event_size, MemfaultEventReadEventLen());
	rc = memcmp(buf, MemfaultEventReadEventData(), event_size);
	zassert_ok(rc);
	prv_platform_nv_event_storage_consume();
	select_event = 1;
	MemfaultEventReset();
	rc = prv_platform_nv_event_storage_write(MemfaultEventReadCallbackHandler,
						 MemfaultEventReadEventLen());
	zassert_true(rc);
	rc = prv_platform_nv_event_storage_read_has_event(&event_size);
	zassert_true(rc);
	rc = prv_platform_nv_event_storage_read(0, buf, event_size);
	zassert_true(rc);
	zassert_equal(event_size, MemfaultEventReadEventLen());
	rc = memcmp(buf, MemfaultEventReadEventData(), event_size);
	prv_platform_nv_event_storage_consume();
}

ZTEST_SUITE(etc_mlf_event_test, NULL, test_setup, NULL, NULL, test_teardown);