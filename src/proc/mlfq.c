//claude: Multi-Level Feedback Queue (MLFQ) 调度算法实现
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc/proc.h"
#include "proc/mlfq.h"
#include "defs.h"

//claude: MLFQ全局调度器状态
static struct mlfq_scheduler mlfq_sched;

//claude: 保护MLFQ数据结构的锁
static struct spinlock mlfq_lock;

//claude: 初始化MLFQ调度器
void
mlfq_init(void)
{
  initlock(&mlfq_lock, "mlfq");  //claude: 初始化锁

  //claude: 初始化全局状态
  mlfq_sched.boost_timer = 0;
  mlfq_sched.total_switches = 0;
  for(int i = 0; i < MLFQ_LEVELS; i++){
    mlfq_sched.level_counts[i] = 0;
  }

  log_info("[MLFQ] Initialized: %d levels, boost interval=%d ticks\n",
         MLFQ_LEVELS, MLFQ_BOOST_INTERVAL);
  log_info("[MLFQ] Time slices: L0=%d, L1=%d, L2=%d, L3=%d\n",
         MLFQ_TIME_SLICE_0, MLFQ_TIME_SLICE_1,
         MLFQ_TIME_SLICE_2, MLFQ_TIME_SLICE_3);
}

//claude: 获取指定级别的时间片大小
uint64
mlfq_get_timeslice(int level)
{
  switch(level){
    case 0: return MLFQ_TIME_SLICE_0;  //claude: 最高优先级，短时间片（交互式）
    case 1: return MLFQ_TIME_SLICE_1;
    case 2: return MLFQ_TIME_SLICE_2;
    case 3: return MLFQ_TIME_SLICE_3;  //claude: 最低优先级，长时间片（CPU密集）
    default: return MLFQ_TIME_SLICE_3;
  }
}

//claude: 将新进程加入MLFQ（从最高优先级开始）
void
mlfq_add_process(struct proc* p)
{
  if(p == 0) return;

  acquire(&mlfq_lock);

  //claude: 新进程从最高优先级（level 0）开始
  p->mlfq.level = 0;
  p->mlfq.time_slice = mlfq_get_timeslice(0);
  p->mlfq.ticks_used = 0;
  p->mlfq.total_ticks = 0;
  p->mlfq.voluntary_yield = 0;

  mlfq_sched.level_counts[0]++;  //claude: 更新统计

  release(&mlfq_lock);
}

//claude: 从MLFQ中移除进程
void
mlfq_remove_process(struct proc* p)
{
  if(p == 0) return;

  acquire(&mlfq_lock);

  //claude: 更新统计信息
  if(p->mlfq.level >= 0 && p->mlfq.level < MLFQ_LEVELS){
    if(mlfq_sched.level_counts[p->mlfq.level] > 0)
      mlfq_sched.level_counts[p->mlfq.level]--;
  }

  release(&mlfq_lock);
}

//claude: 选择下一个要运行的进程（MLFQ核心调度逻辑）
struct proc*
mlfq_pick_next(void)
{
  struct proc* p;
  struct proc* selected = 0;

  //claude: 从高优先级到低优先级遍历
  //claude: 在每个级别中，使用轮转（Round-Robin）选择
  for(int level = 0; level < MLFQ_LEVELS; level++){
    for(p = proc; p < &proc[NPROC]; p++){
      //claude: 检查进程是否可运行且在当前级别
      if(p->state == RUNNABLE && p->mlfq.level == level){
        //claude: 找到第一个匹配的进程就返回（简单轮转）
        //claude: 更复杂的实现可以记住上次调度的位置
        selected = p;
        goto found;
      }
    }
  }

found:
  return selected;  //claude: 返回选中的进程，没有则返回0
}

//claude: 时钟中断处理 - 每个tick调用一次
void
mlfq_tick(struct proc* p)
{
  if(p == 0 || p->state != RUNNING) return;

  acquire(&mlfq_lock);

  //claude: 更新进程的时间统计
  p->mlfq.ticks_used++;
  p->mlfq.total_ticks++;

  //claude: 更新全局boost计时器
  mlfq_sched.boost_timer++;

  //claude: 检查是否需要周期性提升所有进程优先级
  if(mlfq_sched.boost_timer >= MLFQ_BOOST_INTERVAL){
    release(&mlfq_lock);
    mlfq_boost_priority();  //claude: 执行优先级提升（会重新获取锁）
    return;
  }

  //claude: 检查当前进程是否用完时间片
  if(p->mlfq.ticks_used >= p->mlfq.time_slice){
    release(&mlfq_lock);
    mlfq_timeslice_expired(p);  //claude: 时间片用完，可能降级
  } else {
    release(&mlfq_lock);
  }
}

