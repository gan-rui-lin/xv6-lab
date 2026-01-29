#include "param.h"
#include "types.h"
#include "spinlock.h"
#include "proc/signal.h"

#if !defined(PROC_T)
#define PROC_T

// Shared memory attachment structure
struct shm_attach {
  int shmid;       // shared memory ID
  void *vaddr;     // virtual address in process space
  int valid;       // 1 if this entry is in use
};

//claude: MLFQ进程信息结构（前向声明，完整定义在mlfq.h）
struct mlfq_proc_info {
  int level;              //claude: 当前所在的优先级级别（0-3）
  uint64 time_slice;      //claude: 当前级别分配的时间片大小
  uint64 ticks_used;      //claude: 在当前级别已使用的时间片
  uint64 total_ticks;     //claude: 进程总运行时间（用于统计）
  int voluntary_yield;    //claude: 是否主动让出CPU（I/O等待）
};


// 上下文切换保存的寄存器
// swtch() 函数在切换进程时保存这些寄存器
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};


// 高级进程，每个 CPU 只有一个 cpu 结构体
struct cpu {
  struct proc *proc;          // The process running on this cpu, or null.
  struct context context;     // swtch() here to enter scheduler().
  int noff;                   // Depth of push_off() nesting.
  int intena;                 // Were interrupts enabled before push_off()?
};

struct vma;

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// 优先级定义（数值越小，优先级越高）
#define PRIO_MIN      1     // 最高优先级
#define PRIO_MAX      40    // 最低优先级
#define PRIO_DEFAULT  20    // 默认优先级

struct proc
{
  // 调度器或者其他进程会并发访问这些字段
  struct spinlock lock;       // 保护进程状态的锁
  int pid;                     // 进程ID
  int killed;                  // If non-zero, have been killed
  enum procstate state;        // Process state
  void *chan;                  // If non-zero, sleeping on chan
  int xstate;                  // Exit status to be returned to parent's wait
  int priority;                // 进程优先级（1-40，数值越小优先级越高）

  // 持有 wait_lock
  struct proc *parent;         // Parent process

  // 只有该进程本身（当前 CPU 上）会读写它们
  // 调度器不会并发访问这些字段
  // 其他进程也不会访问这些字段
  uint64 sz;                   // 进程内存大小（字节数）
  pagetable_t pagetable;       // 用户页表
  struct trapframe *trapframe; // 用于用户态陷阱处理的trap
  struct context context;      // swtch()到此进程时保存的上下文
  uint64 kernel_stack;         // 进程内核栈地址
  char name[16];               // 进程名字（仅用于调试）

  // 文件系统相关
  struct file *ofile[NOFILE];  // Open files
  uint8 fdflags[NOFILE];        // Per-fd flags (e.g., FD_CLOEXEC)
  struct inode *cwd;           // Current directory
  char cwdpath[MAXPATH];       // Current directory path string

  // mmap VMA 列表
  struct vma *vma;

  // 信号处理相关
  uint64 sigpending;           // pending signals bitmap
  uint64 sigmask;              // blocked signals bitmap
  struct sigaction sigactions[NSIG]; // per-signal handler settings

  // clone/线程相关（简化）
  uint64 clear_child_tid;       // user addr to clear on exit (CLONE_CHILD_CLEARTID)
  int exit_signal;              // signal to parent on exit (0 for none)

  // 内核线程相关
  int is_kthread;
  void (*kthread_fn)(void *);
  void *kthread_arg;

  // robust futex list (glibc 线程支持)
  uint64 robust_list_head;         // pointer to user-space robust_list_head
  uint64 robust_list_len;          // length passed by user

  // 共享内存附加表 (System V shared memory)
  struct shm_attach shm_attach[16]; // SHM_MAX_ATTACH

  // UID/GID for IPC permissions
  uint uid;
  uint gid;

  //claude: MLFQ调度信息
  struct mlfq_proc_info mlfq;  //claude: 多级反馈队列调度状态
};


extern struct cpu cpus[NCPU];

struct proc* allocproc();

struct trapframe {
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};


#endif // PROC_T
