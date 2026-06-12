/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <string.h>

#include <zephyr/net/lwm2m.h>
#include <zephyr/ztest.h>

#include "lwm2m_engine.h"
#include "etc_relay_obj_48935.h"

static void *test_setup(void)
{
	return NULL;
}

ZTEST(lwm2m_etc_relay_obj, test_object_registered)
{
	struct lwm2m_engine_obj *obj = lwm2m_engine_get_obj(&LWM2M_OBJ(ETC_RELAY_OBJECT_ID));
	zassert_not_null(obj, "Object not registered");
	zassert_equal(obj->obj_id, ETC_RELAY_OBJECT_ID);
}

ZTEST(lwm2m_etc_relay_obj, test_response_resource_present)
{
	/* The object is auto-created at SYS_INIT; resources are visible on
	 * instance 0. */
	char readback[ETC_RELAY_OBJ_RESPONSE_MAX_LEN];
	uint16_t read_len = sizeof(readback);
	int ret = lwm2m_get_res_buf(&LWM2M_OBJ(ETC_RELAY_OBJECT_ID, 0, ETC_RELAY_OBJ_R_RESPONSE),
				    (void **)&(void *){NULL}, &read_len, NULL, NULL);
	zassert_ok(ret, "Response resource buffer not retrievable (ret=%d)", ret);
	zassert_equal(read_len, ETC_RELAY_OBJ_RESPONSE_MAX_LEN,
		      "Response buffer should be %d bytes, got %u",
		      ETC_RELAY_OBJ_RESPONSE_MAX_LEN, read_len);
}

ZTEST(lwm2m_etc_relay_obj, test_response_resource_set_and_get)
{
	const char *payload = "count=42";
	int ret = lwm2m_set_string(&LWM2M_OBJ(ETC_RELAY_OBJECT_ID, 0, ETC_RELAY_OBJ_R_RESPONSE),
				   payload);
	zassert_ok(ret, "lwm2m_set_string failed: %d", ret);

	char readback[ETC_RELAY_OBJ_RESPONSE_MAX_LEN] = {0};
	ret = lwm2m_get_string(&LWM2M_OBJ(ETC_RELAY_OBJECT_ID, 0, ETC_RELAY_OBJ_R_RESPONSE),
			       readback, sizeof(readback));
	zassert_ok(ret, "lwm2m_get_string failed: %d", ret);
	zassert_ok(strcmp(readback, payload), "Response readback mismatch: expected '%s' got '%s'",
		   payload, readback);
}

ZTEST_SUITE(lwm2m_etc_relay_obj, NULL, test_setup, NULL, NULL, NULL);
