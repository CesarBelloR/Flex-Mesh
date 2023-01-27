/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
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
} state;

static cloud_wrap_evt_handler_t wrapper_evt_handler;

/* LWM2M client instance. */
static struct lwm2m_ctx client;

static char client_id_buf[ETC_SETTINGS_DEVICE_ID_LEN + 1];
static char endpoint_name[sizeof("urn:dev:mac:") +
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
		break;
	case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
		LOG_ERR("LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR");
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_ERROR;
		notify = true;
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

#if 0
/* Callback handler triggered when the modem should be put in a certain functional mode.
 * Handler is called pre provisioning of DTLS credentials when the modem should be put in
 * offline mode, and when the modem should return to normal mode after
 * provisioning has been carried out.
 */
static int modem_mode_request_cb(enum lte_lc_func_mode new_mode, void *user_data)
{
	ARG_UNUSED(user_data);

	int err;
	enum lte_lc_func_mode mode_current;
	struct cloud_wrap_event cloud_wrap_evt = { 0 };

	err = lte_lc_func_mode_get(&mode_current);
	if (err) {
		LOG_ERR("lte_lc_func_mode_get failed, error: %d", err);
		return err;
	}

	/* Return success if the modem is in the required functional mode. */
	if (mode_current == new_mode) {
		return 0;
	}

	switch (new_mode) {
	case LTE_LC_FUNC_MODE_OFFLINE:
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_LTE_DISCONNECT_REQUEST;
		break;
	case LTE_LC_FUNC_MODE_NORMAL:
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_LTE_CONNECT_REQUEST;
		break;
	default:
		LOG_ERR("Non supported modem functional mode request.");
		return -ENOTSUP;
	}

	cloud_wrapper_notify_event(&cloud_wrap_evt);

	/* If the modem is not in the required functional mode,
	 * return the time that the security object should wait before the handler is called again.
	 * Set by CONFIG_LWM2M_INTEGRATION_MODEM_MODE_REQUEST_RETRY_SECONDS.
	 */
	return CONFIG_LWM2M_INTEGRATION_MODEM_MODE_REQUEST_RETRY_SECONDS;
}

static int firmware_update_state_cb(uint8_t update_state)
{
	int err;
	uint8_t update_result;
	struct cloud_wrap_event cloud_wrap_evt = { 0 };

	/* Get the firmware object update result code */
	err = lwm2m_engine_get_u8(FIRMWARE_UPDATE_RESULT_PATH, &update_result);
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
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_FOTA_DONE;
		break;
	default:
		LOG_ERR("Unknown state: %d", update_state);
		cloud_wrap_evt.type = CLOUD_WRAP_EVT_FOTA_ERROR;
		break;
	}

	cloud_wrapper_notify_event(&cloud_wrap_evt);
	return 0;
}
#endif

static int lwm2m_init_security(struct lwm2m_ctx *client, const char *ep_name)
{
	int ret;
	char *server_url;
	uint16_t server_url_len;
	uint8_t client_psk[CONFIG_LWM2M_SECURITY_KEY_SIZE];

	/* Server URL */
	ret = lwm2m_get_res_buf(&LWM2M_OBJ(LWM2M_OBJECT_SECURITY_ID, 0,
					   SECURITY_SERVER_URI_ID),
				(void **)&server_url, &server_url_len, NULL, NULL);
	if (ret < 0) {
		return ret;
	}

	server_url_len = snprintk(server_url, server_url_len, "%s",
				  CONFIG_LWM2M_INTEGRATION_SERVER_URL);
	lwm2m_set_res_data_len(&LWM2M_OBJ(LWM2M_OBJECT_SECURITY_ID, 0,
					  SECURITY_SERVER_URI_ID),
			       server_url_len + 1);

	/* Security Mode */
	lwm2m_set_u8(&LWM2M_OBJ(LWM2M_OBJECT_SECURITY_ID, 0, SECURITY_MODE_ID), 
		     IS_ENABLED(CONFIG_LWM2M_DTLS_SUPPORT) ? 0 : 3);

#if defined(CONFIG_LWM2M_DTLS_SUPPORT)
	hex2bin(CONFIG_LWM2M_INTEGRATION_PSK, sizeof(CONFIG_LWM2M_INTEGRATION_PSK) - 1,
			client_psk, sizeof(client_psk));
	lwm2m_set_string(&LWM2M_OBJ(0, 0, 3), CONFIG_LWM2M_INTEGRATION_PSK_ID);
	lwm2m_set_opaque(&LWM2M_OBJ(0, 0, 5),
			 (void *)client_psk, sizeof(client_psk));
#endif /* CONFIG_LWM2M_DTLS_SUPPORT */
#if CONFIG_LWM2M_RD_CLIENT_SUPPORT_BOOTSTRAP
	/* Mark 1st instance of security object as a bootstrap server */
	lwm2m_set_u8(&LWM2M_OBJ(0, 0, 1), 1);

	/* Create 2nd instance of security object needed for bootstrap */
	lwm2m_create_object_inst(&LWM2M_OBJ(0, 1));
#else
	/* Set short server id.
	 */
	lwm2m_set_u16(&LWM2M_OBJ(0, 0, 10), CONFIG_LWM2M_SERVER_DEFAULT_SSID);
	lwm2m_set_u16(&LWM2M_OBJ(1, 0, 0), CONFIG_LWM2M_SERVER_DEFAULT_SSID);
#endif
	return 0;
}

int cloud_wrap_init(cloud_wrap_evt_handler_t event_handler)
{
	int err, len;
	char hw_id_buf[ETC_SETTINGS_DEVICE_ID_LEN + 1];

	etc_get_device_id(hw_id_buf, sizeof(hw_id_buf));

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

	err = lwm2m_init_security(&client, endpoint_name);
	if (err) {
		LOG_ERR("lwm2m_init_security, error: %d", err);
		return err;
	}

	err = lwm2m_register_exec_callback(&LWM2M_OBJ(LWM2M_OBJECT_DEVICE_ID,
						      0, DEVICE_OBJECT_REBOOT_RID),
					   device_reboot_cb);
	if (err) {
		LOG_ERR("lwm2m_engine_register_exec_callback, error: %d", err);
		return err;
	}

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

	if (state != CONNECTED) {
		return -ENOTSUP;
	}

	err = lwm2m_rd_client_stop(&client, rd_client_event, false);
	if (err) {
		LOG_ERR("lwm2m_rd_client_stop, error: %d", err);
		return err;
	}

	state = DISCONNECTED;
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
			 struct lwm2m_obj_path path_list[])
{
	ARG_UNUSED(buf);
	ARG_UNUSED(id);

	int err;

	err = lwm2m_send(&client, path_list, len, ack);
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