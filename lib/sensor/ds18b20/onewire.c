#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <hal/nrf_gpio.h>
#include "onewire.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(onewire, LOG_LEVEL_DBG);

void onewire_delay(uint16_t time_us)
{
    k_busy_wait(time_us);
}

void onewire_low(onewire_t *gp)
{
   nrf_gpio_pin_clear(gp->GPIO_Pin);
}

void onewire_high(onewire_t *gp)
{
   nrf_gpio_pin_set(gp->GPIO_Pin);
}

void onewire_input(onewire_t *gp)
{
    nrf_gpio_cfg_input(gp->GPIO_Pin, NRF_GPIO_PIN_NOPULL);
}

void onewire_output(onewire_t *gp)
{
    nrf_gpio_cfg_output(gp->GPIO_Pin);
}

void onewire_init(onewire_t* one_wire_struct, int pin)
{
	one_wire_struct->GPIO_Pin = pin;
	onewire_output(one_wire_struct);
	onewire_high(one_wire_struct);
	OneWireDelay(1000);
	onewire_low(one_wire_struct);
	OneWireDelay(1000);
	onewire_high(one_wire_struct);
	OneWireDelay(2000);
}

inline uint8_t onewire_reset(onewire_t* one_wire_struct)
{
	uint8_t i;

	/* Line low, and wait 480us */
	onewire_low(one_wire_struct);
	onewire_output(one_wire_struct);
	onewire_delay(480);
	onewire_delay(20);
	/* Release line and wait for 70us */
	onewire_input(one_wire_struct);
	onewire_delay(70);
	/* Check bit value */
	i = nrf_gpio_pin_read(one_wire_struct->GPIO_Pin);

	/* Delay for 410 us */
	onewire_delay(410);
	/* Return value of presence pulse, 0 = OK, 1 = ERROR */
	return i;
}

inline void onewire_writebit(onewire_t* one_wire_struct, uint8_t bit)
{
	if (bit)
	{
		/* Set line low */
		onewire_low(one_wire_struct);
		onewire_output(one_wire_struct);
		onewire_delay(10);

		/* Bit high */
		onewire_input(one_wire_struct);

		/* Wait for 55 us and release the line */
		onewire_delay(55);
		onewire_input(one_wire_struct);
	}
	else
	{
		/* Set line low */
		onewire_low(one_wire_struct);
		onewire_output(one_wire_struct);
		onewire_delay(65);

		/* Bit high */
		onewire_input(one_wire_struct);

		/* Wait for 5 us and release the line */
		onewire_delay(5);
		onewire_input(one_wire_struct);
	}

}

inline uint8_t onewire_readbit(onewire_t* one_wire_struct)
{
	uint8_t bit = 0;

	/* Line low */
	onewire_low(one_wire_struct);
	onewire_output(one_wire_struct);
	onewire_delay(2);

	/* Release line */
	onewire_input(one_wire_struct);
	onewire_delay(10);

	/* Read line value */
	if (nrf_gpio_pin_read(one_wire_struct->GPIO_Pin)) {
		/* Bit is HIGH */
		bit = 1;
	}

	/* Wait 50us to complete 60us period */
	onewire_delay(50);

	/* Return bit value */
	return bit;
}

void onewire_writebyte(onewire_t* one_wire_struct, uint8_t byte) {
	uint8_t i = 8;
	/* Write 8 bits */
	while (i--) {
		/* LSB bit is first */
		onewire_writebit(one_wire_struct, byte & 0x01);
		byte >>= 1;
	}
}

uint8_t onewire_readbyte(onewire_t* one_wire_struct) {
	uint8_t i = 8, byte = 0;
	while (i--) {
		byte >>= 1;
		byte |= (onewire_readbit(one_wire_struct) << 7);
	}

	return byte;
}

uint8_t onewire_first(onewire_t* one_wire_struct) {
	/* Reset search values */
	onewire_resetsearch(one_wire_struct);

	/* Start with searching */
	return onewire_search(one_wire_struct, ONEWIRE_CMD_SEARCHROM);
}

