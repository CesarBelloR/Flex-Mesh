/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 * 
 * Copyright (c) 2023 EXACT Technology
 */

#include <zephyr/kernel.h>
#include <zephyr/net/lwm2m.h>
#include <net/lwm2m_client_utils.h>
#include <lwm2m_resource_ids.h>
#include <lwm2m_rd_client.h>
#include <zephyr/net/socket.h>
#include <hw_id.h>
#include <zephyr/net/lwm2m_path.h>
#include <zephyr/net/lwm2m.h>

#include "etc_lwm2m_client_utils.h"
#include "lwm2m_firmware.h"
#include "lwm2m/lwm2m_codec_helpers.h"
#include "etc_reclaim_obj_48934.h"

#include "cloud/cloud_wrapper.h"

#define MODULE lwm2m_integration


#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_CLOUD_INTEGRATION_LOG_LEVEL);

#define LWM2M_INTEGRATION_CLIENT_ID_LEN ETC_SETTINGS_DEVICE_ID_LEN

/* LWM2M_OBJECT_SECURITY_ID */
#define SECURITY_SERVER_URI_ID 0
#define SECURITY_BOOTSTRAP_FLAG_ID 1
#define SECURITY_MODE_ID 2
#define SECURITY_CLIENT_PK_ID 3
#define SECURITY_SERVER_PK_ID 4
#define SECURITY_SECRET_KEY_ID 5
#define SECURITY_SHORT_SERVER_ID 10
/* LWM2M_OBJECT_SERVER_ID */
#define SERVER_SHORT_SERVER_ID 0
#define SERVER_LIFETIME_ID 1

/* Resource ID used to register a reboot callback. */
#define DEVICE_OBJECT_REBOOT_RID 4
#define MAX_RESOURCE_LEN 20

/* Internal states. */
static enum lwm2m_integration_state_type {
	DISCONNECTED,
	CONNECTING,
	CONNECTED,
	PAUSED,
} state;

static cloud_wrap_evt_handler_t wrapper_evt_handler;

/* LWM2M client instance. */
static struct lwm2m_ctx client;

static char client_id_buf[ETC_SETTINGS_DEVICE_ID_LEN + 1];
static char endpoint_name[sizeof("urn:dev:os:60606-") +
			  LWM2M_INTEGRATION_CLIENT_ID_LEN];

/* Enable session lifetime check after initial boot. After bootstrapping, the bootstrap server
 * will override the configured lifetime CONFIG_LWM2M_ENGINE_DEFAULT_LIFETIME.
 * Therefore we check the session lifetime and overrides it with the configured value if
 * it differs.
 */
static bool update_session_lifetime = true;

void client_acknowledge(void)
{
	lwm2m_acknowledge(&client);
}

static void cloud_wrapper_notify_event(const struct cloud_wrap_event *evt)
{
	if ((wrapper_evt_handler != NULL) && (evt != NULL)) {
		wrapper_evt_handler(evt);
	} else {
		LOG_ERR("Library event handler not registered, or empty event");
	}
}

/* Function used to override the configured session lifetime,
 * CONFIG_LWM2M_ENGINE_DEFAULT_LIFETIME.
 */
static void rd_client_update_lifetime(int srv_obj_inst)
{
	char pathstr[MAX_RESOURCE_LEN];
	int err, len;
	uint32_t current_lifetime = 0;
	uint32_t lifetime = CONFIG_LWM2M_ENGINE_DEFAULT_LIFETIME;
	const struct lwm2m_obj_path path = LWM2M_OBJ(1, srv_obj_inst, 1);

	err = lwm2m_get_u32(&path, &current_lifetime);
	if (err) {
		LOG_ERR("Failed getting current session lifetime, error: %d", err);
		return;
	}

	if (current_lifetime != lifetime) {
		err = lwm2m_set_u32(&path, lifetime);
		if (err) {
			LOG_ERR("Failed setting current session lifetime, error: %d", err);
			return;
		}

		LOG_DBG("Update session lifetime from %d to %d", current_lifetime, lifetime);
	}

	update_session_lifetime = false;
}

