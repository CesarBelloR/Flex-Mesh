/***************************************************************************/
/*!
\file       io.c
\brief      GPIO interface header to process interrupt input, button input 
    or normal output.

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>
#include "io.h"
LOG_MODULE_REGISTER(ETC_IO, CONFIG_ETC_IO_LOG_LEVEL);

#define GET_CTLR(name, prop, idx) \
	DT_GPIO_CTLR_BY_IDX(DT_NODELABEL(name), prop, idx)
#define GET_PIN(name, prop, idx) \
	DT_GPIO_PIN_BY_IDX(DT_NODELABEL(name), prop, idx)
#define GET_PORT(name, prop, idx) \
	DT_PROP_BY_PHANDLE_IDX(DT_NODELABEL(name), prop, idx, port)
#define GET_FLAGS(name, prop, idx) \
	DT_GPIO_FLAGS_BY_IDX(DT_NODELABEL(name), prop, idx)
#define GET_DEV(name, prop, idx) DEVICE_DT_GET(GET_CTLR(name, prop, idx))

struct io_cfg {
	const struct device *gpio;
	gpio_pin_t pin;
	gpio_dt_flags_t flags;
	bool on;
#if IS_ENABLED(CONFIG_LOG)
	uint8_t port;
	bool info;
	const char *name;
#endif
};

#define ROUTING_ENABLED(_name) DT_NODE_HAS_STATUS(DT_NODELABEL(_name), okay)
#define IO_CFG(_name, _idx)					\
{								\
	.gpio  = GET_DEV(_name, control_gpios, _idx),		\
	.pin   = GET_PIN(_name, control_gpios, _idx),		\
	.flags = GET_FLAGS(_name, control_gpios, _idx),		\
	.on    = ROUTING_ENABLED(_name),			\
	COND_CODE_1(CONFIG_LOG,					\
	(							\
		.port = GET_PORT(_name, control_gpios, _idx),	\
		.info = (_idx == 0),				\
		.name = #_name,					\
	), ())							\
}
#define HAS_TWO_PINS(_name) \
	DT_PHA_HAS_CELL_AT_IDX(DT_NODELABEL(_name), control_gpios, 1, pin)

#define ROUTING_IO(_name)					\
	COND_CODE_1(DT_NODE_EXISTS(DT_NODELABEL(_name)),	\
	(							\
		COND_CODE_1(HAS_TWO_PINS(_name),		\
		(						\
			IO_CFG(_name, 1),			\
		), ())						\
		IO_CFG(_name, 0),				\
	), ())

static const struct io_cfg routing_io_list[IO_NUM] = {
	ROUTING_IO(sense_enable)
	ROUTING_IO(vsens_enable)
	ROUTING_IO(sens_sel0)
	ROUTING_IO(sens_sel1)
	ROUTING_IO(rtc_int)
	ROUTING_IO(batt_int)
};

int io_init(void) {
	int rc;

	for (int i = 0; i < ARRAY_SIZE(routing_io_list); ++i) {
		const struct io_cfg *cfg = &routing_io_list[i];
		gpio_flags_t flags = cfg->flags;

		if (!device_is_ready(cfg->gpio)) {
			LOG_ERR("%s is not ready", cfg->gpio->name);
			return -ENODEV;
		}

		flags |= (cfg->on ? GPIO_OUTPUT_ACTIVE
				  : GPIO_OUTPUT_INACTIVE);
		rc = gpio_pin_configure(cfg->gpio, cfg->pin, flags);
#if IS_ENABLED(CONFIG_LOG)
		LOG_DBG("Configuring P%d.%02d with flags: 0x%08x",
			cfg->port, cfg->pin, flags);
		if (rc) {
			LOG_ERR("Error %d while configuring pin P%d.%02d (%s)",
				rc, cfg->port, cfg->pin, cfg->name);
		} else if (cfg->info) {
			LOG_INF("%s is %s",
				cfg->name, cfg->on ? "ENABLED" : "disabled");
		}
#endif
		if (rc) {
			return rc;
		}
	}

	return 0;
}

int  io_get(io_id_t id) {
	if (id > IO_NUM) return -1;
	int rc = 0;
	rc = gpio_pin_get(routing_io_list[id].gpio, routing_io_list[id].pin);
	return rc;
}

void io_set(io_id_t id, int value) {
	if (id > IO_NUM) return;
	gpio_pin_set(routing_io_list[id].gpio, routing_io_list[id].pin, value);
}