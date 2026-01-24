# OS 适配 ONPS 协议栈要点报告（RV64）

本文档以“已落地实现”为基准，系统总结 onps（open-npstack）在 RUOS（RISC‑V RV64）内核中的 OS 适配与网卡对接要点，覆盖 OS 适配层、网卡驱动与协议栈对接、系统调用与用户态接口、构建与运行、测试验证和现状结论。文档强调当前可复现的稳定路径，避免保留已过时或已解决的问题描述，便于维护者快速对照工程现状。

---

## 1. 目标与范围

本次适配的核心目标是让 RUOS 内核具备最小可用的网络能力，完成从虚拟网卡到协议栈再到用户态接口的全链路打通。范围包括：

- 在 QEMU virt 平台上使用 virtio-net（MMIO）设备，完成网卡初始化、发送、接收与中断处理。
- 集成 onps 协议栈，开启 Ethernet 与 IPv4 相关能力，最小化 UDP/TCP 接口，并关闭不需要的模块以降低内核负担。
- 提供 BSD 风格 socket 系统调用的最小可用集，支持 UDP 端到端数据收发。
- 保持内核无 libc 依赖，OS 适配层完全基于 xv6 内核原语实现。

---

## 2. 总体结构与数据路径概览

当前集成结构可分为三层：

- 设备驱动层：virtio-net 驱动负责与 QEMU 虚拟网卡交互，完成 RX/TX virtqueue 初始化、中断处理和数据搬运。
- 网络桥接层：net 模块负责将驱动与 onps 进行对接，承担网卡注册、静态 IPv4 配置、发送路径聚合与接收路径上送。
- 协议栈与系统调用层：onps 协议栈负责 IP/UDP/TCP 的处理逻辑，系统调用层提供 socket API 并与文件描述符体系融合。

数据发送路径：用户态 socket → sysnet → onps → net_emac_send → virtio_net_transmit。数据接收路径：virtio_net_intr → virtio_net_rx → net_rx_deliver → ethernet_put_packet → onps 处理线程。

---

## 3. OS 适配层（关键工作总结）

OS 适配层是 onps 能在内核态运行的基础。当前实现已满足 onps 对线程、时间、同步与临界区的全部需求，并与 xv6 的原生机制进行稳定映射。

### 3.1 数据类型与句柄定义

- 在 onps 的端口层中，定义线程互斥锁、信号量等句柄类型，确保 onps 使用的 HMUTEX/HSEM 与内核实现一致。
- 通过 onps 的 OS 数据类型文件完成结构化适配，保证 in_addr_t、struct in_addr 等类型在内核环境中可用。
- PPP 相关 tty 句柄在当前配置中关闭，不引入多余依赖。

相关实现集中在 onps 的 port/include/port 目录，重点文件为 [src/network/open-npstack/port/include/port/os_datatype.h](src/network/open-npstack/port/include/port/os_datatype.h) 与 [src/network/open-npstack/port/include/port/datatype.h](src/network/open-npstack/port/include/port/datatype.h)。

### 3.2 线程与定时任务

onps 内部存在 one-shot 定时器与以太网接收线程等后台任务。当前实现采用内核线程机制（kthread），确保协议栈线程与内核调度模型兼容：

- onps 启动时调用 os_thread_onpstack_start，由此创建 onps 内部工作线程。
- 以太网接收线程 thread_ethernet_ii_recv 通过 net 模块启动，接收链表与信号量配合实现“事件驱动式”处理。

相关实现参见 [src/network/open-npstack/port/os_adapter.c](src/network/open-npstack/port/os_adapter.c) 与 [src/proc/kthread.c](src/proc/kthread.c)。

### 3.3 时间、休眠与运行时计时

onps 依赖秒级、毫秒级休眠与系统运行时间统计，已映射到 xv6 的 ticks 机制：

- os_sleep_secs/os_sleep_ms：基于 sleep/wakeup 实现，保证调度公平性。
- os_get_system_secs/os_get_system_msecs：基于 ticks 计算，保持单调递增语义。

### 3.4 同步与临界区

- 互斥锁与信号量均通过内核同步原语实现，支持 onps 内部的并发访问与事件通知。
- 临界区保护使用关中断与恢复中断语义，确保协议栈对共享结构访问的原子性。

OS 适配层的函数实现均在 [src/network/open-npstack/port/os_adapter.c](src/network/open-npstack/port/os_adapter.c) 中完成，具备可追踪的错误码与可控的资源释放路径。

---

## 4. 网卡相关工作（virtio-net + onps 对接）

网卡移植已完整落地，覆盖初始化、发送、接收三块核心接口，并完成与 onps 的注册与收发对接。

