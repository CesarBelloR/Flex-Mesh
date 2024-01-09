#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/settings/settings.h>
#include <zephyr/random/rand32.h>
#define LOG_LEVEL LOG_LEVEL_DBG
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_ble);

#include <cJSON.h>
#include <cJSON_os.h>
#include "etc_ble.h"
#include "etc_settings.h"

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define BT_UUID_SERVICE_VAL BT_UUID_128_ENCODE(0x24eb85c0, 0x1114, 0x46fd, 0xa9a3, 0x1559361c6a95)

#define BT_UUID_SENSOR_CHAR_VAL                                                                   \
	BT_UUID_128_ENCODE(0x24eb85c1, 0x1114, 0x46fd, 0xa9a3, 0x1559361c6a95)

#define BT_UUID_RECLAIM_CHAR_VAL                                                                     \
	BT_UUID_128_ENCODE(0x24eb85c2, 0x1114, 0x46fd, 0xa9a3, 0x1559361c6a95)

#define BT_UUID_CONFIG_CHAR_VAL                                                                    \
	BT_UUID_128_ENCODE(0x24eb85c3, 0x1114, 0x46fd, 0xa9a3, 0x1559361c6a95)
	
#define BT_UUID_SERVICE BT_UUID_DECLARE_128(BT_UUID_SERVICE_VAL)
#define BT_UUID_SENSOR_CHAR BT_UUID_DECLARE_128(BT_UUID_SENSOR_CHAR_VAL)
#define BT_UUID_RECLAIM_CHAR BT_UUID_DECLARE_128(BT_UUID_RECLAIM_CHAR_VAL)
#define BT_UUID_CONFIG_CHAR BT_UUID_DECLARE_128(BT_UUID_CONFIG_CHAR_VAL)

#define BT_PAYLOAD_OFFSET     offsetof(struct flex_ble_frame, frame_payload)
#define BT_OP_OFFSET (7)
#define FLEX_BT_SENSOR_WORK_DELAY_SECONDS (5)

static void flex_ble_sensor_work_handler(struct k_work* work);
static K_WORK_DELAYABLE_DEFINE(flex_ble_sensor_work, flex_ble_sensor_work_handler);

static struct k_work advertise_work;
static char flex_device_name[CONFIG_BT_DEVICE_NAME_MAX] = { 0x00 };
static struct bt_conn *current_conn;
static uint8_t msg_id_cnt = 0;
static struct sensor_data last_sensor_data;
static struct flex_ble_frame flex_frame;
static etc_ble_evt_handler_t ble_evt_handler;
static uint8_t flex_ble_notify_sub_cnt = 0;
static bool flex_ble_is_ready = false;
static char device_id[ETC_SETTINGS_DEVICE_ID_LEN];

static uint8_t adv_data[] = {
	0x00, 0x00, 0x00, 0x00, // Probe 1
	0x00, 0x00, 0x00, 0x00, // Probe 2
	0x00, 0x00, 0x00, 0x00, // Probe 3
	0x00, 0x00, 0x00, 0x00, // Probe 4
	0x00, 0x00, 0x00, 0x00, // Ambient
	0x00, 0x00, 0x00, 0x00, // Humid 
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, adv_data, sizeof(adv_data)),
};

static ssize_t flex_sensor_on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
	uint16_t len, uint16_t offset);

static void flex_sensor_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value);

static ssize_t flex_reclaim_on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
	uint16_t len, uint16_t offset);

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
BT_GATT_SERVICE_DEFINE(flex_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_SENSOR_CHAR, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, flex_sensor_on_read,
			       NULL, NULL),
	BT_GATT_CCC(flex_sensor_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_RECLAIM_CHAR, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, NULL,
			       NULL, NULL),
	BT_GATT_CCC(flex_reclaim_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_CONFIG_CHAR, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, NULL,
			       flex_config_on_write, NULL),
	BT_GATT_CCC(flex_config_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);


