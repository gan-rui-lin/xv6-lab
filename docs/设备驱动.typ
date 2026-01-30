
= 设备驱动

RuOS 运行在 QEMU `virt` 平台上，主要外设都以 memory-mapped I/O (MMIO) 的形式暴露：CPU 通过读写固定物理地址处的寄存器与设备交互；当设备需要“打断”内核（例如收到字符、完成 DMA、队列有回收项）时，再通过外部中断通知内核。与真实硬件相比，virt 平台的好处是设备模型稳定、可复现且便于调试，但也要求驱动严格遵守 MMIO 的时序与内存序，否则很容易出现“偶发丢中断/丢包/卡死”等难以定位的问题。

本章围绕 `src/devs/uart.c`、`src/devs/plic.c` 与 `src/virtIO/`，从 MMIO、PLIC、中断分发到 VirtIO virtqueue/ring（包括 tx_ring 的发送完成回收），系统性说明 RuOS 的设备驱动基础知识与实现细节。

== MMIO：设备寄存器与内存序

在 RISC‑V 上，MMIO 通常表现为一段“不可缓存、具有副作用”的地址区间：读寄存器可能清状态、写寄存器可能触发发送/通知。RuOS 在实现上采用两条简单但关键的规则：

- volatile 访问：通过 `*(volatile uint8*)/*(volatile uint32*)` 访问寄存器，避免编译器把读写优化掉或合并重排（例如 `src/devs/uart.c` 的 `UART_WRITE_REG/UART_READ_REG`，以及 `src/virtIO/virtio_*.c` 的 `R(n, reg)` 宏）。
- 显式内存屏障：在把描述符/环形队列写入内存并“通知设备”之前，必须先保证这些内存写对设备可见；反之，在处理中断时读取 used ring，也要避免乱序导致看到旧数据。RuOS 广泛使用 `__sync_synchronize()` 作为保守的全栅栏，确保“先写队列，再 kick；先读状态，再回收”的顺序成立。

QEMU `virt` 的关键 MMIO 地址在 `src/memlayout.h` 中定义。这些物理地址在内核页表中保持可访问，因此驱动可以直接以物理地址形式进行寄存器读写）。

== 设备与中断拓扑

外设的数据通路与中断通路通常是“分离”的：数据通路走 MMIO/DMA，中断通路走 PLIC。RuOS 的总体拓扑如 @ruos-device-topology 所示：

#figure(
image("device-topology.png", width: 92%),
caption: "RuOS 设备与中断拓扑（MMIO 数据通路 + PLIC 中断通路）"
) <ruos-device-topology>

在启动阶段（`src/boot/main.c`），hart0 完成全局初始化：`consoleinit()` 初始化 UART；`plicinit()` 设置外设 IRQ 的优先级；随后每个 hart 都会调用 `plicinithart()` 配置自己的 S‑mode 使能与阈值，并打开 `SIE_SEIE`（Supervisor External Interrupt Enable），从而允许设备中断进入 `devintr()`。

== UART：控制台与早期调试

UART 是最朴素也最重要的外设之一：它提供早期打印、交互式 shell 输入输出，是定位启动问题和内核崩溃的“生命线”。RuOS 使用 QEMU virt 默认的 16550 兼容 UART（`UART0@0x10000000`），驱动实现位于 `src/devs/uart.c`，并由 `src/devs/console.c` 提供行缓冲与用户态 `read/write` 的对接。

UART 驱动的关键点包括：

- 初始化：`uartinit()` 先关闭中断，再配置波特率锁存（`LCR_BAUD_LATCH`）与分频因子，设置 8‑bit 数据位（`LCR_EIGHT_BITS`），最后打开 FIFO 并把 RX 触发阈值设为 14 字节（减少单字符中断），再开启 RX/TX 中断（`IER_RX_ENABLE/IER_TX_ENABLE`）。
- 输出路径：`uart_write_byte_nolock()` 通过轮询 `LSR.TX_IDLE` 等待发送端空闲，再写 `THR` 发送字节；上层的 `uart_write/uartputc_sync` 用 `uart_tx_lock` 串行化输出，保证并发 `printf` 不会互相穿插。`panicked` 时直接自旋，避免继续输出导致更多状态破坏。
- 输入路径：`uartintr()` 在外部中断到来后持续读取 `RHR`（直到 `LSR` 表明无数据），把字符交给 `consoleintr()`。控制台层维护一个小型环形缓冲（`INPUT_BUF_SIZE=128`），支持退格、回车换行、Ctrl‑D EOF 等语义，并通过 `sleep/wakeup` 让阻塞的 `consoleread()` 在“整行到达”后被唤醒。

