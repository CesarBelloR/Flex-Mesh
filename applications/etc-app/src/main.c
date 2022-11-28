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
#include <stats/stats.h>
#include <fs/fs.h>
#include <fs/littlefs.h>
#include <storage/flash_map.h>
#include <app_event_manager.h>
#include <zephyr/sys/reboot.h>
#if IS_ENABLED(CONFIG_ETC_DATE_TIME)
#include "etc_date_time.h"
#endif
#include "ui.h"
#include "data/etc_cape.h"
#include "events/app_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_ETC_APP_LOG_LEVEL);

#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
#include <device.h>
#endif
#ifdef CONFIG_MCUMGR_CMD_OS_MGMT
#include "os_mgmt/os_mgmt.h"
#endif
#ifdef CONFIG_MCUMGR_CMD_IMG_MGMT
#include "img_mgmt/img_mgmt.h"
#endif
#ifdef CONFIG_MCUMGR_CMD_STAT_MGMT
#include "stat_mgmt/stat_mgmt.h"
#endif
#ifdef CONFIG_MCUMGR_CMD_SHELL_MGMT
#include "shell_mgmt/shell_mgmt.h"
#endif
#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
#include "fs_mgmt/fs_mgmt.h"
#endif

#define PARTITION_NODE DT_NODELABEL(lfs1)

#if DT_NODE_EXISTS(PARTITION_NODE)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);
#else /* PARTITION_NODE */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
#endif /* PARTITION_NODE */

struct fs_mount_t *mount_point = &FS_FSTAB_ENTRY(PARTITION_NODE);

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

int main_external_flash_erase(unsigned int id)
{
	const struct flash_area *pfa;
	int rc;

	rc = flash_area_open(id, &pfa);
	if (rc < 0) {
		LOG_ERR("FAIL: unable to find flash area %u: %d\n",
			id, rc);
		return rc;
	}

	LOG_INF("Area %u at 0x%x for %u bytes",
		   id, (unsigned int)pfa->fa_off, (unsigned int)pfa->fa_size);

	/* Optional wipe flash contents */
	if (IS_ENABLED(CONFIG_APP_WIPE_STORAGE)) {
		rc = flash_area_erase(pfa, 0, pfa->fa_size);
		LOG_ERR("Erasing flash area ... %d", rc);
	}

	flash_area_close(pfa);
	return rc;
}

void main(void)
{
	int rc = STATS_INIT_AND_REG(smp_svr_stats, STATS_SIZE_32,
		"smp_svr_stats");
	if (rc < 0) {
		LOG_ERR("Error initializing stats system [%d]", rc);
	}
#ifdef CONFIG_MCUMGR_CMD_OS_MGMT
	os_mgmt_register_group();
#endif
#ifdef CONFIG_MCUMGR_CMD_IMG_MGMT
	img_mgmt_register_group();
#endif
#ifdef CONFIG_MCUMGR_CMD_STAT_MGMT
	stat_mgmt_register_group();
#endif
#ifdef CONFIG_MCUMGR_CMD_SHELL_MGMT
	shell_mgmt_register_group();
#endif
#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
	fs_mgmt_register_group();
#endif
#ifdef CONFIG_MCUMGR_SMP_BT
	start_smp_bluetooth();
#endif
#ifdef CONFIG_MCUMGR_SMP_UDP
	start_smp_udp();
#endif
#if 0
	rc = main_external_flash_erase((uintptr_t)mount_point->storage_dev);
	if (rc < 0) {
		LOG_ERR("Failed to erase flash memory %d", rc);
		return;
	}
#endif 
	struct mcuboot_img_header img_hdr;
	etc_cape_init(key, 10, 0);
	etc_cape_set_key(key, 10); 
	
	rc = boot_write_img_confirmed();
	if(rc) {
		LOG_ERR("Img confirmed failed");
	}

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
		LOG_DBG("Build Date: " __DATE__ " " __TIME__ " Version: %u.%u.%u+%u",
		    (unsigned int) img_hdr.h.v1.sem_ver.major,
		    (unsigned int) img_hdr.h.v1.sem_ver.minor,
		    (unsigned int) img_hdr.h.v1.sem_ver.revision,
		    (unsigned int) img_hdr.h.v1.sem_ver.build_num);
	}
	ui_init();
#if IS_ENABLED(CONFIG_ETC_DATE_TIME)
	date_time_start_work();
#endif
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
