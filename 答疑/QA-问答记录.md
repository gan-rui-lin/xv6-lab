# 问答记录

本文档记录学习 RUOS / OS 概念过程中的核心提问与回答。

---

## 1. MLFQ 为什么 CPU 密集型在最低级？

MLFQ 无法预知进程类型，用**行为推断**：
- 用完时间片 → 一直在算 → CPU 密集型 → **降级**
- 没用完就让出（I/O/yield）→ 交互型 → **保持高优先级**

最低级不是"惩罚"，而是资源分配策略——用**更长的时间片**换**更低的调度优先级**，让交互型任务优先响应。boost 机制（每 100 ticks 全部提升到 level 0）防止饥饿。

---

## 2. Linux CFS 调度器原理

### vruntime
每个进程维护 `vruntime`（虚拟运行时间），CFS 总是选 vruntime 最小的运行：

```
vruntime += 实际运行时间 × (1024 / 该进程权重)
```

nice 值通过查表映射到权重。权重大的进程 vruntime 涨得慢 → 长期偏小 → 被调度更多。

**例：三个进程各跑 10ms**

| 进程 | nice | 权重 | vruntime 增量 |
|------|------|------|--------------|
| A | -10 | 9548 | 1.07ms |
| B | 0 | 1024 | 10ms |
| C | 10 | 110 | 93ms |

### 红黑树
所有可运行进程按 vruntime 排序插入红黑树，取最左节点 = O(1)。

### 动态时间片
```
调度周期 = max(6ms, 进程数 × 0.75ms)
每个进程时间片 = 调度周期 × (自己权重 / 总权重)
```

### 新进程/唤醒进程的 vruntime
- **新 fork 进程**：vruntime = 当前红黑树最小值（不从 0 开始，防止无限抢占）
- **唤醒进程**：vruntime = max(自身, 最小值 - 6ms)（给补偿但不过分）

### 组调度
通过 cgroup 先在组间公平，再组内公平。用户 A 开 1 个进程、用户 B 开 100 个进程 → 各获 50% CPU，而不是 1:100。

---

## 3. sched_yield 和动态优先级

- **sched_yield()**：系统调用，进程主动让出 CPU。MLFQ 中 yield 的进程保持当前优先级不降级。
- **动态优先级**：进程优先级在运行时可调整。MLFQ 天然支持（用完时间片降级、yield 保持、boost 提升）。
- **为什么选 MLFQ**：xv6 原版无优先级（~20 行），CFS 需要红黑树（~数千行），MLFQ（~100 行）是满足比赛要求的最小方案。

---

## 4. swtch 是什么

`swtch` = switch 的缩写（switch 是 C 关键字不能用），是内核中用汇编写的**上下文切换函数**。

只保存 callee-saved 寄存器（s0-s11, ra, sp，共 14 个），因为它被当作普通 C 函数调用，caller-saved 寄存器由编译器在调用前自动处理。

进程之间不直接切换，永远是 **进程 A → 调度器 → 进程 B**。

---

## 5. caller-saved 和 callee-saved 寄存器

RISC-V 把寄存器分两组：

| 类型 | 寄存器 | 谁负责保存 |
|------|--------|-----------|
| caller-saved | a0-a7, t0-t6 | 调用者在 call 前自己存 |
| callee-saved | s0-s11, ra, sp | 被调用者进入时存，返回前恢复 |

**通俗理解**：
- caller-saved："我要打电话了，自己先把桌上的东西收好"
- callee-saved："别人的办公桌，我用完必须恢复原样"

swtch 只保存 callee-saved，因为编译器已经帮你存好了 caller-saved。

---

## 6. 进程切换涉及的栈和内存变化

一个进程有三块内存区域：
- **用户栈**：用户态代码用的栈
- **内核栈**：该进程陷入内核后用的栈（每个进程独立一个）
- **trapframe**：保存用户态全部寄存器的页

完整切换过程（A → B）：

```
用户态 A → trap → 存 31 个用户寄存器到 trapframe A → 切到 A 的内核栈
→ swtch → 存 14 个内核寄存器到 A.context → 恢复调度器 context → scheduler
→ 选中 B → swtch → 恢复 B.context → 回到 B 的内核栈
→ usertrapret → 从 trapframe B 恢复 31 个用户寄存器 → sret → 用户态 B
```

两层保存两层恢复：
- 外层（trap/ret）：用户态全部寄存器 + 页表切换
- 内层（swtch）：内核态 callee-saved 寄存器 + 内核栈切换

---

## 7. 结构体对齐 sizeof

