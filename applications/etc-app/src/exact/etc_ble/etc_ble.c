#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/settings/settings.h>
#include <zephyr/random/rand32.h>
#define LOG_LEVEL LOG_LEVEL_DBG
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_ble);

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

static struct k_work advertise_work;
static char flex_device_name[CONFIG_BT_DEVICE_NAME_MAX] = { 0x00 };
static struct bt_conn *current_conn;
static struct sensor_data last_sensor_data;
static struct flex_ble_frame flex_frame;
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
			       BT_GATT_PERM_READ_ENCRYPT, flex_sensor_on_read,
			       NULL, NULL),
	BT_GATT_CCC(flex_sensor_ccc_cfg_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_CHARACTERISTIC(BT_UUID_RECLAIM_CHAR, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT, NULL,
			       NULL, NULL),
	BT_GATT_CCC(flex_reclaim_ccc_cfg_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_CHARACTERISTIC(BT_UUID_CONFIG_CHAR, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, NULL,
			       flex_config_on_write, NULL),
	BT_GATT_CCC(flex_config_ccc_cfg_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT)
);


static void flex_sensor_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	LOG_DBG("Notification has been turned %s", value == BT_GATT_CCC_NOTIFY ? "on" : "off");
}

static void flex_reclaim_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	LOG_DBG("Notification has been turned %s", value == BT_GATT_CCC_NOTIFY ? "on" : "off");
}

static void flex_config_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	LOG_DBG("Notification has been turned %s", value == BT_GATT_CCC_NOTIFY ? "on" : "off");
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
	return 0;
}

int flex_attr_get_index(const struct bt_uuid *uuid) {
	char uuid_str[64] = {0x0};
	for (int i = 0; i < flex_svc.attr_count; i++) {
		const struct bt_gatt_attr *attr = &flex_svc.attrs[i];
		bt_uuid_to_str(attr->uuid, uuid_str, sizeof(uuid_str));
		// LOG_INF("UUID %s", uuid_str);
		if (bt_uuid_cmp(uuid, attr->uuid) == 0) {
			return i;
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

int etc_ble_sensor_notify(const uint8_t *data, uint16_t len)
{
	if (current_conn == NULL) return -ENOTCONN;
	int attr_index = flex_attr_get_index(BT_UUID_SENSOR_CHAR);
	int step = len / ETC_BLE_FRAME_PAYLOAD_MAX_LEN;
	int remain = len % ETC_BLE_FRAME_PAYLOAD_MAX_LEN;
	int rc = 0;
	for (int i = 0; i <= step; i++) {
		int frame_len = (i == step) ? remain : ETC_BLE_FRAME_PAYLOAD_MAX_LEN;
		flex_frame.msg_id = 0;
		flex_frame.frame_id = i;
		flex_frame.frame_len = (i == 0) ? len : 0;
		memcpy(flex_frame.frame_payload, &data[i * ETC_BLE_FRAME_PAYLOAD_MAX_LEN], frame_len);
		LOG_HEXDUMP_INF((const uint8_t *)&flex_frame, sizeof(flex_frame), "BLE_SENSOR");
		rc = flex_ble_notify(current_conn, attr_index, (const uint8_t *)&flex_frame, BT_PAYLOAD_OFFSET + frame_len);
		if (rc) {
			LOG_ERR("Failed to notify UUID_SENSOR_CHAR %d", rc);
			return rc;
		}
		k_sem_take(&flex_ble_notify_sem, K_FOREVER);
	}

	LOG_DBG("Notified UUID_SENSOR_CHAR success");
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
		LOG_INF("Connected");
		current_conn = conn;
		int rc = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
		if (rc) {
			LOG_ERR("Can't exchange MTU request %d", rc);
		}
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	current_conn = NULL;
	LOG_INF("Disconnected (reason 0x%02x)", reason);
	k_work_submit(&advertise_work);
}

#if defined(CONFIG_BT_SMP)
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u", addr, level);
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

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Pairing cancelled: %s", addr);
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
}

void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	LOG_INF("Updated MTU: TX: %d RX: %d bytes", tx, rx);
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = mtu_updated
};

int etc_ble_init(void) {
	int rc = 0;
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

	bt_gatt_cb_register(&gatt_callbacks);

	/* Enable Bluetooth. */
	rc = bt_enable(NULL);
	if (rc != 0) {
		LOG_ERR("Bluetooth init failed (err %d)", rc);
		return rc;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		rc = settings_load();
		if (rc) {
			LOG_ERR("settings load failed (err %d)", rc);
			return rc;
		}
	}

	/* Set BLE device name */
	etc_ble_set_bt_name();

	LOG_INF("Bluetooth initialized");
	memset(last_sensor_data.sensor, 0, sizeof(last_sensor_data.sensor));
	k_work_init(&advertise_work, advertise);
	k_work_submit(&advertise_work);
	return 0;
}

void etc_ble_set_current_sensor(struct sensor_data* data) {
	memcpy(&last_sensor_data, data, sizeof(last_sensor_data));
	if (current_conn == NULL) {
		k_work_submit(&advertise_work);
	}
}