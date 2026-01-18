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

// 是否需要输出系统调用跟踪。
static int
should_trace(struct proc *p)
{
  // 方便调试时通过 GDB 把这个变量置 1，从而开启全局 syscall 跟踪。
  extern int syscall_trace_all;
  if(syscall_trace_all)
    return 1;

  // 默认只跟踪 busybox 进程，帮助定位其系统调用实现问题。
  return (p->name[0] == 'b' &&
          p->name[1] == 'u' &&
          p->name[2] == 's' &&
          p->name[3] == 'y' &&
          p->name[4] == 'b' &&
          p->name[5] == 'o' &&
          p->name[6] == 'x');
}

// 全局开关，默认关闭，便于 GDB 手动打开。
int syscall_trace_all = 0;

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
extern uint64 sys_gettid(void);
extern uint64 sys_getuid(void);
extern uint64 sys_geteuid(void);
extern uint64 sys_getgid(void);
extern uint64 sys_getegid(void);
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
extern uint64 sys_fstatat(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_clone(void);
extern uint64 sys_execve(void);
extern uint64 sys_wait4(void);
extern uint64 sys_getppid(void);
extern uint64 sys_exit_group(void);
extern uint64 sys_set_tid_address(void);
extern uint64 sys_brk(void);
extern uint64 sys_mmap(void);
extern uint64 sys_munmap(void);
extern uint64 sys_mprotect(void);
extern uint64 sys_openat(void);
extern uint64 sys_sched_yield(void);
extern uint64 sys_dup3(void);
extern uint64 sys_getdents64(void);
extern uint64 sys_mount(void);
extern uint64 sys_umount2(void);
extern uint64 sys_getcwd(void);
extern uint64 sys_chdir(void);
extern uint64 sys_pipe2(void);
extern uint64 sys_dup2(void);
extern uint64 sys_mkdirat(void);
extern uint64 sys_uname(void);
extern uint64 sys_nanosleep(void);
extern uint64 sys_times(void);
extern uint64 sys_unlinkat(void);
extern uint64 sys_fcntl(void);
extern uint64 sys_writev(void);
extern uint64 sys_rt_sigaction(void);
extern uint64 sys_rt_sigprocmask(void);
extern uint64 sys_rt_sigtimedwait(void);
extern uint64 sys_kill_signal(void);
extern uint64 sys_prlimit64(void);
extern uint64 sys_symlink(void);
extern uint64 sys_symlinkat(void);
extern uint64 sys_sendfile(void);
extern uint64 sys_ppoll(void);

extern uint64 sys_setpriority(void);
extern uint64 sys_getpriority(void);
// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
[SYS_umount2]    sys_umount2,
[SYS_times]       sys_times,
[SYS_nanosleep]   sys_nanosleep,
[SYS_setpriority] sys_setpriority,
[SYS_getpriority] sys_getpriority,
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
[SYS_exit_group]  sys_exit_group,
[SYS_wait4]       sys_wait4,
[SYS_execve]      sys_execve,
[SYS_getpid]      sys_getpid,
[SYS_gettid]      sys_gettid,
[SYS_getppid]     sys_getppid,
[SYS_getuid]      sys_getuid,
[SYS_geteuid]     sys_geteuid,
[SYS_getgid]      sys_getgid,
[SYS_getegid]     sys_getegid,
[SYS_set_tid_address] sys_set_tid_address,
[SYS_gettimeofday] sys_gettimeofday,
[SYS_openat]      sys_openat,

[SYS_xv6_shutdown] sys_shutdown,

[SYS_xv6_mknod]   sys_mknod,

[SYS_brk]         sys_brk,
[SYS_fstat]       sys_fstat,
[SYS_fcntl]       sys_fcntl,
[SYS_openat]      sys_openat,
[SYS_munmap]      sys_munmap,
[SYS_mmap]        sys_mmap,
[SYS_mprotect]    sys_mprotect,
[SYS_sched_yield] sys_sched_yield,
[SYS_chdir]       sys_chdir,
[SYS_pipe2]       sys_pipe2,
[SYS_mkdirat] sys_mkdirat,
[SYS_uname]       sys_uname,
[SYS_unlinkat]     sys_unlinkat,
[SYS_fstatat]      sys_fstatat,
[SYS_writev]      sys_writev,
[SYS_sendfile]    sys_sendfile,
[SYS_rt_sigaction] sys_rt_sigaction,
[SYS_rt_sigprocmask] sys_rt_sigprocmask,
[SYS_rt_sigtimedwait] sys_rt_sigtimedwait,
[SYS_kill_signal] sys_kill_signal,
[SYS_prlimit64]   sys_prlimit64,
[SYS_symlink]     sys_symlink,
[SYS_symlinkat]   sys_symlinkat,
[SYS_ppoll]       sys_ppoll,
};

