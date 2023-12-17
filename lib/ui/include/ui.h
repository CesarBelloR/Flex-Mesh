#ifndef UI_H__
#define UI_H__

#include <zephyr/kernel.h>
#include <dk_buttons_and_leds.h>

#include "led_effect.h"
#include "autoconf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_LED_1			1
#define UI_LED_2			2
#define UI_LED_3			3
#define UI_LED_4			4

#define UI_LED_ON(x)			(x)
#define UI_LED_BLINK(x)			((x) << 8)
#define UI_LED_GET_ON(x)		((x) & 0xFF)
#define UI_LED_GET_BLINK(x)		(((x) >> 8) & 0xFF)

#ifdef CONFIG_UI_LED_USE_PWM

#define UI_LED_ON_PERIOD_NORMAL		1000
#define UI_LED_OFF_PERIOD_NORMAL	1000
#define UI_LED_ON_PERIOD_ERROR		500
#define UI_LED_OFF_PERIOD_ERROR		500

#define UI_LED_BLINK_SHORT_PERIOD 2000
#define UI_LED_BLINK_NORMAL_PERIOD 1000
#define UI_LED_BLINK_FAST_PERIOD 100

#define UI_LED_MAX			255

#define UI_LED_COLOR_OFF		LED_COLOR(0, 0, 0)
#define UI_LED_COLOR_RED		LED_COLOR(255, 0, 0)
#define UI_LED_COLOR_GREEN		LED_COLOR(0, 255, 0)
#define UI_LED_COLOR_BLUE		LED_COLOR(0, 0, 255)
#define UI_LED_COLOR_WHITE		LED_COLOR(255, 255, 255)
#define UI_LED_COLOR_YELLOW		LED_COLOR(241, 196, 15)
#define UI_LED_COLOR_LIGHT_BLUE LED_COLOR(41, 128, 185)
#define UI_LED_COLOR_PINK 		LED_COLOR(200, 15, 152)
#define UI_LED_COLOR_MAGENTA 	LED_COLOR(137, 0, 137)
#define UI_LED_COLOR_ORANGE		LED_COLOR(219, 106, 0)
#define UI_LED_COLOR_GREEN_WHITE LED_COLOR(26, 188, 156)
#define UI_LED_COLOR_CYAN		LED_COLOR(8, 165, 167)

#endif /* CONFIG_UI_LED_USE_PWM */

/**@brief UI LED state pattern definitions. */
enum ui_led_pattern {
#ifdef CONFIG_UI_LED_USE_PWM
	UI_BATTERY_FULL,
	UI_BATTERY_MED,
	UI_BATTERY_EMPTY,
	UI_CHARGE_BATTERY_FULL,
	UI_CHARGE_BATTERY_IN_CHARING,
	UI_CHARGE_BATTERY_ERROR,
	UI_SENSOR_AQUIRING,
	UI_LORA_LISTEN,
	UI_LORA_SEND,
	UI_LORA_NACK,
	UI_LORA_ERROR,
	UI_LTE_DISCONNECTED,
	UI_LTE_CONNECTING,
	UI_LTE_CONNECTED,
	UI_LTE_ERROR,
	UI_CLOUD_DISCONNECTED,
	UI_CLOUD_CONNECTING,
	UI_CLOUD_CONNECTED,
	UI_CLOUD_ERROR,
	UI_FOTA_DOWNLOADING,
	UI_FOTA_INSTALLING,
	UI_FOTA_ERROR,
	UI_UNKNOWN_ERROR,
	UI_MODEM_RECOVERABLE_ERROR,
	UI_MODEM_IRRECOVERABLE_ERROR,
	UI_TURN_OFF,
#endif
};

/**
 * @brief Initializes the user interface module.
 *
 * @return 0 on success or negative error value on failure.
 */
int ui_init(void);

/**
 * @brief Sets the LED pattern.
 *
 * @param pattern LED pattern.
 */
void ui_led_set_pattern(enum ui_led_pattern pattern);

/**
 * @brief Sets a LED's state. Only the one LED is affected, the rest of the
 *	  LED pattern is preserved. Only has effect if the specified LED is not
 *	  controlled by PWM.
 *
 * @param led LED number to be controlled.
 * @param value 0 turns the LED off, a non-zero value turns the LED on.
 */
void ui_led_set_state(uint32_t led, uint8_t value);

/**
 * @brief Gets the LED pattern.
 *
 * @return Current LED pattern.
 */
enum ui_led_pattern ui_led_get_pattern(void);

/**
 * @brief Sets the LED RGB color.
 *
 * @param red Red, in range 0 - 255.
 * @param green Green, in range 0 - 255.
 * @param blue Blue, in range 0 - 255.
 *
 * @return 0 on success or negative error value on failure.
 */
int ui_led_set_color(uint8_t red, uint8_t green, uint8_t blue);

#ifdef __cplusplus
}
#endif

#endif /* UI_H__ */
