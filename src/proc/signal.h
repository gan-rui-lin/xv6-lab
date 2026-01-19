#ifndef XV6_SIGNAL_H
#define XV6_SIGNAL_H

#include "types.h"

#define NSIG 64

// 常用信号号（Linux 兼容）
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGTTIN  21
#define SIGTTOU  22
#define SIGURG   23
#define SIGWINCH 28

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

// 特殊处理函数值
#define SIG_ERR ((uint64)-1)
#define SIG_DFL ((uint64)0)
#define SIG_IGN ((uint64)1)

// sigaction flags（简化版，保持 Linux 数值）
#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_ONSTACK   0x08000000
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000
#define SA_RESTORER  0x04000000

struct sigset {
  uint64 bits;
};

struct sigaction {
  uint64 sa_handler;
  uint64 sa_flags;
  uint64 sa_restorer;
  struct sigset sa_mask;
};

#endif // XV6_SIGNAL_H
