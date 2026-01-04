// 比赛直接通过 ecall 进行调用，而不需要依赖 fork, exit 的符号，所以不需要 usys.S 文件
// linux 定义的系统调用号
// 都采用下面的方式进行定义

#if defined(__ASSEMBLER__) || defined(__ASSEMBLY__)
// 当被汇编文件（.S）包含时，提供 SYS_xv6_* 的数值宏，避免 C 语法进入汇编器。
// 仅需为 usys.S 中使用到的 xv6 兼容调用号提供定义。
#define SYS_xv6_fork           (1001)
#define SYS_xv6_exit           (1002)
#define SYS_xv6_wait           (1003)
#define SYS_xv6_pipe           (1004)
#define SYS_xv6_read           (1005)
#define SYS_xv6_write          (1006)
#define SYS_xv6_close          (1007)
#define SYS_xv6_kill           (1008)
#define SYS_xv6_exec           (1009)
#define SYS_xv6_open           (1010)
#define SYS_xv6_mknod          (1011)
#define SYS_xv6_unlink         (1012)
#define SYS_xv6_fstat          (1013)
#define SYS_xv6_link           (1014)
#define SYS_xv6_mkdir          (1015)
#define SYS_xv6_chdir          (1016)
#define SYS_xv6_dup            (1017)
#define SYS_xv6_getpid         (1018)
#define SYS_xv6_sbrk           (1019)
#define SYS_xv6_sleep          (1020)
#define SYS_xv6_uptime         (1021)
#define SYS_xv6_shutdown       (1022)
#define SYS_xv6_gettimeofday   (1023)

#else
// 正常包含情况

#ifndef __scc
#define __scc(X) ((long)(X))
typedef long syscall_arg_t;
#endif

// 代表下面的实现都没有被包含，则进行定义
#ifndef __SYSCALL_LL_E

#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

#define __asm_syscall(...)             \
    __asm__ __volatile__("ecall\n\t"     \
                         : "=r"(a0)    \
                         : __VA_ARGS__ \
                         : "memory");  \
    return a0;

static inline long __syscall0(long n)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0");
    __asm_syscall("r"(a7))
}

static inline long __syscall1(long n, long a)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    __asm_syscall("r"(a7), "0"(a0))
}

static inline long __syscall2(long n, long a, long b)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    __asm_syscall("r"(a7), "0"(a0), "r"(a1))
}

static inline long __syscall3(long n, long a, long b, long c)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    __asm_syscall("r"(a7), "0"(a0), "r"(a1), "r"(a2))
}

static inline long __syscall4(long n, long a, long b, long c, long d)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    __asm_syscall("r"(a7), "0"(a0), "r"(a1), "r"(a2), "r"(a3))
}

static inline long __syscall5(long n, long a, long b, long c, long d, long e)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a4 __asm__("a4") = e;
    __asm_syscall("r"(a7), "0"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4))
}

static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f)
{
    register long a7 __asm__("a7") = n;
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a4 __asm__("a4") = e;
    register long a5 __asm__("a5") = f;
    __asm_syscall("r"(a7), "0"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5))
}

#define __syscall1(n, a) __syscall1(n, __scc(a))
#define __syscall2(n, a, b) __syscall2(n, __scc(a), __scc(b))
#define __syscall3(n, a, b, c) __syscall3(n, __scc(a), __scc(b), __scc(c))
#define __syscall4(n, a, b, c, d) __syscall4(n, __scc(a), __scc(b), __scc(c), __scc(d))
#define __syscall5(n, a, b, c, d, e) __syscall5(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e))
#define __syscall6(n, a, b, c, d, e, f) __syscall6(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e), __scc(f))

#define __SYSCALL_NARGS_X(a, b, c, d, e, f, g, h, n, ...) n
#define __SYSCALL_NARGS(...) __SYSCALL_NARGS_X(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, )
#define __SYSCALL_CONCAT_X(a, b) a##b
#define __SYSCALL_CONCAT(a, b) __SYSCALL_CONCAT_X(a, b)
#define __SYSCALL_DISP(b, ...)                        \
    __SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__)) \
    (__VA_ARGS__)

#define __syscall(...) __SYSCALL_DISP(__syscall, __VA_ARGS__)
#define syscall(...) __syscall(__VA_ARGS__)

#endif // __SYSCALL_LL_E

