#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "proc.h"

#ifdef TICKER_DEBUG
volatile static int ticker = 1; // 用于调试的 ticker 变量
#endif

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

extern struct proc *initproc;

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();
// forward declare trapframe and exception handler prototype so
// kerneltrap can call handle_exception without implicit-declaration warnings.
struct trapframe;
void handle_exception(struct trapframe *tf);

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// // set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // 内核态仍然是交给 kernelvec 函数处理，内核态陷入(kerneltrap)
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // 需要手动更改 epc（并非硬件设置！）
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    // printf("INFO: syscall\n");

    syscall_handler();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2){
    // log_info("usertrap: yield\n");
    yield();
  }
    

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  // 单进程方式：直接调度到第一个进程上
  // struct proc *p = initproc;
  // 多进程方式
  struct proc * p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // 提前在 S 态设置好 stvec，指向 trampoline 中的 uservec
  // 但是先保证关中断
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // 在 trapframe 中设置内核态相关字段
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kernel_stack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // 设置 SPP = 0，表示返回用户态
  x |= SSTATUS_SPIE; // SPIE 在 sret指令执行时恢复​ SIE 的值。
  w_sstatus(x);

  // 返回用户态对应代码 pc，继续执行
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // printf("pagetable:%p satp:%p paddr:%p\n", p->pagetable, satp, walkaddr(p->pagetable,TRAMPOLINE));

  // userret 也是 trampoline 里的一段代码
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);

  // 跳转至 userret，传入用户页表的 satp(a0 寄存器)
  // userret 会切换到用户页表并执行 sret 返回用户态
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // printf("scause %p\n", scause);
    // printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    // 如果不是外设中断，交由通用异常处理器处理
    // 这里没有用户 trapframe，因此传入 NULL
    handle_exception((struct trapframe *)0);
  }

  // 目前的内核 trap 只有时钟中断
  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING){
    // printf("yield from kerneltrap\n");
    yield();
  }

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

// 简单异常处理器：根据 scause 分发并在当前实现中打印信息后 panic。
// 你可以扩展这些处理函数来做更精细的恢复/错误码处理。
struct trapframe; // 前向声明（trapframe 在 proc.h 中有定义）

static void
print_trap_info(const char *tag)
{
  printf("%s: scause=%p sepc=%p stval=%p\n", tag, r_scause(), r_sepc(), r_stval());
}

void
handle_syscall(struct trapframe *tf)
{
  print_trap_info("syscall");
  // 现在简单处理：打印并 panic
  panic("syscall handler not implemented");
}

void
handle_instruction_page_fault(struct trapframe *tf)
{
  print_trap_info("instruction page fault");
  panic("instruction page fault");
}

void
handle_load_page_fault(struct trapframe *tf)
{
  print_trap_info("load page fault");
  panic("load page fault");
}

void
handle_store_page_fault(struct trapframe *tf)
{
  print_trap_info("store page fault");
  panic("store page fault");
}

void
handle_exception(struct trapframe *tf)
{
  uint64 cause = r_scause();

  switch(cause){
  case ECODE_SYSCALL: // 用户模式 ecall
    handle_syscall(tf);
    break;
  case ECODE_INSTRUCTION_PAGE_FAULT: // instruction page fault
    handle_instruction_page_fault(tf);
    break;
  case ECODE_LOAD_PAGE_FAULT: // load page fault
    handle_load_page_fault(tf);
    break;
  case ECODE_STORE_PAGE_FAULT: // store page fault
    handle_store_page_fault(tf);
    break;
  case ECODE_ILLEGAL_INSTRUCTION:
    printf("handle_exception: illegal instruction\n");
    panic("illegal instruction");
    break;
  default:
    printf("handle_exception: unknown cause %p\n, sepc=%p, stval=%p\n", cause, r_sepc(), r_stval());
    panic("Unknown exception");
  }
}

void
clockintr()
{
  acquire(&tickslock);
  #ifdef TICKER_DEBUG
  printf("%d tick from cpu %d\n", ++ticker, cpuid());
  #endif
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      // 处理来自 SHELL 的键盘输入
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      // TODO: 处理磁盘中断
      virtio_disk_intr(irq - VIRTIO0_IRQ);
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.
    // 通过机器级的时钟中断(timervec)触发的 S 级的软件中断(sip[1] = 0)

    // 只在 CPU0 下处理时钟中断
    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

