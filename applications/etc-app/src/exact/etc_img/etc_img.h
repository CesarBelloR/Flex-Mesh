/*
 * Copyright (c) 2023 EXACT Technology
 *
 */
#ifndef ETC_IMG_TOOLS_H__
#define ETC_IMG_TOOLS_H__

#include <stdint.h>

#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt.h>
#include <zephyr/mgmt/mcumgr/grp/img_mgmt/img_mgmt_client.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>

int etc_img_get_pubkey_hash(uint8_t *hash);

#endif /* ETC_IMG_TOOLS_H__ */