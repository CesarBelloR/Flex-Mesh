#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_watchdog, CONFIG_ETC_WATCHDOG_LOG_LEVEL);

#include "watchdog.h"

#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
static const struct gpio_dt_spec hw_wdt_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(hw_wdt), control_gpios, 0);
#else
static const struct gpio_dt_spec s0_dt = 
		GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
#endif
static uint16_t wdt_feed_interval_s = CONFIG_WDT_FEED_TIMEOUT_SECONDS;

void hw_wdt_work_handler(struct k_work *work) 
{
	struct k_work_delayable *work_delayable =
		CONTAINER_OF(work, struct k_work_delayable, work);
	etc_watchdog_feed();
	k_work_reschedule(work_delayable, K_SECONDS(wdt_feed_interval_s));
}

K_WORK_DELAYABLE_DEFINE(hw_wdt_work, hw_wdt_work_handler);

static int etc_watchdog_init(void) {
    LOG_INF("Initialized WDT hardware with interval %d (s)", wdt_feed_interval_s);
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	gpio_pin_configure_dt(&hw_wdt_dt, GPIO_OUTPUT_ACTIVE);
#endif
	etc_watchdog_start_work();
	
	return 0;
}

void etc_watchdog_feed(void) {
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	gpio_pin_set_dt(&hw_wdt_dt, 0U);
	k_busy_wait(10);
	gpio_pin_set_dt(&hw_wdt_dt, 1U);
	k_busy_wait(1);
	gpio_pin_set_dt(&hw_wdt_dt, 0U);
#else
	gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT);
	gpio_pin_set_dt(&s0_dt, 0U);
	k_busy_wait(10);
	gpio_pin_set_dt(&s0_dt, 1U);
	/* Minimum required pulse width according to datasheet is 100 ns. */
	k_busy_wait(1);
	gpio_pin_set_dt(&s0_dt, 0U);
#endif
	LOG_DBG("HW WDT fed");
}

void etc_watchdog_set_timeout(uint16_t timeout) {
    wdt_feed_interval_s = timeout;
}
 
void etc_watchdog_start_work(void) {
    k_work_reschedule(&hw_wdt_work, K_NO_WAIT);
}

void etc_watchdog_stop_work(void) {
    k_work_cancel_delayable(&hw_wdt_work);
}

SYS_INIT(etc_watchdog_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);