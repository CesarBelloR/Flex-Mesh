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
/* LWM2M_OBJECT_SERVER_ID */
#define SERVER_SHORT_SERVER_ID 0
#define SERVER_LIFETIME_ID 1

enum security_mode {
	SEC_MODE_PSK = 0,
	SEC_MODE_CERTIFICATE = 2,
	SEC_MODE_NO_SEC = 3,
};

static int write_credential_type(int sec_obj_inst, int sec_tag, int res_id,
				 enum modem_api_cred_type type)
{
	int ret;
	void *cred = NULL;
	uint16_t cred_len;

	ret = lwm2m_get_res_buf(&LWM2M_OBJ(0, sec_obj_inst, res_id), &cred, NULL, &cred_len,  NULL);
	if (ret < 0) {
		LOG_ERR("Unable to get resource data for '%d/%d/%d'", 0, sec_obj_inst, res_id);
		return ret;
	}

	if (cred_len == 0) {
		return -ENOENT;
	}

	ret = modem_set_credentials(DEVICE_DT_GET(DT_NODELABEL(quectel_bg95)),
				    type, cred, cred_len);
	if (ret < 0) {
		LOG_ERR("Unable to write credentials to modem (%d)", ret);
		return ret;
	}
	LOG_DBG("Written sec_tag %d, type %d", sec_tag, type);
	return 0;
}

static int write_sec_obj_to_sec_tag(int sec_obj_inst, int sec_tag, int mode)
{
	int ret;

	if (mode == SEC_MODE_PSK) {
		ret = write_credential_type(sec_obj_inst, sec_tag, SECURITY_CLIENT_PK_ID,
					    MODEM_API_CRED_TYPE_PSK_ID);
		if (ret) {
			goto out;
		}

		ret = write_credential_type(sec_obj_inst, sec_tag, SECURITY_SECRET_KEY_ID,
					    MODEM_API_CRED_TYPE_PSK);
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


int lwm2m_load_credentials_to_modem(struct lwm2m_ctx *ctx)
{
	int ret;
	bool has_credentials;
	int mode;

	if (ctx->bootstrap_mode) {
		ctx->tls_tag = 1;
	} else {
		ctx->tls_tag = 1;
	}

	mode = sec_mode(ctx->sec_obj_inst);
	if (mode < 0) {
		return mode;
	}

	has_credentials = sec_obj_has_credentials(ctx->sec_obj_inst);

	if (!has_credentials) {
		LOG_ERR("No security credentials provisioned");
		return -ENOENT;
	}

	ret = write_sec_obj_to_sec_tag(ctx->sec_obj_inst, ctx->tls_tag, mode);
	if (ret < 0) {
		LOG_ERR("Failed to write credentials to modem, err %d", ret);
		goto out;
	}
out:
	return ret;
}
