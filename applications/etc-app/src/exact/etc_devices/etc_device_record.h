#ifndef ETC_DEVICE_RECORD_H_
#define ETC_DEVICE_RECORD_H_

#define ETC_DEVICE_RECORD_BUF_SIZE (1088)
#define ETC_DEVICE_RECORD_FLAG (0xCAFEBEEF)

/**
 * @brief Define a flash sector size based on HW
 *
 */
#define ETC_FLASH_SECTOR_SIZE	  (4096)

/**
 * @brief Define maximum record per sector
 */
#ifndef CONFIG_ETC_RECORD_MAX_PER_SECTOR
#define ETC_RECORD_MAX_PER_SECTOR ((int)(ETC_FLASH_SECTOR_SIZE) / (ETC_DEVICE_RECORD_SIZE))
#else
#define ETC_RECORD_MAX_PER_SECTOR CONFIG_ETC_RECORD_MAX_PER_SECTOR
#endif
/**
 * @brief Maximum record in requirement
 */
#define ETC_RECORD_MAX_RECORD	  (90 * 24 * 4)

/**
 * @brief Max sector = round(fit sector + 1 free sector for swap) (ETC_RECORD_MAX_RECORD /
 * ETC_RECORD_MAX_PER_SECTOR) + 1
 */
#ifndef CONFIG_ETC_RECORD_MAX_SECTOR
#define ETC_RECORD_MAX_SECTOR	  ((int)((ETC_RECORD_MAX_RECORD) / (ETC_RECORD_MAX_PER_SECTOR)) + 1)
#else
#define ETC_RECORD_MAX_SECTOR CONFIG_ETC_RECORD_MAX_SECTOR
#endif

#define ETC_RECORD_ID_HEADER(x) (x + ETC_RECORD_HEADER)
#define ETC_RECORD_ID(x) (x - ETC_RECORD_HEADER)

#define MAX_RECORD_ID ( ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR + ETC_RECORD_HEADER  - 1)
#define MIN_RECORD_ID ( ETC_RECORD_HEADER )
#define MAX_RECORD_NO_OFFSET_ID ( ETC_RECORD_MAX_SECTOR * ETC_RECORD_MAX_PER_SECTOR - 1)
#define MIN_RECORD_NO_OFFSET_ID ( 0 )

#pragma pack(push, 1)

struct etc_device_record_index { // Constant in flash until the index is override (exflash)
	uint8_t sector_idx;
	uint8_t element_idx;
};

struct etc_device_record_table {
	struct etc_device_record_index oldest;
	struct etc_device_record_index newest;
	uint16_t total;
	uint16_t last_ack_record_id;
};

struct etc_device_record_data {
	struct etc_device_record_table record_stat;
	/* Record bits array to store ACK/NACK data */
	uint8_t record_bits[ETC_DEVICE_RECORD_BUF_SIZE];
	/* Flag to determine data is synced */
	uint32_t record_sync_flag;
};

union etc_device_record_header {
	uint8_t header;
	struct {
		uint8_t ready: 1;
		uint8_t ack: 1;
		uint8_t wait: 1;
		uint8_t unused: 3;
	};
};

#pragma pack(pop)

/**
 * @brief Define a callback function for record reading
 * 
 */
typedef int (*etc_device_record_reading_callback)(uint16_t record_id, void* user_data);

/**
 * @brief Initialize the device record in retain RAM.
 * 
 * Sets up the device record system for use. This might involve setting up initial values
 * if the retain RAM area not ready.
 */
void etc_device_record_init(void);

/**
 * @brief Retrieve the acknowledgement status for a specific record.
 * 
 * @param record_id The ID of the record for which the ack status needs to be fetched.
 * @return int Returns the ack status (usually 0 for no ack, 1 for ack).
 */
int etc_device_record_get_ack(int record_id);

/**
 * @brief Set the acknowledgement status for a specific record.
 * 
 * @param record_id The ID of the record for which the ack status needs to be set.
 */
void etc_device_record_set_ack(int record_id);

/**
 * @brief Set the negative acknowledgement status for a specific record.
 * 
 * @param record_id The ID of the record for which the nack status needs to be set.
 */
void etc_device_record_set_nack(int record_id);

/**
 * @brief Retrieve the nack status for multiple records.
 * 
 * @param new_record The index of the newest record
 * @param old_record The index of the oldest record
 * @param num_record The number of records to check for nack status.
 * @return Return the index of NACK (Greater or equal 0). Otherwise (negative) is no NACK
 */
int etc_device_record_get_nack(int new_record, int old_record, int num_record);

/**
 * @brief Save the record to NVS as backup to avoid losing data
 */
void etc_device_record_save(void);

/**
 * @brief Get number of ACK record in array ACKs
 * 
 * @return Number of ACK record
 */
uint16_t etc_device_record_get_num_ack(void);

/**
 * @brief Clean up record in RAM 
 */
void etc_device_record_clean_up(void);

/**
 * @brief Save the record stat 
 */
void etc_device_record_save_stat(void);

/**
 * @brief Records a reading for a specific device.
 *
 * This function records a reading for the specified device with the given record ID.
 *
 * @param record_id The unique identifier for the record.
 * @param data A pointer to the data associated with the reading.
 * @return 0 if successful, otherwise an error code.
 */
