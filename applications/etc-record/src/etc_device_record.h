#ifndef ETC_DEVICE_RECORD_H_
#define ETC_DEVICE_RECORD_H_

#define ETC_DEVICE_RECORD_BUF_SIZE (1088)
#define ETC_DEVICE_RECORD_FLAG (0xCAFEBEEF)

struct etc_device_record_index { // Constant in flash until the index is override (exflash)
	uint8_t sector_idx;
	uint8_t element_idx;
};

struct etc_device_record_table {
	struct etc_device_record_index oldest;
	struct etc_device_record_index newest;
	uint16_t total;
};

struct etc_device_record_data {
	struct etc_device_record_table record_stat;
	/* Record bits array to store ACK/NACK data */
	uint8_t record_bits[ETC_DEVICE_RECORD_BUF_SIZE];
	/* Flag to determine data is synced */
	uint32_t record_sync_flag;
};

extern struct etc_device_record_data etc_device_record;

void etc_device_record_init(void);
int  etc_device_record_get_ack(int record_id);
void etc_device_record_set_ack(int record_id);
void etc_device_record_set_nack(int record_id);
uint8_t* etc_device_record_dump(void);
void etc_device_record_clean_up(void);
void etc_device_record_save_stat(void);
#endif /* ETC_DEVICE_RECORD_H_ */