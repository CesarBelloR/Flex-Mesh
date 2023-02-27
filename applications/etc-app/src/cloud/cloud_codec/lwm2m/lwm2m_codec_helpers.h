/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**@file
 * @brief LwM2M codec helpers.
 */

#ifndef LWM2M_CODEC_HELPERS__
#define LWM2M_CODEC_HELPERS__

#include <zephyr/net/lwm2m.h>

#include "data_codec.h"

/**
 * @defgroup lwm2m_codec_helpers LwM2M codec helpers library
 * @{
 * @brief Library that contains common APIs for handling objects and resources in
 *	  Asset Tracker v2 LwM2M backend.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Create object and resource instances for objects that do not have this setup already
 *	   through default objects in the LwM2M engine.
 *
 *  @retval 0 If successful, otherwise a negative value indicating the reason of failure.
 */
int lwm2m_codec_helpers_create_objects_and_resources(void);

/** @brief Set up dedicated buffers for resources that do not have storage set in the respective
 *	   LwM2M object source files.
 *
 *  @retval 0 If successful, otherwise a negative value indicating the reason of failure.
 */
int lwm2m_codec_helpers_setup_resources(void);

/** @brief Set the initial values for the application's configuration object and register
 *	   callbacks for configuration changes.
 *
 *  @param[in] cfg Pointer to structure that contains the default configuration values for the
 *		   application.
 *  @param[in] callback Event handler to receive configuration updates.
 *
 *  @retval 0 If successful, otherwise a negative value indicating the reason of failure.
 */
int lwm2m_codec_helpers_setup_configuration_object(union etc_config *cfg,
						   lwm2m_engine_set_data_cb_t callback);

/** @brief Get the current values of the application's configuration object.
 *
 *  @param[out] cfg Pointer to buffer that will be populated with the current values of the
 *		    configuration object.
 *
 *  @retval 0 If successful, otherwise a negative value indicating the reason of failure.
 */
int lwm2m_codec_helpers_get_configuration_object(union etc_config *cfg);


/** @brief Set environmental sensor data.
 *
 *  @param[in] sensor Pointer to structure that contains environmental sensor data.
 *
 *  @retval 0 If successful, otherwise a negative value indicating the reason of failure.
 *  @return -ENODATA if the queued flag present in the input structure is false.
 */
int lwm2m_codec_helpers_set_sensor_data(struct data_sensors *sensor);

/** @brief Set modem dynamic data.
 *
 *  @param[in] modem_dynamic Pointer to structure that contains dynamic modem data.
 *
 *  @retval 0 If successful, otherwise a negative value indicating the reason of failure.
 *  @return -ENODATA if the queued flag present in the input structure is false.
 */
int lwm2m_codec_helpers_set_modem_dynamic_data(struct data_modem_dynamic *modem_dynamic);

/** @brief Set modem static data.
 *
 *  @param[in] modem_static Pointer to structure that contains static modem data.
 *
 *  @retval 0 If successful, otherwise a negative value indicating the reason of failure.
 *  @return -ENODATA if the queued flag present in the input structure is false.
 */
int lwm2m_codec_helpers_set_modem_static_data(struct data_modem_static *modem_static);

/** @brief Generate path lists with reference to objects.
 *	   This function outputs a list of paths that can be used to reference objects that should
 *	   be updated (sent to server) when calling the lwm2m_engine_send() function.
 *
 *  @param[out] output Pointer to structure into which the input path list will be added.
 *  @param[in]  path Pointer to list that contains LwM2M paths that will be
 *		     added to the output variable.
 *  @param[in]  path_size Size of the path list variable (path).
 *
 *  @retval 0 If successful, otherwise a negative value indicating the reason of failure.
 *  @return -ENODATA if the queued flag present in the input structure is false.
 */
int lwm2m_codec_helpers_object_path_list_add(struct cloud_codec_data *output,
					     const struct lwm2m_obj_path path[],
					     size_t path_size);

#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif /* LWM2M_CODEC_HELPERS__ */
