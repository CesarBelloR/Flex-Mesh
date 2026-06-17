/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef ETC_RELAY_COMMAND_H__
#define ETC_RELAY_COMMAND_H__

#include <stddef.h>

/**
 * @brief Dispatch a relay command string received over LwM2M.
 *
 * The string is the execute argument from the EXACT Relay 48935/0/3 Execute
 * resource and looks like `0='<command>:<arg1>,<arg2>,...'`. Currently only
 * the `RECLAIM:` family of subcommands is supported (see FW-991 plan file
 * for the full grammar).
 *
 * The textual reply is written to the EXACT Relay Response resource
 * (48935/0/4). The return value becomes the CoAP Execute response status so
 * that failures such as a full reclaim buffer surface to the cloud as a
 * failed Execute rather than only as a side-channel resource update.
 *
 * @return 0 on success or a negative errno on failure.
 */
int etc_relay_command_dispatch(const char *buf, size_t len);

#endif /* ETC_RELAY_COMMAND_H__ */