uint8_t onewire_next(onewire_t* one_wire_struct) {
   /* Leave the search state alone */
   return onewire_search(one_wire_struct, ONEWIRE_CMD_SEARCHROM);
}

void onewire_resetsearch(onewire_t* one_wire_struct) {
	/* Reset the search state */
	one_wire_struct->LastDiscrepancy = 0;
	one_wire_struct->LastDeviceFlag = 0;
	one_wire_struct->LastFamilyDiscrepancy = 0;
}

uint8_t onewire_search(onewire_t* one_wire_struct, uint8_t command) {
	uint8_t id_bit_number;
	uint8_t last_zero, rom_byte_number, search_result;
	uint8_t id_bit, cmp_id_bit;
	uint8_t rom_byte_mask, search_direction;

	/* Initialize for search */
	id_bit_number = 1;
	last_zero = 0;
	rom_byte_number = 0;
	rom_byte_mask = 1;
	search_result = 0;

	// if the last call was not the last one
	if (!one_wire_struct->LastDeviceFlag)
	{
		// 1-Wire reset
		if (onewire_reset(one_wire_struct))
		{
			/* Reset the search */
			one_wire_struct->LastDiscrepancy = 0;
			one_wire_struct->LastDeviceFlag = 0;
			one_wire_struct->LastFamilyDiscrepancy = 0;
			return 0;
		}

		// issue the search command
		onewire_writebyte(one_wire_struct, command);

		// loop to do the search
		do {
			// read a bit and its complement
			id_bit = onewire_readbit(one_wire_struct);
			cmp_id_bit = onewire_readbit(one_wire_struct);

			// check for no devices on 1-wire
			if ((id_bit == 1) && (cmp_id_bit == 1)) {
				break;
			} else {
				// all devices coupled have 0 or 1
				if (id_bit != cmp_id_bit) {
					search_direction = id_bit;  // bit write value for search
				} else {
					// if this discrepancy if before the Last Discrepancy
					// on a previous next then pick the same as last time
					if (id_bit_number < one_wire_struct->LastDiscrepancy) {
						search_direction = ((one_wire_struct->ROM_NO[rom_byte_number] & rom_byte_mask) > 0);
					} else {
						// if equal to last pick 1, if not then pick 0
						search_direction = (id_bit_number == one_wire_struct->LastDiscrepancy);
					}

					// if 0 was picked then record its position in LastZero
					if (search_direction == 0) {
						last_zero = id_bit_number;

						// check for Last discrepancy in family
						if (last_zero < 9) {
							one_wire_struct->LastFamilyDiscrepancy = last_zero;
						}
					}
				}

				// set or clear the bit in the ROM byte rom_byte_number
				// with mask rom_byte_mask
				if (search_direction == 1) {
					one_wire_struct->ROM_NO[rom_byte_number] |= rom_byte_mask;
				} else {
					one_wire_struct->ROM_NO[rom_byte_number] &= ~rom_byte_mask;
				}

				// serial number search direction write bit
				onewire_writebit(one_wire_struct, search_direction);

				// increment the byte counter id_bit_number
				// and shift the mask rom_byte_mask
				id_bit_number++;
				rom_byte_mask <<= 1;

				// if the mask is 0 then go to new SerialNum byte rom_byte_number and reset mask
				if (rom_byte_mask == 0) {
					//docrc8(ROM_NO[rom_byte_number]);  // accumulate the CRC
					rom_byte_number++;
					rom_byte_mask = 1;
				}
			}
		} while (rom_byte_number < 8);  // loop until through all ROM bytes 0-7

		// if the search was successful then
		if (!(id_bit_number < 65)) {
			// search successful so set LastDiscrepancy,LastDeviceFlag,search_result
			one_wire_struct->LastDiscrepancy = last_zero;

			// check for last device
			if (one_wire_struct->LastDiscrepancy == 0) {
				one_wire_struct->LastDeviceFlag = 1;
			}

			search_result = 1;
		}
	}

	// if no device found then reset counters so next 'search' will be like a first
	if (!search_result || !one_wire_struct->ROM_NO[0]) {
		one_wire_struct->LastDiscrepancy = 0;
		one_wire_struct->LastDeviceFlag = 0;
		one_wire_struct->LastFamilyDiscrepancy = 0;
		search_result = 0;
	}

	return search_result;
}

