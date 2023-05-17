/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * 
 * Copyright (c) 2023 EXACT Technology
 */

#define LOG_MODULE_NAME net_lwm2m_obj_info
#define LOG_LEVEL CONFIG_LWM2M_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"
#include "etc_info_obj_48933.h"

#define OBJECT_ID ETC_INFO_OBJECT_ID
#define OBJECT_VERSION_MAJOR 1
#define OBJECT_VERSION_MINOR 0

#define RESOURCES_MAX_ID			4
#define RESOURCE_INSTANCE_COUNT			(RESOURCES_MAX_ID)

/* Storage variables to hold info values. */
static char imei[ETC_INFO_IMEI_SIZE];
static char modem_rev[ETC_INFO_MODEM_REV_SIZE];
static char imsi[ETC_INFO_IMSI_SIZE];
static char iccid[ETC_INFO_ICCID_SIZE];

static struct lwm2m_engine_obj object;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(ETC_INFO_OBJ_R_IMEI, R, STRING),
	OBJ_FIELD_DATA(ETC_INFO_OBJ_R_MODEM_REV, R, STRING),
	OBJ_FIELD_DATA(ETC_INFO_OBJ_R_IMSI, R, STRING),
	OBJ_FIELD_DATA(ETC_INFO_OBJ_R_ICCID, R, STRING),
};

static struct lwm2m_engine_obj_inst inst;
static struct lwm2m_engine_res res[RESOURCES_MAX_ID];
static struct lwm2m_engine_res_inst res_inst[RESOURCE_INSTANCE_COUNT];

static struct lwm2m_engine_obj_inst *object_create(uint16_t obj_inst_id)
{
	int i = 0, j = 0;

	init_res_instance(res_inst, ARRAY_SIZE(res_inst));

	/* Initialize object instance resource data */
	INIT_OBJ_RES_DATA(ETC_INFO_OBJ_R_IMEI, res, i, res_inst, j,
			  imei, sizeof(imei));
	INIT_OBJ_RES_DATA(ETC_INFO_OBJ_R_MODEM_REV, res, i, res_inst, j,
			  modem_rev, sizeof(modem_rev));
	INIT_OBJ_RES_DATA(ETC_INFO_OBJ_R_IMSI, res, i, res_inst, j,
			  imsi, sizeof(imsi));
	INIT_OBJ_RES_DATA(ETC_INFO_OBJ_R_ICCID, res, i, res_inst, j,
			  iccid, sizeof(iccid));

	inst.resources = res;
	inst.resource_count = i;

	LOG_DBG("Created a EXACT Info object: %d", obj_inst_id);
	return &inst;
}

static int object_init(const struct device *dev)
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
		LOG_ERR("Create configuration object error: %d", ret);
		return ret;
	}

	return 0;
}

SYS_INIT(object_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
