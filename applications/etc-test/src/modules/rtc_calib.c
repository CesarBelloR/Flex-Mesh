/* 
 * Copyright (c) 2023 EXACT Technology
 */
#include <nrfx_timer.h>
#include <nrfx_egu.h>
#include <nrfx_ppi.h>
#include <nrfx_gpiote.h>

#include <zephyr/kernel.h>

#include <stdint.h>
#include <math.h>

#include "pcf85263a.h"
#include "etc_device.h"
#include "rtc_calib.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(rtc_calib, CONFIG_ETC_TEST_LOG_LEVEL);

#define RTC_FREQUENCY_HZ 32768
#define REF_FREQUENCY_HZ 2500000

/* Use timer 1 and 2. Timer 0 is used by MPSL Init */
#define TIMER_RTC_COUNT_INST_IDX 1
#define TIMER_REF_COUNT_INST_IDX 2

#define COUNT_MULTIPLIER 4

#define TIMER_RTC_COUNT_START_VAL 1
#define TIMER_RTC_COUNT_MAX_VAL   (TIMER_RTC_COUNT_START_VAL + COUNT_MULTIPLIER * RTC_FREQUENCY_HZ)

#define EGU_INST_IDX 0

#define EGU_START_TASK_CH 0
#define EGU_STOP_TASK_CH  1

#define RTC_PIN 3
#define RTC_PIN_PORT 0

#define DEV_PIN 8
#define DEV_PIN_PORT 1

#define RTC_MAX_OFFSET_PPM  250.0f
#define RTC_MIN_OFFSET_PPM -250.0f

nrfx_timer_t timer_rtc_inst = NRFX_TIMER_INSTANCE(TIMER_RTC_COUNT_INST_IDX);
nrfx_timer_t timer_ref_inst = NRFX_TIMER_INSTANCE(TIMER_REF_COUNT_INST_IDX);
nrfx_gpiote_t gpiote_inst = NRFX_GPIOTE_INSTANCE(0);

nrfx_egu_t egu_inst = NRFX_EGU_INSTANCE(EGU_INST_IDX);

const nrfx_gpiote_pin_t rtc_pin = NRF_GPIO_PIN_MAP(RTC_PIN_PORT, RTC_PIN);
const nrfx_gpiote_pin_t dev_pin = NRF_GPIO_PIN_MAP(DEV_PIN_PORT, DEV_PIN);

K_SEM_DEFINE(timer_sem, 0, 1);

static void egu_handler(uint8_t event_idx, void * p_context)
{
}

static void timer_handler(nrf_timer_event_t event_type, void * p_context)
{
	nrfx_timer_t * timer_inst = p_context;
	char *timer_name = "";

	if (timer_inst->instance_id == timer_rtc_inst.instance_id) {
		timer_name = "RTC";
		if (event_type == NRF_TIMER_EVENT_COMPARE1) {
			nrfx_timer_capture(timer_inst, NRF_TIMER_CC_CHANNEL0);
			k_sem_give(&timer_sem);
		}
	} else if (timer_inst->instance_id == timer_ref_inst.instance_id) {
		timer_name = "Ref";
	}
}

static void gpiote_handler(nrfx_gpiote_pin_t pin, nrfx_gpiote_trigger_t trigger,
			   void *p_context)
{
	if (pin == rtc_pin) {
		LOG_INF("RTC Int");
	} else if (pin == dev_pin) {
		LOG_INF("Dev int");
	}
}

static void assign_egu_evt_to_timer_task(nrf_egu_event_t egu_evt,
					 nrf_timer_task_t timer_task)
{	
	nrfx_err_t status;
	nrf_ppi_channel_t ppi_channel;

	status = nrfx_ppi_channel_alloc(&ppi_channel);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);

	nrfx_ppi_channel_assign(ppi_channel,
		nrf_egu_event_address_get(egu_inst.p_reg, egu_evt),
		nrf_timer_task_address_get(timer_rtc_inst.p_reg,
					   timer_task));
	nrfx_ppi_channel_fork_assign(ppi_channel, 
		nrf_timer_task_address_get(timer_ref_inst.p_reg,
					   timer_task));
	status = nrfx_ppi_channel_enable(ppi_channel);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);
}

