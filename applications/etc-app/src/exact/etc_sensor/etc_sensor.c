#include <zephyr/kernel.h>
#include <stdio.h>
#include <math.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include "events/sensor_event.h"
#include "cloud/cloud_codec/data_codec.h"
#include "common.h"
#include "etc_sensor.h"
#include "etc_sensor_calibration_load.h"
#include "etc_device.h"
#include "etc_battery.h"
#include "adc.h"
#include "tmp1826.h"
#include "etc_sensor_helper.h"
#include "etc_calibration.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_sensor, CONFIG_ETC_SENSOR_LOG_LEVEL);

/* Mutex to lock write/read from tasks */
K_MUTEX_DEFINE(etc_sensor_mtx);

/* Battery constant information */
const uint32_t sFullOhms = DT_PROP(DT_PATH(vbatt), full_ohms);
const uint32_t sOutputOhms = DT_PROP(DT_PATH(vbatt), output_ohms);

/* Constant prefix for calibrator */
const char sn_prefix[8] = "EXACT\0";

#if IS_ENABLED(CONFIG_ETC_AMBIENT_I2C_SENSOR)
const struct device *const ambient_i2c_dev = DEVICE_DT_GET_ANY(ti_tmp1075);
#endif

// Get any one sht31 in current bus. If NULL, SHT31 is not ready
const struct device *const sht31_i2c_dev = DEVICE_DT_GET_ANY(sensirion_sht31);
const struct device *ds2484_dev = DEVICE_DT_GET_ANY(exact_ds2484);
const struct device *tmp1826_dev = DEVICE_DT_GET(DT_NODELABEL(w1_tmp1826));
const uint8_t tmp1826_family = (uint8_t)DT_PROP(DT_NODELABEL(w1_tmp1826), family_code);

#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
static const struct gpio_dt_spec sense_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sense_enable), control_gpios, 0);
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
static const struct gpio_dt_spec onewire_slpz_dt =
	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(onewire_slpz), control_gpios, 0);
#endif
static const struct gpio_dt_spec s0_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel0), control_gpios, 0);
static const struct gpio_dt_spec s1_dt = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(sens_sel1), control_gpios, 0);
static const struct gpio_dt_spec vsen_en_dt =
	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(vsens_enable), control_gpios, 0);

/* The linear regulator supplying the analog sensor rail (VCC_A) misbehaves when
 * switched off and back on within a few hundred ms: its settling time grows to
 * several seconds, producing invalid readings. Enforce a minimum off-time before
 * the rail may be re-energised. This is a property of the rail, so it lives here
 * (the rail owner) and every power-on path must honour it via
 * etc_sensor_power_settling(). */
#define VSEN_MIN_OFF_MS 2000
/* Settling time of the analog switch inside a splitter after its TMP1826 GPIOs have
 * selected a branch. Without it the ADC can still see the previous branch. */
#define SPLITTER_SETTLE_MS 50
/* 32-bit so reads/writes are atomic on this Cortex-M4: the timestamp is touched
 * from the sensor poll thread, the calibration-timeout workqueue, and read from
 * several threads, so a 64-bit value could be torn. The 2000 ms window is far
 * below the 32-bit millisecond wrap, so unsigned wraparound arithmetic is
 * exact. 0 doubles as the "rail never turned off" sentinel. */
static uint32_t vsen_last_off_ms;

static enum sensor_type list_sensor_type[SENSOR_INPUT_IN8 + 1];
static int list_sensor_raw_adc[SENSOR_INPUT_IN8 + 1];
static float list_sensor_digital_temp[SENSOR_INPUT_IN4 + 1];
/* Whether a splitter answered on each physical port during the current acquisition.
 * Re-evaluated by etc_sensor_run_detection(): a port without a splitter has no B
 * branch, so inputs 5..8 must not be published for it. */
static bool splitter_present[SENSOR_INPUT_IN5];
static float sensor_digital_humid;
static int8_t sensor_digital_humid_port_index;
static int sensor_ambient_raw_adc = 0;
static int sensor_battery_raw_adc = 0;
static uint16_t sensor_r_hw_raw_adc = 0;
static struct etc_sensor_adc_calibration_info etc_sensor_adc_calibration_info = {0x00};
static etc_sensor_evt_handler_t sensor_evt_handler;
static enum etc_sensor_status last_sensor_status = SENSOR_CONNECTED;
static int sensor_calibration_port_1_wire = -1;
static struct w1_rom tmp1826_rom;
/* Who currently owns the analog front-end. A calibration session spans two
 * threads (the scan runs on the system workqueue, the run on the data module
 * thread) and can sit armed for CONFIG_CALIBRATION_TIMEOUT seconds in between,
 * so a k_mutex cannot cover it: mutexes are owned by a thread and cannot be
 * handed over. This flag closes that window - see etc_sensor_run_acquisition().
 */
