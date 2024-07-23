#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/random/random.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/mgmt/callbacks.h>
#define LOG_LEVEL LOG_LEVEL_DBG
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_ble);

#include <cJSON.h>
#include <cJSON_os.h>
#include "etc_ble.h"
#include "etc_battery.h"
#include "etc_settings.h"
#include "etc_util.h"
#include "ble_helpers.h"

#define DEVICE_NAME	CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

enum {
	FLEX_CCC_SUBSCRIBED,
	FLEX_CCC_NUM_FLAGS,
};

ATOMIC_DEFINE(flex_ccc_sensor, FLEX_CCC_NUM_FLAGS);
ATOMIC_DEFINE(flex_ccc_config, FLEX_CCC_NUM_FLAGS);
ATOMIC_DEFINE(flex_ccc_reclaim, FLEX_CCC_NUM_FLAGS);
K_SEM_DEFINE(flex_ble_notify_sem, 0, 1);

#define BT_UUID_SERVICE_VAL BT_UUID_128_ENCODE(0x24eb85c0, 0x1114, 0x46fd, 0xa9a3, 0x1559361c6a95)

#define BT_UUID_SENSOR_CHAR_VAL                                                                    \
	BT_UUID_128_ENCODE(0x24eb85c1, 0x1114, 0x46fd, 0xa9a3, 0x1559361c6a95)

#define BT_UUID_RECLAIM_CHAR_VAL                                                                   \
	BT_UUID_128_ENCODE(0x24eb85c2, 0x1114, 0x46fd, 0xa9a3, 0x1559361c6a95)

#define BT_UUID_CONFIG_CHAR_VAL                                                                    \
	BT_UUID_128_ENCODE(0x24eb85c3, 0x1114, 0x46fd, 0xa9a3, 0x1559361c6a95)

#define BT_UUID_SERVICE	     BT_UUID_DECLARE_128(BT_UUID_SERVICE_VAL)
#define BT_UUID_SENSOR_CHAR  BT_UUID_DECLARE_128(BT_UUID_SENSOR_CHAR_VAL)
#define BT_UUID_RECLAIM_CHAR BT_UUID_DECLARE_128(BT_UUID_RECLAIM_CHAR_VAL)
#define BT_UUID_CONFIG_CHAR  BT_UUID_DECLARE_128(BT_UUID_CONFIG_CHAR_VAL)

#define BT_PAYLOAD_OFFSET		  offsetof(struct flex_ble_frame, frame_payload)
#define BT_OP_OFFSET			  (7)
#define FLEX_BT_SENSOR_WORK_DELAY_SECONDS (5)

static void flex_ble_sensor_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(flex_ble_sensor_work, flex_ble_sensor_work_handler);

static void flex_ble_adv_magnet_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(flex_ble_adv_magnet_work, flex_ble_adv_magnet_work_handler);

static struct k_work advertise_work;
static char flex_device_name[CONFIG_BT_DEVICE_NAME_MAX] = {0x00};
static struct bt_conn *current_conn;
static uint8_t msg_id_cnt = 0;
static struct sensor_data last_sensor_data;
static struct flex_ble_frame flex_frame;
static etc_ble_evt_handler_t ble_evt_handler;
static bool flex_ble_is_adversting = false;
static bool flex_ble_is_magnet_trigger = false;
static char device_id[ETC_SETTINGS_DEVICE_ID_LEN];

static uint8_t adv_data[] = {
	0x00, 0x00, 0x00, 0x00, // Probe 1
	0x00, 0x00, 0x00, 0x00, // Probe 2
	0x00, 0x00, 0x00, 0x00, // Probe 3
	0x00, 0x00, 0x00, 0x00, // Probe 4
	0x00, 0x00, 0x00, 0x00, // Ambient
	0x00, 0x00, 0x00, 0x00, // Humid 
	0x00, // Battery status
	0x00, // Battery
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, adv_data, sizeof(adv_data)),
};

