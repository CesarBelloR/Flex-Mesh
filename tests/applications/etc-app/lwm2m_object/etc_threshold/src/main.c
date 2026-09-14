/*
 * Copyright (c) 2026 EXACT Technology
 */

#include <zephyr/ztest.h>
#include "lwm2m_engine.h"
#include "etc_threshold_obj_48944.h"

#define INSTANCE_COUNT	ETC_THRESHOLD_OBJ_MAX_INSTANCE_COUNT
#define RESOURCE_COUNT	7
#define FLOAT_TOLERANCE 1e-9

/* Path to resource @p rid of threshold slot @p inst. */
#define TH_PATH(inst, rid) (&LWM2M_OBJ(ETC_THRESHOLD_OBJECT_ID, inst, rid))

/* Restore the boot state: all slots present and holding their defaults. */
static void test_before(void *fixture)
{
	struct lwm2m_engine_obj_inst *obj_inst;

	ARG_UNUSED(fixture);

	for (uint16_t id = 0; id < INSTANCE_COUNT; id++) {
		(void)lwm2m_delete_obj_inst(ETC_THRESHOLD_OBJECT_ID, id);
	}

	for (uint16_t id = 0; id < INSTANCE_COUNT; id++) {
		zassert_ok(lwm2m_create_obj_inst(ETC_THRESHOLD_OBJECT_ID, id, &obj_inst),
			   "Failed to create instance %u", id);
	}
}

ZTEST(lwm2m_etc_threshold_obj, test_object_registered)
{
	struct lwm2m_engine_obj *obj = lwm2m_engine_get_obj(&LWM2M_OBJ(ETC_THRESHOLD_OBJECT_ID));

	zassert_not_null(obj, "Object not registered");
	zassert_equal(obj->obj_id, ETC_THRESHOLD_OBJECT_ID, "Object ID mismatch");
	zassert_equal(obj->max_instance_count, INSTANCE_COUNT, "Max instance count mismatch");
}

/* All four slots exist after init and expose the expected resources. */
ZTEST(lwm2m_etc_threshold_obj, test_instances_created_at_init)
{
	for (uint16_t id = 0; id < INSTANCE_COUNT; id++) {
		struct lwm2m_engine_obj_inst *obj_inst =
			lwm2m_engine_get_obj_inst(&LWM2M_OBJ(ETC_THRESHOLD_OBJECT_ID, id));

		zassert_not_null(obj_inst, "Instance %u missing", id);
		zassert_not_null(obj_inst->resources, "Instance %u has no resources", id);
		zassert_equal(obj_inst->resource_count, RESOURCE_COUNT,
			      "Instance %u resource count mismatch", id);

		zassert_equal(obj_inst->resources[0].res_id, ETC_THRESHOLD_OBJ_R_ENABLED);
		zassert_equal(obj_inst->resources[1].res_id, ETC_THRESHOLD_OBJ_R_VALUE_TYPE);
		zassert_equal(obj_inst->resources[2].res_id, ETC_THRESHOLD_OBJ_R_ALERT_TYPE);
		zassert_equal(obj_inst->resources[3].res_id, ETC_THRESHOLD_OBJ_R_THRESHOLD_VALUE);
		zassert_equal(obj_inst->resources[4].res_id, ETC_THRESHOLD_OBJ_R_ALERT);
		zassert_equal(obj_inst->resources[5].res_id, ETC_THRESHOLD_OBJ_R_LAST_TRIGGERED);
		zassert_equal(obj_inst->resources[6].res_id, ETC_THRESHOLD_OBJ_R_TRIGGER_COUNT);
	}
}

