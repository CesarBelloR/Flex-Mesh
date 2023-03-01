/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * 
 * Copyright (c) 2023 EXACT Technology Corporation
 */

#include <zephyr/types.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <lwm2m_resource_ids.h>
#include <zephyr/net/lwm2m.h>
#include <date_time.h>

#include "data_codec.h"
#include "lwm2m_codec_defines.h"
#include "lwm2m_codec_helpers.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cloud_codec, CONFIG_CLOUD_CODEC_LOG_LEVEL);

/* Module event handler.  */
static cloud_codec_evt_handler_t module_evt_handler;


int data_codec_init(struct cloud_data_cfg *cfg, cloud_codec_evt_handler_t event_handler)
{
	int err;

	ARG_UNUSED(cfg);

	err = lwm2m_codec_helpers_create_objects_and_resources();
	if (err) {
		LOG_ERR("lwm2m_codec_helpers_create_objects_and_resources, error: %d", err);
		return err;
	}

	err = lwm2m_codec_helpers_setup_resources();
	if (err) {
		LOG_ERR("lwm2m_codec_helpers_setup_resources, error: %d", err);
		return err;
	}

	module_evt_handler = event_handler;
	return 0;
}

int data_codec_prepare_cloud_packet(struct cloud_codec_data *cloud_data,
				struct data_lora_sensors *lora_buffer, 
				size_t lora_buf_count,
				struct data_sensors *sensor_buffer,
				size_t sensor_buf_count,
				struct data_modem_static *modem_data,
				struct data_battery *batt_data)
{
	int err;

	if (cloud_data == NULL || sensor_buffer == NULL || sensor_buf_count == 0) {
		return -ENOMEM;
	}

	err = lwm2m_codec_helpers_set_sensor_data(&sensor_buffer[0]);
	if (err == 0) {
		static const struct lwm2m_obj_path path_list[] = {
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 0, TIMESTAMP_RID),
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 0, SENSOR_VALUE_RID),
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 1, TIMESTAMP_RID),
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 1, SENSOR_VALUE_RID),
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 2, TIMESTAMP_RID),
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 2, SENSOR_VALUE_RID),
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 3, TIMESTAMP_RID),
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 3, SENSOR_VALUE_RID),
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 4, TIMESTAMP_RID),
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 4, SENSOR_VALUE_RID),
			LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, POWER_SOURCE_VOLTAGE_RID)
		};

		err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
							       path_list,
							       ARRAY_SIZE(path_list));
		if (err) {
			LOG_ERR("Failed populating object path list, error: %d", err);
			return err;
		}
	}
	
	if (modem_data != NULL) {
		err = lwm2m_codec_helpers_set_modem_static_data(modem_data);
		if (err == 0) {
			static const struct lwm2m_obj_path path_list[] = {
				LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, MODEL_NUMBER_RID),
				LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, MANUFACTURER_RID),
				LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, SOFTWARE_VERSION_RID),
				LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0,
					  DEVICE_SERIAL_NUMBER_ID)
			};
			err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
								path_list,
								ARRAY_SIZE(path_list));
			if (err) {
				LOG_ERR("Failed populating object path list, error: %d", err);
				return err;
			}
		}
	}

	return 0;
}