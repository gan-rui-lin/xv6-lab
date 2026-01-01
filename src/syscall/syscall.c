#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

// 获取第n个系统调用参数的原始值
static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  *ip = argraw(n);
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_kill(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_uptime(void);
extern uint64 sys_shutdown(void);
extern uint64 sys_gettimeofday(void);
extern uint64 sys_read(void);
extern uint64 sys_write(void);
extern uint64 sys_open(void);
extern uint64 sys_openat(void);
extern uint64 sys_mknod(void);
extern uint64 sys_close(void);
extern uint64 sys_dup(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_clone(void);
extern uint64 sys_execve(void);
extern uint64 sys_wait4(void);
extern uint64 sys_getppid(void);
extern uint64 sys_brk(void);
extern uint64 sys_mmap(void);
extern uint64 sys_munmap(void);
extern uint64 sys_openat(void);
extern uint64 sys_sched_yield(void);
extern uint64 sys_dup3(void);
extern uint64 sys_getdents64(void);
extern uint64 sys_mount(void);
extern uint64 sys_getcwd(void);
extern uint64 sys_chdir(void);
extern uint64 sys_pipe2(void);
extern uint64 sys_dup2(void);
extern uint64 sys_mkdirat(void);
extern uint64 sys_uname(void);
extern uint64 sys_nanosleep(void);
extern uint64 sys_times(void);
// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
[SYS_times]       sys_times,
[SYS_nanosleep]   sys_nanosleep,
[SYS_dup]         sys_dup,
[SYS_dup2]        sys_dup3,
[SYS_dup3]        sys_dup3,
[SYS_getdents64]  sys_getdents64,
[SYS_mount]       sys_mount,
[SYS_getcwd]      sys_getcwd,
[SYS_clone]      sys_clone,
[SYS_fork]        sys_fork,
[SYS_read]        sys_read,
[SYS_write]       sys_write,
[SYS_close]       sys_close,
[SYS_exit]        sys_exit,
[SYS_wait4]       sys_wait4,
[SYS_execve]      sys_execve,
[SYS_getpid]      sys_getpid,
[SYS_getppid]     sys_getppid,
[SYS_gettimeofday] sys_gettimeofday,
[SYS_openat]      sys_openat,
[SYS_xv6_fork]    sys_fork,
[SYS_xv6_exit]    sys_exit,
[SYS_xv6_wait]    sys_wait,
[SYS_xv6_read]    sys_read,
[SYS_xv6_write]   sys_write,
[SYS_xv6_kill]    sys_kill,
[SYS_xv6_getpid]  sys_getpid,
[SYS_xv6_sbrk]    sys_sbrk,
[SYS_xv6_sleep]   sys_sleep,
[SYS_xv6_uptime]  sys_uptime,
[SYS_xv6_shutdown] sys_shutdown,
[SYS_xv6_gettimeofday] sys_gettimeofday,
[SYS_xv6_open]    sys_open,
[SYS_xv6_mknod]   sys_mknod,
[SYS_xv6_close]   sys_close,
[SYS_xv6_dup]     sys_dup,
[SYS_xv6_exec]    sys_exec,
[SYS_xv6_fstat]   sys_fstat,
[SYS_xv6_mkdir]   sys_mkdir,
[SYS_brk]         sys_brk,
[SYS_fstat]       sys_fstat,
[SYS_openat]      sys_openat,
[SYS_munmap]      sys_munmap,
[SYS_mmap]        sys_mmap,
[SYS_sched_yield] sys_sched_yield,
[SYS_chdir]       sys_chdir,
[SYS_pipe2]       sys_pipe2,
[SYS_mkdirat] sys_mkdirat,
[SYS_uname]       sys_uname,
};

// sysname - return the name of the system call for debugging.
char*
sysname(int num)
{
  switch(num){
  
  case SYS_xv6_fork:    return "fork";
  case SYS_xv6_exit:    return "exit";
  case SYS_xv6_wait:    return "wait";
  case SYS_xv6_read:    return "read";
  case SYS_xv6_write:   return "write";
  case SYS_xv6_kill:    return "kill";
  case SYS_xv6_getpid:  return "getpid";
  case SYS_xv6_sbrk:    return "sbrk";
  case SYS_xv6_sleep:   return "sleep";
  case SYS_xv6_uptime:  return "uptime";
  case SYS_xv6_shutdown: return "shutdown";
  case SYS_xv6_gettimeofday: return "gettimeofday";
  case SYS_xv6_open:    return "open";
  case SYS_xv6_mknod:   return "mknod";
  case SYS_xv6_close:   return "close";
  case SYS_xv6_dup:     return "dup";
  case SYS_xv6_exec:    return "exec";
  case SYS_xv6_fstat:   return "fstat";
  case SYS_xv6_mkdir:   return "mkdir";
  // case SYS_brk:    return "brk";
  // case SYS_mmap:    return "mmap";
  // case SYS_openat:    return "openat";
  // case SYS_getpid:    return "getpid";
  // case SYS_getppid:   return "getppid";
  default:          return "unknown";
  }
}

void
syscall_handler(void)
{
  int num;
  struct proc *p = myproc();
  
  // 获取系统调用号
  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // 轻量 syscall 跟踪：仅针对 busybox 进程打印调用与返回值
    int trace = ((p->name)[0] == 'b' &&
                 (p->name)[1] == 'u' &&
                 (p->name)[2] == 's' &&
                 (p->name)[3] == 'y' &&
                 (p->name)[4] == 'b' &&
                 (p->name)[5] == 'o' &&
                 (p->name)[6] == 'x' );
    if (trace) {
      printf("[syscall] pid=%d name=%s num=%d\n", p->pid, p->name, num);
    }
    // 系统函数返回值放在 p->trapframe->a0
    uint64 ret = syscalls[num]();
    p->trapframe->a0 = ret;
    if (trace) {
      printf("[syscall] pid=%d name=%s num=%d ret=%ld\n", p->pid, p->name, num, (long)ret);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
