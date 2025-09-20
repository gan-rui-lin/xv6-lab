#include "spinlock.h"
#include "defs.h"
#include "proc.h"
#include "riscv.h"

// 如果涉及到中断上下文的访问，spin lock需要和禁止本CPU上的中断联合使用。
// 否则时间中断发生在同一 CPU 时，可能会在中断处理程序中再次请求同一把锁，从而导致死锁。
void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
void
acquire(struct spinlock *lk)
{
  push_off(); // disable interrupts to avoid deadlock.
  // 不是再 acquire 一次，否则会死锁，先 panic
  if(holding(lk))
    panic("acquire");

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  // 自旋等待锁
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu();
}

// Release the lock.
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.
// 主要处理嵌套中断问题，封装的小工具

void
push_off(void)
{
  // 获取当前的中断状态
  int old = intr_get();

  // 关中断
  intr_off();

  // 记录最初的中断状态
  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  // 更深一层的 push_off
  mycpu()->noff += 1;
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  // 应该是在关中断情况执行 pop_off
  if(intr_get())
    panic("pop_off - interruptible");
  // 要先 push_off
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;

  // 如果最初是开中断模式，并且完成所有中断嵌套，就开中断
  if(c->noff == 0 && c->intena)
    intr_on();
}