static ssize_t flex_sensor_on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				   uint16_t len, uint16_t offset);

static ssize_t flex_config_on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				   uint16_t len, uint16_t offset);

static void flex_sensor_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);

static ssize_t flex_reclaim_on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				    void *buf, uint16_t len, uint16_t offset);

static void flex_reclaim_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);

static ssize_t flex_config_on_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				    const void *buf, uint16_t len, uint16_t offset, uint8_t flags);

static void flex_config_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);

static void etc_ble_notify_evt(enum etc_ble_evt_type type);

static void etc_ble_set_bt_name(void)
{
	etc_get_device_id(device_id, sizeof(device_id));
	snprintf(flex_device_name, sizeof(flex_device_name), "Flex_%s", device_id);
	int err = bt_set_name(flex_device_name);
	if (err) {
		LOG_ERR("Can't set BLE device name");
	}
}

/* Flex Service Declaration */
BT_GATT_SERVICE_DEFINE(
	flex_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_SENSOR_CHAR, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
#if defined(CONFIG_BT_SMP)
			       BT_GATT_PERM_READ_ENCRYPT, flex_sensor_on_read,
#else
			       BT_GATT_PERM_READ, flex_sensor_on_read,
#endif
			       NULL, NULL),
#if defined(CONFIG_BT_SMP)
	BT_GATT_CCC(flex_sensor_ccc_cfg_changed,
		    BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
#else
	BT_GATT_CCC(flex_sensor_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
#endif
	BT_GATT_CHARACTERISTIC(BT_UUID_RECLAIM_CHAR, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
#if defined(CONFIG_BT_SMP)
			       BT_GATT_PERM_READ_ENCRYPT, NULL,
#else
			       BT_GATT_PERM_READ, NULL,
#endif
			       NULL, NULL),
#if defined(CONFIG_BT_SMP)
	BT_GATT_CCC(flex_reclaim_ccc_cfg_changed,
		    BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
#else
	BT_GATT_CCC(flex_reclaim_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
#endif
	BT_GATT_CHARACTERISTIC(BT_UUID_CONFIG_CHAR,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
#if defined(CONFIG_BT_SMP)
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
			       flex_config_on_read,
#else
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, flex_config_on_read,
#endif
			       flex_config_on_write, NULL),
#if defined(CONFIG_BT_SMP)
	BT_GATT_CCC(flex_config_ccc_cfg_changed,
		    BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
#else
	BT_GATT_CCC(flex_config_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
#endif
);

static void flex_sensor_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	if (value == BT_GATT_CCC_NOTIFY) {
		atomic_set_bit(flex_ccc_sensor, FLEX_CCC_SUBSCRIBED);
		k_work_schedule(&flex_ble_sensor_work,
				K_SECONDS(FLEX_BT_SENSOR_WORK_DELAY_SECONDS));
	} else {
		atomic_clear_bit(flex_ccc_reclaim, FLEX_CCC_SUBSCRIBED);
	}
	LOG_DBG("Notification has been turned %s", value == BT_GATT_CCC_NOTIFY ? "on" : "off");
}

static void flex_reclaim_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	if (value == BT_GATT_CCC_NOTIFY) {
		atomic_set_bit(flex_ccc_reclaim, FLEX_CCC_SUBSCRIBED);
	} else {
		atomic_clear_bit(flex_ccc_reclaim, FLEX_CCC_SUBSCRIBED);
	}
	LOG_DBG("Notification has been turned %s", value == BT_GATT_CCC_NOTIFY ? "on" : "off");
}

static void flex_config_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	if (value == BT_GATT_CCC_NOTIFY) {
		atomic_set_bit(flex_ccc_config, FLEX_CCC_SUBSCRIBED);
	} else {
		atomic_clear_bit(flex_ccc_config, FLEX_CCC_SUBSCRIBED);
	}
	LOG_DBG("Notification has been turned %s", value == BT_GATT_CCC_NOTIFY ? "on" : "off");
}

static ssize_t flex_sensor_on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				   uint16_t len, uint16_t offset)
{
	return 0;
}

static ssize_t flex_config_on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				   uint16_t len, uint16_t offset)
{
	return 0;
}

