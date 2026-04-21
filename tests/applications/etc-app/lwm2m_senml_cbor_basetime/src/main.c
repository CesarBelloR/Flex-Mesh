/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "lwm2m_engine.h"
#include "lwm2m_rw_senml_cbor.h"
#include "lwm2m_senml_cbor_decode.h"
#include "lwm2m_senml_cbor_types.h"

#define TEST_OBJ_ID	 0xFFFE
#define TEST_OBJ_INST_ID 0

#define TEST_RES_S32   0
#define TEST_RES_FLOAT 1
#define TEST_RES_TIME  2

#define TEST_OBJ_RES_MAX_ID 3

/* 2025-01-01 00:00:00.125 UTC — fractional component exercises the float bt path. */
#define TEST_BASETIME ((double)1735689600.125)

/* CoAP content-format option (2 bytes) + payload marker (1 byte). */
#define TEST_PAYLOAD_OFFSET 3

static bool basetime_disabled;

double lwm2m_rw_senml_cbor_basetime(void)
{
	return basetime_disabled ? 0.0 : TEST_BASETIME;
}

static struct lwm2m_engine_obj test_obj;
static struct lwm2m_engine_obj_inst test_inst;
static struct lwm2m_engine_res test_res[TEST_OBJ_RES_MAX_ID];
static struct lwm2m_engine_res_inst test_res_inst[TEST_OBJ_RES_MAX_ID];

static int32_t test_s32;
static double test_float;
static time_t test_time;

static struct lwm2m_engine_obj_field test_fields[] = {
	OBJ_FIELD_DATA(TEST_RES_S32, RW, S32),
	OBJ_FIELD_DATA(TEST_RES_FLOAT, RW, FLOAT),
	OBJ_FIELD_DATA(TEST_RES_TIME, RW, TIME),
};

static struct lwm2m_engine_obj_inst *test_obj_create(uint16_t obj_inst_id)
{
	int i = 0, j = 0;

	ARG_UNUSED(obj_inst_id);

	init_res_instance(test_res_inst, ARRAY_SIZE(test_res_inst));

	INIT_OBJ_RES_DATA(TEST_RES_S32, test_res, i, test_res_inst, j, &test_s32, sizeof(test_s32));
	INIT_OBJ_RES_DATA(TEST_RES_FLOAT, test_res, i, test_res_inst, j, &test_float,
			  sizeof(test_float));
	INIT_OBJ_RES_DATA(TEST_RES_TIME, test_res, i, test_res_inst, j, &test_time,
			  sizeof(test_time));

	test_inst.resources = test_res;
	test_inst.resource_count = i;

	return &test_inst;
}

static void *test_obj_init(void)
{
	struct lwm2m_engine_obj_inst *obj_inst = NULL;

	test_obj.obj_id = TEST_OBJ_ID;
	test_obj.version_major = 1;
	test_obj.version_minor = 0;
	test_obj.is_core = false;
	test_obj.fields = test_fields;
	test_obj.field_count = ARRAY_SIZE(test_fields);
	test_obj.max_instance_count = 1U;
	test_obj.create_cb = test_obj_create;

	(void)lwm2m_register_obj(&test_obj);
	(void)lwm2m_create_obj_inst(TEST_OBJ_ID, TEST_OBJ_INST_ID, &obj_inst);

	return NULL;
}

static struct lwm2m_message test_msg;

static void context_reset(void)
{
	memset(&test_msg, 0, sizeof(test_msg));

	test_msg.out.writer = &senml_cbor_writer;
	test_msg.out.out_cpkt = &test_msg.cpkt;

	test_msg.in.reader = &senml_cbor_reader;
	test_msg.in.in_cpkt = &test_msg.cpkt;

	test_msg.path.obj_id = TEST_OBJ_ID;
	test_msg.path.obj_inst_id = TEST_OBJ_INST_ID;

	test_msg.cpkt.data = test_msg.msg_data;
	test_msg.cpkt.max_len = sizeof(test_msg.msg_data);
}

static void test_prepare(void *dummy)
{
	ARG_UNUSED(dummy);

	basetime_disabled = false;
	context_reset();

	test_s32 = 42;
	test_float = 3.14;
	test_time = (time_t)1700000000;
}

static int decode_payload(struct lwm2m_senml *out)
{
	size_t payload_len_out;
	size_t payload_len;

	if (test_msg.cpkt.offset < TEST_PAYLOAD_OFFSET) {
		return -EINVAL;
	}

	payload_len = test_msg.cpkt.offset - TEST_PAYLOAD_OFFSET;

	return cbor_decode_lwm2m_senml(test_msg.msg_data + TEST_PAYLOAD_OFFSET, payload_len, out,
				       &payload_len_out);
}

