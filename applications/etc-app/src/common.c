#include "etc_device.h"
#include "etc_sensor.h"
#include "etc_settings.h"
#include "etc_battery.h"
#include "app_version.h"
#include "common.h"
#include "cloud/cloud_codec/data_codec.h"

#define PAYLOAD_LEGACY_LEN	CONFIG_LWM2M_ETC_RELAY_OBJ_DATA_SIZE

static char decoded_buf[PAYLOAD_LEGACY_LEN] = {0x00};

int etc_common_prepare_relay_legacy_data(struct etc_device_relay_record *record, 
					char* out_buf, int* out_len) 
{
	int decoded_buf_len = 0;
	bool is_parent = false;

	char relay_iccid[ETC_SETTING_RELAY_ICCID_LEN + 1] = {0};
	etc_get_relay_iccid(relay_iccid, sizeof(relay_iccid));
	relay_iccid[ETC_SETTING_RELAY_ICCID_LEN] = '\0';

	if (etc_common_is_packet_from_parent(relay_iccid, record->relay_id)) {
		is_parent = true;  
	}
	
	/* Legacy format: 
		<Logger FW Ver>,<Logger RSSI>,<Logger ID>,<Logger Vbat>,
		`<Relay RSSI>`,`<Relay Vbat>`,`<Relay Qual>`,<Relay FW Ver>,
		<packet #>,<timestamp>,
		<temp 1>,<temp 2>,<temp 3>,<temp 4>,<temp 5>,<Humidity>,
		<isParent?>,<isReclaimed?>
	 */
	float relay_vbat = (float)etc_battery_get_voltage_mV() / 1000.0;
	decoded_buf_len += snprintf(decoded_buf, sizeof(decoded_buf), 
				   "%s,%d,%s,%.2f,*,%.2f,*,%s,%d,%d,", record->logger_ver, 
				   record->logger_rssi, record->logger_id, record->battery, relay_vbat,
				   APP_VERSION_STR, record->packet_number, record->timestamp);

	for (int i = 0; i <= SENSOR_INPUT_AMBIENT; i++) {
		if (data_codec_compare_temperature_is_valid(record->sensor[i])) {
			decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
				sizeof(decoded_buf) - decoded_buf_len, "%.1f,",
				record->sensor[i]);
		} else {
			decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
				sizeof(decoded_buf) - decoded_buf_len, "*,");
		}
	}
	
	if (data_codec_compare_humidity_is_valid(record->sensor[SENSOR_INPUT_HUMID])) {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
			sizeof(decoded_buf) - decoded_buf_len, "%.1f,",
			record->sensor[SENSOR_INPUT_HUMID]);
	} else {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
			sizeof(decoded_buf) - decoded_buf_len, "*,");
	}

	decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
				   sizeof(decoded_buf) - decoded_buf_len, "%d,%d,", 
				   is_parent ? 1 : 0, record->is_reclaim ? 1 : 0);
	for (int i = 0; i < ETC_DEVICE_NUM_EXTRA_ELEMENT; i++) {
		if (record->data[i] == -1) {
			decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
						    sizeof(decoded_buf) - decoded_buf_len, "*,");
		} else if (record->data[i] != ETC_DEVICE_INVALID_VALUE_ELEMENT){
			decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
						    sizeof(decoded_buf) - decoded_buf_len, 
						    "%d,", record->data[i]);
		} else {
			break;
		}
	}
	decoded_buf[decoded_buf_len] = '\0';
	/* Include null terminator. Do not increment decodec_buf_len, as this
	 * would also modify out_len. */
	if ((decoded_buf_len + 1) > *out_len) {
		return -ENOMEM;
	}

	memcpy(out_buf, decoded_buf, (decoded_buf_len + 1));
	*out_len = decoded_buf_len;
	return 0;
}

bool etc_common_is_packet_from_parent(char* relay_iccid, char* relay_id) {
	if (((strncmp(relay_id, "OPEN", strlen("OPEN")) == 0) &&
		(strlen(relay_id) == strlen("OPEN"))) ||
		(strncmp(relay_id, relay_iccid, ETC_SETTING_RELAY_ICCID_LEN) == 0 &&
		(strlen(relay_id) == strlen(relay_iccid)))) {
		return true;
	}
	return false;
}

#ifdef CONFIG_ETC_BLE_PAYLOAD_LEGACY_FORMAT

static char buf_tmp[ETC_SETTINGS_DEVICE_ID_LEN];

int etc_common_prepare_logger_legacy_data(union etc_device_record record, bool is_reclaim, 
	char* out_buf, uint8_t* out_len) 
{
	static uint8_t pkt_counter = 0;
	etc_get_device_id(buf_tmp, ETC_SETTINGS_DEVICE_ID_LEN);
	int decoded_buf_len = 0;

	/* Legacy format:
		<Firmware version>,<ID/Serial number>,<Battery voltage>,
		<packet #>,<timestamp>,
		<temp 1>,<temp 2>,<temp 3>,<temp 4>,<temp 5>,<humidity>,
		<isReclaimed>
	 */
	decoded_buf_len += snprintf(decoded_buf, sizeof(decoded_buf), 
		"%s,%s,%1.2f,%d,%d,", APP_VERSION_STR, buf_tmp, record.battery,
		pkt_counter, record.timestamp);

	pkt_counter += 1;
	if (pkt_counter >= LOGGER_MAXIMUM_COUNTER) {
		pkt_counter = 0;
	}

	for (int i = 0; i <= SENSOR_INPUT_AMBIENT; i++) {
		if (data_codec_compare_temperature_is_valid(record.sensor[i])) {
			decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
							sizeof(decoded_buf) - decoded_buf_len, "%2.2f,",
							record.sensor[i]);
		} else {
			decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
							sizeof(decoded_buf) - decoded_buf_len, "*,");
		}
	}
	
	if (data_codec_compare_humidity_is_valid(record.sensor[SENSOR_INPUT_HUMID])) {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
			sizeof(decoded_buf) - decoded_buf_len, "%2.2f,",
			record.sensor[SENSOR_INPUT_HUMID]);
	} else {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
			sizeof(decoded_buf) - decoded_buf_len, "*,");
	}

	decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
		sizeof(decoded_buf) - decoded_buf_len, "%d", is_reclaim ? 1 : 0);
	decoded_buf[decoded_buf_len] = '\0';
	/* Include null terminator. Do not increment decodec_buf_len, as this
	 * would also modify out_len. */
	if ((decoded_buf_len + 1) > *out_len) {
		return -ENOMEM;
	}

	memcpy(out_buf, decoded_buf, decoded_buf_len + 1);
	*out_len = decoded_buf_len;
	return 0;
}
#endif