ZTEST(lwm2m_etc_threshold_obj, test_defaults)
{
	for (uint16_t id = 0; id < INSTANCE_COUNT; id++) {
		bool enabled = true, alert = true;
		uint8_t value_type = 0, alert_type = 0xFF;
		double value = 1.0;
		time_t last_triggered = 1;
		uint32_t trigger_count = 1;

		zassert_ok(lwm2m_get_bool(TH_PATH(id, ETC_THRESHOLD_OBJ_R_ENABLED), &enabled));
		zassert_ok(lwm2m_get_u8(TH_PATH(id, ETC_THRESHOLD_OBJ_R_VALUE_TYPE), &value_type));
		zassert_ok(lwm2m_get_u8(TH_PATH(id, ETC_THRESHOLD_OBJ_R_ALERT_TYPE), &alert_type));
		zassert_ok(lwm2m_get_f64(TH_PATH(id, ETC_THRESHOLD_OBJ_R_THRESHOLD_VALUE), &value));
		zassert_ok(lwm2m_get_bool(TH_PATH(id, ETC_THRESHOLD_OBJ_R_ALERT), &alert));
		zassert_ok(lwm2m_get_time(TH_PATH(id, ETC_THRESHOLD_OBJ_R_LAST_TRIGGERED),
					  &last_triggered));
		zassert_ok(lwm2m_get_u32(TH_PATH(id, ETC_THRESHOLD_OBJ_R_TRIGGER_COUNT),
					 &trigger_count));

		zassert_false(enabled, "Instance %u should default to disabled", id);
		zassert_equal(value_type, 1, "Instance %u value type default mismatch", id);
		zassert_equal(alert_type, 0, "Instance %u alert type default mismatch", id);
		zassert_within(value, 0.0, FLOAT_TOLERANCE,
			       "Instance %u threshold value default mismatch", id);
		zassert_false(alert, "Instance %u alert should default to false", id);
		zassert_equal(last_triggered, 0, "Instance %u last triggered default mismatch", id);
		zassert_equal(trigger_count, 0U, "Instance %u trigger count default mismatch", id);
	}
}

ZTEST(lwm2m_etc_threshold_obj, test_instance_limit)
{
	struct lwm2m_engine_obj_inst *obj_inst;
	int ret = lwm2m_create_obj_inst(ETC_THRESHOLD_OBJECT_ID, INSTANCE_COUNT, &obj_inst);

	zassert_true(ret < 0, "Allowed creation beyond instance limit: %d", ret);
}

ZTEST(lwm2m_etc_threshold_obj, test_duplicate_instance_creation)
{
	struct lwm2m_engine_obj_inst *obj_inst;
	int ret;

	/* Free a slot so the duplicate is rejected by the ID check, not the slot count. */
	zassert_ok(lwm2m_delete_obj_inst(ETC_THRESHOLD_OBJECT_ID, INSTANCE_COUNT - 1),
		   "Failed to delete object instance");

	ret = lwm2m_create_obj_inst(ETC_THRESHOLD_OBJECT_ID, 0, &obj_inst);
	zassert_true(ret < 0, "Allowed duplicate object instance creation");

	zassert_ok(lwm2m_create_obj_inst(ETC_THRESHOLD_OBJECT_ID, INSTANCE_COUNT - 1, &obj_inst),
		   "Failed to re-create object instance");
}