这种设计把“慢速字节流设备”和“面向行的用户交互”分层：UART 专注于寄存器与中断，Console 专注于缓冲与语义，这也是 RuOS 后续接入更多 TTY/伪终端能力的良好起点。

== PLIC：外部中断控制器

PLIC（Platform Level Interrupt Controller）负责把多个外设的 IRQ 汇聚到各个 hart，并提供优先级、屏蔽与 claim/complete 的握手机制。对驱动开发而言，理解 PLIC 的三个接口就足够：

- priority：优先级为 0 表示禁用；非 0 表示可投递。RuOS 在 `plicinit()` 中为 `UART0_IRQ/VIRTIO0_IRQ/VIRTIO1_IRQ` 写入优先级 1（`src/devs/plic.c`）。
- enable/threshold（按 hart）：每个 hart 有独立的 `PLIC_SENABLE(hart)` 位图与 `PLIC_SPRIORITY(hart)` 阈值。RuOS 在 `plicinithart()` 中把 UART、virtio‑blk、virtio‑net 的 enable 位都置 1，并把阈值设为 0（即允许投递所有非 0 优先级的中断）。
- claim/complete（按 hart）：当发生 supervisor external interrupt 时，`devintr()` 调用 `plic_claim()` 读取 `PLIC_SCLAIM(hart)` 获得一个 IRQ 号；处理完成后必须调用 `plic_complete(irq)` 把相同的 IRQ 写回 claim 寄存器，PLIC 才会允许该设备再次触发中断。

RuOS 的中断分发逻辑在 `src/trap/trap.c:devintr()`：根据 claim 到的 IRQ 号调用 `uartintr()`、`virtio_disk_intr()` 或 `virtio_net_intr()`，最后统一 complete。这种“claim->dispatch->complete”的模板使得新增设备非常直接：只需在 PLIC 侧启用对应 IRQ，并在 `devintr()` 增加分支即可。

PLIC 的工作机制如 @ruos-plic-flow 所示：

#figure(
image("plic-flow.jpg",width: 70%),
caption: "PLIC 中断工作机制"
) <ruos-plic-flow>


== VirtIO：半虚拟化设备与 mmio legacy 接口

VirtIO 是 QEMU virt 平台上常用的半虚拟化设备规范，核心思想是：驱动与设备共享一段内存队列（virtqueue），通过少量 MMIO 寄存器完成特性协商、队列注册与通知。RuOS 选择实现 QEMU 的legacy virtio‑mmio接口（`src/virtIO/virtio.h` 给出了寄存器偏移与状态位），其初始化流程可概括为：

- 读取 `MAGIC/VENDOR/DEVICE_ID` 做设备存在性校验；
- 依次设置 `STATUS`：`ACKNOWLEDGE -> DRIVER -> FEATURES_OK -> DRIVER_OK`；
- 读取 `DEVICE_FEATURES` 并清掉不支持/不需要的特性（如 `INDIRECT_DESC` 等），再写回 `DRIVER_FEATURES`；
- 配置页大小 `GUEST_PAGE_SIZE=PGSIZE`；
- 对每个队列：写 `QUEUE_SEL` 选择队列，检查 `QUEUE_NUM_MAX`，设置 `QUEUE_NUM`，为队列分配一段连续、页对齐的内存，并把其 PFN 写入 `QUEUE_PFN`。

RuOS 的 virtio‑blk 与 virtio‑net 都采用“2 页队列布局”：

- 第一页放 `desc[]` 与 `avail[]`

- 第二页放 `used[]`

具体代码见 `src/virtIO/virtio_disk.c` 与 `src/virtIO/virtio_net.c` 的 `pages[2*PGSIZE]`/`rx_pages/tx_pages`。

== virtqueue 与 ring：从入队到回收

virtqueue 的数据结构由三部分组成：描述符表 `desc[]`、驱动侧可用环 `avail`、设备侧完成环 `used`。其典型交互过程如 @ruos-virtqueue-layout 所示：

#figure(
image("virtqueue-layout.png", width: 92%),
caption: "VirtIO virtqueue/ring 基本交互"
) <ruos-virtqueue-layout>

其中最容易踩坑的点是内存可见性与索引推进：

- 驱动需要先把 `desc[]`（DMA 地址、长度、flags、next）写好，再把链表头索引写入 `avail[2 + (avail->idx % NUM)]`，最后在内存屏障之后递增 `avail->idx` 并写 `QUEUE_NOTIFY`；
- 设备处理完成后会更新 `used->idx` 并写入 `used->elems[]`（包含已完成链表头 id 与长度），随后触发 IRQ；
- 驱动在中断上下文中从 `used_idx` 扫描到 `used->idx`，依次回收 descriptor 链并唤醒等待者。

