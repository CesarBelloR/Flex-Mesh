/***************************************************************************/
/*!
\file       ds2484.h
\brief      DS2484 - Bridge IC to support I2C to One-wire interface

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef ONEWIRE_DS2484_H_
#define ONEWIRE_DS2484_H_

#include <stdint.h>
#include <stdbool.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
#define DS248X_CONFIG_APU (1<<0)
#define DS248X_CONFIG_PPM (1<<1)
#define DS248X_CONFIG_SPU (1<<2)
#define DS248X_CONFIG_WS  (1<<3)

#define DS248X_STATUS_1WB  (1<<0)
#define DS248X_STATUS_PPD  (1<<1)
#define DS248X_STATUS_SD   (1<<2)
#define DS248X_STATUS_LL   (1<<3)
#define DS248X_STATUS_RST  (1<<4)
#define DS248X_STATUS_SBR  (1<<5)
#define DS248X_STATUS_TSB  (1<<6)
#define DS248X_STATUS_DIR  (1<<7)
#define DS248X_POLL_LIMIT  (10)
#define DS248X_CB_SHORT_CONDITION     1
#define DS248X_CB_RESET_CONDITION     2
#define DS248X_CB_DEVICE_RESET_NEEDED 3

#define DS248X_MAX_I2C_BUFF_SIZE (8)
typedef enum
{
    active_pull_up = DS248X_CONFIG_APU,
    strong_pull_up = DS248X_CONFIG_SPU,
    over_drive_speed = DS248X_CONFIG_WS
} ds248x_config_t;

/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes the DS2484 driver
 *
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int ds2484_init(void);

/** @brief Read the DS2484's status register
 * 
 * @param status Pointer to where the retrieved status will be stored.
 * 
 * @retval 0 on success. 
*/
int ds2484_read_status(uint8_t *status);

/** @brief Set configuration
 *
 * @param config configuration type @ref ds248x_config_t
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int ds2484_set_config(ds248x_config_t config);

/** @brief Clear the configuration
 *
 * @param config configuration type @ref ds248x_config_t
 * @retval return 0 on success, or a negative error code from an I2C
 * transaction or invalid parameter.
 */
int ds2484_clear_config(ds248x_config_t config);

/** @brief Load the configuration
 *
 * @retval return 0 on success.
 */
int ds2484_load_config(void);

/** @brief Get the value of the configuration register
 * 
 * @param config Pointer to buffer that the config value is stored in.
 * @retval return 0 on success.
*/
int ds2484_get_config(uint8_t *config);

/** @brief Reset the device
 *
 * @retval return 0 on success.
 */
int ds2484_device_reset(void);

/** @brief Write a byte to one-wire
 *
 * @param data a byte data to write
 * @retval return 0 on success.
 */
int ds2484_write_byte(uint8_t data);

/** @brief Read a byte to one-wire
 *
 * @param data pointer to store a byte data
 * @retval return 0 on success.
 */
int ds2484_read_byte(uint8_t* data);

/** @brief Write bytes to one-wire
 *
 * @param data bytes data to write
 * @param len lenght of data to write
 * @retval return 0 on success.
 */
int ds2484_write_bytes(const uint8_t* data, size_t len);

/** @brief Read bytes to one-wire
 *
 * @param data pointer to store bytes data
 * @param len lenght of data to write
 * @retval return 0 on success.
 */
int ds2484_read_bytes(uint8_t* data, size_t  len);

/** @brief Write a bit to one-wire
 *
 * @param data a bit [0:1] data to write
 * @retval return 0 on success.
 */
int ds2484_write_bit(bool bit);

/** @brief Read a bit to one-wire
 *
 * @param data pointer to store a bit [0:1]
 * @retval return 0 on success.
 */
int ds2484_read_bit(bool* data);

/** @brief Send a request reset over one-wire
 *
 * @retval return 0 on success.
 */
int ds2484_request_reset(void);

/** @brief Send a request skip over one-wire
 *
 * @retval return 0 on success.
 */
int ds2484_request_skip(void);

/** @brief Send a request select over one-wire
 *
 * @param rom unique address of device
 * @retval return 0 on success.
 */
int ds2484_request_select(const char* rom);

/** @brief Send a request search for device on 1-wire bus
 *
 * @param rom place to put the unique address of device
 * @retval return 0 on success.
 */
int ds2484_request_search(char* rom);

/** @brief Reset search for next usage
 *
 * @retval return 0 on success.
 */
int ds2484_request_reset_search(void);

/** @brief Set a family code for the next search.
 *
 * @retval return 0 on success.
 */
int ds2484_request_search_family(uint8_t family_code);

int ds2484_crc_validate(uint8_t* data, size_t len);
#endif /* ONEWIRE_DS2484_H_ */
