#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_mgmt, CONFIG_ETC_INTERFACE_LOG_LEVEL);
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <mgmt/mcumgr/util/zcbor_bulk.h>
#include <string.h>
#include <stdio.h>
#include <zcbor_common.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/drivers/flash.h>
#include "etc_device.h"
#include "etc_mgmt.h"

#define ETC_MGMT_RECORD_BUF_SIZE (256)

/**
 * Encodes a response.
 */
static bool etc_mgmt_rsp(zcbor_state_t *zse, int rc)
{
	bool ok;

	ok = zcbor_tstr_put_lit(zse, "rc")	&&
	     zcbor_int32_put(zse, rc);

	return ok;
}

/**
 * Command handler: record status
 */
static int etc_mgmt_record_status(struct smp_streamer *ctxt)
{
	zcbor_state_t *zse = ctxt->writer->zs;
	zcbor_state_t *zsd = ctxt->reader->zs;
	bool ok;
	size_t element_size = etc_device_get_record_element_size();
	struct etc_device_record_table record_table = etc_device_get_record_status();
	/* Encode the response. */
	ok = etc_mgmt_rsp(zse, MGMT_ERR_EOK)							&&
		zcbor_tstr_put_lit(zse, "record")  						&& 
		zcbor_map_start_encode(zse, 0) 							&& 
		zcbor_tstr_put_lit(zse, "first_record") 					&& 
		zcbor_map_start_encode(zse, 0) 							&& 
		zcbor_tstr_put_lit(zse, "index")						&& 
		zcbor_uint32_put(zse, (uint32_t)record_table.oldest.element_idx)		&&
		zcbor_tstr_put_lit(zse, "sector")						&&
		zcbor_uint32_put(zse, (uint32_t)record_table.oldest.sector_idx)			&&
		zcbor_map_end_encode(zse, 0) 							&&
		zcbor_tstr_put_lit(zse, "last_record")  					&&
		zcbor_map_start_encode(zse, 0) 							&&
		zcbor_tstr_put_lit(zse, "index")						&&
		zcbor_uint32_put(zse, (uint32_t)record_table.newest.element_idx)		&&
		zcbor_tstr_put_lit(zse, "sector")						&&
		zcbor_uint32_put(zse, (uint32_t)record_table.newest.sector_idx)     		&&
		zcbor_map_end_encode(zse, 0) 							&&
		zcbor_map_end_encode(zse, 0) 							&& 
		zcbor_tstr_put_lit(zse, "info")  						&&
		zcbor_map_start_encode(zse, 0) 							&&
		zcbor_tstr_put_lit(zse, "element_size")  					&&
		zcbor_uint32_put(zse, (uint32_t)element_size)					&&
		zcbor_tstr_put_lit(zse, "max_index")						&& 
		zcbor_uint32_put(zse, (uint32_t)etc_device_get_record_max_element_index()) 	&&
		zcbor_tstr_put_lit(zse, "max_sector")					 	&& 
		zcbor_uint32_put(zse, (uint32_t)etc_device_get_record_max_sector_index()) 	&&
		zcbor_tstr_put_lit(zse, "total")						&& 
		zcbor_uint32_put(zse, (uint32_t)record_table.total)				&&
		zcbor_map_end_encode(zse, 0);

	return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}
/**
 * Command handler: etc record
 */
static int etc_mgmt_record_read(struct smp_streamer *ctxt)
{
	uint8_t buf[ETC_MGMT_RECORD_BUF_SIZE];
	uint64_t off = ULLONG_MAX;
	size_t length = 0;
	size_t record_len = etc_device_get_record_size();
	off_t record_offset = etc_device_get_record_offset();
	int rc;
	zcbor_state_t *zse = ctxt->writer->zs;
	zcbor_state_t *zsd = ctxt->reader->zs;
	bool ok;
	size_t decoded;

	struct zcbor_map_decode_key_val record_read_decode[] = {
		ZCBOR_MAP_DECODE_KEY_VAL(off, zcbor_uint64_decode, &off),
		ZCBOR_MAP_DECODE_KEY_VAL(length, zcbor_uint32_decode, &length),
	};

	ok = zcbor_map_decode_bulk(zsd, record_read_decode,
		ARRAY_SIZE(record_read_decode), &decoded) == 0;

	if (!ok || off == ULLONG_MAX || length == 0 || length > ETC_MGMT_RECORD_BUF_SIZE) {
		return MGMT_ERR_EINVAL;
	}

	rc = flash_read(etc_device_get_record(), record_offset + off, buf, length);
	if (rc != 0) {
		return MGMT_ERR_EINVAL;
	}
	/* Encode the response. */
	ok = etc_mgmt_rsp(zse, MGMT_ERR_EOK)				&&
	     zcbor_tstr_put_lit(zse, "data")					&&
	     zcbor_bstr_encode_ptr(zse, buf, length)			&&
	     ((off != 0)							||
		(zcbor_tstr_put_lit(zse, "len") && zcbor_uint64_put(zse, record_len)));

	return ok ? MGMT_ERR_EOK : MGMT_ERR_EMSGSIZE;
}

static const struct mgmt_handler etc_mgmt_handlers[] = {
	[ETC_MGMT_ID_RECORD_STATUS] = {
		.mh_read = etc_mgmt_record_status,
	},
	[ETC_MGMT_ID_RECORD_READ] = {
		.mh_read = etc_mgmt_record_read,
	}
};

#define ETC_MGMT_HANDLER_CNT ARRAY_SIZE(etc_mgmt_handlers)

static struct mgmt_group etc_mgmt_group = {
	.mg_handlers = etc_mgmt_handlers,
	.mg_handlers_count = ETC_MGMT_HANDLER_CNT,
	.mg_group_id = MGMT_GROUP_ID_ETC,
};

void etc_mgmt_register_group(void)
{
	mgmt_register_group(&etc_mgmt_group);
}
