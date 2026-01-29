# MLFQ (Multi-Level Feedback Queue) 调度器实现文档

## 概述

本文档详细介绍了在 xv6-lab 中实现的 MLFQ (Multi-Level Feedback Queue，多级反馈队列) 调度算法。MLFQ 是一种先进的 CPU 调度算法，能够在不需要预先知道进程行为的情况下，自动识别和优化交互式进程（I/O密集型）和计算密集型进程的调度。

## 设计目标

1. **自适应优先级调整**：根据进程行为动态调整优先级
2. **优化响应时间**：为交互式任务提供更快的响应
3. **防止饥饿**：周期性提升所有进程优先级
4. **公平性**：长时间运行的CPU密集型进程也能获得执行机会

## MLFQ 算法原理

### 基本规则

1. **规则1**：新进程从最高优先级开始（Level 0）
2. **规则2**：如果进程用完了时间片，降低其优先级（降一级）
3. **规则3**：如果进程主动让出CPU（如等待I/O），保持其优先级不变
4. **规则4**：调度器总是选择优先级最高的RUNNABLE进程运行
5. **规则5**：同一优先级内使用轮转调度（Round-Robin）
6. **规则6**：周期性地将所有进程提升到最高优先级（防止饥饿）

### 优先级级别配置

```c
#define MLFQ_LEVELS 4              // 4个优先级队列（0最高，3最低）
#define MLFQ_TIME_SLICE_0 2        // Level 0: 2 ticks  - 交互式进程
#define MLFQ_TIME_SLICE_1 4        // Level 1: 4 ticks
#define MLFQ_TIME_SLICE_2 8        // Level 2: 8 ticks
#define MLFQ_TIME_SLICE_3 16       // Level 3: 16 ticks - CPU密集型
#define MLFQ_BOOST_INTERVAL 100    // 每100 ticks提升所有进程
```

**设计思想**：
- 高优先级使用短时间片（快速响应）
- 低优先级使用长时间片（减少上下文切换开销）
- 时间片呈指数增长（2 → 4 → 8 → 16）

## 实现细节

### 1. 数据结构

#### 进程MLFQ信息（proc.h）

```c
struct mlfq_proc_info {
  int level;              //claude: 当前所在的优先级级别（0-3）
  uint64 time_slice;      //claude: 当前级别分配的时间片大小
  uint64 ticks_used;      //claude: 在当前级别已使用的时间片
  uint64 total_ticks;     //claude: 进程总运行时间（用于统计）
  int voluntary_yield;    //claude: 是否主动让出CPU（I/O等待）
};
```

每个 `struct proc` 包含一个 `mlfq` 字段来存储其调度信息。

#### 全局调度器状态（mlfq.h）

```c
struct mlfq_scheduler {
  uint64 boost_timer;     //claude: 距离上次提升的ticks数
  uint64 total_switches;  //claude: 总上下文切换次数（统计用）
  uint64 level_counts[MLFQ_LEVELS];  //claude: 各级别进程数（统计用）
};
```

### 2. 核心函数

#### mlfq_init() - 初始化调度器

```c
void mlfq_init(void)
```

- 初始化全局锁和调度器状态
- 在系统启动时调用（main.c中）
- 打印配置信息用于调试

#### mlfq_add_process() - 添加新进程

```c
void mlfq_add_process(struct proc* p)
```

- 新进程从Level 0（最高优先级）开始
- 分配对应的时间片
- 在 `allocproc()` 中调用

#### mlfq_pick_next() - 选择下一个进程

```c
struct proc* mlfq_pick_next(void)
```

- 从高优先级到低优先级遍历
- 在每个级别中使用轮转调度
- 返回第一个RUNNABLE进程
- 在 `scheduler()` 主循环中调用

#### mlfq_tick() - 时钟中断处理

```c
void mlfq_tick(struct proc* p)
```

- 每个时钟中断调用一次
- 更新进程的时间统计
- 检查是否用完时间片
- 触发周期性优先级提升
- 在 `clockintr()` 中调用

#### mlfq_yield() - 主动让出CPU

```c
void mlfq_yield(struct proc* p)
```

- 标记为主动让出（I/O密集型行为）
- **不降低优先级**（鼓励I/O行为）
- 重置时间片计数
- 在 `yield()` 中调用

