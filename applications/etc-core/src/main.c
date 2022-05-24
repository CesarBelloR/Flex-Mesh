#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include "pcf85263a.h"
#include "bq24195.h"
#include "adc.h"
#include "ui.h"
#include "ds18s20.h"
#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_APP_LOG_LEVEL);

static void app_driver_init(void) {
	ui_init();
	ui_led_set_pattern(UI_LED_ERROR_UNKNOWN);
	adc_init();
	pcf85263a_init();
	bq24195_init();
	ds18s20_init();
}

void main(void)
{
	app_driver_init();
}
