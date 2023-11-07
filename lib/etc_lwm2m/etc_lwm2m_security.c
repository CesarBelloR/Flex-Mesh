#include <zephyr/kernel.h>
#include <zephyr/net/lwm2m.h>
#include <lwm2m_engine.h>
#include <lwm2m_rd_client.h>
#include <lwm2m_util.h>
#include <zephyr/settings/settings.h>

#include "etc_lwm2m_client_utils.h"
#include "modem_api.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lwm2m_security, CONFIG_EXACT_LWM2M_CLIENT_UTILS_LOG_LEVEL);

/* LWM2M_OBJECT_SECURITY_ID */
#define SECURITY_SERVER_URI_ID 0
#define SECURITY_BOOTSTRAP_FLAG_ID 1
#define SECURITY_MODE_ID 2
#define SECURITY_CLIENT_PK_ID 3
#define SECURITY_SERVER_PK_ID 4
#define SECURITY_SECRET_KEY_ID 5
#define SECURITY_SHORT_SERVER_ID 10

#define SECURITY_IDENTITY_LEN 128
/* LWM2M_OBJECT_SERVER_ID */
#define SERVER_SHORT_SERVER_ID 0
#define SERVER_LIFETIME_ID 1

enum security_mode {
	SEC_MODE_PSK = 0,
	SEC_MODE_CERTIFICATE = 2,
	SEC_MODE_NO_SEC = 3,
};

static bool have_permanently_stored_keys;

static int write_credentials_psk(int sec_obj_inst, int sec_tag)
{	int ret;
	struct modem_api_psk psk;

	ret = lwm2m_get_res_buf(&LWM2M_OBJ(0, sec_obj_inst, SECURITY_SECRET_KEY_ID), 
				(void **)&psk.psk, NULL, &psk.psk_len, NULL);
	if (ret < 0) {
		LOG_ERR("Unable to get resource data for '%d/%d/%d'", 0, sec_obj_inst, 
			SECURITY_SECRET_KEY_ID);
		return ret;
	}

	if (psk.psk_len == 0) {
		return -ENOENT;
	}

	ret = lwm2m_get_res_buf(&LWM2M_OBJ(0, sec_obj_inst, SECURITY_CLIENT_PK_ID), 
				(void **)&psk.psk_id, NULL, NULL, NULL);
	if (ret < 0) {
		LOG_ERR("Unable to get resource data for '%d/%d/%d'", 0, sec_obj_inst, 
			SECURITY_CLIENT_PK_ID);
		return ret;
	}
	
	ret = modem_set_credentials(DEVICE_DT_GET(DT_NODELABEL(quectel_bg95)),
				    sec_tag, MODEM_API_CRED_TYPE_PSK, &psk);
	if (ret < 0) {
		LOG_ERR("Unable to write credentials to modem (%d)", ret);
		return ret;
	}

	return 0;
}

static int write_sec_obj_to_sec_tag(int sec_obj_inst, int sec_tag, int mode)
{
	int ret;

	if (mode == SEC_MODE_PSK) {
		ret = write_credentials_psk(sec_obj_inst, sec_tag);
		if (ret) {
			goto out;
		}
	} else {
		ret = -ENOTSUP;
	}
out:
	LOG_DBG("write_sec_obj_to_sec_tag(%d, %d, %d) ret = %d", sec_obj_inst, sec_tag, mode, ret);
	return ret;
}

static int sec_mode(int sec_obj_inst)
{
	uint8_t mode;
	int ret;

	ret = lwm2m_get_u8(&LWM2M_OBJ(0, sec_obj_inst, SECURITY_MODE_ID), &mode);
	if (ret < 0) {
		return ret;
	}
	return mode;
}

static bool sec_obj_has_credentials(int sec_obj_inst)
{
	int ret;
	void *cred = NULL;
	uint16_t cred_len;
	int mode;
	struct lwm2m_obj_path path;

	mode = sec_mode(sec_obj_inst);
	if (mode < 0) {
		return false;
	}
	if (mode != SEC_MODE_CERTIFICATE && mode != SEC_MODE_PSK) {
		return false;
	}

	path = LWM2M_OBJ(0, sec_obj_inst, SECURITY_CLIENT_PK_ID);
	ret = lwm2m_get_res_buf(&path, &cred, NULL, &cred_len, NULL);
	if (ret < 0) {
		goto fail;
	}
	if (cred_len == 0) {
		return false;
	}

	path.res_id = SECURITY_SECRET_KEY_ID;
	ret = lwm2m_get_res_buf(&path, &cred, NULL, &cred_len, NULL);
	if (ret < 0) {
		goto fail;
	}

	return cred_len != 0;
fail:
	LOG_ERR("Unable to get resource data for '%d/%d/%d', rc = %d", path.obj_id,
		path.obj_inst_id, path.res_id, ret);
	return false;
}


static int load_credentials_to_modem(struct lwm2m_ctx *ctx)
{
	int ret;
	bool has_credentials;
	int mode;

	mode = sec_mode(ctx->sec_obj_inst);
	if (mode < 0) {
		return mode;
	}

	has_credentials = sec_obj_has_credentials(ctx->sec_obj_inst);

	if (!has_credentials) {
		LOG_ERR("No security credentials provisioned");
		return -ENOENT;
	}

	/* If we have set credentials on the modem previously, assume they
	 * are still correct. Any key changes would require a device reboot.
	 */
	if (have_permanently_stored_keys) {
		LOG_DBG("Existing credentials found on modem");
		return 0;
	}
	ret = write_sec_obj_to_sec_tag(ctx->sec_obj_inst, ctx->tls_tag, mode);
	if (ret < 0) {
		LOG_ERR("Failed to write credentials to modem, err %d", ret);
		goto out;
	}

	/* Mark that we have now written those keys, so
	 * reconnection does not cause another rewrite
	 */
	if (!have_permanently_stored_keys) {
		have_permanently_stored_keys = true;
	}

out:
	return ret;
}


int lwm2m_init_security(struct lwm2m_ctx *client, const char *ep_name,
			struct dtls_psk *psk)
{
	int ret;
	char *server_url;
	uint16_t server_url_len;

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
	client->tls_tag = CONFIG_EXACT_LWM2M_CLIENT_UTILS_TLS_CID;
	client->load_credentials = load_credentials_to_modem;

	lwm2m_set_string(&LWM2M_OBJ(0, 0, 3), ep_name);
	lwm2m_set_opaque(&LWM2M_OBJ(0, 0, 5),
			 psk->psk, psk->psk_len);
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