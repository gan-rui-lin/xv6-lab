
= 系统调用

RuOS 的系统调用沿用 RISC‑V/Linux 的基本约定：用户态通过 `ecall` 进入内核，系统调用号放在 `a7`，参数依次放在 `a0..a5`，返回值写回 `a0`。我们在 xv6 的基础上补齐了更接近 Linux 语义的一套系统调用，并在 trap 入口、参数解码、错误码返回与兼容层方面做了系统化整理，保证用户态程序（如 busybox）的正确执行。

与 xv6 直接硬编码 usys.S 不同，Ruos 在用户态封装使用了 `src/syscall/syscall.h` 中的变参宏 `syscall(...)`：通过 `__SYSCALL_NARGS` 统计参数个数，再用 `__SYSCALL_CONCAT` 选择对应的 `__syscall0..6` 内联实现。每个 `__syscallN` 把参数装入 `a0..a5`、系统调用号装入 `a7`，再执行内联 `ecall` 并返回 `a0`。这种宏分发的写法让上层接口像普通 C 函数一样调用，同时自动处理参数个数与类型提升（`__scc` 统一转为 `long`），避免为不同参数数目编写重复包装代码。

示例代码如下：

```c
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

#define __syscall1(n, a) __syscall1(n, __scc(a))
#define __syscall2(n, a, b) __syscall2(n, __scc(a), __scc(b))
#define __syscall3(n, a, b, c) __syscall3(n, __scc(a), __scc(b), __scc(c))

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
```

== 调用路径概览

系统调用是用户态陷入内核态的最常见路径，其核心链路如下（对应 `src/trap/` 与 `src/syscall/`）：

- 用户态执行 `ecall`，硬件跳转到 `TRAMPOLINE` 映射上的 `uservec`；
- `uservec` 保存寄存器到 `trapframe`，切换到内核页表与内核栈，再跳转到 `usertrap()`；
- `usertrap()` 识别 `scause==ECODE_SYSCALL`，手动 `epc+=4` 跳过 `ecall`，开中断后调用 `syscall_handler()`；
- `syscall_handler()` 根据 `a7` 在 `syscalls[]` 表中找到 `sys_*` 处理函数，执行后把返回值写回 `a0`；
- `usertrapret()` 负责恢复 `stvec`、`sstatus`、`sepc` 并跳转 `userret`，最终 `sret` 返回用户态。

系统调用与外设中断共享同一套陷阱框架：在 `usertrap()` 中，`devintr()` 先处理外设/时钟中断，若是时钟中断则在返回前 `yield()` 让出 CPU；这保证了系统调用不会阻塞调度器，并且中断处理的时序可控。

系统调用全流程如 @ruos-syscall-flow 所示（包含可选 trace/统计与异常处理分支）：

#figure(
image("syscall-flow.png",height: 85%),
caption: "系统调用全流程"
) <ruos-syscall-flow>


== TRAMPOLINE 与 trapframe 机制

TRAMPOLINE 与 trapframe 的设计解决了“切换时地址必须一直有效、且用户不可伪造上下文”的关键问题：陷入发生时 CPU 仍在用户页表下执行，必须有一段在用户页表与内核页表中都映射到同一虚拟地址的入口代码；同时寄存器保存区必须对内核可写、对用户不可写，否则用户可篡改返回现场或内核信息。将 TRAMPOLINE 固定映射到最高虚拟地址页、trapframe 固定映射到其下一页且 `PTE_U=0`，既保证陷入/返回路径的地址稳定，又避免在每次陷入时复制寄存器结构体，兼顾安全性与性能。

为了在用户态与内核态之间快速、安全地切换，RuOS 采用与 xv6 类似的 TRAMPOLINE 设计：

- TRAMPOLINE 映射在最高虚拟地址页（`MAXVA - PGSIZE`），所有进程共享这页代码，用于 `uservec/userret`；
- TRAPFRAME 映射在 `TRAMPOLINE` 下方一页（`MAXVA - 2*PGSIZE`），每个进程独占一页，用户不可访问（`PTE_U=0`）。

`trapframe` 中保存了用户态寄存器与返回上下文，同时还包括内核态需要的“跳板信息”（内核页表、内核栈顶、`usertrap` 入口、hartid）。`uservec` 先把用户寄存器写入 `trapframe`，再切换 `satp` 到内核页表；`userret` 在返回时反向恢复寄存器并 `sret` 回到用户态。这种布局的好处是：内核可以在不信任用户态内存的前提下完成寄存器搬运，同时避免每次陷入都复制结构体。

== 参数传递与用户指针解码

系统调用参数通过寄存器传递：`a0..a5` 为 6 个参数槽，`a7` 为系统调用号。`src/syscall/syscall.c` 中提供了一套统一的参数解码函数：

- `argraw(n)` 直接读取 trapframe 中的寄存器；
- `argint/argaddr/argstr` 在其基础上做类型转换与拷贝；
- `fetchaddr/fetchstr` 负责从用户页表中安全地 `copyin/copyinstr`。

