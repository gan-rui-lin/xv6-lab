#include "types.h"
#include "defs.h"
#include "param.h"
#include "proc.h"
#include "spinlock.h"
#include "memlayout.h"

static void
kthread_trampoline(void)
{
  struct proc *p = myproc();
  void (*fn)(void *) = p->kthread_fn;
  void *arg = p->kthread_arg;

  // release lock held by scheduler
  release(&p->lock);

  if (fn)
    fn(arg);

  // if thread function returns, park it as zombie
  acquire(&p->lock);
  p->state = ZOMBIE;
  sched();
  panic("kthread_trampoline");
}

int
kthread_create(void (*fn)(void *), void *arg, const char *name, int priority)
{
  struct proc *p = allocproc();
  if (p == 0)
    return -1;

  p->is_kthread = 1;
  p->kthread_fn = fn;
  p->kthread_arg = arg;
  if (name)
    safestrcpy(p->name, (char *)name, sizeof(p->name));
  p->priority = priority;

  // parent is optional for kernel threads
  acquire(&wait_lock);
  p->parent = 0;
  release(&wait_lock);

  // start from trampoline on kernel stack
  p->context.ra = (uint64)kthread_trampoline;
  p->context.sp = p->kernel_stack + KSTACK_SIZE;

  p->state = RUNNABLE;
  release(&p->lock);

  return p->pid;
}

void
kthread_exit(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = ZOMBIE;
  sched();
  panic("kthread_exit");
}
