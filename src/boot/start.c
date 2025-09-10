#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "memlayout.h"

// entry.S需要为每个CPU分配一个栈空间
// 16字节对齐，总共为NCPU个CPU分配栈空间
// 未赋值的全局变量，放在 bss 段
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// 每个CPU的机器模式定时器中断的临时存储区域
// 每个CPU需要5个64位字的空间来保存中断处理时的上下文
uint64 timer_scratch[NCPU][5];

// kernelvec.S中的汇编代码，用于处理机器模式的定时器中断
extern void timervec();

void main();
void timer_init();

// entry.S在机器模式下跳转到此处，使用stack0栈空间
void
start()
{

  uart_puts("hello world!");

   // 设置M模式下的前一特权级为管理者模式(Supervisor)，供mret指令使用
  // 当mret执行时，会切换到管理者模式继续执行
  // RV64 的 mstatus 是 40 位，RV32 32 位
  unsigned long x = r_mstatus(); 

  // MPP 有两位，记录先前特权级
  x &= ~MSTATUS_MPP_MASK;  // 清除MPP位域
  x |= MSTATUS_MPP_S;      // 设置MPP为管理者模式 01 表示 S 模式

  // 写 mstatus
  w_mstatus(x);

  // 设置M模式异常程序计数器指向main函数，供mret指令使用
  // 需要编译时使用gcc -mcmodel=medany选项
  // mret 返回到 main 函数
  w_mepc((uint64)main);

  // 暂时禁用分页机制
  w_satp(0);

  // RISC-V 提供了一种异常委托机制。通过该机制可以选择性地将中
  // 断和同步异常交给 S 模式处理，而完全绕过 M 模式。

  // 将所有中断和异常委托给管理者模式处理
  w_medeleg(0xffff);  // 异常委托
  w_mideleg(0xffff);  // 中断委托
  // 启用管理者模式的外部中断、定时器中断和软件中断
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // 这些不可信的代码还必须被限制只能访问自己那部分内存。实现了 M 和 U 模式的处理器
  // 具有一个叫做物理内存保护（PMP，Physical Memory Protection）的功能，允许 M 模式
  // 指定 U 模式可以访问的内存地址。PMP 包括几个地址寄存器（通常为 8 到 16 个）和相应的配置寄存器。
  // 这些配置寄存器可以授予或拒绝读、写和执行权限。
  // 当处于 U 模式的处理器尝试取指或执行 load 或 store 操作时，将地址和所有的 PMP 地址寄存器比较。
  // 如果地址大于等于 PMP 地址 i，但小于 PMP 地址 i+1，则 PMP i+1 的配置寄存器决定该访问是否可以继续，
  // 如果不能将会引发访问异常

  // 配置物理内存保护(PMP)，给予管理者模式访问全部物理内存的权限
  //  0 到 pmpaddr0 的范围，由 pmpcfg0 控制
  w_pmpaddr0(0x3fffffffffffffull);  // 设置PMP地址范围
  w_pmpcfg0(0xf);                   // 设置PMP配置(读写执行权限)

  // 时钟芯片进行编程以初始化定时器中断。
  timer_init();

  // 保存 mhartid 到 tp
  w_tp(r_mhartid()) ;

  // 切换到管理者模式并跳转到main()函数(回到 MPP 所指示的特权集)
  asm volatile("mret");

  
}

// 定时器中断可能发生在用户或内核代码执行的任何时候；内核没有办法在关键操作中禁
// 用定时器中断。因此，定时器中断处理程序必须以保证不干扰被中断的内核代码的方式进行
// 工作。基本策略是处理程序要求 RISC-V 引发一个软件中断并立即返回。RISC-V 用普通的
// trap 机制将软件中断传递给内核，并允许内核禁用它们。

void timer_init(){

  // 每个CPU都有独立的定时器中断源
  int id = r_mhartid();

  int cycles = 1000000;
  // 每个CPU核有独立的mtimecmp寄存器，地址是基地址加上核编号乘以8（因为每个寄存器8字节)
  // 再走 cycles 时间，触发定时器中断
  *(uint64*)CLINT_MTIMECMP(id) = *(uint64*)CLINT_MTIME + cycles;

  // 在scratch[]中为timervec准备信息
  // scratch[0..2] : timervec保存寄存器的空间
  // scratch[3] : CLINT MTIMECMP寄存器地址
  // scratch[4] : 定时器中断之间期望的间隔(周期数)
  uint64 *scratch = &timer_scratch[id][0];
  scratch[3] = CLINT_MTIMECMP(id);
  scratch[4] = cycles;
  // 存放首地址
  w_mscratch((uint64)scratch);

  // 为避免覆盖整数寄存器中的内容，中断处理程序先在最开始用 mscratch 和整数
  // 寄存器（例如 a0）中的值交换。通常，软件会让 mscratch 包含指向附加临时内存空
  // 间的指针，处理程序用该指针来保存其主体中将会用到的整数寄存器。在主体执行之
  // 后，中断程序会恢复它保存到内存中的寄存器，然后再次使用 mscratch 和 a0 交换，
  // 将两个寄存器恢复到它们在发生异常之前的值。最后，处理程序用 mret 指令（M 模
  // 式特有的指令）返回。

  // 设置机器模式的陷阱处理程序
  w_mtvec((uint64)timervec);

  // 启用机器模式中断
  w_mstatus(r_mstatus() | MSTATUS_MIE);

  // 启用机器模式定时器中断
  w_mie(r_mie() | MIE_MTIE);

}