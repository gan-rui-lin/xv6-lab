# xv6-lab 网络模块适配与集成说明（RV64）

本文档详细记录将 open-npstack（简称 onps）协议栈集成到 xv6-lab（RISC‑V RV64）内核的完整过程、关键技术方案、代码改动清单、构建与运行方法、测试与调试要点，以及当前适配状态与后续工作计划。文档覆盖从网卡驱动（virtio-net）到协议栈（ARP/IP/UDP/TCP 的最小实现与接口）、到系统调用与用户态编程接口，再到 QEMU 用户态网络的贯通，力求为维护者与后续开发提供完整的工程视图与参考。

---

## 1. 背景与目标

- 目标平台：xv6-lab（RISC‑V rv64，QEMU virt 机器）。
- 参考仓库：`xv6-networking-stack`（用于参考整体组织方式与基础网络流程）。
- 协议栈选择：`open-npstack`（onps），通过其 OS 适配层在 xv6-lab 内核中运行。
- 网络目标：
  - 网卡驱动：virtio-net（MMIO）设备初始化、发送/接收、IRQ 中断处理。
  - 协议栈：集成 ARP/IP/UDP，保留 TCP 的最小接口以便后续扩展，关闭不需要的模块与工具。
  - 用户接口：提供 socket 族系统调用（`socket/bind/connect/sendto/recvfrom/listen/accept` 的最小可用集，重点验证 UDP）。
  - QEMU 用户态网络打通：来宾 IP 固定为 10.0.2.15/24，经 user-mode 网络访问宿主 10.0.2.2（例如 UDP echo 服务）。

---

## 2. 总体架构与关键思路

- 设备驱动与协议栈解耦：将 virtio-net 驱动实现为纯内核模块，通过“网卡抽象接口”将收到/发送的以太帧交给 onps；onps 通过其 `ethernet_*` 接口收包与发包。
- OS 适配层（onps port/os_adapter）：将 onps 所需的线程/时间/同步等原语映射到 xv6-lab 的自旋锁、睡眠/唤醒与我们添加的内核线程机制（kthread）。
- 内核线程：为 onps 的后台任务（例如以太接收循环）提供运行上下文；在不引入复杂调度更改的前提下，以进程表中标记 `is_kthread` 的方式实现。
- 系统调用接口：在 `syscall/sysnet.c` 中提供 BSD 风格 socket API 的最小包装，使用 onps 的 `bsd/socket.h` 接口完成底层功能；在 `fs/file.*` 中新增 `FD_SOCKET` 类型，将 socket 作为一种文件描述符进行管理。
- 构建与裁剪：使用 onps 的 `sys_config.h` 关闭 IPv6/PPP/NETTOOLS 等非必要模块，减少内核耦合与编译需求；在内核 `Makefile` 中过滤未启用的子目录源文件，保持二进制大小与编译稳定。
- QEMU 网络：使用 `-netdev user` + `-device virtio-net-device` 组合，使来宾直接以用户态网络访问宿主；驱动 MMIO 地址与 IRQ 绑定在 `memlayout.h` 中。

---

## 3. 代码结构与主要文件

- virtio-net 驱动：`src/virtIO/virtio_net.c`、`src/virtIO/virtio_net.h`（新增），`src/virtIO/virtio.h`（补充常量）。
- 网络桥接：`src/network/net.c`、`src/network/net.h`（新增）：负责 onps 与驱动的胶合、静态地址配置与启动接收线程。
- onps 适配层与协议栈：位于 `src/network/open-npstack/`，其中 `port/include/port/*.h`、`port/os_adapter.c` 我们进行了针对 xv6 的适配。
- 内核线程：`src/proc/kthread.c`（新增）、`src/proc/proc.h`/`src/proc/proc.c`（补充字段与创建/退出流程）。
- 中断与平台：`src/trap/trap.c`（在 `devintr` 中接收 virtio-net IRQ）、`src/devs/plic.c`（使能 IRQ）、`src/memlayout.h`（定义 MMIO 地址与栈常量）。
- 文件系统与 FD：`src/fs/file.h`、`src/fs/file.c`（新增 `FD_SOCKET` 与关闭/读写策略）。
- 系统调用接口：`src/syscall/sysnet.c`（UDP 主线 + TCP 最小化 listen/accept 包装）、`src/syscall/syscall.c`（注册条目）。
- 用户态测试：`user/nettests.c`（最初测试程序，后续也在 `user/initcode.c` 中嵌入 `socket_test()` 以开机验证）。
- 构建：`Makefile`（onps 相关 include 与编译过滤）、`run.sh`（QEMU 启动选项与调试）。

