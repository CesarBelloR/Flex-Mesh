/***************************************************************************/
/*!
\file       ds18s20.c
\brief      DS18S20 - Temperature and Humidity sensor

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#include "ds18b20.h"

ds18b20_sensor_t	ds18b20[DS18B20_MAX_SENSORS];

static onewire_t	OneWire;
static uint8_t		OneWireDevices;
static uint8_t		temp_sensor_count=0;
static uint16_t		ds18b20_timeout=0;

bool ds18b20_init(void)
{
	uint8_t ds18b20_try_to_find = 5;
	do
	{
		onewire_init(&OneWire, 28);
		temp_sensor_count = 0;
		while (k_uptime_get_32() < 3000)
		{
			DS18B20_DELAY(100);
		}

		OneWireDevices = onewire_first(&OneWire);
		while (OneWireDevices)
		{
			DS18B20_DELAY(100);
			temp_sensor_count++;
			onewire_getfullrom(&OneWire, ds18b20[temp_sensor_count - 1].address);
			OneWireDevices = onewire_next(&OneWire);
		}
		if (temp_sensor_count > 0)
			break;
		ds18b20_try_to_find--;
	} while (ds18b20_try_to_find > 0);
	if (ds18b20_try_to_find == 0)
		return false;

	for (uint8_t i = 0; i < temp_sensor_count; i++)
	{
		DS18B20_DELAY(50);
		ds18b20_setresolution(&OneWire, ds18b20[i].address, DS18B20_RESOLUTION_12BITS);
		DS18B20_DELAY(50);
		ds18b20_disablealarmtemperature(&OneWire, ds18b20[i].address);
	}
	return true;
}

bool		ds18b20_manualconvert(void)
{
	ds18b20_timeout= DS18B20_CONVERT_TIMEOUT_MS / 10;
	ds18b20_startall(&OneWire);
	DS18B20_DELAY(100);
	while (!ds18b20_alldone(&OneWire))
	{
		DS18B20_DELAY(10);
		ds18b20_timeout-=1;
		if(ds18b20_timeout==0)
			break;
	}
	if(ds18b20_timeout>0)
	{
		for (uint8_t i = 0; i < temp_sensor_count; i++)
		{
			DS18B20_DELAY(100);
			ds18b20[i].data_is_valid = ds18b20_read(&OneWire, ds18b20[i].address, &ds18b20[i].temperature);
		}
	}
	else
	{
		for (uint8_t i = 0; i < temp_sensor_count; i++)
			ds18b20[i].data_is_valid = false;
	}
	if (ds18b20_timeout == 0)
		return false;
	else
		return true;
}

int ds18b20_start(onewire_t* OneWire, uint8_t *ROM)
{
	/* Check if device is DS18B20 */
	if (!ds18b20_is(ROM)) {
		return -1;
	}

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Start temperature conversion */
	onewire_writebyte(OneWire, DS18B20_CMD_CONVERTTEMP);

	return 0;
}

void ds18b20_startall(onewire_t* OneWire)
{
	/* Reset pulse */
	onewire_reset(OneWire);
	/* Skip rom */
	onewire_writebyte(OneWire, ONEWIRE_CMD_SKIPROM);
	/* Start conversion on all connected devices */
	onewire_writebyte(OneWire, DS18B20_CMD_CONVERTTEMP);
}

