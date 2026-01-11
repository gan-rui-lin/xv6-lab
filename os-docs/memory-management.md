## 内存管理总览

```
物理内存 (0x8000_0000..PHYSTOP)
   │  frelist(kalloc) 提供 4KiB 页
   ▼
内核页表 kernel_pagetable (Sv39)
   ├─ 设备恒等映射：UART/PLIC/VIRTIO/TEST_DEVICE
   ├─ 内核 text/rodata/data/bss/堆 1:1 映射
   ├─ TRAMPOLINE (trampoline.S 一页)
   └─ 每核内核栈：KSTACK(i) 两页（含 guard）
用户页表 (每进程)
   ├─ 代码/数据/栈/堆 映射到物理页
   ├─ TRAPFRAME (最高一页，U 不可访问)
   └─ TRAMPOLINE (共享，用户可执行)
```

- 物理地址空间与设备基址定义：`src/memlayout.h`。
- 页表与权限宏：`src/riscv.h`；全局参数（NCPU、PHYSTOP 等）：`src/param.h`。
- 关键模块：物理页分配器 `src/mm/kalloc.c`，页表/映射与用户空间管理 `src/mm/vm.c`，高位 trampoline 代码 `src/trap/trampoline.S`。

## 物理内存与分配器（src/mm/kalloc.c）

- 分配粒度：固定 4KiB 页，供内核栈、页表页、用户物理页、管道缓冲等使用。
- 初始化 `kinit()`：`freerange(end, PHYSTOP)` 以 `end`（`kernel.ld` 导出）为起点，把剩余物理内存按页插入空闲链表。
- 空闲链表节点：`struct run { struct run *next; }`；`kmem.freelist` 受自旋锁保护。
- `kalloc()`：弹出一页，填 0x05 调试填充，返回物理地址（以内核直映虚拟地址形式）。
- `kfree(pa)`：校验页对齐/范围，填 0x01，头插回链表。

> 设计取舍：简单的单链表分配，无伙伴系统；多核下用自旋锁串行化，仍足够支撑教学场景。

## 物理/虚拟布局（memlayout.h）

```
物理地址
0x0000_1000    QEMU boot ROM
0x0200_0000    CLINT (mtime/mtimecmp)
0x0C00_0000    PLIC
0x1000_0000    UART0
0x1000_1000    VIRTIO0 (磁盘)
0x8000_0000    内核加载物理基址 KERNBASE
...            物理内存持续到 PHYSTOP=KERNBASE+128MiB
```

高地址保留：
- `TRAMPOLINE = MAXVA - PGSIZE`：共享一页，用于 S/U 切换。
- `TRAPFRAME = TRAMPOLINE - PGSIZE`：每进程 trapframe 虚拟页，PTE_U=0 防用户访问。
- `KSTACK(i) = TRAMPOLINE - (i+1)*2*PGSIZE`：每核两页，低页为 guard（无效），高页为栈。

## 内核页表与直接映射（src/mm/vm.c）

### 构建：`kvmmake()`
- 新分配顶级页表，清零。
- 恒等映射设备：`TEST_DEVICE`、`UART0`、`VIRTIO0`、`PLIC`（4MiB）。
- 代码段映射：`[KERNBASE, etext)` -> `PTE_R|PTE_X` 只读可执行。
- 数据/堆映射：`[etext, PHYSTOP)` -> `PTE_R|PTE_W`。
- TRAMPOLINE：映射至物理 `trampoline`（或同址调试），权限 `R|X`。
- `proc_mapstacks()`：为每个 hart 分配一页栈并映射到 `KSTACK(i)+PGSIZE`，留下 guard page 防止溢出。

### 切换：`kvminithart()`
- `w_satp(MAKE_SATP(kernel_pagetable))` 写 satp，前后 `sfence_vma` 刷新 TLB。

### 内核地址助手
- `kvmpa(va)`：把内核直映虚拟地址转回物理地址（用于传递给设备，如 VirtIO 的 header 位于内核栈）。

## 页表基元与映射

- `walk(pagetable, va, alloc)`：Sv39 三级遍历，必要时分配中间页表（`kalloc` + 清零）；返回叶子级 PTE 指针，`va>=MAXVA` panic。
- `mappages(pagetable, va, size, pa, perm)`：按页循环写叶子 PTE，禁止重映射已有 PTE_V。
- `uvmunmap(pagetable, va, npages, do_free)`：解除映射，可选择释放物理页；要求页对齐且存在映射。
- `freewalk()`：递归释放页表层级，禁止残留叶子。