//claude: 进程主动让出CPU（I/O等待等）- 不降级
void
mlfq_yield(struct proc* p)
{
  if(p == 0) return;

  acquire(&mlfq_lock);

  //claude: 标记为主动让出（I/O密集型行为）
  p->mlfq.voluntary_yield = 1;

  //claude: 重置时间片计数（但不降级）
  //claude: 这鼓励I/O密集型进程保持高优先级
  p->mlfq.ticks_used = 0;

  release(&mlfq_lock);
}

//claude: 进程用完时间片 - 可能降级
void
mlfq_timeslice_expired(struct proc* p)
{
  if(p == 0) return;

  acquire(&mlfq_lock);

  //claude: 如果是主动让出，不降级
  if(p->mlfq.voluntary_yield){
    p->mlfq.voluntary_yield = 0;
    p->mlfq.ticks_used = 0;
    release(&mlfq_lock);
    return;
  }

  //claude: 用完时间片表示是CPU密集型，降低优先级
  int old_level = p->mlfq.level;

  if(p->mlfq.level < MLFQ_LEVELS - 1){
    //claude: 降到下一级
    mlfq_sched.level_counts[old_level]--;
    p->mlfq.level++;
    mlfq_sched.level_counts[p->mlfq.level]++;

    //claude: 更新时间片配置
    p->mlfq.time_slice = mlfq_get_timeslice(p->mlfq.level);

    //claude: 调试输出（可选）
    // printf("[MLFQ] PID %d: L%d -> L%d (CPU-bound)\n",
    //        p->pid, old_level, p->mlfq.level);
  }

  //claude: 重置时间片计数
  p->mlfq.ticks_used = 0;

  release(&mlfq_lock);
}

//claude: 周期性提升所有进程优先级（防止饥饿）
void
mlfq_boost_priority(void)
{
  struct proc* p;

  acquire(&mlfq_lock);

  //claude: 重置boost计时器
  mlfq_sched.boost_timer = 0;

  log_info("[MLFQ] Priority boost: moving all processes to L0\n");

  //claude: 遍历所有进程，提升到最高优先级
  for(p = proc; p < &proc[NPROC]; p++){
    //claude: 只处理有效的进程
    if(p->state != UNUSED && p->state != ZOMBIE){
      //claude: 更新统计
      if(p->mlfq.level > 0 && p->mlfq.level < MLFQ_LEVELS){
        if(mlfq_sched.level_counts[p->mlfq.level] > 0)
          mlfq_sched.level_counts[p->mlfq.level]--;
      }

      //claude: 提升到最高优先级
      p->mlfq.level = 0;
      p->mlfq.time_slice = mlfq_get_timeslice(0);
      p->mlfq.ticks_used = 0;

      //claude: 更新统计
      mlfq_sched.level_counts[0]++;
    }
  }

  release(&mlfq_lock);
}

//claude: 打印MLFQ统计信息（调试和性能分析）
void
mlfq_print_stats(void)
{
  acquire(&mlfq_lock);

  log_info("\n=== MLFQ Statistics ===\n");
  log_info("Total context switches: %d\n", mlfq_sched.total_switches);
  log_info("Ticks since last boost: %d/%d\n",
         mlfq_sched.boost_timer, MLFQ_BOOST_INTERVAL);

  log_info("\nProcesses by level:\n");
  for(int i = 0; i < MLFQ_LEVELS; i++){
    log_info("  Level %d (slice=%d): %d processes\n",
           i, mlfq_get_timeslice(i), mlfq_sched.level_counts[i]);
  }

  log_info("\nActive processes:\n");
  struct proc* p;
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == RUNNING || p->state == RUNNABLE){
      log_info("  PID %d: L%d, used=%d/%d, total=%d ticks\n",
             p->pid, p->mlfq.level, p->mlfq.ticks_used,
             p->mlfq.time_slice, p->mlfq.total_ticks);
    }
  }

  log_info("=======================\n\n");

  release(&mlfq_lock);
}
