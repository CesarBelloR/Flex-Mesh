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
#ifdef CONFIG_ETC_BLE_PAYLOAD_LEGACY_FORMAT
/**
 * @brief Prepares legacy logger data for the common ETC device.
 * 
 * This function prepares legacy logger data for the common ETC device based on the provided device record.
 * 
 * @param record The device record containing necessary data.
 * @param is_reclaim Flag indicating if the operation is a reclaim.
 * @param out_buf Pointer to the buffer where the prepared data will be stored.
 * @param out_len Pointer to the variable storing the length of the prepared data.
 * 
 * @return 0 on success
 */
int etc_common_prepare_logger_legacy_data(union etc_device_record record, bool is_reclaim, 
	char* out_buf, uint8_t* out_len) ;
#endif

/**
 * @brief Export the relay command from lwM2M to Flex action
 * 
 * @param buf The input command from LwM2M
 * @param len The length of input command.
 * 
 * @return 0 on success
 */
int etc_common_export_relay_command(const char* buf, const size_t len);
#endif /* COMMON_H_ */