### 4.1 网卡初始化与注册

- virtio-net 通过 MMIO 设备完成特性探测与 virtqueue 初始化，分配 RX/TX 描述符与缓冲区。
- net 模块负责启动 onps、初始化 virtio-net，并完成网卡注册。
- 以太网接口通过 ethernet_add 注册到 onps，注册时提供网卡名称、MAC、IPv4 配置、发送函数与接收线程启动函数。

关键代码位于 [src/virtIO/virtio_net.c](src/virtIO/virtio_net.c) 与 [src/network/net.c](src/network/net.c)。

### 4.2 发送路径

- onps 发送路径采用 buf_list 链表结构，net_emac_send 负责将链表合并为线性缓冲。
- 发送数据通过 virtio_net_transmit 投递到 TX 队列，发送完成后由中断路径回收缓冲。
- 发送失败场景有明确的错误码与日志路径，避免 silent drop。

### 4.3 接收路径

- virtio-net 中断触发后，驱动从 RX 队列取出数据并调用 net_rx_deliver。
- net_rx_deliver 使用 onps 的内存管理分配节点，将帧封装为链表节点后交给 ethernet_put_packet。
- onps 的以太网接收线程消费接收链表并进入协议栈处理流程。

### 4.4 中断路径与平台配置

- PLIC 已为 virtio-net IRQ 完成优先级与 S 态使能设置。
- trap 中断分发在 devintr 路径内调用 virtio_net_intr，统一处理 RX/TX。

相关平台配置见 [src/devs/plic.c](src/devs/plic.c)、[src/trap/trap.c](src/trap/trap.c) 与 [src/memlayout.h](src/memlayout.h)。

---

## 5. 网络桥接与协议栈启动

net 模块是 onps 与网卡驱动之间的核心胶合层，职责清晰且可复用：

- 在系统启动时调用 open_npstack_load 进行协议栈初始化。
- 设置静态 IPv4 地址为 10.0.2.15/24，网关 10.0.2.2，DNS 为常规预置值。
- 注册 Ethernet 网卡并启动 onps 内部接收线程。

核心实现在 [src/network/net.c](src/network/net.c)。系统入口在 [src/boot/main.c](src/boot/main.c)。

---

## 6. 系统调用与用户态接口

当前 socket 系统调用最小集已可用，能满足 UDP 的基本发送与接收需求：

- socket/bind/connect/sendto/recvfrom/listen/accept 已完成最小包装。
- socket 作为 FD_SOCKET 类型纳入文件描述符管理，关闭时调用 onps 的 close 释放资源。
- recvfrom 支持返回对端地址与端口，便于用户态复用。

关键实现位于 [src/syscall/sysnet.c](src/syscall/sysnet.c)、[src/syscall/syscall.c](src/syscall/syscall.c) 与 [src/fs/file.c](src/fs/file.c)。

---

## 7. 构建与运行现状

构建系统已经完成对 onps 的必要裁剪与包含路径配置，避免引入 PPP、IPv6 与网络工具等非必需模块，维持内核二进制体积与编译稳定性。网络设备参数与 virtio-mmio 总线已保持一致，系统启动后自动初始化网络并进入可用状态。

涉及的构建配置集中在 [Makefile](Makefile) 与 [run.sh](run.sh)。

---

## 8. 已验证能力

当前已验证能力包括：

- onps IPv4 + UDP 端到端收发链路稳定可用。
- virtio-net 收发路径与中断路径贯通。
- 以太网接收线程稳定运行，能连续处理外部报文。
- 用户态 UDP echo 流程可在 QEMU user-mode 网络中完成。

---

## 9. 维护说明与扩展建议

当前实现侧重最小可用与稳定路径。若后续扩展需求增加，建议按以下方向进行：

- TCP 完整性：完善 send/recv 与连接状态判断，增加端到端验证程序。
- ICMP 工具化：视需求开启网络工具并补充用户态示例。
- 资源与性能：根据并发规模调整 onps 的资源上限与缓冲配置。
- 诊断可观测性：增加统计信息输出，便于定位链路或性能问题。

---

## 10. 结论

当前 RUOS 的网络适配已经完成 OS 适配层、网卡驱动、协议栈对接与用户态接口的全链路打通，并能在 QEMU virt 环境下稳定运行。OS 适配层为 onps 提供了线程、时间、同步、临界区的完整支撑；网卡移植与 onps 的注册和收发对接也已闭环实现；系统调用与 FD 管理实现了可用的 socket 接口。此版本适合作为后续 TCP 扩展与性能优化的稳定基础。
