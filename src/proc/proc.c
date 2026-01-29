#include "riscv.h"
#include "proc.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "sleeplock.h"
#include "errno.h"
#include "mm/vma.h"
#include "proc/mlfq.h"  //claude: MLFQ调度算法
#include "../fs/fs.h"
#include "../fs/file.h"

// clone flags used in proc.c (keep in sync with sysproc.c)
#define CLONE_SETTLS        0x00080000ULL
#define CLONE_CHILD_CLEARTID 0x00200000ULL
#define CLONE_CHILD_SETTID  0x01000000ULL

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

// Changed from static to allow use in sys_wait4
void freeproc(struct proc *p);

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

  // ?多余的 vminithart
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

  p->cwd = namei("/");
  safestrcpy(p->cwdpath, "/", sizeof(p->cwdpath));

  safestrcpy(p->name, "zeroproc", sizeof(p->name));

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

// // 单进程实现使用的内核栈，已注释掉
// // __attribute__ ((aligned (16))) char proc0stack[8192];

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
  p->state = USED;
  p->priority = PRIO_DEFAULT;  // 设置默认优先级
  p->clear_child_tid = 0;
  p->exit_signal = SIGCHLD;
  p->robust_list_head = 0;
  p->robust_list_len = 0;
  // 初始化信号相关状态
  signal_init(p);
  memset(p->ofile, 0, sizeof(p->ofile));
  memset(p->fdflags, 0, sizeof(p->fdflags));
  vma_init(p);

  // 初始化共享内存附加表
  for(int i = 0; i < 16; i++) {  // SHM_MAX_ATTACH
    p->shm_attach[i].valid = 0;
  }
  p->uid = 0;  // root user
  p->gid = 0;  // root group

  //claude: 初始化MLFQ调度信息，新进程从最高优先级开始
  mlfq_add_process(p);

  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 创建并设置用户页表
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0) {
    // panic("allocproc: proc_pagetable failed");
    // 不直接 panic，调用者负责错误处理
    freeproc(p);
    release(&p->lock);
    return 0;

  }
  #ifdef LOG_DEBUG
    
  log_debug("proc %d: created pagetable at %p\n", p->pid, p->pagetable);

  log_debug("proc %d: trapframe at %p\n", p->pid, p->trapframe);

  log_debug("proc %d: kernel stack at %p\n", p->pid, p->kernel_stack);
  #endif
  // context
  memset(&p->context, 0, sizeof(p->context));
  // swtch 函数会从 context 的 ra 和 sp 字段恢复
  // forkret -> usertrapret -> userret
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kernel_stack + KSTACK_SIZE;

  #ifdef LOG_DEBUG
  log_debug("proc %d: context at %p\n", p->pid, p->context.sp);
  #endif
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


void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // 文件系统初始化函数 fsinit()内部会调用 sleep()函数。而 sleep()函数正常工作需要一个已完全初始化的进程环境
    first = 0;
    fsinit(minor(ROOTDEV));

  }

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
// Changed from static to allow use in sys_wait4
void
freeproc(struct proc *p)
{
  //claude: 从MLFQ调度器中移除进程
  mlfq_remove_process(p);

  if(p->vma)
    vma_free_all(p);
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
  p->priority = PRIO_DEFAULT;  // 重置优先级
  p->clear_child_tid = 0;
  p->exit_signal = SIGCHLD;
  p->is_kthread = 0;
  p->kthread_fn = 0;
  p->kthread_arg = 0;
  signal_init(p);
  memset(p->ofile, 0, sizeof(p->ofile));
  memset(p->fdflags, 0, sizeof(p->fdflags));
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

// 进行进程调度 - 优先级调度
// 选择优先级最高（priority值最小）的RUNNABLE进程运行
void
//claude: MLFQ调度器主循环
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  //claude: myproc() 暂时返回 NULL
  c->proc = 0;
  for(;;){
    //claude: Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    //claude: 使用MLFQ算法选择下一个进程
    //claude: MLFQ会从高优先级队列到低优先级队列依次查找RUNNABLE进程
    p = mlfq_pick_next();

    //claude: 如果找到了可运行的进程，切换到它
    if(p != 0) {
      acquire(&p->lock);

      //claude: 再次检查状态（可能在pick_next之后被其他CPU修改）
      if(p->state == RUNNABLE){
        //claude: Switch to chosen process.  It is the process's job
        //claude: to release its lock and then reacquire it
        //claude: before jumping back to us.
        p->state = RUNNING;

        //claude: myproc() 返回当前运行的进程
        c->proc = p;

        //claude: 保存调度器上下文，切换到进程上下文
        //claude: 之后通过 mycpu()->context 切换回调度器
        swtch(&c->context, &p->context);

        //claude: Process is done running for now.
        //claude: It should have changed its p->state before coming back.
        c->proc = 0;
      }

      release(&p->lock);
    }
  }
}

