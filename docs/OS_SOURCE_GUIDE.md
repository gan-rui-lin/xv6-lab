# RUOS 操作系统源码导读

本文汇总仓库中的操作系统相关源码，按职责划分为若干部分，便于后续在 `os-docs/` 中展开细节。各章节链接已指向对应的空白占位文件，补充时可直接填充内容。

## 快速索引
- [引导与启动链路](./os-docs/boot-and-startup.md) — 从固件到内核第一条指令：`src/boot/`, `src/bootloader/`, `src/boot/entry.S`, `src/boot/main.c`, `linker/kernel.ld`, `user/initcode.*`。
- [内存管理](./os-docs/memory-management.md) — 物理页分配与多级页表：`src/mm/`, `src/memlayout.h`, `src/riscv.h`, `src/param.h`。
- [进程、调度与上下文切换](./os-docs/process-and-scheduling.md) — 进程生命周期与上下文保存：`src/proc/`, `src/sync/`, `src/trap/trampoline.S`。
- [中断、异常与系统调用](./os-docs/trap-and-syscall.md) — S/U 态转换、陷阱向量与 syscalls：`src/trap/`, `src/syscall/`, `src/lib/sbi.c`, `src/proc/elf.h`。
- [文件系统与存储](./os-docs/filesystem-and-storage.md) — FAT32/日志/缓存与管道：`src/fs/`, `src/mkfs/mkfs.c`, `src/types-fat32.h`, `src/fcntl.h`。
- [设备与驱动抽象](./os-docs/device-and-drivers.md) — 控制台、UART、PLIC 与 VirtIO 磁盘：`src/devs/`, `src/virtIO/`。
- [同步与锁](./os-docs/synchronization.md) — 自旋锁与睡眠锁的封装与使用：`src/sync/`。
- [用户态与运行库](./os-docs/userland.md) — 用户程序、系统调用封装与链接脚本：`user/`, `src/lib/`, `src/fcntl.h`, `user/user.ld`、`user/user-riscv.ld`。
- [构建、链接与运行辅助](./os-docs/build-and-tools.md) — 构建脚本、链接脚本与运行入口：`Makefile`, `run.sh`, `src/linker/kernel.ld`, `src/bootloader/*.bin`。

## 模块划分说明

### 引导与启动链路
- 负责在 QEMU/SBI 环境下完成从固件到内核 C 入口的最小化初始化；重点文件包括 `src/boot/entry.S`（设置栈、跳转 C）、`src/boot/main.c`（加载内核与页表建立）、`src/boot/start.c` 与 `src/boot/initcode.S`（用户态 init 程序）。`src/bootloader/` 保存用于启动的 OpenSBI/RustSBI 固件镜像。
- 链接脚本 `src/linker/kernel.ld` 与 `user/user*.ld` 描述内核与用户态的段布局，影响符号分布与内存映射。

### 内存管理
- `src/mm/kalloc.c` 提供基于空闲链表的页分配器；`src/mm/vm.c` 实现多级页表的创建、映射与复制，配合 `src/memlayout.h` 定义内核/用户虚拟地址布局。
- `src/riscv.h` 提供 CSR 与页表宏，`src/param.h` 规定系统全局参数（页数、进程数、文件数等），共同决定内存资源的边界。

### 进程、调度与上下文切换
- `src/proc/proc.c`/`proc.h` 管理进程结构体、调度器与状态转换，`src/proc/swtch.S` 负责保存/恢复寄存器上下文。
- `src/proc/exec.c` 依据 ELF 头（`src/proc/elf.h`）加载可执行文件到用户空间；进程创建/退出与调度与内存管理、同步模块协同。

### 中断、异常与系统调用
- `src/trap/trampoline.S` 和 `src/trap/kernelvec.S` 提供 S 态陷阱入口与 trampoline 跳板，`src/trap/trap.c` 处理中断/异常分发。
- `src/syscall/syscall.c` 负责系统调用分发表，`sysproc.c`、`sysfile.c`、`systime.c` 等实现具体接口；`src/lib/sbi.c` 封装 SBI 调用用于时钟/串口等低级服务。

### 文件系统与存储
- `src/fs/` 下包含块缓存与日志（`bio.c`、`log.c`）、VFS 核心（`fs.c`、`file.c`、`file.h`）、设备特殊文件（`pipe.c`），以及 FAT32 实现（`fat32.c`、`fat32.h`、`types-fat32.h`）。
- `src/mkfs/mkfs.c` 提供文件系统镜像制作工具；`src/fcntl.h`、`src/fs/stat.h` 定义文件操作标志与元数据结构。

### 设备与驱动抽象
- `src/devs/` 包含平台相关的中断控制器（`plic.c`）、串口 UART（`uart.c`）与控制台（`console.c`）驱动，负责最基础 I/O。
- `src/virtIO/virtio_disk.c` 与 `virtio.h` 实现 VirtIO 磁盘驱动，作为块设备抽象供文件系统使用。

### 同步与锁
- `src/sync/spinlock.{c,h}` 提供裸自旋锁，封装中断开关保障内核临界区；`src/sync/sleeplock.{c,h}` 适用于需要睡眠等待的场景（如磁盘 I/O）。
- 同步原语被广泛用于进程表、文件系统缓存等共享数据结构，是并发安全的基础。

### 用户态与运行库
- `user/` 下包含内置用户程序（`sh.c`, `ls.c`, `cat.c` 等）、启动代码（`entry.S`）、系统调用封装（`usys.S`/`usys.pl`）与用户态 libc 片段（`printf.c`, `ulib.c`, `umalloc.c`）。
- `src/lib/` 中的通用实现（`string.c`, `printf.c` 等）在内核与用户态复用；`user/initcode.*` 定义最初启动的用户进程。

### 构建、链接与运行辅助
- `Makefile` 定义内核与用户程序的编译、链接、镜像生成流程，`run.sh` 提供在 QEMU 下的运行脚本。
- 构建产物（如 `kernel/kernel`、`kernel/kernel.asm`、`kernel/kernel.sym`、`user/*.o`）位于仓库根目录与子目录中，便于调试和反汇编。