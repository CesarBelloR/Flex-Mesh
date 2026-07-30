/*
 * Copyright (c) 2026 EXACT Technology Corporation
 *
 * Stand-ins for the two devices etc_sensor.c talks to over 1-Wire: the DS2484
 * bus master and the TMP1826 inside a splitter. The 1-Wire master emulates the
 * ROM search protocol closely enough for Zephyr's real w1_search_bus() to run
 * against it, so the code under test takes its normal path.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/w1.h>

#include "mock/mock_deps.h"
#include "tmp1826.h"

/* ROM of the splitter's temperature sensor, without byte 0. That byte is the
 * family code and comes from the attached part, so a test can present a TMP1826,
 * a TMP1827 or something the firmware must reject. w1_search_bus() does not
 * verify the CRC byte, so its value is arbitrary. */
#define STUB_ROM_BODY 0xAB0102030405C300ULL

struct stub_w1_config {
	struct w1_master_config w1_config;
};

struct stub_w1_data {
	struct w1_master_data w1_data;
	/* ROM of the part that answered the last presence pulse. */
	uint64_t rom;
	/* Position in the ROM being clocked out, and which half of the
	 * bit/complement pair the next read_bit answers. */
	int bit_index;
	bool complement_next;
};

static int stub_w1_reset_bus(const struct device *dev)
{
	struct stub_w1_data *data = dev->data;
	int port = mock_selected_port();

	data->bit_index = 0;
	data->complement_next = false;

	/* Only a splitter answers the presence pulse. A bare probe or an empty
	 * port leaves the line idle. */
	if (port < 0 || !mock.port[port].splitter) {
		return 0;
	}
	data->rom = STUB_ROM_BODY | mock.port[port].splitter_family;
	return 1;
}

static int stub_w1_read_bit(const struct device *dev)
{
	struct stub_w1_data *data = dev->data;
	bool bit = (data->rom >> data->bit_index) & 1U;

	if (!data->complement_next) {
		data->complement_next = true;
		return bit;
	}
	data->complement_next = false;
	return !bit;
}

static int stub_w1_write_bit(const struct device *dev, bool bit)
{
	struct stub_w1_data *data = dev->data;

	ARG_UNUSED(bit);
	if (data->bit_index < 63) {
		data->bit_index++;
	}
	return 0;
}

static int stub_w1_read_byte(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int stub_w1_write_byte(const struct device *dev, const uint8_t byte)
{
	struct stub_w1_data *data = dev->data;

	if (byte == W1_CMD_SEARCH_ROM) {
		data->bit_index = 0;
		data->complement_next = false;
	}
	return 0;
}

static int stub_w1_configure(const struct device *dev, enum w1_settings_type type, uint32_t value)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(type);
	ARG_UNUSED(value);
	return 0;
}

static int stub_w1_init(const struct device *dev)
{
	struct stub_w1_data *data = dev->data;

	k_mutex_init(&data->w1_data.bus_lock);
	return 0;
}

static const struct w1_driver_api stub_w1_api = {
	.reset_bus = stub_w1_reset_bus,
	.read_bit = stub_w1_read_bit,
	.write_bit = stub_w1_write_bit,
	.read_byte = stub_w1_read_byte,
	.write_byte = stub_w1_write_byte,
	.configure = stub_w1_configure,
};

static const struct stub_w1_config stub_w1_config = {
	.w1_config.slave_count = 1,
};
static struct stub_w1_data stub_w1_data;

DEVICE_DT_DEFINE(DT_NODELABEL(w1), stub_w1_init, NULL, &stub_w1_data, &stub_w1_config, POST_KERNEL,
		 CONFIG_W1_INIT_PRIORITY, &stub_w1_api);

/* --- TMP1826 -------------------------------------------------------------- */

static int stub_tmp1826_attr_set(const struct device *dev, enum sensor_channel chan,
				 enum sensor_attribute attr, const struct sensor_value *val)
{
	enum mock_branch branch;
	int port;

	ARG_UNUSED(dev);
	ARG_UNUSED(chan);

	if ((int)attr == (int)SENSOR_ATTR_W1_ROM) {
		return 0;
	}
	if ((int)attr != (int)TMP1826_SENSOR_ATTR_GPIO) {
		return -ENOTSUP;
	}

	port = mock_selected_port();
	if (port < 0 || !mock.port[port].splitter) {
		return -EIO;
	}
	/* etc_sensor_set_splitter_switch() drives GPIO0 high for the A branch. */
	branch = val->val1 ? MOCK_BRANCH_A : MOCK_BRANCH_B;
	if (mock.port[port].switch_fail_branch == (int)branch) {
		return -EIO;
	}
	mock.port[port].branch = branch;
	return 0;
}

static int stub_tmp1826_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);
	return 0;
}

static int stub_tmp1826_channel_get(const struct device *dev, enum sensor_channel chan,
				    struct sensor_value *val)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);
	val->val1 = 0;
	val->val2 = 0;
	return 0;
}

static int stub_tmp1826_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static const struct sensor_driver_api stub_tmp1826_api = {
	.attr_set = stub_tmp1826_attr_set,
	.sample_fetch = stub_tmp1826_sample_fetch,
	.channel_get = stub_tmp1826_channel_get,
};

DEVICE_DT_DEFINE(DT_NODELABEL(w1_tmp1826), stub_tmp1826_init, NULL, NULL, NULL, POST_KERNEL,
		 CONFIG_SENSOR_INIT_PRIORITY, &stub_tmp1826_api);
