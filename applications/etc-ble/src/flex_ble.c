#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/settings/settings.h>
#include <zephyr/random/random.h>
#define LOG_LEVEL LOG_LEVEL_DBG
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(flex_ble);

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

static struct k_work advertise_work;
static char flex_device_name[CONFIG_BT_DEVICE_NAME_MAX] = { 0x00 };
static struct bt_conn *current_conn;

static uint8_t adv_data[] = {
	0x00, 0x00, 0x00, 0x00, // Manufacturer 
	0x00, 0x00, 0x00, 0x00, // Probe 1
	0x00, 0x00, 0x00, 0x00, // Probe 2
	0x00, 0x00, 0x00, 0x00, // Probe 3
	0x00, 0x00, 0x00, 0x00, // Probe 4
	0x00, 0x00, 0x00, 0x00, // Humid 
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, adv_data, sizeof(adv_data)),
};

static void flex_sensor_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	LOG_DBG("Notification has been turned %s",
		value == BT_GATT_CCC_NOTIFY ? "on" : "off");
}

static void flex_reclaim_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	LOG_DBG("Notification has been turned %s",
		value == BT_GATT_CCC_NOTIFY ? "on" : "off");
}

static void flex_config_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
	LOG_DBG("Notification has been turned %s",
		value == BT_GATT_CCC_NOTIFY ? "on" : "off");
}

static void flex_ble_set_bt_name(void)
{
	snprintf(flex_device_name, sizeof(flex_device_name), "Flex_%d", 10000013);
	int err = bt_set_name(flex_device_name);
	if (err) {
		LOG_ERR("Can't set BLE device name");
	}
}

/* Flex Service Declaration */
BT_GATT_SERVICE_DEFINE(flex_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_SERVICE),
	BT_GATT_CHARACTERISTIC(BT_UUID_SENSOR_CHAR, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT, NULL,
			       NULL, NULL),
	BT_GATT_CCC(flex_sensor_ccc_cfg_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_CHARACTERISTIC(BT_UUID_RECLAIM_CHAR, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT, NULL,
			       NULL, NULL),
	BT_GATT_CCC(flex_reclaim_ccc_cfg_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_CHARACTERISTIC(BT_UUID_CONFIG_CHAR, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, NULL,
			       NULL, NULL),
	BT_GATT_CCC(flex_config_ccc_cfg_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT)
);

static void flex_ble_sim_data(void) {
	float temp_1 = (float)(sys_rand32_get() % 125);
	float temp_2 = (float)(sys_rand32_get() % 125);
	float temp_3 = (float)(sys_rand32_get() % 125);
	float temp_4 = (float)(sys_rand32_get() % 125);
	float humid = (float)(sys_rand32_get() % 100);
	memcpy(&adv_data[4], &temp_1, sizeof(temp_1));
	memcpy(&adv_data[8], &temp_2, sizeof(temp_2));
	memcpy(&adv_data[12], &temp_3, sizeof(temp_3));
	memcpy(&adv_data[16], &temp_4, sizeof(temp_4));
	memcpy(&adv_data[20], &humid, sizeof(humid));
	LOG_INF("Sim data %2.1f %2.1f %2.1f %2.1f %2.1f", temp_1, temp_2, temp_3, temp_4, humid);
}

static void advertise(struct k_work *work)
{
	int rc;
	/* Generate simulation data */
	flex_ble_sim_data();

	bt_le_adv_stop();

	rc = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (rc) {
		LOG_ERR("Advertising failed to start (rc %d)", rc);
		return;
	}

	LOG_INF("Advertising successfully started");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (err 0x%02x)", err);
	} else {
		LOG_INF("Connected");
		current_conn = conn;
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
		int rc = bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(conn));
		LOG_DBG("Unpair this connection %d", rc);
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
	k_work_init(&advertise_work, advertise);

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
	flex_ble_set_bt_name();

	LOG_INF("Bluetooth initialized");
	k_work_submit(&advertise_work);

	return 0;
}