//claude: Give up the CPU for one scheduling round.
//claude: 主动让出CPU（I/O等待等），在MLFQ中不会降级
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);

  //claude: 标记为主动让出，MLFQ不会因此降级进程优先级
  //claude: 这样可以让I/O密集型进程保持高优先级
  mlfq_yield(p);

  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    uint64 va = KSTACK((int) (p - proc));
    for(int i = 0; i < KSTACK_PAGES; i++){
      char *pa = kalloc();
      if(pa == 0)
        panic("kalloc");
      kvmmap(kpgtbl, va + i * PGSIZE, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
  }

}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  // int i, pid;
  int pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Debug: check parent's page table before uvmcopy
  pte_t *parent_pte_before = walk(p->pagetable, 0x7fa78, 0);
  log_debug("[fork] parent pid=%d sz=%p, pte@0x7fa78 before=%p\n",
         p->pid, p->sz, parent_pte_before ? *parent_pte_before : 0);
  log_debug("[fork] parent pagetable=%p, child pagetable=%p\n",
         p->pagetable, np->pagetable);

  // 复制页表和物理页内容
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // Debug: check parent's page table after uvmcopy
  pte_t *parent_pte_after = walk(p->pagetable, 0x7fa78, 0);
  log_debug("[fork] parent pid=%d pte@0x7fa78 after=%p\n",
         p->pid, parent_pte_after ? *parent_pte_after : 0);
  if(vma_copy(np, p) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);
  // 继承信号处理设置（pending 不继承）
  signal_copy(np, p);

  // fork 语义：默认 SIGCHLD，清除 clear_child_tid
  np->exit_signal = SIGCHLD;
  np->clear_child_tid = 0;

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  int i;
  for(i = 0; i < NOFILE; i++){
    if(p->ofile[i]){
      np->ofile[i] = filedup(p->ofile[i]);
      np->fdflags[i] = p->fdflags[i];
    } else {
      np->fdflags[i] = 0;
    }
  }
  np->cwd = idup(p->cwd);
  safestrcpy(np->cwdpath, p->cwdpath, sizeof(np->cwdpath));

  safestrcpy(np->name, p->name, sizeof(p->name));

  // 子进程继承父进程的优先级
  np->priority = p->priority;

  pid = np->pid;

  release(&np->lock);

  // wait_lock 保护 parent 字段
  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  // 设置子进程状态为可运行
  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Clone version of fork with custom stack support