```c
struct { int16_t a; int64_t b; int8_t c; };
```

规则：字段起始地址对齐到自身大小的倍数，总大小对齐到最大字段的倍数。

```
[0-1] a(2B) → [2-7] 填充6B → [8-15] b(8B) → [16] c(1B) → [17-23] 填充7B
sizeof = 24
```

按大小从大到小排列（b, a, c）可减少填充：sizeof = 16。

---

## 8. C/C++/Java 异常处理

**C**：没有异常。用返回值 + errno，内核用 `goto cleanup` 逐步回滚。

**C++**：`try / catch / throw`，抛异常时沿调用栈向上找 catch，途中析构局部对象（栈展开）。构造函数抛异常时析构函数不会被调用，需用 RAII（如 unique_ptr）包装资源。

**Java**：分受检异常（编译器强制处理）和非受检异常（RuntimeException），有 finally 和 try-with-resources。

**Linux 内核用 C 的 goto cleanup 模式**：因为内核没有 C++ 运行时库，不支持栈展开。

---

## 9. trapframe 是什么

trapframe = 每个进程专属的一页内存，保存用户态全部寄存器快照。

包含：
- 内核上下文（kernel_satp, kernel_sp, kernel_trap, kernel_hartid）
- 31 个用户寄存器（ra, sp, a0-a7, s0-s11, t0-t6）
- epc（陷入时的用户态 PC）

**什么时候用**：trap 入口存寄存器到 trapframe，返回用户态时从 trapframe 恢复。

**和 context 的区别**：trapframe 是用户态↔内核态的桥梁（31 个寄存器），context 是进程↔调度器的桥梁（14 个寄存器）。

---

## 10. clone_fork 实现详解

调用链：`fork()` → `ecall SYS_clone` → `sys_clone()` → `clone_fork()`

musl 的 fork() 实际发的是 `clone(SIGCHLD, 0, 0, 0, 0)`。fork、vfork、pthread_create 底层都是 clone，区别只在 flags：

| flag | fork | pthread_create |
|------|------|----------------|
| CLONE_VM（共享地址空间） | 否 | 是 |
| CLONE_FILES（共享 fd 表） | 否 | 是 |
| CLONE_THREAD（线程组） | 否 | 是 |

clone_fork 主要步骤：
1. `allocproc()` 分配 proc 槽
2. `uvmcopy()` 复制页表 + `vma_copy()` 复制 mmap 信息
3. 复制 trapframe，设 `a0=0`（子进程返回值）
4. 设自定义栈（`stack != 0` 时覆盖 sp）
5. 设 TLS（`CLONE_SETTLS` 时设 tp 寄存器）
6. tid 写回（`CLONE_CHILD_SETTID/CLEARTID`）
7. 复制 fd 表、cwd
8. 设 parent，标记 RUNNABLE

---

## 11. 信号处理机制

### 数据结构
- `sigpending`：uint64 位图，第 N 位 = 信号 N 待处理
- `sigmask`：uint64 位图，被屏蔽的信号
- `sigactions[64]`：每个信号的 handler/flags/mask

### 投递时机
从内核态返回用户态时检查 `sigpending & ~sigmask`。

### sigframe 机制
内核在返回用户态前**改 trapframe 劫持执行流**：

```
第 1 步: 把 trapframe 里的寄存器复制到用户栈上（构造 sigframe）
第 2 步: 修改 trapframe：epc=handler, ra=restorer, a0=信号编号, sp 下移
第 3 步: sret 回到用户态 → 执行 handler
handler return → ret 跳到 ra = restorer
restorer: ecall SYS_rt_sigreturn → 内核从 sigframe 恢复原始寄存器
sret → 回到原代码，好像什么都没发生
```

### 为什么不直接跳回原程序要走 ecall？
用户态没法恢复完整现场——改了 sp 就找不到 sigframe，改了 ra 就跳不回恢复代码，sepc 是特权寄存器用户态写不了。

### restorer 是什么？
固定的两条指令：`li a7, SYS_rt_sigreturn; ecall`。由 libc 或 VDSO 提供。

### 内核怎么从用户栈读 sigframe？
`copyin(p->pagetable, &frame, sp, sizeof(frame))` — 用用户页表把虚拟地址翻译成物理地址再拷贝到内核。

---

## 12. sepc 和 ra 的区别

| | sepc | ra |
|---|------|-----|
| 谁用 | `sret` 指令 | `ret` 指令 |
| 场景 | 跨特权级：内核→用户 | 同特权级：函数→函数 |
| 类型 | CPU 特权寄存器 | 普通通用寄存器 |

