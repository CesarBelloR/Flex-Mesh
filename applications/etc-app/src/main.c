#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <drivers/gpio.h>
#include <usb/usb_device.h>
#include <app_version.h>
#include <dfu/mcuboot.h>
#include <pm/pm.h>
#include <pm/device.h>
#include <drivers/hwinfo.h>
#include <fs/fs.h>
#include <fs/littlefs.h>
#include <app_event_manager.h>
#include <zephyr/sys/reboot.h>

#include "events/app_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_APP_LOG_LEVEL);

void main(void)
{
	struct mcuboot_img_header img_hdr;

	int rc = boot_write_img_confirmed();
	if(rc)
		LOG_ERR("Img confirmed failed\n");

	/* using __TIME__ ensure that a new binary will be built on every
	 * compile which is convient when testing firmware upgrade.
	 */
	rc = boot_read_bank_header(FLASH_AREA_ID(image_0), &img_hdr, sizeof(img_hdr));
	if (rc) 
	{
		LOG_ERR("Failed to get header %d", rc);
	} else {
		if(APP_VERSION_MAJOR !=  img_hdr.h.v1.sem_ver.major
			|| APP_VERSION_MINOR != img_hdr.h.v1.sem_ver.minor
			|| APP_VERSION_PATCH != img_hdr.h.v1.sem_ver.revision)
			LOG_ERR("Version Mismatch %s", APP_VERSION_STR);
		LOG_DBG("Build Date: " __DATE__ " " __TIME__ " Version: %u.%u.%u+%u",
		    (unsigned int) img_hdr.h.v1.sem_ver.major,
		    (unsigned int) img_hdr.h.v1.sem_ver.minor,
		    (unsigned int) img_hdr.h.v1.sem_ver.revision,
		    (unsigned int) img_hdr.h.v1.sem_ver.build_num);
	}

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
		k_sleep(K_SECONDS(1));
	}
}