static void flex_sensor_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	if (value == BT_GATT_CCC_NOTIFY) {
		flex_ble_notify_sub_cnt += 1;
	} else {
		if (flex_ble_notify_sub_cnt > 0) {
			flex_ble_notify_sub_cnt = flex_ble_notify_sub_cnt - 1;
		}
	}
	LOG_DBG("Notification has been turned %s %d", 
		value == BT_GATT_CCC_NOTIFY ? "on" : "off", flex_ble_notify_sub_cnt);
}

static void flex_reclaim_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	if (value == BT_GATT_CCC_NOTIFY) {
		flex_ble_notify_sub_cnt += 1;
	} else {
		if (flex_ble_notify_sub_cnt > 0) {
			flex_ble_notify_sub_cnt = flex_ble_notify_sub_cnt - 1;
		}
	}
	LOG_DBG("Notification has been turned %s %d", 
		value == BT_GATT_CCC_NOTIFY ? "on" : "off", flex_ble_notify_sub_cnt);
}

static void flex_config_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	if (value == BT_GATT_CCC_NOTIFY) {
		flex_ble_notify_sub_cnt += 1;
	} else {
		if (flex_ble_notify_sub_cnt > 0) {
			flex_ble_notify_sub_cnt = flex_ble_notify_sub_cnt - 1;
		}
	}
	LOG_DBG("Notification has been turned %s %d", 
		value == BT_GATT_CCC_NOTIFY ? "on" : "off", flex_ble_notify_sub_cnt);
}

static ssize_t flex_sensor_on_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
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

	/* {"request" : "reclaim", "start" : xxx, "end" : xxxx} */
	if (strstr(request_json->valuestring, "reclaim") != NULL) {
		LOG_DBG("Reclaim request");
		cJSON *start_json = cJSON_GetObjectItem(json, "start");
		cJSON *end_json = cJSON_GetObjectItem(json, "end");
		if (start_json == NULL || end_json == NULL) {
			LOG_ERR("Missing start/stop parameter");
		} else {
			int start_time = (int)start_json->valuedouble;
			int end_time = (int)end_json->valuedouble;
			if (start_time > end_time) {
				LOG_ERR("start_time > end_time");
			} else {
				LOG_DBG("Start %d - End %d", start_time, end_time);
				if (ble_evt_handler != NULL) {
					struct etc_ble_evt evt = {
						.type = ETC_BLE_EVT_CCC_RECLAIM_READY,
						.reclaim.start_time_s = start_time,
						.reclaim.end_time_s = end_time,
					};
					ble_evt_handler(&evt);
				}
			}
		}
	} else {
		LOG_WRN("Unsupported request %s", request_json->valuestring);
	}

	cJSON_Delete(json);
	return 0;
}

int flex_attr_get_index(int channel) {
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
		}

	}
	return -ENOENT;
}

K_SEM_DEFINE(flex_ble_notify_sem, 0, 1);

void flex_ble_notify_complete(struct bt_conn *conn, void *user_data)
{
	k_sem_give(&flex_ble_notify_sem);
}

static int flex_ble_notify(struct bt_conn *conn, int attr_index, const uint8_t *data, uint16_t len)
{
	struct bt_gatt_notify_params params = { 0 };
	const struct bt_gatt_attr *attr = &flex_svc.attrs[attr_index];

	params.attr = attr;
	params.data = data;
	params.len = len;
	params.func = flex_ble_notify_complete;
	if (bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY)) {
		return bt_gatt_notify_cb(conn, &params);
	}
	LOG_WRN("The UUID is not subscribed yet");
	return -EINVAL;
}