static void gpiote_init(void)
{
	nrfx_err_t status;
	uint8_t rtc_in_channel;
	uint8_t ref_in_channel;
	status = nrfx_gpiote_channel_alloc(&gpiote_inst, &rtc_in_channel);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);
	status = nrfx_gpiote_channel_alloc(&gpiote_inst, &ref_in_channel);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);

	/* Use high to low edge trigger due to RTC /INTA (CLK) pin 
	 * being open drain. Rising edge will have a delay due to low 13k
	 * internal pull up.
	 */
	nrf_gpio_pin_pull_t pull_cfg = NRF_GPIO_PIN_PULLUP;
	nrfx_gpiote_trigger_config_t trigger_cfg = {
		.trigger = NRFX_GPIOTE_TRIGGER_HITOLO
	};
	nrfx_gpiote_handler_config_t handler_cfg = {
		.handler = gpiote_handler,
		.p_context = NULL
	};
	nrfx_gpiote_input_pin_config_t input_config = {
		.p_pull_config = &pull_cfg,
		.p_trigger_config = &trigger_cfg,
		.p_handler_config = &handler_cfg
	};

	trigger_cfg.p_in_channel = &rtc_in_channel;
	status = nrfx_gpiote_input_configure(&gpiote_inst, rtc_pin,
					     &input_config);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);
	
	pull_cfg = NRF_GPIO_PIN_NOPULL;
	trigger_cfg.p_in_channel = &ref_in_channel;
	status = nrfx_gpiote_input_configure(&gpiote_inst, dev_pin,
				    	     &input_config);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);
	
	nrfx_gpiote_trigger_enable(&gpiote_inst, rtc_pin, false);
	nrfx_gpiote_trigger_enable(&gpiote_inst, dev_pin, false);

#if defined(__ZEPHYR__)
	IRQ_DIRECT_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_GPIOTE),
			   IRQ_PRIO_LOWEST, nrfx_gpiote_0_irq_handler, 0);
#endif
}

static void assign_pin_to_timer_count(nrfx_gpiote_pin_t pin, nrfx_timer_t *timer)
{
	nrfx_err_t status;
	nrf_ppi_channel_t ppi_channel;

	status = nrfx_ppi_channel_alloc(&ppi_channel);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);

	status = nrfx_ppi_channel_assign(ppi_channel,
		nrfx_gpiote_in_event_address_get(&gpiote_inst, pin),
		nrf_timer_task_address_get(timer->p_reg,
					   NRF_TIMER_TASK_COUNT));
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);
	status = nrfx_ppi_channel_enable(ppi_channel);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);
}

static void ppi_init(void)
{
	nrfx_err_t status;
	nrf_ppi_channel_t ppi_channel;

	/* Connect events to PPI */
	/* Connect timer start and stop tasks to EGU*/
	assign_egu_evt_to_timer_task(NRF_EGU_EVENT_TRIGGERED0, NRF_TIMER_TASK_START);
	assign_egu_evt_to_timer_task(NRF_EGU_EVENT_TRIGGERED1, NRF_TIMER_TASK_STOP);

	/* Connect RTC timer compare event to Ref timer capture task */
	status = nrfx_ppi_channel_alloc(&ppi_channel);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);

	nrfx_ppi_channel_assign(ppi_channel,
		nrf_timer_event_address_get(timer_rtc_inst.p_reg,
					   NRF_TIMER_EVENT_COMPARE1),
		nrf_timer_task_address_get(timer_ref_inst.p_reg,
					   NRF_TIMER_TASK_CAPTURE0));
	nrfx_ppi_channel_enable(ppi_channel);

	status = nrfx_ppi_channel_alloc(&ppi_channel);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);
	nrfx_ppi_channel_assign(ppi_channel,
		nrf_timer_event_address_get(timer_rtc_inst.p_reg,
					   NRF_TIMER_EVENT_COMPARE0),
		nrf_timer_task_address_get(timer_ref_inst.p_reg,
					   NRF_TIMER_TASK_START));
	nrfx_ppi_channel_enable(ppi_channel); 

	/* Connect GPIOTE to Timer to enable counting pulses */
	assign_pin_to_timer_count(rtc_pin, &timer_rtc_inst);
	assign_pin_to_timer_count(dev_pin, &timer_ref_inst);
}

static void setup_rtc(void)
{
	/* Disable interrupt on /INTA and enable CLK output. */
	pcf85263a_set_interrupt_io(false);
}

static void timer_init(void)
{
	nrfx_err_t status;

	nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(NRF_TIMER_FREQ_16MHz);
	config.mode = NRF_TIMER_MODE_COUNTER;
	config.bit_width = NRF_TIMER_BIT_WIDTH_32;
	config.p_context = &timer_rtc_inst;
	status = nrfx_timer_init(&timer_rtc_inst, &config, timer_handler);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);

	config.p_context = &timer_ref_inst;
	status = nrfx_timer_init(&timer_ref_inst, &config, timer_handler);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);

	/* RTC CLKOUT counter to trigger a compare event when it 
	 * has reached TIMER_RTC_COUNT_START_VAL. This event triggers the start
	 * task on the ref timer.
	 */
	nrfx_timer_compare(&timer_rtc_inst, NRF_TIMER_CC_CHANNEL0, 
			   TIMER_RTC_COUNT_START_VAL, true);
	/* Set the RTC CLKOUT counter to trigger a compare event when it 
	 * has reached TIMER_RTC_COUNT_MAX_VAL. This triggers a capture
	 * task on the ref timer.
	 */
	nrfx_timer_compare(&timer_rtc_inst, NRF_TIMER_CC_CHANNEL1, 
			   TIMER_RTC_COUNT_MAX_VAL, true);