int etc_device_record_reading(uint16_t record_id, void *data);

/**
 * @brief Retrieves the latest record ID in the device's records.
 *
 * @return The latest record ID.
 */
uint16_t etc_device_record_get_latest_id(void);

/**
 * @brief Retrieves the oldest record ID in the device's records.
 *
 * @return The oldest record ID.
 */
uint16_t etc_device_record_get_oldest_id(void);

/**
 * @brief Retrieves the total number of records stored in the device.
 *
 * @return The total number of records.
 */
uint16_t etc_device_record_get_total_record(void);

/**
 * @brief Retrieves the address offset for a record based on its index.
 *
 * @param index The index of the record.
 * @return The address offset for the specified record index.
 */
off_t etc_device_record_get_addr_offset_by_index(struct etc_device_record_index index);

/**
 * @brief Retrieves the record ID for a record based on its index.
 *
 * @param index The index of the record.
 * @return The record ID for the specified record index.
 */
uint16_t etc_device_record_get_id_by_index(struct etc_device_record_index index);

/**
 * @brief Retrieves the record index for a given record ID.
 *
 * @param record_id The record ID.
 * @return The record index for the specified record ID.
 */
struct etc_device_record_index etc_device_get_index_by_id(uint16_t record_id);

/**
 * @brief Calculates the device record index based on the given address offset.
 *
 * This function reverses the calculation performed in etc_deviced_record_get_addr_offset_by_index
 * to retrieve the device record index corresponding to the given address offset.
 *
 * @param offset The address offset for which to calculate the device record index.
 * @return The device record index corresponding to the given address offset.
 */
struct etc_device_record_index etc_device_get_index_by_addr_offset(off_t offset);

/**
 * @brief Writes data to the specified address offset in the device's records.
 *
 * @param addr The address offset where the data will be written.
 * @param data A pointer to the data to be written.
 * @param data_len The length of the data to be written.
 * @return 0 if successful, otherwise an error code.
 */
int etc_device_record_write_data(off_t addr, void* data, int data_len);

/**
 * @brief Reads data from the specified address offset in the device's records.
 *
 * @param addr The address offset from where the data will be read.
 * @param data A pointer to the buffer where the read data will be stored.
 * @param data_len The length of the data to be read.
 * @return 0 if successful, otherwise an error code.
 */
int etc_device_record_read_data(off_t addr, void* data, int data_len);

/**
 * 
 * @brief Reclaim records in the ETC device within a specified time range.
 *
 * @param start_time	The start time of the reclaim range.
 * @param stop_time	The stop time of the reclaim range.
 * @return	0 on success, an error code otherwise.
 */
int etc_device_record_reclaim(int start_time, int stop_time);

/**
 * @brief Get the status of records in the ETC device.
 *
 * @return	A structure containing the status of records.
 */
struct etc_device_record_table* etc_device_record_get_status(void);

/**
 * @brief Get the header of a specific record in the ETC device.
 *
 * @param element	The element index of the record.
 * @param sector	The sector index of the record.
 * @param header	A pointer to store the read record header.
 * @return	0 on success, an error code otherwise.
 */
int etc_device_record_get_header(uint8_t element, uint8_t sector, 
	union etc_device_record_header *header);

/**
 * @brief Get the next index in record 
 * 
 * @return next index (sector, element) for new record 
 */
struct etc_device_record_index etc_device_record_get_next_index(void);

/**
 *  @brief Find one NACK record
 * 
 * @param reading_callback callback function will be invoked when found NACK
 * @param data callback data
 * @param active_reclaim Buffer to store the current reclaim status. True if
 * reclaim is active.
 * 
 * @return Record ID > 0 if success. Otherwise 0 if no NACK or < 0 if error
 */
int etc_device_record_find_nack(etc_device_record_reading_callback reading_callback, 
	void *data, bool *active_reclaim);

/**
 * @brief Get a pointer to the current record in the ETC device.
 *
 * @return	A pointer to the current record.
 */
const struct device* etc_device_record_get(void);

/**
 * @brief Get the size of the current record in the ETC device.
 *
 * @return	The size of the current record.
 */
size_t etc_device_record_get_size(void);

/**
 * @brief Get the offset of the current record in the ETC device.
 *
 * @return	The offset of the current record.
 */
off_t etc_device_record_get_offset(void);

/**
 * @brief Get the maximum element index for records in the ETC device.
 *
 * @return	The maximum element index.
 */
size_t etc_device_record_get_max_element_index(void);

/**
 * @brief Get the maximum sector index for records in the ETC device.
 *
 * @return	The maximum sector index.
 */
size_t etc_device_record_get_max_sector_index(void);

/**
 * @brief Get the size of each record element in the ETC device.
 *
 * @return	The size of each record element.
 */
size_t etc_device_record_get_element_size(void);

/**
 * @brief This API will send the number of records to be sent to upper 
 * layer as requested for reclamation
 * 
 * @return Number of records
 */
int etc_device_record_num_reclaim_records(void);
#endif /* ETC_DEVICE_RECORD_H_ */