static enum etc_sensor_hw_owner hw_owner = HW_OWNER_NONE;
/* Flag that signals if current connected sensors' state qualifies for functional
 * test mode. */
static bool enter_functional_test = false;

extern int ds2484_get_logic_level(const struct device *dev);

/* Remap channels according to HW-772, so that PCBA ports match housing port numbering */
inline static int8_t remap_th_channel(int8_t channel)
{
	__ASSERT(channel >= 0 && channel <= 3, "invalid channel number");

#if !defined(CONFIG_BOARD_ETC_0_3_0)
	switch (channel) {
	case 0:
		return 3;
	case 1:
		return 2;
	case 2:
		return 0;
	case 3:
		return 1;
	default:
		return 0;
	}
#else
	switch (channel) {
	case 0:
		return 1;
	case 1:
		return 0;
	case 2:
		return 3;
	case 3:
		return 2;
	default:
		return 0;
	}
#endif
}

static void etc_sensor_adc_switch_channel(int8_t channel)
{
	if (channel >= SENSOR_INPUT_IN5 && channel <= SENSOR_INPUT_IN8) {
		channel = channel - SENSOR_INPUT_IN5;
	}
	channel = remap_th_channel(channel);
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	gpio_pin_set_dt(&sense_dt, 0U);
#endif
	gpio_pin_set_dt(&s0_dt, channel & 0x01);
	gpio_pin_set_dt(&s1_dt, (channel >> 1) & 0x01);
}

static void etc_sensor_adc_hw_init(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	if (!device_is_ready(sense_dt.port)) {
		return;
	}
#endif
	if (!device_is_ready(s0_dt.port)) {
		return;
	}
	if (!device_is_ready(s1_dt.port)) {
		return;
	}
	if (!device_is_ready(vsen_en_dt.port)) {
		return;
	}

#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
	if (!device_is_ready(onewire_slpz_dt.port)) {
		return;
	}
#endif
	/* Configure as output with input connected. This allows reading the pin state through
	 * the gpio_get API calls. */
	gpio_pin_configure_dt(&vsen_en_dt, GPIO_OUTPUT_INACTIVE | GPIO_INPUT);

	adc_init();
}

static void etc_sensor_gpios_enable(void)
{
	gpio_pin_set_dt(&vsen_en_dt, 1U);
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	gpio_pin_configure_dt(&sense_dt, GPIO_OUTPUT_INACTIVE);
#endif
	/* Note: pin s0 is configured by watchdog module if s0/wdt are shared */
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	gpio_pin_configure_dt(&s0_dt, GPIO_OUTPUT_INACTIVE);
#endif
	gpio_pin_configure_dt(&s1_dt, GPIO_OUTPUT_INACTIVE);
}

static void etc_sensor_gpios_one_wire_enable(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
	gpio_pin_configure_dt(&onewire_slpz_dt, GPIO_OUTPUT_INACTIVE);
#endif
}

bool etc_sensor_power_settling(void)
{
	return vsen_last_off_ms != 0 && (k_uptime_get_32() - vsen_last_off_ms) < VSEN_MIN_OFF_MS;
}

bool etc_sensor_disable_power(void)
{
	if (gpio_pin_get_dt(&vsen_en_dt)) {
		gpio_pin_set_dt(&vsen_en_dt, 0U);
		vsen_last_off_ms = k_uptime_get_32();
		return true;
	}
	return false;
}

static void etc_sensor_gpios_disable(void)
{
	if (gpio_pin_get_dt(&vsen_en_dt)) {
		gpio_pin_set_dt(&vsen_en_dt, 0U);
		vsen_last_off_ms = k_uptime_get_32();
	}
#if DT_NODE_EXISTS(DT_NODELABEL(sense_enable))
	gpio_pin_configure_dt(&sense_dt, GPIO_DISCONNECTED);
#endif
	/* Note: pin s0 is configured by watchdog module if s0/wdt are shared */
#if DT_NODE_EXISTS(DT_NODELABEL(hw_wdt))
	gpio_pin_configure_dt(&s0_dt, GPIO_DISCONNECTED);
#else
	gpio_pin_set_dt(&s0_dt, 0);
#endif
	gpio_pin_configure_dt(&s1_dt, GPIO_DISCONNECTED);
}

static void etc_sensor_gpios_one_wire_disable(void)
{
#if DT_NODE_EXISTS(DT_NODELABEL(onewire_slpz))
	gpio_pin_configure_dt(&onewire_slpz_dt, GPIO_OUTPUT_ACTIVE);
#endif
}

