#include <kernel.h>
#include <logging/log.h>
#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <sys/crc.h>
#include "onewire/ds2484.h"
#include "ds18s20.h"

LOG_MODULE_REGISTER(DS18S20, CONFIG_ETC_APP_LOG_LEVEL);

static struct k_thread ds18s20_thread;

static K_KERNEL_STACK_DEFINE(ds18s20_stack, CONFIG_DS18S20_THREAD_STACK_SIZE);

static void ds18s20_thread_handler(void)
{
    uint8_t rom[8] = {0x00};
    uint8_t data[9] = {0x00};
	int ret = 0;
	while (true) {
		ret = ds2484_request_search(rom);
		if (ret != 0) {
			LOG_DBG("No devices on the bus");
			k_sleep(K_SECONDS(5));
			continue;
		}

		ds2484_request_reset_search();

		if (rom[0] != DS18S20_ROM_ID_TYPE_1 && rom[0] != DS18S20_ROM_ID_TYPE_2) {
			LOG_DBG("No DS18S20 on the bus");
			k_sleep(K_SECONDS(5));
			continue;
		}

		LOG_DBG("Found the DS18S20 on the bus");

		while(true) {
			ret = ds2484_request_reset();
			if (ret != 0) {
				LOG_DBG("Sensor is no longer on the bus");
				break;
			} 

			ret = ds2484_request_select(rom);
			if (ret != 0) {
				LOG_DBG("Failed to ds2484_request_select %d", ret);
				break;
			} 

			ds2484_write_byte(0x44);
			k_sleep(K_MSEC(750));

			ret = ds2484_set_config(strong_pull_up);
			if (ret != 0) {
				LOG_DBG("Failed to ds2484_set_config %d", ret);
				break;
			} 

			ret = ds2484_request_reset();
			if (ret != 0) {
				LOG_DBG("Failed to ds2484_request_reset %d", ret);
				break;
			} 

			ret = ds2484_request_select(rom);
			if (ret != 0) {
				LOG_DBG("Failed to ds2484_request_select %d", ret);
				break;
			} 

			ds2484_write_byte(0xBE);
			ds2484_read_bytes(data, sizeof(data));

			ret = ds2484_crc_validate(data, sizeof(data));
			if (ret != 0) {
				LOG_ERR("CRC is invalid");
				break;
			}

			int16_t raw = (data[1] << 8) | data[0];
			LOG_HEXDUMP_DBG(data, sizeof(data), "RAW");

			switch (rom[0]) {
				case DS18S20_ROM_ID_TYPE_1:
					raw = raw << 3;

					if (data[7] == 0x10) {
						raw = (raw & 0xFFF0) + 12 - data[6];
					}
				break;
				case DS18S20_ROM_ID_TYPE_2: {
					uint8_t cfg = (data[4] & 0x60); // default is 12 bit resolution, 750 ms conversion time
					if (cfg == 0x00) { // 9 bit resolution, 93.75 ms
						raw &= ~7;
					} else if (cfg == 0x20) { // 10 bit res, 187.5 ms
						raw &= ~3;
					} else if (cfg == 0x40) { // 11 bit res, 375 ms
						raw &= ~1;
					}
				}
				default: 
					break;
			}
			LOG_DBG("Temperature %i *mC", ((int32_t)raw * 100) >> 4);
			k_sleep(K_SECONDS(30));
		}
	}
}

int ds18s20_init(void)
{
	int ret = 0;
	ret = ds2484_init();
	if (ret != 0) {
		LOG_ERR("Failed to ds2484_init error %d", ret);
		return ret;
	}

	ret = ds2484_set_config(active_pull_up);
	if (ret != 0) {
		LOG_ERR("Failed to ds2848_set_config error %d", ret);
		return ret;
	}

	k_thread_create(&ds18s20_thread, ds18s20_stack,
			K_KERNEL_STACK_SIZEOF(ds18s20_stack),
			(k_thread_entry_t) ds18s20_thread_handler,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	return ret;
}

int ds18s20_get_temperature(void)
{
	return -1;
}

int ds18s20_get_humidity(void)
{
	return -1;
}