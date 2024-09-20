#ifndef DATA_CODEC_H__
#define DATA_CODEC_H__

#include <stdbool.h>
#include "events/sensor_event.h"

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

#endif 