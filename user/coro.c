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

#include "user.h"
#include "coro.h"

struct tick_ctx {
  const char *name;
  int step;
  int max;
  int delay;
};

static void
ticker(struct coro *co, void *arg)
{
  struct tick_ctx *ctx = (struct tick_ctx *)arg;

  CORO_BEGIN(co);
  while (ctx->step < ctx->max) {
    printf("%s step %d at tick %d\n", ctx->name, ctx->step, uptime());
    ctx->step++;
    CORO_SLEEP_TICKS(co, ctx->delay);
  }
  CORO_END(co);
}

int
test_coro()
{
  struct tick_ctx fast = { "fast", 0, 6, 2 };
  struct tick_ctx slow = { "slow", 0, 4, 5 };
  struct coro_task tasks[2];

  coro_task_init(&tasks[0], ticker, &fast);
  coro_task_init(&tasks[1], ticker, &slow);

  printf("coro_test: start\n");
  coro_run(tasks, 2);
  printf("coro_test: done\n");
  
  return 0;
}