bool ds18b20_read(onewire_t* OneWire, uint8_t *ROM, float *destination)
{
	uint16_t temperature;
	uint8_t resolution;
	int8_t digit, minus = 0;
	float decimal;
	uint8_t i = 0;
	uint8_t data[9];
	uint8_t crc;

	/* Check if device is DS18B20 */
	if (!ds18b20_is(ROM)) {
		return false;
	}

	/* Check if line is released, if it is, then conversion is complete */
	if (!onewire_readbit(OneWire))
	{
		/* Conversion is not finished yet */
		return false;
	}

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Read scratchpad command by onewire protocol */
	onewire_writebyte(OneWire, ONEWIRE_CMD_RSCRATCHPAD);

	/* Get data */
	for (i = 0; i < 9; i++)
	{
		/* Read byte by byte */
		data[i] = onewire_readbyte(OneWire);
	}

	/* Calculate CRC */
	crc = onewire_crc8(data, 8);

	/* Check if CRC is ok */
	if (crc != data[8])
		/* CRC invalid */
		return 0;


	/* First two bytes of scratchpad are temperature values */
	temperature = data[0] | (data[1] << 8);

	/* Reset line */
	onewire_reset(OneWire);

	/* Check if temperature is negative */
	if (temperature & 0x8000)
	{
		/* Two's complement, temperature is negative */
		temperature = ~temperature + 1;
		minus = 1;
	}


	/* Get sensor resolution */
	resolution = ((data[4] & 0x60) >> 5) + 9;


	/* Store temperature integer digits and decimal digits */
	digit = temperature >> 4;
	digit |= ((temperature >> 8) & 0x7) << 4;

	/* Store decimal digits */
	switch (resolution)
	{
	case 9:
		decimal = (temperature >> 3) & 0x01;
		decimal *= (float)DS18B20_DECIMAL_STEPS_9BIT;
		break;
	case 10:
		decimal = (temperature >> 2) & 0x03;
		decimal *= (float)DS18B20_DECIMAL_STEPS_10BIT;
		break;
	case 11:
		decimal = (temperature >> 1) & 0x07;
		decimal *= (float)DS18B20_DECIMAL_STEPS_11BIT;
		break;
	case 12:
		decimal = temperature & 0x0F;
		decimal *= (float)DS18B20_DECIMAL_STEPS_12BIT;
		break;
	default:
		decimal = 0xFF;
		digit = 0;
	}

	/* Check for negative part */
	decimal = digit + decimal;
	if (minus)
		decimal = 0 - decimal;


	/* Set to pointer */
	*destination = decimal;

	/* Return 1, temperature valid */
	return true;
}

uint8_t ds18b20_getresolution(onewire_t* OneWire, uint8_t *ROM)
{
	uint8_t conf;

	if (!ds18b20_is(ROM))
		return 0;

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Read scratchpad command by onewire protocol */
	onewire_writebyte(OneWire, ONEWIRE_CMD_RSCRATCHPAD);

	/* Ignore first 4 bytes */
	onewire_readbyte(OneWire);
	onewire_readbyte(OneWire);
	onewire_readbyte(OneWire);
	onewire_readbyte(OneWire);

	/* 5th byte of scratchpad is configuration register */
	conf = onewire_readbyte(OneWire);

	/* Return 9 - 12 value according to number of bits */
	return ((conf & 0x60) >> 5) + 9;
}

uint8_t ds18b20_setresolution(onewire_t* OneWire, uint8_t *ROM, ds18b20_resolution_e resolution)
{
	uint8_t th, tl, conf;
	if (!ds18b20_is(ROM))
		return 0;


	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Read scratchpad command by onewire protocol */
	onewire_writebyte(OneWire, ONEWIRE_CMD_RSCRATCHPAD);

	/* Ignore first 2 bytes */
	onewire_readbyte(OneWire);
	onewire_readbyte(OneWire);

	th = onewire_readbyte(OneWire);
	tl = onewire_readbyte(OneWire);
	conf = onewire_readbyte(OneWire);

	if (resolution == DS18B20_RESOLUTION_9BITS)
	{
		conf &= ~(1 << DS18B20_RESOLUTION_R1);
		conf &= ~(1 << DS18B20_RESOLUTION_R0);
	}
	else if (resolution == DS18B20_RESOLUTION_10BITS)
	{
		conf &= ~(1 << DS18B20_RESOLUTION_R1);
		conf |= 1 << DS18B20_RESOLUTION_R0;
	}
	else if (resolution == DS18B20_RESOLUTION_11BITS)
	{
		conf |= 1 << DS18B20_RESOLUTION_R1;
		conf &= ~(1 << DS18B20_RESOLUTION_R0);
	}
	else if (resolution == DS18B20_RESOLUTION_12BITS)
	{
		conf |= 1 << DS18B20_RESOLUTION_R1;
		conf |= 1 << DS18B20_RESOLUTION_R0;
	}

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Write scratchpad command by onewire protocol, only th, tl and conf register can be written */
	onewire_writebyte(OneWire, ONEWIRE_CMD_WSCRATCHPAD);

	/* Write bytes */
	onewire_writebyte(OneWire, th);
	onewire_writebyte(OneWire, tl);
	onewire_writebyte(OneWire, conf);

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Copy scratchpad to EEPROM of DS18B20 */
	onewire_writebyte(OneWire, ONEWIRE_CMD_CPYSCRATCHPAD);

	return 1;
}

