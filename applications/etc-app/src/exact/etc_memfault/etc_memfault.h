#ifndef ETC_MEMFAULT_H_
#define ETC_MEMFAULT_H_

#ifdef CONFIG_MEMFAULT
#include <memfault/core/trace_event.h>

#define ETC_MEMFAULT_TRACE_EVENT(reason)    MEMFAULT_TRACE_EVENT(reason)
#endif

#define ETC_MEMFAULT_TRACE_EVENT(reason)    

#endif /* ETC_MEMFAULT_H_ */