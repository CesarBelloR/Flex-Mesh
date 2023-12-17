/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <string.h>
#include <zephyr/pm/device.h>
#include "ui.h"
#include "led_pwm.h"
#include "led_effect.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ui_led_pwm, CONFIG_UI_LOG_LEVEL);

struct led {
	const struct device *pwm_dev;

	size_t id;
	struct led_color color;
	const struct led_effect *effect;
	uint16_t effect_step;
	uint16_t effect_substep;

	struct k_work_delayable work;
	struct k_work_sync work_sync;
};

static const struct led_effect effect[] = {
	[UI_BATTERY_FULL] = LED_EFFECT_LED_ON(UI_LED_COLOR_GREEN),
	[UI_BATTERY_MED] = LED_EFFECT_LED_ON(UI_LED_COLOR_YELLOW),
	[UI_BATTERY_EMPTY] = LED_EFFECT_LED_ON(UI_LED_COLOR_RED),
	[UI_CHARGE_BATTERY_FULL] = LED_EFFECT_LED_ON(UI_LED_COLOR_GREEN),
	[UI_CHARGE_BATTERY_IN_CHARING] = LED_EFFECT_LED_ON(UI_LED_COLOR_YELLOW),
	[UI_CHARGE_BATTERY_ERROR] = LED_EFFECT_LED_BLINK(UI_LED_BLINK_NORMAL_PERIOD, UI_LED_COLOR_RED),
	[UI_SENSOR_AQUIRING] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL, UI_LED_ON_PERIOD_NORMAL, UI_LED_COLOR_ORANGE),
	[UI_LORA_LISTEN] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL, UI_LED_ON_PERIOD_NORMAL, UI_LED_COLOR_ORANGE),
	[UI_LORA_SEND] = LED_EFFECT_LED_ON(UI_LED_COLOR_ORANGE),
	[UI_LORA_NACK] = LED_EFFECT_LED_ON(UI_LED_COLOR_RED),
	[UI_LORA_ERROR] = LED_EFFECT_LED_BLINK(UI_LED_BLINK_NORMAL_PERIOD, UI_LED_COLOR_RED),
	[UI_LTE_DISCONNECTED] = LED_EFFECT_LED_OFF(),
	[UI_LTE_CONNECTING] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL, UI_LED_ON_PERIOD_NORMAL, UI_LED_COLOR_WHITE),
	[UI_LTE_CONNECTED] = LED_EFFECT_LED_ON(UI_LED_COLOR_WHITE),
	[UI_LTE_ERROR] = LED_EFFECT_LED_BLINK(UI_LED_BLINK_NORMAL_PERIOD, UI_LED_COLOR_RED),
	[UI_CLOUD_DISCONNECTED] = LED_EFFECT_LED_OFF(),
	[UI_CLOUD_CONNECTING] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL, UI_LED_ON_PERIOD_NORMAL, UI_LED_COLOR_CYAN),
	[UI_CLOUD_CONNECTED] = LED_EFFECT_LED_ON(UI_LED_COLOR_CYAN),
	[UI_CLOUD_ERROR] = LED_EFFECT_LED_BLINK(UI_LED_BLINK_NORMAL_PERIOD, UI_LED_COLOR_RED),
	[UI_FOTA_DOWNLOADING] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL, UI_LED_ON_PERIOD_NORMAL, UI_LED_COLOR_MAGENTA),
	[UI_FOTA_INSTALLING] = LED_EFFECT_LED_ON(UI_LED_COLOR_MAGENTA),
	[UI_FOTA_ERROR] = LED_EFFECT_LED_BLINK(UI_LED_BLINK_NORMAL_PERIOD, UI_LED_COLOR_RED),
	[UI_UNKNOWN_ERROR] = LED_EFFECT_LED_BLINK(UI_LED_BLINK_NORMAL_PERIOD, UI_LED_COLOR_RED),
	[UI_MODEM_RECOVERABLE_ERROR] = LED_EFFECT_LED_BLINK(UI_LED_BLINK_NORMAL_PERIOD, UI_LED_COLOR_RED),
	[UI_MODEM_IRRECOVERABLE_ERROR] = LED_EFFECT_LED_BLINK(UI_LED_BLINK_NORMAL_PERIOD, UI_LED_COLOR_RED),
	[UI_TURN_OFF] = LED_EFFECT_LED_OFF(),
};

static struct led_effect custom_effect =
	LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
		UI_LED_OFF_PERIOD_NORMAL,
		LED_NOCOLOR());

static struct led leds;
static bool led_is_ready = false;
static const struct pwm_dt_spec pwm_led0 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));
static const struct pwm_dt_spec pwm_led1 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1));
static const struct pwm_dt_spec pwm_led2 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2));

