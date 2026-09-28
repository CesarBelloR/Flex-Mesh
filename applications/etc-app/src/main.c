#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <app_version.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/stats/stats.h>
#include <zephyr/storage/flash_map.h>
#include <app_event_manager.h>
#include <zephyr/sys/reboot.h>
#if IS_ENABLED(CONFIG_ETC_DATE_TIME)
#include "etc_date_time.h"
#endif
#include "ui.h"
#include "data/etc_cape.h"
#include "events/app_event.h"
#include "etc_device.h"
#include "etc_settings.h"
#if defined(CONFIG_BT)
#include "etc_ble.h"
#endif
#if defined(CONFIG_LWM2M_INTEGRATION_FIRMWARE_UPDATE_OBJ_SUPPORT)
#include "lwm2m_firmware.h"
#endif
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_APP_LOG_LEVEL);

#define MODULE_APP_THREAD_STACK_SIZE	2048
#define MODULE_CLOUD_THREAD_STACK_SIZE	4096
#define MODULE_BLE_THREAD_STACK_SIZE	4096
#define MODULE_DATA_THREAD_STACK_SIZE	2048
#define MODULE_LORA_THREAD_STACK_SIZE	2048
#define MODULE_MODEM_THREAD_STACK_SIZE	2048
#define MODULE_SENSOR_THREAD_STACK_SIZE 2048

static struct k_thread app_thread;
static struct k_thread cloud_thread;
static struct k_thread ble_thread;
static struct k_thread data_thread;
static struct k_thread lora_thread;
static struct k_thread modem_thread;
static struct k_thread sensor_thread;

static K_KERNEL_STACK_DEFINE(app_stack, MODULE_APP_THREAD_STACK_SIZE);
static K_KERNEL_STACK_DEFINE(cloud_stack, MODULE_CLOUD_THREAD_STACK_SIZE);
static K_KERNEL_STACK_DEFINE(ble_stack, MODULE_BLE_THREAD_STACK_SIZE);
static K_KERNEL_STACK_DEFINE(data_stack, MODULE_DATA_THREAD_STACK_SIZE);
static K_KERNEL_STACK_DEFINE(lora_stack, MODULE_LORA_THREAD_STACK_SIZE);
static K_KERNEL_STACK_DEFINE(modem_stack, MODULE_MODEM_THREAD_STACK_SIZE);
static K_KERNEL_STACK_DEFINE(sensor_stack, MODULE_SENSOR_THREAD_STACK_SIZE);

#ifdef CONFIG_MCUMGR_GRP_STAT
#include <zephyr/mgmt/mcumgr/grp/stat_mgmt/stat_mgmt.h>
#endif
#ifdef CONFIG_MCUMGR_CMD_ETC_MGMT
#include "etc_mgmt.h"
#endif

/* Define an example stats group; approximates seconds since boot. */
STATS_SECT_START(smp_svr_stats)
STATS_SECT_ENTRY(ticks)
STATS_SECT_END;

/* Assign a name to the `ticks` stat. */
STATS_NAME_START(smp_svr_stats)
STATS_NAME(smp_svr_stats, ticks)
STATS_NAME_END(smp_svr_stats);
/* Define an instance of the stats group. */
STATS_SECT_DECL(smp_svr_stats) smp_svr_stats;

char key[] = "ElL10TaC4T";

extern void app_module_thread_fn(void);
extern void cloud_module_thread_fn(void);
extern void ble_module_thread_fn(void);
extern void data_module_thread_fn(void);
extern void lora_module_thread_fn(void);
extern void modem_module_thread_fn(void);
extern void sensor_module_thread_fn(void);

