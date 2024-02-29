#ifndef COMMON_H_
#define COMMON_H_

#include "etc_device.h"

typedef enum {
        ETC_ADC_CHANNEL_AMB = 0,
        ETC_ADC_CHANNEL_BATTERY,
        ETC_ADC_CHANNEL_SENSOR,
        ETC_ADC_CHANNEL_HW_VER,
} etc_adc_channel_e;

#define LOGGER_MAXIMUM_COUNTER  99

/**
 * @brief Prepares legacy relay data.
 *
 * This function prepares legacy relay data based on the given record. The data 
 * is stored in the provided buffer and its length is updated accordingly.
 *
 * @param record Pointer to the relay record to prepare legacy data for.
 * @param out_buf Pointer to the buffer where the legacy data will be stored.
 * @param out_len Pointer to the length of the output data, updated by the function.
 *
 * @return 0 on success
 */
int etc_common_prepare_relay_legacy_data(struct etc_device_relay_record *record, 
	char* out_buf, int* out_len);

/**
 * @brief Checks if a packet is from the parent relay.
 *
 * This function checks if a packet is from the parent relay based on the given
 * relay ID.
 *
 * @param relay_iccid The current relay ICCID
 * @param relay_id The relay ID to check.
 *
 * @return true if the packet is from the parent relay, false otherwise.
 */
bool etc_common_is_packet_from_parent(char* relay_iccid, char* relay_id);
#endif /* COMMON_H_ */