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

ZTEST(lwm2m_etc_reclaim_obj, test_cancel_resource_executable)
{
	struct lwm2m_engine_obj *obj = lwm2m_engine_get_obj(&LWM2M_OBJ(ETC_RECLAIM_OBJECT_ID));
	zassert_not_null(obj, "Object not registered");

	const struct lwm2m_engine_obj_field *cancel = NULL;

	for (int i = 0; i < obj->field_count; i++) {
		if (obj->fields[i].res_id == ETC_RECLAIM_OBJ_R_CANCEL) {
			cancel = &obj->fields[i];
			break;
		}
	}

	zassert_not_null(cancel, "Cancel resource not present");
	zassert_true(cancel->permissions & BIT(LWM2M_OP_EXECUTE),
		     "Cancel resource is not executable");
}

ZTEST_SUITE(lwm2m_etc_reclaim_obj, NULL, test_setup, NULL, NULL, NULL);