static ssize_t flex_config_on_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				    const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	LOG_HEXDUMP_INF(buf, len, "Flex Config");
	cJSON *json = cJSON_ParseWithLength(buf, len);
	if (json == NULL) {
		LOG_ERR("Failed in parsering config command");
		return -EINVAL;
	}

	cJSON *request_json = cJSON_GetObjectItem(json, "request");
	if (request_json == NULL) {
		LOG_ERR("Failed in parsering request information");
		cJSON_Delete(json);
		return -EINVAL;
	}

	if (strstr(request_json->valuestring, "reclaim") != NULL) {
		ble_helpers_handle_reclaim_request(json, ble_evt_handler);
	} else if (strstr(request_json->valuestring, "query") != NULL) {
		ble_helpers_handle_query_request(json, ble_evt_handler);
	} else {
		LOG_WRN("Unsupported request %s", request_json->valuestring);
	}

	cJSON_Delete(json);
	return 0;
}

int flex_attr_get_index(int channel)
{
	for (int i = 0; i < flex_svc.attr_count; i++) {
		const struct bt_gatt_attr *attr = &flex_svc.attrs[i];
		if (channel == ETC_BLE_SENSOR_CHAR) {
			if (bt_uuid_cmp(BT_UUID_SENSOR_CHAR, attr->uuid) == 0) {
				return i;
			}
		} else if (channel == ETC_BLE_RECLAIM_CHAR) {
			if (bt_uuid_cmp(BT_UUID_RECLAIM_CHAR, attr->uuid) == 0) {
				return i;
			}
		} else if (channel == ETC_BLE_CONFIG_CHAR) {
			if (bt_uuid_cmp(BT_UUID_CONFIG_CHAR, attr->uuid) == 0) {
				return i;
			}
		}
	}
	return -ENOENT;
}

void flex_ble_notify_complete(struct bt_conn *conn, void *user_data)
{
	k_sem_give(&flex_ble_notify_sem);
}

static int flex_ble_notify(struct bt_conn *conn, int attr_index, const uint8_t *data, uint16_t len)
{
	struct bt_gatt_notify_params params = {0};
	const struct bt_gatt_attr *attr = &flex_svc.attrs[attr_index];

	params.attr = attr;
	params.data = data;
	params.len = len;
	params.func = flex_ble_notify_complete;

	int ccc_sensor_index = flex_attr_get_index(ETC_BLE_SENSOR_CHAR);
	int ccc_reclaim_index = flex_attr_get_index(ETC_BLE_RECLAIM_CHAR);
	int ccc_config_index = flex_attr_get_index(ETC_BLE_CONFIG_CHAR);

	if (ccc_sensor_index == attr_index) {
		if (atomic_test_bit(flex_ccc_sensor, FLEX_CCC_SUBSCRIBED)) {
			return bt_gatt_notify_cb(conn, &params);
		}
	} else if (ccc_reclaim_index == attr_index) {
		if (atomic_test_bit(flex_ccc_reclaim, FLEX_CCC_SUBSCRIBED)) {
			return bt_gatt_notify_cb(conn, &params);
		}
	} else if (ccc_config_index == attr_index) {
		if (atomic_test_bit(flex_ccc_config, FLEX_CCC_SUBSCRIBED)) {
			return bt_gatt_notify_cb(conn, &params);
		}
	}

	LOG_WRN("The UUID is not subscribed yet");
	return -EINVAL;
}

