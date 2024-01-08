/***************************************************************************/
/*!
\file       adc.c
\brief      ADC Driver to read analog data

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <hal/nrf_saadc.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>

#include "adc.h"

#define SAMPLES_FILTERED_ADC CONFIG_ADC_MODULES_FILTER_SAMPLE_COUNT

LOG_MODULE_REGISTER(ETC_ADC, CONFIG_ADC_MODULES_LOG_LEVEL);

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA)
};

int adc_init(void)
{
	/* Configure channels individually prior to sampling. */
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_channels[i].dev)) {
			LOG_ERR("ADC controller device not ready\n");
			return -EINVAL;
		}

		int err = adc_channel_setup_dt(&adc_channels[i]);
		if (err < 0) {
			LOG_ERR("Could not setup channel #%d (%d)", i, err);
			return -EINVAL;
		}
	}

	return 0;
}

int adc_get_channel_filtered(int channel)
{
	int16_t sample_buffer[SAMPLES_FILTERED_ADC];
	for (int i = 0; i < SAMPLES_FILTERED_ADC - 1; i++) {
		sample_buffer[i] = adc_get_channel(channel);
	}
	/* Sort samples and return median */
	for (int i = 0; i < SAMPLES_FILTERED_ADC - 1; i++) {
		for (int j = 0; j < SAMPLES_FILTERED_ADC - i - 1; j++) {
			if (sample_buffer[j] > sample_buffer[j + 1]) {
				// Swap elements
				int16_t temp = sample_buffer[j];
				sample_buffer[j] = sample_buffer[j + 1];
				sample_buffer[j + 1] = temp;
			}
		}
	}
	return sample_buffer[SAMPLES_FILTERED_ADC/2];
}

int adc_get_channel(int channel)
{
	int16_t sample_buffer[1];
	int err = 0;
	struct adc_sequence sequence = {
		.buffer      = sample_buffer,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(sample_buffer),
	};

	if (channel >= 0 && channel < ADC_NUM_CHANNELS) {
		(void)adc_sequence_init_dt(&adc_channels[channel], &sequence);

		err = adc_read(adc_channels[channel].dev, &sequence);
		if (err < 0) {
			LOG_ERR("Could not read (%d)", err);
			return -1;
		} else {
			return sample_buffer[0];
		}
	}
	return -1;
}

int adc_get_raw_to_millivolts(int channel, int* raw) {
	return adc_raw_to_millivolts_dt(&adc_channels[channel], raw);
}

int adc_get_full_scale_voltage_mv(int channel)
{
	if (channel >= 0 && channel < ADC_NUM_CHANNELS) {
		const struct adc_dt_spec *chan = &adc_channels[channel];
		int32_t inv_gain = 1;
		
		adc_gain_invert(chan->channel_cfg.gain, &inv_gain);

		return adc_ref_internal(chan->dev) * inv_gain;
	}
	return 0;
}

int adc_get_full_scale_count(int channel)
{
	if (channel >= 0 && channel < ADC_NUM_CHANNELS) {
		const struct adc_dt_spec *chan = &adc_channels[channel];
		return (1 << chan->resolution) - 1;
	}
	return 0;
}

int adc_set_start_calibration(int channel) {
	int16_t sample_buffer[1];
	int err = 0;
	struct adc_sequence sequence = {
		.buffer      = sample_buffer,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(sample_buffer),
		.calibrate = true
	};

	if (channel >= 0 && channel < ADC_NUM_CHANNELS) {
		(void)adc_sequence_init_dt(&adc_channels[channel], &sequence);

		err = adc_read(adc_channels[channel].dev, &sequence);
		if (err < 0) {
			LOG_ERR("Could not read (%d)", err);
			return -1;
		} else {
			return sample_buffer[0];
		}
	}
	return -1;
}