所谓tx_ring（发送环）本质上就是“用于发送方向的 virtqueue”：以 virtio‑net 为例，队列 1 作为 TX queue，驱动把“报文头 + 报文数据”的 descriptor 链入队；设备把完成项写入 used ring；驱动在 `virtio_net_tx_complete()` 里根据 used ring 回收链表并释放对应缓冲，从而实现发送完成的资源回收与背压控制。

== virtio‑blk：块设备 I/O

RuOS 的 virtio‑blk 驱动采用中断完成的同步模型：发起 I/O 的线程在睡眠中等待中断唤醒。每次读写会构造3 个 descriptor 的链表，这是 legacy virtio‑blk 的标准做法：

- descriptor0：请求头 `virtio_blk_outhdr`（type/sector 等）；由于该结构体在内核栈上，需用 `kvmpa()` 转成可 DMA 的物理地址；
- descriptor1：数据缓冲区 `b->data`（读时 `VRING_DESC_F_WRITE` 让设备写入，写时 flags=0 让设备读取）；
- descriptor2：1 字节 status（设备写入完成状态）。

驱动用 `vdisk_lock` 保护 free list、`avail/used` 指针与 `used_idx`，当 descriptor 不足时对 `free[0]` 睡眠等待；发起请求后把链表头塞入 avail ring 并 notify，然后在 `b->disk==1` 时对 `b` 睡眠。中断处理函数 `virtio_disk_intr()` 扫描 used ring：校验 status、清除 `b->disk`、唤醒等待该 buf 的线程，并回收整个 descriptor 链。该模型简单可靠，足以支撑文件系统与 busybox 的大量 I/O。

== virtio‑net：收发队列与协议栈对接

virtio‑net 相比块设备更强调吞吐与并发，RuOS 采用 *双队列* 设计：队列 0 为 RX，队列 1 为 TX。

- RX（预投递缓冲 + 回收再投递）：初始化时为每个 descriptor 预先绑定一块 `rx_buf[i]`（包含 `virtio_net_hdr` + payload 空间），flags 置 `VRING_DESC_F_WRITE` 允许设备写入，然后把所有 descriptor 索引推入 `rx_avail` 并 notify。中断到来时 `virtio_net_rx()` 扫描 `rx_used`：取出完成项长度，跳过 virtio 头后把报文交给 `net_rx_deliver()`，随后把同一个 descriptor id 再次写回 `rx_avail` 实现缓冲复用，最后 notify 让设备继续收包。
- TX（tx_ring 入队 + 完成回收）：发送时 `virtio_net_transmit()` 分配 2 个 descriptor（header + data），把链表头写入 `tx_avail` 并 notify=1。完成后 `virtio_net_tx_complete()` 扫描 `tx_used`：释放对应 `tx_info` 中记录的发送缓冲并 `free_chain()` 回收 descriptor。当前实现选择在 TX 完成时 `kmfree(data)`，因此网络栈侧通常以“发送缓冲由内核分配、驱动负责回收”的约定避免额外拷贝；若后续需要支持零拷贝或用户态 socket 直传，可在此处引入引用计数与更细粒度的生命周期管理。

`virtio_net_intr()` 负责统一 ACK `INTERRUPT_STATUS` 并串行执行 RX/TX 回收逻辑；`vnet.lock` 保护队列状态与 free list，避免发送线程与中断处理并发修改 ring 造成索引错乱。

== 工程化细节与可扩展点

设备驱动往往是“最像硬件、最容易出现时序 bug”的部分。RuOS 在实现时做了几处工程化取舍以降低风险：

- 保守的 feature 协商：显式关闭 `EVENT_IDX/INDIRECT_DESC/ANY_LAYOUT` 等特性，避免实现复杂度和兼容性问题；
- 统一的同步原语：块设备走 “sleep 等中断” 的同步模型，网卡走 “中断回收 + 上层队列” 的异步模型，但二者都用自旋锁保护 ring 与 free list，保证最小正确性；
- 清晰的扩展点：新增 VirtIO 设备通常只需复用 `virtio.h` 的寄存器框架与 virtqueue 初始化模板；增加统计/trace 可在 `devintr()`、`virtio_*_intr()`、以及每次 notify 前后记录时间戳与队列深度。

通过 UART+PLIC+VirtIO 的组合，RuOS 在 QEMU virt 平台上形成了一条稳定的“输入/输出/网络”最小闭环，为文件系统、网络协议栈与用户态 busybox 提供了必要的底层支撑；同时代码结构也保持了与 xv6 类似的清晰分层，便于比赛期间快速迭代与定位问题。
