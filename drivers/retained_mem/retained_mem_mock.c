
#define DT_DRV_COMPAT zephyr_retained_ram

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(retained_mem_mock_ram, CONFIG_RETAINED_MEM_LOG_LEVEL);

struct mock_retained_mem_config {
	uint8_t *address;
	size_t size;
};

static ssize_t mock_retained_mem_size(const struct device * dev)
{
	const struct mock_retained_mem_config *config = dev->config;

	return (ssize_t)config->size;
}

static int mock_retained_mem_read(const struct device * dev, off_t offset, uint8_t * buffer, size_t size)
{
	const struct mock_retained_mem_config *config = dev->config;

	memcpy(buffer, (config->address + offset), size);

	return 0;
}

static int mock_retained_mem_write(const struct device * dev, off_t offset, const uint8_t * buffer, size_t size)
{
	const struct mock_retained_mem_config *config = dev->config;
	memcpy((config->address + offset), buffer, size);

	return 0;
}

static int mock_retained_mem_clear(const struct device * dev)
{
	const struct mock_retained_mem_config *config = dev->config;
	memset(config->address, 0, config->size);

	return 0;
}

static int mock_retained_mem_ram_init(const struct device *dev)
{

	return 0;
}

static const struct retained_mem_driver_api mock_retained_mem_ram_api = {
	.size = mock_retained_mem_size,
	.read = mock_retained_mem_read,
	.write = mock_retained_mem_write,
	.clear = mock_retained_mem_clear,
};

#define MOCK_RETAINED_MEM_RAM_DEVICE(inst)                                                         \
	static uint8_t                                                                             \
		mock_retained_ram_##inst[DT_REG_SIZE(DT_PARENT(DT_INST(inst, DT_DRV_COMPAT)))];    \
	static const struct mock_retained_mem_config mock_retained_mem_config_##inst = {           \
		.address = (uint8_t *)&mock_retained_ram_##inst,                                   \
		.size = sizeof(mock_retained_ram_##inst),                                          \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, &mock_retained_mem_ram_init, NULL, NULL,                       \
			      &mock_retained_mem_config_##inst, POST_KERNEL,                       \
			      CONFIG_RETAINED_MEM_INIT_PRIORITY, &mock_retained_mem_ram_api);

DT_INST_FOREACH_STATUS_OKAY(MOCK_RETAINED_MEM_RAM_DEVICE)
