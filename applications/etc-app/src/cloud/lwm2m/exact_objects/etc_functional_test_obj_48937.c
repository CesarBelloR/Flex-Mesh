/*
 * Copyright (c) 2023 EXACT Technology
 */

#define LOG_MODULE_NAME net_lwm2m_etc_functional_test_obj
#define LOG_LEVEL CONFIG_LWM2M_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <stdint.h>
#include <zephyr/init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"
#include "lwm2m_resource_ids.h"
#include "etc_functional_test_obj_48937.h"

#define OBJECT_ID ETC_FUNCTIONAL_TEST_OBJECT_ID
#define TEMP_VERSION_MAJOR 1
#define TEMP_VERSION_MINOR 0

#define RESOURCE_INSTANCE_COUNT	1

#define R_STATUS_DEFAULT 99

/* resource state variables */
static uint8_t status;

static struct lwm2m_engine_obj etc_temp_sensor;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(ETC_FUNCTIONAL_TEST_OBJ_R_STATUS, R, U8),
};

static struct lwm2m_engine_obj_inst inst;
static struct lwm2m_engine_res res[RESOURCE_INSTANCE_COUNT];
static struct lwm2m_engine_res_inst res_inst[RESOURCE_INSTANCE_COUNT];

static struct lwm2m_engine_obj_inst *object_create(uint16_t obj_inst_id)
{
	int i = 0, j = 0;

	init_res_instance(res_inst, ARRAY_SIZE(res_inst));

	/* Initialize object instance resource data */
	INIT_OBJ_RES_DATA(ETC_FUNCTIONAL_TEST_OBJ_R_STATUS, res, i, res_inst, j,
			  &status, sizeof(status));
	status = R_STATUS_DEFAULT;

	inst.resources = res;
	inst.resource_count = i;

	LOG_DBG("Created a EXACT Functional Test object: %d", obj_inst_id);
	return &inst;
}

static int etc_functional_test_obj_init(void)
{
	int ret;
	struct lwm2m_engine_obj_inst *obj_inst = NULL;

	etc_temp_sensor.obj_id = OBJECT_ID;
	etc_temp_sensor.version_major = TEMP_VERSION_MAJOR;
	etc_temp_sensor.version_minor = TEMP_VERSION_MINOR;
	etc_temp_sensor.is_core = false;
	etc_temp_sensor.fields = fields;
	etc_temp_sensor.field_count = ARRAY_SIZE(fields);
	etc_temp_sensor.max_instance_count = 1U;
	etc_temp_sensor.create_cb = object_create;
	lwm2m_register_obj(&etc_temp_sensor);

	ret = lwm2m_create_obj_inst(OBJECT_ID, 0, &obj_inst);
	if (ret < 0) {
		LOG_ERR("Create EXACT Functional Test object error: %d", ret);
		return ret;
	}

	return 0;
}

SYS_INIT(etc_functional_test_obj_init, APPLICATION,
	 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