---

## 4. 设备驱动与中断路径

### 4.1 MMIO 与 IRQ 布局

- 在 `src/memlayout.h` 中增加：
  - `#define VIRTIO1 0x10002000`（virtio-net MMIO 基址）
  - `#define VIRTIO1_IRQ 2`（映射的 PLIC 中断号）。
- 在 `src/devs/plic.c` 中：使能并设置 `VIRTIO1_IRQ` 的优先级与 S 态中断使能。
- 在 `src/trap/trap.c` 的 `devintr()` 中：判断 `plic_claim` 返回的中断号是否为 `VIRTIO1_IRQ`，若是则调用 `virtio_net_intr()` 完成接收与发送完成处理。

### 4.2 virtio-net 驱动实现要点

- 初始化流程：
  - 探测 MMIO 配置区、确认设备特性（MAC、状态），建立 RX/TX virtqueue，分配合适数量的描述符与缓冲区；将 RX 缓冲挂入队列待设备填充。
  - 将驱动层的 `net_rx_deliver()` 注册为收到以太帧时的上送回调（由 net 层承接）。
- 发送流程：
  - `virtio_net_transmit()` 将待发送的以太帧打包到 TX 队列，kick 设备；完成中断时回收并释放对应的缓冲。
- 接收流程：
  - 中断触发后从 RX 队列取出填充完成的缓冲，将帧数据上送至 `net_rx_deliver()`（由 net 层调用 onps 的 `ethernet_put_packet()`）。
- 内存管理：
  - 临时报文在发送路径上由 `kmalloc()` 分配，发送完成后在 TX 完成中断路径中释放，避免泄漏。

---

## 5. 网络桥接（net 层）

- 文件：`src/network/net.c` 与 `src/network/net.h`（新增）。
- 主要职责：
  - 在 `net_init()` 中加载 onps（`open_npstack_load()`），初始化 virtio-net 驱动，并将其注册为 Ethernet 设备。
  - 配置静态 IPv4：来宾地址 `10.0.2.15`，子网掩码 `255.255.255.0`，网关 `10.0.2.2`；同时为 DNS 设置预设值（可选）。
  - 启动 onps 的以太接收后台线程：调用 `kthread_create(thread_ethernet_ii_recv, ...)` 进入接收循环。
- 报文路径：
  - 发送：`net_emac_send()` 将 onps 的 buf_list 合并为线性缓冲后调用 `virtio_net_transmit()`。
  - 接收：`net_rx_deliver()` 接收驱动上送的以太帧，调用 onps 的 `ethernet_put_packet()` 完成协议栈处理。

---

## 6. onps OS 适配层

### 6.1 适配原则

- 不引入标准 C 库；在内核态使用现有 `string.c` 等实现或内联轻量替代。
- 将 onps 的 `HMUTEX/HSEM` 等句柄映射为内核内部的索引或结构，并以“池化+一次性初始化”的方式管理，减少动态分配。
- 线程与临界区：
  - `os_thread_onpstack_start()` 通过 `kthread_create()` 启动 onps 线程。
  - `os_critical_enter/exit()` 映射为 `push_off/pop_off` 或持锁，保证中断安全区域。
- 时间与休眠：
  - `os_sleep_secs/ms` 通过系统 ticks 驱动的 `sleep/wakeup` 实现；`os_get_system_secs/ms` 通过 ticks 计算。

### 6.2 关键文件改动

- `src/network/open-npstack/port/include/port/datatype.h`
  - 在 `ONPS_KERNEL` 分支下引入 `types.h`/`defs.h`，并为缺失的 `pow()` 提供整数版本 `onps_ipow()`；声明 `rand/srand` 原型，并在 `src/lib/rand.c`（新增）提供简易 LCG 实现。
- `src/network/open-npstack/port/include/port/os_datatype.h`
  - 定义 `HMUTEX/HSEM/BOOL`、`in_addr_t` 与 `struct in_addr`，保证在内核环境下类型可用；`HTTY` 仅在 `SUPPORT_PPP` 为 1 时存在（当前关闭）。
