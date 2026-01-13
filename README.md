# RUOS 项目说明

本项目为教学与竞赛场景下的轻量内核实现，基于 xv6 改造并面向 RISC-V 平台。在保持简洁内核设计的同时，增强了系统调用接口、文件系统（FAT32）支持、进程调度与内存管理等能力，并对 BusyBox 进行了初步适配。

本项目的所有文档均位于 [docs/](docs/) 目录下，包含设计说明、调试记录与适配指南等内容。 README 的末尾提供了文档索引以便查阅。


## 项目特性

- SBI 启动：在 QEMU virt 上通过 OpenSBI 接口完成关机、定时器等基础平台服务（`sbi_shutdown`、`sbi_set_timer`）。
- 系统调用：用户态以 Linux 号为主的轻量封装，移除了大量传统 xv6 式调用；用户侧统一在 `user/ulib.c` 中实现，支持 `fork/execve/wait/getpid/open/read/write/mmap` 等常用调用。
- FAT32 文件系统：
	- 支持根目录与子目录的路径解析（SFN/8.3 与 LFN 长文件名拼接）。
	- 支持读取与基本写入，提供 `getdents64` 风格目录枚举与 `unlinkat` 删除。
	- 与 xv6fs 通过 `fat32_mode` 开关融合，使用 `ip->major == FAT32_INODE_TAG` 标识 FAT32 inode，读写路径由 `readi/writei/namei/createat` 分发到 FAT32 实现。
	- 详细适配说明见 [docs/grlDocs/xv6_fat32_适配说明.md](docs/grlDocs/xv6_fat32_适配说明.md)。
- 进程调度：借鉴 Pintos 的多级反馈队列（MLFQ）思想，支持按照交互/计算型负载进行动态优先级调整（实现细节位于调度与 `proc` 模块）。
- 内存管理：引入 buddy + slab 组合策略，兼顾大块分配与高频小对象分配的效率与碎片控制。
- BusyBox 初步适配：可运行基本 BusyBox 应用（如 `busybox echo`），便于在早期阶段验证系统调用与文件系统栈。

## 快速运行

### 构建与启动（RISC-V）

```bash
make all      # 完整构建
make run      # 启动 QEMU（RISC-V）
make debug    # 启动 QEMU 并且输出 LOG 信息
```

调试时在另一个终端执行：

```bash
gdb-multiarch -x debug_riscv.gdb
```

## 工具链建议

- 交叉编译器：`riscv64-unknown-elf-*` 或 `riscv64-linux-gnu-*`
- QEMU：需支持 `riscv64`，推荐与测评的版本一致(7.0.0)。

## 目录结构

- docs/：项目文档与调试记录
- kernel/：内核镜像与反汇编输出
- src/：核心源码
	- boot/：启动与入口
	- devs/：设备与驱动
	- fs/：文件系统（xv6fs 与 FAT32 融合层在 `fs.c`/`fat32.c`）
	- mm/：内存管理（buddy/slab）
	- proc/：进程与调度（包含 MLFQ 改造）
	- syscall/：系统调用分发与实现
	- trap/：异常与中断
	- virtIO/：虚拟设备支持
- user/：用户态程序与启动 `initcode`

## 测试通过情况

本项目通过了**所有的测试点**

| 测试样例名         | 通过测试点 | 全部测试点 |
| :----------------- | :--------: | :--------: |
| test_execve        |     3      |     3      |
| test_open          |     3      |     3      |
| test_getdents      |     5      |     5      |
| test_gettimeofday  |     3      |     3      |
| test_munmap        |     4      |     4      |
| test_yield         |     4      |     4      |
| test_getpid        |     3      |     3      |
| test_mount         |     5      |     5      |
| test_dup           |     2      |     2      |
| test_waitpid       |     4      |     4      |
| test_write         |     2      |     2      |
| test_close         |     2      |     2      |
| test_exit          |     2      |     2      |
| test_times         |     6      |     6      |
| test_read          |     3      |     3      |
| test_getppid       |     2      |     2      |
| test_clone         |     4      |     4      |
| test_openat        |     4      |     4      |
| test_mmap          |     3      |     3      |
| test_fork          |     3      |     3      |
| test_sleep         |     2      |     2      |
| test_mkdir         |     3      |     3      |
| test_umount        |     5      |     5      |
| test_chdir         |     3      |     3      |
| test_unlink        |     2      |     2      |
| test_fstat         |     3      |     3      |
| test_pipe          |     4      |     4      |
| test_getcwd        |     2      |     2      |
| test_dup2          |     2      |     2      |
| test_brk           |     3      |     3      |
| test_uname         |     2      |     2      |
| test_wait          |     4      |     4      |

