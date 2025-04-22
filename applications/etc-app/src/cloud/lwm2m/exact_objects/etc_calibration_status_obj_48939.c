/*
 * Copyright (c) 2025 EXACT Technology
 */

#define LOG_MODULE_NAME net_lwm2m_etc_calibration_status_obj
#define LOG_LEVEL	CONFIG_LWM2M_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <stdint.h>
#include <string.h>
#include <zephyr/init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"
#include "lwm2m_resource_ids.h"
#include "etc_calibration_status_obj_48939.h"

#define OBJECT_ID     ETC_CALIBRATION_STATUS_OBJ_ID
#define VERSION_MAJOR 1
#define VERSION_MINOR 1
#define MAX_ID	      6

#define RESOURCE_INSTANCE_COUNT (MAX_ID)

/* Resource state variables */
static uint8_t status;
static uint8_t result;
static char pre_adjustment_values[ETC_ADJUSTMENT_MAX_STR_SIZE];
static char post_adjustment_values[ETC_ADJUSTMENT_MAX_STR_SIZE];
static char reference_values[ETC_REFERENCE_MAX_STR_SIZE];
static uint32_t active_calibration;

static struct lwm2m_engine_obj etc_calibration_status;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(ETC_CALIBRATION_STATUS_R_STATUS, R, U8),
	OBJ_FIELD_DATA(ETC_CALIBRATION_STATUS_R_RESULT, R, U8),
	OBJ_FIELD_DATA(ETC_CALIBRATION_STATUS_R_PRE_VALUES, R, STRING),
	OBJ_FIELD_DATA(ETC_CALIBRATION_STATUS_R_POST_VALUES, R, STRING),
	OBJ_FIELD_DATA(ETC_CALIBRATION_STATUS_R_REF_VALUES, R, STRING),
	OBJ_FIELD_DATA(ETC_CALIBRATION_STATUS_R_ACTIVE_CALIB, R, U32),
};

static struct lwm2m_engine_obj_inst inst;
static struct lwm2m_engine_res res[MAX_ID];
static struct lwm2m_engine_res_inst res_inst[RESOURCE_INSTANCE_COUNT];

static struct lwm2m_engine_obj_inst *object_create(uint16_t obj_inst_id)
{
	int i = 0, j = 0;

	if (inst.resource_count) {
		LOG_ERR("Only 1 instance of Calibration Status object can exist.");
		return NULL;
	}

	/* Initialize with default values */
	status = 0;
	result = 0;
	memset(pre_adjustment_values, 0, ETC_ADJUSTMENT_MAX_STR_SIZE);
	memset(post_adjustment_values, 0, ETC_ADJUSTMENT_MAX_STR_SIZE);
	memset(reference_values, 0, ETC_REFERENCE_MAX_STR_SIZE);
	active_calibration = 0;

	init_res_instance(res_inst, ARRAY_SIZE(res_inst));

	/* Initialize object instance resource data */
	INIT_OBJ_RES_DATA(ETC_CALIBRATION_STATUS_R_STATUS, res, i, res_inst, j, &status,
			  		  sizeof(status));
	INIT_OBJ_RES_DATA(ETC_CALIBRATION_STATUS_R_RESULT, res, i, res_inst, j, &result,
			  		  sizeof(result));
	INIT_OBJ_RES_DATA_LEN(ETC_CALIBRATION_STATUS_R_PRE_VALUES, res, i, res_inst, j,
			      pre_adjustment_values, ETC_ADJUSTMENT_MAX_STR_SIZE,
			      strlen(pre_adjustment_values));
	INIT_OBJ_RES_DATA_LEN(ETC_CALIBRATION_STATUS_R_POST_VALUES, res, i, res_inst, j,
			      post_adjustment_values, ETC_ADJUSTMENT_MAX_STR_SIZE,
			      strlen(post_adjustment_values));
	INIT_OBJ_RES_DATA_LEN(ETC_CALIBRATION_STATUS_R_REF_VALUES, res, i, res_inst, j,
			      reference_values, ETC_REFERENCE_MAX_STR_SIZE,
			      strlen(reference_values));
	INIT_OBJ_RES_DATA(ETC_CALIBRATION_STATUS_R_ACTIVE_CALIB, res, i, res_inst, j,
			  		  &active_calibration, sizeof(active_calibration));

	inst.resources = res;
	inst.resource_count = i;

	LOG_DBG("Created an EXACT Calibration Status object: %d", obj_inst_id);
	return &inst;
}

static int etc_calibration_status_init(void)
{
	struct lwm2m_engine_obj_inst *obj_inst = NULL;
	int ret = 0;

	etc_calibration_status.obj_id = OBJECT_ID;
	etc_calibration_status.version_major = VERSION_MAJOR;
	etc_calibration_status.version_minor = VERSION_MINOR;
	etc_calibration_status.is_core = false;
	etc_calibration_status.fields = fields;
	etc_calibration_status.field_count = ARRAY_SIZE(fields);
	etc_calibration_status.max_instance_count = 1U;
	etc_calibration_status.create_cb = object_create;
	lwm2m_register_obj(&etc_calibration_status);

	ret = lwm2m_create_obj_inst(OBJECT_ID, 0, &obj_inst);
	if (ret < 0) {
		LOG_ERR("Create EXACT Calibration Status object error: %d", ret);
		return ret;
	}

	return 0;
}

SYS_INIT(etc_calibration_status_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);