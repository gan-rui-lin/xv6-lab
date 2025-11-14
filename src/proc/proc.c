#include "riscv.h"
#include "proc.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"

static int nextpid = 1;  // 下一个分配的 PID
struct proc* initproc;  // 用于 reparent 子进程等操作
struct cpu cpus[NCPU];
struct proc proc[NPROC]; // 进程表

extern void forkret(void);

extern char trampoline[]; // trampoline.S

// initcode.S 的开始和结尾
extern uchar initcode_start[];
extern uchar initcode_end[];

// 保证多个进程分配时对 nextpid 的互斥访问
struct spinlock pid_lock;

// 防止 wait()/exit() 之间的竞争条件，确保唤醒不会丢失
// 临界区数据：子进程的状态 ZOMBIE 或者 非 ZOMBIE
struct spinlock wait_lock;

static void freeproc(struct proc *p);

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

// 初始化进程表
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kernel_stack = KSTACK((int) (p - proc));
  }
}

void userinit(void){

  // allocproc 函数体内，执行所有用户进程创建的一些步骤。
  // allocproc 本身没有释放锁，因为对应进程并没有初始化好
  struct proc *p = allocproc();
  initproc = p;

  uint64 initcode_sz = (uint64)initcode_end - (uint64)initcode_start;

  // 映射用户代码到进程的用户页表
  uint64 alloc_size = uvmfirst(p->pagetable, initcode_start, initcode_sz);

  p->sz = alloc_size;

  // 作为第一个程序，准备到用户空间的第一行代码(pc = 0)
  // 其它程序的值在用户空间内提前设置好了
  p->trapframe->epc = 0;      // 用户程序从地址
  p->trapframe->sp = alloc_size; // 用户栈指针初始化为一页大小

  safestrcpy(p->name, "zeroproc", sizeof(p->name));
  // static int nextpid = 0;
  // p->pid = nextpid++;

  // 暂时不调度，直接只跑这第一个程序
  // 设置进程状态为可运行
  p->state = RUNNABLE;

  // 释放进程锁，允许调度器调度该进程
  release(&p->lock);
}

int
allocpid()
{
  // pid 不是共享变量，可以安全返回
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// 单进程实现使用的内核栈，已注释掉
// __attribute__ ((aligned (16))) char proc0stack[8192];

struct proc* allocproc(void)
{
  struct proc *p;

  // 寻找可使用的空进程表项
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:  

  p->pid = allocpid();
  p->state = USED; //TODO 有必要吗？


  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 创建并设置用户页表
  p->pagetable = proc_pagetable(p);
  printf("proc pagetable: %p\n", p->pagetable);
  if (p->pagetable == 0) {
    // panic("allocproc: proc_pagetable failed");
    // 不直接 panic，调用者负责错误处理
    freeproc(p);
    release(&p->lock);
    return 0;

  }
    
  printf("proc %d: pagetable at %p\n", p->pid, p->pagetable);

  printf("proc %d: trapframe at %p\n", p->pid, p->trapframe);

  printf("proc %d: kernel stack at %p\n", p->pid, p->kernel_stack);

  // context
  memset(&p->context, 0, sizeof(p->context));
  // swtch 函数会从 context 的 ra 和 sp 字段恢复
  // forkret -> usertrapret -> userret
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kernel_stack + PGSIZE;

  printf("proc %d: context at %p\n", p->pid, p->context.sp);
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
              (uint64)trampoline, PTE_R | PTE_X | PTE_V) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }
  // printf("mapped trampoline at %p\n", TRAMPOLINE);
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
  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  usertrapret();
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// 释放内存，清理进程表项
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  // 高地址单独 unmap
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  // 从 0 开始 unmap
  uvmfree(pagetable, sz);
}

// 进行进程调度
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  // myproc() 暂时返回 NULL
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        // myproc() 返回当前运行的进程
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }

}