#ifdef CONFIG_ETC_BLE_ENCRYPTION
static uint8_t *etc_ble_encrypt_data(const uint8_t *data, uint16_t len, uint16_t *encrypted_len)
{
	uint16_t max_encrypted_len = (len / AES_KEY_BLOCK_SIZE + 1) * AES_KEY_BLOCK_SIZE;
	uint8_t *out_buf = (uint8_t *)k_malloc(max_encrypted_len);
	if (out_buf == NULL) {
		LOG_ERR("Failed to allocate memory for encrypting data");
		*encrypted_len = 0;
		return NULL;
	}

	uint8_t buf[ETC_SETTING_PSK_LEN] = {0x00};
	etc_get_psk(buf, ETC_SETTING_PSK_LEN);
	int ret = encrypt_data(buf, data, len, out_buf);
	if (ret > 0) {
		*encrypted_len = ret;
		LOG_DBG("Encrypted data with length %d (input %d)", ret, len);
		return out_buf;
	} else {
		LOG_ERR("Can't encrypted data %d", ret);
	}

	return NULL;
}
#endif

int etc_ble_notify(int channel, const uint8_t *data, uint16_t len, bool need_encrypt)
{
	if (current_conn == NULL)
		return -ENOTCONN;
	int attr_index = flex_attr_get_index(channel);
	if (attr_index == -ENOENT) {
		LOG_ERR("The attr index is invalid");
		return -ENOENT;
	}
	int mtu_size = bt_gatt_get_mtu(current_conn) - BT_OP_OFFSET;

	uint16_t encrypted_len = 0;
	uint8_t *encrypted_buf;
	if (need_encrypt) {
#ifdef CONFIG_ETC_BLE_ENCRYPTION
		encrypted_buf = etc_ble_encrypt_data(data, len, &encrypted_len);
		if (encrypted_buf == NULL) {
			return -EINVAL;
		}
#else
		encrypted_len = len;
		encrypted_buf = (uint8_t *)data;
#endif
	} else {
		encrypted_len = len;
		encrypted_buf = (uint8_t *)data;
	}

	int step = encrypted_len / mtu_size;
	int remain = encrypted_len % mtu_size;
	int rc = 0;

	uint8_t msg_id = msg_id_cnt;
	msg_id_cnt = (msg_id_cnt + 1) % 255;

	for (int i = 0; i <= step; i++) {
		int frame_len = (i == step) ? remain : mtu_size;
		if (frame_len == 0) {
			break;
		}
		flex_frame.msg_id = msg_id;
		flex_frame.frame_id = i;
		flex_frame.frame_len = (i == 0) ? encrypted_len : 0;
		memcpy(flex_frame.frame_payload, &encrypted_buf[i * mtu_size], frame_len);
		LOG_HEXDUMP_INF(&flex_frame, BT_PAYLOAD_OFFSET + frame_len, "DATA");
		rc = flex_ble_notify(current_conn, attr_index, (const uint8_t *)&flex_frame,
				     BT_PAYLOAD_OFFSET + frame_len);
		if (rc) {
			LOG_ERR("Failed to notify current characteristic %d", rc);
			goto done;
		}
		k_sem_take(&flex_ble_notify_sem, K_FOREVER);
	}

	LOG_DBG("Notified success");
done:
	if (need_encrypt) {
#ifdef CONFIG_ETC_BLE_ENCRYPTION
		if (encrypted_buf) {
			k_free(encrypted_buf);
		}
#endif
	}

	return rc;
}

