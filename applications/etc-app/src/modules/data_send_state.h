/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef DATA_SEND_STATE_H_
#define DATA_SEND_STATE_H_

#include <stdbool.h>

/** Send bookkeeping for one output channel (cloud or BLE). */
struct data_send_state {
	/** Record id of the reading being sent, 0 if none. */
	int record_id;
	/** A send is in flight and awaiting its ACK or failure. */
	bool active;
};

static inline void data_send_state_begin(struct data_send_state *state)
{
	state->active = true;
}

/**
 * @brief Finish the in-flight send.
 *
 * @return The record id that was being sent, 0 if none.
 */
static inline int data_send_state_finish(struct data_send_state *state)
{
	int record_id = state->record_id;

	state->record_id = 0;
	state->active = false;
	return record_id;
}

#endif /* DATA_SEND_STATE_H_ */