static void w1_search_callback(struct w1_rom val, void *user_data)
{
	int *devices_on_bus = (int *)user_data;
	char *family_name = "1w";

	*devices_on_bus = *devices_on_bus + 1;
	if (val.family == tmp1826_family) {
		tmp1826_rom = val;
		family_name = "TMP1826";
		return;
	}
	LOG_DBG("found %s sensor with id 0x%016llx", family_name, w1_rom_to_uint64(&val));
}

static int etc_sensor_scan_probe_slaves(void)
{
	const struct device *const w1 = DEVICE_DT_GET(DT_NODELABEL(w1));
	uint8_t family_code;
	int num_devices = 0;
	family_code = DT_PROP(DT_NODELABEL(w1_tmp1826), family_code);
	memset(&tmp1826_rom, 0, sizeof(tmp1826_rom));
	w1_search_bus(w1, W1_CMD_SEARCH_ROM, family_code, w1_search_callback, &num_devices);
	LOG_DBG("found %d devices on one-wire bus", num_devices);
	if (num_devices > 0) {
		struct sensor_value val;
		w1_rom_to_sensor_value(&tmp1826_rom, &val);
		if (tmp1826_rom.family != tmp1826_family) {
			return 0;
		}
		sensor_attr_set(tmp1826_dev, SENSOR_CHAN_ALL, SENSOR_ATTR_W1_ROM, &val);
	}
	return num_devices;
}

/**
 * @brief Point the splitter on the currently selected port at a branch.
 *
 * @param channel Sensor input. Inputs 1..4 select the A branch, 5..8 the B branch.
 * @param[out] found Whether a splitter answered on this port, which is a separate
 *		     question from whether its branch could be selected. May be NULL.
 *
 * @retval 0 the requested branch is selected
 * @retval -ENODEV no splitter answered
 * @retval -EIO a splitter answered but would not actuate
 */
static int etc_sensor_set_splitter_switch(int8_t channel, bool *found)
{
	if (found != NULL) {
		*found = false;
	}
	etc_sensor_gpios_one_wire_enable();
	k_msleep(1);
	int ret = ds2484_get_logic_level(ds2484_dev);
	if (ret == 0) {
		LOG_DBG("1w shorted to GND");
		ret = -ENODEV;
		goto exit;
	}
	ret = etc_sensor_scan_probe_slaves();
	if (ret == 0) {
		ret = -ENODEV;
		goto exit;
	}
	if (found != NULL) {
		*found = true;
	}
	if (!device_is_ready(tmp1826_dev)) {
		LOG_ERR("TMP1826 is not ready in I2C bus");
		ret = -EIO;
		goto exit;
	}
	struct sensor_value val;
	int retries = 0;
	val.val1 = channel <= SENSOR_INPUT_IN4 ? 0x01 : 0x00;
	val.val2 = 0;
	LOG_DBG("Val %d", val.val1);
	do {
		ret = sensor_attr_set(tmp1826_dev, SENSOR_CHAN_ALL, TMP1826_SENSOR_ATTR_GPIO, &val);
		retries++;
	} while (ret != 0 && retries < CONFIG_ETC_SENSOR_TMP1826_GPIO_RETRIES);
	if (ret != 0) {
		LOG_ERR("Failed to set GPIO (err %d)", ret);
		ret = -EIO;
	}
exit:
	etc_sensor_gpios_one_wire_disable();
	return ret;
}

#if IS_ENABLED(CONFIG_BOARD_ETC_0_3_0)
static bool etc_sensor_set_type(int8_t channel, int raw_adc)
{
	bool is_set = true;
	if (raw_adc >= SENSOR_ADC_NO_CONNECTED) {
		list_sensor_type[channel] = SENSOR_TYPE_UNDEF;
		is_set = false;
	} else if (raw_adc <= SENSOR_ADC_ONE_WIRE_CONNECTED) {
		list_sensor_type[channel] = SENSOR_TYPE_DIGITAL;
	} else {
		list_sensor_type[channel] = SENSOR_TYPE_ANALOG;
	}
	return is_set;
}
#endif

