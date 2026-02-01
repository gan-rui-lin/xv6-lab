#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "mm/vma.h"
#include "proc/signal.h"
#include "fcntl.h"
#include "errno.h"
#include "../sync/sleeplock.h"
#include "../fs/fs.h"
#include "../fs/file.h"
#include "signal.h"

#ifndef WNOHANG
#define WNOHANG 1
#endif

// waitid 相关定义 (Linux 兼容)
#ifndef WEXITED
#define WEXITED     0x04
#endif
#ifndef WSTOPPED
#define WSTOPPED    0x02
#endif
#ifndef WCONTINUED
#define WCONTINUED  0x08
#endif
#ifndef WNOWAIT
#define WNOWAIT     0x01000000
#endif

// idtype_t 枚举
typedef enum {
  P_ALL  = 0,
  P_PID  = 1,
  P_PGID = 2
} idtype_t;

// id_t 类型
typedef int id_t;

// siginfo_t 结构体（waitid 使用的子集）
typedef struct {
  int si_signo;     // 信号号
  int si_errno;     // 错误号
  int si_code;      // 信号代码
  int __pad0;
  int si_pid;       // 发送信号的进程 ID
  int si_uid;       // 发送信号的用户 ID
  int si_status;    // 退出状态或信号
  char __pad[128 - 7 * sizeof(int)];  // 填充到 128 字节
} siginfo_t;

// si_code 值（子进程状态变化）
#define CLD_EXITED    1   // 子进程正常退出
#define CLD_KILLED    2   // 子进程被信号杀死
#define CLD_DUMPED    3   // 子进程异常终止（core dump）
#define CLD_TRAPPED   4   // 被跟踪的子进程陷入
#define CLD_STOPPED   5   // 子进程被停止
#define CLD_CONTINUED 6   // 子进程继续执行

// clone flags (Linux-compatible values)
#define CLONE_VM            0x00000100ULL
#define CLONE_FS            0x00000200ULL
#define CLONE_FILES         0x00000400ULL
#define CLONE_SIGHAND       0x00000800ULL
#define CLONE_PARENT        0x00008000ULL
#define CLONE_THREAD        0x00010000ULL
#define CLONE_SETTLS        0x00080000ULL
#define CLONE_PARENT_SETTID 0x00100000ULL
#define CLONE_CHILD_CLEARTID 0x00200000ULL
#define CLONE_CHILD_SETTID  0x01000000ULL

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return getpid();
}

uint64
sys_fork(void)
{
  return fork();
}

// Linux-compatible clone (subset): supports TLS + parent/child TID semantics.
// Args (as passed from userspace on RISC-V):
// a0: flags, a1: stack, a2: ptid, a3: tls, a4: ctid
uint64
sys_clone(void)
{
  uint64 flags, stack, ptid, tls, ctid;
  // fetch arguments
  argaddr(0, &flags);
  argaddr(1, &stack);
  argaddr(2, &ptid);
  argaddr(3, &tls);
  argaddr(4, &ctid);

  // We only support a small subset of clone flags.
  const uint64 allowed = 0x7fULL | CLONE_SETTLS | CLONE_PARENT_SETTID |
                         CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;
  if(flags & ~allowed)
    return -EINVAL;

  // Exit signal is in low 7 bits.
  int exit_signal = (int)(flags & 0x7fULL);
  if(exit_signal != 0 && exit_signal != SIGCHLD)
    return -EINVAL;

  // Reject thread/shared-VM style flags (not supported in this kernel).
  if(flags & (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_PARENT))
    return -EINVAL;

  // Sanitize optional arguments if caller didn't pass them (or flags don't use them)
  if(!(flags & CLONE_PARENT_SETTID))
    ptid = 0;
  if(!(flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)))
    ctid = 0;
  if(!(flags & CLONE_SETTLS))
    tls = 0;

  if((flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) && ctid == 0)
    return -EINVAL;
  if((flags & CLONE_PARENT_SETTID) && ptid == 0)
    return -EINVAL;

  // Use clone_fork which supports custom stack and clone args
  // If stack != 0, the child's stack pointer will be set to the provided stack
  // The userspace __clone wrapper has already saved the function pointer
  // and argument on this stack before making the syscall.
  int pid = clone_fork(stack, flags, tls, ctid, exit_signal);
  if(pid < 0)
    return pid;

  // Parent TID write-back
  if(flags & CLONE_PARENT_SETTID){
    if(copyout(myproc()->pagetable, ptid, (char *)&pid, sizeof(pid)) < 0)
      return -EFAULT;
  }

  return pid;
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

// 使进程内存增加 n 字节，返回新内存的起始地址
uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

// Minimal syscall wrappers to satisfy linker during early development.
// These are intentionally lightweight: they defer to kernel helpers where
// available, or return simple defaults.