static void advertise(struct k_work *work)
{
	int rc;
	/* Sync last sensor data */
	int offset = 0;
	uint8_t battery = 0;
	enum battery_status bat_status = etc_battery_get_status();
	memcpy(&adv_data[offset], &last_sensor_data.sensor[SENSOR_INPUT_IN1], sizeof(float));
	offset += sizeof(float);
	memcpy(&adv_data[offset], &last_sensor_data.sensor[SENSOR_INPUT_IN2], sizeof(float));
	offset += sizeof(float);
	memcpy(&adv_data[offset], &last_sensor_data.sensor[SENSOR_INPUT_IN3], sizeof(float));
	offset += sizeof(float);
	memcpy(&adv_data[offset], &last_sensor_data.sensor[SENSOR_INPUT_IN4], sizeof(float));
	offset += sizeof(float);
	memcpy(&adv_data[offset], &last_sensor_data.sensor[SENSOR_INPUT_AMBIENT], sizeof(float));
	offset += sizeof(float);
	memcpy(&adv_data[offset], &last_sensor_data.sensor[SENSOR_INPUT_HUMID], sizeof(float));
	offset += sizeof(float);
	memcpy(&adv_data[offset], &bat_status, sizeof(enum battery_status));
	offset += sizeof(enum battery_status);
	battery = etc_battery_percentage_from_voltage(last_sensor_data.battery_mV);
	memcpy(&adv_data[offset], &battery, sizeof(uint8_t));
	LOG_HEXDUMP_INF(adv_data, sizeof(adv_data), "ADV-DATA");

	bt_le_adv_stop();

	rc = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (rc) {
		LOG_ERR("Advertising failed to start (rc %d)", rc);
		return;
	}

	LOG_INF("Advertising successfully started");
}

static void flex_ble_sensor_work_handler(struct k_work *work)
{
	etc_ble_notify_evt(ETC_BLE_EVT_CCC_MEASURE_READY);
}

static void flex_ble_adv_magnet_work_handler(struct k_work *work)
{
	flex_ble_is_magnet_trigger = false;
	flex_ble_is_adversting = false;
	/* Stop adv */
	bt_le_adv_stop();
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	LOG_DBG("%s: MTU exchange %s (%u)", __func__, err == 0U ? "successful" : "failed",
		bt_gatt_get_mtu(conn));
}

static struct bt_gatt_exchange_params mtu_exchange_params = {.func = mtu_exchange_cb};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (err 0x%02x)", err);
	} else {
		LOG_INF("Connected to peer - Need to pair/bond");
		current_conn = conn;
		int rc = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
		if (rc) {
			LOG_ERR("Can't exchange MTU request %d", rc);
		}

		/* Cancel work scheduler for stop advertising */
		if (flex_ble_is_magnet_trigger) {
			k_work_cancel_delayable(&flex_ble_adv_magnet_work);
		}

#if !defined(CONFIG_BT_SMP)
		etc_ble_notify_evt(ETC_BLE_EVT_CONNECTED);
#endif
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	current_conn = NULL;
	LOG_INF("Disconnected (reason 0x%02x)", reason);
	/* Clear all bits */
	atomic_clear_bit(flex_ccc_config, FLEX_CCC_SUBSCRIBED);
	atomic_clear_bit(flex_ccc_sensor, FLEX_CCC_SUBSCRIBED);
	atomic_clear_bit(flex_ccc_reclaim, FLEX_CCC_SUBSCRIBED);

	/* Submit work for advertise */
	k_work_submit(&advertise_work);
	/* Restart the scheduler for magnet advertising */
	if (flex_ble_is_magnet_trigger) {
		k_work_reschedule(&flex_ble_adv_magnet_work,
				  K_SECONDS(CONFIG_ETC_BLE_ADV_MAGNET_TIMEOUT_SEC));
	}
	etc_ble_notify_evt(ETC_BLE_EVT_DISCONNECTED);
}

#if defined(CONFIG_BT_SMP)
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u", addr, level);
		etc_ble_notify_evt(ETC_BLE_EVT_CONNECTED);
	} else {
		LOG_WRN("Security failed: %s level %u err %d", addr, level, err);
		(void)bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(conn));
	}
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
#if defined(CONFIG_BT_SMP)
	.security_changed = security_changed,
#endif
};

#if defined(CONFIG_BT_SMP)
static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Pairing cancelled: %s", addr);
	etc_ble_notify_evt(ETC_BLE_EVT_ERR);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Pairing failed conn: %s, reason %d", addr, reason);
	etc_ble_notify_evt(ETC_BLE_EVT_ERR);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {.pairing_complete = pairing_complete,
							       .pairing_failed = pairing_failed};

#endif

