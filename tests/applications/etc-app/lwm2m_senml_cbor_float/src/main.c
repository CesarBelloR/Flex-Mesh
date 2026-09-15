/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "lwm2m_engine.h"
#include "lwm2m_rw_senml_cbor.h"
#include "lwm2m_senml_cbor_encode.h"
#include "lwm2m_senml_cbor_types.h"

#define TEST_OBJ_ID	 0xFFFE
#define TEST_OBJ_INST_ID 0

#define TEST_RES_S32   0
#define TEST_RES_FLOAT 1
#define TEST_RES_BOOL  2

#define TEST_OBJ_RES_MAX_ID 3

#define TEST_PATH_S32	"/65534/0/0"
#define TEST_PATH_FLOAT "/65534/0/1"
#define TEST_PATH_BOOL	"/65534/0/2"

static struct lwm2m_engine_obj test_obj;
static struct lwm2m_engine_obj_inst test_inst;
static struct lwm2m_engine_res test_res[TEST_OBJ_RES_MAX_ID];
static struct lwm2m_engine_res_inst test_res_inst[TEST_OBJ_RES_MAX_ID];

static int32_t test_s32;
static double test_float;
static bool test_bool;

static struct lwm2m_engine_obj_field test_fields[] = {
	OBJ_FIELD_DATA(TEST_RES_S32, RW, S32),
	OBJ_FIELD_DATA(TEST_RES_FLOAT, RW, FLOAT),
	OBJ_FIELD_DATA(TEST_RES_BOOL, RW, BOOL),
};

static struct lwm2m_engine_obj_inst *test_obj_create(uint16_t obj_inst_id)
{
	int i = 0, j = 0;

	ARG_UNUSED(obj_inst_id);

	init_res_instance(test_res_inst, ARRAY_SIZE(test_res_inst));

	INIT_OBJ_RES_DATA(TEST_RES_S32, test_res, i, test_res_inst, j, &test_s32, sizeof(test_s32));
	INIT_OBJ_RES_DATA(TEST_RES_FLOAT, test_res, i, test_res_inst, j, &test_float,
			  sizeof(test_float));
	INIT_OBJ_RES_DATA(TEST_RES_BOOL, test_res, i, test_res_inst, j, &test_bool,
			  sizeof(test_bool));

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

static struct lwm2m_ctx test_ctx;
static struct lwm2m_message test_msg;
static uint8_t test_payload[64];

/*
 * Build a one-record SenML CBOR write payload for @p path carrying a single
 * value of the requested union choice, e.g. for an integer 10:
 *
 *   81                 array(1)
 *   A2                 map(2)
 *     21               key -2 (bn)
 *     6A 2F 36 35 ...  text(10) "/65534/0/1"
 *     02               key 2 (v)
 *     0A               unsigned(10)
 *
 * A float value uses the same keys with a CBOR float (0xFB ...) instead.
 */
static size_t build_payload(const char *path, int choice, int64_t vi, double vf)
{
	static struct lwm2m_senml senml;
	struct record *rec = &senml.lwm2m_senml_record_m[0];
	size_t len = 0;
	int err;

	memset(&senml, 0, sizeof(senml));
	senml.lwm2m_senml_record_m_count = 1;

	rec->record_bn_present = true;
	rec->record_bn.record_bn.value = (const uint8_t *)path;
	rec->record_bn.record_bn.len = strlen(path);

	rec->record_union_present = true;
	rec->record_union.record_union_choice = choice;
	if (choice == union_vi_c) {
		rec->record_union.union_vi = vi;
	} else {
		rec->record_union.union_vf = vf;
	}

	err = cbor_encode_lwm2m_senml(test_payload, sizeof(test_payload), &senml, &len);
	zassert_equal(err, 0, "Payload encode failed: %d", err);

	return len;
}

/** @brief Feed a hand-built payload through the SenML CBOR write path. */
static int write_payload(size_t len)
{
	memset(&test_msg, 0, sizeof(test_msg));

	test_msg.ctx = &test_ctx;
	test_msg.operation = LWM2M_OP_WRITE;

	test_msg.in.reader = &senml_cbor_reader;
	test_msg.in.in_cpkt = &test_msg.cpkt;
	test_msg.in.offset = 0;

	test_msg.cpkt.data = test_payload;
	test_msg.cpkt.offset = len;
	test_msg.cpkt.max_len = len;

	return do_write_op_senml_cbor(&test_msg);
}

static void test_prepare(void *dummy)
{
	ARG_UNUSED(dummy);

	memset(&test_ctx, 0, sizeof(test_ctx));

	test_s32 = 42;
	test_float = 3.14;
	test_bool = false;
}

/* Coiote encodes a whole number written to a Float resource as a CBOR integer. */
ZTEST(lwm2m_senml_cbor_float, test_write_integer_to_float_resource)
{
	int ret;

	ret = write_payload(build_payload(TEST_PATH_FLOAT, union_vi_c, 10, 0.0));
	zassert_equal(ret, 0, "Write failed: %d", ret);
	zassert_within(test_float, 10.0, 1e-9, "Expected 10.0, got %f", test_float);
}

ZTEST(lwm2m_senml_cbor_float, test_write_negative_integer_to_float_resource)
{
	int ret;

	ret = write_payload(build_payload(TEST_PATH_FLOAT, union_vi_c, -5, 0.0));
	zassert_equal(ret, 0, "Write failed: %d", ret);
	zassert_within(test_float, -5.0, 1e-9, "Expected -5.0, got %f", test_float);
}

ZTEST(lwm2m_senml_cbor_float, test_write_float_to_float_resource)
{
	int ret;

	ret = write_payload(build_payload(TEST_PATH_FLOAT, union_vf_c, 0, 10.5));
	zassert_equal(ret, 0, "Write failed: %d", ret);
	zassert_within(test_float, 10.5, 1e-9, "Expected 10.5, got %f", test_float);
}

/* Symmetric case: a whole number for an Integer resource encoded as a CBOR float. */
ZTEST(lwm2m_senml_cbor_float, test_write_float_to_integer_resource)
{
	int ret;

	ret = write_payload(build_payload(TEST_PATH_S32, union_vf_c, 0, 7.0));
	zassert_equal(ret, 0, "Write failed: %d", ret);
	zassert_equal(test_s32, 7, "Expected 7, got %d", test_s32);
}

ZTEST(lwm2m_senml_cbor_float, test_write_integer_to_integer_resource)
{
	int ret;

	ret = write_payload(build_payload(TEST_PATH_S32, union_vi_c, 123, 0.0));
	zassert_equal(ret, 0, "Write failed: %d", ret);
	zassert_equal(test_s32, 123, "Expected 123, got %d", test_s32);
}

/* A Boolean resource must reject a numeric value rather than store garbage. */
ZTEST(lwm2m_senml_cbor_float, test_write_integer_to_bool_resource_rejected)
{
	int ret;

	ret = write_payload(build_payload(TEST_PATH_BOOL, union_vi_c, 1, 0.0));
	zassert_not_equal(ret, 0, "Write of integer to Boolean resource should fail");
	zassert_false(test_bool, "Boolean resource must keep its previous value");
}

ZTEST_SUITE(lwm2m_senml_cbor_float, NULL, test_obj_init, test_prepare, NULL, NULL);
