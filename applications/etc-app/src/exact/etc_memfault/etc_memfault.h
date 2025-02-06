#ifndef ETC_MEMFAULT_H_
#define ETC_MEMFAULT_H_

#ifdef CONFIG_MEMFAULT
#include <memfault/core/trace_event.h>

#define ETC_MEMFAULT_TRACE_EVENT(reason) MEMFAULT_TRACE_EVENT(reason)
#define ETC_MEMFAULT_TRACE_EVENT_WITH_STATUS(reason, status)                                       \
	MEMFAULT_TRACE_EVENT_WITH_STATUS(reason, status)
#define ETC_MEMFAULT_TRACE_EVENT_WITH_LOG(reason, ...)                                             \
	MEMFAULT_TRACE_EVENT_WITH_LOG(reason, __VA_ARGS__)
#else
#define ETC_MEMFAULT_TRACE_EVENT(reason)
#define ETC_MEMFAULT_TRACE_EVENT_WITH_STATUS(reason, status)
#define ETC_MEMFAULT_TRACE_EVENT_WITH_LOG(reason, ...)
#endif

int memfault_etc_device_id_set(const char *device_id, size_t len);

#endif /* ETC_MEMFAULT_H_ */