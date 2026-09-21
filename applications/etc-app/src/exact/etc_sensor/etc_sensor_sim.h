/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef ETC_SENSOR_SIM_H_
#define ETC_SENSOR_SIM_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include "events/sensor_event.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_ETC_SENSOR_SIM_SHELL

/**
 * @brief Simulate one input, replacing whatever the probe reports.
 *
 * @param in Input to simulate.
 * @param value Reading substituted into every following sample. The
 * NO_CONNECTED sentinels are legal, so an unplugged probe can be simulated.
 * @return 0 on success, -EINVAL when @p in is out of range.
 */
int etc_sensor_sim_set(enum sensor_input in, float value);

/**
 * @brief Stop simulating inputs and let their probes through again.
 *
 * @param mask Inputs to release (bit = @ref enum sensor_input).
 */
void etc_sensor_sim_clear(uint16_t mask);

/**
 * @brief Substitute the simulated readings into a sample.
 *
 * Logs the active mask once per sample while anything is simulated, so the
 * device log marks the samples that are not the probes'.
 *
 * @param data Sample patched in place; inputs that are not simulated keep the
 * value the physical getters produced.
 */
void etc_sensor_sim_apply(struct sensor_data *data);

/** @brief Inputs currently simulated (bit = @ref enum sensor_input). */
uint16_t etc_sensor_sim_active_mask(void);

#else /* !CONFIG_ETC_SENSOR_SIM_SHELL */

/* Only the sample hook is called from code that is built either way. */
static inline void etc_sensor_sim_apply(struct sensor_data *data)
{
	ARG_UNUSED(data);
}

#endif /* CONFIG_ETC_SENSOR_SIM_SHELL */

#ifdef __cplusplus
}
#endif

#endif /* ETC_SENSOR_SIM_H_ */
