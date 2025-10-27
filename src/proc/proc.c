#include "riscv.h"
#include "proc.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"

struct proc* initproc;
struct cpu cpus[NCPU];

extern void forkret(void);

extern char trampoline[]; // trampoline.S

// initcode.S 的开始和结尾
extern uchar initcode_start[];
extern uchar initcode_end[];

// 需要关中断，以防止内核切换过程中的险态
int
cpuid()
{
  int id = r_tp();
  return id;
}

struct cpu* mycpu(){

  int my_id = cpuid();

  return &cpus[my_id];
}

struct proc *myproc()
{
  struct cpu *c;
  c = mycpu();
  return c->proc;
}

void userinit(void){

  // allocproc 函数体内，执行所有用户进程创建的一些步骤。
  // TODO: 应该这里面分配 pid
  struct proc *p = allocproc();
  initproc = p;

  uint64 initcode_sz = (uint64)initcode_end - (uint64)initcode_start;
  uint64 alloc_size = uvmfirst(p->pagetable, initcode_start, initcode_sz);

  p->sz = alloc_size;

  // 作为第一个程序，准备到用户空间的第一行代码(pc = 0)
  p->trapframe->epc = 0;      // 用户程序从地址
  p->trapframe->sp = alloc_size; // 用户栈指针初始化为一页大小

  safestrcpy(p->name, "zeroproc", sizeof(p->name));
  // static int nextpid = 0;
  // p->pid = nextpid++;

  // 暂时不调度，直接只跑这第一个程序
  // 设置进程状态为可运行
  // p->state = RUNNABLE;
  // struct proc *p;
  struct cpu *c = mycpu();
  swtch(&c->context, &p->context);  // 上下文切换到进程
}

struct proc* allocproc(void)
{
  struct proc *p;

  // 为单进程实现，直接分配 proc 结构体
  p = (struct proc*)kalloc();
  if (p == 0)
    panic("allocproc: out of memory for proc");
  memset(p, 0, sizeof(*p));

  // 分配 trapframe
  p->trapframe = (struct trapframe*)kalloc();
  if (p->trapframe == 0)
    panic("allocproc: out of memory for trapframe");
  memset(p->trapframe, 0, sizeof(*p->trapframe));

  // 创建并设置用户页表
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
    panic("allocproc: proc_pagetable failed");

  // 分配内核栈
  // @todo 可以考虑放在 procinit 函数中，暂时放在这里
  p->kernel_stack = (uint64)kalloc();
  if (p->kernel_stack == 0)
    panic("allocproc: out of memory for kernel stack");
  p->kernel_stack += PGSIZE;

  // pid
  static int nextpid = 1;
  p->pid = nextpid++;

  // context
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kernel_stack + PGSIZE;

  safestrcpy(p->name, "initcode", sizeof(p->name));

  return p;
}

pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// @todo 暂时忽略锁机制；到进程调度阶段再考虑
// 直接在 trap 中准备返回到 USER 态的环境
void
forkret(void)
{
  // // Still holding p->lock from scheduler.
  // release(&myproc()->lock);

  usertrapret();
}