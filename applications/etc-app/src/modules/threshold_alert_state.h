/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef THRESHOLD_ALERT_STATE_H_
#define THRESHOLD_ALERT_STATE_H_

#include <stdint.h>

/** Alert flags of the EXACT Threshold slots awaiting delivery. */
struct threshold_alert_state {
	/** Slots (bit = slot index) that triggered and are not yet carried by a send. */
	uint8_t pending;
	/** Slots carried by the send in flight. */
	uint8_t in_flight;
};

/**
 * @brief Mark slots as triggered.
 *
 * @param s Alert state.
 * @param slots Bitmask of slots that triggered.
 */
static inline void threshold_alert_set(struct threshold_alert_state *s, uint8_t slots)
{
	s->pending |= slots;
}

/**
 * @brief Hand the pending slots to a send about to go out.
 *
 * Slots of an earlier send that has not settled yet stay in flight, so a take
 * never drops a flag still awaiting its ack.
 *
 * @param s Alert state.
 * @return Bitmask of slots the send must carry.
 */
static inline uint8_t threshold_alert_take_for_send(struct threshold_alert_state *s)
{
	s->in_flight |= s->pending;
	s->pending = 0;
	return s->in_flight;
}

/**
 * @brief Settle a send that was delivered.
 *
 * Slots that triggered again while the send was in flight stay pending, so the
 * next send carries them.
 *
 * @param s Alert state.
 * @return Bitmask of slots whose Alert flag must now be cleared.
 */
static inline uint8_t threshold_alert_send_acked(struct threshold_alert_state *s)
{
	uint8_t cleared = s->in_flight & ~s->pending;

	s->in_flight = 0;
	return cleared;
}

/**
 * @brief Settle a send that failed; its slots go back to pending for the retry.
 *
 * @param s Alert state.
 */
static inline void threshold_alert_send_failed(struct threshold_alert_state *s)
{
	s->pending |= s->in_flight;
	s->in_flight = 0;
}

#endif /* THRESHOLD_ALERT_STATE_H_ */
