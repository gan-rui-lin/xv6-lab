#include "coro.h"
#include "user.h"

void
coro_init(struct coro *c)
{
  if (c == 0)
    return;
  c->state = 0;
  c->done = 0;
  c->sleep_until = 0;
}

void
coro_task_init(struct coro_task *t, coro_fn fn, void *arg)
{
  if (t == 0)
    return;
  coro_init(&t->co);
  t->fn = fn;
  t->arg = arg;
}

int
coro_done(const struct coro *c)
{
  if (c == 0)
    return 1;
  return c->done != 0;
}

int
coro_step(struct coro_task *t)
{
  if (t == 0 || t->fn == 0)
    return 1;
  if (t->co.done)
    return 1;
  t->fn(&t->co, t->arg);
  return t->co.done != 0;
}

void
coro_run(struct coro_task *tasks, int count)
{
  int pending;
  int i;

  if (tasks == 0 || count <= 0)
    return;

  for (;;) {
    pending = 0;
    for (i = 0; i < count; i++) {
      if (!coro_step(&tasks[i]))
        pending = 1;
    }
    if (!pending)
      break;
    sched_yield();
  }
}