static int
sleep_ticks(int n)
{
  log_debug("sys_sleep: sleeping for %d ticks\n", n);
  uint ticks0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// 复制用户态 sigset_t 的低 64bit
static int
sigset_copyin(uint64 addr, uint64 sigsetsize, uint64 *out)
{
  if(out == 0)
    return -EINVAL;
  *out = 0;
  if(addr == 0 || sigsetsize == 0)
    return 0;
  if(sigsetsize < sizeof(uint64))
    return -EINVAL;
  if(copyin(myproc()->pagetable, (char *)out, addr, sizeof(uint64)) < 0)
    return -EFAULT;
  return 0;
}

// 将内核 mask 写回用户态 sigset_t（低 64bit + 其余清零）
static int
sigset_copyout(uint64 addr, uint64 sigsetsize, uint64 val)
{
  if(addr == 0 || sigsetsize == 0)
    return 0;
  if(sigsetsize < sizeof(uint64))
    return -EINVAL;
  if(copyout(myproc()->pagetable, addr, (char *)&val, sizeof(uint64)) < 0)
    return -EFAULT;
  uint64 off = sizeof(uint64);
  uint64 zero = 0;
  while(off < sigsetsize){
    uint64 chunk = sigsetsize - off;
    if(chunk > sizeof(zero))
      chunk = sizeof(zero);
    if(copyout(myproc()->pagetable, addr + off, (char *)&zero, chunk) < 0)
      return -EFAULT;
    off += chunk;
  }
  return 0;
}

uint64
sys_sleep(void)
{
  int n;
  argint(0, &n);
  if (n <= 0)
    return 0;
  return sleep_ticks(n);
}

uint64
sys_kill(void)
{
  int pid;
  argint(0, &pid);

  return kill(pid);
}

uint64
sys_shutdown(void)
{
  // #define TEST_FINISHER_FAIL    0x3333
  // #define TEST_FINISHER_PASS    0x5555
  // #define TEST_FINISHER_RESET   0x7777

  // volatile uint32 *test_dev = (volatile uint32 *)TEST_DEVICE;
  // *test_dev = TEST_FINISHER_PASS;
  
  sbi_shutdown();
  
  return 0;  // not reached
}

// Linux getpid (172) is already implemented as sys_getpid.
// Provide getppid (173).
uint64
sys_getppid(void)
{
  struct proc *p = myproc();
  if (p->parent)
    return p->parent->pid;
  return 0;
}

// Basic credential queries: return 0 for all IDs (single-user system).
uint64 sys_getuid(void)   { return 0; }
uint64 sys_geteuid(void)  { return 0; }
uint64 sys_getgid(void)   { return 0; }
uint64 sys_getegid(void)  { return 0; }

// set_tid_address(addr): record clear_child_tid address if provided, return tid.
uint64
sys_set_tid_address(void)
{
  uint64 addr;
  argaddr(0, &addr);
  struct proc *p = myproc();

  // Record clear_child_tid address for CLONE_CHILD_CLEARTID semantics
  p->clear_child_tid = addr;

  // Mirror Linux by returning the thread id (here, pid).
  // We also attempt to write the tid to the provided address if non-null.
  if(addr != 0){
    if(copyout(p->pagetable, addr, (char *)&p->pid, sizeof(p->pid)) < 0)
      return -1;
  }
  return p->pid;
}

// gettid: return thread id (pid in this single-threaded model).
uint64
sys_gettid(void)
{
  return myproc()->pid;
}

uint64
sys_ppoll(void)
{
  uint64 fds_addr;
  uint64 nfds;
  uint64 tmo_addr;
  uint64 sigmask_addr;
  uint64 sigsetsize;

  if(argaddr(0, &fds_addr) < 0 || argaddr(1, &nfds) < 0 ||
     argaddr(2, &tmo_addr) < 0 || argaddr(3, &sigmask_addr) < 0 ||
     argaddr(4, &sigsetsize) < 0)
    return -EINVAL;

  // Minimal ppoll: mark requested events as ready for valid fds.
  if(nfds > 1024)
    return -EINVAL;

  int ready = 0;
  if(nfds > 0 && fds_addr != 0){
    struct {
      int fd;
      short events;
      short revents;
    } pfd;

    for(uint64 i = 0; i < nfds; i++){
      uint64 off = fds_addr + i * sizeof(pfd);
      if(copyin(myproc()->pagetable, (char *)&pfd, off, sizeof(pfd)) < 0)
        return -EFAULT;
      if(pfd.fd >= 0 && pfd.fd < NOFILE && myproc()->ofile[pfd.fd] != 0){
        pfd.revents = pfd.events;
        if(pfd.revents)
          ready++;
      } else {
        pfd.revents = 0;
      }
      if(copyout(myproc()->pagetable, off, (char *)&pfd, sizeof(pfd)) < 0)
        return -EFAULT;
    }
    return ready;
  }

  if(tmo_addr != 0){
    struct { long sec; long nsec; } tmo;
    if(copyin(myproc()->pagetable, (char *)&tmo, tmo_addr, sizeof(tmo)) < 0)
      return -EFAULT;
    if(tmo.sec < 0 || tmo.nsec < 0 || tmo.nsec >= 1000000000L)
      return -EINVAL;
    unsigned long long total_ns = (unsigned long long)tmo.sec * 1000000000ULL
                                + (unsigned long long)tmo.nsec;
    const long TICK_NS = 1000000L;
    int n_ticks = (int)((total_ns + TICK_NS - 1) / TICK_NS);
    if(n_ticks > 0)
      return sleep_ticks(n_ticks);
  }

  return 0;
}

// exit_group(status): treat the group as the single process; reuse exit.
uint64
sys_exit_group(void)
{
  int status;
  argint(0, &status);
  exit(status);
  return 0;
}

// rt_sigaction: 处理用户态信号处理器设置
uint64
sys_rt_sigaction(void)
{
  int signum;
  uint64 act_addr;
  uint64 oldact_addr;
  uint64 sigsetsize;
  struct proc *p = myproc();

  if(argint(0, &signum) < 0 || argaddr(1, &act_addr) < 0 ||
     argaddr(2, &oldact_addr) < 0 || argaddr(3, &sigsetsize) < 0)
    return -EINVAL;

  if(signum < 1 || signum > NSIG)
    return -EINVAL;

  if(sigsetsize != 0 && sigsetsize < sizeof(uint64))
    return -EINVAL;

  if(oldact_addr != 0){
    struct sigaction oldact = p->sigactions[signum - 1];
    if(copyout(p->pagetable, oldact_addr, (char *)&oldact.sa_handler,
               sizeof(oldact.sa_handler)) < 0)
      return -EFAULT;
    if(copyout(p->pagetable, oldact_addr + sizeof(uint64),
               (char *)&oldact.sa_flags, sizeof(oldact.sa_flags)) < 0)
      return -EFAULT;
    if(copyout(p->pagetable, oldact_addr + 2 * sizeof(uint64),
               (char *)&oldact.sa_restorer, sizeof(oldact.sa_restorer)) < 0)
      return -EFAULT;
    if(sigset_copyout(oldact_addr + 3 * sizeof(uint64), sigsetsize,
                      oldact.sa_mask.bits) < 0)
      return -EFAULT;
  }

  if(act_addr != 0){
    struct sigaction newact;
    if(copyin(p->pagetable, (char *)&newact.sa_handler, act_addr,
              sizeof(newact.sa_handler)) < 0)
      return -EFAULT;
    if(copyin(p->pagetable, (char *)&newact.sa_flags,
              act_addr + sizeof(uint64), sizeof(newact.sa_flags)) < 0)
      return -EFAULT;
    if(copyin(p->pagetable, (char *)&newact.sa_restorer,
              act_addr + 2 * sizeof(uint64), sizeof(newact.sa_restorer)) < 0)
      return -EFAULT;
    if(sigset_copyin(act_addr + 3 * sizeof(uint64), sigsetsize,
                     &newact.sa_mask.bits) < 0)
      return -EFAULT;

    // SIGKILL/SIGSTOP 不能被捕获或忽略
    if(signum == SIGKILL || signum == SIGSTOP){
      if(newact.sa_handler != SIG_DFL)
        return -EINVAL;
    }
    if(newact.sa_handler == SIG_ERR)
      return -EINVAL;

    p->sigactions[signum - 1] = newact;
  }

  return 0;
}

// rt_sigprocmask: 设置或读取当前屏蔽字
uint64
sys_rt_sigprocmask(void)
{
  int how;
  uint64 set_addr;
  uint64 oldset_addr;
  uint64 sigsetsize;
  struct proc *p = myproc();

  if(argint(0, &how) < 0 || argaddr(1, &set_addr) < 0 ||
     argaddr(2, &oldset_addr) < 0 || argaddr(3, &sigsetsize) < 0)
    return -EINVAL;

  if(sigsetsize != 0 && sigsetsize < sizeof(uint64))
    return -EINVAL;

  if(oldset_addr != 0){
    if(sigset_copyout(oldset_addr, sigsetsize, p->sigmask) < 0)
      return -EFAULT;
  }

  if(set_addr != 0){
    uint64 setmask;
    if(sigset_copyin(set_addr, sigsetsize, &setmask) < 0)
      return -EFAULT;

    switch(how){
    case SIG_BLOCK:
      p->sigmask |= setmask;
      break;
    case SIG_UNBLOCK:
      p->sigmask &= ~setmask;
      break;
    case SIG_SETMASK:
      p->sigmask = setmask;
      break;
    default:
      return -EINVAL;
    }
    // SIGKILL/SIGSTOP 不可屏蔽
    p->sigmask &= ~(1ULL << (SIGKILL - 1));
    p->sigmask &= ~(1ULL << (SIGSTOP - 1));
  }

  return 0;
}

// rt_sigtimedwait: 最小实现，按需睡眠后返回成功，避免用户态失败
uint64
sys_rt_sigtimedwait(void)
{
  uint64 set_addr;
  uint64 info_addr;
  uint64 timeout_addr;
  uint64 sigsetsize;
  uint64 setmask = 0;
  struct proc *p = myproc();

  if(argaddr(0, &set_addr) < 0 || argaddr(1, &info_addr) < 0 ||
     argaddr(2, &timeout_addr) < 0 || argaddr(3, &sigsetsize) < 0)
    return -EINVAL;

  if(sigset_copyin(set_addr, sigsetsize, &setmask) < 0)
    return -EFAULT;

  acquire(&p->lock);
  uint64 pending = p->sigpending & setmask;
  if(pending == 0){
    release(&p->lock);
    if(timeout_addr != 0){
      struct { long sec; long nsec; } tmo;
      if(copyin(p->pagetable, (char *)&tmo, timeout_addr, sizeof(tmo)) < 0)
        return -EFAULT;
      if(tmo.sec < 0 || tmo.nsec < 0 || tmo.nsec >= 1000000000L)
        return -EINVAL;
      unsigned long long total_ns = (unsigned long long)tmo.sec * 1000000000ULL
                                  + (unsigned long long)tmo.nsec;
      const long TICK_NS = 1000000L;
      int n_ticks = (int)((total_ns + TICK_NS - 1) / TICK_NS);
      if(n_ticks > 0)
        sleep_ticks(n_ticks);
    }
    return -EAGAIN;
  }

  int sig = 0;
  for(int s = 1; s <= NSIG; s++){
    if(pending & (1ULL << (s - 1))){
      sig = s;
      p->sigpending &= ~(1ULL << (s - 1));
      break;
    }
  }
  release(&p->lock);

  if(sig == 0)
    return -EAGAIN;

  if(info_addr != 0){
    struct { int signo; int errno_; int code; int pad[29]; } info;
    memset(&info, 0, sizeof(info));
    info.signo = sig;
    if(copyout(p->pagetable, info_addr, (char *)&info, sizeof(info)) < 0)
      return -EFAULT;
  }
  return sig;
}

uint64
sys_rt_sigreturn(void)
{
  int ret = signal_return(myproc());
  return ret;
}

// kill(pid, sig): 兼容 SYS_kill_signal，忽略信号号的细节
uint64
sys_kill_signal(void)
{
  int pid, sig;
  argint(0, &pid);
  argint(1, &sig);

  if(sig == 0){
    // sig==0 仅用于探测进程是否存在
    struct proc *p;
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->pid == pid){
        release(&p->lock);
        return 0;
      }
      release(&p->lock);
    }
    return -ESRCH;
  }

  return signal_send_pid(pid, sig);
}

