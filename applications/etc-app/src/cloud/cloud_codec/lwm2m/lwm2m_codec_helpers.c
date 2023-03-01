/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/net/lwm2m.h>
#include <date_time.h>
#include <lwm2m_resource_ids.h>
#include <string.h>

#include "lwm2m_codec_defines.h"
#include "lwm2m_codec_helpers.h"
#include "etc_settings.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lwm2m_codec_helpers, CONFIG_CLOUD_CODEC_LOG_LEVEL);

/* Some resources does not have designated buffers. Therefore we define those in here. */
static uint8_t bearers[2] = { LTE_FDD_BEARER, NB_IOT_BEARER };
static int battery_voltage;
static time_t button_ts;

/* Timestamps, minimum, and maximum values for the BME680 present on the Thingy:91. */
static double temp_min_range_val = TEMP_MIN_RANGE_VALUE;
static double temp_max_range_val = TEMP_MAX_RANGE_VALUE;
static double humid_min_range_val = HUMID_MIN_RANGE_VALUE;
static double humid_max_range_val = HUMID_MAX_RANGE_VALUE;
static time_t temperature_ts[SENSOR_INPUT_MAX];
static time_t humidity_ts;

static int lwm2m_codec_helpers_set_sensor_ranges(void)
{
	int err;
	
	for (int i = 0; i < SENSOR_INPUT_MAX; i++)
	{
		/* Temperature object. */
		err = lwm2m_set_f64(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, i,
					       MIN_RANGE_VALUE_RID),
				    temp_min_range_val);
		if (err) {
			return err;
		}

		err = lwm2m_set_f64(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, i,
					       MAX_RANGE_VALUE_RID),
				    temp_max_range_val);
		if (err) {
			return err;
		}
	}

	/* Humidity object. */
	err = lwm2m_set_f64(&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 0,
				       MIN_RANGE_VALUE_RID),
			    humid_min_range_val);
	if (err) {
		return err;
	}

	err = lwm2m_set_f64(&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 0,
				       MAX_RANGE_VALUE_RID),
			    humid_max_range_val);
	if (err) {
		return err;
	}

	return 0;
}

int lwm2m_codec_helpers_create_objects_and_resources(void)
{
	int err;

	for (int i = 0; i < SENSOR_INPUT_MAX; i++) {
		err = lwm2m_create_object_inst(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID,
							  i));
		if (err) {
			return err;
		}
	}

	err = lwm2m_create_object_inst(
				&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 0));
	if (err) {
		return err;
	}
#if 0
	err = lwm2m_create_res_inst(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
						AVAIL_NETWORK_BEARER_ID, 0));
	if (err) {
		return err;
	}

	err = lwm2m_create_res_inst(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
						AVAIL_NETWORK_BEARER_ID, 1));
	if (err) {
		return err;
	}

	err = lwm2m_create_res_inst(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
						IP_ADDRESSES, 0));
	if (err) {
		return err;
	}

	err = lwm2m_create_res_inst(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
						APN, 0));
	if (err) {
		return err;
	}
#endif

	err = lwm2m_create_res_inst(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0,
						POWER_SOURCE_VOLTAGE_RID, 0));
	if (err) {
		return err;
	}

	return 0;
}

static int lwm2m_codec_helpers_set_callback_for_config_object(lwm2m_engine_set_data_cb_t callback)
{
	int err;

#if 0
	err = lwm2m_engine_register_post_write_callback(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
								   PASSIVE_MODE_RID),
							callback);
	if (err) {
		return err;
	}

	err = lwm2m_engine_register_post_write_callback(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
								   LOCATION_TIMEOUT_RID),
							callback);
	if (err) {
		return err;
	}

	err = lwm2m_engine_register_post_write_callback(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
								   ACTIVE_WAIT_TIMEOUT_RID),
							callback);
	if (err) {
		return err;
	}

	err = lwm2m_engine_register_post_write_callback(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
								   MOVEMENT_RESOLUTION_RID),
							callback);
	if (err) {
		return err;
	}

	err = lwm2m_engine_register_post_write_callback(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
								   MOVEMENT_TIMEOUT_RID),
							callback);
	if (err) {
		return err;
	}

	err = lwm2m_engine_register_post_write_callback(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
							ACCELEROMETER_ACT_THRESHOLD_RID),
							callback);
	if (err) {
		return err;
	}

	err = lwm2m_engine_register_post_write_callback(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
							ACCELEROMETER_INACT_THRESHOLD_RID),
							callback);
	if (err) {
		return err;
	}

	err = lwm2m_engine_register_post_write_callback(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
							ACCELEROMETER_INACT_TIMEOUT_RID),
							callback);
	if (err) {
		return err;
	}

	err = lwm2m_engine_register_post_write_callback(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
								   GNSS_ENABLE_RID),
							callback);
	if (err) {
		return err;
	}

	err = lwm2m_engine_register_post_write_callback(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
								   NEIGHBOR_CELL_ENABLE_RID),
							callback);
	if (err) {
		return err;
	}