static void etc_sensor_run_detection(void)
{
#if IS_ENABLED(CONFIG_BOARD_ETC_0_3_0)
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		bool found;

		/* Classify both branches from scratch every acquisition: a splitter that
		 * has been unplugged must not leave its B branch behind. */
		list_sensor_type[i] = SENSOR_TYPE_UNDEF;
		list_sensor_type[i + SENSOR_INPUT_IN5] = SENSOR_TYPE_UNDEF;

		etc_sensor_adc_switch_channel(i);
		k_msleep(50);
		int rc = etc_sensor_set_splitter_switch(i, &found);
		splitter_present[i] = found;
		if (found) {
			/* A splitter that will not actuate leaves the branch position
			 * unknown, so no reading from this port can be attributed to a
			 * branch. Skip it and retry on the next acquisition. */
			if (rc != 0) {
				LOG_WRN("Splitter on port %d unreachable (err %d)", i + 1, rc);
				continue;
			}
			k_msleep(SPLITTER_SETTLE_MS);
		}
		etc_sensor_set_type(i, adc_get_channel_filtered(ETC_ADC_CHANNEL_SENSOR));
		/* Only process the second splitter channel if there is a splitter connected
		 * and its branch switch actually took effect. */
		if (found && etc_sensor_set_splitter_switch(i + SENSOR_INPUT_IN5, NULL) == 0) {
			k_msleep(SPLITTER_SETTLE_MS);
			etc_sensor_set_type(i + SENSOR_INPUT_IN5,
					    adc_get_channel_filtered(ETC_ADC_CHANNEL_SENSOR));
		}
	}
	LOG_INF("detect: splitter %d%d%d%d type %d%d%d%d/%d%d%d%d", splitter_present[0],
		splitter_present[1], splitter_present[2], splitter_present[3],
		list_sensor_type[SENSOR_INPUT_IN1], list_sensor_type[SENSOR_INPUT_IN2],
		list_sensor_type[SENSOR_INPUT_IN3], list_sensor_type[SENSOR_INPUT_IN4],
		list_sensor_type[SENSOR_INPUT_IN5], list_sensor_type[SENSOR_INPUT_IN6],
		list_sensor_type[SENSOR_INPUT_IN7], list_sensor_type[SENSOR_INPUT_IN8]);
#endif
}

static void etc_sensor_probe_check(void)
{
	bool probe_connected = false;
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN8; i++) {
		float temp = etc_sensor_get_probe_temp(i);
		if (sensor_temperature_is_valid(temp)) {
			probe_connected = true;
			break;
		}
	}

	enum etc_sensor_status sensor_status = SENSOR_NO_CONNECTION;
	if (probe_connected) {
		sensor_status = SENSOR_CONNECTED;
	}

	if (sensor_evt_handler) {
		k_mutex_lock(&etc_sensor_mtx, K_FOREVER);
		if (sensor_status != last_sensor_status) {
			sensor_evt_handler(sensor_status);
			last_sensor_status = sensor_status;
		}
		k_mutex_unlock(&etc_sensor_mtx);
	}
}

static inline int etc_sensor_acquire_digital_sensor(float *humidity_val, int8_t index)
{
	__ASSERT_NO_MSG(humidity_val != NULL);

	if (ds2484_get_logic_level(ds2484_dev) == 0) {
		// Do not read sensor if the 1-wire signal
		// is GND. This will cause the thread to hang.
		LOG_WRN("1-wire signal is GND");
		return -1;
	}
	if (!device_is_ready(sht31_i2c_dev)) {
		LOG_ERR("SHT31 is not ready in I2C bus");
		return -1;
	}

	/* Reset the bus */
	sensor_attr_set(sht31_i2c_dev, SENSOR_CHAN_ALL, SENSOR_ATTR_CONFIGURATION, NULL);
	struct sensor_value temp, hum;
	int rc = sensor_sample_fetch(sht31_i2c_dev);
	if (rc) {
		LOG_ERR("Failed to fetch sensor SHT31 (err %d)", rc);
		return rc;
	}

	rc = sensor_channel_get(sht31_i2c_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	if (rc) {
		LOG_ERR("Failed to get temperature sensor SHT31 (err %d)", rc);
		return rc;
	} else {
		list_sensor_digital_temp[index] = (float)sensor_value_to_double(&temp);
	}

	rc = sensor_channel_get(sht31_i2c_dev, SENSOR_CHAN_HUMIDITY, &hum);
	if (rc) {
		LOG_ERR("Failed to get humidity sensor SHT31 (err %d)", rc);
		return rc;
	}

	*humidity_val = (float)sensor_value_to_double(&hum);

	return 0;
}

static void etc_sensor_run_digital_sample(void)
{
	int ret;
	int input_high_index = SENSOR_INPUT_IN4;

	sensor_digital_humid = SENSOR_HUMID_NO_CONNECTED;
	sensor_digital_humid_port_index = -1;
	enter_functional_test = true;

	enum etc_device_type device_type = etc_get_device_type();

	if (device_type == ETC_DEVICE_TYPE_EMBEDDABLE) {
		input_high_index = SENSOR_INPUT_IN2;
	} else if (device_type == ETC_DEVICE_TYPE_AMBIENT) {
		input_high_index = SENSOR_INPUT_IN1;
	}

	etc_sensor_gpios_one_wire_enable();
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		list_sensor_digital_temp[i] = SENSOR_TEMP_NO_CONNECTED;
		etc_sensor_adc_switch_channel(i);
		if (enter_functional_test) {
			k_msleep(50);
			ret = ds2484_get_logic_level(ds2484_dev);
			LOG_DBG("LL: %d", ret);
			if (ret != 0 && i <= input_high_index) {
				enter_functional_test = false;
			}
		} else if (list_sensor_type[i] == SENSOR_TYPE_DIGITAL) {
			float humidity_val;
			enter_functional_test = false;
			k_msleep(50);
			ret = etc_sensor_acquire_digital_sensor(&humidity_val, i);
			/* Only one humidity sensor is supported */
			if (ret == 0 && sensor_digital_humid == SENSOR_HUMID_NO_CONNECTED) {
				sensor_digital_humid = humidity_val;
				sensor_digital_humid_port_index = i;
			}
		}
	}
	etc_sensor_gpios_one_wire_disable();
}