static void rd_client_event(struct lwm2m_ctx *client, enum lwm2m_rd_client_event client_event)
{
	ARG_UNUSED(client);

	struct cloud_wrap_event cloud_wrap_evt = { 0 };
	bool notify = false;

	switch (client_event) {
	case LWM2M_RD_CLIENT_EVENT_NONE:
		LOG_DBG("LWM2M_RD_CLIENT_EVENT_NONE");
		break;
	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_FAILURE:
		LOG_WRN("LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_FAILURE");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_DISCONNECTED;
		notify = true;
		break;
	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_COMPLETE:
		LOG_DBG("LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_COMPLETE");

		/* Trigger update of session lifetime after bootstrap. */
		update_session_lifetime = true;

		/* Bootstrap registration complete. Lwm2m engine will proceed to connect to
		 * the management server.
		 */
		state = CONNECTING;
		break;
	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_TRANSFER_COMPLETE:
		LOG_DBG("LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_TRANSFER_COMPLETE");
		break;
	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
		LOG_WRN("LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_DISCONNECTED;
		notify = true;
		break;
	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE:
		LOG_DBG("LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE");

		if (update_session_lifetime) {
			/* Read and update the current server lifetime value. */
			rd_client_update_lifetime(client->srv_obj_inst);
		}

		cloud_wrap_evt.type = CLOUD_WRAP_EVT_CONNECTED;
		notify = true;
		state = CONNECTED;
		break;
	case LWM2M_RD_CLIENT_EVENT_REG_TIMEOUT:
		LOG_WRN("LWM2M_RD_CLIENT_EVENT_REG_TIMEOUT");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_CONNECTING;
		state = CONNECTING;
		notify = true;
		break;
	case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE:
		LOG_DBG("LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE");
		if (state == CONNECTING) {
			cloud_wrap_evt.type = CLOUD_WRAP_EVT_CONNECTED;
			notify = true;
			state = CONNECTED;
		}
		break;
	case LWM2M_RD_CLIENT_EVENT_DEREGISTER_FAILURE:
		LOG_WRN("LWM2M_RD_CLIENT_EVENT_DEREGISTER_FAILURE");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_ERROR;
		notify = true;
		break;
	case LWM2M_RD_CLIENT_EVENT_DISCONNECT:
		LOG_DBG("LWM2M_RD_CLIENT_EVENT_DISCONNECT");
		break;
	case LWM2M_RD_CLIENT_EVENT_QUEUE_MODE_RX_OFF:
		LOG_DBG("LWM2M_RD_CLIENT_EVENT_QUEUE_MODE_RX_OFF");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_RX_OFF;
		notify = true;
		break;
	case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
		LOG_ERR("LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_ERROR;
		notify = true;
		break;
	case LWM2M_RD_CLIENT_EVENT_ENGINE_SUSPENDED:
		LOG_DBG("LWM2M_RD_CLIENT_EVENT_ENGINE_SUSPENDED");
		break;
	default:
		LOG_ERR("Unknown event: %d", client_event);
		break;
	}

	/* If a LwM2M failure has occurred, we explicitly stop the engine before the cloud module
	 * is notified with the CLOUD_WRAP_EVT_DISCONNECTED event. This is to clear up any
	 * LwM2M engine state to ensure that we are able to perform a clean restart of the engine.
	 */
	if (notify && cloud_wrap_evt.type == CLOUD_WRAP_EVT_DISCONNECTED) {
		int err = cloud_wrap_disconnect();

		if (err) {
			LOG_ERR("cloud_wrap_disconnect, error: %d", err);
			cloud_wrap_evt.type = CLOUD_WRAP_EVT_ERROR;
		}
	}

	if (notify) {
		cloud_wrapper_notify_event(&cloud_wrap_evt);
	}
}

/* Callback handler triggered when lwm2m object resource 1/0/4 (device/reboot) is executed. */
static int device_reboot_cb(uint16_t obj_inst_id, uint8_t *args, uint16_t args_len)
{
	ARG_UNUSED(args);
	ARG_UNUSED(args_len);
	ARG_UNUSED(obj_inst_id);

	struct cloud_wrap_event cloud_wrap_evt = {
		.type = CLOUD_WRAP_EVT_REBOOT_REQUEST
	};

	cloud_wrapper_notify_event(&cloud_wrap_evt);
	return 0;
}

/* Callback handler triggered when lwm2m object resource 48934/0/3 
 * (EXACT Reclaim/reclaim) is executed. */
static int reclaim_exec_cb(uint16_t obj_inst_id, uint8_t *args, uint16_t args_len)
{
	ARG_UNUSED(args);
	ARG_UNUSED(args_len);
	ARG_UNUSED(obj_inst_id);
	int err;

	struct cloud_wrap_event cloud_wrap_evt = {
		.type = CLOUD_WRAP_EVT_RECLAIM_REQUEST
	};

	err = lwm2m_get_s32(&LWM2M_OBJ(ETC_RECLAIM_OBJECT_ID,
				       obj_inst_id, 
				       ETC_RECLAIM_OBJ_R_START_TIME),
			    &cloud_wrap_evt.reclaim.start_time_s);
	if (err) {
		LOG_ERR("get start time, err %d", err);
		return -ENOENT;
	}
	
	err = lwm2m_get_s32(&LWM2M_OBJ(ETC_RECLAIM_OBJECT_ID,
				       obj_inst_id, 
				       ETC_RECLAIM_OBJ_R_END_TIME),
			    &cloud_wrap_evt.reclaim.end_time_s);
	if (err) {
		LOG_ERR("get start time, err %d", err);
		return -ENOENT;
	}

	if (cloud_wrap_evt.reclaim.start_time_s > 
	    cloud_wrap_evt.reclaim.end_time_s) {
		LOG_ERR("start time < end time");
		return -EINVAL;
	}

	cloud_wrapper_notify_event(&cloud_wrap_evt);
	return 0;
}

static void send_cb(enum lwm2m_send_status status)
{
	struct cloud_wrap_event cloud_wrap_evt = { 0 };
	bool notify = false;

	switch (status) {
		case LWM2M_SEND_STATUS_SUCCESS:
		cloud_wrap_evt.type =  CLOUD_WRAP_EVT_DATA_SEND_ACK;
		notify = true;
		break;

		case LWM2M_SEND_STATUS_FAILURE:
		case LWM2M_SEND_STATUS_TIMEOUT:
		cloud_wrap_evt.type =  CLOUD_WRAP_EVT_DATA_SEND_FAIL;
		notify = true;
		break;
	}

	if (notify) {
		cloud_wrapper_notify_event(&cloud_wrap_evt);
	}
}

static int firmware_update_state_cb(uint8_t update_state)
{
	int err;
	uint8_t update_result;
	struct cloud_wrap_event cloud_wrap_evt = { 0 };

	/* Get the firmware object update result code */
	err = lwm2m_get_u8(&LWM2M_OBJ(5, 0, 5), &update_result);
	if (err) {
		LOG_ERR("Failed getting firmware result resource value");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_ERROR;
		cloud_wrap_evt.err = err;
		cloud_wrapper_notify_event(&cloud_wrap_evt);
		return 0;
	}

	switch (update_state) {
	case STATE_IDLE:
		LOG_DBG("STATE_IDLE, result: %d", update_result);

		/* If the FOTA state returns to its base state STATE_IDLE, the FOTA failed. */
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_FOTA_ERROR;
		break;
	case STATE_DOWNLOADING:
		LOG_DBG("STATE_DOWNLOADING, result: %d", update_result);
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_FOTA_START;
		break;
	case STATE_DOWNLOADED:
		LOG_DBG("STATE_DOWNLOADED, result: %d", update_result);
		return 0;
	case STATE_UPDATING:
		LOG_DBG("STATE_UPDATING, result: %d", update_result);
		/* Disable further callbacks from FOTA */
		lwm2m_firmware_set_update_state_cb(NULL);
		return 0;
	default:
		LOG_ERR("Unknown state: %d", update_state);
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_FOTA_ERROR;
		break;
	}

	cloud_wrapper_notify_event(&cloud_wrap_evt);
	return 0;
}

int cloud_wrap_init(cloud_wrap_evt_handler_t event_handler)
{
	int err, len;
	char hw_id_buf[ETC_SETTINGS_DEVICE_ID_LEN + 1];

#if defined(CONFIG_LWM2M_INTEGRATION_ENDPOINT_HWINFO)
	etc_get_hw_id(hw_id_buf, sizeof(hw_id_buf));
#elif defined(CONFIG_LWM2M_INTEGRATION_ENDPOINT_SERIALNUMBER)
	etc_get_device_id(hw_id_buf, sizeof(hw_id_buf));
#else
#error "Endpoint type not defined"
#endif

	strncpy(client_id_buf, hw_id_buf, sizeof(client_id_buf) - 1);

	/* Explicitly null terminate client_id_buf to be sure that we carry a
	 * null terminated buffer after strncpy().
	 */
	client_id_buf[sizeof(client_id_buf) - 1] = '\0';

	len = snprintk(endpoint_name, sizeof(endpoint_name), "%s%s",
		       CONFIG_LWM2M_INTEGRATION_ENDPOINT_PREFIX, client_id_buf);
	if ((len < 0) || (len >= sizeof(endpoint_name))) {
		return -ERANGE;
	}

	LOG_DBG("LwM2M endpoint name: %s", endpoint_name);

	struct dtls_psk psk;
	err = etc_get_psk(psk.psk, sizeof(psk.psk));
	if (err <= 0) {
		LOG_ERR("etc_get_psk error %d", err);
		return err;
	}
	psk.psk_len = (uint8_t)err;

	err = lwm2m_init_security(&client, endpoint_name, &psk);
	if (err) {
		LOG_ERR("lwm2m_init_security, error: %d", err);
		return err;
	}

	err = lwm2m_init_firmware();
	if (err) {
		LOG_ERR("lwm2m_init_firmware, error: %d", err);
		return err;
	}

	err = lwm2m_init_image();
	if (err < 0) {
		LOG_ERR("lwm2m_init_image, error: %d", err);
		return err;
	}

	err = lwm2m_register_exec_callback(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID,
						      0, DEVICE_OBJECT_REBOOT_RID),
					   device_reboot_cb);
	if (err) {
		LOG_ERR("lwm2m_engine_register_exec_callback, error: %d", err);
		return err;
	}

	err = lwm2m_register_exec_callback(&LWM2M_OBJ(ETC_RECLAIM_OBJECT_ID,
						      0, ETC_RECLAIM_OBJ_R_RECLAIM),
					   reclaim_exec_cb);
	if (err) {
		LOG_ERR("register reclaim exec callback, error: %d", err);
		return err;
	}

	lwm2m_firmware_set_update_state_cb(firmware_update_state_cb);

	wrapper_evt_handler = event_handler;
	state = DISCONNECTED;
	return 0;
}

int cloud_wrap_connect(void)
{
	int err;
	int flags = IS_ENABLED(CONFIG_LWM2M_RD_CLIENT_SUPPORT_BOOTSTRAP) ?
			LWM2M_RD_CLIENT_FLAG_BOOTSTRAP : 0;

	if (state != DISCONNECTED) {
		return -EINPROGRESS;
	}

	err = lwm2m_rd_client_start(
			&client, endpoint_name,
			flags,
			rd_client_event, NULL);
	if (err) {
		LOG_ERR("lwm2m_rd_client_start, error: %d", err);
		return err;
	}

	state = CONNECTING;
	return 0;
}

int cloud_wrap_disconnect(void)
{
	int err;
	struct cloud_wrap_event event = { 0 };

	if ((state != CONNECTED) || (state != CONNECTING)) {
		return -ENOTSUP;
	}

	err = lwm2m_rd_client_stop(&client, rd_client_event, false);
	if (err) {
		LOG_ERR("lwm2m_rd_client_stop, error: %d", err);
		return err;
	}

	event.type = CLOUD_WRAP_EVT_DISCONNECTED;
	cloud_wrapper_notify_event(&event);
	
	state = DISCONNECTED;
	return 0;
}

int cloud_wrap_pause(void)
{
	int err;
	struct cloud_wrap_event event = { 0 };

	err = lwm2m_engine_pause();
	if (err) {
		LOG_ERR("lwm2m_engine_pause, error: %d", err);
		return err;
	}	

	event.type = CLOUD_WRAP_EVT_PAUSED;
	cloud_wrapper_notify_event(&event);
	
	state = PAUSED;
	return 0;
}

int cloud_wrap_resume(void)
{
	int err;

	if (state != PAUSED) {
		return -ENOTSUP;
	}

	err = lwm2m_engine_resume();
	if (err) {
		LOG_ERR("lwm2m_engine_resume, error: %d", err);
		return err;
	}	

	state = CONNECTING;
	return 0;
}

int cloud_wrap_state_get(bool ack, uint32_t id)
{
	return -ENOTSUP;
}

int cloud_wrap_state_send(char *buf, size_t len, bool ack, uint32_t id)
{
	return -ENOTSUP;
}

int cloud_wrap_data_send(char *buf, size_t len, bool ack, uint32_t id,  
			 const struct lwm2m_obj_path path_list[])
{
	ARG_UNUSED(buf);
	ARG_UNUSED(id);

	int err;

	lwm2m_codec_helpers_path_list_log(path_list, len);

	err = lwm2m_send_cb(&client, path_list, len, send_cb);
	if (err) {
		LOG_ERR("lwm2m_send, error: %d", err);
		return err;
	}

	return 0;
}

int cloud_wrap_batch_send(char *buf, size_t len, bool ack, uint32_t id)
{
	return -ENOTSUP;
}

extern int lwm2m_engine_export_data(const struct lwm2m_obj_path path_list[], uint8_t path_list_size,
	uint8_t* out_buf, int* out_len);

int cloud_wrap_data_export(const struct lwm2m_obj_path path_list[], size_t len, uint8_t* out_buf, int* out_len) 
{
	int err;

	lwm2m_codec_helpers_path_list_log(path_list, len);
	
	err = lwm2m_engine_export_data(path_list, len, out_buf, out_len);
	if (err) {
		LOG_ERR("lwm2m_send, error: %d", err);
		return err;
	}

	return 0;
}