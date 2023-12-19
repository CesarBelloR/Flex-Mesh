/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * 
 * Copyright (c) 2023 EXACT Technology
 */

#ifndef LWM2M_FIRMWARE_H__
#define LWM2M_FIRMWARE_H__

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/lwm2m.h>

#ifdef __cplusplus
extern "C" {
#endif


#if defined(CONFIG_LWM2M_INTEGRATION_FIRMWARE_UPDATE_OBJ_SUPPORT)
/**
 * @brief Firmware update state change event callback.
 *
 * @param[in] update_state LwM2M Firmware Update object states
 *
 * @return Callback returns a negative error code (errno.h) indicating
 *         reason of failure or 0 for success.
 */
typedef int (*lwm2m_firmware_get_update_state_cb_t)(uint8_t update_state);

/**
 * @brief Set event callback for firmware update changes.
 *
 * LwM2M clients use this function to register a callback for receiving the
 * update state changes when performing a firmware update.
 *
 * @param[in] cb A callback function to receive firmware update state changes or NULL for disable.
 */
void lwm2m_firmware_set_update_state_cb(lwm2m_firmware_get_update_state_cb_t cb);

/**
 * @brief Firmware read callback
 */
void *firmware_read_cb(uint16_t obj_inst_id, size_t *data_len);
/**
 * @brief Verify active firmware image
 */
int lwm2m_init_firmware(void);

/**
 * @brief Initialize Image Update object
 */
int lwm2m_init_image(void);

/**
 * @brief Trigger the OTA job if any pending
 */
void lwm2m_firmware_start_pending_job(void);
#endif

#define RESULT_ADV_FOTA_CANCELLED 10
#define RESULT_ADV_FOTA_DEFERRED 11
#define RESULT_ADV_CONFLICT_STATE 12
#define RESULT_ADV_DEPENDENCY_ERR 13

/* Reboot execute possible argument's */
#define REBOOT_SOURCE_DEVICE_OBJ 0
#define REBOOT_SOURCE_FOTA_OBJ 1

/* Firmware resource IDs */
#define LWM2M_FOTA_PACKAGE_ID 0
#define LWM2M_FOTA_PACKAGE_URI_ID 1
#define LWM2M_FOTA_UPDATE_ID 2
#define LWM2M_FOTA_STATE_ID 3
#define LWM2M_FOTA_UPDATE_RESULT_ID 5
#define LWM2M_FOTA_PACKAGE_NAME_ID 6
#define LWM2M_FOTA_PACKAGE_VERSION_ID 7
#define LWM2M_FOTA_UPDATE_PROTO_SUPPORT_ID 8
#define LWM2M_FOTA_UPDATE_DELIV_METHOD_ID 9

#ifdef __cplusplus
}
#endif

#endif /* LWM2M_FIRMWARE_H__ */