// sysname - return the name of the system call for debugging.
char*
sysname(int num)
{
  switch(num) {
  case SYS_fork: return "SYS_fork";
  case SYS_wait: return "SYS_wait";
  case SYS_setxattr: return "SYS_setxattr";
  case SYS_kill: return "SYS_kill";
  // case SYS_lsetxattr: return "SYS_lsetxattr";
  case SYS_fsetxattr: return "SYS_fsetxattr";
  case SYS_getxattr: return "SYS_getxattr";
  case SYS_lgetxattr: return "SYS_lgetxattr";
  case SYS_fgetxattr: return "SYS_fgetxattr";
  case SYS_listxattr: return "SYS_listxattr";
  case SYS_llistxattr: return "SYS_llistxattr";
  // case SYS_flistxattr: return "SYS_flistxattr";
  case SYS_sleep: return "SYS_sleep";
  // case SYS_removexattr: return "SYS_removexattr";
  case SYS_uptime: return "SYS_uptime";
  case SYS_lremovexattr: return "SYS_lremovexattr";
  // case SYS_fremovexattr: return "SYS_fremovexattr";
  case SYS_mknod: return "SYS_mknod";
  case SYS_getcwd: return "SYS_getcwd";
  case SYS_eventfd2: return "SYS_eventfd2";
  case SYS_epoll_create1: return "SYS_epoll_create1";
  case SYS_epoll_ctl: return "SYS_epoll_ctl";
  case SYS_dup2: return "SYS_dup2";
  case SYS_dup: return "SYS_dup";
  case SYS_dup3: return "SYS_dup3";
  case SYS_fcntl: return "SYS_fcntl";
  case SYS_inotify_init1: return "SYS_inotify_init1";
  case SYS_ioctl: return "SYS_ioctl";
  case SYS_flock: return "SYS_flock";
  case SYS_mknodat: return "SYS_mknodat";
  case SYS_mkdirat: return "SYS_mkdirat";
  case SYS_unlinkat: return "SYS_unlinkat";
  case SYS_symlinkat: return "SYS_symlinkat";
  case SYS_linkat: return "SYS_linkat";
  case SYS_umount2: return "SYS_umount2";
  case SYS_mount: return "SYS_mount";
  case SYS_statfs: return "SYS_statfs";
  case SYS_fstatfs: return "SYS_fstatfs";
  case SYS_truncate: return "SYS_truncate";
  case SYS_ftruncate: return "SYS_ftruncate";
  case SYS_fallocate: return "SYS_fallocate";
  case SYS_faccessat: return "SYS_faccessat";
  case SYS_chdir: return "SYS_chdir";
  case SYS_fchdir: return "SYS_fchdir";
  case SYS_chroot: return "SYS_chroot";
  case SYS_fchmod: return "SYS_fchmod";
  case SYS_fchmodat: return "SYS_fchmodat";
  case SYS_fchownat: return "SYS_fchownat";
  // case SYS_fchown: return "SYS_fchown";
  case SYS_exec: return "SYS_exec";
  case SYS_openat: return "SYS_openat";
  case SYS_close: return "SYS_close";
  case SYS_pipe2: return "SYS_pipe2";
  case SYS_getdents64: return "SYS_getdents64";
  case SYS_lseek: return "SYS_lseek";
  case SYS_read: return "SYS_read";
  case SYS_write: return "SYS_write";
  case SYS_readv: return "SYS_readv";
  case SYS_writev: return "SYS_writev";
  case SYS_pread64: return "SYS_pread64";
  case SYS_pwrite64: return "SYS_pwrite64";
  case SYS_preadv: return "SYS_preadv";
  case SYS_pwritev: return "SYS_pwritev";
  case SYS_sendfile: return "SYS_sendfile";
  case SYS_pselect6: return "SYS_pselect6";
  case SYS_ppoll: return "SYS_ppoll";
  case SYS_signalfd4: return "SYS_signalfd4";
  case SYS_vmsplice: return "SYS_vmsplice";
  case SYS_splice: return "SYS_splice";
  case SYS_readlinkat: return "SYS_readlinkat";
  case SYS_fstatat: return "SYS_fstatat";
  case SYS_fstat: return "SYS_fstat";
  case SYS_sync: return "SYS_sync";
  case SYS_fsync: return "SYS_fsync";
  case SYS_fdatasync: return "SYS_fdatasync";
  case SYS_sync_file_range: return "SYS_sync_file_range";
  case SYS_timerfd_create: return "SYS_timerfd_create";
  case SYS_symlink: return "SYS_symlink";
  // case SYS_utimensat: return "SYS_utimensat";
  case SYS_acct: return "SYS_acct";
  case SYS_exit: return "SYS_exit";
  case SYS_exit_group: return "SYS_exit_group";
  case SYS_waitid: return "SYS_waitid";
  case SYS_set_tid_address: return "SYS_set_tid_address";
  case SYS_futex: return "SYS_futex";
  case SYS_set_robust_list: return "SYS_set_robust_list";
  case SYS_get_robust_list: return "SYS_get_robust_list";
  case SYS_nanosleep: return "SYS_nanosleep";
  case SYS_getitimer: return "SYS_getitimer";
  case SYS_setitimer: return "SYS_setitimer";
  case SYS_timer_create: return "SYS_timer_create";
  case SYS_timer_gettime: return "SYS_timer_gettime";
  case SYS_timer_settime: return "SYS_timer_settime";
  case SYS_timer_delete: return "SYS_timer_delete";
  case SYS_clock_settime: return "SYS_clock_settime";
  case SYS_clock_gettime: return "SYS_clock_gettime";
  case SYS_clock_getres: return "SYS_clock_getres";
  case SYS_clock_nanosleep: return "SYS_clock_nanosleep";
  case SYS_syslog: return "SYS_syslog";
  case SYS_ptrace: return "SYS_ptrace";
  case SYS_sched_setscheduler: return "SYS_sched_setscheduler";
  case SYS_sched_getscheduler: return "SYS_sched_getscheduler";
  case SYS_sched_getparam: return "SYS_sched_getparam";
  case SYS_sched_setaffinity: return "SYS_sched_setaffinity";
  case SYS_sched_getaffinity: return "SYS_sched_getaffinity";
  case SYS_sched_yield: return "SYS_sched_yield";
  case SYS_kill_signal: return "SYS_kill_signal";
  case SYS_tkill: return "SYS_tkill";
  case SYS_tgkill: return "SYS_tgkill";
  case SYS_sigaltstack: return "SYS_sigaltstack";
  case SYS_rt_sigsuspend: return "SYS_rt_sigsuspend";
  case SYS_rt_sigaction: return "SYS_rt_sigaction";
  case SYS_rt_sigprocmask: return "SYS_rt_sigprocmask";
  case SYS_rt_sigpending: return "SYS_rt_sigpending";
  case SYS_rt_sigtimedwait: return "SYS_rt_sigtimedwait";
  case SYS_rt_sigqueueinfo: return "SYS_rt_sigqueueinfo";
  case SYS_rt_sigreturn: return "SYS_rt_sigreturn";
  case SYS_setpriority: return "SYS_setpriority";
  case SYS_getpriority: return "SYS_getpriority";
  case SYS_reboot: return "SYS_reboot";
  case SYS_setregrid: return "SYS_setregrid";
  case SYS_setgid: return "SYS_setgid";
  case SYS_setreuid: return "SYS_setreuid";
  case SYS_setuid: return "SYS_setuid";
  case SYS_setresuid: return "SYS_setresuid";
  case SYS_getresuid: return "SYS_getresuid";
  case SYS_setresgid: return "SYS_setresgid";
  case SYS_getresgid: return "SYS_getresgid";
  case SYS_setfsuid: return "SYS_setfsuid";
  case SYS_setfsgid: return "SYS_setfsgid";
  case SYS_times: return "SYS_times";
  case SYS_setpgid: return "SYS_setpgid";
  case SYS_getpgid: return "SYS_getpgid";
  case SYS_getsid: return "SYS_getsid";
  case SYS_setsid: return "SYS_setsid";
  case SYS_getgroups: return "SYS_getgroups";
  case SYS_setgroups: return "SYS_setgroups";
  case SYS_uname: return "SYS_uname";
  case SYS_sethostname: return "SYS_sethostname";
  case SYS_setdomainname: return "SYS_setdomainname";
  case SYS_getrusage: return "SYS_getrusage";
  case SYS_umask: return "SYS_umask";
  case SYS_prctl: return "SYS_prctl";
  case SYS_gettimeofday: return "SYS_gettimeofday";
  case SYS_adjtimex: return "SYS_adjtimex";
  case SYS_getpid: return "SYS_getpid";
  case SYS_getppid: return "SYS_getppid";
  case SYS_getuid: return "SYS_getuid";
  case SYS_geteuid: return "SYS_geteuid";
  case SYS_getgid: return "SYS_getgid";
  case SYS_getegid: return "SYS_getegid";
  case SYS_gettid: return "SYS_gettid";
  case SYS_sysinfo: return "SYS_sysinfo";
  case SYS_semget: return "SYS_semget";
  case SYS_semctl: return "SYS_semctl";
  case SYS_semtimedop: return "SYS_semtimedop";
  case SYS_semop: return "SYS_semop";
  case SYS_shmget: return "SYS_shmget";
  case SYS_shmctl: return "SYS_shmctl";
  case SYS_shmat: return "SYS_shmat";
  case SYS_shmdt: return "SYS_shmdt";
  case SYS_socket: return "SYS_socket";
  case SYS_socketpair: return "SYS_socketpair";
  case SYS_bind: return "SYS_bind";
  case SYS_listen: return "SYS_listen";
  case SYS_accept: return "SYS_accept";
  case SYS_connect: return "SYS_connect";
  case SYS_getsockname: return "SYS_getsockname";
  case SYS_getpeername: return "SYS_getpeername";
  case SYS_sendto: return "SYS_sendto";
  case SYS_recvfrom: return "SYS_recvfrom";
  case SYS_setsockopt: return "SYS_setsockopt";
  case SYS_getsockopt: return "SYS_getsockopt";
  case SYS_shutdown_socket: return "SYS_shutdown_socket";
  case SYS_sendmsg: return "SYS_sendmsg";
  case SYS_recvmsg: return "SYS_recvmsg";
  case SYS_readahead: return "SYS_readahead";
  case SYS_brk: return "SYS_brk";
  case SYS_munmap: return "SYS_munmap";
  case SYS_mremap: return "SYS_mremap";
  case SYS_add_key: return "SYS_add_key";
  case SYS_keyctl: return "SYS_keyctl";
  case SYS_clone: return "SYS_clone";
  case SYS_execve: return "SYS_execve";
  case SYS_mmap: return "SYS_mmap";
  case SYS_fadvise64: return "SYS_fadvise64";
  case SYS_mprotect: return "SYS_mprotect";
  case SYS_msync: return "SYS_msync";
  case SYS_mlock: return "SYS_mlock";
  case SYS_madvise: return "SYS_madvise";
  case SYS_remap_file_pages: return "SYS_remap_file_pages";
  case SYS_get_mempolicy: return "SYS_get_mempolicy";
  case SYS_perf_event_open: return "SYS_perf_event_open";
  case SYS_accept4: return "SYS_accept4";
  case SYS_wait4: return "SYS_wait4";
  case SYS_prlimit64: return "SYS_prlimit64";
  case SYS_fanotify_init: return "SYS_fanotify_init";
  case SYS_clockadjtime: return "SYS_clockadjtime";
  case SYS_setns: return "SYS_setns";
  case SYS_renameat2: return "SYS_renameat2";
  case SYS_getrandom: return "SYS_getrandom";
  case SYS_memfd_create: return "SYS_memfd_create";
  case SYS_bpf: return "SYS_bpf";
  case SYS_userfaultfd: return "SYS_userfaultfd";
  case SYS_membarrier: return "SYS_membarrier";
  case SYS_copy_file_range: return "SYS_copy_file_range";
  case SYS_statx: return "SYS_statx";
  case SYS_strerror: return "SYS_strerror";
  case SYS_perror: return "SYS_perror";
  case SYS_io_uring_setup: return "SYS_io_uring_setup";
  case SYS_open_tree: return "SYS_open_tree";
  case SYS_fsopen: return "SYS_fsopen";
  case SYS_fspick: return "SYS_fspick";
  case SYS_pidfd_open: return "SYS_pidfd_open";
  case SYS_clone3: return "SYS_clone3";
  case SYS_close_range: return "SYS_close_range";
  case SYS_openat2: return "SYS_openat2";
  case SYS_faccessat2: return "SYS_faccessat2";
  case SYS_memfd_secret: return "SYS_memfd_secret";
  case SYS_fchmodat2: return "SYS_fchmodat2";
  default: return "unknown";
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
    int trace = should_trace(p);

    // 提前记录调用参数，避免系统调用内部修改 a 寄存器后看不到原始值。
    uint64 args[6] = {
      p->trapframe->a0, p->trapframe->a1, p->trapframe->a2,
      p->trapframe->a3, p->trapframe->a4, p->trapframe->a5
    };


    if (trace) {
      log_trace("[syscall] pid=%d name=%s num=%d (%s) args=[%p,%p,%p,%p,%p,%p]\n",
             p->pid, p->name, num, sysname(num),
             (void *)args[0], (void *)args[1], (void *)args[2],
             (void *)args[3], (void *)args[4], (void *)args[5]);
    }

    // 系统函数返回值放在 p->trapframe->a0
    uint64 ret = syscalls[num]();
    p->trapframe->a0 = ret;
  } else {
    printf("%d %s: unimplemented sys call %s\n",
            p->pid, p->name, sysname(num));
    p->trapframe->a0 = -1;
  }
}
