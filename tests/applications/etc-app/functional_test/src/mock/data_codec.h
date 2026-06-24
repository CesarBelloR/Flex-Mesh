/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Minimal mock of data_codec.h for the functional test unit test. Only
 * data_codec_rsrp_is_valid() is referenced (by the shell command, which is
 * disabled in this test build).
 */
#ifndef DATA_CODEC_MOCK_H_
#define DATA_CODEC_MOCK_H_

#include <stdbool.h>
#include <stdint.h>

static inline bool data_codec_rsrp_is_valid(int16_t rsrp)
{
	return rsrp > -125 && rsrp < 0;
}

#endif /* DATA_CODEC_MOCK_H_ */
