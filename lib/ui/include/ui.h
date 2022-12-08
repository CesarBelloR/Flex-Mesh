#ifndef UI_H__
#define UI_H__

#include <zephyr.h>
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

#define UI_LED_MAX			50

#define UI_LED_COLOR_OFF		LED_COLOR(0, 0, 0)
#define UI_LED_COLOR_RED		LED_COLOR(UI_LED_MAX, 0, 0)
#define UI_LED_COLOR_GREEN		LED_COLOR(0, UI_LED_MAX, 0)
#define UI_LED_COLOR_BLUE		LED_COLOR(0, 0, UI_LED_MAX)
#define UI_LED_COLOR_WHITE		LED_COLOR(UI_LED_MAX, UI_LED_MAX,      \
						  UI_LED_MAX)
#define UI_LED_COLOR_YELLOW		LED_COLOR(UI_LED_MAX, UI_LED_MAX, 0)
#define UI_LED_COLOR_CYAN		LED_COLOR(0, UI_LED_MAX, UI_LED_MAX)
#define UI_LED_COLOR_PURPLE		LED_COLOR(UI_LED_MAX, 0, UI_LED_MAX)

#define UI_LTE_DISCONNECTED_COLOR	UI_LED_COLOR_YELLOW
#define UI_LTE_CONNECTING_COLOR		UI_LED_COLOR_WHITE
#define UI_LTE_CONNECTED_COLOR		UI_LED_COLOR_CYAN
#define UI_CLOUD_PUBLISHING_COLOR       UI_LED_COLOR_GREEN
#define UI_CLOUD_CONNECTING_COLOR	UI_LED_COLOR_CYAN
#define UI_CLOUD_CONNECTED_COLOR	UI_LED_COLOR_BLUE
#define UI_CLOUD_PAIRING_COLOR		UI_LED_COLOR_YELLOW
#define UI_LED_ERROR_CLOUD_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_MODEM_REC_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_MODEM_IRREC_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_LTE_LC_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_UNKNOWN_COLOR	UI_LED_COLOR_OFF
#define UI_LED_AQUIRING_SENSOR_COLOR    UI_LED_COLOR_BLUE

#endif /* CONFIG_UI_LED_USE_PWM */

/**@brief UI LED state pattern definitions. */
enum ui_led_pattern {
#ifdef CONFIG_UI_LED_USE_PWM
	UI_LTE_DISCONNECTED,
	UI_LTE_CONNECTING,
	UI_LTE_CONNECTED,
	UI_CLOUD_PUBLISHING,
	UI_CLOUD_CONNECTING,
	UI_CLOUD_ASSOCIATING,
	UI_CLOUD_ASSOCIATED,
	UI_ERROR_CLOUD,
	UI_SENSOR_AQUIRING,
	UI_LORA_TRANSMITTING,
	UI_LORA_RECEIVING,
	UI_ACTIVE_MODE,
	UI_PASSIVE_MODE,
	UI_ERROR_SYSTEM_FAULT,
	UI_FOTA_UPDATING,
	UI_FOTA_UPDATE_REBOOT,
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
