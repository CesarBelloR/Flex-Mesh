/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/gpio.h>
#include <assert.h>

#include "watchdog_app.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(watchdog, CONFIG_WATCHDOG_LOG_LEVEL);

#define WDT_FEED_WORKER_DELAY_MS					\
	(((CONFIG_WATCHDOG_APPLICATION_TIMEOUT_SEC * 1000) / 2) -	\
	 (CONFIG_WATCHDOG_APPLICATION_FEED_DELAY_SEC * 1000))
#define WATCHDOG_TIMEOUT_MSEC						\
	(CONFIG_WATCHDOG_APPLICATION_TIMEOUT_SEC * 1000)

BUILD_ASSERT(WDT_FEED_WORKER_DELAY_MS > 0,
	     "WDT feed worker delay is 0 or negative. Check the values of"
	     "CONFIG_WATCHDOG_APPLICATION_TIMEOUT_SEC and "
	     "CONFIG_WATCHDOG_APPLICATION_FEED_DELAY_SEC");

#define HW_WDT_TIMEOUT_S (17 * 60)
#define HW_WDT_WORK_INTERVAL_S (HW_WDT_TIMEOUT_S - (2 * 60))
#define HW_WDT_RETRY_INTERVAL_S 10

struct wdt_config_storage {
	const struct device *wdt;
};

struct wdt_data_storage {
	int wdt_channel_id;
	struct k_work_delayable system_workqueue_work;
};

#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
static const struct gpio_dt_spec hw_wdt_dt =  GPIO_DT_SPEC_GET_OR(DT_NODELABEL(hw_wdt), control_gpios, 0);
#else
struct k_sem sens_sel0_wdt_sem;
static const struct gpio_dt_spec s0_watchdog_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
#endif

static watchdog_evt_handler_t app_evt_handler = NULL;
static struct k_work_delayable hw_wdt_work;

/* Flag set when the library has been initialized and started. */
static bool init_and_start;

static void watchdog_notify_event(const struct watchdog_evt *evt)
{
	__ASSERT(evt != NULL, "Library event not found");

	if (app_evt_handler != NULL) {
		app_evt_handler(evt);
	}
}

static const struct wdt_config_storage wdt_config = {
	.wdt = DEVICE_DT_GET(DT_NODELABEL(wdt)),
};

static struct wdt_data_storage wdt_data;

static void watchdog_feed(void)
{
	struct watchdog_evt evt = {
		.type = WATCHDOG_EVT_FEED,
	};

	int err = wdt_feed(wdt_config.wdt, wdt_data.wdt_channel_id);

	LOG_DBG("Feeding watchdog");

	if (err) {
		LOG_ERR("Cannot feed watchdog. Error code: %d", err);
	}

	watchdog_notify_event(&evt);
}

static void primary_feed_worker(struct k_work *work_desc)
{
#if IS_ENABLED(CONFIG_WATCHDOG_APPLICATION_FEED_WORKQUEUE)
	watchdog_feed();
	k_work_reschedule(&wdt_data.system_workqueue_work,
				K_MSEC(WDT_FEED_WORKER_DELAY_MS));
#elif IS_ENABLED(CONFIG_WATCHDOG_APPLICATION_FEED_REQUEST)
	struct watchdog_evt evt = {
		.type = WATCHDOG_EVT_FEED_REQUEST,
	};
	watchdog_notify_event(&evt);
#endif
}

static int watchdog_timeout_install(const struct wdt_config_storage *config,
				    struct wdt_data_storage *data)
{
	static const struct wdt_timeout_cfg wdt_settings = {
		.window = {
			.min = 0,
			.max = WATCHDOG_TIMEOUT_MSEC,
		},
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC
	};
	struct watchdog_evt evt = {
		.type = WATCHDOG_EVT_TIMEOUT_INSTALLED,
		.timeout = WATCHDOG_TIMEOUT_MSEC
	};

	__ASSERT_NO_MSG(config != NULL);
	__ASSERT_NO_MSG(data != NULL);

	data->wdt_channel_id =
		wdt_install_timeout(config->wdt, &wdt_settings);
	if (data->wdt_channel_id < 0) {
		LOG_ERR("Cannot install watchdog timer! Error code: %d",
			data->wdt_channel_id);
		return -EFAULT;
	}

	watchdog_notify_event(&evt);

	LOG_DBG("Watchdog timeout installed. Timeout: %d",
		CONFIG_WATCHDOG_APPLICATION_TIMEOUT_SEC);
	return 0;
}

static int watchdog_start(const struct wdt_config_storage *config)
{
	__ASSERT_NO_MSG(config != NULL);

	int err = wdt_setup(config->wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);

	if (err) {
		LOG_ERR("Cannot start watchdog! Error code: %d", err);
	} else {
		LOG_DBG("Watchdog started");
	}
	return err;
}

static int watchdog_feed_enable(const struct wdt_config_storage *config,
				struct wdt_data_storage *data)
{
	__ASSERT_NO_MSG(config != NULL);
	__ASSERT_NO_MSG(data != NULL);

	struct watchdog_evt evt = {
		.type = WATCHDOG_EVT_FEED,
	};

