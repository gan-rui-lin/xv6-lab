//claude: Multi-Level Feedback Queue (MLFQ) 调度算法头文件
#ifndef MLFQ_H
#define MLFQ_H

#include "types.h"

struct proc;

//claude: MLFQ配置参数
#define MLFQ_LEVELS 4              //claude: 优先级队列数量（0最高，3最低）
#define MLFQ_TIME_SLICE_0 2        //claude: 级别0的时间片（ticks）- 交互式进程
#define MLFQ_TIME_SLICE_1 4        //claude: 级别1的时间片
#define MLFQ_TIME_SLICE_2 8        //claude: 级别2的时间片
#define MLFQ_TIME_SLICE_3 16       //claude: 级别3的时间片 - CPU密集型
#define MLFQ_BOOST_INTERVAL 100    //claude: 优先级提升间隔（ticks）- 防止饥饿

//claude: 进程在MLFQ中的状态
struct mlfq_proc_info {
  int level;              //claude: 当前所在的优先级级别（0-3）
  uint64 time_slice;      //claude: 当前级别分配的时间片大小
  uint64 ticks_used;      //claude: 在当前级别已使用的时间片
  uint64 total_ticks;     //claude: 进程总运行时间（用于统计）
  int voluntary_yield;    //claude: 是否主动让出CPU（I/O等待）
};

//claude: MLFQ调度器全局状态
struct mlfq_scheduler {
  uint64 boost_timer;     //claude: 距离上次提升的ticks数
  uint64 total_switches;  //claude: 总上下文切换次数（统计用）
  uint64 level_counts[MLFQ_LEVELS];  //claude: 各级别进程数（统计用）
};

//claude: MLFQ函数接口

//claude: 初始化MLFQ调度器
void mlfq_init(void);

//claude: 将进程加入MLFQ（新创建的进程）
void mlfq_add_process(struct proc* p);

//claude: 从MLFQ中移除进程（进程退出时）
void mlfq_remove_process(struct proc* p);

//claude: 选择下一个要运行的进程（核心调度函数）
struct proc* mlfq_pick_next(void);

//claude: 时钟中断处理 - 更新时间片和可能的降级
void mlfq_tick(struct proc* p);

//claude: 进程主动让出CPU（I/O等待，不降级）
void mlfq_yield(struct proc* p);

//claude: 进程用完时间片（降级）
void mlfq_timeslice_expired(struct proc* p);

//claude: 周期性提升所有进程优先级（防止饥饿）
void mlfq_boost_priority(void);

//claude: 获取指定级别的时间片大小
uint64 mlfq_get_timeslice(int level);

//claude: 打印MLFQ统计信息（调试用）
void mlfq_print_stats(void);

#endif // MLFQ_H