uint64
sys_tkill(void)
{
  int tid, sig;
  argint(0, &tid);
  argint(1, &sig);
  return signal_send_pid(tid, sig);
}

uint64
sys_tgkill(void)
{
  int tgid, tid, sig;
  argint(0, &tgid);
  argint(1, &tid);
  argint(2, &sig);
  (void)tgid;
  return signal_send_pid(tid, sig);
}

// prlimit64: 简化为返回默认限制，忽略设置
uint64
sys_prlimit64(void)
{
  int pid, resource;
  uint64 new_limit;
  uint64 old_limit;
  struct proc *p = myproc();

  if(argint(0, &pid) < 0 || argint(1, &resource) < 0 ||
     argaddr(2, &new_limit) < 0 || argaddr(3, &old_limit) < 0)
    return -EINVAL;

  (void)pid;
  (void)resource;
  (void)new_limit;

  if(old_limit != 0){
    struct { uint64 rlim_cur; uint64 rlim_max; } lim;
    lim.rlim_cur = 1024;
    lim.rlim_max = 1024;
    if(copyout(p->pagetable, old_limit, (char *)&lim, sizeof(lim)) < 0)
      return -EFAULT;
  }

  return 0;
}

// Linux execve(path, argv, envp)
// We currently do not support envp; we silently ignore it if it's empty (first element is NULL).
uint64
sys_execve(void)
{
  char path[MAXPATH], *argv[MAXARG], *envv[MAXARG];
  int i;
  uint64 uargv, uarg, uenvp;
  int err = -EFAULT;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0 || argaddr(2, &uenvp) < 0)
    return -EFAULT;

  // Map /proc/self/exe to the current executable path (no procfs support).
  if(strncmp(path, "/proc/self/exe", 14) == 0 && path[14] == '\0')
    safestrcpy(path, "/musl/busybox", sizeof(path));
  
  // envp is ignored for now; accept non-empty arrays to avoid aborting.
  if(uenvp != 0) {
    // printf("here uenvp=%p\n", (void*)uenvp);
    uint64 first_env;
    if(fetchaddr(uenvp, &first_env) < 0)
      return -EFAULT;
  }

  memset(argv, 0, sizeof(argv));
  memset(envv, 0, sizeof(envv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      { err = -E2BIG; goto bad; }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0)
      { err = -EFAULT; goto bad; }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      { err = -ENOMEM; goto bad; }
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      { err = -EFAULT; goto bad; }
  }

  int envc = 0;
  if(uenvp != 0){
    for(envc = 0;; envc++){
      if(envc >= MAXARG)
        { err = -E2BIG; goto bad; }
      uint64 uenv;
      if(fetchaddr(uenvp + sizeof(uint64) * envc, &uenv) < 0)
        { err = -EFAULT; goto bad; }
      if(uenv == 0){
        envv[envc] = 0;
        break;
      }
      envv[envc] = kalloc();
      if(envv[envc] == 0)
        { err = -ENOMEM; goto bad; }
      if(fetchstr(uenv, envv[envc], PGSIZE) < 0)
        { err = -EFAULT; goto bad; }
    }
  } else {
    envv[0] = 0;
  }

  int ret = exec(path, argv, envv);
  if(ret == -ENOENT){
    // printf("sys_execve: ENOENT path='%s'\n", path);
    // If path has no '/', try PATH search from envp.
    int has_slash = 0;
    for(char *c = path; *c; c++){
      if(*c == '/'){
        has_slash = 1;
        break;
      }
    }
    if(!has_slash){
      char *pathenv = 0;
      for(i = 0; envv[i]; i++){
        if(strncmp(envv[i], "PATH=", 5) == 0){
          pathenv = envv[i] + 5;
          break;
        }
      }
      const char *fallback_path = "/bin:/musl";
      const char *p = (pathenv && *pathenv) ? pathenv : fallback_path;
      printf("sys_execve: PATH='%s'\n", p);
      if(*p){
        char candidate[MAXPATH];
        while(*p){
          const char *start = p;
          while(*p && *p != ':')
            p++;
          int len = p - start;
          if(len > 0 && len < MAXPATH - 2){
            int n = 0;
            for(int k = 0; k < len && n < MAXPATH - 2; k++)
              candidate[n++] = start[k];
            if(candidate[n-1] != '/')
              candidate[n++] = '/';
            for(char *c = path; *c && n < MAXPATH - 1; c++)
              candidate[n++] = *c;
            candidate[n] = '\0';
            int r = exec(candidate, argv, envv);
            if(r != -ENOENT)
              { ret = r; break; }
          }
          if(*p == ':')
            p++;
        }
      }
    }
  }
  // Busybox 兼容：如果 PATH 也找不到，尝试用 busybox 作为 applet 入口
  if(ret == -ENOENT){
    int has_slash = 0;
    for(char *c = path; *c; c++){
      if(*c == '/'){
        has_slash = 1;
        break;
      }
    }
    if(!has_slash){
      int r = exec("/musl/busybox", argv, envv);
      if(r == -ENOENT)
        r = exec("/busybox", argv, envv);
      if(r != -ENOENT)
        ret = r;
    }
  }
  // No kernel-level fallback here; rely on userspace to handle ENOEXEC.

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  for(i = 0; i < NELEM(envv) && envv[i] != 0; i++)
    kfree(envv[i]);
  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  for(i = 0; i < NELEM(envv) && envv[i] != 0; i++)
    kfree(envv[i]);
  return err;
}

