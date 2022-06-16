#include <stdio.h>
#include <zephyr.h>
#include <device.h>
#include <init.h>
#include <pm/pm.h>
#include "retained.h"
#include <hal/nrf_gpio.h>
#include "pcf85263a.h"
#include <drivers/gpio.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define ALARM_PIN (4)

void main(void)
{
	const struct device* gpio = device_get_binding("GPIO_1");
	if (gpio == NULL) {
		LOG_ERR("Can't get GPIO_1");
	}

	gpio_pin_configure(gpio, ALARM_PIN, GPIO_INPUT);
	pcf85263a_init("I2C_1");
	time_t unix_time = 0;
	pcf85263a_rtc_set_time(1655343273);
	pcf85263a_alarm_type_1_config_t config = {
		.seconds = 45,
		.minutes = 0,
		.hours = 0,
		.days = 0,
		.months = 0,
	};

	pcf85263a_alarm_type_1_flag_t flag = {
		.enable_seconds = 1,
		.enable_minutes = 0,
		.enable_hours = 0,
		.enable_days = 0,
		.enable_months = 0,
	};

	pcf85263a_interrupt_flag_t interrupt_flag = {
		.enable_level_pulse = 0,
		.enable_periodic = 0,
		.enable_offset_correction = 0,
		.enable_alarm_1 = 1,
		.enable_alarm_2 = 0,
		.enable_timestamp = 0,
		.enable_battery_switch = 0,
		.enable_wdg = 0,
	};

	pcf85263a_interrupt_enable(interrupt_flag);
	pcf85263a_set_interrupt_io(true);
	pcf85263a_alarm_config_type_1(config);
	pcf85263a_alarm_enable_type_1(flag);

	k_sleep(K_SECONDS(1));
	while(1) {
		int level = gpio_pin_get(gpio, ALARM_PIN);
		if (level == 0) {
			pcf85263a_rtc_get_time(&unix_time);
			LOG_DBG("Alarm %d", unix_time);
			k_sleep(K_SECONDS(1));
		}
		k_usleep(100);
	}
}
