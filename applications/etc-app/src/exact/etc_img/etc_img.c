/*
 * Copyright (c) 2018-2021 mcumgr authors
 * Copyright (c) 2022-2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Copyright (c) 2023 EXACT Technology
 */

#include "etc_img.h"

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(etc_img, CONFIG_ETC_APP_LOG_LEVEL);

#define IMAGE_TLV_KEYHASH 0x01 /* hash of the public key */

extern int img_mgmt_read(int slot, unsigned int offset, void *dst,
			 unsigned int num_bytes);

#define ERASED_VAL_32(x) (((x) << 24) | ((x) << 16) | ((x) << 8) | (x))
extern int img_mgmt_erased_val(int slot, uint8_t *erased_val);

/**
 * Finds the TLVs in the specified image slot, if any.
 */
static int img_mgmt_find_tlvs(int slot, size_t *start_off, size_t *end_off,
			      uint16_t magic)
{
	struct image_tlv_info tlv_info;
	int rc;

	rc = img_mgmt_read(slot, *start_off, &tlv_info, sizeof(tlv_info));
	if (rc != 0) {
		/* Read error. */
		return MGMT_ERR_EUNKNOWN;
	}

	if (tlv_info.it_magic != magic) {
		/* No TLVs. */
		return MGMT_ERR_ENOENT;
	}

	*start_off += sizeof(tlv_info);
	*end_off = *start_off + tlv_info.it_tlv_tot;

	return 0;
}

int etc_img_get_pubkey_hash(uint8_t *hash)
{
	struct image_header hdr;
	struct image_tlv tlv;
	size_t data_off;
	size_t data_end;
	uint8_t erased_val;
	uint32_t erased_val_32;
	bool hash_found;
	int image_slot;
	int rc;

	image_slot = img_mgmt_active_slot(img_mgmt_active_image());

	rc = img_mgmt_erased_val(image_slot, &erased_val);
	if (rc != 0) {
		return -1;
	}
	erased_val_32 = ERASED_VAL_32(erased_val);

	rc = img_mgmt_read(image_slot, 0, &hdr, sizeof(hdr));
	if (rc != 0) {
		return -1;
	}

	if (hdr.ih_magic == erased_val_32) {
		return -1;
	} else if (hdr.ih_magic != IMAGE_MAGIC) {
		return -1;
	}

	data_off = hdr.ih_hdr_size + hdr.ih_img_size;

	rc = img_mgmt_find_tlvs(image_slot, &data_off, &data_end, 
				IMAGE_TLV_PROT_INFO_MAGIC);
	if (!rc) {
		/* The data offset should start after the header bytes after the end of
		 * the protected TLV, if one exists.
		 */
		data_off = data_end - sizeof(struct image_tlv_info);
	}

	rc = img_mgmt_find_tlvs(image_slot, &data_off, &data_end, 
				IMAGE_TLV_INFO_MAGIC);
	if (rc != 0) {
		return -1;
	}

	hash_found = false;
	while (data_off + sizeof(tlv) <= data_end) {
		rc = img_mgmt_read(image_slot, data_off, &tlv, sizeof(tlv));
		if (rc != 0) {
			return -1;
		}
		if (tlv.it_type == 0xff && tlv.it_len == 0xffff) {
			return -1;
		}
		if (tlv.it_type != IMAGE_TLV_KEYHASH || tlv.it_len != IMAGE_HASH_LEN) {
			/* Non-hash TLV.  Skip it. */
			data_off += sizeof(tlv) + tlv.it_len;
			continue;
		}

		hash_found = true;

		data_off += sizeof(tlv);
		if (hash != NULL) {
			if (data_off + IMAGE_HASH_LEN > data_end) {
				return -1;
			}
			rc = img_mgmt_read(image_slot, data_off, hash,
					   IMAGE_HASH_LEN);
			if (rc != 0) {
				return -1;
			}
			return 0;
		}
	}

	return -ENOENT;
}