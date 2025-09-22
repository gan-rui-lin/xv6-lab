#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "memlayout.h"

// 启动阶段的全局变量
// entry.S需要为每个CPU分配一个栈空间
// 16字节对齐，总共为NCPU个CPU分配栈空间
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// 每个CPU的机器模式定时器中断的临时存储区域
// 每个CPU需要5个64位字的空间来保存中断处理时的上下文
uint64 timer_scratch[NCPU][5];

// 外部声明
extern void timervec();
void main();

// 内部函数声明
static void setup_supervisor_mode(void);
static void setup_memory_protection(void);
static void setup_interrupt_delegation(void);
static void timer_init(void);
static void setup_timer_scratch(int cpu_id, int timer_interval);

// entry.S在机器模式下跳转到此处，完成从M模式到S模式的转换
void
start()
{
  // 设置管理者模式(Supervisor Mode)相关配置
  setup_supervisor_mode();
  
  // 配置中断委托机制
  setup_interrupt_delegation();
  
  // 配置物理内存保护
  setup_memory_protection();
  
  // 初始化定时器中断
  timer_init();
  
  // 保存当前CPU ID到tp寄存器
  w_tp(r_mhartid());
  
  // 切换到管理者模式并跳转到main()函数
  asm volatile("mret");
}

// 配置管理者模式相关设置
static void 
setup_supervisor_mode(void)
{
  // 设置M模式下的前一特权级为管理者模式(Supervisor)，供mret指令使用
  // 当mret执行时，会切换到管理者模式继续执行
  unsigned long mstatus = r_mstatus();
  
  // MPP字段有两位，记录先前特权级
  mstatus &= ~MSTATUS_MPP_MASK;  // 清除MPP位域
  mstatus |= MSTATUS_MPP_S;      // 设置MPP为管理者模式(01表示S模式)
  
  w_mstatus(mstatus);
  
  // 设置M模式异常程序计数器指向main函数，供mret指令使用
  w_mepc((uint64)main);
  
  // 暂时禁用分页机制(因为此时虚拟内存未启用)
  w_satp(0);
}

// 配置中断和异常委托机制
static void 
setup_interrupt_delegation(void)
{
  // RISC-V异常委托机制：将中断和异常委托给S模式处理，绕过M模式
  
  // 将所有异常委托给管理者模式处理
  w_medeleg(0xffff);
  
  // 将所有中断委托给管理者模式处理
  w_mideleg(0xffff);
  
  // 启用管理者模式的中断：外部中断、定时器中断和软件中断
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
}

// 配置物理内存保护(PMP)
static void 
setup_memory_protection(void)
{
  // 物理内存保护功能允许M模式限制U模式可访问的内存地址
  // PMP包括地址寄存器和配置寄存器，控制读写执行权限
  
  // 设置PMP地址范围：0到pmpaddr0的范围
  w_pmpaddr0(0x3fffffffffffffull);
  
  // 设置PMP配置：给予读写执行权限(0xf = 1111二进制)
  w_pmpcfg0(0xf);
}
// 初始化定时器中断
// 定时器中断的处理策略：M模式定时器中断触发S模式软件中断
// 这样可以避免在内核关键操作中被定时器中断直接打断
static void 
timer_init(void)
{
  int cpu_id = r_mhartid();
  int timer_interval = 1000000;  // 定时器间隔(CPU周期数)
  
  // 设置下一次定时器中断的时间
  // 每个CPU核有独立的mtimecmp寄存器
  *(uint64*)CLINT_MTIMECMP(cpu_id) = *(uint64*)CLINT_MTIME + timer_interval;
  
  // 为timervec准备scratch内存区域
  setup_timer_scratch(cpu_id, timer_interval);
  
  // 设置机器模式的陷阱处理程序
  w_mtvec((uint64)timervec);
  
  // 启用机器模式中断
  w_mstatus(r_mstatus() | MSTATUS_MIE);
  
  // 启用机器模式定时器中断
  w_mie(r_mie() | MIE_MTIE);
}

// 设置定时器中断的scratch内存区域
static void
setup_timer_scratch(int cpu_id, int timer_interval)
{
  // timervec使用mscratch寄存器来保存临时数据
  // scratch数组布局：
  // [0-2]: 保存寄存器的空间
  // [3]:   CLINT MTIMECMP寄存器地址
  // [4]:   定时器中断间隔
  
  uint64 *scratch = &timer_scratch[cpu_id][0];
  scratch[3] = CLINT_MTIMECMP(cpu_id);  // 定时器比较寄存器地址
  scratch[4] = timer_interval;          // 中断间隔
  
  // 将scratch数组地址存储到mscratch寄存器
  w_mscratch((uint64)scratch);
}