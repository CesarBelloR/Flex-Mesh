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

#include "etc_device.h"
#include "data_codec.h"
#include "lwm2m_codec_defines.h"
#include "lwm2m_codec_helpers.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cloud_codec, CONFIG_CLOUD_CODEC_LOG_LEVEL);

/* Module event handler.  */
static cloud_codec_evt_handler_t module_evt_handler;


/* Function that is called whenever the configuration object is written to. */
static int config_update_cb(uint16_t obj_inst_id, uint16_t res_id, uint16_t res_inst_id,
			    uint8_t *data, uint16_t data_len, bool last_block, size_t total_size)
{
	/* Because we are dependent on providing all configurations in the
	 * CLOUD_CODEC_EVT_CONFIG_UPDATE event, all configuration is retrieved whenever the
	 * configuration object changes.
	 */
	ARG_UNUSED(obj_inst_id);
	ARG_UNUSED(res_id);
	ARG_UNUSED(res_inst_id);
	ARG_UNUSED(data);
	ARG_UNUSED(data_len);
	ARG_UNUSED(last_block);
	ARG_UNUSED(total_size);

	int err;
	struct etc_config cfg;
	memset(&cfg, 0, sizeof(cfg));
	struct cloud_codec_evt evt = {
		.type = CLOUD_CODEC_EVT_CONFIG_UPDATE,
	};

	err = lwm2m_codec_helpers_get_configuration_object(&cfg);
	if (err) {
		LOG_ERR("lwm2m_codec_helpers_get_configuration_object, error: %d",
			err);
		return err;
	}

	evt.config_update = cfg;
	module_evt_handler(&evt);
	return 0;
}

int data_codec_init(struct etc_config *cfg, cloud_codec_evt_handler_t event_handler)
{
	int err;

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

	err = lwm2m_codec_helpers_setup_configuration_object(cfg, config_update_cb);
	if (err) {
		LOG_ERR("lwm2m_codec_helpers_setup_configuration_object, error: %d",
			err);
		return err;
	}

	err = lwm2m_codec_helpers_set_device_data();
	if (err) {
		LOG_ERR("lwm2m_codec_helpers_set_device_data, error: %d",
			err);
		return err;
	}

	module_evt_handler = event_handler;
	return 0;
}

int data_codec_prepare_update_packet(struct cloud_codec_data *cloud_data)
{
	static const struct lwm2m_obj_path path_list[] = {
		LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, MANUFACTURER_RID),
		LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, MODEL_NUMBER_RID),
		LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, DEVICE_SERIAL_NUMBER_ID),
		LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, FIRMWARE_VERSION_RID),
		LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, DEVICE_TYPE_RID),
		LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, HARDWARE_VERSION_RID),
		LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, BATTERY_STATUS_RID),
		LWM2M_OBJ(ETC_CFG_OBJECT_ID),
		LWM2M_OBJ(ETC_TEMP_OBJECT_ID),
		LWM2M_OBJ(ETC_HUMID_OBJECT_ID),
	};
	int err;

	err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
						       path_list,
						       ARRAY_SIZE(path_list));
	
	return err;
}

int data_codec_prepare_record_packet(struct cloud_codec_data *cloud_data,
				     union etc_device_record *record)
{
	int err = 0;
	if (cloud_data == NULL || record == NULL) {
		LOG_ERR("Null cloud or record data (%d %d)", (cloud_data == NULL), (record == NULL));
		return -ENOMEM;
	}

	err = lwm2m_codec_helpers_set_sensor_data(cloud_data, record);
	if (err == 0) {
		static const struct lwm2m_obj_path path_list[] = {
			/* Always send first temperature object containing ambient data. */
			LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 0),
		};

		err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
							       path_list,
							       ARRAY_SIZE(path_list));
		if (err) {
			LOG_ERR("Failed populating object path list, error: %d", err);
			return err;
		}
	}

	return err;
}

int data_codec_prepare_modem_static_packet(struct cloud_codec_data *cloud_data,
				    struct data_modem_static *modem_data)
{
	int err = 0;

