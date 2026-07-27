/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef MODEM_API_H
#define MODEM_API_H

#include <stdint.h>

/** @brief Signal measurement with the age of the measurement. */
struct modem_signal_sample {
	int16_t rssi;
	int16_t rsrp;
	int16_t rsrq;
	int64_t age_ms;
};

#endif /* MODEM_API_H */
