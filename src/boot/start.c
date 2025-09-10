#include "types.h"
#include "param.h"
// #include "riscv.h"
#include "defs.h"

// entry.S需要为每个CPU分配一个栈空间
// 16字节对齐，总共为NCPU个CPU分配栈空间
// 未赋值的全局变量，放在 bss 段
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// entry.S在机器模式下跳转到此处，使用stack0栈空间
void
start()
{

  uart_puts("hello world!");

  // 时钟芯片进行编程以初始化定时器中断。
  
}
