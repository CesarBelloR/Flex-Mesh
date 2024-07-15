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
	zassert_within(result, 14.6829, 0.0001);
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
	zassert_ok(ret, "parse_for_float result invalid");
	zassert_within(result, -9.2451, 0.0001);
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
	zassert_ok(ret, "parse_for_int result invalid");
	zassert_equal(result, -915);
}