trapframe.epc 是 sepc 在内存中的"影子"：陷入时 sepc→epc，返回时 epc→sepc。

信号中：sret 用 sepc 跳到 handler（跨特权级），handler 的 ret 用 ra 跳到 restorer（同特权级）。

---

## 13. PTE 结构与页表查找

PTE = 页表数组里的一个 64 位整数：

```
[63:54] Reserved | [53:10] PPN(物理页号,44位) | [9:8] RSW | [7] D | [6] A | [5] G | [4] U | [3] X | [2] W | [1] R | [0] V
```

Sv39 三级页表查找：虚拟地址拆成 VPN[2]/VPN[1]/VPN[0]/offset，逐级查表，最终 `物理地址 = PTE.PPN << 12 | offset`。

---

## 14. Buddy + Slab 分配器

### Buddy
管理 2^n 大小的连续页块。分配时向上找空闲块再逐级分裂，释放时通过 `buddy = addr XOR (1 << order)` 找伙伴合并。

### Slab
从 buddy 拿一整页，切成固定大小的 slot（如 64B），用 bitmap 管理。7 个 size class（32/64/128/256/512/1024/2048），请求 100B → 找 128B 的 slab。

### 两层配合
```
<= 2KB → Slab（小对象，接近 O(1)）
>  2KB → Buddy（大块，2^n 对齐）
```

---

## 15. mmap 和 malloc

| | malloc | mmap |
|---|--------|------|
| 全称 | memory allocate | memory map |
| 层级 | 用户态库函数（libc） | 内核系统调用 |
| 粒度 | 任意字节 | 页对齐（4KB 倍数） |
| 能映射文件 | 不能 | 能 |

malloc 是 mmap 的上层封装。小请求用 brk 扩堆，大请求 (>=128KB) 调 mmap。mmap 还可用于文件映射、共享内存、动态链接器加载 .so。

---

## 16. COW 引用计数

每个物理页一个 `refcount`。fork 时父子共享物理页并标记只读+COW，refcount=2。

写入触发 page fault 时：
- `refcount > 1`：复制物理页，新页 refcount=1，旧页 refcount--
- `refcount == 1`：只剩自己，不复制，直接改权限为可写

---

## 17. 为什么 PTE 不能直接赋值物理地址

PTE 是地址和标志位拼在一起的 64 位整数：

```c
*pte = ((new_pa >> 12) << 10) | PTE_R | PTE_W | PTE_U | PTE_V;
```

直接 `*pte = new_pa` 会导致低 10 位全是地址数据而非标志位，V=0 页表项无效。

---

## 18. malloc、brk/sbrk、mmap、buddy、slab 的层次关系

### malloc 是谁提供的？
malloc 是**用户态 C 库函数**（libc/musl 提供），不是系统调用。它在用户态管理堆内存的切分和回收，底层通过 brk 和 mmap 向内核要大块内存。

### brk 和 sbrk 的区别

| | brk | sbrk |
|---|---|---|
| 本质 | **真正的系统调用** | **libc 封装的库函数** |
| 参数 | 绝对地址：`brk(0x50000)` | 相对偏移：`sbrk(4096)` |
| 返回值 | 新堆顶 | **旧堆顶**（= 新内存起始） |

Linux 内核里只有 `sys_brk`，sbrk 是 libc 在用户态维护当前堆顶，调用 brk 实现的：

```c
void *sbrk(intptr_t inc) {
    unsigned long old = __brk;       // libc 维护的当前堆顶
    brk(old + inc);                  // 真正的系统调用
    __brk = old + inc;
    return (void *)old;              // 返回旧堆顶
}
```

### brk 做了什么？

brk **只改内核里的一个数字**（`mm_struct.brk`），标记堆虚拟地址的上界。不分配物理页，不填页表。

```
brk 前：heap_start ──── brk       |  非法
brk 后：heap_start ──────── new_brk |  非法
                    ^^^^^^^^ 这段虚拟地址变"合法"了
```

物理页在访问时通过 Page Fault 按需分配（Lazy Allocation）。

### 完整调用链

```
用户 malloc(64)
  → libc 从空闲链表切 64 字节
  → 不够？sbrk(4096) → brk(old+4096) → 内核改 mm.brk
  → 返回用户态，libc 把新空间加入空闲链表
  → 用户写入 *ptr = 42
  → Page Fault → 内核 buddy 分配物理页 → 填页表 → 继续执行
```

### 各层对比

