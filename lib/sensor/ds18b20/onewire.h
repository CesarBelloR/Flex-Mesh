#ifndef ONEWIRE_H
#define ONEWIRE_H

#include <stdint.h>
#include <stdbool.h>

/* C++ detection */
#ifdef __cplusplus
extern "C" {
#endif

#define	OneWireDelay(x)			k_msleep(x)

typedef struct {
	int GPIO_Pin;             /*!< GPIO Pin to be used for I/O functions */
	uint8_t LastDiscrepancy;       /*!< Search private */
	uint8_t LastFamilyDiscrepancy; /*!< Search private */
	uint8_t LastDeviceFlag;        /*!< Search private */
	uint8_t ROM_NO[8];             /*!< 8-bytes address of last search device */
} onewire_t;

/* OneWire delay */
void onewire_delay(uint16_t time_us);

/* Pin settings */
void onewire_low(onewire_t *gp);
void onewire_high(onewire_t *gp);
void onewire_input(onewire_t *gp);
void onewire_output(onewire_t *gp);

/* OneWire commands */
#define ONEWIRE_CMD_RSCRATCHPAD			0xBE
#define ONEWIRE_CMD_WSCRATCHPAD			0x4E
#define ONEWIRE_CMD_CPYSCRATCHPAD		0x48
#define ONEWIRE_CMD_RECEEPROM			0xB8
#define ONEWIRE_CMD_RPWRSUPPLY			0xB4
#define ONEWIRE_CMD_SEARCHROM			0xF0
#define ONEWIRE_CMD_READROM				0x33
#define ONEWIRE_CMD_MATCHROM			0x55
#define ONEWIRE_CMD_SKIPROM				0xCC

void onewire_init(onewire_t* one_wire_struct, int pin);
uint8_t onewire_reset(onewire_t* one_wire_struct);
uint8_t onewire_readbyte(onewire_t* one_wire_struct);
void onewire_writebyte(onewire_t* one_wire_struct, uint8_t byte);
void onewire_writebit(onewire_t* one_wire_struct, uint8_t bit);
uint8_t onewire_readbit(onewire_t* one_wire_struct);
uint8_t onewire_search(onewire_t* one_wire_struct, uint8_t command);
void onewire_resetsearch(onewire_t* one_wire_struct);
uint8_t onewire_first(onewire_t* one_wire_struct);
uint8_t onewire_next(onewire_t* one_wire_struct);
void onewire_getfullrom(onewire_t* one_wire_struct, uint8_t *firstindex);
void onewire_select(onewire_t* one_wire_struct, uint8_t* addr);
void onewire_selectwithpointer(onewire_t* one_wire_struct, uint8_t* rom);
uint8_t onewire_crc8(uint8_t* addr, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif
