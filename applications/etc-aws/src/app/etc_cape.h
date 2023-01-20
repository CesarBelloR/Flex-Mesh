/***************************************************************************/
/*!
\file       etc_cape.h
\brief      ETC encryption/decryption method

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef ETC_CAPE_H_
#define ETC_CAPE_H_

#include <stdint.h>
#include <stdbool.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes the ETC encrypt/decrypt feature
 *
 * @param key input for encrypt/decrypt
 * @param length of key.
 * @param s salt value
 * @retval None
 */
void etc_cape_init(const char* key, int length, char s);

/** @brief Decrypt the source data
 *
 * @param source to decrypt data
 * @param destination to store decrypted data.
 * @param length of decrypted data.
 * @retval None
 */
void etc_cape_decrypt(char *source, char *destination, uint16_t length);

/** @brief Encrypt the source data
 *
 * @param source to encrypt data
 * @param destination to store encrypted data.
 * @param length of decrypted data.
 * @param iv
 * @retval None
 */
void etc_cape_encrypt( char *source, char *destination, uint16_t length, uint8_t iv);

/** @brief Set key for encrypt/decrypt data
 *
 * @param key input for encrypt/decrypt
 * @param length of key.
 * @retval None
 */
void etc_cape_set_key(char *key, uint16_t length);

#endif /* ETC_CAPE_H_ */