| | 层次 | 管理什么 | 最小粒度 | 谁用 |
|---|---|---|---|---|
| malloc/free | 用户态库 | 用户堆内存 | 1 字节 | 用户程序 |
| brk | 系统调用 | 堆顶虚拟地址边界 | 页对齐 | malloc 内部 |
| mmap | 系统调用 | 虚拟地址映射 | 页对齐 | malloc 大请求 / 文件映射 |
| Buddy | 内核内部 | 物理页框 | 1 页(4KB) | 内核分配物理页时 |
| Slab | 内核内部 | 内核小对象 | 几十~几百字节 | 内核分配结构体时 |

---

## 19. 虚拟地址空间的大小限制

虚拟地址空间**不是无限大**，由 CPU 地址位宽决定：

| 架构 | 虚拟地址位数 | 用户空间 |
|------|------------|---------|
| 32 位 | 32 bit | 3 GB |
| RISC-V Sv39 | 39 bit | 256 GB |
| x86_64 | 48 bit | 128 TB |

用户程序想访问超出物理内存的数据量时：
1. malloc 可能直接返回 NULL
2. 若内核允许 overcommit，malloc 成功但访问时按需分配物理页
3. 物理内存 + swap 耗尽 → OOM Killer 杀进程

虚拟地址空间很大但有限，物理内存才是真正的瓶颈。

---

## 20. mmap 与 brk 的区别

| | brk | mmap |
|---|---|---|
| 管理方式 | 移动一条线，只能连续扩展/收缩 | 任意位置开辟独立区域 |
| 位置 | 堆区，紧接 data 段往上 | 独立的 mmap 区域（高地址） |
| 释放 | 只能从顶部往回缩 | 任意一块可单独 munmap |
| 能映射文件 | 不能 | 能 |

brk 的缺点：中间的块无法单独释放（空洞只能留给 malloc 内部复用）。mmap 每块独立，munmap 直接归还内核。

所以 malloc 小请求用 brk（批量扩展，高效），大请求用 mmap（独立映射，释放干净）。

---

## 21. VMA 的存储与查找

### VMA 存在哪

每个进程的 `task_struct` → `mm_struct` → VMA 链表 + 红黑树。

```c
struct mm_struct {
    struct vm_area_struct *mmap;    // VMA 链表
    struct rb_root mm_rb;           // VMA 红黑树（按地址排序）
    unsigned long start_brk, brk;
};

struct vm_area_struct {
    unsigned long vm_start, vm_end; // 虚拟地址范围
    pgprot_t vm_page_prot;          // 权限
    struct file *vm_file;           // 映射的文件（NULL=匿名）
    struct mm_struct *vm_mm;        // 所属进程的 mm
};
```

### VMA 归属

VMA 挂在进程自己的 mm_struct 下，天然归属该进程。不同进程的 VMA 互相独立，虚拟地址可以重合（各自有独立页表）。

### Page Fault 时查 VMA

在 mm_struct 的红黑树中 O(log n) 查找：地址落在哪个 VMA 范围内？
- 找到且权限匹配 → 合法，分配物理页
- 找不到或权限不对 → SIGSEGV

### mmap 如何选地址

mmap(NULL, size, ...) 时，内核在当前进程 VMA 红黑树的**间隙**中找第一个放得下 size 的空位（通常从高地址往低地址搜索），创建新 VMA 插入红黑树，返回起始虚拟地址。

同一进程内 VMA 不会重叠（否则 Page Fault 无法确定规则）。

### VMA 数量限制

`/proc/sys/vm/max_map_count` 默认 65536。即使物理内存充足，VMA 数量到上限后 mmap 返回 ENOMEM。

### 修改 VMA 大小

`mremap(ptr, old_size, new_size, MREMAP_MAYMOVE)`：后面有空间则原地扩展，否则搬到新位置（只改页表映射，物理页不动）。

---

## 22. 地址合法性校验在哪一层

**用户态没有任何校验**，全部由 CPU 硬件 + 内核完成：

```
用户执行 *p = 42
  → CPU 硬件查页表
    → 有映射且权限对 → 正常访问
    → 无映射 → Page Fault → 内核查 VMA
      → 在 VMA 内 → 合法，分配物理页
      → 不在任何 VMA → SIGSEGV 杀进程
    → 有映射但权限不对（如写只读页）→ SIGSEGV
```

用户态无法直接修改 VMA，只能通过系统调用（brk/mmap/munmap/mprotect/mremap）请求内核修改。VMA 存在内核内存中，页表标记为仅内核态可访问。

---

## 23. Slab 的 7 个 size class

每个 size class **内部**切的都是固定大小的 slot，但需要多个 size class 覆盖不同大小的请求：

