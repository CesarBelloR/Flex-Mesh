/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * 
 * Copyright (c) 2023 EXACT Technology
 */

#define LOG_MODULE_NAME net_lwm2m_obj_configuration
#define LOG_LEVEL CONFIG_LWM2M_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <string.h>
#include <zephyr/init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"
#include "etc_cfg_obj_48931.h"

#define OBJECT_ID 48931
#define OBJECT_VERSION_MAJOR 1
#define OBJECT_VERSION_MINOR 4

#define RESOURCES_MAX_ID			16
#define RESOURCE_INSTANCE_COUNT			(RESOURCES_MAX_ID)

/* Storage variables to hold configuration values. */
static uint8_t device_mode;
static uint8_t power_mode;
static uint32_t tx_interval;
static uint32_t tx_interval_alarm;
static uint32_t log_interval;
static uint32_t log_interval_alarm;
static uint16_t tx_delay;
static uint16_t wake_early;
static uint16_t rx_duration;
static uint32_t tx_probe;
static uint16_t lora_probe_offset;
static uint16_t lte_probe_offset;
static uint32_t location_req_interval;
static uint16_t gnss_timeout;
static uint8_t rx_timeout;
static uint32_t threshold_report_interval;

static struct lwm2m_engine_obj object;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_DEVICE_MODE, RW, U8),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_POWER_MODE, RW, U8),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_TX_INTERVAL, RW, U32),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_LOG_INTERVAL, RW, U32),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_LOG_INTERVAL_ALARM, RW, U32),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_TX_DELAY, RW, U16),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_WAKE_EARLY, RW, U16),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_RX_DURATION, RW, U16),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_TX_INTERVAL_ALARM, RW, U32),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_TX_PROBE, RW, U32),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_LORA_PROBE_OFFSET, RW, U16),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_LTE_PROBE_OFFSET, RW, U16),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_LOCATION_REQ_INTERVAL, RW, U32),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_GNSS_TIMEOUT, RW, U16),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_RX_TIMEOUT, RW, U8),
	OBJ_FIELD_DATA(ETC_CFG_OBJ_R_THRESHOLD_REPORT_INTERVAL, RW, U32),
};

static struct lwm2m_engine_obj_inst inst;
static struct lwm2m_engine_res res[RESOURCES_MAX_ID];
static struct lwm2m_engine_res_inst res_inst[RESOURCE_INSTANCE_COUNT];

static struct lwm2m_engine_obj_inst *object_create(uint16_t obj_inst_id)
{
	int i = 0, j = 0;

	init_res_instance(res_inst, ARRAY_SIZE(res_inst));

	/* Initialize object instance resource data */
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_DEVICE_MODE, res, i, res_inst, j,
			  &device_mode, sizeof(device_mode));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_POWER_MODE, res, i, res_inst, j,
			  &power_mode, sizeof(power_mode));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_TX_INTERVAL, res, i, res_inst, j,
			  &tx_interval, sizeof(tx_interval));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_LOG_INTERVAL, res, i, res_inst, j,
			  &log_interval, sizeof(log_interval));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_LOG_INTERVAL_ALARM, res, i, res_inst, j,
			  &log_interval_alarm, sizeof(log_interval_alarm));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_TX_DELAY, res, i, res_inst, j,
			  &tx_delay, sizeof(tx_delay));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_WAKE_EARLY, res, i, res_inst, j,
			  &wake_early, sizeof(wake_early));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_RX_DURATION, res, i, res_inst, j,
			  &rx_duration, sizeof(rx_duration));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_TX_INTERVAL_ALARM, res, i, res_inst, j,
			  &tx_interval_alarm, sizeof(tx_interval_alarm));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_TX_PROBE, res, i, res_inst, j,
			  &tx_probe, sizeof(tx_probe));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_LORA_PROBE_OFFSET, res, i, res_inst, j,
			  &lora_probe_offset, sizeof(lora_probe_offset));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_LTE_PROBE_OFFSET, res, i, res_inst, j,
			  &lte_probe_offset, sizeof(lte_probe_offset));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_LOCATION_REQ_INTERVAL, res, i, res_inst, j,
			  &location_req_interval, sizeof(location_req_interval));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_GNSS_TIMEOUT, res, i, res_inst, j,
			  &gnss_timeout, sizeof(gnss_timeout));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_RX_TIMEOUT, res, i, res_inst, j,
			  &rx_timeout, sizeof(rx_timeout));
	INIT_OBJ_RES_DATA(ETC_CFG_OBJ_R_THRESHOLD_REPORT_INTERVAL, res, i, res_inst, j,
			  &threshold_report_interval, sizeof(threshold_report_interval));

	inst.resources = res;
	inst.resource_count = i;

	LOG_DBG("Created a configuration object: %d", obj_inst_id);
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
		LOG_ERR("Create configuration object error: %d", ret);
		return ret;
	}

	return 0;
}

SYS_INIT(object_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
