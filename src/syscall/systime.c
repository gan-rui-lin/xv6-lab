#include "defs.h"
#include "types.h"
#include "memlayout.h"

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