#### mlfq_timeslice_expired() - 时间片用完

```c
void mlfq_timeslice_expired(struct proc* p)
```

- 非主动让出的情况下降低优先级
- 表示进程是CPU密集型
- 移动到下一个优先级队列
- 更新时间片配置

#### mlfq_boost_priority() - 优先级提升

```c
void mlfq_boost_priority(void)
```

- 周期性（每100 ticks）执行
- 将所有进程提升到Level 0
- 防止低优先级进程饥饿
- 给所有进程一个"新机会"

### 3. 与原系统的集成

#### 修改的文件

**新增文件**：
- `src/proc/mlfq.h` - MLFQ头文件定义
- `src/proc/mlfq.c` - MLFQ实现代码

**修改文件**：
- `src/proc/proc.h` - 添加 `struct mlfq_proc_info mlfq` 字段
- `src/proc/proc.c` - 修改调度器主循环和相关函数
- `src/trap/trap.c` - 添加时钟中断MLFQ处理
- `src/boot/main.c` - 添加 `mlfq_init()` 调用
- `src/defs.h` - 添加MLFQ函数声明

#### proc.c 修改详情

**scheduler() 函数**（第329-375行）：
```c
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    intr_on();

    //claude: 使用MLFQ算法选择下一个进程
    //claude: MLFQ会从高优先级队列到低优先级队列依次查找RUNNABLE进程
    p = mlfq_pick_next();

    if(p != 0) {
      acquire(&p->lock);

      //claude: 再次检查状态（可能在pick_next之后被其他CPU修改）
      if(p->state == RUNNABLE){
        p->state = RUNNING;
        c->proc = p;

        //claude: 保存调度器上下文，切换到进程上下文
        swtch(&c->context, &p->context);

        c->proc = 0;
      }

      release(&p->lock);
    }
  }
}
```

**yield() 函数**（第379-392行）：
```c
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);

  //claude: 标记为主动让出，MLFQ不会因此降级进程优先级
  //claude: 这样可以让I/O密集型进程保持高优先级
  mlfq_yield(p);

  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}
```

**allocproc() 函数**（第165-166行）：
```c
//claude: 初始化MLFQ调度信息，新进程从最高优先级开始
mlfq_add_process(p);
```

**freeproc() 函数**（第289-292行）：
```c
void freeproc(struct proc *p)
{
  //claude: 从MLFQ调度器中移除进程
  mlfq_remove_process(p);

  // ... 其余清理代码 ...
}
```

#### trap.c 修改详情

**clockintr() 函数**（第376-383行）：
```c
void clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);

  //claude: MLFQ调度：在时钟中断中更新当前进程的时间片
  //claude: 如果进程用完时间片，mlfq_tick会触发降级
  struct proc* p = myproc();
  if(p != 0 && p->state == RUNNING){
    mlfq_tick(p);  //claude: 更新MLFQ时间片统计和可能的降级
  }

  // Schedule next tick.
  sbi_set_timer(r_time() + TICK_CYCLES);
}
```

## 工作流程示例

### 场景1：交互式进程（文本编辑器）

1. **初始**：进程创建，从Level 0开始，时间片=2 ticks
2. **运行**：快速执行少量代码后等待用户输入
3. **I/O等待**：调用 `yield()` → `mlfq_yield()` → 保持Level 0
4. **用户输入**：进程被唤醒，仍在Level 0
5. **结果**：始终保持高优先级，获得快速响应

### 场景2：CPU密集型进程（科学计算）

1. **初始**：Level 0，时间片=2 ticks
2. **运行2 ticks**：用完时间片 → 降级到Level 1（时间片=4）
3. **运行4 ticks**：用完时间片 → 降级到Level 2（时间片=8）
4. **运行8 ticks**：用完时间片 → 降级到Level 3（时间片=16）
5. **稳定**：在Level 3运行，长时间片减少上下文切换

### 场景3：混合负载

假设系统有：
- 进程A（交互式）：Level 0
- 进程B（CPU密集）：Level 3
- 进程C（中等）：Level 1

**调度顺序**：
1. 调度器首先检查Level 0：找到进程A，运行A
2. A主动让出后，再次检查Level 0：A还在Level 0但SLEEPING
3. 检查Level 1：找到进程C，运行C
4. C用完时间片，降级到Level 2
5. 检查Level 2：找到进程C，运行C
6. C用完时间片，降级到Level 3
7. 检查Level 3：找到进程B或C，轮转执行

