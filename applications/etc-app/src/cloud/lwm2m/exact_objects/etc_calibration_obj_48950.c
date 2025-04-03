/*
 * Copyright (c) 2025 EXACT Technology
 */

#define LOG_MODULE_NAME net_lwm2m_etc_calibration_obj
#define LOG_LEVEL	CONFIG_LWM2M_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <stdint.h>
#include <math.h>
#include <zephyr/init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"
#include "lwm2m_resource_ids.h"
#include "etc_calibration_obj_48950.h"

#define OBJECT_ID     ETC_CALIBRATION_OBJ_ID
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define MAX_ID	      6

#define MAX_INSTANCE_COUNT	2
#define RESOURCE_INSTANCE_COUNT (MAX_ID)

/* resource state variables */
static uint8_t calibration_type[MAX_INSTANCE_COUNT];
static time_t calibration_time[MAX_INSTANCE_COUNT];
static uint8_t calibrator_id[MAX_INSTANCE_COUNT][ETC_CALIBRATOR_ID_NUMBER_SIZE];
static double calibration_offset[MAX_INSTANCE_COUNT];
static double calibration_high[MAX_INSTANCE_COUNT];
static double calibration_reference[MAX_INSTANCE_COUNT];

static struct lwm2m_engine_obj etc_calibration;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(ETC_CALIBRATION_R_TYPE, R, U8),
	OBJ_FIELD_DATA(ETC_CALIBRATION_R_TIME, R, TIME),
	OBJ_FIELD_DATA(ETC_CALIBRATION_R_ID, R, STRING),
	OBJ_FIELD_DATA(ETC_CALIBRATION_R_OFFSET, R, FLOAT),
	OBJ_FIELD_DATA(ETC_CALIBRATION_R_HIGH, R, FLOAT),
	OBJ_FIELD_DATA(ETC_CALIBRATION_R_REFERENCE, R, FLOAT),
};

static struct lwm2m_engine_obj_inst inst[MAX_INSTANCE_COUNT];
static struct lwm2m_engine_res res[MAX_INSTANCE_COUNT][RESOURCE_INSTANCE_COUNT];
static struct lwm2m_engine_res_inst res_inst[MAX_INSTANCE_COUNT][RESOURCE_INSTANCE_COUNT];

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
	calibration_type[index] = 0;
	calibration_time[index] = 0;
	calibration_offset[index] = NAN;
	calibration_high[index] = NAN;
	calibration_reference[index] = NAN;
	memset(calibrator_id[index], 0, ETC_CALIBRATOR_ID_NUMBER_SIZE);

	(void)memset(res[index], 0, sizeof(res[index][0]) * ARRAY_SIZE(res[index]));
	init_res_instance(res_inst[index], ARRAY_SIZE(res_inst[index]));

	/* Initialize instance resource data */
	INIT_OBJ_RES_DATA(ETC_CALIBRATION_R_TYPE, res[index], i, res_inst[index], j,
			  &calibration_type[index], sizeof(calibration_type[index]));
	INIT_OBJ_RES_DATA(ETC_CALIBRATION_R_TIME, res[index], i, res_inst[index], j,
			  &calibration_time[index], sizeof(calibration_time[index]));
	INIT_OBJ_RES_DATA_LEN(ETC_CALIBRATION_R_ID, res[index], i, res_inst[index], j,
			      calibrator_id[index], ETC_CALIBRATOR_ID_NUMBER_SIZE, 0);
	INIT_OBJ_RES_DATA(ETC_CALIBRATION_R_OFFSET, res[index], i, res_inst[index], j,
			  &calibration_offset[index], sizeof(calibration_offset[index]));
	INIT_OBJ_RES_DATA(ETC_CALIBRATION_R_HIGH, res[index], i, res_inst[index], j,
			  &calibration_high[index], sizeof(calibration_high[index]));
	INIT_OBJ_RES_DATA(ETC_CALIBRATION_R_REFERENCE, res[index], i, res_inst[index], j,
			  &calibration_reference[index], sizeof(calibration_reference[index]));

	inst[index].resources = res[index];

	inst[index].resource_count = i;
	inst[index].obj = &etc_calibration;
	inst[index].obj_inst_id = obj_inst_id;

	LOG_DBG("Created an EXACT Calibration object instance: %d", obj_inst_id);
	return &inst[index];
}

static int etc_calibration_init(void)
{
	struct lwm2m_engine_obj_inst *obj_inst = NULL;
	int ret = 0;

	etc_calibration.obj_id = OBJECT_ID;
	etc_calibration.version_major = VERSION_MAJOR;
	etc_calibration.version_minor = VERSION_MINOR;
	etc_calibration.is_core = false;
	etc_calibration.fields = fields;
	etc_calibration.field_count = ARRAY_SIZE(fields);
	etc_calibration.max_instance_count = MAX_INSTANCE_COUNT;
	etc_calibration.create_cb = object_create;
	lwm2m_register_obj(&etc_calibration);

	for (int idx = 0; idx < MAX_INSTANCE_COUNT; idx++) {
		ret = lwm2m_create_obj_inst(OBJECT_ID, idx, &obj_inst);
		if (ret < 0) {
			LOG_DBG("Creating EXACT Calibration object instance %d recv error: %d", idx,
				ret);
			break;
		}
	}

	return 0;
}

SYS_INIT(etc_calibration_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);