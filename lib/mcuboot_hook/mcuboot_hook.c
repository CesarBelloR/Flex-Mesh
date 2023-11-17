#include <stdint.h>
#include "bootutil/image.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/boot_hooks.h"
#include "bootutil/mcuboot_status.h"

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mcuboot_hook, LOG_LEVEL_INF);

#define LED_COLOR(_r, _g, _b) {		\
		.c = {_r, _g, _b}	\
}

#define UI_LED_MAX			50

#define UI_LED_COLOR_OFF		LED_COLOR(0, 0, 0)
#define UI_LED_COLOR_PURPLE		LED_COLOR(UI_LED_MAX, 0, UI_LED_MAX)
#define UI_LED_COLOR_RED		LED_COLOR(UI_LED_MAX, 0, 0)

#define UI_UPDATE_IN_PROGRESS_COLOR	UI_LED_COLOR_PURPLE
#define UI_ERROR_COLOR			UI_LED_COLOR_RED

static const struct pwm_dt_spec pwm_led0 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));
static const struct pwm_dt_spec pwm_led1 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1));
static const struct pwm_dt_spec pwm_led2 = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2));

struct led_color {
	uint8_t c[3];
};

struct led {
	size_t id;
	struct led_color color;
};

static struct led led;

static void pwm_out(struct led *led, struct led_color *color)
{
	pwm_set_dt(&pwm_led0, PWM_USEC(1000000 / CONFIG_MCUBOOT_PWM_FREQUENCY), PWM_USEC(color->c[0]));
	pwm_set_dt(&pwm_led1, PWM_USEC(1000000 / CONFIG_MCUBOOT_PWM_FREQUENCY), PWM_USEC(color->c[1]));
	pwm_set_dt(&pwm_led2, PWM_USEC(1000000 / CONFIG_MCUBOOT_PWM_FREQUENCY), PWM_USEC(color->c[2]));
}

static void pwm_off(struct led *led)
{
	struct led_color nocolor = {0};

	pwm_out(led, &nocolor);
}

void mcuboot_status_change(mcuboot_status_type_t status) 
{
	if (status == MCUBOOT_STATUS_UPGRADING) {
		struct led_color new_color = UI_UPDATE_IN_PROGRESS_COLOR;
		pwm_out(&led, &new_color);
	} else if (status == MCUBOOT_STATUS_BOOT_FAILED) {
		struct led_color new_color = UI_ERROR_COLOR;
		pwm_out(&led, &new_color);
	} else {
		pwm_off(&led);
	}
}