- `src/network/open-npstack/port/include/port/sys_config.h`
  - 关闭不需要模块：`SUPPORT_IPV6=0`、`SUPPORT_PPP=0`、`NETTOOLS_* = 0`；保留 `SUPPORT_ETHERNET=1`，`UDP_LINK_NUM_MAX` 等资源参数按最小可用配置设置。
- `src/network/open-npstack/port/os_adapter.c`
  - 实现 onps 所需的 OS 适配函数（休眠、时间、互斥、信号量、临界区、线程启动等），内部依赖 xv6 现有锁与 sleep/wakeup 机制。

---

## 7. 内核线程（kthread）

- 文件：`src/proc/kthread.c`（新增）、`src/proc/proc.h`（新增字段）、`src/proc/proc.c`（清理/初始化）。
- 核心思路：
  - 在 `struct proc` 中新增 `is_kthread/kthread_fn/kthread_arg` 字段；创建时设置 `parent=0`，并标记 `RUNNABLE`。
  - 启动：分配进程条目，设置 `context.ra = kthread_trampoline`，`context.sp = p->kernel_stack + KSTACK_SIZE`（因此在 `kthread.c` 中补充 `#include "memlayout.h"`，以获取 `KSTACK_SIZE`）。
  - 结束：当线程函数返回或显式调用 `kthread_exit()` 时进入 `ZOMBIE`，由调度器统一回收。

---

## 8. 系统调用与 FD 集成

### 8.1 文件描述符类型

- 文件：`src/fs/file.h`/`src/fs/file.c`
  - 新增 `FD_SOCKET` 类型，并在 `fileclose()` 中识别此类型调用 onps 的 `close()` 释放 socket。
  - `fileread/filewrite` 对 `FD_SOCKET` 返回不支持（统一通过 socket API 收发）。

### 8.2 系统调用包装

- 文件：`src/syscall/sysnet.c`
  - `sys_socket()`：分配 `struct file`，调用 onps `socket()` 获取 `SOCKET` 句柄，并将 `f->sock` 记录到 FD。
  - `sys_bind()`/`sys_connect()`：从用户态参数解析 IP 字符串与端口，调用 onps 接口；错误统一按 `-E*` 返回。
  - `sys_sendto()`：将用户缓冲拷入内核（`kmalloc + copyin`），调用 onps `sendto()`；返回发送字节数或错误码。
  - `sys_recvfrom()`：在内核分配接收缓冲，调用 onps `recvfrom()`；拷出到用户缓冲，并将源地址/端口写回用户指针参数。
  - `sys_listen()`/`sys_accept()`：为 TCP 服务器提供最小包装（接口声明在 onps `bsd/socket.h` 中，受 `SUPPORT_ETHERNET` 开关保护）。
  - 内部辅助：`getsockfd()` 校验 fd 并提取 `struct file*`；统一错误处理与返回值约定。
- 文件：`src/syscall/syscall.c`
  - 注册上述系统调用编号与名称映射（`SYS_socket/SYS_bind/SYS_connect/SYS_sendto/SYS_recvfrom/SYS_listen/SYS_accept`）。

---

## 9. onps 头文件依赖修复与轻量替代

为保证在内核环境下编译通过且不依赖 libc，我们对 onps 的部分头文件与工具函数进行了修正：

- `src/network/open-npstack/include/onps_input.h`
  - 增加 `#include "port/datatype.h"`、`#include "port/os_datatype.h"`、`#include "onps_errors.h"`，确保 `INT/USHORT/BOOL/in_addr_t/EN_ONPSERR` 等类型在任何包含次序下均可解析。
- `src/network/open-npstack/include/bsd/socket.h`
  - 引入 `#include "port/sys_config.h"`，以便访问 `SUPPORT_ETHERNET` 宏，确保 `listen()/accept()` 的声明被正确编译；同时包含 `onps_input.h`、`onps_utils.h` 以满足依赖。
- `src/network/open-npstack/onps_utils.c`
  - 移除对 `sprintf/strcat/atoi` 的依赖，改为内联轻量实现：
    - `onps_atoi()`：数字字符串转整型（支持负号）。
    - `snprintf_hex()`：以内部十六进制字符生成逻辑替代 `sprintf/strcat`，控制目标缓冲写入边界。
    - `inet_ntoa/_ext/_safe/_safe_ext`：统一通过 `onps_ipv4_to_str()` 生成 IP 字符串，避免 stdio 依赖。
