#ifndef XV6_CORO_H
#define XV6_CORO_H

#include "types.h"

struct coro;
typedef void (*coro_fn)(struct coro *c, void *arg);

struct coro {
  int state;
  int done;
  uint64 sleep_until;
};

struct coro_task {
  struct coro co;
  coro_fn fn;
  void *arg;
};

int uptime(void);

void coro_init(struct coro *c);
void coro_task_init(struct coro_task *t, coro_fn fn, void *arg);
int coro_done(const struct coro *c);
int coro_step(struct coro_task *t);
void coro_run(struct coro_task *tasks, int count);

#define CORO_BEGIN(c) switch ((c)->state) { case 0:

#define CORO_YIELD(c) do { \
  (c)->state = __LINE__; \
  return; \
  case __LINE__:; \
} while (0)

#define CORO_WAIT_UNTIL(c, cond) do { \
  while (!(cond)) { \
    CORO_YIELD(c); \
  } \
} while (0)

#define CORO_SLEEP_TICKS(c, ticks) do { \
  if ((c)->sleep_until == 0) { \
    (c)->sleep_until = (uint64)uptime() + (ticks); \
  } \
  CORO_WAIT_UNTIL(c, (uint64)uptime() >= (c)->sleep_until); \
  (c)->sleep_until = 0; \
} while (0)

#define CORO_END(c) } (c)->done = 1; return

#endif