### 场景4：防止饥饿

假设100 ticks过去了：
1. `mlfq_boost_priority()` 被调用
2. 所有进程（A、B、C）都被提升到Level 0
3. 每个进程都获得一次在高优先级运行的机会
4. 根据各自行为重新分类到合适的优先级

## 性能特性

### 优势

1. **自适应性**：无需预先知道进程类型，自动识别和优化
2. **响应性**：交互式进程获得快速响应（短时间片+高优先级）
3. **吞吐量**：CPU密集型进程在低优先级用长时间片，减少切换开销
4. **公平性**：周期性提升防止任何进程被永久忽视

### 开销

1. **内存开销**：每个进程增加 `mlfq_proc_info` 结构（约32字节）
2. **时间开销**：
   - 每次调度需要遍历进程表（O(n)，n=进程数）
   - 每个tick需要更新当前进程状态（O(1)）
   - 每100 ticks需要提升所有进程（O(n)）

### 与简单优先级调度的对比

| 特性 | 简单优先级 | MLFQ |
|------|-----------|------|
| 优先级调整 | 静态/手动 | 动态/自动 |
| 交互式进程 | 需预先设置高优先级 | 自动识别并优先 |
| CPU密集型 | 可能长期占用CPU | 自动降级，公平分配 |
| 饥饿问题 | 低优先级可能饥饿 | 周期性提升防止饥饿 |
| 复杂度 | 简单 | 中等 |

## 调试和监控

### 调试输出

MLFQ实现包含调试信息输出：

```c
// 初始化时
[MLFQ] Initialized: 4 levels, boost interval=100 ticks
[MLFQ] Time slices: L0=2, L1=4, L2=8, L3=16

// 优先级提升时
[MLFQ] Priority boost: moving all processes to L0

// 进程降级时（可选，已注释）
// [MLFQ] PID %d: L%d -> L%d (CPU-bound)
```

### 统计信息

可调用 `mlfq_print_stats()` 查看：

```c
=== MLFQ Statistics ===
Total context switches: 1234
Ticks since last boost: 45/100

Processes by level:
  Level 0 (slice=2): 3 processes
  Level 1 (slice=4): 2 processes
  Level 2 (slice=8): 1 processes
  Level 3 (slice=16): 2 processes

Active processes:
  PID 1: L0, used=1/2, total=150 ticks
  PID 2: L3, used=12/16, total=8523 ticks
  PID 3: L1, used=2/4, total=421 ticks
=======================
```

## 参数调优

可以通过修改 `mlfq.h` 中的宏定义来调优：

```c
// 增加级别数（更细粒度的分类）
#define MLFQ_LEVELS 5

// 调整时间片（平衡响应性和吞吐量）
#define MLFQ_TIME_SLICE_0 1   // 更短的时间片 → 更快响应
#define MLFQ_TIME_SLICE_3 32  // 更长的时间片 → 更高吞吐量

// 调整提升间隔（平衡饥饿风险和优先级稳定性）
#define MLFQ_BOOST_INTERVAL 200  // 更长间隔 → 更稳定的优先级
```

**权衡考虑**：
- 短时间片：更好的响应性，但更多的上下文切换开销
- 长时间片：更少的切换开销，但响应性降低
- 频繁提升：更好的公平性，但可能打乱已稳定的分类
- 少量提升：更稳定的分类，但增加饥饿风险

## 总结

MLFQ调度器成功实现了：

1. ✅ **多级队列结构**：4个优先级级别，指数级时间片
2. ✅ **自动优先级调整**：根据进程行为自动升降级
3. ✅ **I/O优化**：主动让出不降级，保持交互式进程响应性
4. ✅ **饥饿防止**：周期性优先级提升机制
5. ✅ **与xv6完全集成**：修改最小化，兼容现有代码

该实现参考了现代操作系统（如FreeBSD）的调度算法，在保持简洁的同时提供了良好的性能和公平性。

## 参考资料

- Operating Systems: Three Easy Pieces - Chapter 8: Multi-Level Feedback Queue
- FreeBSD Scheduler Documentation
- Linux CFS (Completely Fair Scheduler) - 类似思想的现代实现