- `src/network/open-npstack/ip/icmp.c`
  - 修复 `onps_input_recv()` 参数类型：将 `NULL` 的源 IP 实参显式改为 `(in_addr_t)0`，满足签名要求，消除编译器警告。

---

## 10. 构建系统与过滤（Makefile）

- 在 `Makefile` 中：
  - 增加 onps 的 include 路径：`-I$(SRC)/network/open-npstack/include` 与 `-I$(SRC)/network/open-npstack/port/include`。
  - 定义编译宏：`-DONPS_KERNEL`，让 onps 走内核分支实现。
  - 过滤未启用的源目录：
    - `SRCS := $(filter-out $(SRC)/network/open-npstack/ppp/%,$(SRCS))`
    - `SRCS := $(filter-out $(SRC)/network/open-npstack/net_tools/%,$(SRCS))`
    - `SRCS := $(filter-out $(SRC)/network/open-npstack/port/telnet/%,$(SRCS))`
  - 保持 QEMU 网络设备参数与 user-mode 网络一致：`-netdev user,id=net0` + `-device virtio-net-device,netdev=net0`（注：在 `run.sh` 中也提供了另一组参数，见下一节）。

---

## 11. QEMU 启动与用户态网络

- `Makefile` 的默认 QEMU 选项（内核开发路径）采用：
  - `-netdev user,id=net0`
  - `-device virtio-net-device,netdev=net0`
- `run.sh`（用于镜像 + 引导另一二进制名 `kernel-qemu`）当前片段使用：
  - `-device virtio-net-device,netdev=net,bus=virtio-mmio-bus.1`
  - `-netdev user,id=net`
- 两处配置的 `netdev id` 不同（`net0` vs `net`），但均为 user-mode 网络；请保持与内核 virtio-net MMIO 总线编号一致（virtio-mmio-bus.1），避免冲突。建议统一为 `net0`，以减少歧义。

---

## 12. 用户态接口与测试

### 12.1 用户原型导出与封装

- 在 `user/user.h` 与 `user/ulib.c` 中导出对应的用户态 syscall 封装，使用户程序可直接调用：
  - `int socket(int domain, int type, int protocol);`
  - `int bind(int fd, const char *ip, int port);`
  - `int connect(int fd, const char *ip, int port);`
  - `int sendto(int fd, const void *buf, int len, const char *dest_ip, int dest_port);`
  - `int recvfrom(int fd, void *buf, int len, uint32 *from_ip, uint16 *from_port);`
  - 以及 `listen/accept` 的最小接口（面向 TCP）。

### 12.2 初始化程序测试（`user/initcode.c`）

- 在 `main()` 中加入 `socket_test()`，用于开机即验证 UDP：
  - 创建 UDP socket（`AF_INET=2, SOCK_DGRAM=2`）。
  - 发送到宿主 `10.0.2.2:12345`，接收回显并打印。
  - 出错时打印提示并退出。
- 文件片段（为清晰起见，略）：
  - 打开/创建设备 `console`，`dup(0)` 建立 stdout/stderr，随后执行 `socket_test()`。

### 12.3 独立测试程序（`user/nettests.c`）

- 用于更系统化的网络路径验证（发送/接收、错误回退），可配宿主测试脚本（例如 `nc -u -l 12345`）。

---

## 13. 当前适配状态与编译警告处理

### 13.1 已完成

- virtio-net 驱动（初始化、收发、IRQ），中断路径贯通。
- onps OS 适配（时间、睡眠、线程、互斥、信号量、临界区）。
- net 层胶合（onps/Ethernet ↔ 驱动），静态 IPv4 配置与后台接收线程。
- socket 系统调用最小集（UDP 主线），FD_SOCKET 集成。
- QEMU user-mode 网络贯通（来宾 10.0.2.15 ↔ 宿主 10.0.2.2）。
- 构建系统修复与 onps 依赖轻量化（去除 stdio 与部分工具目录）。

### 13.2 已修复的编译错误/警告