## 文档索引

### RUOS 源码导读与系统设计

见 [docs/OS_SOURCE_GUIDE.md](docs/OS_SOURCE_GUIDE.md)，它描述了 RUOS 各个板块的详细设计。

### 核心适配与实现

| 文档 | 主题 | 备注 |
| - | - | - |
| [docs/grlDocs/xv6_fat32_适配说明.md](docs/grlDocs/xv6_fat32_适配说明.md) | FAT32 适配 | 设计与集成点、读写与路径解析 |
| [docs/grlDocs/移除xv6系统调用_类型定义修复.md](docs/grlDocs/移除xv6系统调用_类型定义修复.md) | 系统调用收敛与类型 | 移除 xv6 式 syscall、POSIX 类型补齐 |
| [docs/grlDocs/sbi 调查报告.md](docs/grlDocs/sbi%20调查报告.md) | SBI 启动/关机/定时器 | 链接失败与调用约定排查 |
| [docs/grlDocs/busybox_exec调试记录.md](docs/grlDocs/busybox_exec调试记录.md) | BusyBox 适配 | exec/路径/文件操作联调 |
| [docs/grlDocs/mount_umount_exec_debug.md](docs/grlDocs/mount_umount_exec_debug.md) | 挂载/卸载/exec 调试 | 流程与问题定位 |
| [docs/grlDocs/dir_remove_unlink_debug.md](docs/grlDocs/dir_remove_unlink_debug.md) | 目录删除/unlink | FAT32 删除实现与调试 |
| [docs/grlDocs/sleep 睡死调试.md](docs/grlDocs/sleep%20睡死调试.md) | sleep 调度问题 | tick/等待/唤醒分析 |
| [docs/grlDocs/链接器找不到 sbi_xxx.md](docs/grlDocs/链接器找不到%20sbi_xxx.md) | 链接错误 | inline 导致缺符号的修复 |
| [docs/grlDocs/TODO.md](docs/grlDocs/TODO.md) | 待办事项 | 后续工作清单 |

### 机制与测试

| 文档 | 主题 | 备注 |
| - | - | - |
| [docs/HWTDocs/调度机制.md](docs/HWTDocs/调度机制.md) | 调度机制 | 总览与策略说明 |
| [docs/HWTDocs/内存管理测试.md](docs/HWTDocs/内存管理测试.md) | 内存管理测试 | buddy/slab 相关测试 |
| [docs/HWTDocs/dup2.md](docs/HWTDocs/dup2.md) | dup2 | 接口与实现要点 |
| [docs/HWTDocs/进程测试.md](docs/HWTDocs/进程测试.md) | 进程测试 | 用例与结论 |
| [docs/HWTDocs/23年官方文档.md](docs/HWTDocs/23年官方文档.md) | 参考资料 | 历史资料汇编 |


### 其他文档

| 文档 | 主题 | 备注 |
| - | - | - |
| [docs/ZJYDocs/readme.md](docs/ZJYDocs/readme.md) | 说明 | 子目录概览 |
| [docs/ZJYDocs/问题记录.md](docs/ZJYDocs/问题记录.md) | 问题记录 | 变更与问题跟踪 |

[docs/stages/阶段01.md](docs/stages/阶段01.md) ~ [docs/stages/阶段06.md](docs/stages/阶段06.md) 为实验阶段代码与说明，展示了项目开发的**前期过程**。