#if defined(__ZEPHYR__)
	IRQ_DIRECT_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_TIMER_INST_GET(TIMER_RTC_COUNT_INST_IDX)),
			   IRQ_PRIO_LOWEST, NRFX_TIMER_INST_HANDLER_GET(TIMER_RTC_COUNT_INST_IDX), 0);
	IRQ_DIRECT_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_TIMER_INST_GET(TIMER_REF_COUNT_INST_IDX)),
			   IRQ_PRIO_LOWEST, NRFX_TIMER_INST_HANDLER_GET(TIMER_REF_COUNT_INST_IDX), 0);
#endif
}

static void egu_init(void)
{
	nrfx_err_t status;
	/* Set up software interrupts through EGU */
	status = nrfx_egu_init(&egu_inst, NRFX_EGU_DEFAULT_CONFIG_IRQ_PRIORITY, 
			       egu_handler, NULL);
	__ASSERT_NO_MSG(status == NRFX_SUCCESS);
}

static float calculate_offset_ppm(uint32_t ref_count)
{
	float time_per_cycle_s;
	float diff_per_cycle_s;
	float deviation_ppm;

	time_per_cycle_s = (float)ref_count / (float)REF_FREQUENCY_HZ / (float)COUNT_MULTIPLIER / 
			   (float)RTC_FREQUENCY_HZ;
	diff_per_cycle_s = 1.0f / (float)RTC_FREQUENCY_HZ - time_per_cycle_s;
	deviation_ppm = powf(10.0f, 6.0f) * diff_per_cycle_s / time_per_cycle_s;

	LOG_INF("RTC offset: %.2f ppm", deviation_ppm);

	return deviation_ppm;
}

float get_rtc_offset(void)
{
	int ret;
	float offset_ppm;

	ret = etc_device_read_setting(ETC_RTC_CALIBRATION_OFFSET_PPM, 
				       &offset_ppm,
				       sizeof(offset_ppm));
	if (ret != 0) {
		return 0.0f;
	}
	
	return offset_ppm;
}

int set_rtc_offset(float *offset_ppm)
{
	int ret;

	if (*offset_ppm > RTC_MAX_OFFSET_PPM ||
	    *offset_ppm < RTC_MIN_OFFSET_PPM) {
		LOG_ERR("Offset out of range");
		return -1;
	}

	ret = pcf85263a_set_offset(offset_ppm);
	if (ret < 0) {
		LOG_ERR("Setting offset");
		return ret;
	}

	ret = etc_device_write_setting(ETC_RTC_CALIBRATION_OFFSET_PPM, 
				       offset_ppm,
				       sizeof(*offset_ppm));
	if (ret != 0) {
		LOG_ERR("Writing offset to flash");
		return ret;
	}

	return 0;
}

void rtc_calib_init(void)
{
	int ret;

	/* Read RTC calibration offset and set RTC offset register if available. */
	float offset_ppm;
	ret = etc_device_read_setting(ETC_RTC_CALIBRATION_OFFSET_PPM,
				      &offset_ppm, sizeof(offset_ppm));
	if (ret == 0) {
		ret = pcf85263a_set_offset(&offset_ppm);
		if (ret != 0) {
			LOG_ERR("Setting RTC offset");
		} else {
			LOG_INF("RTC offset set to %.2f ppm", offset_ppm);
		}
	} else {
		LOG_WRN("No RTC calibration available");
	}
}

int calibrate_rtc(void)
{
	static bool initialized = false;
	int ret;
	uint32_t ref_ticks;
	uint32_t rtc_ticks;
	float offset_ppm;

	if (!initialized) {
		initialized = true;
		setup_rtc();
		timer_init();
		egu_init();

		gpiote_init();
		ppi_init();
	} else {
		nrfx_timer_uninit(&timer_rtc_inst);
		nrfx_timer_uninit(&timer_ref_inst);
		timer_init();
	}

	k_sem_reset(&timer_sem);
	nrfx_timer_enable(&timer_rtc_inst);

	/* Wait for RTC timer to reach configured count */
	ret = k_sem_take(&timer_sem, K_SECONDS(5));
	if (ret != 0) {
		LOG_WRN("Timer timeout");
		return ret;
	}
	nrfx_timer_disable(&timer_rtc_inst);
	nrfx_timer_disable(&timer_ref_inst);
	ref_ticks = nrfx_timer_capture_get(&timer_ref_inst, NRF_TIMER_CC_CHANNEL0);
	rtc_ticks = nrfx_timer_capture_get(&timer_rtc_inst, NRF_TIMER_CC_CHANNEL0);
	LOG_DBG("RTC: %u, Ref: %u", rtc_ticks, ref_ticks);
	
	offset_ppm = calculate_offset_ppm(ref_ticks);
	return set_rtc_offset(&offset_ppm);
}