void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	LOG_INF("Updated MTU: TX: %d RX: %d bytes", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

static void etc_ble_notify_evt(enum etc_ble_evt_type type)
{
	if (ble_evt_handler != NULL) {
		struct etc_ble_evt evt = {.type = type};
		ble_evt_handler(&evt);
	}
}

static void etc_ble_set_serial(void)
{
#if defined(CONFIG_BT_DIS_SETTINGS)
	char hw_id[ETC_SETTINGS_DEVICE_ID_LEN + 1];
	char serial_number[CONFIG_BT_DIS_STR_MAX + 1];
	int len;

#if defined(CONFIG_LWM2M_INTEGRATION_ENDPOINT_HWINFO)
	etc_get_hw_id(hw_id, sizeof(hw_id));
#elif defined(CONFIG_LWM2M_INTEGRATION_ENDPOINT_SERIALNUMBER)
	etc_get_device_id(hw_id, sizeof(hw_id));
#else
#error "Endpoint type not defined"
#endif

	len = snprintk(serial_number, sizeof(serial_number), "%s%s",
		       CONFIG_LWM2M_INTEGRATION_ENDPOINT_PREFIX, hw_id);

	if ((len < 0) || (len >= sizeof(serial_number))) {
		LOG_WRN("Buffer too small");
	}

	LOG_DBG("Set serial number: %s", serial_number);
	settings_save_one("bt/dis/serial", serial_number, strlen(serial_number));
	settings_load_subtree("bt/dis");
#endif
}

enum mgmt_cb_return mgmt_on_evt(uint32_t event, enum mgmt_cb_return prev_status,
		 int32_t *rc, uint16_t *group, bool *abort_more, void *data,
		 size_t data_size)
{
	switch (event) {
		case MGMT_EVT_OP_IMG_MGMT_DFU_STARTED:
			etc_ble_notify_evt(ETC_BLE_EVT_FOTA_START);
			break;
		case MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED:
			etc_ble_notify_evt(ETC_BLE_EVT_FOTA_DONE);
			break;
		default:
			break;
	}
	return MGMT_CB_OK;
}

static struct mgmt_callback ble_img_mgmt_callback = {
	.callback = mgmt_on_evt,
	.event_id = MGMT_EVT_OP_IMG_MGMT_DFU_STARTED | MGMT_EVT_OP_IMG_MGMT_DFU_STOPPED,
};

int etc_ble_init(etc_ble_evt_handler_t evt_handler)
{
	int rc = 0;

	if (evt_handler != NULL) {
		ble_evt_handler = evt_handler;
	}

#if defined(CONFIG_BT_SMP)
	rc = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (rc) {
		LOG_ERR("Failed to register authorization callbacks");
		return 0;
	}

	rc = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (rc) {
		printk("Failed to register authorization info callbacks.");
		return 0;
	}
#endif
	bt_gatt_cb_register(&gatt_callbacks);
	etc_ble_set_serial();
	/* Enable Bluetooth. */
	rc = bt_enable(NULL);
	if (rc != 0) {
		LOG_ERR("Bluetooth init failed (err %d)", rc);
		etc_ble_notify_evt(ETC_BLE_EVT_ERR);
		return rc;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		rc = settings_load();
		if (rc) {
			LOG_ERR("settings load failed (err %d)", rc);
			etc_ble_notify_evt(ETC_BLE_EVT_ERR);
			return rc;
		}
	}

	/* Initialize the Bluetooth mcumgr transport. */
	smp_bt_register();

	/* Register callback for mcumgr img */
	mgmt_callback_register(&ble_img_mgmt_callback);

	/* Set BLE device name */
	etc_ble_set_bt_name();

	LOG_INF("Bluetooth initialized");
	memset(last_sensor_data.sensor, 0, sizeof(last_sensor_data.sensor));
	k_work_init(&advertise_work, advertise);
	etc_ble_notify_evt(ETC_BLE_EVT_DISCONNECTED);
	flex_ble_is_adversting = false;
	return 0;
}

void etc_ble_start_adv(void)
{
	/* Force to cancel this work. Since BLE mode always advertise */
	k_work_cancel_delayable(&flex_ble_adv_magnet_work);
	flex_ble_is_magnet_trigger = false;
	flex_ble_is_adversting = true;
	k_work_submit(&advertise_work);
}

void etc_ble_stop_adv(void)
{
	if (current_conn != NULL) {
		bt_le_adv_stop();
	} else {
		bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_le_adv_stop();
	}
}

void etc_ble_start_adv_with_timeout(void)
{
	if (current_conn != NULL) {
		return;
	}

	flex_ble_is_adversting = true;

	if (etc_get_device_mode() != ETC_DEVICE_MODE_BLE) {
		flex_ble_is_magnet_trigger = true;
		k_work_schedule(&flex_ble_adv_magnet_work,
				K_SECONDS(CONFIG_ETC_BLE_ADV_MAGNET_TIMEOUT_SEC));
	}
	k_work_submit(&advertise_work);
}

void etc_ble_set_current_sensor(struct sensor_data *data)
{
	if (!flex_ble_is_adversting) {
		return;
	}

	memcpy(&last_sensor_data, data, sizeof(last_sensor_data));
	if (current_conn == NULL) {
		k_work_submit(&advertise_work);
	}
}

bool etc_ble_get_is_connected(void)
{
	return (current_conn != NULL);
}

int etc_ble_notify_reclaim_status(int reclaim_status)
{
	char *response_msg =
		ble_helpers_prepare_response("reclaim", "reclaim", false, reclaim_status);
	if (response_msg == NULL) {
		return -EINVAL;
	}

	LOG_INF("Response message %s", response_msg);
	etc_ble_notify(ETC_BLE_CONFIG_CHAR, response_msg, strlen(response_msg), false);
	cJSON_free(response_msg);
	return 0;
}

int etc_ble_notify_query_reclaim(int reclaim_status)
{
	char *response_msg = ble_helpers_prepare_response("query", "reclaim", true, reclaim_status);
	if (response_msg == NULL) {
		return -EINVAL;
	}

	LOG_INF("Response message %s", response_msg);
	etc_ble_notify(ETC_BLE_CONFIG_CHAR, response_msg, strlen(response_msg), false);
	cJSON_free(response_msg);
	return 0;
}

int etc_ble_notify_battery(uint8_t level)
{
	if (etc_ble_get_is_connected()) {
		return bt_bas_set_battery_level(level);
	}
	return 0;
}

static const char *etc_ble_error_type_to_string(int type)
{
	switch (type) {
	case ETC_BLE_ERR_RECLAIM_TYPE:
		return "reclaim";
	case ETC_BLE_ERR_QUERY_TYPE:
		return "query";
	case ETC_BLE_ERR_RETRIEVE_TYPE:
		return "retrieve";
	}
	return "unknown";
}

int etc_ble_notify_error(int type, int error)
{
	char *response_msg = ble_helpers_prepare_response(
		"error", etc_ble_error_type_to_string(type), false, error);
	if (response_msg == NULL) {
		LOG_ERR("Can't create response object");
		return -EINVAL;
	}

	LOG_INF("Response message %s", response_msg);
	etc_ble_notify(ETC_BLE_CONFIG_CHAR, response_msg, strlen(response_msg), false);
	cJSON_free(response_msg);

	return 0;
}

int etc_ble_notify_status(int type, int status)
{
	char *response_msg = ble_helpers_prepare_response(
		"status", etc_ble_error_type_to_string(type), false, status);
	if (response_msg == NULL) {
		LOG_ERR("Can't create response object");
		return -EINVAL;
	}

	LOG_INF("Response message %s", response_msg);
	etc_ble_notify(ETC_BLE_CONFIG_CHAR, response_msg, strlen(response_msg), false);
	cJSON_free(response_msg);

	return 0;
}
