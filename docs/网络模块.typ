
= 网络模块

RuOS 集成了 ONPS TCP/IP 协议栈，支持 TCP/UDP 协议。ONPS 协议栈面向资源受限场景，提供完整的 Ethernet/IPv4/TCP/UDP 与 Berkeley socket 层，同时强调 buf list 零拷贝发送 与较低内存占用，保证了高效的网络性能。

在 RuOS 中，我们保留与以太网、IPv4、TCP/UDP相关的核心功能，关闭 PPP、IPv6 及网络工具等非必需模块，减小内核体积并降低维护成本。RuOS 可通过 virtio-net 设备驱动与虚拟网卡交互，实现数据包的发送与接收。用户态程序可以通过标准的 socket 接口进行网络通信。

== QEMU 网络模式

=== User Networking（SLIRP）

User Networking (SLIRP) 是 QEMU 的默认网络后端，无需 root 权限即可使用，但存在以下典型限制：

1. 性能较差：由于 SLIRP 在用户态实现完整 TCP/IP 栈，额外拷贝和转换较多，吞吐/延迟不如 tap/bridge。
2. 默认不允许 ICMP：不特殊配置时无法发送或接收 ICMP，因此 ping 通常不可用。
3. 外部无法主动访问虚拟机：缺省没有端口转发（hostfwd），宿主机和外网无法直接连接虚拟机。
4. NAT 模式：SLIRP 实现了 NAT 转发，虚拟机访问外部网络是“出站可达”，但“入站默认不可达”。

对应的 qemu 启动参数示例：

- `-device virtio-net-device,netdev=net,bus=virtio-mmio-bus.1`
- `-netdev user,id=net`

对应的默认网络拓扑为：

- 虚拟机 IP：`10.0.2.15`
- 网关：`10.0.2.2`
- DNS：`10.0.2.3`

该拓扑可以用@qemu-usernet-topology 表示：

#figure(image("default-qemu-networking.jpg"), caption: "QEMU User Networking 拓扑图") <qemu-usernet-topology>

结合 SLIRP 的限制可以理解：

- 从虚拟机向宿主机发 UDP 包（`10.0.2.2`）可以到达，但如果宿主机没有服务监听，就无法得到回包。
- 想让宿主机主动连虚拟机，需要额外设置 `-netdev user,hostfwd=...`。

=== Tap Networking

当切换到 tap/bridge 模式时，虚拟机不再经过 SLIRP 的用户态 NAT，而是把二层包直接丢给宿主机桥接设备。这样可以观察更真实的 ARP/TCP 行为，也便于做入站连接或更复杂的网络验证。

对应的一种可能的 qemu 启动参数示例：

- `-device virtio-net-device,netdev=net,bus=virtio-mmio-bus.1`
- `-netdev tap,id=net,ifname=tap0,script=no,downscript=no`

我们需要先在宿主机上配置 tap0 设备并加入桥接，例如：

```bash
sudo ip tuntap add dev tap0 mode tap user $USER
sudo ip link set tap0 up
sudo ip link add br0 type bridge
sudo ip link set br0 up
sudo ip link set tap0 master br0
sudo ip addr add 10.0.2.2/24 dev br0
sudo ip link set br0 up

```

该模式下，虚拟机可以直接与宿主机通信，且支持 ICMP 和入站连接。

Tap 模式下的一种可能的网络拓扑如 @qemu-tap-topology 所示：

#figure(image("tap-bridge-networking.jpg", height: 70%), caption: "QEMU Tap Networking 拓扑图") <qemu-tap-topology>

== 设备层

设备层负责“把网卡的数据拿进来、把要发的数据送出去”，并保证中断驱动下的持续收发：

+ 初始化：在 virtio‑mmio 总线上完成设备发现与特性协商，创建 RX/TX virtqueue；为 RX 队列预分配缓冲区并填充描述符，确保网卡一有数据就能写入；
+ 接收：网卡产生中断后由 PLIC 转发到 `virtio_net_intr`，驱动读取中断状态并确认后进入 `virtio_net_rx`，从 RX used ring 取出已填充的缓冲区，跳过 `virtio_net_hdr` 得到以太网帧，再调用 `net_rx_deliver` 上送协议栈；
+ 发送：`virtio_net_transmit` 把待发数据组织成 TX 描述符链并通知设备；发送完成后在中断里由 `virtio_net_tx_complete` 统一回收；
+ 回收：收包后立即把描述符重新放回 RX avail ring，保证队列不耗尽、收包不断流。

下面是 `net_rx_deliver` 的代码：