```
size class 0: 一页切成 32B 的 slot  → 128 个
size class 1: 一页切成 64B 的 slot  → 64 个
size class 2: 一页切成 128B 的 slot → 32 个
...
size class 6: 一页切成 2048B 的 slot → 2 个
```

请求来了找最小能装下的：要 100B → 找 128B 的 slot，浪费 28B。如果只有一种固定大小，500B 的对象就没法分配。

---

## 24. 全局锁在多核场景下的瓶颈

Buddy/Slab 有一把全局锁，任何核心分配/释放都要抢锁：

- **2 核**：偶尔等一下，问题不大
- **128 核**：大量核心排队等一把锁，同一时刻只有一个在干活

Linux 的解决方案是 **per-CPU 缓存**：每个核有自己的小仓库，分配时先从本地拿（无锁），仓库空了才去全局池（才需要锁）。RUOS 跑在 QEMU 2 核上，全局锁够用。

---

## 25. 零页优化（zero_page）

内核预先准备一个全局物理页，内容全 0，所有进程共享：

- **申请时（uvmalloc_lazy）**：只记录虚拟地址合法，不分配物理页
- **首次读**：Page Fault → 映射到 zero_page（只读），读到 0，不分配新页
- **首次写**：Store Page Fault → 分配真正的物理页，内容初始化为 0，映射为可读写

1000 个全零页只用 1 个物理页。和 COW 思想一样——共享只读，写时才复制。

---

## 26. uvmcopy vs memmove

| | memmove | uvmcopy |
|---|---------|---------|
| 层级 | 最底层，逐字节搬数据 | 高层，操作页表 |
| 做什么 | 复制一段内存内容 | 复制整个地址空间的映射关系 |
| 感知页表吗 | 不感知 | 遍历页表，创建新映射 |
| 谁调它 | uvmcopy 内部 / 任何拷贝场景 | fork |

uvmcopy 在 COW 模式下遍历父进程的每个虚拟页，在子页表中建立映射指向同一物理页，设只读+COW 标记，refcount++。非 COW 版本内部调 memmove 复制物理页内容。

---

## 27. walk 函数：软件查页表

`walk(pagetable, va, alloc)` 沿三级页表逐级查找，返回最终 PTE 的指针。

```
虚拟地址拆分:  VPN[2](9bit) | VPN[1](9bit) | VPN[0](9bit) | offset(12bit)

查找过程:
  第一级页表[VPN[2]] → PTE → 取出 PPN → 第二级页表的物理地址
  第二级页表[VPN[1]] → PTE → 取出 PPN → 第三级页表的物理地址
  第三级页表[VPN[0]] → 返回这个 PTE 的指针
```

中间某级 PTE 无效时：
- `alloc=0`：返回 NULL（只查找）
- `alloc=1`：kalloc 分配新页表，填入 PTE，继续往下走（用于 mappages 建立新映射）

调用者拿到 PTE 指针后可读取或修改：`*pte = (pa >> 12) << 10 | flags;`

---

## 28. VMA 惰性映射

**核心思想**：mmap 时不分配物理内存，只在 VMA 链表里记个笔记。真正访问时通过 Page Fault 分配。

**mmap 调用时**：找空闲虚拟地址 → vma_add 记录到链表 → 返回虚拟地址。没有 kalloc，没有 mappages，页表里什么都没填。

**用户访问时**：CPU 查页表发现没映射 → Page Fault → 内核查 VMA 发现合法 → kalloc 分配物理页 → 匿名映射 memset(0)，文件映射 readi 从磁盘读 → mappages 建立映射 → 返回用户态重新执行。

**"VMA 记录语义，PTE 记录状态"**：VMA 说"合法可读写"，PTE 说"没映射"，两者可以不一致。PTE 空不代表非法，可能只是还没分配。

---

## 29. mmap 返回什么

返回内核分配的一段**空闲虚拟地址的起始地址**。此时这个地址合法但没有物理内存支撑（VMA 有记录，页表为空，物理页未分配）。首次访问时 Page Fault 触发真正的物理页分配。mmap 返回的是一个"承诺"——地址可用，物理资源等真正用到时再给。

---

## 30. VMA split/unmap 和 mprotect

munmap 取消映射时，范围不一定对齐 VMA 边界，四种情况：
- **完全覆盖**：删除整个 VMA
- **砍左边**：vma->start 右移
- **砍右边**：vma->end 左移
- **砍中间**（最复杂）：一个 VMA 分裂成两个，需分配新 VMA 节点

mprotect 用**两阶段法**：先 split（失败则回滚，什么都没改），再改权限。避免"改了一半权限然后 split 失败"的不一致状态。

