#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/sys/slist.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_interface, CONFIG_ETC_INTERFACE_LOG_LEVEL);

#include "etc_interface.h"

#define ETC_INTERFACE_STACK_SIZE 512

static const struct gpio_dt_spec rtc_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(rtc_int), control_gpios, 0);
static const struct gpio_dt_spec hall_sensor_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(hall_int), control_gpios, 0);
static struct gpio_callback hall_sensor_callback;
static struct gpio_callback rtc_int_callback;

struct etc_interface_event_callback {
	sys_snode_t node;
	etc_interface_event_handler handler;
};

static enum etc_interface_event_type event_type = ETC_INTERFACE_EVENT_UNKNOWN;
static sys_slist_t etc_interface_callback_list = SYS_SLIST_STATIC_INIT(&etc_interface_callback_list);
static void etc_interface_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(etc_interface_work, etc_interface_work_handler);

static void hall_sensor_callback_handler(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	event_type = ETC_INTERFACE_EVENT_HALL;
	k_work_reschedule(&etc_interface_work, K_SECONDS(1));
}

static void rtc_int_callback_handler(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	event_type = ETC_INTERFACE_EVENT_RTC;
	k_work_reschedule(&etc_interface_work, K_SECONDS(1));
}

static int etc_interface_init(const struct device *unused)
{
	ARG_UNUSED(unused);

	if (!device_is_ready(hall_sensor_dt.port)) {
		LOG_ERR("HALL sensor device not ready");
		return -EINVAL;
	}

	if (!device_is_ready(rtc_dt.port)) {
		LOG_ERR("The RTC interrupt not ready");
		return -EINVAL;
	}

	LOG_INF("Initialized the ETC Interface successfully");

	gpio_pin_configure_dt(&hall_sensor_dt, GPIO_INPUT | GPIO_PULL_UP);
    	gpio_pin_interrupt_configure_dt(&hall_sensor_dt, GPIO_INT_LEVEL_LOW);
	gpio_init_callback(&hall_sensor_callback, hall_sensor_callback_handler, BIT(hall_sensor_dt.pin));
	gpio_add_callback(hall_sensor_dt.port, &hall_sensor_callback);

	return 0;
}

static void etc_interface_work_handler(struct k_work *work)
{
	if (sys_slist_is_empty(&etc_interface_callback_list)) {
		return;
	}

	struct etc_interface_event_callback* cb;

	SYS_SLIST_FOR_EACH_CONTAINER(&etc_interface_callback_list, cb, node)
	{
		if (cb->handler)
		{
			cb->handler(event_type);
		}
	}
}

SYS_INIT(etc_interface_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

void etc_interface_register_event_handler(etc_interface_event_handler handler)
{
	struct etc_interface_event_callback *callback = (struct etc_interface_event_callback *)k_malloc(sizeof(struct etc_interface_event_callback));
	if (callback == NULL) {
		LOG_ERR("No enough buffer to allocate for callback");
		return;
	}
	callback->handler = handler;
	sys_slist_append(&etc_interface_callback_list, &callback->node);
}

void etc_interface_enable_rtc_event(void) 
{
	gpio_pin_configure_dt(&rtc_dt, GPIO_INPUT | GPIO_PULL_UP);
    	gpio_pin_interrupt_configure_dt(&rtc_dt, GPIO_INT_EDGE_FALLING);
	gpio_init_callback(&rtc_int_callback, rtc_int_callback_handler, BIT(rtc_dt.pin));
	gpio_add_callback(rtc_dt.port, &rtc_int_callback);
}

void etc_interface_disable_rtc_event(void) 
{
	gpio_pin_interrupt_configure_dt(&rtc_dt, GPIO_INT_DISABLE);
	gpio_remove_callback(rtc_dt.port, &rtc_int_callback);
}