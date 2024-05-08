/*
 * Copyright (c) 2023 EXACT Technology
 */

#define LOG_MODULE_NAME net_lwm2m_etc_location_obj
#define LOG_LEVEL CONFIG_LWM2M_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <stdint.h>
#include <math.h>
#include <zephyr/init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"
#include "lwm2m_resource_ids.h"
#include "etc_location_obj_48938.h"

#define OBJECT_ID ETC_LOCATION_OBJ_ID
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define MAX_ID 4

/*
 * Calculate resource instances as follows:
 * start with MAX_ID
 * subtract EXEC resources (1)
 */
#define RESOURCE_INSTANCE_COUNT	(MAX_ID  - 1)

/* resource state variables */
static double latitude;
static double longitude;
static time_t timestamp;

static struct lwm2m_engine_obj etc_location;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(ETC_LOCATION_OBJ_R_LATITUDE, R, FLOAT),
	OBJ_FIELD_DATA(ETC_LOCATION_OBJ_R_LONGITUDE, R, FLOAT),
	OBJ_FIELD_DATA(TIMESTAMP_RID, R, TIME),
	OBJ_FIELD_EXECUTE(ETC_LOCATION_OBJ_R_REQUEST),
};

static struct lwm2m_engine_obj_inst inst;
static struct lwm2m_engine_res res[MAX_ID];
static struct lwm2m_engine_res_inst res_inst[RESOURCE_INSTANCE_COUNT];

static struct lwm2m_engine_obj_inst *object_create(uint16_t obj_inst_id)
{
	int index, i = 0, j = 0;

	if (inst.resource_count) {
		LOG_ERR("Only 1 instance of Location object can exist.");
		return NULL;
	}

	latitude = NAN;
	longitude = NAN;
	timestamp = 0;

	init_res_instance(res_inst, ARRAY_SIZE(res_inst));

	/* Initialize object instance resource data */
	INIT_OBJ_RES_DATA(ETC_LOCATION_OBJ_R_LATITUDE, res, i, res_inst, j,
			  &latitude, sizeof(latitude));
	INIT_OBJ_RES_DATA(ETC_LOCATION_OBJ_R_LONGITUDE, res, i, res_inst, j,
			  &longitude, sizeof(longitude));
	INIT_OBJ_RES_DATA(TIMESTAMP_RID, res, i, res_inst, j,
			  &timestamp, sizeof(timestamp));
	INIT_OBJ_RES_EXECUTE(ETC_LOCATION_OBJ_R_REQUEST, res, i, NULL);

	inst.resources = res;
	inst.resource_count = i;

	LOG_DBG("Created an EXACT Location object: %d", obj_inst_id);
	return &inst;
}

static int etc_location_init(void)
{
	struct lwm2m_engine_obj_inst *obj_inst = NULL;
	int ret = 0;

	etc_location.obj_id = OBJECT_ID;
	etc_location.version_major = VERSION_MAJOR;
	etc_location.version_minor = VERSION_MINOR;
	etc_location.is_core = false;
	etc_location.fields = fields;
	etc_location.field_count = ARRAY_SIZE(fields);
	etc_location.max_instance_count = 1U;
	etc_location.create_cb = object_create;
	lwm2m_register_obj(&etc_location);
	
	ret = lwm2m_create_obj_inst(OBJECT_ID, 0, &obj_inst);
	if (ret < 0) {
		LOG_ERR("Create EXACT Location object error: %d", ret);
		return ret;
	}

	return 0;
}

SYS_INIT(etc_location_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