RUOS 用链表存 VMA，O(n) 查找，够用。Linux 用 Maple Tree（6.1+），O(log n) 查找，且自动合并相邻同属性 VMA 防止碎片。

---

## 31. trampoline 和 trapframe

**trapframe**：每个进程一份，一页内存，保存用户态全部 31 个寄存器。进内核时存档，回用户态时读档。

**trampoline**：全局一份，一页代码（uservec + userret 汇编），所有进程共享。作用是**切换页表时的安全跳板**——在用户页表和内核页表中映射到同一个虚拟地址（最高地址处），这样切换页表的瞬间 PC 还有效，CPU 不会断。

流程：中断/ecall → trampoline.uservec（存寄存器到 trapframe，切内核页表）→ usertrap() → 处理完 → usertrapret() → trampoline.userret（切用户页表，从 trapframe 恢复寄存器）→ sret 回用户态。

---

## 32. trampoline 为什么每个进程都看得到 & uservec/userret 是什么

**为什么都看得到**：`proc_pagetable()` 创建进程页表时主动 mappages 把 trampoline 映射进去，所有进程页表和内核页表都映射到同一个虚拟地址、同一个物理页。没有 PTE_U 标志，用户代码不能主动调用，只有 CPU 因中断/ecall 切到 S-mode 后才能执行。

**uservec/userret**：`trampoline.S` 里的两段固定汇编代码，编译后永远不变。uservec 通过 sscratch 找到 trapframe 地址，存完 31 个寄存器后从 trapframe 加载内核上下文（kernel_sp/kernel_satp），切换到内核页表，跳到 usertrap()。userret 反过来，切换到用户页表，从 trapframe 恢复寄存器，sret 回用户态。

**代码（trampoline）永远不变，数据（trapframe）每次 trap 都在更新。**

trampoline 怎么知道操作哪个进程的 trapframe？通过 `sscratch` 寄存器：usertrapret 返回用户态前把当前进程的 trapframe 地址写入 sscratch，下次 trap 时 uservec 从 sscratch 取出。trampoline 是通用的存取逻辑，具体操作哪个 trapframe 取决于当前进程。

---

## 33. 目录、文件名和 inode 的关系

**目录是一个特殊文件**，内容是 (文件名, inode号) 的映射表。inode 存文件的所有信息（类型、大小、权限、数据块位置），但**不包含文件名**。

**硬链接**：多个目录项指向同一个 inode。两个名字共享同一份数据，删一个名字只是链接数 -1，链接数到 0 才真正释放。

**软链接**：创建新 inode，内容是目标路径字符串。访问时内核再去解析路径。删了原文件，软链接就断了（悬空）。

---

## 34. 块缓存 + 日志的写入过程

`write(fd, "hello", 5)` 的完整路径：

1. **bread(blockno)**：在缓存池里找目标块，命中直接返回，未命中从磁盘读入缓存
2. **修改 buf->data**：只改内存，磁盘还是旧数据
3. **log_write(buf)**：不写磁盘，只在日志待写列表里记一笔
4. **commit()**（系统调用结束时触发）：
   - 把所有待写 buf 写到磁盘**日志区域**
   - 写日志头标记 commit（断电恢复的关键点）
   - 把日志区数据复制到磁盘**实际位置**
   - 清零日志头
5. **brelse(buf)**：释放 buf 引用，buf 留在缓存池供后续复用

数据被写了两次（日志区 + 实际位置），这是日志的写放大代价，换来断电一致性。commit 前断电 → 丢弃日志；commit 后断电 → 重放日志恢复。

---

## 35. bread 的过程（和页表无关）

bread 是磁盘块缓存，不涉及页表。页表管虚拟地址→物理地址，bread 管磁盘块号→内存 buf 的缓存。

`bread(dev, blockno)` 的过程：
1. **bget**：遍历缓存池链表找 blockno 匹配的 buf。命中 → refcnt++，直接返回。未命中 → 回收一个 refcnt==0 的 buf（LRU），标记 valid=0
2. **valid==0 时从磁盘读**：通过 VirtIO 块设备驱动发 DMA 读请求，sleep 等设备完成，中断唤醒后 buf->data 就是磁盘内容
3. 返回 buf，调用者直接操作 buf->data

---

## 36. 文件偏移量怎么找到磁盘块号（bmap）

`read(fd, buf, 100)` 时内核需要把文件偏移量转成磁盘块号：`offset / block_size = 逻辑块号`，再通过 bmap 查出磁盘块号。

