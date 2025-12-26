#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return getpid();
}

uint64
sys_fork(void)
{
  return fork();
}

// Minimal Linux-compatible clone: only supports SIGCHLD exit signal, no threads/TLS.
// Args (as passed from userspace on RISC-V):
// a0: flags, a1: stack, a2: ptid, a3: tls, a4: ctid
// Unsupported options will panic to avoid silent misbehavior.
uint64
sys_clone(void)
{
  uint64 flags, stack, ptid, tls, ctid;
  // fetch arguments
  argaddr(0, &flags);
  argaddr(1, &stack);
  argaddr(2, &ptid);
  argaddr(3, &tls);
  argaddr(4, &ctid);

  // only allow pure fork semantics: exit signal must be SIGCHLD and no other flags
  // Linux SIGCHLD commonly 17. We only accept (flags & ~0x7f)==0 and (flags & 0x7f)==17
  #define LINUX_SIGCHLD 17
  if (((flags & ~((uint64)0x7f)) != 0) || ((flags & 0x7f) != LINUX_SIGCHLD))
    panic("sys_clone: unsupported flags");
  
  if (ptid != 0 || tls != 0 || ctid != 0){
    //TODO 实现 ptid/tls/ctid 支持
    // 不过目前先打印调试信息
    #ifdef LOG_DEBUG
    log_debug("sys_clone: ptid/tls/ctid unsupported (ptid=%p, tls=%p, ctid=%p)\n", ptid, tls, ctid);
    #endif
  }
  
  // Use clone_fork which supports custom stack
  // If stack != 0, the child's stack pointer will be set to the provided stack
  // The userspace __clone wrapper has already saved the function pointer
  // and argument on this stack before making the syscall.
  return clone_fork(stack);
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

// 使进程内存增加 n 字节，返回新内存的起始地址
uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

// Minimal syscall wrappers to satisfy linker during early development.
// These are intentionally lightweight: they defer to kernel helpers where
// available, or return simple defaults.

uint64
sys_sleep(void)
{
    int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  // 直到滴答数达到要求才返回
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    // 等待 devintr 中的时钟中断唤醒
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;
  argint(0, &pid);

  return kill(pid);
}

uint64
sys_shutdown(void)
{
  // #define TEST_FINISHER_FAIL    0x3333
  // #define TEST_FINISHER_PASS    0x5555
  // #define TEST_FINISHER_RESET   0x7777

  // volatile uint32 *test_dev = (volatile uint32 *)TEST_DEVICE;
  // *test_dev = TEST_FINISHER_PASS;
  
  sbi_shutdown();
  
  return 0;  // not reached
}

// Linux getpid (172) is already implemented as sys_getpid.
// Provide getppid (173).
uint64
sys_getppid(void)
{
  struct proc *p = myproc();
  if (p->parent)
    return p->parent->pid;
  return 0;
}

// Linux execve(path, argv, envp)
// We currently do not support envp; we silently ignore it if it's empty (first element is NULL).
uint64
sys_execve(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg, uenvp;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0 || argaddr(2, &uenvp) < 0)
    return -1;
  
  // Check if envp is non-empty (not just non-NULL)
  // If envp is provided, check if the first element is NULL (empty array)
  if(uenvp != 0) {
    uint64 first_env;
    if(fetchaddr(uenvp, &first_env) < 0)
      return -1;
    if(first_env != 0)
      panic("sys_execve: non-empty envp unsupported");
    // If first_env == 0, envp is empty, we can proceed
  }

  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      goto bad;
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0)
      goto bad;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      panic("sys_execve kalloc");
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return ret;

bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

// Minimal wait4(pid, status, options): support only pid == -1 and options == 0
// Panic on unsupported to surface issues early.
uint64
sys_wait4(void)
{
  int pid, options;
  uint64 status;
  argint(0, &pid);
  argaddr(1, &status);
  argint(2, &options);

  if (pid != -1)
    panic("sys_wait4: pid != -1 unsupported");
  if (options != 0)
    panic("sys_wait4: options unsupported");

  return wait(status);
}

// brk() 设置程序的堆顶地址
// 参数 addr: 新的堆顶地址
// 如果 addr == 0，返回当前堆顶地址
// 如果 addr != 0，尝试将堆顶设置为 addr，成功返回新地址，失败返回 -1
uint64
sys_brk(void)
{
  uint64 addr;
  struct proc *p = myproc();
  
  // 获取参数：目标堆顶地址
  if(argaddr(0, &addr) < 0)
    return -1;
  
  // 如果 addr == 0，返回当前堆顶地址（查询模式）
  if(addr == 0) {
    return p->sz;
  }
  
  // 检查地址是否合法（不能小于当前大小或太大）
  if(addr < p->sz) {
    // 缩小内存
    int n = addr - p->sz;  // n 是负数
    if(growproc(n) < 0)
      return -1;
    return addr;
  } else if(addr > p->sz) {
    // 扩大内存
    int n = addr - p->sz;  // n 是正数
    if(growproc(n) < 0)
      return -1;
    return addr;
  }
  
  // addr == p->sz，不需要改变
  return addr;
}

uint64
sys_mmap(void)
{
  // TODO: 实现 mmap 系统调用
  // 暂时返回错误，避免编译错误
  return -1;
}

uint64
sys_sched_yield(void)
{
    // 原始 C++ 风格的实现（已注释）：
    // // printfCyan("[sche]  yield here \n");
    // Cpu::get_cpu()->push_intr_off();
    // Pcb *p = Cpu::get_cpu()->get_cur_proc();
    // Cpu::get_cpu()->pop_intr_off();
    // // printfCyan("[sche]  yield here,p->addr:%x \n",Cpu::get_cpu()->get_cur_proc());
    // p->_lock.acquire();
    // // printfCyan("[sche]  yield here \n");
    // p->_state = ProcState::RUNNABLE;
    // call_sched(); // 注意swtch的逻辑是函数调用, 所以重新调用就是视为从这个函数返回
    // p->_lock.release();
    
    // xv6 C 风格的实现：
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();  // 切换到调度器，让出 CPU
    release(&p->lock);
    return 0;
}