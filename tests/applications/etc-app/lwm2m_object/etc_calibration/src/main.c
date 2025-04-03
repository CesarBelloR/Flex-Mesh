#include <zephyr/ztest.h>
#include "lwm2m_engine.h"
#include "etc_calibration_obj_48950.h"

#define MAX_INSTANCE_COUNT 1

/* Test setup: Initialize the LwM2M object */
static void *test_setup(void)
{
	return NULL;
}

/* Test case: Verify that the object is registered correctly */
ZTEST(lwm2m_etc_calibration_obj, test_object_registered)
{
	struct lwm2m_engine_obj *obj = lwm2m_engine_get_obj(&LWM2M_OBJ(ETC_CALIBRATION_OBJ_ID));
	zassert_not_null(obj, "Object not registered");
	zassert_equal(obj->obj_id, ETC_CALIBRATION_OBJ_ID, "Object ID mismatch");
}

/* Test case: Verify resource initialization */
ZTEST(lwm2m_etc_calibration_obj, test_resource_initialization)
{
	int ret;
	struct lwm2m_engine_obj_inst *obj_inst;
	lwm2m_create_obj_inst(ETC_CALIBRATION_OBJ_ID, 0, &obj_inst);

	zassert_not_null(obj_inst->resources, "Resources not initialized");
	zassert_equal(obj_inst->resource_count, 6, "Resource count mismatch");

	/* Verify specific resource data */
	zassert_equal(obj_inst->resources[0].res_id, ETC_CALIBRATION_R_TYPE);
	zassert_equal(obj_inst->resources[1].res_id, ETC_CALIBRATION_R_TIME);
	zassert_equal(obj_inst->resources[2].res_id, ETC_CALIBRATION_R_ID);
	zassert_equal(obj_inst->resources[3].res_id, ETC_CALIBRATION_R_OFFSET);
	zassert_equal(obj_inst->resources[4].res_id, ETC_CALIBRATION_R_HIGH);
	zassert_equal(obj_inst->resources[5].res_id, ETC_CALIBRATION_R_REFERENCE);

	ret = lwm2m_delete_obj_inst(ETC_CALIBRATION_OBJ_ID, 0);
	zassert_ok(ret, "Failed to delete object instance");
}

/*  duplicate object instance creation */
ZTEST(lwm2m_etc_calibration_obj, test_duplicate_instance_creation)
{
	int ret;
	struct lwm2m_engine_obj_inst *obj_inst;

	lwm2m_create_obj_inst(ETC_CALIBRATION_OBJ_ID, 0, &obj_inst);
	ret = lwm2m_create_obj_inst(ETC_CALIBRATION_OBJ_ID, 0, &obj_inst);
	zassert_true(ret < 0, "Allowed duplicate object instance creation");

	ret = lwm2m_delete_obj_inst(ETC_CALIBRATION_OBJ_ID, 0);
	zassert_ok(ret, "Failed to delete object instance");
}

/* verify object instance creation and limit*/
ZTEST(lwm2m_etc_calibration_obj, test_instance_creation_deletion_limit)
{
	struct lwm2m_engine_obj_inst *obj_inst;
	int ret;

	ret = lwm2m_create_obj_inst(ETC_CALIBRATION_OBJ_ID, 0, &obj_inst);
	zassert_ok(ret, "Failed to create object instance");

	ret = lwm2m_create_obj_inst(ETC_CALIBRATION_OBJ_ID, MAX_INSTANCE_COUNT, &obj_inst);
	zassert_true(ret < 0, "Allowed creation beyond instance limit %d", ret);

	ret = lwm2m_delete_obj_inst(ETC_CALIBRATION_OBJ_ID, 0);
	zassert_ok(ret, "Failed to delete object instance");
}

/* Test suite */
ZTEST_SUITE(lwm2m_etc_calibration_obj, NULL, test_setup, NULL, NULL, NULL);
