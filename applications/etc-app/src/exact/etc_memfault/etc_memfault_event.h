/*
 * Copyright (c) 2024 EXACT Technology
 *
 */
#ifndef ETC_MEMFAULT_EVENT_H_
#define ETC_MEMFAULT_EVENT_H_

#if defined(CONFIG_ZTEST)
#define ETC_MFLT_PUT_IN_SECTION(x)
#define ETC_MFLT_EXPORT_FUNC

/**
 * REUSE the  MemfaultEventReadCallback from memfault for ZTEST ONLY
 */
typedef bool(MemfaultEventReadCallback)(uint32_t offset, void *buf, size_t buf_len);
#else
#define ETC_MFLT_PUT_IN_SECTION(x) __attribute__((section(x)))
#define ETC_MFLT_EXPORT_FUNC	   static
#endif

/**
 * @brief Initializes the event management system.
 *
 * This function prepares the event management structure for use
 * by resetting internal fields and setting up the initial state.
 * It should be called before any operations on the event system.
 */
void memfault_etc_event_init(void);

#endif /* ETC_MEMFAULT_EVENT_H_ */