#endif

	return 0;
}

int lwm2m_codec_helpers_setup_resources(void)
{
	int err;

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0,
					   POWER_SOURCE_VOLTAGE_RID),
				&battery_voltage, sizeof(battery_voltage),
				sizeof(battery_voltage), LWM2M_RES_DATA_FLAG_RW);
	if (err) {
		return err;
	}

	for (int i = 0; i < SENSOR_INPUT_MAX; i++) {
		err = lwm2m_set_res_buf(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 
						   i, TIMESTAMP_RID),
					&temperature_ts[i], sizeof(temperature_ts[i]),
					sizeof(temperature_ts[i]), LWM2M_RES_DATA_FLAG_RW);
		if (err) {
			return err;
		}
		err = lwm2m_set_res_buf(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, 
						   i, SENSOR_UNITS_RID),
					TEMP_UNIT, (uint16_t)strlen(TEMP_UNIT),
					(uint16_t)strlen(TEMP_UNIT), LWM2M_RES_DATA_FLAG_RO);
		if (err) {
			return err;
		}
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 
					0, TIMESTAMP_RID),
			&humidity_ts, sizeof(humidity_ts),
			sizeof(humidity_ts), LWM2M_RES_DATA_FLAG_RW);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 0,
					   SENSOR_UNITS_RID),
				HUMID_UNIT, (uint16_t)strlen(HUMID_UNIT),
				(uint16_t)strlen(HUMID_UNIT),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_codec_helpers_set_sensor_ranges();
	if (err) {
		return err;
	}

	return 0;
}

int lwm2m_codec_helpers_setup_configuration_object(struct cloud_data_cfg *cfg,
						   lwm2m_engine_set_data_cb_t callback)
{
	int err;

#if 0
	err = lwm2m_engine_set_bool(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0, PASSIVE_MODE_RID),
				    !cfg->active_mode);
	if (err) {
		return err;
	}

	err = lwm2m_engine_set_s32(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0, LOCATION_TIMEOUT_RID),
				   cfg->location_timeout);
	if (err) {
		return err;
	}

	err = lwm2m_engine_set_s32(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0, ACTIVE_WAIT_TIMEOUT_RID),
				   cfg->active_wait_timeout);
	if (err) {
		return err;
	}

	err = lwm2m_engine_set_s32(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0, MOVEMENT_RESOLUTION_RID),
				   cfg->movement_resolution);
	if (err) {
		return err;
	}

	err = lwm2m_engine_set_s32(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0, MOVEMENT_TIMEOUT_RID),
				   cfg->movement_timeout);
	if (err) {
		return err;
	}

	err = lwm2m_engine_set_float(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
				     ACCELEROMETER_ACT_THRESHOLD_RID),
				     &cfg->accelerometer_activity_threshold);
	if (err) {
		return err;
	}

	err = lwm2m_engine_set_float(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
				     ACCELEROMETER_INACT_THRESHOLD_RID),
				     &cfg->accelerometer_inactivity_threshold);
	if (err) {
		return err;
	}

	err = lwm2m_engine_set_float(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
				     ACCELEROMETER_INACT_TIMEOUT_RID),
				     &cfg->accelerometer_inactivity_timeout);
	if (err) {
		return err;
	}

	/* If the GNSS and Neighbor cell entry in the No data structure is set, its disabled in the
	 * corresponding object.
	 */
	err = lwm2m_engine_set_bool(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
					       GNSS_ENABLE_RID),
				    !cfg->no_data.gnss);
	if (err) {
		return err;
	}

	err = lwm2m_engine_set_bool(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
					       NEIGHBOR_CELL_ENABLE_RID),
				    !cfg->no_data.neighbor_cell);
	if (err) {
		return err;
	}

	err = lwm2m_codec_helpers_set_callback_for_config_object(callback);
	if (err) {
		return err;
	}

#endif
	return 0;
}

