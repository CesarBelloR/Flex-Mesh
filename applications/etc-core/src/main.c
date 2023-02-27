#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include "pcf85263a.h"
#include "bq24195.h"
#include "adc.h"
#include "ui.h"
#include "etc_setting.h"
#include "etc_app.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_APP_LOG_LEVEL);

static void app_driver_init(void) {
	ui_init();
	ui_led_set_pattern(UI_LED_ERROR_UNKNOWN);
	adc_init();
	pcf85263a_init("I2C_0");
	bq24195_init();
}

void main(void)
{
	app_driver_init();
	etc_setting_init();
	etc_app_init();

	for (;;) {
		k_cpu_idle();
#if CONFIG_LOG
		k_sleep(K_SECONDS(1));
#endif	
	}
}
