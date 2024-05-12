/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * 
 * Copyright (c) 2023 EXACT Technology
 */

#define LOG_MODULE_NAME net_lwm2m_obj_reclaim
#define LOG_LEVEL CONFIG_LWM2M_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"
#include "etc_reclaim_obj_48934.h"

#define OBJECT_ID ETC_RECLAIM_OBJECT_ID
#define OBJECT_VERSION_MAJOR 1
#define OBJECT_VERSION_MINOR 0

#define RESOURCES_MAX_ID			4
#define RESOURCE_INSTANCE_COUNT			(RESOURCES_MAX_ID)

/* Storage variables to hold reclaim values. */
int32_t start_time_s;
int32_t end_time_s;
uint8_t status;

static struct lwm2m_engine_obj object;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(ETC_RECLAIM_OBJ_R_START_TIME, RW, S32),
	OBJ_FIELD_DATA(ETC_RECLAIM_OBJ_R_END_TIME, RW, S32),
	OBJ_FIELD_EXECUTE(ETC_RECLAIM_OBJ_R_RECLAIM),
	OBJ_FIELD_DATA(ETC_RECLAIM_OBJ_R_STATUS, R, U8),
};

static struct lwm2m_engine_obj_inst inst;
static struct lwm2m_engine_res res[RESOURCES_MAX_ID];
static struct lwm2m_engine_res_inst res_inst[RESOURCE_INSTANCE_COUNT];

static struct lwm2m_engine_obj_inst *object_create(uint16_t obj_inst_id)
{
	int i = 0, j = 0;

	init_res_instance(res_inst, ARRAY_SIZE(res_inst));

	/* Initialize object instance resource data */
	INIT_OBJ_RES_DATA(ETC_RECLAIM_OBJ_R_START_TIME, res, i, res_inst, j,
			  &start_time_s, sizeof(start_time_s));
	INIT_OBJ_RES_DATA(ETC_RECLAIM_OBJ_R_END_TIME, res, i, res_inst, j,
			  &end_time_s, sizeof(end_time_s));
	INIT_OBJ_RES_EXECUTE(ETC_RECLAIM_OBJ_R_RECLAIM, res, i, NULL);
	INIT_OBJ_RES_DATA(ETC_RECLAIM_OBJ_R_STATUS, res, i, res_inst, j,
			  &status, sizeof(status));

	inst.resources = res;
	inst.resource_count = i;

	LOG_DBG("Created an EXACT Reclaim object: %d", obj_inst_id);
	return &inst;
}

static int object_init(void)
{
	struct lwm2m_engine_obj_inst *obj_inst = NULL;
	int ret = 0;

	object.obj_id = OBJECT_ID;
	object.version_major = OBJECT_VERSION_MAJOR;
	object.version_minor = OBJECT_VERSION_MINOR;
	object.is_core = false;
	object.fields = fields;
	object.field_count = ARRAY_SIZE(fields);
	object.max_instance_count = 1U;
	object.create_cb = object_create;
	lwm2m_register_obj(&object);

	ret = lwm2m_create_obj_inst(OBJECT_ID, 0, &obj_inst);
	if (ret < 0) {
		LOG_ERR("Create EXACT Reclaim object error: %d", ret);
		return ret;
	}

	return 0;
}

SYS_INIT(object_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