// If stack != 0, sets the child's stack pointer to the provided value
int
clone_fork(uint64 stack, uint64 flags, uint64 tls, uint64 ctid, int exit_signal)
{
  int pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Debug: check parent's page table before uvmcopy
  pte_t *parent_pte_before = walk(p->pagetable, 0x7fa78, 0);
  log_debug("[clone_fork] parent pid=%d sz=%p, pte@0x7fa78 before=%p\n",
         p->pid, p->sz, parent_pte_before ? *parent_pte_before : 0);

  // 复制页表和物理页内容
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // Debug: check parent's page table after uvmcopy
  pte_t *parent_pte_after = walk(p->pagetable, 0x7fa78, 0);
  log_debug("[clone_fork] parent pid=%d pte@0x7fa78 after=%p\n",
         p->pid, parent_pte_after ? *parent_pte_after : 0);
  if(vma_copy(np, p) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);
  // 继承信号处理设置（pending 不继承）
  signal_copy(np, p);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;
  
  // If a custom stack was provided, update the child's stack pointer
  // This must be done BEFORE making the child RUNNABLE
  if(stack != 0) {
    np->trapframe->sp = stack;
  }

  // 设置线程本地存储 (RISC-V tp)
  if(flags & CLONE_SETTLS){
    np->trapframe->tp = tls;
  }

  // 记录退出信号与 clear_child_tid
  np->exit_signal = exit_signal;
  if(flags & CLONE_CHILD_CLEARTID){
    np->clear_child_tid = ctid;
  } else {
    np->clear_child_tid = 0;
  }

  // 在子进程地址空间写入 tid（CLONE_CHILD_SETTID）
  if((flags & CLONE_CHILD_SETTID) && ctid != 0){
    if(copyout(np->pagetable, ctid, (char *)&np->pid, sizeof(np->pid)) < 0){
      freeproc(np);
      release(&np->lock);
      return -1;
    }
  }

  // increment reference counts on open file descriptors.
  int i;
  for(i = 0; i < NOFILE; i++){
    if(p->ofile[i]){
      np->ofile[i] = filedup(p->ofile[i]);
      np->fdflags[i] = p->fdflags[i];
    } else {
      np->fdflags[i] = 0;
    }
  }
  np->cwd = idup(p->cwd);
  safestrcpy(np->cwdpath, p->cwdpath, sizeof(np->cwdpath));

  safestrcpy(np->name, p->name, sizeof(p->name));

  // 子进程继承父进程的优先级
  np->priority = p->priority;

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
// 执行 wait 的进程 p 被 wakeup 的 **前提** 是自己进入 SLEEPING 状态，这与子进程 pp 进入 ZOMBIE 状态（比如 exit)是**两个独立的动作**，需要通过 wait_lock 锁来保证这两个动作的有序性，防止 lost wakeup 问题的出现。

// 1.父进程扫描进程表，但是没有发现ZOMBIE态的子进程，准备进入休眠。但是在安全进入SLEEPING状态之前，子进程就执行了wakeup操作，父进程在此wakeup操作发生之后才进入了SLEEPING状态，导致错过了wakeup动作。

// 2.父进程扫描进程表，但是没有发现ZOMBIE态的进程，且已经安全进入了SLEEPING状态。此时子进程唤醒了父进程，由于缺乏wait_lock锁的保护，醒来的父进程即刻开始扫描进程表，而此时子进程还没有安全进入ZOMBIE状态，导致父进程再一次进入SLEEPING状态，此后将不再会有wakeup操作来唤醒父进程了。

// 从上面提及的两种错误情况来看，wait_lock本质上规避了lost wakeup问题，因此不可以去掉wait_lock。除此之外，wait_lock还是进程结构体种parent字段的保护锁，每当涉及到修改进程的父进程信息时，都需要先持有wait_lock才可以修改parent字段，例如fork函数以及reparent函数中的相应代码片段。所以，wait_lock既是用来保证同步的条件锁，又是某些数据结构的保护锁，请仔细品味。
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        
        acquire(&pp->lock);

        havekids = 1;
        // 如果是僵尸进程，回收资源，将 status 复制到用户地址空间(addr)
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -EFAULT;
          }
          // 释放进程资源，返回子进程的 pid
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids){
      release(&wait_lock);
      return -ECHILD;
    }
    if(killed(p)){
      release(&wait_lock);
      return -EINTR;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}


// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
// 
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  // 保存当前进程的寄存器到 p->context
  // 从 mycpu()->context 恢复调度器的寄存器
  
  swtch(&p->context, &mycpu()->context);

  // 再次返回到此执行点时恢复 intena; 这是因为一个 CPU 对应多个进程导致的必然结果
  mycpu()->intena = intena;
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  // 不能在持有 lk 时持有 p->lock，否则有死锁风险
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files to release resources (e.g., pipe write ends)
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
      p->fdflags[fd] = 0;
    }
  }

  // Clean up shared memory attachments
  shm_cleanup_proc(p);

  // CLONE_CHILD_CLEARTID: clear and (optionally) futex-wake (not implemented)
  if(p->clear_child_tid != 0){
    int zero = 0;
    copyout(p->pagetable, p->clear_child_tid, (char *)&zero, sizeof(zero));
    p->clear_child_tid = 0;
  }

  // begin_op();
  // iput(p->cwd);
  // end_op();
  // p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  // 通知父进程退出信号
  if(p->parent && p->exit_signal != 0)
    signal_send(p->parent, p->exit_signal);
  
  acquire(&p->lock);

  // Encode exit status in Linux wait format:
  // bits 8-15: exit status (for normal termination)
  // bits 0-7: signal number (0 for normal termination)
  if((p->xstate & 0x7f) == 0)
    p->xstate = (status & 0xff) << 8;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

int getpid(void)
{
  return myproc()->pid;
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc_lazy(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}