// Minimal wait4(pid, status, options): support only pid == -1 and options == 0
// Now also supports waiting for a specific child process by PID
uint64
sys_wait4(void)
{
  int pid, options;
  uint64 status;
  argint(0, &pid);
  argaddr(1, &status);
  argint(2, &options);

  if(options & ~WNOHANG)
    return -EINVAL;
  int nohang = (options & WNOHANG) != 0;

  // If pid == -1 and no WNOHANG, wait for any child (original behavior)
  if (pid == -1 && !nohang)
    return wait(status);
  
  // If pid > 0, wait for specific child process
  // We need to implement waitpid functionality
  struct proc *pp;
  int havekids;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for the specific child
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      // Check if this is our child
      if(pp->parent == p){
        // Check if this matches the requested PID
        if(pid == -1 || pp->pid == pid){
          acquire(&pp->lock);
          havekids = 1;
          
          if(pp->state == ZOMBIE){
            // Found the matching child
            int cpid = pp->pid;

            // Debug: print the xstate before copying
            // log_debug("sys_wait4: found zombie child pid=%d, xstate=%d\n", cpid, pp->xstate);

            // Debug: check parent's PTE before freeing child
            pte_t *parent_pte = walk(p->pagetable, 0x7fa78, 0);
            log_debug("[wait4] Before freeproc: parent pid=%d pte=%p\n", p->pid,
                   parent_pte ? *parent_pte : 0);

            if(status != 0 && copyout(p->pagetable, status, (char *)&pp->xstate,
                                    sizeof(pp->xstate)) < 0) {
              release(&pp->lock);
              release(&wait_lock);
              return -EFAULT;
            }
            freeproc(pp);

            // Debug: check parent's PTE after freeing child
            parent_pte = walk(p->pagetable, 0x7fa78, 0);
            log_debug("[wait4] After freeproc: parent pid=%d pte=%p\n", p->pid,
                   parent_pte ? *parent_pte : 0);

            release(&pp->lock);
            release(&wait_lock);
            return cpid;
          }
          release(&pp->lock);
        }
      }
    }

    // No matching child found
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -ECHILD;
    }
    if(nohang){
      release(&wait_lock);
      return 0;
    }

    // Wait for the child to exit
    sleep(p, &wait_lock);
  }
}