int lwm2m_codec_helpers_get_configuration_object(struct cloud_data_cfg *cfg)
{
	int err;

#if 0
	/* There has been a configuration update. Send callback to application with the latest
	 * state of the configuration.
	 */
	err = lwm2m_engine_get_s32(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0, LOCATION_TIMEOUT_RID),
				   &cfg->location_timeout);
	if (err) {
		return err;
	}

	err = lwm2m_engine_get_s32(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0, ACTIVE_WAIT_TIMEOUT_RID),
				   &cfg->active_wait_timeout);
	if (err) {
		return err;
	}

	err = lwm2m_engine_get_s32(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0, MOVEMENT_RESOLUTION_RID),
				   &cfg->movement_resolution);
	if (err) {
		return err;
	}

	err = lwm2m_engine_get_s32(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0, MOVEMENT_TIMEOUT_RID),
				   &cfg->movement_timeout);
	if (err) {
		return err;
	}

	err = lwm2m_engine_get_float(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
				     ACCELEROMETER_ACT_THRESHOLD_RID),
				     &cfg->accelerometer_activity_threshold);
	if (err) {
		return err;
	}

	err = lwm2m_engine_get_float(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
				     ACCELEROMETER_INACT_THRESHOLD_RID),
				     &cfg->accelerometer_inactivity_threshold);
	if (err) {
		return err;
	}

	err = lwm2m_engine_get_float(LWM2M_PATH(CONFIGURATION_OBJECT_ID, 0,
				     ACCELEROMETER_INACT_TIMEOUT_RID),
				     &cfg->accelerometer_inactivity_timeout);
	if (err) {
		return err;
	}

	/* If the GNSS and neighbor cell entry in the No data structure is set, its disabled in the
	 * corresponding object.
	 */
	bool gnss_enable_temp;
	bool ncell_enable_temp;
	bool passive_mode_temp;

	err = lwm2m_engine_get_bool(LWM2M_PATH(CONFIGURATION_OBJECT_ID,
					       0,
					       PASSIVE_MODE_RID),
					       &passive_mode_temp);
	if (err) {
		return err;
	}

	cfg->active_mode = (passive_mode_temp == true) ? false : true;

	err = lwm2m_engine_get_bool(LWM2M_PATH(CONFIGURATION_OBJECT_ID,
					       0,
					       GNSS_ENABLE_RID),
					       &gnss_enable_temp);
	if (err) {
		return err;
	}

	err = lwm2m_engine_get_bool(LWM2M_PATH(CONFIGURATION_OBJECT_ID,
					       0,
					       NEIGHBOR_CELL_ENABLE_RID),
					       &ncell_enable_temp);
	if (err) {
		return err;
	}

	cfg->no_data.gnss = (gnss_enable_temp == true) ? false : true;
	cfg->no_data.neighbor_cell = (ncell_enable_temp == true) ? false : true;

	return 0;
#endif
}

int lwm2m_codec_helpers_set_modem_dynamic_data(struct data_modem_dynamic *modem_dynamic)
{
	int err;
	int64_t current_time;

	if (!modem_dynamic->queued) {
		return -ENODATA;
	}

#if 0
	if (modem_dynamic->nw_mode == LTE_LC_LTE_MODE_LTEM) {
		err = lwm2m_engine_set_u8(LWM2M_PATH(
						LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
						NETWORK_BEARER_ID),
						LTE_FDD_BEARER);
		if (err) {
			return err;
		}
	} else if (modem_dynamic->nw_mode == LTE_LC_LTE_MODE_NBIOT) {
		err = lwm2m_engine_set_u8(LWM2M_PATH(
						LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0,
						NETWORK_BEARER_ID),
						NB_IOT_BEARER);
		if (err) {
			return err;
		}
	} else {
		return -EINVAL;
	}
#endif

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID,
					   0, AVAIL_NETWORK_BEARER_ID, 0),
				&bearers[0], sizeof(bearers[0]), sizeof(bearers[0]),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID,
					   0, AVAIL_NETWORK_BEARER_ID, 1),
				&bearers[1], sizeof(bearers[1]), sizeof(bearers[1]),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID,
					   0, IP_ADDRESSES, 0),
				modem_dynamic->ip, (uint16_t)strlen(modem_dynamic->ip),
				(uint16_t)strlen(modem_dynamic->ip),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID,
					   0, APN, 0),
				modem_dynamic->apn, (uint16_t)strlen(modem_dynamic->apn),
				(uint16_t)strlen(modem_dynamic->apn),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_s8(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, RSS),
			   (int8_t)modem_dynamic->rsrp);
	if (err) {
		return err;
	}

	err = lwm2m_set_u32(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, CELLID),
			    modem_dynamic->cell);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, SMNC),
			    modem_dynamic->mnc);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, SMCC),
			    modem_dynamic->mcc);
	if (err) {
		return err;
	}

	err = lwm2m_set_u16(&LWM2M_OBJ(LWM2M_OBJECT_CONNECTIVITY_MONITORING_ID, 0, LAC),
			    modem_dynamic->area);
	if (err) {
		return err;
	}

	err = date_time_now(&current_time);
	if (err) {
		return err;
	}

	err = lwm2m_set_time(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, CURRENT_TIME_RID),
			     (current_time / MSEC_PER_SEC));
	if (err) {
		return err;
	}

	return 0;
}

