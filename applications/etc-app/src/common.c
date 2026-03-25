#include "etc_device.h"
#include "etc_sensor.h"
#include "etc_settings.h"
#include "etc_battery.h"
#include "app_version.h"
#include "common.h"
#include "etc_util.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(common, CONFIG_ETC_APP_LOG_LEVEL);

#define PAYLOAD_LOGGER_LEGACY_LEN 110

static char decoded_buf[PAYLOAD_LOGGER_LEGACY_LEN] = {0x00};

void etc_common_add_sensor_value(char *decoded_buf, int *decoded_buf_len, int decoded_buf_size,
				 float value)
{
	if (sensor_temperature_is_valid(value)) {
		*decoded_buf_len += snprintf(decoded_buf + *decoded_buf_len,
					     decoded_buf_size - *decoded_buf_len, "%.1f,", value);
	} else {
		*decoded_buf_len += snprintf(decoded_buf + *decoded_buf_len,
					     decoded_buf_size - *decoded_buf_len, "*,");
	}
}

int etc_common_prepare_relay_legacy_data(struct etc_device_relay_record *record, char *out_buf,
					 int *out_len, int out_size)
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
		<isParent?>,<isReclaimed?>,
		<temp 1.B>,<temp 2.B>,<temp 3.B>,<temp 4.B>,
		<Error Code>
	 */
	float relay_vbat = (float)etc_battery_get_voltage_mV() / 1000.0;
	decoded_buf_len +=
		snprintf(out_buf, out_size, "%s,%d,%s,%.2f,*,%.2f,*,%s,%d,%d,", record->logger_ver,
			 record->logger_rssi, record->logger_id, record->battery, relay_vbat,
			 APP_VERSION_STRING, record->packet_number, record->timestamp);

	for (int i = 0; i <= SENSOR_INPUT_IN4; i++) {
		etc_common_add_sensor_value(out_buf, &decoded_buf_len, out_size, record->sensor[i]);
	}
	etc_common_add_sensor_value(out_buf, &decoded_buf_len, out_size,
				    record->sensor[SENSOR_INPUT_AMBIENT]);

	if (sensor_humidity_is_valid(record->sensor[SENSOR_INPUT_HUMID])) {
		decoded_buf_len += snprintf(out_buf + decoded_buf_len, out_size - decoded_buf_len,
					    "%.1f,", record->sensor[SENSOR_INPUT_HUMID]);
	} else {
		decoded_buf_len +=
			snprintf(out_buf + decoded_buf_len, out_size - decoded_buf_len, "*,");
	}

	decoded_buf_len += snprintf(out_buf + decoded_buf_len, out_size - decoded_buf_len, "%d,%d,",
				    is_parent ? 1 : 0, record->is_reclaim ? 1 : 0);

	/* Splitter sub-port temperatures (fixed position after isReclaimed).
	 * Only include if at least one sub-port has a valid reading.
	 */
	bool has_splitter = false;
	for (int i = SENSOR_INPUT_IN5; i <= SENSOR_INPUT_IN8; i++) {
		if (sensor_temperature_is_valid(record->sensor[i])) {
			has_splitter = true;
			break;
		}
	}
	if (has_splitter) {
		for (int i = SENSOR_INPUT_IN5; i <= SENSOR_INPUT_IN8; i++) {
			etc_common_add_sensor_value(out_buf, &decoded_buf_len, out_size,
						    record->sensor[i]);
		}
	}

	for (int i = 0; i < ETC_DEVICE_NUM_EXTRA_ELEMENT; i++) {
		if (record->data[i] == -1) {
			decoded_buf_len += snprintf(out_buf + decoded_buf_len,
						    out_size - decoded_buf_len, "*,");
		} else if (record->data[i] != ETC_DEVICE_INVALID_VALUE_ELEMENT) {
			decoded_buf_len +=
				snprintf(out_buf + decoded_buf_len, out_size - decoded_buf_len,
					 "%d,", record->data[i]);
		} else {
			break;
		}
	}

	if (decoded_buf_len + 1 <= out_size) {
		out_buf[decoded_buf_len] = '\0';
	} else {
		return -ENOMEM;
	}

	*out_len = decoded_buf_len;
	return 0;
}

int etc_common_prepare_relay_legacy_packet(struct etc_device_relay_packet *packet, char *out_buf,
					   int *out_len, int out_size)
{
	char *p_buf = out_buf;
	int temp_len = 0;
	int ret;
	int packed = 0;

	*out_len = 0;

	for (int i = 0; i < packet->num_records; i++) {
		int sep_len = 0;

		/* Records are joined with '|'. Write the separator first, but be ready
		 * to back it out if the following record does not fit. */
		if (packed > 0) {
			if ((out_size - *out_len) <= 1) {
				break;
			}
			*p_buf = '|';
			p_buf++;
			(*out_len)++;
			*p_buf = '\0';
			sep_len = 1;
		}

		ret = etc_common_prepare_relay_legacy_data(&packet->records[i], p_buf, &temp_len,
							   out_size - *out_len);
		if (ret != 0) {
			/* This record does not fit. Drop the separator just written and
			 * stop here; the unpacked records stay queued for the next send so
			 * the relay always makes forward progress instead of stalling on an
			 * oversized package (FW-886). */
			p_buf -= sep_len;
			*out_len -= sep_len;
			if (sep_len) {
				*p_buf = '\0';
			}
			break;
		}
		p_buf += temp_len;
		*out_len += temp_len;
		packed++;
	}

	if (packed == 0) {
		/* Not even the first record fit (a single record larger than out_size).
		 * Surface the error as before - there is no partial progress to make. */
		return -ENOMEM;
	}

	/* Report how many records actually made it into the buffer so the caller can
	 * advance the relay read index by exactly that many. */
	packet->num_records = packed;
	return 0;
}

bool etc_common_is_packet_from_parent(char *relay_iccid, char *relay_id)
{
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
					  char *out_buf, uint8_t *out_len)
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
	decoded_buf_len +=
		snprintf(decoded_buf, sizeof(decoded_buf), "%s,%s,%1.2f,%d,%d,", APP_VERSION_STRING,
			 buf_tmp, record.battery, pkt_counter, record.timestamp);

	pkt_counter += 1;
	if (pkt_counter >= LOGGER_MAXIMUM_COUNTER) {
		pkt_counter = 0;
	}

	for (int i = 0; i <= SENSOR_INPUT_IN4; i++) {
		etc_common_add_sensor_value(decoded_buf, &decoded_buf_len, sizeof(decoded_buf),
					    record.sensor[i]);
	}
	etc_common_add_sensor_value(decoded_buf, &decoded_buf_len, sizeof(decoded_buf),
				    record.sensor[SENSOR_INPUT_AMBIENT]);

	if (sensor_humidity_is_valid(record.sensor[SENSOR_INPUT_HUMID])) {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
					    sizeof(decoded_buf) - decoded_buf_len, "%2.2f,",
					    record.sensor[SENSOR_INPUT_HUMID]);
	} else {
		decoded_buf_len += snprintf(decoded_buf + decoded_buf_len,
					    sizeof(decoded_buf) - decoded_buf_len, "*,");
	}

	decoded_buf_len +=
		snprintf(decoded_buf + decoded_buf_len, sizeof(decoded_buf) - decoded_buf_len, "%d",
			 is_reclaim ? 1 : 0);
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