	__ASSERT_NO_MSG(cloud_data != NULL);
	__ASSERT_NO_MSG(modem_data != NULL);

	if (cloud_data == NULL || modem_data == NULL) {
		LOG_ERR("Null cloud or modem data (%d %d)", (cloud_data == NULL), (modem_data == NULL));
		return -EINVAL;
	}

	err = lwm2m_codec_helpers_set_modem_static_data(modem_data);
	if (err == 0) {
		static const struct lwm2m_obj_path path_list[] = {
			LWM2M_OBJ(ETC_INFO_OBJECT_ID),
		};
		err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
							       path_list,
							       ARRAY_SIZE(path_list));
		if (err) {
			LOG_ERR("Failed populating object path list, error: %d", err);
			return err;
		}
	}
	return 0;
}

int data_codec_prepare_modem_dynamic_packet(struct cloud_codec_data *cloud_data,
				    struct data_modem_dynamic *modem_data)
{
	int err = 0;

	__ASSERT_NO_MSG(cloud_data != NULL);
	__ASSERT_NO_MSG(modem_data != NULL);

	if (cloud_data == NULL || modem_data == NULL) {
		LOG_ERR("Null cloud or modem data (%d %d)", (cloud_data == NULL), (modem_data == NULL));
		return -EINVAL;
	}

	err = lwm2m_codec_helpers_set_modem_dynamic_data(modem_data);
	if (err == 0) {
		static const struct lwm2m_obj_path path_list[] = {
			LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID),
		};
		err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
							       path_list,
							       ARRAY_SIZE(path_list));
		if (err) {
			LOG_ERR("Failed populating object path list, error: %d", err);
			return err;
		}
	}
	return 0;
}

int data_codec_prepare_relay_packet(struct cloud_codec_data *cloud_data,
				    const uint8_t *relay_data, uint16_t relay_data_len, bool is_legacy)
{
	static const struct lwm2m_obj_path path_list[] = {
		LWM2M_OBJ(ETC_RELAY_OBJECT_ID, 0, ETC_RELAY_OBJ_R_DATA),
	};
	static const struct lwm2m_obj_path legacy_path_list[] = {
		LWM2M_OBJ(ETC_RELAY_OBJECT_ID, 0, ETC_RELAY_OBJ_R_LEGACY_DATA),
	};
	int err = 0;
	const struct lwm2m_obj_path *path_list_ptr;

	__ASSERT_NO_MSG(cloud_data != NULL);
	__ASSERT_NO_MSG(relay_data != NULL);
	
	if (cloud_data == NULL || relay_data == NULL) {
		LOG_ERR("Null cloud or relay data (%d %d)", (cloud_data == NULL), (relay_data == NULL));
		return -EINVAL;
	}

	if (is_legacy) {
		path_list_ptr = legacy_path_list;
		err = lwm2m_codec_helpers_set_relay_legacy_data((const char*)relay_data, relay_data_len);
	} else {
		path_list_ptr = path_list;
		err = lwm2m_codec_helpers_set_relay_data(relay_data, relay_data_len);
	}

	if (err == 0) {
		err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
							       path_list_ptr,
							       ARRAY_SIZE(path_list));
		BUILD_ASSERT(ARRAY_SIZE(path_list) == ARRAY_SIZE(legacy_path_list));
		if (err) {
			LOG_ERR("Failed populating object path list, error: %d", err);
			return err;
		}
	}

	return 0;
}

int data_codec_clear_data(struct cloud_codec_data *cloud_data)
{
	if (cloud_data == NULL) {
		LOG_ERR("Null cloud data");
		return -EINVAL;
	}

	lwm2m_codec_helpers_object_path_list_clear(cloud_data);
	return 0;
}

int data_codec_prepare_cloud_packet(struct cloud_codec_data *cloud_data,
				    union etc_device_record *record,
				    struct data_modem_dynamic *modem_data)
{
	int err = 0;

	if (cloud_data == NULL) {
		LOG_ERR("Null cloud data");
		return -ENOMEM;
	}