int onewire_verify(onewire_t* one_wire_struct) {
	unsigned char rom_backup[8];
	int i,rslt,ld_backup,ldf_backup,lfd_backup;

	// keep a backup copy of the current state
	for (i = 0; i < 8; i++)
	rom_backup[i] = one_wire_struct->ROM_NO[i];
	ld_backup = one_wire_struct->LastDiscrepancy;
	ldf_backup = one_wire_struct->LastDeviceFlag;
	lfd_backup = one_wire_struct->LastFamilyDiscrepancy;

	// set search to find the same device
	one_wire_struct->LastDiscrepancy = 64;
	one_wire_struct->LastDeviceFlag = 0;

	if (onewire_search(one_wire_struct, ONEWIRE_CMD_SEARCHROM)) {
		// check if same device found
		rslt = 1;
		for (i = 0; i < 8; i++) {
			if (rom_backup[i] != one_wire_struct->ROM_NO[i]) {
				rslt = 1;
				break;
			}
		}
	} else {
		rslt = 0;
	}

	// restore the search state
	for (i = 0; i < 8; i++) {
		one_wire_struct->ROM_NO[i] = rom_backup[i];
	}
	one_wire_struct->LastDiscrepancy = ld_backup;
	one_wire_struct->LastDeviceFlag = ldf_backup;
	one_wire_struct->LastFamilyDiscrepancy = lfd_backup;

	// return the result of the verify
	return rslt;
}

void onewire_targetSetup(onewire_t* one_wire_struct, uint8_t family_code) {
   uint8_t i;

	// set the search state to find SearchFamily type devices
	one_wire_struct->ROM_NO[0] = family_code;
	for (i = 1; i < 8; i++) {
		one_wire_struct->ROM_NO[i] = 0;
	}

	one_wire_struct->LastDiscrepancy = 64;
	one_wire_struct->LastFamilyDiscrepancy = 0;
	one_wire_struct->LastDeviceFlag = 0;
}

void onewire_familyskipsetup(onewire_t* one_wire_struct) {
	// set the Last discrepancy to last family discrepancy
	one_wire_struct->LastDiscrepancy = one_wire_struct->LastFamilyDiscrepancy;
	one_wire_struct->LastFamilyDiscrepancy = 0;

	// check for end of list
	if (one_wire_struct->LastDiscrepancy == 0) {
		one_wire_struct->LastDeviceFlag = 1;
	}
}

uint8_t onewire_getrom(onewire_t* one_wire_struct, uint8_t index) {
	return one_wire_struct->ROM_NO[index];
}

void OneWire_Select(onewire_t* one_wire_struct, uint8_t* addr) {
	uint8_t i;
	onewire_writebyte(one_wire_struct, ONEWIRE_CMD_MATCHROM);

	for (i = 0; i < 8; i++) {
		onewire_writebyte(one_wire_struct, *(addr + i));
	}
}

void onewire_selectwithpointer(onewire_t* one_wire_struct, uint8_t *ROM) {
	uint8_t i;
	onewire_writebyte(one_wire_struct, ONEWIRE_CMD_MATCHROM);

	for (i = 0; i < 8; i++) {
		onewire_writebyte(one_wire_struct, *(ROM + i));
	}
}

void onewire_getfullrom(onewire_t* one_wire_struct, uint8_t *firstIndex) {
	uint8_t i;
	for (i = 0; i < 8; i++) {
		*(firstIndex + i) = one_wire_struct->ROM_NO[i];
	}
}

uint8_t onewire_crc8(uint8_t *addr, uint8_t len) {
	uint8_t crc = 0, inbyte, i, mix;

	while (len--) {
		inbyte = *addr++;
		for (i = 8; i; i--) {
			mix = (crc ^ inbyte) & 0x01;
			crc >>= 1;
			if (mix) {
				crc ^= 0x8C;
			}
			inbyte >>= 1;
		}
	}

	/* Return calculated CRC */
	return crc;
}