uint8_t ds18b20_is(uint8_t *ROM)
{
	/* Checks if first byte is equal to DS18B20's family code */
	if (*ROM == DS18B20_FAMILY_CODE)
		return 1;

	return 0;
}

uint8_t ds18b20_setalarmlowtemperature(onewire_t* OneWire, uint8_t *ROM, int8_t temp)
{
	uint8_t tl, th, conf;
	if (!ds18b20_is(ROM))
		return 0;

	if (temp > 125)
		temp = 125;

	if (temp < -55)
		temp = -55;

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Read scratchpad command by onewire protocol */
	onewire_writebyte(OneWire, ONEWIRE_CMD_RSCRATCHPAD);

	/* Ignore first 2 bytes */
	onewire_readbyte(OneWire);
	onewire_readbyte(OneWire);

	th = onewire_readbyte(OneWire);
	tl = onewire_readbyte(OneWire);
	conf = onewire_readbyte(OneWire);

	tl = (uint8_t)temp;

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Write scratchpad command by onewire protocol, only th, tl and conf register can be written */
	onewire_writebyte(OneWire, ONEWIRE_CMD_WSCRATCHPAD);

	/* Write bytes */
	onewire_writebyte(OneWire, th);
	onewire_writebyte(OneWire, tl);
	onewire_writebyte(OneWire, conf);

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Copy scratchpad to EEPROM of DS18B20 */
	onewire_writebyte(OneWire, ONEWIRE_CMD_CPYSCRATCHPAD);

	return 1;
}

uint8_t ds18b20_setalarmhightemperature(onewire_t* OneWire, uint8_t *ROM, int8_t temp)
{
	uint8_t tl, th, conf;
	if (!ds18b20_is(ROM))
		return 0;

	if (temp > 125)
		temp = 125;

	if (temp < -55)
		temp = -55;

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Read scratchpad command by onewire protocol */
	onewire_writebyte(OneWire, ONEWIRE_CMD_RSCRATCHPAD);

	/* Ignore first 2 bytes */
	onewire_readbyte(OneWire);
	onewire_readbyte(OneWire);

	th = onewire_readbyte(OneWire);
	tl = onewire_readbyte(OneWire);
	conf = onewire_readbyte(OneWire);

	th = (uint8_t)temp;

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Write scratchpad command by onewire protocol, only th, tl and conf register can be written */
	onewire_writebyte(OneWire, ONEWIRE_CMD_WSCRATCHPAD);

	/* Write bytes */
	onewire_writebyte(OneWire, th);
	onewire_writebyte(OneWire, tl);
	onewire_writebyte(OneWire, conf);

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Copy scratchpad to EEPROM of DS18B20 */
	onewire_writebyte(OneWire, ONEWIRE_CMD_CPYSCRATCHPAD);

	return 1;
}

uint8_t ds18b20_disablealarmtemperature(onewire_t* OneWire, uint8_t *ROM)
{
	uint8_t tl, th, conf;
	if (!ds18b20_is(ROM))
		return 0;

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Read scratchpad command by onewire protocol */
	onewire_writebyte(OneWire, ONEWIRE_CMD_RSCRATCHPAD);

	/* Ignore first 2 bytes */
	onewire_readbyte(OneWire);
	onewire_readbyte(OneWire);

	th = onewire_readbyte(OneWire);
	tl = onewire_readbyte(OneWire);
	conf = onewire_readbyte(OneWire);

	th = 125;
	tl = (uint8_t)-55;

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Write scratchpad command by onewire protocol, only th, tl and conf register can be written */
	onewire_writebyte(OneWire, ONEWIRE_CMD_WSCRATCHPAD);

	/* Write bytes */
	onewire_writebyte(OneWire, th);
	onewire_writebyte(OneWire, tl);
	onewire_writebyte(OneWire, conf);

	/* Reset line */
	onewire_reset(OneWire);
	/* Select ROM number */
	onewire_selectwithpointer(OneWire, ROM);
	/* Copy scratchpad to EEPROM of DS18B20 */
	onewire_writebyte(OneWire, ONEWIRE_CMD_CPYSCRATCHPAD);

	return 1;
}

uint8_t ds18b20_alarmsearch(onewire_t* OneWire)
{
	/* Start alarm search */
	return onewire_search(OneWire, DS18B20_CMD_ALARMSEARCH);
}

uint8_t ds18b20_alldone(onewire_t* OneWire)
{
	/* If read bit is low, then device is not finished yet with calculation temperature */
	return onewire_readbit(OneWire);
}