/**
 * Read an analog sample from an already configured physical sensor port.
 */
static void read_analog_sample(int8_t channel)
{
	int8_t port = channel <= SENSOR_INPUT_IN4 ? channel : channel - SENSOR_INPUT_IN5;

	if (list_sensor_type[channel] != SENSOR_TYPE_ANALOG) {
		list_sensor_raw_adc[channel] = -1;
		return;
	}
	/* A port without a splitter has a single branch and needs no switching. One with
	 * a splitter may be pointing at the other branch, so sampling after a failed
	 * switch would publish that branch's reading under this channel. */
	if (splitter_present[port]) {
		if (etc_sensor_set_splitter_switch(channel, NULL) != 0) {
			LOG_WRN("Branch switch failed on input %d, dropping sample", channel + 1);
			list_sensor_type[channel] = SENSOR_TYPE_UNDEF;
			list_sensor_raw_adc[channel] = -1;
			return;
		}
		k_msleep(SPLITTER_SETTLE_MS);
	}
	list_sensor_raw_adc[channel] = etc_sensor_helper_get_calibrated_adc(
		adc_get_channel_filtered(ETC_ADC_CHANNEL_SENSOR), &sensor_r_hw_raw_adc,
		&etc_sensor_adc_calibration_info);
	LOG_INF("ADC[%d] %d", channel, list_sensor_raw_adc[channel]);
}

static void etc_sensor_run_analog_sample(void)
{
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		if (list_sensor_type[i] == SENSOR_TYPE_ANALOG ||
		    list_sensor_type[i + SENSOR_INPUT_IN5] == SENSOR_TYPE_ANALOG) {
			etc_sensor_adc_switch_channel(i);
			k_msleep(50);
		}
		/* Read from the splitter */
		read_analog_sample(i);
		read_analog_sample(i + SENSOR_INPUT_IN5);
	}
}

static void etc_sensor_load_calibration(void)
{
	etc_sensor_load_calibration_info(&etc_sensor_adc_calibration_info);
}

void etc_sensor_init(etc_sensor_evt_handler_t handler)
{
#if IS_ENABLED(CONFIG_ETC_AMBIENT_I2C_SENSOR)
	__ASSERT(ambient_i2c_dev != NULL, "Failed to get device binding");
	__ASSERT(device_is_ready(ambient_i2c_dev), "Device %s is not ready", ambient_i2c_dev->name);
#endif
	etc_sensor_load_calibration();
	etc_sensor_adc_hw_init();
	for (int i = 0; i < SENSOR_INPUT_IN8 + 1; i++) {
#if IS_ENABLED(CONFIG_BOARD_ETC_0_3_0)
		list_sensor_type[i] = SENSOR_TYPE_UNDEF;
#else
		/* Without splitter detection there is no basis for a B branch. */
		list_sensor_type[i] =
			i <= SENSOR_INPUT_IN4 ? SENSOR_TYPE_ANALOG : SENSOR_TYPE_UNDEF;
#endif
	}

	sensor_evt_handler = handler;
}

float etc_sensor_get_ambient_temp(void)
{
#if IS_ENABLED(CONFIG_ETC_AMBIENT_NTC_SENSOR)
	return etc_sensor_helper_ntc_get(sensor_ambient_raw_adc, ETC_ADC_CHANNEL_AMB);
#elif IS_ENABLED(CONFIG_ETC_AMBIENT_I2C_SENSOR)
	int rc = sensor_sample_fetch(ambient_i2c_dev);
	if (rc) {
		LOG_ERR("Failed to sample the sensor (err %d)", rc);
		return SENSOR_TEMP_NO_CONNECTED;
	}

	struct sensor_value temp_value;
	rc = sensor_channel_get(ambient_i2c_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_value);
	if (rc) {
		LOG_ERR("Faied to sensor_channel_get (err %d)", rc);
		return SENSOR_TEMP_NO_CONNECTED;
	}

	return (float)sensor_value_to_double(&temp_value);
#endif
	return SENSOR_TEMP_NO_CONNECTED;
}

