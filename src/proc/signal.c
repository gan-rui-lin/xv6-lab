#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "proc/signal.h"
#include "errno.h"

#define SIGFRAME_MAGIC 0x53494746524d4141ULL // "SIGFRMAA"

struct sigframe {
  uint64 magic;
  uint64 old_mask;
  struct trapframe tf;
};

static int
signal_is_valid(int sig)
{
  return sig >= 1 && sig <= NSIG;
}

static uint64
signal_bit(int sig)
{
  return 1ULL << (sig - 1);
}

static int
signal_default_ignore(int sig)
{
  return sig == SIGCHLD || sig == SIGURG || sig == SIGWINCH;
}

static int
signal_default_coredump(int sig)
{
  return sig == SIGABRT || sig == SIGSEGV || sig == SIGILL ||
         sig == SIGTRAP || sig == SIGBUS || sig == SIGFPE;
}

void
signal_init(struct proc *p)
{
  p->sigpending = 0;
  p->sigmask = 0;
  for(int i = 0; i < NSIG; i++){
    p->sigactions[i].sa_handler = SIG_DFL;
    p->sigactions[i].sa_flags = 0;
    p->sigactions[i].sa_restorer = 0;
    p->sigactions[i].sa_mask.bits = 0;
  }
}

void
signal_copy(struct proc *dst, struct proc *src)
{
  dst->sigpending = 0;
  dst->sigmask = src->sigmask;
  for(int i = 0; i < NSIG; i++){
    dst->sigactions[i] = src->sigactions[i];
  }
}

int
signal_send(struct proc *p, int sig)
{
  if(!signal_is_valid(sig))
    return -EINVAL;

  acquire(&p->lock);
  p->sigpending |= signal_bit(sig);
  if(p->state == SLEEPING)
    p->state = RUNNABLE;
  release(&p->lock);
  return 0;
}

int
signal_send_pid(int pid, int sig)
{
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      if(!signal_is_valid(sig)){
        release(&p->lock);
        return -EINVAL;
      }
      p->sigpending |= signal_bit(sig);
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -ESRCH;
}

static int
signal_pick_pending(struct proc *p, int *out_sig)
{
  uint64 pending = p->sigpending;
  if(pending == 0)
    return 0;

  uint64 mask = p->sigmask;
  uint64 unmasked = pending & ~mask;
  if(pending & signal_bit(SIGKILL))
    unmasked |= signal_bit(SIGKILL);
  if(pending & signal_bit(SIGSTOP))
    unmasked |= signal_bit(SIGSTOP);

  if(unmasked == 0)
    return 0;

  for(int sig = 1; sig <= NSIG; sig++){
    if(unmasked & signal_bit(sig)){
      *out_sig = sig;
      return 1;
    }
  }
  return 0;
}

static void
signal_do_terminate(struct proc *p, int sig)
{
  int status = sig & 0x7f;
  if(signal_default_coredump(sig))
    status |= 0x80;
  p->xstate = status;
  p->killed = 1;
}

static int
signal_setup_frame(struct proc *p, int sig, struct sigaction *act)
{
  struct sigframe frame;
  uint64 sp = p->trapframe->sp;
  uint64 frame_size = sizeof(frame);

  // 16 字节对齐，避免 ABI 问题
  sp = (sp - frame_size) & ~0xFULL;

  frame.magic = SIGFRAME_MAGIC;
  frame.old_mask = p->sigmask;
  frame.tf = *p->trapframe;

  if(copyout(p->pagetable, sp, (char *)&frame, sizeof(frame)) < 0)
    return -EFAULT;

  // 进入用户态 handler
  p->trapframe->sp = sp;
  p->trapframe->epc = act->sa_handler;
  p->trapframe->a0 = sig;
  p->trapframe->ra = act->sa_restorer;

  // 更新屏蔽字：handler 期间自动阻塞当前信号 + sa_mask
  uint64 newmask = p->sigmask | act->sa_mask.bits;
  if(!(act->sa_flags & SA_NODEFER))
    newmask |= signal_bit(sig);
  // SIGKILL/SIGSTOP 不可屏蔽
  newmask &= ~(signal_bit(SIGKILL) | signal_bit(SIGSTOP));
  p->sigmask = newmask;

  if(act->sa_flags & SA_RESETHAND){
    act->sa_handler = SIG_DFL;
    act->sa_flags = 0;
    act->sa_restorer = 0;
    act->sa_mask.bits = 0;
  }

  return 0;
}

void
signal_handle(struct proc *p)
{
  int sig;

  acquire(&p->lock);
  if(!signal_pick_pending(p, &sig)){
    release(&p->lock);
    return;
  }

  // 清除 pending
  p->sigpending &= ~signal_bit(sig);

  struct sigaction *act = &p->sigactions[sig - 1];
  uint64 handler = act->sa_handler;

  if(handler == SIG_IGN){
    release(&p->lock);
    return;
  }

  if(handler == SIG_DFL){
    if(signal_default_ignore(sig)){
      release(&p->lock);
      return;
    }
    signal_do_terminate(p, sig);
    release(&p->lock);
    return;
  }

  release(&p->lock);

  // 安装用户态 handler 栈帧
  if(signal_setup_frame(p, sig, act) < 0){
    acquire(&p->lock);
    signal_do_terminate(p, SIGSEGV);
    release(&p->lock);
    return;
  }
}

int
signal_return(struct proc *p)
{
  struct sigframe frame;
  uint64 sp = p->trapframe->sp;

  if(copyin(p->pagetable, (char *)&frame, sp, sizeof(frame)) < 0)
    return -EFAULT;
  if(frame.magic != SIGFRAME_MAGIC)
    return -EINVAL;

  // 恢复寄存器与屏蔽字
  *p->trapframe = frame.tf;
  p->sigmask = frame.old_mask;
  p->sigmask &= ~(signal_bit(SIGKILL) | signal_bit(SIGSTOP));
  return 0;
}
