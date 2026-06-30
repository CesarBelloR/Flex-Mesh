#include "etc_sensor_calibration_load.h"
#include "etc_device.h"
#include "etc_calibration.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_sensor_cal_load, CONFIG_ETC_SENSOR_LOG_LEVEL);

int etc_sensor_load_calibration_info(struct etc_sensor_adc_calibration_info *info)
{
	int rc;

	info->loaded = false;
	rc = etc_device_read_setting(ETC_CALIBRATION_USER_OFFSET_ID, &info->offset,
				     sizeof(info->offset));
	if (rc) {
		LOG_ERR("Can't load the user calibration for offset");
		goto factory;
	}
	rc = etc_device_read_setting(ETC_CALIBRATION_USER_RAWHIGH_ID, &info->high,
				     sizeof(info->high));
	if (rc) {
		LOG_ERR("Can't load the user calibration for raw high offset");
		goto factory;
	}
	rc = etc_device_read_setting(ETC_CALIBRATION_USER_REF_ID, &info->ref,
				     sizeof(info->ref));
	if (rc) {
		LOG_ERR("Can't load the user calibration for reference");
		goto factory;
	}
	LOG_INF("Calibration value %f %f %f", info->offset, info->high, info->ref);
	/* User calibration loaded successfully; do not fall through and overwrite
	 * it with the factory values. The factory block below is only the fallback
	 * for when a user setting could not be read. */
	info->loaded = true;
	return 0;
factory:
	rc = etc_device_read_setting(ETC_CALIBRATION_OFFSET_ID, &info->offset,
				     sizeof(info->offset));
	if (rc) {
		LOG_ERR("Can't load the factory calibration for offset");
		return rc;
	}
	rc = etc_device_read_setting(ETC_CALIBRATION_RAWHIGH_ID, &info->high,
				     sizeof(info->high));
	if (rc) {
		LOG_ERR("Can't load the factory calibration for raw high offset");
		return rc;
	}
	rc = etc_device_read_setting(ETC_CALIBRATION_REF_ID, &info->ref, sizeof(info->ref));
	if (rc) {
		LOG_ERR("Can't load the factory calibration for reference");
		return rc;
	}
	info->loaded = true;
	return 0;
}
