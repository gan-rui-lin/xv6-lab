# RUOS 答疑索引

本目录记录对 RUOS 内核各模块的深入问答，面向教学场景。每章结构：

1. **核心概念**：该主题的基础 OS 概念和关键术语
2. **基本推论**：从概念推导出的设计原则和约束
3. **Linux / 通用实现**：Linux 或一般操作系统怎么做的
4. **本系统实现**：RUOS 怎么做的、优缺点、开发者为什么这么想
5. **八股 / 面试高频问题**：该章节对应的常见面试题和关键词
6. **项目贡献亮点**：可在面试中展示的具体技术贡献

---

## 模块拆解总览

RUOS 基于 xv6 改造，面向 RISC-V (Sv39)，整体分为 **OS 核心模块** 和 **开发基础设施模块** 两大类。

---

### 一、OS 核心模块

| # | 模块 | 核心文件 | 一句话概括 |
|---|------|----------|-----------|
| 1 | [启动与初始化](01-启动与初始化.md) | `entry.S`, `main.c`, `kernel.ld` | OpenSBI 交接 → hart0 全局初始化 → 其他 hart 等信号加入 |
| 2 | [进程管理](02-进程管理.md) | `proc.c`, `mlfq.c`, `signal.c`, `swtch.S` | 64 个 proc 槽、MLFQ 四级调度、clone 线程、64 路信号 |
| 3 | [内存管理](03-内存管理.md) | `kalloc.c`, `vm.c`, `vma.c` | buddy+slab、Sv39 三级页表、VMA 惰性映射、COW+零页 |
| 4 | [文件系统](04-文件系统.md) | `fs.c`, `fat32.c`, `ext4fs.c`, `file.c` | 三层 VFS 自动探测、FAT32 LFN、lwext4 嵌入 |
| 5 | [中断与异常](05-中断与异常.md) | `trap.c`, `trampoline.S`, `kernelvec.S` | trampoline 跳板页、Page Fault 分层处理链 |
| 6 | [系统调用](06-系统调用.md) | `syscall.c`, `sysproc.c`, `sysfile.c` | Linux 兼容号、a7 分发、五大类 syscall |
| 7 | [同步原语](07-同步原语.md) | `spinlock.c`, `sleeplock.c` | spinlock 关中断+TAS、sleeplock sleep/wakeup |
| 8 | [进程间通信](08-进程间通信.md) | `pipe.c`, `msgqueue.h`, `eventfd.c`, `shm.h` | pipe/msgqueue/shm/eventfd/signal 五种机制 |

### 二、开发基础设施模块

| # | 模块 | 核心文件 | 一句话概括 |
|---|------|----------|-----------|
| 9 | [硬件驱动](09-硬件驱动.md) | `uart.c`, `plic.c`, `virtio_disk.c` | MMIO、PLIC 中断、VirtIO 块设备和网卡 |
| 10 | [SBI 固件](10-SBI固件.md) | `entry.S`, OpenSBI 接口 | ecall 调 OpenSBI、M/S 态中断委托 |
| 11 | [网络协议栈](11-网络协议栈.md) | `net.c`, `open-npstack/` | onps TCP/IP 栈、virtio-net、BSD socket API |
| 12 | [用户态](12-用户态.md) | `initcode.c`, `ulib.c`, `umalloc.c` | syscall 封装、K&R malloc、ELF/动态链接 |
| 13 | [构建工具链](13-构建工具链.md) | `Makefile`, `kernel.ld` | 交叉编译、链接脚本、QEMU 调试 |
| 14 | [测评系统](14-测评系统.md) | `initcode.c`, 决赛题目 | 31 项基础测试全过、决赛 3+1 题 |

### 三、求职与调试

| # | 文档 | 内容 |
|---|------|------|
| 15 | [岗位匹配与面试准备](15-岗位匹配与面试准备.md) | 技术栈匹配度、知识缺口、面试话术、简历要点 |
| 16 | [调试实战](16-调试实战.md) | 6 个真实调试案例：内核栈溢出、BusyBox exec、FAT32 目录满、ext4 容量、动态链接、sleep 睡死 |

---

## 快速导航

- 想了解**整体架构** → 本文件 + [docs/OS_SOURCE_GUIDE.md](../docs/OS_SOURCE_GUIDE.md)
- 想了解**某个 syscall** → [06-系统调用.md](06-系统调用.md)
- 想了解**比赛题目** → [14-测评系统.md](14-测评系统.md)
- 想了解**调试经验** → [16-调试实战.md](16-调试实战.md)
- 想查**八股/面试题** → 每章第五节"八股 / 面试高频问题"
- 想看**项目亮点** → 每章第六节"项目贡献亮点" + [15-岗位匹配.md](15-岗位匹配与面试准备.md)