```c
void
net_rx_deliver(const uint8 *data, int len)
{
  if (!g_netif || !data || len <= 0)
    return;

  log_info("net: rx len=%d\n", len);
  EN_ONPSERR err = ERRNO;
  PST_SLINKEDLIST_NODE node = (PST_SLINKEDLIST_NODE)buddy_alloc(sizeof(ST_SLINKEDLIST_NODE) + (UINT)len, &err);
  if (!node) {
    log_warn("net: rx drop (alloc failed, len=%d, err=%d)\n", len, err);
    return;
  }

  node->uniData.unVal = (UINT)len;
  memmove(((UCHAR *)node) + sizeof(ST_SLINKEDLIST_NODE), data, len);
  ethernet_put_packet(g_netif, node);
}
```

其中，`data` 指向以太网帧数据，`len` 为帧长度。函数首先检查网络接口和数据有效性，然后分配一个链表节点，将数据复制进去，最后调用 `ethernet_put_packet` 将数据包传递给 ONPS 协议栈进行处理。

通过“预分配缓冲 + 中断驱动 + ring 回收”的机制，设备层为协议栈提供稳定、连续的收发通路。

== 网络层：以太网、ARP 与 IPv4

网络层由 ONPS 提供，RuOS 侧需要做的核心是网卡注册与地址配置（接口聚合在 `src/network/net.c`），原因很直接：ONPS 只有在拿到网卡的 MAC/IP、发送回调 和 接收线程入口 后，才能把驱动当作一个可用的 `netif` （Network Interface，网络接口）来管理。

在 ONPS 协议栈中, `ST_IPV4` 结构体如下面代码所示：

```c
//* 记录IPv4地址的结构体
typedef struct _ST_IPV4_ {
    UINT unAddr;
    UINT unSubnetMask;
    UINT unGateway;
    UINT unPrimaryDNS;
    UINT unSecondaryDNS;
    UINT unBroadcast;
} ST_IPV4, *PST_IPV4;
```

具体流程如下：

- 在 `net_init` 中先 `open_npstack_load` 启动协议栈，再通过 `virtio_net_init` 获取网卡 MAC；
- 构造 `ST_IPV4`（`10.0.2.15/24` + 网关 `10.0.2.2` + DNS），匹配 QEMU user networking 的默认语义；
- 调用 `ethernet_add` 完成注册：
  - 该函数在 ONPS 内部为网卡分配控制块、ARP 资源与接收队列，并保存发送回调 `net_emac_send`；
  - 同时注册接收线程入口 `net_start_eth_recv_thread`，用于消费由 `net_rx_deliver` 上送的报文。
  - 完成了网卡驱动与ONPS协议栈的关键对接。它本质上是创建并初始化了一个 netif（网络接口）​ 结构体，并将其添加到 ONPS 的网络接口列表中。

完成注册后，ONPS 就能对 `eth0` 做 ARP 解析、IPv4 解包与封装，并驱动接收线程处理进入协议栈的数据。该层承担“二层帧 → 三层包”的转换，并提供路由与地址解析基础。

== 传输层：UDP/TCP

传输层由 ONPS 提供实现，它保证了传输层的基础能力（包含TCP/UDP连接管理、数据收发、超时控制、错误码返回等核心能力），RuOS不重复开发底层能力，仅做接口封装和语义对齐，只选用当前业务需要的功能子集。

1. 功能裁剪与聚焦

ONPS虽支持TCP/UDP全量基础能力，但RuOS仅封装实际用到的核心接口：
- UDP：仅封装`sendto/recvfrom`接口；
- TCP：仅封装`connect/listen/accept`（连接管理）和`send/recv`（数据收发）接口。

2. 系统调用封装实现

网络相关系统调用集中在`src/syscall/sysnet.c`文件中实现，核心作用是将用户态传入的参数格式、调用方式，转换为ONPS API可识别的格式，对外暴露的系统调用包括：
- `sys_socket`、`sys_bind`、`sys_connect`
- `sys_sendto`、`sys_recvfrom`
- `sys_listen`、`sys_accept`

3. 文件描述符融合设计

将socket抽象为`FD_SOCKET`类型纳入文件描述符表管理，复用文件操作接口：
- 用户态调用`read/write`操作socket时，内核自动映射为ONPS的`recv/send`接口（逻辑见`src/fs/file.c`）；
- 调用`close`关闭socket文件描述符时，内核触发socket资源的回收逻辑。

4. 错误码语义对齐

ONPS返回的原生错误码，在内核层统一映射为Linux风格的`errno`，确保用户态程序感知到的错误码语义、行为与Linux系统一致。

== 数据路径

RuOS 通过 virtio-net 驱动与虚拟网卡交互，ONPS 协议栈处理网络协议逻辑。具体发送与接收路径如下：

- 发送：用户态 socket → 系统调用层 → ONPS socket → IP/以太网封装 → `net_emac_send` → `virtio_net_transmit`；
- 接收：virtio-net 中断 → `virtio_net_rx` → `net_rx_deliver` → `ethernet_put_packet` → ONPS → 用户态 `recv/recvfrom`。

网络数据的发送与接收流程如 @ruos-network-dataflow 所示：

#figure(image("net_data_path.png"), caption: "RuOS 网络数据路径图") <ruos-network-dataflow>

