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

int etc_common_prepare_relay_legacy_data(struct etc_device_relay_record record, 
        bool is_reclaim, char* out_buf, int* out_len);
#endif /* COMMON_H_ */