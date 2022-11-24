#include <zephyr.h>
#include <drivers/gpio.h>
#include <device.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(etc_interface, CONFIG_ETC_INTERFACE_LOG_LEVEL);

#include "etc_interface.h"

#define ETC_INTERFACE_USER_BUTTON_PIN (5)
#define ETC_INTERFACE_HALL_SENSOR_PIN (28)
#define ETC_INTERFACE_STACK_SIZE 512
static const struct device* user_btn_dev = NULL;
static const struct device* hall_sensor_dev = NULL;

static struct gpio_callback user_btn_callback;
static struct gpio_callback hall_sensor_callback;

static etc_interface_event_handler etc_interface_handler = NULL;

static void etc_interface_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(etc_interface_work, etc_interface_work_handler);

static void user_btn_callback_handler(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	k_work_reschedule(&etc_interface_work, K_SECONDS(1));
}

static void hall_sensor_callback_handler(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	k_work_reschedule(&etc_interface_work, K_SECONDS(1));
}

static int etc_interface_init(const struct device *unused)
{
	ARG_UNUSED(unused);
	user_btn_dev = device_get_binding("GPIO_1");
	
	if (!device_is_ready(user_btn_dev)) {
		LOG_ERR("User button device not ready");
		return -EINVAL;;
	}

	hall_sensor_dev = device_get_binding("GPIO_0");
	if (!device_is_ready(hall_sensor_dev)) {
		LOG_ERR("HALL sensor device not ready");
		return -EINVAL;;
	}

	LOG_INF("Initialized the ETC Interface successfully");

	gpio_pin_configure(user_btn_dev, ETC_INTERFACE_USER_BUTTON_PIN, GPIO_INPUT | GPIO_PULL_UP);
    	gpio_pin_interrupt_configure(user_btn_dev, ETC_INTERFACE_USER_BUTTON_PIN, GPIO_INT_LEVEL_LOW);
	gpio_init_callback(&user_btn_callback, user_btn_callback_handler, BIT(ETC_INTERFACE_USER_BUTTON_PIN));
	gpio_add_callback(user_btn_dev, &user_btn_callback);

	gpio_pin_configure(hall_sensor_dev, ETC_INTERFACE_HALL_SENSOR_PIN, GPIO_INPUT | GPIO_PULL_UP);
    	gpio_pin_interrupt_configure(hall_sensor_dev, ETC_INTERFACE_HALL_SENSOR_PIN, GPIO_INT_LEVEL_LOW);
	gpio_init_callback(&hall_sensor_callback, hall_sensor_callback_handler, BIT(ETC_INTERFACE_HALL_SENSOR_PIN));
	gpio_add_callback(hall_sensor_dev, &hall_sensor_callback);

	return 0;
}

static void etc_interface_work_handler(struct k_work *work) {
	if (etc_interface_handler) {
		etc_interface_handler();
	}
}

SYS_INIT(etc_interface_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

void etc_interface_register_event_handler(etc_interface_event_handler handler) {
	etc_interface_handler = handler;
}