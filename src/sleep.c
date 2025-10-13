#include "types.h"
#include "spinlock.h"
#include "defs.h"

// Minimal placeholder implementations of sleep/wakeup so the kernel links.
// These do not provide real blocking behavior. Replace with full
// implementations (in proc.c) when adding scheduler and sleep logic.

void
sleep(void *chan, struct spinlock *lk)
{
  // release the provided lock and immediately reacquire it.
  // This is a no-op placeholder to allow compilation during driver work.
  release(lk);
  acquire(lk);
}

void
wakeup(void *chan)
{
  // no-op placeholder
  (void)chan;
}