// brk() 设置程序的堆顶地址
// 参数 addr: 新的堆顶地址
// 如果 addr == 0，返回当前堆顶地址
// 如果 addr != 0，尝试将堆顶设置为 addr，成功返回新地址，失败返回 -1
uint64
sys_brk(void)
{
  uint64 addr;
  struct proc *p = myproc();
  
  // 获取参数：目标堆顶地址
  if(argaddr(0, &addr) < 0)
    return -1;
  
  // 如果 addr == 0，返回当前堆顶地址（查询模式）
  if(addr == 0) {
    return p->sz;
  }
  
  // 检查地址是否合法（不能小于当前大小或太大）
  if(addr < p->sz) {
    // 缩小内存
    int n = addr - p->sz;  // n 是负数
    if(growproc(n) < 0)
      return -1;
    return addr;
  } else if(addr > p->sz) {
    // 扩大内存
    int n = addr - p->sz;  // n 是正数
    if(growproc(n) < 0)
      return -1;
    return addr;
  }
  
  // addr == p->sz，不需要改变
  return addr;
}

static int do_mprotect(struct proc *p, uint64 addr, uint64 length, int prot);

uint64
sys_mmap(void)
{
  uint64 addr;
  uint64 length;
  int prot;
  int flags;
  int fd;
  uint64 offset;
  
  // 获取参数
  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 || 
     argint(2, &prot) < 0 || argint(3, &flags) < 0 || 
     argint(4, &fd) < 0 || argaddr(5, &offset) < 0)
    return -1;

  struct proc *p = myproc();
  struct file *f;

  //claude: 跟踪 mmap 调用以诊断动态链接库加载
  if (flags & MAP_ANONYMOUS) {
    log_debug("[mmap] pid=%d ANONYMOUS addr=%p len=%p prot=%d flags=%x\n",
              p->pid, addr, length, prot, flags);
  } else {
    log_debug("[mmap] pid=%d FILE fd=%d offset=%p len=%p prot=%d flags=%x\n",
              p->pid, fd, offset, length, prot, flags);
  }

  // Linux 语义：length == 0 返回 -EINVAL
  if(length == 0)
    return -EINVAL;
  if(prot == PROT_NONE)
    return -EINVAL;

  // 对齐起始地址与长度
  uint64 map_size = PGROUNDUP(length);
  uint64 old_sz = p->sz;
  uint64 base = PGROUNDUP(old_sz);
  uint64 new_sz = base + map_size;

  // 计算权限标志：根据 prot 精确设置
  int perm = 0;
  if(prot & PROT_READ)
    perm |= PTE_R;
  if(prot & PROT_WRITE)
    perm |= PTE_W;
  if(prot & PROT_EXEC)
    perm |= PTE_X;

  // MAP_FIXED 先不支持
  if(flags & MAP_FIXED)
    return -EINVAL;

  // 匿名映射：忽略 fd/offset，直接扩展地址空间
  if(flags & MAP_ANONYMOUS){
    if((new_sz = uvmalloc_lazy(p->pagetable, old_sz, new_sz, perm)) == 0)
      return -1;
    p->sz = new_sz;
    if(vma_add(p, base, base + map_size, prot, flags, 0, 0, 0) < 0){
      uvmdealloc(p->pagetable, new_sz, old_sz);
      p->sz = old_sz;
      return -ENOMEM;
    }
    if(do_mprotect(p, base, length, prot) < 0){
      vma_unmap(p, base, base + map_size);
      uvmdealloc(p->pagetable, new_sz, old_sz);
      p->sz = old_sz;
      return -EINVAL;
    }
    return base;
  }

  // 文件映射：需要有效、可读的文件描述符
  if(fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0)
    return -1;
  if(!f->readable)
    return -1;

  if(offset % PGSIZE)
    return -EINVAL;
  if(f->type != FD_INODE)
    return -EINVAL;

  uint64 file_len = 0;
  ilock(f->ip);
  if(offset < f->ip->size){
    file_len = f->ip->size - offset;
    if(file_len > length)
      file_len = length;
  }
  iunlock(f->ip);

  if(vma_add(p, base, base + map_size, prot, flags, f, offset, file_len) < 0)
    return -ENOMEM;
  p->sz = new_sz;
  return base;
}