	k_work_init_delayable(&data->system_workqueue_work, primary_feed_worker);

	int err = wdt_feed(config->wdt, data->wdt_channel_id);

	if (err) {
		LOG_ERR("Cannot feed watchdog. Error code: %d", err);
		return err;
	}

	watchdog_notify_event(&evt);

	k_work_schedule(&data->system_workqueue_work,
				K_MSEC(WDT_FEED_WORKER_DELAY_MS));
	LOG_DBG("Watchdog feed enabled. Timeout: %d", WDT_FEED_WORKER_DELAY_MS);

	return err;
}

static int watchdog_enable(const struct wdt_config_storage *config,
			   struct wdt_data_storage *data)
{
	int err;

	__ASSERT_NO_MSG(data != NULL);

	if (!device_is_ready(config->wdt)) {
		LOG_ERR("Watchdog device not ready");
		return -ENODEV;
	}

	err = watchdog_timeout_install(config, data);
	if (err) {
		return err;
	}

	err = watchdog_start(config);
	if (err) {
		return err;
	}

	err = watchdog_feed_enable(config, data);
	if (err) {
		return err;
	}

	return 0;
}

static void hw_wdt_feed(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	LOG_INF("Feeding HW WDT with HW WDT pin");
	gpio_pin_set_dt(&hw_wdt_dt, 0);
	k_busy_wait(50);
	gpio_pin_set_dt(&hw_wdt_dt, 1);
	/* Minimum required pulse width according to datasheet is 100 ns. */
	k_busy_wait(50);
	gpio_pin_set_dt(&hw_wdt_dt, 0);
#else
	LOG_INF("Feeding HW WDT with HW S0 pin");
	gpio_pin_set_dt(&s0_watchdog_dt, 0);
	k_busy_wait(50);
	gpio_pin_set_dt(&s0_watchdog_dt, 1);
	/* Minimum required pulse width according to datasheet is 100 ns. */
	k_busy_wait(50);
	gpio_pin_set_dt(&s0_watchdog_dt, 0);
#endif
}

static void hw_wdt_work_fn(struct k_work *work)
{
	if (watchdog_sens_sel0_wdt_sem_take(K_NO_WAIT) != 0) {
		k_work_schedule(&hw_wdt_work, K_SECONDS(HW_WDT_RETRY_INTERVAL_S));
	}

	hw_wdt_feed();

	watchdog_sens_sel0_wdt_sem_give();
	k_work_schedule(&hw_wdt_work, K_SECONDS(HW_WDT_WORK_INTERVAL_S));
}

static void init_hw_wdt(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	gpio_pin_configure_dt(&hw_wdt_dt, GPIO_OUTPUT_ACTIVE);
#else
	k_sem_init(&sens_sel0_wdt_sem, 1, 1);
	gpio_pin_configure_dt(&s0_watchdog_dt, GPIO_OUTPUT_INACTIVE);
#endif
	k_work_init_delayable(&hw_wdt_work, hw_wdt_work_fn);
	k_work_schedule(&hw_wdt_work, K_SECONDS(5));
}

int watchdog_sens_sel0_wdt_sem_take(k_timeout_t timeout)
{
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	return 0;
#else
	return k_sem_take(&sens_sel0_wdt_sem, timeout);
#endif
}

void watchdog_sens_sel0_wdt_sem_give(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	return;
#else
	k_sem_give(&sens_sel0_wdt_sem);
#endif
}

int watchdog_init_and_start(void)
{
	int err;
	struct watchdog_evt evt = {
		.type = WATCHDOG_EVT_START
	};

	err = watchdog_enable(&wdt_config, &wdt_data);
	if (err) {
		LOG_ERR("Failed to enable watchdog, error: %d", err);
		return err;
	}

	watchdog_notify_event(&evt);
	LOG_INF("Initialized and start WDT with period %d (ms)", WATCHDOG_TIMEOUT_MSEC);
	init_and_start = true;

	init_hw_wdt();

	return 0;
}

void watchdog_feed_from_request(void)
{
	watchdog_feed();

	/* Reschedule next feed request. */
	k_work_reschedule(&wdt_data.system_workqueue_work,
				K_MSEC(WDT_FEED_WORKER_DELAY_MS));
}

void watchdog_register_handler(watchdog_evt_handler_t evt_handler)
{
	if (evt_handler == NULL) {
		app_evt_handler = NULL;

		LOG_DBG("Previously registered handler %p de-registered",
			app_evt_handler);

		return;
	}

	LOG_DBG("Registering handler %p", evt_handler);

	app_evt_handler = evt_handler;

	/* If the application watchdog already has been initialized and started prior to an
	 * external module registering a handler, the newly registered handler is notified that
	 * the library has been started and that a watchdog timeout has been installed.
	 */
	if (init_and_start) {
		struct watchdog_evt evt = {
			.type = WATCHDOG_EVT_START,
		};

		watchdog_notify_event(&evt);

		evt.type = WATCHDOG_EVT_TIMEOUT_INSTALLED;
		evt.timeout = WATCHDOG_TIMEOUT_MSEC;

		watchdog_notify_event(&evt);
	}
}