static void pwm_out(struct led *led, struct led_color *color)
{
	pwm_set_dt(&pwm_led0, PWM_USEC(UI_LED_MAX), PWM_USEC(color->c[0]));
	pwm_set_dt(&pwm_led1, PWM_USEC(UI_LED_MAX), PWM_USEC(color->c[1]));
	pwm_set_dt(&pwm_led2, PWM_USEC(UI_LED_MAX), PWM_USEC(color->c[2]));
}

static void pwm_off(struct led *led)
{
	struct led_color nocolor = {0};

	pwm_out(led, &nocolor);
}

static void work_handler(struct k_work *work)
{
	struct led *led = CONTAINER_OF(work, struct led, work);
	const struct led_effect_step *effect_step =
		&leds.effect->steps[leds.effect_step];
	int substeps_left = effect_step->substep_count - leds.effect_substep;

	for (size_t i = 0; i < ARRAY_SIZE(leds.color.c); i++) {
		int diff = (effect_step->color.c[i] - leds.color.c[i]) /
			substeps_left;
		leds.color.c[i] += diff;
	}

	pwm_out(led, &leds.color);

	leds.effect_substep++;
	if (leds.effect_substep == effect_step->substep_count) {
		leds.effect_substep = 0;
		leds.effect_step++;

		if (leds.effect_step == leds.effect->step_count) {
			if (leds.effect->loop_forever) {
				leds.effect_step = 0;
			}
		} else {
			__ASSERT_NO_MSG(leds.effect->steps[leds.effect_step].substep_count > 0);
		}
	}

	if (leds.effect_step < leds.effect->step_count) {
		int32_t next_delay =
			leds.effect->steps[leds.effect_step].substep_time;

		k_work_schedule(&leds.work, K_MSEC(next_delay));
	}
}

static void led_update(struct led *led)
{
	k_work_cancel_delayable_sync(&led->work, &led->work_sync);

	led->effect_step = 0;
	led->effect_substep = 0;

	if (!led->effect ||
	    (led->effect == &effect[UI_TURN_OFF])) {
		ui_leds_stop();
		return;
	}

	ui_leds_start();

	__ASSERT_NO_MSG(led->effect->steps);

	if (led->effect->step_count > 0) {
		int32_t next_delay =
			led->effect->steps[led->effect_step].substep_time;

		k_work_reschedule(&led->work, K_MSEC(next_delay));
	} else {
		LOG_DBG("LED effect with no effect");
	}
}

int ui_leds_init(void)
{
	int err = 0;

	if (!device_is_ready(pwm_led0.dev)) {
		LOG_ERR("Error: PWM device %s is not ready\n",
		       pwm_led0.dev->name);
		return -ENODEV;
	}

	if (!device_is_ready(pwm_led1.dev)) {
		LOG_ERR("Error: PWM device %s is not ready\n",
		       pwm_led1.dev->name);
		return -ENODEV;
	}

	if (!device_is_ready(pwm_led2.dev)) {
		LOG_ERR("Error: PWM device %s is not ready\n",
		       pwm_led2.dev->name);
		return -ENODEV;
	}

	leds.id = 0;
	leds.effect = &effect[UI_LTE_CONNECTING];

	k_work_init_delayable(&leds.work, work_handler);
	led_update(&leds);
	led_is_ready = true;
	return err;
}

void ui_leds_start(void)
{
	if (led_is_ready) return;
#if defined(CONFIG_PM_DEVICE)
	int err = pm_device_action_run(pwm_led0.dev, PM_DEVICE_ACTION_RESUME);
	if (err) {
		LOG_ERR("PWM enable failed %d", err);
	}
#endif
	LOG_DBG("PWM on");
	led_is_ready = true;
}

void ui_leds_stop(void)
{
	if (!led_is_ready) return;
	pwm_off(&leds);
	k_work_cancel_delayable_sync(&leds.work, &leds.work_sync);
#if defined(CONFIG_PM_DEVICE)
	int err = pm_device_action_run(pwm_led0.dev, PM_DEVICE_ACTION_SUSPEND);
	if (err) {
		LOG_ERR("PWM disable failed %d", err);
	}
#endif
	LOG_DBG("PWM off");
	led_is_ready = false;
}

void ui_led_set_effect(enum ui_led_pattern state)
{
	leds.effect = &effect[state];
	led_update(&leds);
}

int ui_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
	struct led_effect effect =
		LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
			UI_LED_OFF_PERIOD_NORMAL,
			LED_COLOR(red, green, blue));

	memcpy((void *)custom_effect.steps, (void *)effect.steps,
		effect.step_count * sizeof(struct led_effect_step));

	leds.effect = &custom_effect;
	led_update(&leds);

	return 0;
}