static int
do_mprotect(struct proc *p, uint64 addr, uint64 length, int prot)
{
  uint64 start = PGROUNDDOWN(addr);
  uint64 end = PGROUNDUP(addr + length);

  if(length == 0)
    return 0;
  if(prot == PROT_NONE)
    return -EINVAL;
  if(end > p->sz || start >= end)
    return -EINVAL;

  if(vma_protect(p, start, end, prot) < 0)
    return -ENOMEM;

  int want_read = (prot & PROT_READ) != 0;
  int want_write = (prot & PROT_WRITE) != 0;
  int want_exec = (prot & PROT_EXEC) != 0;

  // 逐页更新权限，保留 V/A/D 等标志
  for(uint64 va = start; va < end; va += PGSIZE){
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte == 0 || (*pte & PTE_V) == 0)
      continue;
    if(PTE_FLAGS(*pte) == PTE_V)
      continue;

    uint64 pa = PTE2PA(*pte);
    uint64 flags = PTE_FLAGS(*pte);

    flags &= ~(PTE_R | PTE_W | PTE_X);

    if(want_exec)
      flags |= PTE_X;
    if(want_read)
      flags |= PTE_R;

    if(want_write){
      if(flags & PTE_COW){
        flags &= ~PTE_W;
        flags |= PTE_R;
      } else {
        flags |= PTE_W;
      }
    } else {
      flags &= ~PTE_COW;
    }

    *pte = PA2PTE(pa) | flags;
  }
  sfence_vma();
  return 0;
}

// mprotect: 修改用户态内存页的权限（用于 RELRO 等场景）
uint64
sys_mprotect(void)
{
  uint64 addr;
  uint64 length;
  int prot;

  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 || argint(2, &prot) < 0)
    return -EINVAL;

  struct proc *p = myproc();
  return do_mprotect(p, addr, length, prot);
}

uint64
sys_munmap(void)
{
  uint64 addr;
  uint64 length;
  
  // 获取参数
  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0)
    return -1;
  
  // 长度必须大于0
  if(length == 0)
    return 0;  // 长度为0时，不做任何操作，返回成功
  
  struct proc *p = myproc();
  
  // 页面对齐
  uint64 start = PGROUNDDOWN(addr);
  uint64 end = PGROUNDUP(addr + length);
  uint64 npages = (end - start) / PGSIZE;
  
  // 简化实现：检查地址是否在进程地址空间内
  if(start >= p->sz) {
    return -1;  // 地址超出进程空间
  }
  
  // 调整end，不能超过进程大小
  if(end > p->sz) {
    end = p->sz;
    npages = (end - start) / PGSIZE;
  }
  
  // 如果没有页面需要解除映射，直接返回成功
  if(npages == 0) {
    return 0;
  }
  
  // 使用uvmunmap解除映射
  uvmunmap_lazy(p->pagetable, start, npages, 1);

  if(vma_unmap(p, start, end) < 0)
    return -1;
  
  // 关键修复：如果解除映射的区域在进程末尾，需要调整进程大小
  // 这样可以防止进程退出时再次尝试释放这些页面
  if(end == p->sz) {
    p->sz = start;
  }
  
  return 0;
}

uint64
sys_msync(void)
{
  // 简化实现：忽略刷新请求，直接返回成功
  return 0;
}

uint64
sys_setpgid(void)
{
  int pid, pgid;
  argint(0, &pid);
  argint(1, &pgid);
  
  struct proc *p = myproc();
  struct proc *target;
  
  // pid == 0 表示设置当前进程
  if(pid == 0)
    pid = p->pid;
  
  // pgid == 0 表示使用进程自己的 PID 作为 PGID
  if(pgid == 0)
    pgid = pid;
  
  // 查找目标进程
  target = 0;
  for(struct proc *pp = proc; pp < &proc[NPROC]; pp++){
    acquire(&pp->lock);
    if(pp->pid == pid){
      target = pp;
      break;
    }
    release(&pp->lock);
  }
  
  if(target == 0)
    return -ESRCH;
  
  // 简化实现：只允许设置自己或子进程的进程组
  // 完整实现需要更多检查
  target->pgid = pgid;
  release(&target->lock);
  
  return 0;
}

uint64
sys_setitimer(void)
{
  int which;
  uint64 newv;
  uint64 oldv;
  if(argint(0, &which) < 0 || argaddr(1, &newv) < 0 || argaddr(2, &oldv) < 0)
    return -EINVAL;
  (void)which;
  (void)newv;
  if(oldv != 0){
    // 清零旧计时器信息，避免用户态读取失败
    uint64 zero = 0;
    for(uint64 off = 0; off < 4 * sizeof(uint64); off += sizeof(uint64)){
      if(copyout(myproc()->pagetable, oldv + off, (char *)&zero, sizeof(uint64)) < 0)
        return -EFAULT;
    }
  }
  return 0;
}

uint64
sys_sched_yield(void)
{
    // 原始 C++ 风格的实现（已注释）：
    // // printfCyan("[sche]  yield here \n");
    // Cpu::get_cpu()->push_intr_off();
    // Pcb *p = Cpu::get_cpu()->get_cur_proc();
    // Cpu::get_cpu()->pop_intr_off();
    // // printfCyan("[sche]  yield here,p->addr:%x \n",Cpu::get_cpu()->get_cur_proc());
    // p->_lock.acquire();
    // // printfCyan("[sche]  yield here \n");
    // p->_state = ProcState::RUNNABLE;
    // call_sched(); // 注意swtch的逻辑是函数调用, 所以重新调用就是视为从这个函数返回
    // p->_lock.release();
    
    // xv6 C 风格的实现：
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();  // 切换到调度器，让出 CPU
    release(&p->lock);
    return 0;
}

