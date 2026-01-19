#include "defs.h"
#include "types.h"
#include "memlayout.h"
#include "errno.h"

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_gettimeofday(void)
{
  uint64 tv_addr;
  struct timeval tv;

  // 获取用户空间指针
  argaddr(0, &tv_addr);

  // 读取时间计数器（cycles）
  uint64 cycles = r_time();

  // QEMU/RISC-V 默认 timebase 常见为 12.5MHz
  // 如需适配其他平台，可将 TIMEBASE 移至配置文件
  const uint64 TIMEBASE = 12500000ULL; // cycles per second

  // 安全换算：
  // 秒 = cycles / TIMEBASE
  // 微秒 = ((cycles % TIMEBASE) * 1_000_000) / TIMEBASE  (保证 < 1_000_000)
  tv.tv_sec  = cycles / TIMEBASE;
  tv.tv_usec = ((cycles % TIMEBASE) * 1000000ULL) / TIMEBASE;


  // 复制到用户空间
  if(copyout(myproc()->pagetable, tv_addr, (char*)&tv, sizeof(tv)) < 0)
    return -1;

  return 0;
}

uint64
sys_clock_gettime(void)
{
  int clock_id;
  uint64 ts_addr;
  struct {
    uint64 tv_sec;
    uint64 tv_nsec;
  } ts;

  if(argint(0, &clock_id) < 0 || argaddr(1, &ts_addr) < 0)
    return -EINVAL;

  // 仅支持 CLOCK_REALTIME(0) 与 CLOCK_MONOTONIC(1)
  if(clock_id != 0 && clock_id != 1)
    return -EINVAL;

  uint64 cycles = r_time();
  const uint64 TIMEBASE = 12500000ULL;
  ts.tv_sec = cycles / TIMEBASE;
  ts.tv_nsec = ((cycles % TIMEBASE) * 1000000000ULL) / TIMEBASE;

  if(copyout(myproc()->pagetable, ts_addr, (char *)&ts, sizeof(ts)) < 0)
    return -EFAULT;
  return 0;
}


uint64
sys_times(){
  // Linux times(): fill user tms if provided, return ticks since start
  uint64 tms_addr;
  struct {
    long tms_utime;
    long tms_stime;
    long tms_cutime;
    long tms_cstime;
  } tms;

  // get buffer address
  argaddr(0, &tms_addr);

  // read current ticks
  uint xticks;
  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);

  // minimal accounting: expose uptime ticks as user time
  tms.tms_utime = (long)xticks;
  tms.tms_stime = 0;
  tms.tms_cutime = 0;
  tms.tms_cstime = 0;

  if (tms_addr != 0) {
    if (copyout(myproc()->pagetable, tms_addr, (char*)&tms, sizeof(tms)) < 0)
      return -1;
  }

  return xticks;
}