- `onps_input.h` 类型缺失：补齐头文件依赖，解决 `INT/USHORT/BOOL/in_addr_t/EN_ONPSERR` 缺失。
- `bsd/socket.h`：通过包含 `port/sys_config.h` 保证 `SUPPORT_ETHERNET` 宏可见，`listen/accept` 正确声明。
- `kthread.c`：缺少 `KSTACK_SIZE`，补充 `#include "memlayout.h"`。
- `onps_utils.c`：消除 `sprintf/strcat/atoi` 隐式声明与 libc 依赖，改为内联实现。矫正 `printf_hex` 的缩进告警（不影响功能）。
- `icmp.c`：修正 `onps_input_recv` 的参数类型，消除 void* → in_addr_t 的告警。
- `Makefile`：过滤未启用的 PPP/telnet/net_tools 源目录，避免编译器报 `HTTY` 类型未定义等错误。

### 13.3 仍可能出现的非致命警告

- packed 结构体转 `USHORT*` 计算校验的对齐警告：属于 onps 既有实现风格，功能正确但编译器警告；若需彻底消除，可在本地复制计算字段避免取地址转换。
- `char` 下标与 `switch` 未覆盖枚举项警告：不影响当前最小功能，可在后续清理阶段做细化处理。

---

## 14. 运行与验证

### 14.1 构建与运行（Makefile 流程）

使用内核自带规则（建议）：

```bash
make debug
make all
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 2 -nographic \
  -drive file=fs.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  -netdev user,id=net0 \
  -device virtio-net-device,netdev=net0
```

### 14.2 使用 run.sh（镜像引导）

`run.sh` 针对 `sdcard.img/sdcard-final.img` 等镜像：

```bash
./run.sh -t debug -f sdcard-final.img
# 或
./run.sh -t all -f sdcard.img
```

脚本中当前采用：

```bash
-device virtio-net-device,netdev=net,bus=virtio-mmio-bus.1 
-netdev user,id=net
```

建议与 Makefile 统一为 `net0`，并确认 bus 索引与内核 MMIO 地址匹配（virtio-mmio-bus.1 对应 VIRTIO1）。

### 14.3 宿主 UDP echo 测试

在宿主机执行：

```bash
# 开启一个简单的 UDP Echo（以 busybox 为例）
busybox nc -u -l -p 12345 -s 10.0.2.2
```

在来宾（xv6-lab）启动后，`initcode` 会运行 `socket_test()`，输出收发结果。也可在 shell 中运行 `_nettests` 做更丰富的测试。

---

## 15. 变更清单（未提交的代码调整与修复）

以下为本次适配过程中对仓库进行的关键代码变更（含未提交的修复），按模块归纳：

- 头文件与类型修复：
  - `src/network/open-npstack/include/onps_input.h`：引入 `port/datatype.h`、`port/os_datatype.h`、`onps_errors.h`，保证类型可见性。
  - `src/network/open-npstack/include/bsd/socket.h`：引入 `port/sys_config.h`、`onps_input.h`、`onps_utils.h`，保证宏/类型依赖；`listen/accept` 在 `SUPPORT_ETHERNET` 下声明。
- OS 适配与工具函数：
  - `src/network/open-npstack/onps_utils.c`：移除 `sprintf/strcat/atoi` 依赖，加入 `onps_atoi()/onps_ipv4_to_str()` 等内联实现；修正 `printf_hex` 缩进提示。
  - `src/network/open-npstack/port/include/port/datatype.h`：`ONPS_KERNEL` 分支下的最小实现（`pow` 替换，`rand/srand` 原型）。
- 内核线程：
  - `src/proc/kthread.c`：补充 `#include "memlayout.h"`，修复 `KSTACK_SIZE` 未定义错误；实现 `kthread_trampoline/kthread_create/kthread_exit`。
  - `src/proc/proc.h`：增加 `is_kthread/kthread_fn/kthread_arg` 字段；保留现有调度字段不变。
  - `src/proc/proc.c`：在分配/回收流程中清理新增字段。
- 中断与平台：
  - `src/memlayout.h`：定义 `VIRTIO1/VIRTIO1_IRQ` 与 `KSTACK_SIZE` 常量；保证内核栈计算与总线编号一致。
  - `src/trap/trap.c`：在 `devintr()` 中增加 `VIRTIO1_IRQ → virtio_net_intr` 分发。
  - `src/devs/plic.c`：使能 `VIRTIO1_IRQ` 的 S 态中断与优先级设置。