**xv6 inode 的 addrs[]**：前 12 个是直接块号（O(1) 查表），第 13 个指向间接块（一页存 256 个块号，多读一次磁盘）。最大文件 268 块。

**FAT32 簇链**：FAT 表是大数组，`fat[当前簇] = 下一个簇`。找第 N 个块要从头遍历 N 次，O(n)。

**ext4 extent**：B+ 树存 (起始逻辑块, 起始物理块, 长度)，连续磁盘块用一个 extent 描述，O(log n) 查找。

---

## 37. 磁盘布局：超级块、inode 区、数据块区

磁盘格式化后分三个区域：
- **超级块**（1 块）：文件系统元信息（总块数、块大小、空闲块数）。挂载时一次性读入常驻内存。
- **inode 区**：所有文件的 inode（类型、大小、权限、数据块指针 addrs[]）。访问文件时按需读入。
- **数据块区**：文件内容和目录内容。读写时按需读入。

打开 `/file1` 的过程：读根目录数据块 → 找到 "file1" 对应 inode 号 → 去 inode 区读 inode → 拿到 addrs 指向的数据块号 → 去数据块区读内容。

---

## 44. RUOS 适配 ext4 做了什么

嵌入 lwext4 第三方库 + 写胶水层（ext4fs.c ~800 行）：
1. **块设备桥接**：lwext4 的 bdev_read/bwrite → xv6 的 bread/bwrite，处理 512B↔1024B 扇区换算
2. **内存分配桥接**：ext4_user_malloc/free → kmalloc/kmfree（内核没有标准库）
3. **伪 inode 包装**：make_inode() 构造 xv6 inode，major=EXT4_INODE_TAG 标记，存 ext4_path 供 lwext4 路径操作
4. **路径解析**：resolve_path 处理相对路径/./..,  ext4_namei 路径→伪 inode，符号链接递归解析（最多 8 层）
5. **动态链接器符号链接**：挂载后自动创建 /lib/ld-musl-*.so.1 → /musl/lib/libc.so
6. **VFS 接口逐个适配**：ext4_readi/writei/createat/unlink/getdents64/truncate，每个调 lwext4 API 再转换
7. **虚拟块设备容量**：从 1GB 改为 8GB，修复高块号文件读取失败的 bug

lwext4 "只认自己的接口"：它定义了 `ext4_blockdev_iface` 结构体（bread/bwrite/open/close 函数指针），内部读写磁盘只调这些函数指针，不关心底层硬件。RUOS 实现这些函数指针，把 lwext4 的 512B 扇区请求转换为 xv6 的 1024B bread/bwrite 调用。像电源适配器——lwext4 是欧洲插头，xv6 是中国插座，bdev_read 是转换器。

---

## 45. 竞态条件

同样的代码，执行时序不同结果不同。例如两个 CPU 同时 `counter++`（读→改→写三步），如果交替执行可能两次 +1 只生效一次（都读到旧值）。加锁强制顺序执行，消除时序不确定性。

---

## 46. 丢失唤醒 & 高级同步原语

**丢失唤醒**：release(lock) 和 sleep(channel) 之间有缝隙，wakeup 可能从中溜走导致永远睡死。正确做法：`sleep(channel, lock)` 原子地释放锁+进入 SLEEPING。

**高级同步原语**：
- **信号量**：计数器，P 减 V 加，sem=1 就是 mutex，sem=N 允许 N 并发
- **读写锁**：读共享写互斥，适合读多写少
- **RCU**：读完全无锁，写时复制后原子替换指针，等旧读者结束后释放旧数据
- **完成量**：等待"事件完成"的语义化同步
- **Futex**：无竞争时用户态原子操作（不进内核），有竞争才 syscall 进内核等待。pthread_mutex 底层实现

---

## 47. SBI（Supervisor Binary Interface）

RISC-V 中 M-mode 固件（OpenSBI）提供给 S-mode 内核的服务接口。内核通过 ecall 请求 OpenSBI 帮忙做内核自己做不了的事（串口输出、关机、设定时器、核间中断、启动其他核）。类似用户程序 ecall→内核，内核 ecall→OpenSBI 是再上一层。启动时 OpenSBI 初始化硬件、设置中断委托，然后跳转到内核入口 0x80200000。

SBI 调用语法：`register uint64 a0 asm("a0") = val` 强制绑定变量到指定寄存器（GCC 扩展），`asm volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a6))` 执行 ecall 指令，a7=扩展号，a6=功能号，a0=参数兼返回值，volatile 防止编译器优化掉。

