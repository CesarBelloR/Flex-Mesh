#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/stats/stats.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log.h>
#include <modem_api.h>

LOG_MODULE_REGISTER(main, CONFIG_ETC_APP_LOG_LEVEL);

#ifdef CONFIG_MCUMGR_CMD_FS_MGMT
#include <zephyr/device.h>
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
#if 0
	rc = main_external_flash_erase((uintptr_t)mount_point->storage_dev);
	if (rc < 0) {
		LOG_ERR("Failed to erase flash memory %d", rc);
		return;
	}
#endif 

	const struct device *modem_dev;
	modem_dev = device_get_binding("quectel-bg95");
	if (modem_dev == NULL) {
		LOG_ERR("Unable to get modem device");
	}
	pm_device_runtime_enable(modem_dev);

	k_sleep(K_SECONDS(10));

	pm_device_runtime_get(modem_dev);

	while (!quectel_bg95_is_ready()) {
		k_sleep(K_SECONDS(1));
	}

	if (quectel_bg95_is_ready()) {
		LOG_INF("BG95 ready!");
	}

	/* The system work queue handles all incoming mcumgr requests.  Let the
	 * main thread idle while the mcumgr server runs.
	 */
	while (true) {
		k_sleep(K_SECONDS(1));
	}
}
