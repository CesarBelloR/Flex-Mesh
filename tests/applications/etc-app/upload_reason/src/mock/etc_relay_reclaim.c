/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

/* Stub: these tests do not exercise the relay reclaim feature; provide no-op
 * implementations so etc_device.c links without the retained-RAM backend. */

#include "etc_relay_reclaim.h"

void etc_relay_reclaim_init(void)
{
}

int etc_relay_reclaim_clear_all(void)
{
	return 0;
}
