/*
 * Copyright (c) 2023 EXACT Technology
 */

#define LOG_MODULE_NAME net_lwm2m_etc_humidity_obj
#define LOG_LEVEL CONFIG_LWM2M_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <stdint.h>
#include <zephyr/init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"
#include "lwm2m_resource_ids.h"
#include "etc_humid_obj_48936.h"

#define OBJECT_ID 48936
#define HUMID_VERSION_MAJOR 1
#define HUMID_VERSION_MINOR 0
#define HUMID_MAX_ID 9

#define MAX_INSTANCE_COUNT	CONFIG_LWM2M_INTEGRATION_MAX_HUMID_SENSOR_INSTANCE_COUNT

#define UNIT_STR_MAX_SIZE	8
#define UID_STR_MAX_SIZE	16

/*
 * Calculate resource instances as follows:
 * start with HUMID_MAX_ID
 * subtract EXEC resources (1)
 */
#define RESOURCE_INSTANCE_COUNT	HUMID_MAX_ID

/* resource state variables */
static double sensor_value[MAX_INSTANCE_COUNT];
static double min_range_value[MAX_INSTANCE_COUNT];
static double max_range_value[MAX_INSTANCE_COUNT];
static uint8_t type[MAX_INSTANCE_COUNT];
static int8_t port[MAX_INSTANCE_COUNT];
static char uid[MAX_INSTANCE_COUNT][UID_STR_MAX_SIZE];

static struct lwm2m_engine_obj etc_humid_sensor;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(SENSOR_VALUE_RID, R, FLOAT),
	OBJ_FIELD_DATA(SENSOR_UNITS_RID, R_OPT, STRING),
	OBJ_FIELD_DATA(MIN_RANGE_VALUE_RID, R_OPT, FLOAT),
	OBJ_FIELD_DATA(MAX_RANGE_VALUE_RID, R_OPT, FLOAT),
	OBJ_FIELD_DATA(TIMESTAMP_RID, R_OPT, TIME),
	OBJ_FIELD_DATA(FRACTIONAL_TIMESTAMP_RID, R_OPT, FLOAT),
	OBJ_FIELD_DATA(ETC_HUMID_OBJ_R_TYPE, R, U8),
	OBJ_FIELD_DATA(ETC_HUMID_OBJ_R_PORT, R, S8),
	OBJ_FIELD_DATA(ETC_HUMID_OBJ_R_UID, R_OPT, STRING),
};

static struct lwm2m_engine_obj_inst inst[MAX_INSTANCE_COUNT];
static struct lwm2m_engine_res res[MAX_INSTANCE_COUNT][HUMID_MAX_ID];
static struct lwm2m_engine_res_inst
		res_inst[MAX_INSTANCE_COUNT][RESOURCE_INSTANCE_COUNT];

static struct lwm2m_engine_obj_inst *humid_sensor_create(uint16_t obj_inst_id)
{
	int index, i = 0, j = 0;

	/* Check that there is no other instance with this ID */
	for (index = 0; index < MAX_INSTANCE_COUNT; index++) {
		if (inst[index].obj && inst[index].obj_inst_id == obj_inst_id) {
			LOG_ERR("Can not create instance - "
				"already existing: %u", obj_inst_id);
			return NULL;
		}
	}

	for (index = 0; index < MAX_INSTANCE_COUNT; index++) {
		if (!inst[index].obj) {
			break;
		}
	}

	if (index >= MAX_INSTANCE_COUNT) {
		LOG_ERR("Can not create instance - no more room: %u",
			obj_inst_id);
		return NULL;
	}

	/* Set default values */
	sensor_value[index] = 0;
	min_range_value[index] = 0;
	max_range_value[index] = 0;
	type[index] = 0;
	port[index] = -1;
	uid[index][0] = '\0';

	(void)memset(res[index], 0,
		     sizeof(res[index][0]) * ARRAY_SIZE(res[index]));
	init_res_instance(res_inst[index], ARRAY_SIZE(res_inst[index]));

	/* initialize instance resource data */
	INIT_OBJ_RES(SENSOR_VALUE_RID, res[index], i,
		     res_inst[index], j, 1, false, true,
		     &sensor_value[index], sizeof(*sensor_value),
		     NULL, NULL, NULL, NULL, NULL);
	INIT_OBJ_RES_DATA(MIN_RANGE_VALUE_RID, res[index], i,
			  res_inst[index], j, &min_range_value[index],
			  sizeof(*min_range_value));
	INIT_OBJ_RES_DATA(MAX_RANGE_VALUE_RID, res[index], i,
			  res_inst[index], j, &max_range_value[index],
			  sizeof(*max_range_value));
	INIT_OBJ_RES_DATA(ETC_HUMID_OBJ_R_TYPE, res[index], i, res_inst[index], j, 
			  &type[index], sizeof(*type));
	INIT_OBJ_RES_DATA(ETC_HUMID_OBJ_R_PORT, res[index], i, res_inst[index], j,
			  &port[index], sizeof(*port));
	INIT_OBJ_RES_DATA(ETC_HUMID_OBJ_R_UID, res[index], i, res_inst[index], j, 
			  uid[index], UID_STR_MAX_SIZE);
	INIT_OBJ_RES_OPTDATA(TIMESTAMP_RID, res[index], i, res_inst[index], j);
	INIT_OBJ_RES_OPTDATA(FRACTIONAL_TIMESTAMP_RID, res[index], i,
			     res_inst[index], j);
	INIT_OBJ_RES_OPTDATA(SENSOR_UNITS_RID, res[index], i,
			     res_inst[index], j);

	inst[index].resources = res[index];
	inst[index].resource_count = i;
	LOG_DBG("Create EXACT Humidity Sensor instance: %d", obj_inst_id);
	return &inst[index];
}

static int etc_humid_sensor_init(const struct device *dev)
{
	etc_humid_sensor.obj_id = OBJECT_ID;
	etc_humid_sensor.version_major = HUMID_VERSION_MAJOR;
	etc_humid_sensor.version_minor = HUMID_VERSION_MINOR;
	etc_humid_sensor.is_core = false;
	etc_humid_sensor.fields = fields;
	etc_humid_sensor.field_count = ARRAY_SIZE(fields);
	etc_humid_sensor.max_instance_count = MAX_INSTANCE_COUNT;
	etc_humid_sensor.create_cb = humid_sensor_create;
	lwm2m_register_obj(&etc_humid_sensor);

	return 0;
}

SYS_INIT(etc_humid_sensor_init, APPLICATION,
	 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
