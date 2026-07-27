/*
 * Copyright (c) 2026 EXACT Technology Corporation
 */

#ifndef MOCK_DEPS_H_
#define MOCK_DEPS_H_

#include <stdbool.h>
#include <stdint.h>

#define MOCK_NUM_PORTS 4
/* Raw ADC that etc_sensor_set_type() classifies as nothing connected. */
#define MOCK_ADC_OPEN 4095

enum mock_branch {
	MOCK_BRANCH_A = 0,
	MOCK_BRANCH_B,
	MOCK_BRANCH_COUNT,
};

/** @brief What is physically wired to one of the four front-end ports. */
struct mock_port {
	/** A splitter is attached, so the port has two switchable branches. */
	bool splitter;
	/** Raw ADC each branch answers with. */
	int adc[MOCK_BRANCH_COUNT];
	/** Branch the splitter is currently pointing at. */
	enum mock_branch branch;
	/** Fault injection: refuse to actuate towards this branch, -1 for none. */
	int switch_fail_branch;
};

struct mock_state {
	struct mock_port port[MOCK_NUM_PORTS];
	/** Counts front-end ADC reads. Also used to jitter consecutive reads of
	 * one probe by an LSB, the way a real median-of-N sampler does. */
	int adc_reads;
};

extern struct mock_state mock;

/** @brief Reset to four open ports with no splitters. */
void mock_reset(void);

/** @brief Attach a splitter with the given raw ADC on each branch. */
void mock_attach_splitter(int port, int adc_a, int adc_b);

/** @brief Attach a plain probe with the given raw ADC, no splitter. */
void mock_attach_probe(int port, int adc);

/** @brief Logical port the board mux points at, read back from the emulated
 * select GPIOs. Negative if the select pins are not driven.
 */
int mock_selected_port(void);

/** @brief Temperature the mocked NTC conversion yields for a raw ADC count. */
float mock_temperature(int raw_adc);

#endif /* MOCK_DEPS_H_ */
