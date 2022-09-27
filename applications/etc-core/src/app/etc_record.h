/***************************************************************************/
/*!
\file       etc_record.h
\brief      Record manager

\product    General purpose
\processor  ARM Cortex M
\compiler   ANSI C

\author     Kien Bui
 */
/***************************************************************************/
#ifndef ETC_RECORD_H_
#define ETC_RECORD_H_

#include <stdint.h>
#include <stdbool.h>
/***************************************************************************/
/* Definitions                                                             */
/***************************************************************************/
#define RECORD_MANAGER_MAX_ELEMENTS (36)

typedef union {
    uint8_t bytes;
    struct {
        uint8_t is_data_ready : 1;
        uint8_t is_changed    : 1;
        uint8_t unused        : 6;
    } __packed;
} record_header_flag_t;

typedef struct
{
  uint8_t index;
  uint8_t write;
  uint8_t read;
  uint8_t change;
  record_header_flag_t flag[RECORD_MANAGER_MAX_ELEMENTS];
} record_header_t;

/***************************************************************************/
/* Prototypes                                                              */
/***************************************************************************/
/** @brief Initializes Record Manager
 *
 * @param None
 * @retval Zero if success
 */
int etc_record_init(void);

/** @brief Put data to queue storage flash
 *
 * @param data point to where data to be put
 * @param length length of putting data.
 * @retval Zero if success
 */
int etc_record_put(void* data, int length);

/** @brief Pop data to queue storage flash
 *
 * @param data point to where data to be pop
 * @param length length of poping data.
 * @retval Zero if success
 */
int etc_record_pop(void* data, int length);

/** @brief Get status of record
 *
 * @retval Zero if empty
 */
int etc_record_is_empty(void);

/** @brief Force to save header data
 *  
 * @param none
 * @retval none
 */
void etc_record_force_save(void);
#endif /* ETC_RECORD_H_ */