#import "lib.typ": *
// 可配置信息

#let cover_header = "RuOS"
#let report_title = "决赛设计文档"
#let title = ""
#let course = ""
#let major = "计算机科学与技术"
#let teacher1_name = "蔡朝晖"
#let teacher1_title = none
#let teacher2_name = none
#let teacher2_title = none
#let student_id = "RUOK"
#let student_name = "干瑞麟 周锦耀 黄文婷"
#let year = "2026"
#let month = "1"
#let maketitle = true
#let makeabstract = true
#let makeoutline = true
#let outline-depth = 3
#let first-line-indent = auto
#let font = none // 使用默认 font

#let abstract = [
RuOS 是一个使用 C 语言实现，支持 RISCV64 硬件平台的多核宏内核操作系统。RuOS 基于 xv6 操作系统，并在进程管理、内存管理、文件系统、信号机制、网络模块、系统调用等方面进行了大量改进和优化，提升了系统的性能和稳定性。本文档详细介绍了 RuOS 的设计理念、架构、实现细节以及测试结果，展示了其在多核处理器环境下的高效、稳定的运行能力。

RuOS 各个模块的具体改进如下表所示：

#figure(
  table(
    align: center,
    columns: (auto, auto),
    row-gutter: auto,
    inset: 10pt,
    [模块],
    [改进内容],
    [进程管理],
    [引入多级反馈队列调度算法，实现简单的负载均衡],
    [内存管理],
    [Buddy + Slab 分配器结合，提升内存分配效率 #linebreak() 支持写时复制、零页分配、懒分配，减少内存分配的时间开销],
    // [内存管理],
    // [支持写时复制、零页分配、懒分配，减少内存分配的时间开销],
    [文件系统],
    [通过类 VFS 设计提供对 FAT32、EXT4 文件系统的支持],
    [信号机制],
    [实现类 Linux 信号子系统，pending/屏蔽字与用户态 handler，提供 `rt_sigaction`/`rt_sigprocmask`/`rt_sigtimedwait`/`rt_sigreturn`/`kill_signal` 等系统调用],
    [网络模块],
    [集成 ONPS TCP/IP 协议栈，支持 TCP/UDP 协议，支持协议栈内回环以及 tap 模式下与宿主机通信],
    [程序装载],
    [完善 exec/ELF 装载与动态链接兼容：修正用户栈 argv/envp/auxv 布局与对齐，支持 `PT_INTERP` 解释器装载与路径回退，补齐 `AT_*` auxv 并实现 `mprotect`（RELRO）],
    [系统调用],
    [按 LINUX 语义实现或简化实现几十种系统调用，按 LINUX 语义返回错误码，为 busybox 等用户态程序提供内核支持],
    [设备驱动],
    [完善 PLIC 中断分发与设备驱动支持，如串口与 virtio 磁盘/网卡设备],
  ),
)

RuOS 通过了初赛的所有系统调用测试，初赛得分为 102/102:

 #figure(image("/assets/score-pre.png", width: 78%, height: 11%))

]

#let teacher1 = (teacher1_name, teacher1_title)
#let teacher2 = if teacher2_name == none or teacher2_name == "" {
  none
} else {
  (teacher2_name, teacher2_title)
}

#let keywords = ()

#show: ori.with(
  cover_header: cover_header,
  report_title: report_title,
  title: title,
  course: course,
  major: major,
  teacher1: teacher1,
  teacher2: teacher2,
  student_id: student_id,
  student_name: student_name,
  year: year,
  month: month,
  maketitle: maketitle,
  makeabstract: makeabstract,
  abstract: [
    #abstract
  ],
  keywords: keywords,
  makeoutline: makeoutline,
  outline-depth: outline-depth,
  first-line-indent: first-line-indent,
  font: font,
  heading_numbering: numbly("", default: ""),
)

= RuOS 架构图

初赛架构图如 @ruos-architecture-diagram 所示：

#figure(image("/assets/architecture.jpg"), caption: "RuOS 初赛架构图") <ruos-architecture-diagram>

决赛架构图如 @ruos-architecture-diagram-final 所示：

#figure(image("final-architecture.jpg"), caption: "RuOS 决赛架构图") <ruos-architecture-diagram-final>

#include "进程.typ"

#include "内存.typ"

#include "文件.typ"

#include "进程间通信.typ"

#include "网络模块.typ"

#include "程序装载.typ"

#include "系统调用.typ"

#include "设备驱动.typ"