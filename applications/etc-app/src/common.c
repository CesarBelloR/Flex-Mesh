#include "etc_device.h"
#include "etc_sensor.h"
#include "etc_settings.h"
#include "app_version.h"
#include "common.h"
#include "cloud/cloud_codec/data_codec.h"

#define PAYLOAD_LEGACY_LEN	CONFIG_LWM2M_ETC_RELAY_OBJ_DATA_SIZE

static char decoded_buf[PAYLOAD_LEGACY_LEN] = {0x00};

int etc_common_prepare_relay_legacy_data(struct etc_device_relay_record record, 
    bool is_reclaim, char* out_buf, int* out_len) 
{
	static uint8_t pkt_counter = 0;
	int decoded_buf_len = 0;

	decoded_buf_len += snprintf(decoded_buf, sizeof(decoded_buf), 
		"%s,%d,%s,%.2f,-1,-1,-1,%s,%d,", record.logger_ver, record.logger_rssi, 
        record.logger_id, record.battery, APP_VERSION_STR, record.timestamp);

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
	decoded_buf_len += 1;
	decoded_buf[decoded_buf_len] = '\0';
	if (decoded_buf_len > *out_len) {
		return -ENOMEM;
	}

	memcpy(out_buf, decoded_buf, decoded_buf_len);
	*out_len = decoded_buf_len;
	return 0;
}
