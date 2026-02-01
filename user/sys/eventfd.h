#ifndef _SYS_EVENTFD_H
#define _SYS_EVENTFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../user.h"

// eventfd already defined in user.h:
// typedef uint64 eventfd_t;
// int eventfd(unsigned int initval, int flags);

// eventfd flags (already defined in user.h)
// #define EFD_CLOEXEC 02000000
// #define EFD_NONBLOCK 04000
// #define EFD_SEMAPHORE 00000001

// Helper functions (already defined in user.h)
// static inline int eventfd_read(int fd, eventfd_t *value);
// static inline int eventfd_write(int fd, eventfd_t value);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_EVENTFD_H */