// Minimal sched_getaffinity(pid, cpusetsize, mask): report available CPUs.
// We ignore pid and always return affinity for the current system (0..NCPU-1).
uint64
sys_sched_getaffinity(void)
{
  int pid;
  uint64 cpusetsize;
  uint64 mask_addr;
  struct proc *p = myproc();

  if(argint(0, &pid) < 0 || argaddr(1, &cpusetsize) < 0 || argaddr(2, &mask_addr) < 0)
    return -EINVAL;

  // Require at least one long worth of space.
  if(cpusetsize == 0)
    return -EINVAL;

  // Build a zeroed buffer and set bits for available CPUs.
  char *kbuf = kalloc();
  if(!kbuf)
    return -ENOMEM;
  int sz = (int)cpusetsize;
  if(sz > PGSIZE) sz = PGSIZE; // cap to one page to avoid excessive copy
  memset(kbuf, 0, sz);

  // Set bits 0..NCPU-1
  for(int cpu = 0; cpu < NCPU; cpu++){
    int byte = cpu / 8;
    int bit = cpu % 8;
    if(byte < sz)
      kbuf[byte] |= (1 << bit);
  }

  int r = copyout(p->pagetable, mask_addr, kbuf, sz);
  kfree(kbuf);
  if(r < 0)
    return -EFAULT;
  return 0;
}

// sys_waitpid () {
  // int pid;
  // uint64 wstatus_addr;
  // int option;
  // if (_arg_int(0, pid) < 0)
  //     return -1;
  // if (_arg_addr(1, wstatus_addr) < 0)
  //     return -1;
  // if (_arg_int(2, option) < 0)
  //     return -1;

  // // 检查无效的PID值
  // // 根据POSIX标准，PID不能是INT_MIN或其他无效值
  // if (pid == INT_MIN)
  // {
  //     return SYS_ESRCH;
  // }

  // // printf("[SyscallHandler::sys_wait4] pid: %d, wstatus_addr: %p, option: %d\n",
  // //    pid, wstatus_addr, option);
  // int waitret = proc::k_pm.wait4(pid, wstatus_addr, option);
  // // printf("[SyscallHandler::sys_wait4] waitret: %d\n",waitret);
  // return waitret;pid != -1 unsupported
// }


uint64
sys_uname(void)
{
  uint64 addr;
  if (argaddr(0, &addr) < 0)
    return -1;

  struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
  } un;

  safestrcpy(un.sysname, "Linux", 65);  // glibc 需要 "Linux"
  safestrcpy(un.nodename, "ru-node", 65);
  safestrcpy(un.release, "5.10.0", 65);  // 模拟 Linux 5.10 内核
  safestrcpy(un.version, "#1 SMP", 65);
  safestrcpy(un.machine, "riscv64", 65);
  safestrcpy(un.domainname, "(none)", 65);

  if (copyout(myproc()->pagetable, addr, (char*)&un, sizeof(un)) < 0)
    return -1;

  return 0;
}

uint64
sys_nanosleep(void)
{
  // args: a0 = req (user ptr), a1 = rem (user ptr)
  uint64 req_addr, rem_addr;
  argaddr(0, &req_addr);
  argaddr(1, &rem_addr);

  struct {
    long sec;
    long usec;
  } req;

  if (req_addr == 0)
    return -1;
  if (copyin(myproc()->pagetable, (char*)&req, req_addr, sizeof(req)) != 0)
    return -1;

  if (req.sec < 0 || req.usec < 0 || req.usec >= 1000000L)
    return -1;

  // 精确转换为 tick 数：假定时钟中断为 1ms/tick（HZ=1000）。
  // 将秒/微秒转换为总纳秒并按 1ms 进行向上取整。
  const long TICK_NS = 1000000L;   // 1 ms
  unsigned long long total_ns = (unsigned long long)req.sec * 1000000000ULL
                              + (unsigned long long)req.usec * 1000ULL;
  int n_ticks = (int)((total_ns + TICK_NS - 1) / TICK_NS); // ceil(total_ns / 1ms)

  if (n_ticks <= 0)
    return 0;

  int ret = sleep_ticks(n_ticks);

  // 完成后，若需要，置剩余时间为0
  if (rem_addr) {
    struct { long sec; long usec; } rem = {0, 0};
    if (copyout(myproc()->pagetable, rem_addr, (char*)&rem, sizeof(rem)) < 0)
      return -1;
  }
  return ret;
}

// 设置当前进程的优先级
// 参数：priority (1-40, 数值越小优先级越高)
// 返回：成功返回0，失败返回-1
uint64
sys_setpriority(void)
{
  int priority;
  argint(0, &priority);
  
  // 检查优先级范围
  if (priority < PRIO_MIN || priority > PRIO_MAX) {
    return -1;
  }
  
  struct proc *p = myproc();
  acquire(&p->lock);
  p->priority = priority;
  release(&p->lock);
  
  return 0;
}

// 获取当前进程的优先级
// 返回：当前进程的优先级值
uint64
sys_getpriority(void)
{
  struct proc *p = myproc();
  return p->priority;
}

// set_robust_list: 设置 robust futex 列表（glibc 线程支持）
// 参数：
//   a0: head - 指向用户空间 robust_list_head 结构的指针
//   a1: len - 结构体大小
// 返回：0 表示成功
uint64
sys_set_robust_list(void)
{
  uint64 head;
  uint64 len;
  struct proc *p = myproc();

  if(argaddr(0, &head) < 0)
    return -EFAULT;
  if(argaddr(1, &len) < 0)
    return -EFAULT;

  // 简单存储这些值，xv6 不需要实际处理 robust futex
  // 这只是为了让 glibc 程序能够正常运行
  p->robust_list_head = head;
  p->robust_list_len = len;

  return 0;
}