PTE 判定：
- 叶子页：`PTE_R|PTE_W|PTE_X` 任一置位；非叶子只置 `PTE_V`。
- 用户访问检查：`PTE_U`。

## 用户地址空间生命周期

### 创建与初始加载
- `uvmcreate()`：分配空页表。
- `uvmfirst(pagetable, src, sz)`：为 initcode 生成地址空间：
  - `prog_pages = ceil(sz/PGSIZE)`，再分配同样数量的栈页（只 RW）。
  - 程序段映射 `PTE_R|W|X|U`，逐页拷贝 `src`。
  - 返回总大小（代码+栈），供 `userinit()` 设置 `sz` 与栈指针。

### 扩缩容
- `uvmalloc(pagetable, oldsz, newsz, xperm)`：向上扩展，逐页 `kalloc` 并映射，失败回滚。
- `uvmdealloc(pagetable, oldsz, newsz)`：向下收缩，超出部分 `uvmunmap(..., do_free=1)`。
- `uvmfree(pagetable, sz)`：释放进程全部物理页并递归释放页表。

### 复制与清理
- `uvmcopy(old, new, sz)`：遍历父页表拷贝每页数据（无 COW），保持原权限；失败清理已映射部分。
- `uvmclear(pagetable, va)`：清除用户访问位，用于在 `exec` 时保护栈顶页面（返回路径使用）。

### 用户态映射结构（示意）

```
低地址
┌───────────────┐
│ text/data/bss │  PTE_U|R/W/X
├───────────────┤
│ user stack    │  PTE_U|R/W   (固定大小，uvmfirst 分配)
├───────────────┤
│ heap (brk)    │  PTE_U|R/W   (uvmalloc 扩展)
│ ...           │
├───────────────┤
│ TRAPFRAME     │  PTE_R/W, !U (陷阱保存区)
├───────────────┤
│ TRAMPOLINE    │  PTE_R/X, U  (共享陷阱入口)
└───────────────┘
高地址
```

## 复制/访问助手

- `walkaddr(pagetable, va)`：仅用于用户页，需 `PTE_U` 且 `PTE_V`，返回物理地址或 0。
- `copyin(pagetable, dst, srcva, len)` / `copyout(pagetable, dstva, src, len)`：跨页搬运，逐页校验映射。
- `copyinstr(...)`：拷贝以 '\0' 结尾的字符串，防越界/缺页。

## 内存管理与其他子系统的衔接

## DOT 图（页表与分配链路）

```dot
digraph mm {
  rankdir=LR;
  node [shape=box, style="rounded,filled", fillcolor="#f8fbff"];

  phys [label="物理内存\n[0x8000_0000..PHYSTOP]\nfreelist by kalloc"];
  kalloc [label="kalloc/kfree\nrun 链表 + spinlock"];
  kvmmake [label="kvmmake()\n恒等映射设备/内核\nproc_mapstacks()"];
  pagetable [label="walk/mappages/uvmalloc\nSv39 三级页表"];
  proc [label="proc_pagetable()\n映射 TRAMPOLINE/TRAPFRAME"];
  trap [label="trapframe\nusertrapret/userret"];
  copy [label="copyin/copyout\nwalkaddr 检查 PTE_U"];

  phys -> kalloc -> {kvmmake pagetable};
  pagetable -> proc -> trap;
  pagetable -> copy [style=dashed, label="用户访问"];
  kvmmake -> trap [label="TRAMPOLINE 映射"];
}
```

- **陷阱与上下文切换**：`TRAMPOLINE` 与 `TRAPFRAME` 用于 S/U 切换；`trap.c` 设置 `stvec=TRAMPOLINE`，`usertrapret` 通过 `satp` 切换到用户页表。
- **进程栈**：`proc_mapstacks()` 为每核内核栈加入 guard page；用户栈由 `uvmfirst`/`exec` 分配，`uvmclear` 去除顶页用户访问。
- **I/O 缓冲**：`bio`/`pipe` 等通过 `kalloc/kfree` 获取页做缓存；VirtIO header 需 `kvmpa` 获得物理地址。

## 调试与扩展提示

- 开启 `PAGE_TABLE_DEBUG` 可打印 walk/mappages 特定地址的调试信息。
- 若引入写时拷贝，可在 `uvmcopy` 中共享物理页并设置 `PTE_W`→清除、增加引用计数，缺页时复制。
- 调整物理内存上限或映射布局需同步 `PHYSTOP`、`kernel.ld`、`memlayout.h`，并考虑 TRAMPOLINE/KSTACK 位置。
