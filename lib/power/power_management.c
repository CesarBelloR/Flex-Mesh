#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/sys/printk.h>
#include <zephyr/pm/pm.h>
#include "power_management.h"
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(power_management, CONFIG_POWER_LOG_LEVEL);

#define ALARM_CHANNEL_ID 0

#if POWER_MODULE_RTC
static const struct device *power_dev;
#endif 

int power_management_init(void) {
#if POWER_MODULE_RTC
	power_dev = device_get_binding(CONFIG_POWER_MODULE_RTC_NAME);
	if (power_dev == NULL) {
		LOG_ERR("Device not found %s", CONFIG_POWER_MODULE_RTC_NAME);
		return -EINVAL;
	}

	counter_start(power_dev);
#endif
    return 0;
}

static void power_management_enter_sleep(void) {
#if POWER_MODULE_RTC
    static struct counter_alarm_cfg alarm_cfg;
	alarm_cfg.flags = 0;
	alarm_cfg.ticks = counter_us_to_ticks(power_dev, CONFIG_POWER_MODULE_SLEEP_TIME_SECOND);
	alarm_cfg.callback = NULL;
	alarm_cfg.user_data = &alarm_cfg;

	int err = counter_set_channel_alarm(power_dev, ALARM_CHANNEL_ID,
					&alarm_cfg);
	LOG_DBG("Set alarm in %u sec (%u ticks)",
	       (uint32_t)(counter_ticks_to_us(counter_dev,
					   alarm_cfg.ticks) / USEC_PER_SEC),
	       alarm_cfg.ticks);

	if (-EINVAL == err) {
		LOG_DBG("Alarm settings invalid");
	} else if (-ENOTSUP == err) {
		LOG_DBG("Alarm setting request not supported");
	} else if (err != 0) {
		LOG_DBG("Error\n");
	}

    pm_power_state_force((struct pm_state_info){PM_STATE_SOFT_OFF, 0, 0});
    while(true) {
        k_sleep(K_FOREVER);
    }
#endif
}

int power_management_set_mode(power_management_mode_e mode) {
    switch (mode) {
        case POWER_MANAGE_MODE_ACTIVE:
            /* No action required */
            break;
        case POWER_MANAGE_MODE_SLEEP:
            power_management_enter_sleep();
            break;
    }

    return 0;
}