enum SysNum
{
    SYS_fork = 1,
    SYS_wait = 3,
    SYS_kill = 6,
    SYS_setxattr = 5,  // from rocket
    SYS_lsetxattr = 6, // from rocket
    SYS_fsetxattr = 7, // from rocket
    SYS_getxattr = 8,
    SYS_lgetxattr = 9,  // from rocket
    SYS_fgetxattr = 10, // from rocket
    SYS_listxattr = 11,
    SYS_llistxattr = 12,
    SYS_flistxattr = 13,
    SYS_removexattr = 14,
    SYS_lremovexattr = 15,
    SYS_fremovexattr = 16,
    SYS_sleep = 13,
    SYS_uptime = 14,
    SYS_mknod = 16,
    SYS_getcwd = 17,
    SYS_eventfd2 = 19,
    SYS_epoll_create1 = 20,
    SYS_epoll_ctl = 21,
    SYS_dup2 = 22,
    SYS_dup = 23,
    SYS_dup3 = 24,
    SYS_fcntl = 25,
    SYS_inotify_init1 = 26,
    SYS_ioctl = 29,
    SYS_flock = 32,
    SYS_mknodat = 33, // from rocket
    SYS_mkdirat = 34,
    SYS_unlinkat = 35,
    SYS_symlink = 88,   // symlink syscall
    SYS_symlinkat = 36, // from rocket
    SYS_linkat = 37,
    SYS_umount2 = 39,
    SYS_mount = 40,
    SYS_statfs = 43,    // form tsh
    SYS_fstatfs = 44,   // from rocket
    SYS_truncate = 45,  // from rocket
    SYS_ftruncate = 46, // form tsh
    SYS_fallocate = 47, // from rocket
    SYS_faccessat = 48, // form tsh
    SYS_chdir = 49,
    SYS_fchdir = 50,   // from rocket
    SYS_chroot = 51,   // from rocket
    SYS_fchmod = 52,   // from rocket
    SYS_fchmodat = 53, // from rocket
    SYS_fchownat = 54, // from rocket
    SYS_fchown = 55,   // from rocket
    SYS_exec = 55,     // Note: conflict with fchown, exec moved to 221
    SYS_openat = 56,
    SYS_close = 57,
    SYS_pipe2 = 59,
    SYS_getdents64 = 61,
    SYS_lseek = 62,
    SYS_read = 63,
    SYS_write = 64,
    SYS_readv = 65,
    SYS_writev = 66,
    SYS_pread64 = 67,  // form tsh
    SYS_pwrite64 = 68, // form tsh
    SYS_preadv = 69,   // from rocket
    SYS_pwritev = 70,  // from rocket
    SYS_sendfile = 71,
    SYS_pselect6 = 72, // form tsh
    SYS_ppoll = 73,
    SYS_signalfd4 = 74,
    SYS_vmsplice = 75,
    SYS_splice = 76,
    SYS_readlinkat = 78,
    SYS_fstatat = 79,
    SYS_fstat = 80,
    SYS_sync = 81,            // form tsh
    SYS_fsync = 82,           // form tsh
    SYS_fdatasync = 83,       // form tsh
    SYS_sync_file_range = 84, // from rocket
    SYS_timerfd_create = 85,  // from rocket
    SYS_utimensat = 88,
    SYS_acct = 89, // from rocket
    SYS_exit = 93,
    SYS_exit_group = 94,
    SYS_waitid = 95,
    SYS_set_tid_address = 96,
    SYS_futex = 98, // form tsh
    SYS_set_robust_list = 99,
    SYS_get_robust_list = 100, // form tsh
    SYS_nanosleep = 101,
    SYS_getitimer = 102,
    SYS_setitimer = 103, // form tsh
    SYS_timer_create = 107,
    SYS_timer_gettime = 108,
    SYS_timer_settime = 110,
    SYS_timer_delete = 111,
    SYS_clock_settime = 112, // from rocket
    SYS_clock_gettime = 113,
    SYS_clock_getres = 114, // from rocket
    SYS_clock_nanosleep = 115,
    SYS_syslog = 116,
    SYS_ptrace = 117,             // from rocket
    SYS_sched_setscheduler = 119, // from rocket
    SYS_sched_getscheduler = 120, // from rocket
    SYS_sched_getparam = 121,     // from rocket
    SYS_sched_setaffinity = 122,  // from rocket
    SYS_sched_getaffinity = 123,  // form tsh
    SYS_sched_yield = 124,
    SYS_kill_signal = 129,
    SYS_tkill = 130,
    SYS_tgkill = 131,
    SYS_sigaltstack = 132,   // from rocket
    SYS_rt_sigsuspend = 133, // from rocket
    SYS_rt_sigaction = 134,
    SYS_rt_sigprocmask = 135,
    SYS_rt_sigpending = 136, // from rocket
    SYS_rt_sigtimedwait = 137,
    SYS_rt_sigqueueinfo = 138, // from rocket
    SYS_rt_sigreturn = 139,
    SYS_setpriority = 140,
    SYS_getpriority = 141,
    SYS_reboot = 142,
    SYS_setregrid = 143, // from rocket
    SYS_setgid = 144,
    SYS_setreuid = 145, // from rocket
    SYS_setuid = 146,
    SYS_setresuid = 147, // from rocket
    SYS_getresuid = 148, // from rocket
    SYS_setresgid = 149, // from rocket
    SYS_getresgid = 150, // from rocket
    SYS_setfsuid = 151,  // from rocket
    SYS_setfsgid = 152,  // from rocket
    SYS_times = 153,
    SYS_setpgid = 154,   // form tsh
    SYS_getpgid = 155,   // form tsh
    SYS_setsid = 157,    // form tsh
    SYS_getsid = 156,    // 新增: get session id
    SYS_getgroups = 158, // from rocket
    SYS_setgroups = 159, // from rocket
    SYS_uname = 160,
    SYS_sethostname = 161,   // from rocket
    SYS_setdomainname = 162, // from rocket
    SYS_getrusage = 165,     // form tsh
    SYS_umask = 166,         // from rocket
    SYS_prctl = 167,         // from rocket
    SYS_gettimeofday = 169,
    SYS_adjtimex = 171, // from rocket
    SYS_getpid = 172,
    SYS_getppid = 173,
    SYS_getuid = 174,
    SYS_geteuid = 175,
    SYS_getgid = 176,
    SYS_getegid = 177, // form tsh
    SYS_gettid = 178,
    SYS_sysinfo = 179,
    SYS_semget = 190,
    SYS_semctl = 191,
    SYS_semtimedop = 192,
    SYS_semop = 193,
    SYS_shmget = 194,          // form tsh
    SYS_shmctl = 195,          // form tsh
    SYS_shmat = 196,           // form tsh
    SYS_shmdt = 197,           // from rocket
    SYS_socket = 198,          // form tsh
    SYS_socketpair = 199,      // form tsh
    SYS_bind = 200,            // form tsh
    SYS_listen = 201,          // form tsh
    SYS_accept = 202,          // form tsh
    SYS_connect = 203,         // form tsh
    SYS_getsockname = 204,     // form tsh
    SYS_getpeername = 205,     // form tsh
    SYS_sendto = 206,          // form tsh
    SYS_recvfrom = 207,        // form tsh
    SYS_setsockopt = 208,      // form tsh
    SYS_getsockopt = 209,      // form tsh
    SYS_shutdown_socket = 210, // from rocket
    SYS_sendmsg = 211,         // form tsh
    SYS_recvmsg = 212,         // from rocket
    SYS_readahead = 213,
    SYS_brk = 214,
    SYS_munmap = 215,
    SYS_mremap = 216,
    SYS_add_key = 217, // from rocket
    SYS_keyctl = 219,
    SYS_clone = 220,
    SYS_execve = 221,
    SYS_mmap = 222,
    SYS_fadvise64 = 223, // from rocket
    SYS_mprotect = 226,  // form tsh
    SYS_msync = 227,     // from rocket
    SYS_mlock = 228,     // from rocket
    SYS_madvise = 233,
    SYS_remap_file_pages = 234, // from rocket
    SYS_get_mempolicy = 236,    // from rocket
    SYS_perf_event_open = 241,
    SYS_accept4 = 242, // from rocket
    SYS_wait4 = 260,
    SYS_prlimit64 = 261,
    SYS_fanotify_init = 262,
    SYS_clockadjtime = 266, // from rocket
    SYS_setns = 268,
    SYS_renameat2 = 276,
    SYS_getrandom = 278,
    SYS_memfd_create = 279,
    SYS_bpf = 280,
    SYS_userfaultfd = 282,
    SYS_membarrier = 283,      // form tsh
    SYS_copy_file_range = 285, // from rocket
    SYS_statx = 291,
    SYS_strerror = 300, // from rocket
    SYS_perror = 301,   // from rocket
    SYS_io_uring_setup = 425,
    SYS_open_tree = 428,
    SYS_fsopen = 430,
    SYS_fspick = 433,
    SYS_pidfd_open = 434,
    SYS_clone3 = 435,      // form tsh
    SYS_close_range = 436, // from rocket
    SYS_openat2 = 437,     // from rocket
    SYS_faccessat2 = 439,  // from rocket
    SYS_memfd_secret = 447,
    SYS_fchmodat2 = 452,

    // xv6 兼容的调用号，从 1000 开始
    // 当对应 linux 对应 sys_func 实现好了后，就会从 1000 撤下，不再使用 xv6 兼容调用号
    SYS_xv6_mknod = 1 + 1000,

    SYS_xv6_shutdown = 2 + 1000,
};

#endif // __ASSEMBLER__/__ASSEMBLY__