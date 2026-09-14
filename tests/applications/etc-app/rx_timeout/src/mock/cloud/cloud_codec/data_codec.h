#ifndef DATA_CODEC_TEST_H
#define DATA_CODEC_TEST_H

#include "etc_device.h"

int data_codec_sync_config(struct etc_config *cfg);
int data_codec_sync_thresholds(const struct etc_threshold thresholds[ETC_THRESHOLD_SLOT_COUNT]);

#endif /* DATA_CODEC_TEST_H */