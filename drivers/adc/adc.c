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
#include <device.h>
#include <kernel.h>
#include <logging/log.h>
#include <sys/util.h>
#include <hal/nrf_saadc.h>
#include <drivers/adc.h>
#include <drivers/gpio.h>

LOG_MODULE_REGISTER(ETC_ADC, CONFIG_ADC_MODULES_LOG_LEVEL);

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define ADC_NUM_CHANNELS    	DT_PROP_LEN(DT_PATH(zephyr_user), io_channels)
#define ADC_NODE				DT_PHANDLE(DT_PATH(zephyr_user), io_channels)
#define ADC_RESOLUTION		    12
#define ADC_OVERSAMPLING	    8
#define ADC_GAIN		        ADC_GAIN_1
#define ADC_REFERENCE		    ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME	ADC_ACQ_TIME_DEFAULT

/* Get the numbers of up to two channels */
static uint8_t channel_ids[ADC_NUM_CHANNELS] = {
	DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 0),
	DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 1),
	DT_IO_CHANNELS_INPUT_BY_IDX(DT_PATH(zephyr_user), 2)
};

static const struct device *adc_dev = NULL;
static int32_t adc_vref;
static int16_t adc_sample_buffer[ADC_NUM_CHANNELS];

struct adc_channel_cfg base_cfg = { 
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	/* channel ID will be overwritten below */
	.channel_id = 0,
	.differential = 0
};

struct adc_sequence base_seq = { 
	.channels    = 0,
	.buffer_size = sizeof(int16_t),
	.resolution  = ADC_RESOLUTION,
	.oversampling	= ADC_OVERSAMPLING,
};

struct adc_channel_cfg channel_cfg[ADC_NUM_CHANNELS];
struct adc_sequence sequence[ADC_NUM_CHANNELS];

int adc_init(void)
{
	adc_dev = DEVICE_DT_GET(ADC_NODE);
	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC device not found");
		return -EINVAL;
	}

	/*
	 * Configure channels individually prior to sampling
	 */
	for (uint8_t i = 0; i < ADC_NUM_CHANNELS; i++) {
		channel_cfg[i] = base_cfg;
		sequence[i] = base_seq;
		channel_cfg[i].channel_id = channel_ids[i];
#ifdef CONFIG_ADC_NRFX_SAADC
		channel_cfg[i].input_positive = SAADC_CH_PSELP_PSELP_AnalogInput0
					     + channel_ids[i];
#endif
		adc_channel_setup(adc_dev, &channel_cfg[i]);
		sequence[i].channels |= BIT(channel_ids[i]);
		sequence[i].buffer = &adc_sample_buffer[i];
	}

	adc_vref = adc_ref_internal(adc_dev);
    return 0;
}

int adc_get_channel(int channel)
{
	if (channel >= 0 && channel <= ADC_NUM_CHANNELS) {
		int err = adc_read(adc_dev, &sequence[channel]);
		if (err) {
			return -1;
		} else  {
			return adc_sample_buffer[channel];
		}
	}
	return -1;
}