float etc_sensor_get_probe_temp(enum sensor_input input)
{
	__ASSERT(input >= 0 && input <= 7, "invalid channel number");
	if (list_sensor_type[input] == SENSOR_TYPE_ANALOG) {
		return etc_sensor_helper_ntc_get(list_sensor_raw_adc[input],
						 ETC_ADC_CHANNEL_SENSOR);
	} else if (list_sensor_type[input] == SENSOR_TYPE_DIGITAL) {
		if (input <= SENSOR_INPUT_IN4) {
			return list_sensor_digital_temp[input];
		} else {
			return SENSOR_TEMP_NO_CONNECTED;
		}
	} else {
		/* No action required */
	}
	return SENSOR_TEMP_NO_CONNECTED;
}

float etc_sensor_get_probe_humid(void)
{
	return sensor_digital_humid;
}

int8_t etc_sensor_get_probe_humid_index(void)
{
	return sensor_digital_humid_port_index;
}

uint16_t etc_sensor_get_battery(void)
{
	int raw_battery_adc = sensor_battery_raw_adc;
	adc_get_raw_to_millivolts(ETC_ADC_CHANNEL_BATTERY, &raw_battery_adc);
	int adc_mv_battery = raw_battery_adc * (sFullOhms / sOutputOhms);
	return adc_mv_battery;
}

uint16_t etc_sensor_sample_and_get_battery(void)
{
	sensor_battery_raw_adc = etc_sensor_helper_get_calibrated_adc(
		adc_get_channel(ETC_ADC_CHANNEL_BATTERY), &sensor_r_hw_raw_adc,
		&etc_sensor_adc_calibration_info);
	return etc_sensor_get_battery();
}

bool etc_sensor_calibration_owns_hw(void)
{
	enum etc_sensor_hw_owner owner;

	k_mutex_lock(&etc_sensor_mtx, K_FOREVER);
	owner = hw_owner;
	k_mutex_unlock(&etc_sensor_mtx);
	return owner == HW_OWNER_CALIBRATION;
}

static void etc_sensor_hw_owner_set(enum etc_sensor_hw_owner owner)
{
	k_mutex_lock(&etc_sensor_mtx, K_FOREVER);
	hw_owner = owner;
	k_mutex_unlock(&etc_sensor_mtx);
}

int etc_sensor_run_acquisition(void)
{
	/* Skip rather than wait. A sample taken during calibration reads the
	 * calibrator's reference network instead of the probes, so blocking would
	 * stall this thread for seconds only to produce a reading we must discard.
	 */
	if (etc_sensor_calibration_owns_hw()) {
		/* INF, not DBG: this explains a missing reading, and etc_sensor logs at
		 * INF by default. */
		LOG_INF("Calibration owns the front-end, skipping acquisition");
		return -EBUSY;
	}

	/* Never K_FOREVER: this thread must not be parked indefinitely behind a
	 * calibration session or a wedged bus. */
	if (etc_calibration_lock_timeout(K_SECONDS(CONFIG_ETC_SENSOR_HW_LOCK_TIMEOUT_S)) != 0) {
		LOG_WRN("Front-end lock busy, skipping acquisition");
		return -EBUSY;
	}

	/* Re-check: a calibration session may have claimed the hardware while we
	 * waited for the lock. */
	if (etc_sensor_calibration_owns_hw()) {
		LOG_INF("Calibration claimed the front-end, skipping acquisition");
		etc_calibration_unlock();
		return -EBUSY;
	}

	sensor_digital_humid = SENSOR_HUMID_NO_CONNECTED;
	/* Enable the GPIOs SEL0/SEL1 */
	etc_sensor_gpios_enable();
	/* Retrieve HW_INF (R) value used for ADC temperature compensation */
	sensor_r_hw_raw_adc = adc_get_channel_filtered(ETC_ADC_CHANNEL_HW_VER);
	/* Run detection sensor */
	etc_sensor_run_detection();
	/* Run sample for ambient ADC */
	sensor_ambient_raw_adc = etc_sensor_helper_get_calibrated_adc(
		adc_get_channel(ETC_ADC_CHANNEL_AMB), &sensor_r_hw_raw_adc,
		&etc_sensor_adc_calibration_info);
	/* Run sample for battery */
	sensor_battery_raw_adc = etc_sensor_helper_get_calibrated_adc(
		adc_get_channel(ETC_ADC_CHANNEL_BATTERY), &sensor_r_hw_raw_adc,
		&etc_sensor_adc_calibration_info);
	/* Run sample for HW sensor */
	/* Run sample sensor for all ports - analog part*/
	etc_sensor_run_analog_sample();
	/* Run sample sensor for all ports - digital part */
	etc_sensor_run_digital_sample();
	/* Disable the GPIOs SEL0/SEL1 */
	etc_sensor_gpios_disable();
	/* Sync battery status */
	etc_battery_poll_status();
	/* Check probe connection */
	etc_sensor_probe_check();
	/* Populate Rr value if needed */
	if (!etc_sensor_rr_value_is_valid(etc_get_rr_value())) {
		LOG_DBG("rr val: %u", sensor_r_hw_raw_adc);
		if (etc_sensor_temp_ambient_for_rr_is_valid(etc_sensor_get_ambient_temp()) && 
		    (etc_sensor_rr_value_is_valid(sensor_r_hw_raw_adc))) {
			etc_set_rr_value(sensor_r_hw_raw_adc);
		}
	}

	etc_calibration_unlock();
	return 0;
}