	if (record != NULL) {
		data_codec_prepare_record_packet(cloud_data, record);
	}
	
	if (modem_data != NULL) {
		err = lwm2m_codec_helpers_set_modem_dynamic_data(modem_data);
		if (err == 0) {
			static const struct lwm2m_obj_path path_list[] = {
				LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, RSS),
				LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, QUAL),
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

	/* Add paths currently required by software */
	static const struct lwm2m_obj_path path_list[] = {
		LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, DEVICE_SERIAL_NUMBER_ID),
	};
	err = lwm2m_codec_helpers_object_path_list_add(cloud_data,
						path_list,
						ARRAY_SIZE(path_list));

	return err;
}

int data_codec_prepare_functional_test_data(struct cloud_codec_data *cloud_data,
					    struct functional_test_data *test_data)
{
	int err;
	
	err = lwm2m_codec_helpers_update_functional_test(cloud_data,
							 &test_data->sensor_data,
							 test_data->lte_rsrp,
							 test_data->result);
	return err;
}

int data_codec_prepare_ble_packet(struct cloud_codec_data *cloud_data,
	union etc_device_record *record)
{
	int err = 0;

	if (cloud_data == NULL) {
		LOG_ERR("Null cloud data");
		return -ENOMEM;
	}

	if (record != NULL) {
		err = data_codec_prepare_record_packet(cloud_data, record);
		if (err == 0) {
			static const struct lwm2m_obj_path path_list[] = {
				/* Always send first temperature object containing ambient data. */
				LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 1),
				LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 2),
				LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 3),
				LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 4),
				LWM2M_OBJ(ETC_HUMID_OBJECT_ID, 0),
				LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, DEVICE_SERIAL_NUMBER_ID),
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
	
	return err;
}

int data_codec_has_data(struct cloud_codec_data *cloud_data)
{
	__ASSERT_NO_MSG(cloud_data != NULL);

	return !lwm2m_codec_helpers_object_path_list_is_empty(cloud_data);
}

int data_codec_move_data(struct cloud_codec_data *cloud_data,
			 struct cloud_codec_data *backup_data)
{
	int ret;
	
	ret = lwm2m_codec_helpers_object_path_list_move(cloud_data, backup_data);
	if (ret != 0) {
		LOG_ERR("Move data");
	}

	return ret;
}

int data_codec_split_data(struct cloud_codec_data *cloud_data,
			  struct cloud_codec_data *backup_data)
{
	__ASSERT_NO_MSG(cloud_data != NULL);
	__ASSERT_NO_MSG(backup_data != NULL);

	int ret;

	ret = lwm2m_codec_helpers_object_path_list_split(cloud_data, backup_data);
	if (ret != 0) {
		LOG_ERR("Could not split paths");
		return ret;
	}

	return 0;
}

int data_codec_recover_data(struct cloud_codec_data *cloud_data,
			    struct cloud_codec_data *backup_data)
{
	__ASSERT_NO_MSG(cloud_data != NULL);
	__ASSERT_NO_MSG(backup_data != NULL);

	int ret;
	ret = lwm2m_codec_helpers_object_path_list_add(cloud_data,
						       backup_data->paths,
						       backup_data->valid_object_paths);
	__ASSERT_NO_MSG(ret == 0);
	if (ret != 0) {
		LOG_ERR("Unable to recover paths");
	}
	lwm2m_codec_helpers_object_path_list_clear(backup_data);

	return 0;
}

int data_codec_contains_measurement_data(struct cloud_codec_data *cloud_data)
{
	__ASSERT_NO_MSG(cloud_data != NULL);

	return lwm2m_codec_helpers_object_path_list_contains_measurement(cloud_data);
}

int data_codec_update_reclaim_state(struct cloud_codec_data *cloud_data,
				    enum data_reclaim_state new_state)
{
	int ret;

	ret = lwm2m_codec_helpers_update_reclaim_state(cloud_data, new_state);
	if (ret != 0) {
		LOG_ERR("update reclaim status");
	}

	return ret;
}