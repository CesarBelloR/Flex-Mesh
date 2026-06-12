/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <stdbool.h>

#include "etc_util.h"

ZTEST_SUITE(etc_util_test, NULL, NULL, NULL, NULL, NULL);


ZTEST(etc_util_test, test_parse_for_float_ok)
{
	const char *valid_float = "14.6829";
	float result;
	int ret;

	ret = parse_for_float(valid_float, &result);
	zassert_ok(ret, "parse_for_float was not ok");
	zassert_within(result, 14.6829f, 0.0001f);
}

ZTEST(etc_util_test, test_parse_for_float_invalid)
{
	const char *float_str = "abcd\4\5";
	float result;
	int ret;

	ret = parse_for_float(float_str, &result);
	zassert_not_ok(ret, "parse_for_float result invalid");
}

ZTEST(etc_util_test, test_parse_for_float_non_digit_prefix)
{
	const char *float_str = "JY-9.2451";
	float result;
	int ret;

	ret = parse_for_float(float_str, &result);
	zassert_not_ok(ret, "parse_for_float result invalid");
}

ZTEST(etc_util_test, test_parse_for_float_non_digit_suffix)
{
	const char *float_str = "+9.2451\1\10";
	float result;
	int ret;

	ret = parse_for_float(float_str, &result);
	zassert_not_ok(ret, "parse_for_float result invalid");
}

ZTEST(etc_util_test, test_parse_for_float_iv)
{
	const char *float_str = "\x15";
	float result;
	int ret;

	ret = parse_for_float(float_str, &result);
	zassert_equal(ret, 1);
}

ZTEST(etc_util_test, test_parse_for_int_ok)
{
	const char *valid_int = "153";
	int result;
	int ret;

	ret = parse_for_int(valid_int, &result);
	zassert_ok(ret, "parse_for_int was not ok");
	zassert_equal(result, 153);
}

ZTEST(etc_util_test, test_parse_for_int_invalid)
{	
	const char *int_str = "abcd\4\5";
	int result;
	int ret;

	ret = parse_for_int(int_str, &result);
	zassert_not_ok(ret, "parse_for_int result invalid");
}

ZTEST(etc_util_test, test_parse_for_int_non_digit_prefix)
{
	const char *int_str = "us#-915";
	int result;
	int ret;

	ret = parse_for_int(int_str, &result);
	zassert_not_ok(ret, "parse_for_float result invalid");
}

ZTEST(etc_util_test, test_parse_for_int_non_digit_suffix)
{
	const char *int_str = "+754\25t";
	int result;
	int ret;

	ret = parse_for_int(int_str, &result);
	zassert_not_ok(ret, "parse_for_float result invalid");
}

ZTEST(etc_util_test, test_parse_for_int_iv)
{
	const char *int_str = "\x15";
	int result;
	int ret;

	ret = parse_for_int(int_str, &result);
	zassert_equal(ret, 1);
}

ZTEST(etc_util_test, test_parse_reclaim_ok)
{
	int ret;
	ret = 0;
	const char* command = "0=\'RECLAIM:1111,1722234338,1722236338\'";
	struct relay_reclaim_request request = {0};
	ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_ok(ret, "parse reclaim relay command was not ok");
	zassert_equal(request.subcmd, RELAY_RECLAIM_SUBCMD_ADD);
	ret = strcmp(request.logger_id, "1111");
	zassert_ok(ret, "logger id was not match");
	zassert_equal(request.start_time, 1722234338);
	zassert_equal(request.stop_time, 1722236338);
}

ZTEST(etc_util_test, test_parse_reclaim_count_ok)
{
	const char *command = "0=\'RECLAIM:count\'";
	struct relay_reclaim_request request = {0};
	int ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_ok(ret);
	zassert_equal(request.subcmd, RELAY_RECLAIM_SUBCMD_COUNT);
}

ZTEST(etc_util_test, test_parse_reclaim_clear_ok)
{
	const char *command = "0=\'RECLAIM:clear\'";
	struct relay_reclaim_request request = {0};
	int ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_ok(ret);
	zassert_equal(request.subcmd, RELAY_RECLAIM_SUBCMD_CLEAR);
}