int main(void)
{
	int rc = STATS_INIT_AND_REG(smp_svr_stats, STATS_SIZE_32, "smp_svr_stats");
	if (rc < 0) {
		LOG_ERR("Error initializing stats system [%d]", rc);
		return -EINVAL;
	}

	LOG_INF("EXACT Monitor 2.0 version %s", APP_VERSION_STRING);
#ifdef CONFIG_MCUMGR_CMD_ETC_MGMT
	etc_mgmt_register_group();
#endif

	struct mcuboot_img_header img_hdr;
	etc_cape_init(key, 10, 0);
	etc_cape_set_key(key, 10);

#ifdef CONFIG_MCUMGR
	/* using __TIME__ ensure that a new binary will be built on every
	 * compile which is convient when testing firmware upgrade.
	 */
	rc = boot_read_bank_header(FLASH_AREA_ID(image_0), &img_hdr, sizeof(img_hdr));
	if (rc) {
		LOG_ERR("Failed to get header %d", rc);
		return -EINVAL;
	} else {
		if (APP_VERSION_MAJOR != img_hdr.h.v1.sem_ver.major ||
		    APP_VERSION_MINOR != img_hdr.h.v1.sem_ver.minor ||
		    APP_VERSION_PATCH != img_hdr.h.v1.sem_ver.revision)
			LOG_DBG("Build Date: " __DATE__ " " __TIME__ " Version: %u.%u.%u+%u",
				(unsigned int)img_hdr.h.v1.sem_ver.major,
				(unsigned int)img_hdr.h.v1.sem_ver.minor,
				(unsigned int)img_hdr.h.v1.sem_ver.revision,
				(unsigned int)img_hdr.h.v1.sem_ver.build_num);
	}
#endif

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#if defined(CONFIG_LWM2M_INTEGRATION_FIRMWARE_UPDATE_OBJ_SUPPORT)
	lwm2m_firmware_self_confirm();
#else
	if (!boot_is_img_confirmed()) {
		LOG_INF("Unconfirmed image on boot; self-confirming to prevent rollback...");
		rc = boot_write_img_confirmed();
		if (rc) {
			LOG_ERR("Failed to confirm image: %d", rc);
		} else {
			LOG_INF("Firmware image confirmed successfully");
		}
	}
#endif
#endif

	etc_device_nvs_init();
	etc_settings_init();
	etc_device_init();
	ui_init();

	/* Initialize all threads */
	k_thread_create(&app_thread, app_stack, K_KERNEL_STACK_SIZEOF(app_stack),
			(k_thread_entry_t)app_module_thread_fn, NULL, NULL, NULL,
			K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_create(&cloud_thread, cloud_stack, K_KERNEL_STACK_SIZEOF(cloud_stack),
			(k_thread_entry_t)cloud_module_thread_fn, NULL, NULL, NULL,
			K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_create(&ble_thread, ble_stack, K_KERNEL_STACK_SIZEOF(ble_stack),
			(k_thread_entry_t)ble_module_thread_fn, NULL, NULL, NULL,
			K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_create(&data_thread, data_stack, K_KERNEL_STACK_SIZEOF(data_stack),
			(k_thread_entry_t)data_module_thread_fn, NULL, NULL, NULL,
			K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_create(&lora_thread, lora_stack, K_KERNEL_STACK_SIZEOF(lora_stack),
			(k_thread_entry_t)lora_module_thread_fn, NULL, NULL, NULL,
			K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_create(&modem_thread, modem_stack, K_KERNEL_STACK_SIZEOF(modem_stack),
			(k_thread_entry_t)modem_module_thread_fn, NULL, NULL, NULL,
			K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_create(&sensor_thread, sensor_stack, K_KERNEL_STACK_SIZEOF(sensor_stack),
			(k_thread_entry_t)sensor_module_thread_fn, NULL, NULL, NULL,
			K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);

	if (app_event_manager_init()) {
		/* Without the Application Event Manager, the application will not work
		 * as intended. A reboot is required in an attempt to recover.
		 */
		LOG_ERR("Application Event Manager could not be initialized, rebooting...");
		k_sleep(K_SECONDS(5));
		sys_reboot(SYS_REBOOT_COLD);
		while (1) {
			k_yield();
		}
	} else {
		LOG_INF("Application Event Manager initialized successfully");
	}

	struct app_event *event = new_app_event();
	event->type = APP_EVT_START;
	APP_EVENT_SUBMIT(event);
	/* The system work queue handles all incoming mcumgr requests.  Let the
	 * main thread idle while the mcumgr server runs.
	 */
	while (true) {
		k_sleep(K_FOREVER);
	}

	/* Never reach here */
	return 0;
}
