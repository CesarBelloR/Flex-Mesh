#ifndef POWER_MANAGEMENT_H_
#define POWER_MANAGEMENT_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_MANAGE_MODE_ACTIVE,
    POWER_MANAGE_MODE_SLEEP,
} power_management_mode_e;

/**
 * @brief Initialize power management to active counter rtc module
 *
 * @param None
 *
 * @return 0 on success or negative error value on failure.
 */
int power_management_init(void);

/**
 * @brief Set the power mode
 *
 * @param mode ref @power_management_mode_e
 *
 * @return 0 on success or negative error value on failure.
 */
int power_management_set_mode(power_management_mode_e mode);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGEMENT_H_ */