int lwm2m_codec_helpers_set_modem_static_data(struct data_modem_static *modem_static)
{
	int err;

	if (!modem_static->queued) {
		return -ENODATA;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, MODEL_NUMBER_RID),
				modem_static->brdv,
				(uint16_t)strlen(modem_static->brdv),
				(uint16_t)strlen(modem_static->brdv),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, MANUFACTURER_RID),
				CONFIG_CLOUD_CODEC_MANUFACTURER,
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_MANUFACTURER),
				(uint16_t)strlen(CONFIG_CLOUD_CODEC_MANUFACTURER),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, SOFTWARE_VERSION_RID),
				modem_static->fw,
				(uint16_t)strlen(modem_static->fw),
				(uint16_t)strlen(modem_static->fw),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	err = lwm2m_set_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0,
					    DEVICE_SERIAL_NUMBER_ID),
				modem_static->imei,
				(uint16_t)strlen(modem_static->imei),
				(uint16_t)strlen(modem_static->imei),
				LWM2M_RES_DATA_FLAG_RO);
	if (err) {
		return err;
	}

	return 0;
}


int lwm2m_codec_helpers_set_sensor_data(struct data_sensors *sensor)
{
	int err;

	if (!sensor->queued) {
		return -ENODATA;
	}

	err = lwm2m_set_s32(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID, 0, POWER_SOURCE_VOLTAGE_RID),
			   sensor->data.battery_mV);
	if (err) {
		return err;
	}

	for (int i = 0; i < SENSOR_INPUT_MAX; i++) {
		err = lwm2m_set_time(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, i, TIMESTAMP_RID),
				(int32_t)(sensor->data.timestamp / MSEC_PER_SEC));
		if (err) {
			return err;
		}
	}

#if 0
	err = lwm2m_set_time(&LWM2M_OBJ(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 0, TIMESTAMP_RID),
			     (int32_t)(sensor->data.timestamp / MSEC_PER_SEC));
	if (err) {
		return err;
	}
#endif

	for (int i = 0; i < SENSOR_INPUT_MAX; i++) {
		err = lwm2m_set_f64(&LWM2M_OBJ(ETC_TEMP_OBJECT_ID, i, SENSOR_VALUE_RID),
				    sensor->data.temperature[i]);
		if (err) {
			return err;
		}
	}

#if 0
	err = lwm2m_engine_set_float(LWM2M_PATH(IPSO_OBJECT_HUMIDITY_SENSOR_ID, 0,
						SENSOR_VALUE_RID),
				     &sensor->humidity);
	if (err) {
		return err;
	}
#endif

	return 0;
}

int lwm2m_codec_helpers_object_path_list_add(struct cloud_codec_data *output,
					     const struct lwm2m_obj_path path[],
					     size_t path_size)
{
	bool path_added = false;
	uint8_t j = 0;

	if (output == NULL || path == NULL || path_size == 0) {
		return -EINVAL;
	}

	for (int i = 0; i < ARRAY_SIZE(output->paths); i++) {
		if (output->paths[i].level == 0) {

			output->paths[i].obj_id = path[j].obj_id;
			output->paths[i].obj_inst_id = path[j].obj_inst_id;
			output->paths[i].res_id = path[j].res_id;
			output->paths[i].res_inst_id = path[j].res_inst_id;
			output->paths[i].level = path[j].level;

			output->valid_object_paths++;
			path_added = true;
			j++;

			if (j == path_size) {
				break;
			}
		}
	}

	/* This API can be called multiple times to populate the same path buffer. Due to this we
	 * also check if all entries in the incoming path[] list was added.
	 */
	if (!path_added || (j != path_size)) {
		return -ENOMEM;
	}

	return 0;
}
