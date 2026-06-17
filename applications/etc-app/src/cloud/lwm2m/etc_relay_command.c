/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/lwm2m.h>

#include "etc_relay_command.h"
#include "etc_device.h"
#include "etc_relay_reclaim.h"
#include "etc_util.h"
#include "etc_relay_obj_48935.h"

LOG_MODULE_REGISTER(etc_relay_command, CONFIG_ETC_APP_LOG_LEVEL);

static const char *etc_relay_errno_name(int err)
{
	switch (err) {
	case -EINVAL:
		return "EINVAL";
	case -ENOMEM:
		return "ENOMEM";
	case -ENOENT:
		return "ENOENT";
	default:
		return "EIO";
	}
}

struct etc_relay_list_ctx {
	const char *logger_id;
	char *buf;
	size_t buf_size;
	size_t pos;
	int matches;
	bool truncated;
};

static void etc_relay_list_visitor(int idx, const struct etc_device_reclaim_request *req,
				   void *ctx_v)
{
	struct etc_relay_list_ctx *ctx = ctx_v;

	if (strncmp(ctx->logger_id, req->logger_id, ETC_DEVICE_LORA_LOGGER_ID_SIZE) != 0) {
		return;
	}
	ctx->matches++;
	if (ctx->truncated || ctx->pos + 1 >= ctx->buf_size) {
		ctx->truncated = true;
		return;
	}
	int written = snprintf(ctx->buf + ctx->pos, ctx->buf_size - ctx->pos,
			       "\nidx=%d,start=%d,stop=%d,created=%d", idx, req->start_time,
			       req->stop_time, req->created_at);
	if (written < 0 || (size_t)written >= ctx->buf_size - ctx->pos) {
		ctx->truncated = true;
		return;
	}
	ctx->pos += (size_t)written;
}

static void etc_relay_response_set(const char *text)
{
	int err = lwm2m_set_string(&LWM2M_OBJ(ETC_RELAY_OBJECT_ID, 0, ETC_RELAY_OBJ_R_RESPONSE),
				   text);
	if (err) {
		LOG_WRN("Failed to set Relay Response resource: %d", err);
	}
}

static void etc_relay_response_format(char *buf, size_t buf_size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int written = vsnprintf(buf, buf_size, fmt, ap);
	va_end(ap);
	if (written < 0) {
		buf[0] = '\0';
		etc_relay_response_set(buf);
		return;
	}
	if ((size_t)written >= buf_size) {
		const char *marker = "...";
		size_t marker_len = strlen(marker);
		if (buf_size > marker_len) {
			memcpy(&buf[buf_size - 1 - marker_len], marker, marker_len);
			buf[buf_size - 1] = '\0';
		}
	}
	etc_relay_response_set(buf);
}

/* Write an "ERR:<name>" reply for a negative errno and return that errno (which
 * becomes the CoAP Execute status). */
static int etc_relay_fail(char *buf, size_t buf_size, int rc)
{
	etc_relay_response_format(buf, buf_size, "ERR:%s", etc_relay_errno_name(rc));
	return rc;
}

int etc_relay_command_dispatch(const char *buf, size_t len)
{
	char response[ETC_RELAY_OBJ_RESPONSE_MAX_LEN];
	struct relay_reclaim_request request = {0};
	int rc = etc_common_parser_reclaim_replay_command(buf, len, &request);
	if (rc < 0) {
		return etc_relay_fail(response, sizeof(response), rc);
	}

	switch (request.subcmd) {
	case RELAY_RECLAIM_SUBCMD_ADD: {
		int set_rc = etc_relay_reclaim_set(request.logger_id, request.start_time,
						   request.stop_time);
		if (set_rc < 0) {
			return etc_relay_fail(response, sizeof(response), set_rc);
		}
		etc_relay_response_format(response, sizeof(response),
					  set_rc == 1 ? "OK,evicted=1" : "OK");
		return 0;
	}
	case RELAY_RECLAIM_SUBCMD_COUNT: {
		int count = etc_relay_reclaim_count();
		if (count < 0) {
			return etc_relay_fail(response, sizeof(response), count);
		}
		etc_relay_response_format(response, sizeof(response), "count=%d", count);
		return 0;
	}
	case RELAY_RECLAIM_SUBCMD_GET_IDX: {
		struct etc_device_reclaim_request entry;
		int get_rc = etc_relay_reclaim_get_by_index(request.index, &entry);
		if (get_rc < 0) {
			return etc_relay_fail(response, sizeof(response), get_rc);
		}
		etc_relay_response_format(response, sizeof(response),
					  "idx=%d,logger=%s,start=%d,stop=%d,created=%d",
					  request.index, entry.logger_id, entry.start_time,
					  entry.stop_time, entry.created_at);
		return 0;
	}
	case RELAY_RECLAIM_SUBCMD_LIST_LOGGER: {
		struct etc_relay_list_ctx ctx = {
			.logger_id = request.logger_id,
			.buf = response,
			.buf_size = sizeof(response),
			.pos = 0,
			.matches = 0,
			.truncated = false,
		};
		int visit_rc = etc_relay_reclaim_for_each(etc_relay_list_visitor, &ctx);
		if (visit_rc < 0) {
			return etc_relay_fail(response, sizeof(response), visit_rc);
		}
		char header[24];
		int header_len = snprintf(header, sizeof(header), "count=%d", ctx.matches);
		if (header_len < 0) {
			return etc_relay_fail(response, sizeof(response), -EIO);
		}
		if ((size_t)header_len + ctx.pos + 1 > sizeof(response)) {
			ctx.truncated = true;
		}
		const char *marker = ctx.truncated ? "..." : "";
		size_t marker_len = strlen(marker);
		if (ctx.truncated) {
			size_t avail = sizeof(response) - 1 - (size_t)header_len - marker_len;
			if (ctx.pos > avail) {
				ctx.pos = avail;
			}
		}
		memmove(response + header_len, response, ctx.pos);
		memcpy(response, header, header_len);
		memcpy(response + header_len + ctx.pos, marker, marker_len);
		response[header_len + ctx.pos + marker_len] = '\0';
		etc_relay_response_set(response);
		return 0;
	}
	case RELAY_RECLAIM_SUBCMD_CLEAR: {
		int cleared = etc_relay_reclaim_clear_all();
		etc_relay_response_format(response, sizeof(response), "OK,cleared=%d", cleared);
		return 0;
	}
	default:
		return etc_relay_fail(response, sizeof(response), -EINVAL);
	}
}