enum sensor_type etc_sensor_get_probe_type(enum sensor_input input)
{
	__ASSERT(input >= 0 && input <= 3, "invalid channel number");
	return list_sensor_type[input];
}

enum etc_sensor_status etc_sensor_get_status(void)
{
	enum etc_sensor_status status;
	k_mutex_lock(&etc_sensor_mtx, K_FOREVER);
	status = last_sensor_status;
	k_mutex_unlock(&etc_sensor_mtx);
	return status;
}

bool etc_sensor_get_enter_functional_test(void)
{
	return enter_functional_test;
}

void etc_sensor_enter_functional_test(void)
{
	etc_sensor_gpios_enable();
}

void etc_sensor_exit_functional_test(void)
{
	etc_sensor_gpios_disable();
}

void etc_sensor_calibration_enter(void)
{
	etc_sensor_hw_owner_set(HW_OWNER_CALIBRATION);
	etc_sensor_gpios_enable();
}

int etc_sensor_calibration_scan(void)
{
	int num_sensor = 0;
	int rc = 0;
	etc_sensor_gpios_one_wire_enable();
	k_msleep(10);
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		etc_sensor_adc_switch_channel(i);
		k_msleep(10);
		rc = ds2484_get_logic_level(ds2484_dev);
		if (rc == 0) {
			continue;
		}
		/* Probe slaves. Only treat this channel as the calibrator port when a
		 * TMP1826 was actually found here: etc_sensor_scan_probe_slaves()
		 * programs the active 1-wire ROM only on a successful find, so keying
		 * off its return value (rather than the always-true device_is_ready())
		 * keeps the selected mux channel and the active ROM pointed at the same
		 * sensor regardless of calibrator orientation. */
		rc = etc_sensor_scan_probe_slaves();
		if (rc <= 0) {
			continue;
		}
		sensor_calibration_port_1_wire = i;
		num_sensor += 1;
	}
	etc_sensor_gpios_one_wire_disable();
	return num_sensor;
}

int etc_sensor_calibration_read_sn(void)
{
	uint32_t serial_number;
	uint8_t buf[sizeof(sn_prefix) + sizeof(serial_number)];
	int rc = -ENOENT;

	if (sensor_calibration_port_1_wire == -1) {
		return -ENOENT;
	}

	etc_sensor_gpios_one_wire_enable();
	k_msleep(10);
	etc_sensor_adc_switch_channel(sensor_calibration_port_1_wire);
	k_msleep(10);
	if (tmp1826_read_eeprom(tmp1826_dev, 0, buf, sizeof(buf)) == 0) {
		if (memcmp(buf, sn_prefix, sizeof(sn_prefix)) == 0) {
			LOG_DBG("Found the calibration code");
			serial_number = *((uint32_t *)&buf[sizeof(sn_prefix)]);
			rc = serial_number;
		}
	}
	/* Leaving the transceiver awake lets the calibrator hold the 1-wire line
	 * during a later acquisition, so disable it on every exit. */
	etc_sensor_gpios_one_wire_disable();
	return rc;
}

int etc_sensor_calibration_write_sn(uint32_t serial_number)
{
	uint8_t buf[sizeof(sn_prefix) + sizeof(serial_number)];
	int rc;

	if (serial_number > ETC_CALIB_MAX_SN) {
		return -EINVAL;
	}

	etc_sensor_gpios_one_wire_enable();
	k_msleep(10);
	etc_sensor_adc_switch_channel(sensor_calibration_port_1_wire);
	k_msleep(10);

	memcpy(buf, sn_prefix, sizeof(sn_prefix));
	memcpy(buf + sizeof(sn_prefix), &serial_number, sizeof(serial_number));
	rc = tmp1826_write_eeprom(tmp1826_dev, 0, buf, sizeof(buf));
	etc_sensor_gpios_one_wire_disable();
	return rc;
}

