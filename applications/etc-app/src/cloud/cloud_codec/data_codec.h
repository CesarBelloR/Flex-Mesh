#ifndef DATA_CODEC_H__
#define DATA_CODEC_H__

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <zephyr/net/net_ip.h>
#include "events/lora_event.h"
#include "events/sensor_event.h"

#include "etc_device.h"
#include "etc_settings.h"
#include "modem_api.h"

#if defined(CONFIG_LWM2M)
#include <zephyr/net/lwm2m.h>
#else
#include "lwm2m/lwm2m_dummy.h"
#endif

/** @brief Structure containing battery data published to cloud. */
struct data_battery {
	struct battery_data data;
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

struct data_sensors {
	struct sensor_data data;
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

struct data_modem_static {
	/** Static modem data timestamp. UNIX milliseconds. */
	int64_t ts;
	char manufacturer[10];
	/** Device board version. */
	char model[16];
	/** Modem firmware. */
	char fw[64];
	/** Device IMEI. */
	char imei[16];
	/** SIM IMSI */
	char imsi[16];
	/** SIM ICCID */
	char iccid[23];
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

struct data_modem_dynamic {
	/** Dynamic modem data timestamp. UNIX milliseconds. */
	int64_t ts;
	/** Band number. */
	uint8_t band;
	/** Mobile Country Code. */
	uint16_t mcc;
	/** Mobile Network Code. */
	uint16_t mnc;
	/** Area code. */
	uint16_t area;
	/** Cell id. */
	uint32_t cell;
	/** Reference Signal Received Power. */
	int16_t rsrp;
	/** Signal quality, RSRQ */
	int16_t qual;
	/* Access technology (NB-IoT or LTE-M) */
	enum access_technology nw_mode;
	/* PSM Active timer value in s */
	uint16_t psm_active_time_s;
	/* PSM periodic timer value in s */
	uint32_t psm_periodic_atu_s;
	/** Internet Protocol Address. */
	char ip[INET6_ADDRSTRLEN];
	/** Access Point Name. */
	char apn[CONFIG_CLOUD_CODEC_APN_LEN_MAX];
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

struct data_lora_sensors {
	int64_t env_ts;
	char sensor_msg[LORA_EVENT_MSG_DATA_LEN];
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

struct cloud_codec_data {
	/** Encoded output. */
	char *buf;
	/** Length of encoded output. */
	size_t len;
	/** LwM2M object paths. */
	struct lwm2m_obj_path paths[CONFIG_CLOUD_CODEC_LWM2M_PATH_LIST_ENTRIES_MAX];
	/** Number of valid paths in the paths variable. */
	uint8_t valid_object_paths;
};

enum cloud_codec_event_type {
	/** Only used in LwM2M codec. This event carries a config update. */
	CLOUD_CODEC_EVT_CONFIG_UPDATE = 1,
};

struct cloud_codec_evt {
	/** Cloud codec event type. */
	enum cloud_codec_event_type type;
	/** New config data. */
	struct etc_config config_update;
};

/**
 * @brief Event handler prototype.
 *
 * @param[in] evt Event type.
 */
typedef void (*cloud_codec_evt_handler_t)(const struct cloud_codec_evt *evt);

/** @brief Type of data to be handled by the respective API. Used to signify what data structure
 *         that is passed in to the function.
 */
enum json_common_buffer_type {
	JSON_COMMON_SENSOR,
	JSON_COMMON_LORA_SENSOR,
	JSON_COMMON_COUNT
};

typedef union {
	struct data_lora_sensors lora;
	struct data_battery battery;
	struct data_sensors sensor;
} data_etc_sensors;

/** @brief Operation to be carried out with the passed in data. */
enum json_common_op_code {
	JSON_COMMON_INVALID,
	/** Encode data and add it to a passed in parent array object. This option does not
	 *  label the encoded data.
	 */
	JSON_COMMON_ADD_DATA_TO_ARRAY,
	/** Encode data and add it to a passed in parent object. */
	JSON_COMMON_ADD_DATA_TO_OBJECT,
	/** Encode data and set the passed in object pointer to point to it. */
	JSON_COMMON_GET_POINTER_TO_OBJECT
};

static inline bool data_codec_compare_temperature_is_valid(float temperature) {
	if ((temperature >= SENSOR_TEMP_C_MIN) && 
	    (temperature <= SENSOR_TEMP_C_MAX)) {
		return true;
	}
	return false;
}

static inline bool data_codec_compare_humidity_is_valid(float humidity) {
	if ((humidity >= SENSOR_HUMID_C_MIN) && 
	    (humidity <= SENSOR_HUMID_C_MAX)) {
		return true;
	}
	return false;
} 

int data_codec_init(struct etc_config *cfg, cloud_codec_evt_handler_t event_handler);

void data_codec_populate_lora_sensor_buffer(
				struct data_lora_sensors *sensor_buffer,
				struct data_lora_sensors *new_sensor_data,
				int *head_sensor_buf,
				size_t buffer_count);

void data_codec_populate_sensor_internal_buffer(
				struct data_sensors *sensor_buffer,
				struct data_sensors *new_sensor_data,
				int *head_sensor_buf,
				size_t buffer_count);

int data_codec_prepare_cloud_packet(struct cloud_codec_data *cloud_data,
				    union etc_device_record *record,
				    struct data_modem_dynamic *modem_data);

int data_codec_prepare_modem_static_packet(struct cloud_codec_data *cloud_data,
				    struct data_modem_static *modem_data);

int data_codec_prepare_modem_dynamic_packet(struct cloud_codec_data *cloud_data,
				    struct data_modem_dynamic *modem_data);

/**
 * @brief Prepare a relay packet that relays data from LoRa loggers to the cloud.
 * 
 * @param cloud_data Pointer to the cloud_data struct.
 * @param[in] relay_data Pointer to the relay data that should be sent to cloud.
 * @param[in] relay_data_len Length of the relay data.
 * 
 * @return 0 on success, otherwise a negative value indicating an error.
*/
int data_codec_prepare_relay_packet(struct cloud_codec_data *cloud_data,
				    const uint8_t *relay_data, uint16_t relay_data_len);

/**
 * Prepare a packet that reflects the current status of devices. This is
 * used to update the server's data model when a device first turns on.
 * 
 * @param cloud_data Pointer to the cloud_data struct.
*/
int data_codec_prepare_update_packet(struct cloud_codec_data *cloud_data);

/** 
 * @brief Clear the data saved in the cloud_data struct. This should be done
 *        after the data has been sent to the cloud, if the struct is to be reused.
 * 
 * @param cloud_data Pointer to the cloud_data struct.
*/
int data_codec_clear_data(struct cloud_codec_data *cloud_data);

/**
 * @brief Check if cloud_data contains any data to be sent.
 * 
 * @param cloud_data Pointer to struct cloud_data instance.
 * @return 1 if cloud_data contains data, 0 if not.
*/
int data_codec_has_data(struct cloud_codec_data *cloud_data);

/**
 * @brief Remove data to be sent from cloud_data and save it in backup_data.
 * 
 * This can be used if the message size would be otherwise too big to
 * send all data at once.
 * @param cloud_data Pointer to struct cloud_data instance that data will be removed from.
 * @param backup_data Pointer to struct cloud_data instance that removed data
 *                    from cloud_data will be added to.
 * @return 0 on success, otherwise error.
*/
int data_codec_split_data(struct cloud_codec_data *cloud_data,
			  struct cloud_codec_data *backup_data);

/**
 * @brief Add data from backup_data to cloud_data and clear backup_data data.
 * 
 * Use this to recover data previously saved in backup_data with
 * @ref data_codec_split_data().
 * 
 * @param cloud_data Pointer to struct cloud_data instance that data will be added to.
 * @param backup_data Pointer to struct cloud_data instance that with data
 *                    to recover and copy into cloud_data.
 * @return 0 on success, otherwise error.
*/
int data_codec_recover_data(struct cloud_codec_data *cloud_data,
			    struct cloud_codec_data *backup_data);

/**
 * @brief Check if cloud_data contains any measurement data.
 * 
 * Temperature and humidity data is considered measurement data. Anything
 * else is not.
 * @param cloud_data Pointer to struct cloud_data instance to check for
 *                   measurement data.
 * @return 0 on success, otherwise error.
*/
int data_codec_contains_measurement_data(struct cloud_codec_data *cloud_data);

#endif /* DATA_CODEC_H__ */