**扇区 vs 逻辑块**：磁盘硬件最小单位是扇区（512B），文件系统以逻辑块（4KB = 8 扇区）为单位读写，减少 I/O 次数。不全部加载到内存是因为磁盘可能 TB 级而内存只有几 GB，所以按需加载。

---

## 38. 文件描述符、打开文件表、write 的完整路径

**fd** 是 open 返回的整数编号，是进程 `ofile[]` 数组的下标。`ofile[fd]` 指向 `struct file`，包含 inode 指针、文件偏移量（off）、读写权限、引用计数。

**write(fd, buf, n) 路径**：`ofile[fd]` → struct file → inode → bmap 算磁盘块号 → bread 读缓存 → 内存中修改 → log_write 记日志 → file->off 前进。

**为什么用 fd 不用文件名**：open 只做一次路径解析，后续 write 直接查表 O(1)。

**引用计数**：dup/fork 让多个 fd 指向同一个 struct file，ref++。close 时 ref--，ref=0 才真正释放。

**用户按字节，文件系统按块**：用户写第 5000 字节 → 文件系统算出磁盘块 4、块内偏移 904 → 读整块 → 改对应字节 → 写回整块。

---

## 39. open 的完整过程

`open("hello.txt", O_RDWR)` 分六步：

1. **路径解析（namei）**：从 cwd 开始逐级读目录数据块，匹配文件名，拿到 inode 号。多级路径逐级走。
2. **读 inode（iget）**：在内存 inode 缓存中找，未命中则从磁盘 inode 区读入。
3. **权限检查**：O_RDWR 是否允许。
4. **分配 struct file（filealloc）**：设置 type、inode 指针、off=0、readable/writable、ref=1。
5. **分配 fd（fdalloc）**：在进程 ofile[] 数组找第一个空槽，填入 struct file 指针。
6. **返回 fd**：用户拿到整数编号。

最终链路：`ofile[fd] → struct file (off, 权限) → inode (size, addrs[]) → 磁盘块`。

O_CREAT 且文件不存在时：在目录数据块分配新目录项 + 在 inode 区分配新 inode，再走后续相同步骤。

---

## 40. ip 是什么 / inode 是什么 / file->ip 是指针不是继承

`ip` = `struct inode *`，指向内存中 inode 结构体的指针。inode 存在两处：磁盘（持久化，只有 type/size/addrs 等数据）和内存缓存（磁盘数据 + 运行时的 ref/lock/valid）。

`iget(dev, 42)` 在内存 inode 缓存数组中找 inum==42 的，命中则 ref++，未命中则占空槽等 ilock 时从磁盘读入。

`file->ip` 就是一个 8 字节指针字段，不是继承（C 没有继承）。多个 struct file 可以指向同一个 inode（同一文件被多次 open），各有独立的 off（读写位置），共享 inode 的 ref 计数。

---

## 41. 文件在磁盘上的三种存放方式

- **连续存放**：文件头记起始块+长度，顺序读最快，但删除后产生外部碎片。
- **链表/FAT**：每个块指向下一个块（FAT 表把指针集中存放），无碎片但随机访问 O(n)。
- **索引存放**：inode 的 addrs[] 直接记录每个块号，随机访问 O(1)，间接块扩展容量。

RUOS 三个文件系统：xv6fs 用索引（addrs[12]+间接块），FAT32 用簇链（遍历 FAT 表），ext4（lwext4 库）用 extent 树（B+ 树存连续段，O(log n)）。

Linux：ext2/3 用索引（直接+三级间接），ext4 用 extent 树，FAT32 用簇链。

---

## 42. 为什么一级索引块能映射 n 个数据块

索引块本身是一个磁盘块（如 4KB），里面装满块号（每个 4B）：n = 4096/4 = 1024 个。一级索引块存 n 个块号 → n 个数据块。二级索引块存 n 个指针 → 每个指向一级索引块 → n² 个数据块。三级类推 → n³。ext2/3 的 13 个指针：10 直接 + 1 一级(n) + 1 二级(n²) + 1 三级(n³)，总容量约 4TB（n=1024 时）。

xv6 只有 256 个是因为块大小 1024B（不是 4KB）：1024/4=256。且只有 12 直接+1 一级间接，最大文件 268KB。教学够用。

---

## 43. 怎么通过 inode 号找到 inode

inode 区是磁盘上的连续数组，inode 号就是数组下标，直接算偏移 O(1)：`block = inode_start + (inum * inode_size) / block_size`，`offset = (inum * inode_size) % block_size`。bread 读出该块，从偏移处取固定字节即为 inode 数据。不需要任何查找。