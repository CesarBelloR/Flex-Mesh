/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <string.h>

#include <zephyr/net/lwm2m.h>
#include <zephyr/ztest.h>

#include "lwm2m_engine.h"
#include "etc_reclaim_obj_48934.h"

static void *test_setup(void)
{
	return NULL;
}

ZTEST(lwm2m_etc_reclaim_obj, test_object_registered)
{
	struct lwm2m_engine_obj *obj = lwm2m_engine_get_obj(&LWM2M_OBJ(ETC_RECLAIM_OBJECT_ID));
	zassert_not_null(obj, "Object not registered");
	zassert_equal(obj->obj_id, ETC_RECLAIM_OBJECT_ID);
}

ZTEST_SUITE(lwm2m_etc_reclaim_obj, NULL, test_setup, NULL, NULL, NULL);