int etc_sensor_calibration_read_adc(struct etc_sensor_adc_raw_data *raw_adc)
{
	int sum_adc = 0;
	int cur_adc = 0;
	etc_sensor_gpios_one_wire_disable();
	k_msleep(10);
	for (int8_t i = SENSOR_INPUT_IN1; i <= SENSOR_INPUT_IN4; i++) {
		etc_sensor_adc_switch_channel(i);
		k_msleep(10);
		cur_adc = adc_get_channel_filtered(ETC_ADC_CHANNEL_SENSOR);
		if (raw_adc) {
			raw_adc->port[i] = cur_adc;
		}
		sum_adc += cur_adc;
	}
	return sum_adc / 4;
}

int etc_sensor_calibration_set_gpio_mask(int8_t mask)
{
	struct sensor_value val;
	val.val1 = mask;
	etc_sensor_gpios_one_wire_enable();
	k_msleep(10);
	etc_sensor_adc_switch_channel(sensor_calibration_port_1_wire);
	k_msleep(10);
	int rc = sensor_attr_set(tmp1826_dev, SENSOR_CHAN_ALL, TMP1826_SENSOR_ATTR_GPIO, &val);
	etc_sensor_gpios_one_wire_disable();
	return rc;
}

float etc_sensor_calibration_convert_temperature(
	int raw_adc, uint16_t *rr_hw_adc, struct etc_sensor_adc_calibration_info *calibration_info)
{
	float temp = 0.0;
	uint16_t calibrated_adc =
		etc_sensor_helper_get_calibrated_adc(raw_adc, rr_hw_adc, calibration_info);
	return etc_sensor_helper_ntc_get(calibrated_adc, ETC_ADC_CHANNEL_SENSOR);
}

float etc_sensor_calibration_read_temperature_from_sensor(void)
{
	struct sensor_value temp, hum;
	float out = SENSOR_TEMP_NO_CONNECTED;
	/* Reset the bus */
	etc_sensor_gpios_one_wire_enable();
	k_msleep(10);
	etc_sensor_adc_switch_channel(sensor_calibration_port_1_wire);
	k_msleep(10);
	int rc = sensor_sample_fetch(tmp1826_dev);
	if (rc) {
		LOG_ERR("Failed to fetch sensor TMP1826 (err %d)", rc);
		goto done;
	}
	rc = sensor_channel_get(tmp1826_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	if (rc) {
		LOG_ERR("Failed to get temperature sensor (err %d)", rc);
		goto done;
	}
	out = (float)sensor_value_to_double(&temp);
	LOG_DBG("Temperature value %f", out);
done:
	etc_sensor_gpios_one_wire_disable();
	return out;
}

float etc_sensor_calibration_read_device_ambient(void)
{
#if IS_ENABLED(CONFIG_ETC_AMBIENT_NTC_SENSOR)
	int raw_adc;

	/* Prime the cached sample that etc_sensor_get_ambient_temp() converts:
	 * only the regular acquisition path refreshes it, and that path is skipped
	 * while calibration owns the front-end.
	 */
	raw_adc = adc_get_channel(ETC_ADC_CHANNEL_AMB);
	if (raw_adc < 0) {
		LOG_ERR("Failed to sample the ambient ADC (err %d)", raw_adc);
		return SENSOR_TEMP_NO_CONNECTED;
	}
	sensor_r_hw_raw_adc = adc_get_channel_filtered(ETC_ADC_CHANNEL_HW_VER);
	sensor_ambient_raw_adc = etc_sensor_helper_get_calibrated_adc(
		raw_adc, &sensor_r_hw_raw_adc, &etc_sensor_adc_calibration_info);
#endif
	/* The I2C ambient sensor is read live, so it needs no priming. */
	return etc_sensor_get_ambient_temp();
}

void etc_sensor_calibration_save_temperature_compensation(uint16_t hw_raw_adc)
{
	if (etc_sensor_rr_value_is_valid(hw_raw_adc)) {
		etc_set_rr_value(hw_raw_adc);
	}
}

uint16_t etc_sensor_calibration_get_hw_version_adc(void)
{
	return adc_get_channel_filtered(ETC_ADC_CHANNEL_HW_VER);
}

void etc_sensor_calibration_exit(void)
{
	etc_sensor_gpios_disable();
	/* Released only after the hardware is actually powered down, so a sensor
	 * acquisition can never observe HW_OWNER_NONE while the calibrator is still
	 * driving the ports. */
	etc_sensor_hw_owner_set(HW_OWNER_NONE);
}

void etc_sensor_calibration_release_hw(void)
{
	/* Deliberately leaves the rail powered (see the header): only the owner is
	 * cleared, so the acquisition that follows a failed calibration check runs
	 * instead of being skipped with -EBUSY. */
	etc_sensor_hw_owner_set(HW_OWNER_NONE);
}
