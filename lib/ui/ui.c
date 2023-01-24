#include <zephyr/logging/log.h>
#include <stdlib.h>
#include "ui.h"
#include "led_pwm.h"

LOG_MODULE_REGISTER(ui, CONFIG_UI_LOG_LEVEL);

static enum ui_led_pattern current_led_state;

#if !defined(CONFIG_UI_LED_USE_PWM)
static struct k_work_delayable leds_update_work;

/**@brief Update LEDs state. */
static void leds_update(struct k_work *work)
{
	static bool led_on;
	static uint8_t current_led_on_mask;
	uint8_t led_on_mask;

	led_on_mask = UI_LED_GET_ON(current_led_state);
	led_on = !led_on;

	if (led_on) {
		led_on_mask |= UI_LED_GET_BLINK(current_led_state);
	} else {
		led_on_mask &= ~UI_LED_GET_BLINK(current_led_state);
	}

	if (led_on_mask != current_led_on_mask) {
		current_led_on_mask = led_on_mask;
	}

	if (work) {
		if (led_on) {
			k_work_reschedule(&leds_update_work,
					      K_MSEC(UI_LED_ON_PERIOD_NORMAL));
		} else {
			k_work_reschedule(&leds_update_work,
					      K_MSEC(UI_LED_OFF_PERIOD_NORMAL));
		}
	}
}
#endif /* CONFIG_UI_LED_USE_PWM */

void ui_led_set_pattern(enum ui_led_pattern state)
{
	current_led_state = state;
#ifdef CONFIG_UI_LED_USE_PWM
	ui_led_set_effect(state);
#endif /* CONFIG_UI_LED_USE_PWM */
}

enum ui_led_pattern ui_led_get_pattern(void)
{
	return current_led_state;
}

int ui_led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
#ifdef CONFIG_UI_LED_USE_PWM
	return ui_led_set_rgb(red, green, blue);
#else
	return -ENOTSUP;
#endif /* CONFIG_UI_LED_USE_PWM */
}

void ui_led_set_state(uint32_t led, uint8_t value)
{
#if !defined(CONFIG_UI_LED_USE_PWM)
	if (value) {
		current_led_state |= BIT(led - 1);
	} else {
		current_led_state &= ~BIT(led - 1);
	}
#endif
}

int ui_init()
{
	int err = 0;

#ifdef CONFIG_UI_LED_USE_PWM
	err = ui_leds_init();
	if (err) {
		LOG_ERR("Error when initializing PWM controlled LEDs");
		return err;
	}
	LOG_DBG("Intialized the PWM controlled LEDs successfully");
#else

	k_work_init_delayable(&leds_update_work, leds_update);
	k_work_reschedule(&leds_update_work, K_NO_WAIT);
#endif /* CONFIG_UI_LED_USE_PWM */

	return err;
}

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
static int cmd_ui_set(const struct shell *shell, size_t argc, char **argv)
{
	int pattern = atoi(argv[1]);
	shell_print(shell, "Set UI color %d", pattern);
	ui_led_set_pattern((enum ui_led_pattern)pattern);
	return 0;
}

/* Creating subcommands (level 1 command) array for command "demo". */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_ui,
	SHELL_CMD(set,   NULL, "Set the LED patten", cmd_ui_set),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(ui, &sub_ui, "ETC UI Commands", NULL);
#endif