/*
 * Copyright (c) 2026 EXACT Technology
 */

#define LOG_MODULE_NAME net_lwm2m_etc_threshold_obj
#define LOG_LEVEL	CONFIG_LWM2M_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <stdint.h>
#include <string.h>
#include <zephyr/init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"
#include "etc_threshold_obj_48944.h"

#define OBJECT_ID	     ETC_THRESHOLD_OBJECT_ID
#define OBJECT_VERSION_MAJOR 1
#define OBJECT_VERSION_MINOR 0

#define MAX_INSTANCE_COUNT	ETC_THRESHOLD_OBJ_MAX_INSTANCE_COUNT
#define RESOURCES_MAX_ID	7
#define RESOURCE_INSTANCE_COUNT (RESOURCES_MAX_ID)

/* Storage variables to hold the threshold values, one entry per slot. */
static bool enabled[MAX_INSTANCE_COUNT];
static uint8_t value_type[MAX_INSTANCE_COUNT];
static uint8_t alert_type[MAX_INSTANCE_COUNT];
static double threshold_value[MAX_INSTANCE_COUNT];
static bool alert[MAX_INSTANCE_COUNT];
static time_t last_triggered[MAX_INSTANCE_COUNT];
static uint32_t trigger_count[MAX_INSTANCE_COUNT];

static struct lwm2m_engine_obj object;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(ETC_THRESHOLD_OBJ_R_ENABLED, RW, BOOL),
	OBJ_FIELD_DATA(ETC_THRESHOLD_OBJ_R_VALUE_TYPE, RW, U8),
	OBJ_FIELD_DATA(ETC_THRESHOLD_OBJ_R_ALERT_TYPE, RW, U8),
	OBJ_FIELD_DATA(ETC_THRESHOLD_OBJ_R_THRESHOLD_VALUE, RW, FLOAT),
	OBJ_FIELD_DATA(ETC_THRESHOLD_OBJ_R_ALERT, R, BOOL),
	OBJ_FIELD_DATA(ETC_THRESHOLD_OBJ_R_LAST_TRIGGERED, R, TIME),
	OBJ_FIELD_DATA(ETC_THRESHOLD_OBJ_R_TRIGGER_COUNT, R, U32),
};

static struct lwm2m_engine_obj_inst inst[MAX_INSTANCE_COUNT];
static struct lwm2m_engine_res res[MAX_INSTANCE_COUNT][RESOURCES_MAX_ID];
static struct lwm2m_engine_res_inst res_inst[MAX_INSTANCE_COUNT][RESOURCE_INSTANCE_COUNT];

/**
 * @brief Create one threshold slot instance and bind its resources to the slot storage.
 *
 * @param obj_inst_id Object instance ID of the slot.
 * @return Pointer to the created instance, or NULL on error.
 */
static struct lwm2m_engine_obj_inst *object_create(uint16_t obj_inst_id)
{
	int index, i = 0, j = 0;

	/* Check that there is no other instance with this ID */
	for (index = 0; index < MAX_INSTANCE_COUNT; index++) {
		if (inst[index].obj && inst[index].obj_inst_id == obj_inst_id) {
			LOG_ERR("Can not create instance - already existing: %u", obj_inst_id);
			return NULL;
		}
	}

	/* Find free instance slot */
	for (index = 0; index < MAX_INSTANCE_COUNT; index++) {
		if (!inst[index].obj) {
			break;
		}
	}

	if (index >= MAX_INSTANCE_COUNT) {
		LOG_ERR("Can not create instance - no more room: %u", obj_inst_id);
		return NULL;
	}

	/* Set default values */
	enabled[index] = false;
	value_type[index] = ETC_THRESHOLD_OBJ_R_VALUE_TYPE_MIN_VAL;
	alert_type[index] = ETC_THRESHOLD_OBJ_R_ALERT_TYPE_MIN_VAL;
	threshold_value[index] = 0.0;
	alert[index] = false;
	last_triggered[index] = 0;
	trigger_count[index] = 0;

	(void)memset(res[index], 0, sizeof(res[index][0]) * ARRAY_SIZE(res[index]));
	init_res_instance(res_inst[index], ARRAY_SIZE(res_inst[index]));

	/* Initialize instance resource data */
	INIT_OBJ_RES_DATA(ETC_THRESHOLD_OBJ_R_ENABLED, res[index], i, res_inst[index], j,
			  &enabled[index], sizeof(enabled[index]));
	INIT_OBJ_RES_DATA(ETC_THRESHOLD_OBJ_R_VALUE_TYPE, res[index], i, res_inst[index], j,
			  &value_type[index], sizeof(value_type[index]));
	INIT_OBJ_RES_DATA(ETC_THRESHOLD_OBJ_R_ALERT_TYPE, res[index], i, res_inst[index], j,
			  &alert_type[index], sizeof(alert_type[index]));
	INIT_OBJ_RES_DATA(ETC_THRESHOLD_OBJ_R_THRESHOLD_VALUE, res[index], i, res_inst[index], j,
			  &threshold_value[index], sizeof(threshold_value[index]));
	INIT_OBJ_RES_DATA(ETC_THRESHOLD_OBJ_R_ALERT, res[index], i, res_inst[index], j,
			  &alert[index], sizeof(alert[index]));
	INIT_OBJ_RES_DATA(ETC_THRESHOLD_OBJ_R_LAST_TRIGGERED, res[index], i, res_inst[index], j,
			  &last_triggered[index], sizeof(last_triggered[index]));
	INIT_OBJ_RES_DATA(ETC_THRESHOLD_OBJ_R_TRIGGER_COUNT, res[index], i, res_inst[index], j,
			  &trigger_count[index], sizeof(trigger_count[index]));

	inst[index].resources = res[index];
	inst[index].resource_count = i;
	inst[index].obj = &object;
	inst[index].obj_inst_id = obj_inst_id;

	LOG_DBG("Created an EXACT Threshold object instance: %d", obj_inst_id);
	return &inst[index];
}

/**
 * @brief Register the EXACT Threshold object and create one instance per slot.
 */
static int object_init(void)
{
	struct lwm2m_engine_obj_inst *obj_inst = NULL;
	int ret;

	object.obj_id = OBJECT_ID;
	object.version_major = OBJECT_VERSION_MAJOR;
	object.version_minor = OBJECT_VERSION_MINOR;
	object.is_core = false;
	object.fields = fields;
	object.field_count = ARRAY_SIZE(fields);
	object.max_instance_count = MAX_INSTANCE_COUNT;
	object.create_cb = object_create;
	lwm2m_register_obj(&object);

	for (int idx = 0; idx < MAX_INSTANCE_COUNT; idx++) {
		ret = lwm2m_create_obj_inst(OBJECT_ID, idx, &obj_inst);
		if (ret < 0) {
			LOG_ERR("Create EXACT Threshold object instance %d error: %d", idx, ret);
			return ret;
		}
	}

	return 0;
}

SYS_INIT(object_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
