/*
 * @Author: zjy whuzjy@qq.com
 * @Date: 2025-11-17 11:43:30
 * @Description: 
 * 
 */
// Sleeping locks

#include "../types.h"
#include "../riscv.h"
#include "../defs.h"
#include "../param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

static struct proc *
current_proc_safe(void)
{
  struct proc *p = myproc();
  if(p == 0)
    return 0;
  if(p < proc || p >= &proc[NPROC])
    return 0;
  return p;
}

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  while (lk->locked) {
    sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  struct proc *p = current_proc_safe();
  lk->pid = p ? p->pid : -1;
  release(&lk->lk);
}

void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  wakeup(lk);
  release(&lk->lk);
}

int
holdingsleep(struct sleeplock *lk)
{
  int r;
  
  acquire(&lk->lk);
  struct proc *p = current_proc_safe();
  int pid = p ? p->pid : -1;
  r = lk->locked && (lk->pid == pid);
  release(&lk->lk);
  return r;
}

