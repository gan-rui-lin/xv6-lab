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
  
  // S 态无法直接访问 硬件时钟寄存器，需要通过 r_time() 获取
  uint64 cycles = r_time();

  // RISC-V CLINT 时钟频率通常为 10MHz
  // 将时钟周期转换为秒和微秒
  tv.tv_sec = cycles / 10000000;
  tv.tv_usec = (cycles % 10000000) / 10;

  // 复制到用户空间
  if(copyout(myproc()->pagetable, tv_addr, (char*)&tv, sizeof(tv)) < 0)
    return -1;
    
  return 0;
}