ZTEST(lwm2m_senml_cbor_basetime, test_single_resource_has_bt)
{
	static struct lwm2m_senml decoded;
	const struct record *r0;
	int ret;

	test_msg.path.level = LWM2M_PATH_LEVEL_RESOURCE;
	test_msg.path.res_id = TEST_RES_S32;

	ret = do_read_op_senml_cbor(&test_msg);
	zassert_true(ret >= 0, "Encode failed: %d", ret);

	ret = decode_payload(&decoded);
	zassert_equal(ret, 0, "Decode failed: %d", ret);

	zassert_equal(decoded.lwm2m_senml_record_m_count, 1, "Unexpected record count: %zu",
		      decoded.lwm2m_senml_record_m_count);

	r0 = &decoded.lwm2m_senml_record_m[0];
	zassert_true(r0->record_bt_present, "First record missing bt");
	zassert_equal(r0->record_bt.record_bt_choice, record_bt_float_c, "bt choice not float: %d",
		      (int)r0->record_bt.record_bt_choice);
	zassert_within(r0->record_bt.record_bt_float, TEST_BASETIME, 1e-4, "bt value wrong: %f",
		       r0->record_bt.record_bt_float);
	zassert_false(r0->record_t_present, "First record should not emit t");
}

ZTEST(lwm2m_senml_cbor_basetime, test_multi_resource_has_single_bt)
{
	static struct lwm2m_senml decoded;
	const struct record *r0;
	int ret;

	test_msg.path.level = LWM2M_PATH_LEVEL_OBJECT_INST;

	ret = do_read_op_senml_cbor(&test_msg);
	zassert_true(ret >= 0, "Encode failed: %d", ret);

	ret = decode_payload(&decoded);
	zassert_equal(ret, 0, "Decode failed: %d", ret);

	zassert_true(decoded.lwm2m_senml_record_m_count >= 2, "Expected >=2 records, got %zu",
		     decoded.lwm2m_senml_record_m_count);

	r0 = &decoded.lwm2m_senml_record_m[0];
	zassert_true(r0->record_bt_present, "First record missing bt");
	zassert_equal(r0->record_bt.record_bt_choice, record_bt_float_c, "bt choice not float: %d",
		      (int)r0->record_bt.record_bt_choice);
	zassert_within(r0->record_bt.record_bt_float, TEST_BASETIME, 1e-4, "bt value wrong: %f",
		       r0->record_bt.record_bt_float);
	zassert_false(r0->record_t_present, "First record should not emit t");

	for (size_t i = 1; i < decoded.lwm2m_senml_record_m_count; i++) {
		const struct record *ri = &decoded.lwm2m_senml_record_m[i];

		zassert_false(ri->record_bt_present, "Record %zu should not carry bt", i);
		zassert_false(ri->record_t_present, "Record %zu should not carry t", i);
	}
}

ZTEST(lwm2m_senml_cbor_basetime, test_bt_preserves_milliseconds)
{
	static struct lwm2m_senml decoded;
	const struct record *r0;
	int ret;

	test_msg.path.level = LWM2M_PATH_LEVEL_RESOURCE;
	test_msg.path.res_id = TEST_RES_S32;

	ret = do_read_op_senml_cbor(&test_msg);
	zassert_true(ret >= 0, "Encode failed: %d", ret);

	ret = decode_payload(&decoded);
	zassert_equal(ret, 0, "Decode failed: %d", ret);

	r0 = &decoded.lwm2m_senml_record_m[0];
	zassert_true(r0->record_bt_present, "bt missing");
	zassert_equal(r0->record_bt.record_bt_choice, record_bt_float_c,
		      "bt choice must be float to preserve ms");
	zassert_within(r0->record_bt.record_bt_float, TEST_BASETIME, 1e-3,
		       "ms precision lost: got %f want %f", r0->record_bt.record_bt_float,
		       (double)TEST_BASETIME);
}

ZTEST(lwm2m_senml_cbor_basetime, test_no_bt_when_clock_unavailable)
{
	static struct lwm2m_senml decoded;
	int ret;

	basetime_disabled = true;

	test_msg.path.level = LWM2M_PATH_LEVEL_OBJECT_INST;

	ret = do_read_op_senml_cbor(&test_msg);
	zassert_true(ret >= 0, "Encode failed: %d", ret);

	ret = decode_payload(&decoded);
	zassert_equal(ret, 0, "Decode failed: %d", ret);

	for (size_t i = 0; i < decoded.lwm2m_senml_record_m_count; i++) {
		const struct record *ri = &decoded.lwm2m_senml_record_m[i];

		zassert_false(ri->record_bt_present, "Record %zu should not carry bt", i);
		zassert_false(ri->record_t_present, "Record %zu should not carry t", i);
	}
}

ZTEST_SUITE(lwm2m_senml_cbor_basetime, NULL, test_obj_init, test_prepare, NULL, NULL);