int etc_ble_notify(int channel, const uint8_t *data, uint16_t len)
{
	if (current_conn == NULL) return -ENOTCONN;
	int attr_index = flex_attr_get_index(channel);
	if (attr_index == -ENOENT) {
		LOG_ERR("The attr index is invalid");
		return -ENOENT;
	}
	int mtu_size = bt_gatt_get_mtu(current_conn) - BT_OP_OFFSET;
	int step = len / mtu_size;
	int remain = len % mtu_size;
	int rc = 0;

	uint8_t msg_id = msg_id_cnt;
	msg_id_cnt = (msg_id_cnt + 1) % 255;

	for (int i = 0; i <= step; i++) {
		int frame_len = (i == step) ? remain : mtu_size;
		flex_frame.msg_id = msg_id;
		flex_frame.frame_id = i;
		flex_frame.frame_len = (i == 0) ? len : 0;
		memcpy(flex_frame.frame_payload, &data[i * mtu_size], frame_len);
		LOG_HEXDUMP_INF((const uint8_t *)&flex_frame, frame_len, channel == ETC_BLE_SENSOR_CHAR ? "SENSOR" : "RECLAIM");
		rc = flex_ble_notify(current_conn, attr_index, (const uint8_t *)&flex_frame, BT_PAYLOAD_OFFSET + frame_len);
		if (rc) {
			LOG_ERR("Failed to notify current characteristic %d", rc);
			return rc;
		}
		k_sem_take(&flex_ble_notify_sem, K_FOREVER);
	}

	LOG_DBG("Notified success");
	return 0;
}

static void advertise(struct k_work *work)
{
	int rc;
	/* Sync last sensor data */
	int offset = 0;
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

	LOG_HEXDUMP_INF(adv_data, sizeof(adv_data), "ADV-DATA");

	bt_le_adv_stop();

	rc = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (rc) {
		LOG_ERR("Advertising failed to start (rc %d)", rc);
		return;
	}

	LOG_INF("Advertising successfully started");
}

static void flex_ble_sensor_work_handler(struct k_work* work) {
	etc_ble_notify_evt(ETC_BLE_EVT_CCC_MEASURE_READY);
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	LOG_DBG("%s: MTU exchange %s (%u)", __func__, 
		err == 0U ? "successful" : "failed",
		bt_gatt_get_mtu(conn));
}

static struct bt_gatt_exchange_params mtu_exchange_params = {
	.func = mtu_exchange_cb
};

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

#if !defined(CONFIG_BT_SMP)
	etc_ble_notify_evt(ETC_BLE_EVT_CONNECTED);
#endif
		k_work_schedule(&flex_ble_sensor_work, K_SECONDS(FLEX_BT_SENSOR_WORK_DELAY_SECONDS));
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	current_conn = NULL;
	LOG_INF("Disconnected (reason 0x%02x)", reason);
	k_work_submit(&advertise_work);
	etc_ble_notify_evt(ETC_BLE_EVT_DISCONNECTED);
}

#if defined(CONFIG_BT_SMP)
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u", addr, level);
		etc_ble_notify_evt(ETC_BLE_EVT_CONNECTED);
	} else {
		LOG_WRN("Security failed: %s level %u err %d", addr,
			level, err);
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

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};

#endif

void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	LOG_INF("Updated MTU: TX: %d RX: %d bytes", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = mtu_updated
};

static void etc_ble_notify_evt(enum etc_ble_evt_type type) {
	if (ble_evt_handler != NULL) {
		struct etc_ble_evt evt = {
			.type = type
		};
		ble_evt_handler(&evt);
	}
}

int etc_ble_init(etc_ble_evt_handler_t evt_handler) {
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

	/* Set BLE device name */
	etc_ble_set_bt_name();

	LOG_INF("Bluetooth initialized");
	memset(last_sensor_data.sensor, 0, sizeof(last_sensor_data.sensor));
	k_work_init(&advertise_work, advertise);
	k_work_submit(&advertise_work);
	etc_ble_notify_evt(ETC_BLE_EVT_DISCONNECTED);
	flex_ble_is_ready = true;
	return 0;
}

void etc_ble_set_current_sensor(struct sensor_data* data) {
	if (!flex_ble_is_ready) {
		return;
	}

	memcpy(&last_sensor_data, data, sizeof(last_sensor_data));
	if (current_conn == NULL) {
		k_work_submit(&advertise_work);
	}
}