ZTEST(etc_util_test, test_parse_reclaim_get_idx_ok)
{
	const char *command = "0=\'RECLAIM:i,7\'";
	struct relay_reclaim_request request = {0};
	int ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_ok(ret);
	zassert_equal(request.subcmd, RELAY_RECLAIM_SUBCMD_GET_IDX);
	zassert_equal(request.index, 7);
}

ZTEST(etc_util_test, test_parse_reclaim_get_idx_missing_value)
{
	const char *command = "0=\'RECLAIM:i,\'";
	struct relay_reclaim_request request = {0};
	int ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_equal(ret, -EINVAL);
}

ZTEST(etc_util_test, test_parse_reclaim_get_idx_missing_comma)
{
	const char *command = "0=\'RECLAIM:i\'";
	struct relay_reclaim_request request = {0};
	int ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	/* Without the comma "i" is treated as a logger id and parsed as LIST_LOGGER. */
	zassert_ok(ret);
	zassert_equal(request.subcmd, RELAY_RECLAIM_SUBCMD_LIST_LOGGER);
	zassert_ok(strcmp(request.logger_id, "i"));
}

ZTEST(etc_util_test, test_parse_reclaim_get_idx_non_numeric)
{
	const char *command = "0=\'RECLAIM:i,abc\'";
	struct relay_reclaim_request request = {0};
	int ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_equal(ret, -EINVAL);
}

ZTEST(etc_util_test, test_parse_reclaim_list_logger_ok)
{
	const char *command = "0=\'RECLAIM:12000747\'";
	struct relay_reclaim_request request = {0};
	int ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_ok(ret);
	zassert_equal(request.subcmd, RELAY_RECLAIM_SUBCMD_LIST_LOGGER);
	zassert_ok(strcmp(request.logger_id, "12000747"));
}

ZTEST(etc_util_test, test_parse_reclaim_list_logger_oversized)
{
	/* UTIL_LOGGER_ID_SIZE = 17 (16 chars + null). 17 'F' chars must be rejected. */
	const char *command = "0=\'RECLAIM:FFFFFFFFFFFFFFFFF\'";
	struct relay_reclaim_request request = {0};
	int ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_equal(ret, -EINVAL);
}

ZTEST(etc_util_test, test_parse_reclaim_unknown_keyword)
{
	const char *command = "0=\'RECLAIM:bogus,1,2\'";
	struct relay_reclaim_request request = {0};
	int ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	/* "bogus" is treated as a logger_id, then start/stop must parse. */
	zassert_ok(ret);
	zassert_equal(request.subcmd, RELAY_RECLAIM_SUBCMD_ADD);
}

ZTEST(etc_util_test, test_parse_reclaim_trailing_junk_after_count)
{
	const char *command = "0=\'RECLAIM:count,1\'";
	struct relay_reclaim_request request = {0};
	int ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_equal(ret, -EINVAL);
}

ZTEST(etc_util_test, test_parse_reclaim_nok_1)
{
	int ret;
	ret = 0;
	const char* command = "0=\'TEST:1111,1722234338,1722236338\'";
	struct relay_reclaim_request request = {0};
	ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_equal(ret, -EINVAL);
}

ZTEST(etc_util_test, test_parse_reclaim_nok_2)
{
	int ret;
	ret = 0;
	const char* command = "0=\'RECLAIM:1111,,1722236338\'";
	struct relay_reclaim_request request = {0};
	ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_equal(ret, -EINVAL);
}

ZTEST(etc_util_test, test_parse_reclaim_nok_3)
{
	int ret;
	ret = 0;
	const char* command = "0=\'RECLAIM:1111,,\'";
	struct relay_reclaim_request request = {0};
	ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_equal(ret, -EINVAL);
}

ZTEST(etc_util_test, test_parse_reclaim_nok_4)
{
	int ret;
	ret = 0;
	const char* command = "0=\'RECLAIM:,,\'";
	struct relay_reclaim_request request = {0};
	ret = etc_common_parser_reclaim_replay_command(command, strlen(command), &request);
	zassert_equal(ret, -EINVAL);
}
