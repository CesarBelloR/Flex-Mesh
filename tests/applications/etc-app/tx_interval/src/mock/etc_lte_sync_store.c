/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

/* RAM-backed stub: the LTE-sync predicate is tested through its parameters, so
 * these tests need only a linkable store without the retained-RAM backend. */

#include "etc_lte_sync_store.h"

static time_t stub_last_attempt;
static uint32_t stub_failures;

void etc_lte_sync_store_init(void)
{
	stub_last_attempt = 0;
	stub_failures = 0;
}

time_t etc_lte_sync_get_last_attempt(void)
{
	return stub_last_attempt;
}

uint32_t etc_lte_sync_get_failures(void)
{
	return stub_failures;
}

void etc_lte_sync_record_attempt(time_t now)
{
	stub_last_attempt = now;
}

void etc_lte_sync_record_success(void)
{
	stub_failures = 0;
}

void etc_lte_sync_record_failure(void)
{
	stub_failures++;
}
