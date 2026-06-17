/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * 
 * Copyright (c) 2023 EXACT Technology
 */

#define LOG_MODULE_NAME net_lwm2m_obj_relay
#define LOG_LEVEL CONFIG_LWM2M_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"
#include "etc_relay_obj_48935.h"

#define OBJECT_ID ETC_RELAY_OBJECT_ID
#define OBJECT_VERSION_MAJOR 1
#define OBJECT_VERSION_MINOR 2

#define RESOURCES_MAX_ID			4
#define RESOURCE_INSTANCE_COUNT			(RESOURCES_MAX_ID)

/* Storage variables to hold relay values. */
static uint8_t data[ETC_RELAY_DATA_SIZE];
static uint8_t legacy_data[ETC_RELAY_DATA_SIZE];
static char response_buf[ETC_RELAY_OBJ_RESPONSE_MAX_LEN];

static struct lwm2m_engine_obj object;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(ETC_RELAY_OBJ_R_DATA, R, OPAQUE),
	OBJ_FIELD_DATA(ETC_RELAY_OBJ_R_LEGACY_DATA, R, STRING),
	OBJ_FIELD_EXECUTE(ETC_RELAY_OBJ_R_COMMAND),
	OBJ_FIELD_DATA(ETC_RELAY_OBJ_R_RESPONSE, R, STRING),
};

static struct lwm2m_engine_obj_inst inst;
static struct lwm2m_engine_res res[RESOURCES_MAX_ID];
static struct lwm2m_engine_res_inst res_inst[RESOURCE_INSTANCE_COUNT];

static struct lwm2m_engine_obj_inst *object_create(uint16_t obj_inst_id)
{
	int i = 0, j = 0;

	init_res_instance(res_inst, ARRAY_SIZE(res_inst));

	/* Initialize object instance resource data */
	INIT_OBJ_RES_DATA_LEN(ETC_RELAY_OBJ_R_DATA, res, i, res_inst, j, data, sizeof(data), 0);
	INIT_OBJ_RES_DATA_LEN(ETC_RELAY_OBJ_R_LEGACY_DATA, res, i, res_inst, j, legacy_data,
			      sizeof(legacy_data), 0);
	INIT_OBJ_RES_EXECUTE(ETC_RELAY_OBJ_R_COMMAND, res, i, NULL);
	response_buf[0] = '\0';
	INIT_OBJ_RES_DATA(ETC_RELAY_OBJ_R_RESPONSE, res, i, res_inst, j, response_buf,
			  sizeof(response_buf));

	inst.resources = res;
	inst.resource_count = i;

	LOG_DBG("Created an EXACT Relay object: %d", obj_inst_id);
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
		LOG_ERR("Create EXACT Relay object error: %d", ret);
		return ret;
	}

	return 0;
}

SYS_INIT(object_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
