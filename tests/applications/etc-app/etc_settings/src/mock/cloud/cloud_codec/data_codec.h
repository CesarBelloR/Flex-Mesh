#include "etc_device.h"

/**
 * Sync the configuration from local device to cloud
 *
 * @retval 0 success
 */
int data_codec_sync_config(struct etc_config *cfg);

/**
 * Sync the immediate report thresholds from local device to cloud
 *
 * @retval 0 success
 */
int data_codec_sync_thresholds(const struct etc_threshold thresholds[ETC_THRESHOLD_SLOT_COUNT]);
