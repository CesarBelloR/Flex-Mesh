#ifndef DATA_CODEC_H__
#define DATA_CODEC_H__

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/** @brief Structure containing battery data published to cloud. */
struct data_battery {
	/** Battery voltage level. */
	uint16_t bat;
	/** Battery data timestamp. UNIX milliseconds. */
	int64_t bat_ts;
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

struct data_sensors {
	/** Environmental sensors timestamp. UNIX milliseconds. */
	int64_t env_ts;
	/** Temperature in celcius. */
	double temperature;
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

struct data_modem_static {
	/** Static modem data timestamp. UNIX milliseconds. */
	int64_t ts;
	/** Device board version. */
	char brdv[30];
	/** Modem firmware. */
	char fw[40];
	/** Device IMEI. */
	char imei[16];
	/** Flag signifying that the data entry is to be encoded. */
	bool queued : 1;
};

#endif /* DATA_CODEC_H__ */