/* Each resource type round-trips through the engine, spread over the slots. */
ZTEST(lwm2m_etc_threshold_obj, test_resource_round_trip)
{
	bool enabled = false, alert = false;
	uint8_t value_type = 0, alert_type = 0;
	double value = 0.0;
	time_t last_triggered = 0;
	uint32_t trigger_count = 0;

	zassert_ok(lwm2m_set_bool(TH_PATH(0, ETC_THRESHOLD_OBJ_R_ENABLED), true));
	zassert_ok(lwm2m_set_u8(TH_PATH(1, ETC_THRESHOLD_OBJ_R_VALUE_TYPE),
				ETC_THRESHOLD_OBJ_R_VALUE_TYPE_MAX_VAL));
	zassert_ok(lwm2m_set_u8(TH_PATH(1, ETC_THRESHOLD_OBJ_R_ALERT_TYPE),
				ETC_THRESHOLD_OBJ_R_ALERT_TYPE_MAX_VAL));
	zassert_ok(lwm2m_set_f64(TH_PATH(2, ETC_THRESHOLD_OBJ_R_THRESHOLD_VALUE), -12.5));
	zassert_ok(lwm2m_set_bool(TH_PATH(2, ETC_THRESHOLD_OBJ_R_ALERT), true));
	zassert_ok(
		lwm2m_set_time(TH_PATH(3, ETC_THRESHOLD_OBJ_R_LAST_TRIGGERED), (time_t)1756044301));
	zassert_ok(lwm2m_set_u32(TH_PATH(3, ETC_THRESHOLD_OBJ_R_TRIGGER_COUNT), 42U));

	zassert_ok(lwm2m_get_bool(TH_PATH(0, ETC_THRESHOLD_OBJ_R_ENABLED), &enabled));
	zassert_ok(lwm2m_get_u8(TH_PATH(1, ETC_THRESHOLD_OBJ_R_VALUE_TYPE), &value_type));
	zassert_ok(lwm2m_get_u8(TH_PATH(1, ETC_THRESHOLD_OBJ_R_ALERT_TYPE), &alert_type));
	zassert_ok(lwm2m_get_f64(TH_PATH(2, ETC_THRESHOLD_OBJ_R_THRESHOLD_VALUE), &value));
	zassert_ok(lwm2m_get_bool(TH_PATH(2, ETC_THRESHOLD_OBJ_R_ALERT), &alert));
	zassert_ok(lwm2m_get_time(TH_PATH(3, ETC_THRESHOLD_OBJ_R_LAST_TRIGGERED), &last_triggered));
	zassert_ok(lwm2m_get_u32(TH_PATH(3, ETC_THRESHOLD_OBJ_R_TRIGGER_COUNT), &trigger_count));

	zassert_true(enabled, "Enabled did not round-trip");
	zassert_equal(value_type, ETC_THRESHOLD_OBJ_R_VALUE_TYPE_MAX_VAL,
		      "Value type did not round-trip");
	zassert_equal(alert_type, ETC_THRESHOLD_OBJ_R_ALERT_TYPE_MAX_VAL,
		      "Alert type did not round-trip");
	zassert_within(value, -12.5, FLOAT_TOLERANCE, "Threshold value did not round-trip");
	zassert_true(alert, "Alert did not round-trip");
	zassert_equal(last_triggered, (time_t)1756044301, "Last triggered did not round-trip");
	zassert_equal(trigger_count, 42U, "Trigger count did not round-trip");
}

/* Writing one slot must not disturb the others. */
ZTEST(lwm2m_etc_threshold_obj, test_per_instance_storage_independent)
{
	for (uint16_t id = 0; id < INSTANCE_COUNT; id++) {
		zassert_ok(lwm2m_set_bool(TH_PATH(id, ETC_THRESHOLD_OBJ_R_ENABLED), (id % 2) == 0));
		zassert_ok(lwm2m_set_u8(TH_PATH(id, ETC_THRESHOLD_OBJ_R_VALUE_TYPE),
					(uint8_t)(id + 1)));
		zassert_ok(lwm2m_set_f64(TH_PATH(id, ETC_THRESHOLD_OBJ_R_THRESHOLD_VALUE),
					 (double)id + 0.25));
		zassert_ok(lwm2m_set_u32(TH_PATH(id, ETC_THRESHOLD_OBJ_R_TRIGGER_COUNT),
					 (uint32_t)(id * 10U)));
	}

	for (uint16_t id = 0; id < INSTANCE_COUNT; id++) {
		bool enabled = false;
		uint8_t value_type = 0;
		double value = 0.0;
		uint32_t trigger_count = 0;

		zassert_ok(lwm2m_get_bool(TH_PATH(id, ETC_THRESHOLD_OBJ_R_ENABLED), &enabled));
		zassert_ok(lwm2m_get_u8(TH_PATH(id, ETC_THRESHOLD_OBJ_R_VALUE_TYPE), &value_type));
		zassert_ok(lwm2m_get_f64(TH_PATH(id, ETC_THRESHOLD_OBJ_R_THRESHOLD_VALUE), &value));
		zassert_ok(lwm2m_get_u32(TH_PATH(id, ETC_THRESHOLD_OBJ_R_TRIGGER_COUNT),
					 &trigger_count));

		zassert_equal(enabled, (id % 2) == 0, "Instance %u enabled leaked", id);
		zassert_equal(value_type, (uint8_t)(id + 1), "Instance %u value type leaked", id);
		zassert_within(value, (double)id + 0.25, FLOAT_TOLERANCE,
			       "Instance %u threshold value leaked", id);
		zassert_equal(trigger_count, (uint32_t)(id * 10U),
			      "Instance %u trigger count leaked", id);
	}
}

ZTEST_SUITE(lwm2m_etc_threshold_obj, NULL, NULL, test_before, NULL, NULL);