// 判断进程是否匹配 waitid 的等待条件
static int
waitid_match(struct proc *child, idtype_t idtype, id_t id)
{
  switch(idtype) {
    case P_ALL:
      // 等待任意子进程
      return 1;
    case P_PID:
      // 等待特定 PID 的子进程
      return child->pid == id;
    case P_PGID:
      // 等待特定进程组的子进程
      return child->pgid == id;
    default:
      return 0;
  }
}

// 根据 xstate 解析退出信息并填充 siginfo_t
static void
waitid_fill_siginfo(siginfo_t *info, struct proc *child)
{
  memset(info, 0, sizeof(siginfo_t));
  info->si_signo = SIGCHLD;
  info->si_pid = child->pid;
  info->si_uid = child->uid;
  
  int xstate = child->xstate;
  int termsig = xstate & 0x7f;  // 低 7 位是信号号
  
  if(termsig == 0) {
    // 正常退出：信号号为 0，退出码在 bits 8-15
    info->si_code = CLD_EXITED;
    info->si_status = (xstate >> 8) & 0xff;
  } else {
    // 被信号杀死
    info->si_code = CLD_KILLED;
    info->si_status = termsig;
    // 如果有 core dump (bit 7 of xstate)
    if(xstate & 0x80) {
      info->si_code = CLD_DUMPED;
    }
  }
}

// waitid 系统调用实现
// 参数通过系统调用获取：a0=idtype, a1=id, a2=infop, a3=options
uint64
sys_waitid(void)
{
  int idtype_val, options;
  id_t id;
  uint64 infop_addr;
  
  // 获取参数
  argint(0, &idtype_val);
  argint(1, &id);
  argaddr(2, &infop_addr);
  argint(3, &options);
  
  idtype_t idtype = (idtype_t)idtype_val;
  
  // 验证 idtype
  if(idtype != P_ALL && idtype != P_PID && idtype != P_PGID)
    return -EINVAL;
  
  // 必须指定至少一个等待条件
  if(!(options & (WEXITED | WSTOPPED | WCONTINUED)))
    return -EINVAL;
  
  // 目前只支持 WEXITED（等待退出的子进程）
  // WSTOPPED 和 WCONTINUED 需要进程停止/继续状态支持
  
  struct proc *p = myproc();
  struct proc *pp;
  int havekids;
  int nohang = (options & WNOHANG) != 0;
  int nowait = (options & WNOWAIT) != 0;
  
  acquire(&wait_lock);
  
  for(;;) {
    // 扫描进程表寻找匹配的子进程
    havekids = 0;
    
    for(pp = proc; pp < &proc[NPROC]; pp++) {
      // 检查是否是当前进程的子进程
      if(pp->parent != p)
        continue;
      
      // 检查是否匹配等待条件
      if(!waitid_match(pp, idtype, id))
        continue;
      
      havekids = 1;
      
      acquire(&pp->lock);
      
      if(pp->state == ZOMBIE && (options & WEXITED)) {
        // 找到已退出的子进程
        if(infop_addr != 0) {
          siginfo_t info;
          waitid_fill_siginfo(&info, pp);
          
          // 复制到用户空间
          if(copyout(p->pagetable, infop_addr, (char *)&info, sizeof(info)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -EFAULT;
          }
        }
        
        // 如果没有设置 WNOWAIT，则回收子进程
        if(!nowait) {
          freeproc(pp);
        }
        
        release(&pp->lock);
        release(&wait_lock);
        return 0;
      }
      
      release(&pp->lock);
    }
    
    // 没有匹配的子进程
    if(!havekids) {
      release(&wait_lock);
      return -ECHILD;
    }
    
    // 非阻塞模式：没有状态变化立即返回
    if(nohang) {
      // WNOHANG 且无状态变化时，将 si_pid 设为 0
      if(infop_addr != 0) {
        siginfo_t info;
        memset(&info, 0, sizeof(info));
        if(copyout(p->pagetable, infop_addr, (char *)&info, sizeof(info)) < 0) {
          release(&wait_lock);
          return -EFAULT;
        }
      }
      release(&wait_lock);
      return 0;
    }
    
    // 检查当前进程是否被杀死
    if(killed(p)) {
      release(&wait_lock);
      return -EINTR;
    }
    
    // 阻塞等待子进程状态变化
    sleep(p, &wait_lock);
  }
}

// 保留旧的内核内部接口（如果有其他地方调用）
uint64 waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
  // 验证 idtype
  if(idtype != P_ALL && idtype != P_PID && idtype != P_PGID)
    return -EINVAL;
  
  // 必须指定至少一个等待条件
  if(!(options & (WEXITED | WSTOPPED | WCONTINUED)))
    return -EINVAL;
  
  struct proc *p = myproc();
  struct proc *pp;
  int havekids;
  int nohang = (options & WNOHANG) != 0;
  int nowait = (options & WNOWAIT) != 0;
  
  acquire(&wait_lock);
  
  for(;;) {
    havekids = 0;
    
    for(pp = proc; pp < &proc[NPROC]; pp++) {
      if(pp->parent != p)
        continue;
      
      if(!waitid_match(pp, idtype, id))
        continue;
      
      havekids = 1;
      
      acquire(&pp->lock);
      
      if(pp->state == ZOMBIE && (options & WEXITED)) {
        if(infop != 0) {
          waitid_fill_siginfo(infop, pp);
        }
        
        if(!nowait) {
          freeproc(pp);
        }
        
        release(&pp->lock);
        release(&wait_lock);
        return 0;
      }
      
      release(&pp->lock);
    }
    
    if(!havekids) {
      release(&wait_lock);
      return -ECHILD;
    }
    
    if(nohang) {
      if(infop != 0) {
        memset(infop, 0, sizeof(siginfo_t));
      }
      release(&wait_lock);
      return 0;
    }
    
    if(killed(p)) {
      release(&wait_lock);
      return -EINTR;
    }
    
    sleep(p, &wait_lock);
  }
}