这一层“参数解码 + 安全拷贝”的抽象很关键：它把用户指针与内核指针严格隔离，所有用户内存访问都必须经过 `copyin/copyout`。因此即使用户传入非法地址，内核也只会返回 `-EFAULT`，而不会发生越界访问。

== 系统调用分发与返回

系统调用分发由 `syscall_handler()` 完成，其逻辑非常直接：

- 从 `trapframe->a7` 取系统调用号；
- 在 `syscalls[]` 表中找到对应 `sys_*` 函数并执行；
- 返回值写回 `trapframe->a0`，作为用户态的系统调用返回值。

值得注意的是：`usertrap()` 会手动把 `epc` 加 4，这是为了跳过 `ecall` 指令本身，避免回到用户态后再次触发陷入；这也是 RISC‑V 软件处理系统调用的通用做法。

在实现上，RuOS 保留了统一的跟踪开关 `syscall_trace_all`（默认关闭），便于在 GDB 中快速启用系统调用日志；此外还保留了错误打印与返回码映射路径，用于调试用户态程序兼容性。

== 系统调用集合

RuOS 实现了接近 Linux 语义的系统调用集合，涵盖文件系统、进程管理、内存管理、时间管理与网络通信等核心功能。主要系统调用包括：

- 进程/线程：`fork`、`clone`、`execve`、`exit/exit_group`、`wait/wait4`、`getpid/getppid`、`set_tid_address`；
- 内存管理：`brk/sbrk`、`mmap/munmap`、`mprotect`、`msync`；
- 文件与目录：`open/openat`、`close`、`read/write/writev`、`lseek`、`fstat/fstatat`、`mkdir/mkdirat`、`chdir/getcwd`、`unlinkat`、`fcntl`；
- 时间与定时：`sleep`、`nanosleep`、`gettimeofday`、`clock_gettime`、`times`、`setitimer`；
- 信号与调度：`rt_sigaction`、`rt_sigprocmask`、`rt_sigreturn`、`kill/tkill/tgkill`、`sched_yield`、`sched_getaffinity`；
- 管道与重定向：`pipe2`、`dup/dup2/dup3`；
- 系统/挂载：`uname`、`mount`、`umount2`、`shutdown`、`prlimit64`；
- 网络：`socket`、`bind`、`connect`、`listen`、`accept`、`sendto`、`recvfrom`、`sendfile`、`ppoll`、`ioctl`。

== 错误码与语义对齐

为了尽量与 Linux 用户态兼容，RuOS 的系统调用返回值遵循“成功返回非负，失败返回负 errno”的约定。内核中每个 `sys_*` 会根据底层模块的错误返回值进行映射：

- 文件系统与进程管理：直接返回 `-EINVAL/-ENOENT/-EFAULT/-ENOMEM` 等；
- 网络子系统：ONPS 的错误码会通过 `onps_err_to_errno()` 映射为 Linux errno；
- 不支持或未实现的调用：返回 `-ENOTSUP` 或 `-EINVAL`。

部分 RuOS 已经支持的错误码映射如下：

```c
#define EPERM   1   /* Operation not permitted */
#define ENOENT  2   /* No such file or directory */
#define EINTR   4   /* Interrupted system call */
#define EIO     5   /* I/O error */
#define ENODEV  19  /* No such device */
#define ENOTDIR 20  /* Not a directory */
#define EISDIR  21  /* Is a directory */
#define EINVAL  22  /* Invalid argument */
#define EMFILE  24  /* Too many open files */
#define ENFILE  23  /* File table overflow */
#define ENOTTY  25  /* Inappropriate ioctl for device */
#define EACCES  13  /* Permission denied */
#define EFAULT  14  /* Bad address */
#define ENOMEM  12  /* Out of memory */
#define EAGAIN  11  /* Resource temporarily unavailable */
#define EWOULDBLOCK 11 /* Operation would block (same as EAGAIN) */
#define EBADF   9   /* Bad file descriptor */
#define ECHILD  10  /* No child processes */
#define ESRCH   3   /* No such process */
#define ENOEXEC 8   /* Exec format error */
#define E2BIG   7   /* Argument list too long */
#define ENOSYS  38  /* Function not implemented */
#define ENOTSUP 95  /* Operation not supported */
#define ESPIPE  29  /* Illegal seek */
```

这一层错误码语义是 busybox 等程序正常运行的关键：用户态不需要理解内核内部细节，只需按 POSIX/LINUX 的方式判断返回值即可。

== 调试与扩展点

系统调用相关的扩展与调试主要集中在以下位置：

- `syscall_handler()`：增加特定进程 tracing、参数和返回值打印等功能；
- `arg*`/`copyin`：可增加用户指针合法性检查或访问统计；
- `handle_exception()`：可扩展页故障恢复策略（如懒分配、COW）；
- `syscalls[]`：新增系统调用时只需注册入口并实现 `sys_*` 即可。

通过这些扩展点，RuOS 能在保持内核结构清晰的前提下逐步完善 Linux 语义与用户态兼容性，这也是我们在比赛过程中快速迭代系统调用的关键工程方法。

