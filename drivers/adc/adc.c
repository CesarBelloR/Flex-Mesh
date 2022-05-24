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
static struct k_work_delayable adc_sample_work;
static int32_t adc_vref;
static int16_t adc_sample_buffer[ADC_NUM_CHANNELS];
static int adc_ready;

struct adc_channel_cfg channel_cfg = {
	.gain = ADC_GAIN,
	.reference = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	/* channel ID will be overwritten below */
	.channel_id = 0,
	.differential = 0
};

struct adc_sequence sequence = {
	.channels    = 0,
	.buffer      = adc_sample_buffer,
	.buffer_size = sizeof(adc_sample_buffer),
	.resolution  = ADC_RESOLUTION,
};

static void adc_sample_work_fn(struct k_work *work) 
{
	int err = adc_read(adc_dev, &sequence);
	if (err != 0)
	{
		LOG_ERR("ADC reading failed with error %d.", err);
		k_work_cancel_delayable(&adc_sample_work);
		adc_ready = -1;
		return;
	}
	LOG_DBG("Sample ADC done");
	adc_ready = 0;
	k_work_schedule(&adc_sample_work, K_MSEC(CONFIG_ADC_MODULES_SAMPLE_RATE_MSEC));
}

int adc_init(void)
{
	adc_ready = 0;
	adc_dev = DEVICE_DT_GET(ADC_NODE);
	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC device not found");
		return -EINVAL;
	}

	/*
	 * Configure channels individually prior to sampling
	 */
	for (uint8_t i = 0; i < ADC_NUM_CHANNELS; i++) {
		channel_cfg.channel_id = channel_ids[i];
#ifdef CONFIG_ADC_NRFX_SAADC
		channel_cfg.input_positive = SAADC_CH_PSELP_PSELP_AnalogInput0
					     + channel_ids[i];
#endif

		adc_channel_setup(adc_dev, &channel_cfg);
		sequence.channels |= BIT(channel_ids[i]);
	}

	adc_vref = adc_ref_internal(adc_dev);
	k_work_init_delayable(&adc_sample_work, adc_sample_work_fn);
	k_work_schedule(&adc_sample_work, K_NO_WAIT);
	adc_ready = -1;
    return 0;
}

int adc_get_channel(int channel)
{
	if (adc_ready == 0 && (channel >=1 && channel <= 6)) {
		return adc_sample_buffer[channel + 1]; 
	}
	return -1;
}