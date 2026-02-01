#ifndef XV6_EVENTFD_H
#define XV6_EVENTFD_H

#include "types.h"
#include "spinlock.h"

// eventfd flags
#define EFD_CLOEXEC   (1 << 0)  // 0x1 - Close on exec
#define EFD_NONBLOCK  (1 << 1)  // 0x2 - Non-blocking I/O
#define EFD_SEMAPHORE (1 << 2)  // 0x4 - Semaphore semantics

// eventfd 结构体
struct eventfd {
  struct spinlock lock;   // 保护计数器和等待队列
  uint64 counter;         // 64位无符号计数器
  int flags;              // 标志位 (EFD_CLOEXEC, EFD_NONBLOCK, EFD_SEMAPHORE)
  int ref;                // 引用计数
};

// eventfd 函数原型
struct eventfd* eventfd_alloc(unsigned int initval, int flags);
void eventfd_close(struct eventfd *efd);
int eventfd_read(struct eventfd *efd, uint64 addr, int nonblock);
int eventfd_write(struct eventfd *efd, uint64 addr, int nonblock);

#endif // XV6_EVENTFD_H