- 驱动与网络桥接：
  - `src/virtIO/virtio_net.c / .h`（新增）：virtio-net 驱动，完成 RX/TX 队列与中断处理。
  - `src/network/net.c / .h`（新增）：onps ↔ 驱动胶合、静态 IPv4、接收线程启动、发送路径整合。
- 协议栈集成：
  - `src/network/open-npstack/include/onps.h`：确保包含顺序与裁剪宏正确；`open_npstack_load/unload` 在 `net_init()` 中调用。
  - `src/network/open-npstack/ip/icmp.c`：修复 `onps_input_recv` 调用参数类型。
- 文件描述符与系统调用：
  - `src/fs/file.h / file.c`：加入 `FD_SOCKET`、在 `fileclose()` 中调用 onps `close()`；`fileread/filewrite` 对 socket 返回不支持。
  - `src/syscall/sysnet.c`：实现 `socket/bind/connect/sendto/recvfrom/listen/accept`；统一缓冲拷贝与错误码规范、fd → `struct file*` 映射。
  - `src/syscall/syscall.c`：注册上述 syscalls；`sysname` 增加对应名称。
- 构建与运行：
  - `Makefile`：加入 onps include 路径与 `-DONPS_KERNEL`；过滤未启用目录（PPP/telnet/net_tools）；QEMU user-mode 网络参数。
  - `run.sh`：提供镜像引导与可选 GDB；网络参数与 MMIO 总线编号需统一校对（建议统一 `net0`）。
- 用户态测试：
  - `user/initcode.c`：加入 `socket_test()`，开机触发 UDP 测试；保留基础测试项以回归其他 syscalls。
  - `user/nettests.c`：独立测试程序（构建为 `_nettests`）。

---

## 16. 风险与待办

- TCP 完整性：当前仅提供 `listen/accept` 的最小封装，尚未进行端到端 TCP 收发验证；后续需完善 `send/recv/is_tcp_connected` 等路径与缓冲策略。
- 对齐与 packed 警告：onps 在校验和计算路径上使用 packed 结构体指针转换，可能触发对齐警告；建议后续以 memcpy 到对齐缓冲的方式消除警告。
- 资源配额：`UDP_LINK_NUM_MAX/SOCKET_NUM_MAX/TCP_LINK_NUM_MAX` 在 `sys_config.h` 的设置较小，若后续并发需求增加需同步扩大并评估内存。 
- DNS/DHCP：当前以静态 IP 为主；若需要 DHCP，则需开启相关模块并实现更多 OS 适配与事件循环。
- 统一 QEMU 参数：`Makefile` 与 `run.sh` 存在 netdev id 差异，建议统一，避免后续误用导致设备无法匹配。
- 安全与鲁棒：用户态输入字符串的解析与长度边界已在 syscall 层做基本保护，但仍建议在 onps 侧增加额外健壮性检查。

---

## 17. 后续计划（建议）

1. 扩展 TCP：
   - 完成 `connect/send/recv` 在 TCP 下的路径验证，增加简易 echo 客户端/服务器测试。
   - 打通 `tcpsrv_recv_poll/tcpsrv_set_recv_mode` 等服务器端工具接口，支持多连接轮询。
2. ICMP/PING：
   - 若需在来宾内直接 ping 宿主，开启 `NETTOOLS_PING` 并完善输出。
3. 性能与稳定性：
   - RX/TX 队列大小、缓冲分配批次与回收策略调优；增加统计接口与 `/proc` 风格信息输出（可通过 onps 的 nvt 命令适配）。
4. 构建一致性：
   - 统一 `Makefile` 与 `run.sh` 的网络设备参数；引入一个集中化的 `qemu.mk` 或 `qemu.conf` 管理。
5. 文档与示例：
   - 增加用户态示例（DNS、NTP 简易客户端）与完整教程。

---

## 18. 结语

本次适配在保持 xv6-lab 内核简洁性的前提下，将 open-npstack 的 IPv4/UDP 路径完整打通，并通过 virtio-net 驱动与 QEMU 的 user-mode 网络连接宿主，完成端到端的数据交互。我们重点关注了 OS 适配最小集、类型与头文件依赖的稳定性，以及对标准库的替代，保证编译环境纯内核化。下一阶段建议围绕 TCP 的完整通信与更丰富的网络工具进行扩展，同时持续优化资源与稳定性，以满足课程/竞赛与研究的需要。
