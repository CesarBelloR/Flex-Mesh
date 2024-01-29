#ifndef ETC_DEVICE_H_
#define ETC_DEVICE_H_

#include <zephyr/shell/shell.h>

#include "etc_device_record.h"

#define ETC_CONFIG_TYPE_SIZE   (32)
#define ETC_DEVICE_RECORD_SIZE (36)
#define ETC_DEVICE_NUM_SENSOR  (6) // 5 temperatures + 1 humidity
#define ETC_DEVICE_LOGGER_LORA_SYNC_CLOUD_OFFSET_HOUR (16)

/**
 * @brief Define a flash sector size based on HW
 *
 */
#define ETC_FLASH_SECTOR_SIZE	  (4096)
/**
 * @brief Define maximum record per sector
 */
#define ETC_RECORD_MAX_PER_SECTOR ((int)(ETC_FLASH_SECTOR_SIZE) / (ETC_DEVICE_RECORD_SIZE))
/**
 * @brief Maximum record in requirement
 */
#define ETC_RECORD_MAX_RECORD	  (90 * 24 * 4)
/**
 * @brief Max sector = round(fit sector + 1 free sector for swap) (ETC_RECORD_MAX_RECORD /
 * ETC_RECORD_MAX_PER_SECTOR) + 1
 */
#define ETC_RECORD_MAX_SECTOR	  ((int)((ETC_RECORD_MAX_RECORD) / (ETC_RECORD_MAX_PER_SECTOR)) + 1)

#define ETC_RECORD_ID_HEADER(x) (x + ETC_RECORD_HEADER)
#define ETC_RECORD_ID(x) (x - ETC_RECORD_HEADER)

#define MAX_RECORD_ID ( ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR + ETC_RECORD_HEADER - 1)
#define MIN_RECORD_ID ( ETC_RECORD_HEADER )
#define MAX_RECORD_NO_OFFSET_ID ( ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR - 1) 
#define MIN_RECORD_NO_OFFSET_ID ( 0 )

enum etc_setting_id {
	ETC_RECORD_HEADER = 0x1000,
};

void etc_device_init(void);
struct etc_device_record_index etc_device_write(void);
void etc_device_read(void);
void etc_device_report(const struct shell *shell);
void etc_device_export_old_structure(const struct shell *shell);
uint8_t* etc_device_dump(void);
#endif /* ETC_DEVICE_H_ */