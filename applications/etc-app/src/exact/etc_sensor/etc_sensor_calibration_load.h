#ifndef ETC_SENSOR_CALIBRATION_LOAD_H_
#define ETC_SENSOR_CALIBRATION_LOAD_H_

struct etc_sensor_adc_calibration_info;

/**
 * @brief Load ADC calibration coefficients with user-first, factory-fallback priority.
 *
 * Reads the user calibration (offset/high/ref) from settings. If any user
 * setting cannot be read, falls back to the factory calibration. On success the
 * info->loaded flag is set. The factory values are used only as the fallback, so
 * a device that has been (re)calibrated keeps using its user coefficients.
 *
 * This logic is kept in its own translation unit (free of sensor-driver and
 * devicetree dependencies) so it can be unit tested with a mocked settings store.
 *
 * @param info Destination calibration info. Must be non-NULL.
 * @return 0 on success, a negative errno if neither user nor factory calibration
 *         could be read.
 */
int etc_sensor_load_calibration_info(struct etc_sensor_adc_calibration_info *info);

#endif /* ETC_SENSOR_CALIBRATION_LOAD_H_ */
