#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"
#include "../sync/sleeplock.h"
#include "../fs/fs.h"
#include "../fs/file.h"

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

static int
sleep_ticks(int n)
{
  log_debug("sys_sleep: sleeping for %d ticks\n", n);
  uint ticks0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_sleep(void)
{
  int n;
  argint(0, &n);
  if (n <= 0)
    return 0;
  return sleep_ticks(n);
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

// Basic credential queries: return 0 for all IDs (single-user system).
uint64 sys_getuid(void)   { return 0; }
uint64 sys_geteuid(void)  { return 0; }
uint64 sys_getgid(void)   { return 0; }
uint64 sys_getegid(void)  { return 0; }

// set_tid_address(addr): record clear_child_tid address if provided, return tid.
uint64
sys_set_tid_address(void)
{
  uint64 addr;
  argaddr(0, &addr);
  struct proc *p = myproc();

  // Mirror Linux by returning the thread id (here, pid).
  // We also attempt to write the tid to the provided address if non-null.
  if(addr != 0){
    if(copyout(p->pagetable, addr, (char *)&p->pid, sizeof(p->pid)) < 0)
      return -1;
  }
  return p->pid;
}

// gettid: return thread id (pid in this single-threaded model).
uint64
sys_gettid(void)
{
  return myproc()->pid;
}

// exit_group(status): treat the group as the single process; reuse exit.
uint64
sys_exit_group(void)
{
  int status;
  argint(0, &status);
  exit(status);
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
// Now also supports waiting for a specific child process by PID
uint64
sys_wait4(void)
{
  int pid, options;
  uint64 status;
  argint(0, &pid);
  argaddr(1, &status);
  argint(2, &options);

  // 原来的实现（已注释）：
  // if (pid != -1)
  //   panic("sys_wait4: pid != -1 unsupported");
  
  if (options != 0)
    panic("sys_wait4: options unsupported");

  // If pid == -1, wait for any child (original behavior)
  if (pid == -1) {
    return wait(status);
  }
  
  // If pid > 0, wait for specific child process
  // We need to implement waitpid functionality
  struct proc *pp;
  int havekids;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for the specific child
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      // Check if this is our child
      if(pp->parent == p){
        // Check if this matches the requested PID
        if(pp->pid == pid){
          acquire(&pp->lock);
          havekids = 1;
          
          if(pp->state == ZOMBIE){
            // Found the matching child
            int cpid = pp->pid;
            
            // Debug: print the xstate before copying
            // printf("sys_wait4: found zombie child pid=%d, xstate=%d\n", cpid, pp->xstate);
            
            if(status != 0 && copyout(p->pagetable, status, (char *)&pp->xstate,
                                    sizeof(pp->xstate)) < 0) {
              release(&pp->lock);
              release(&wait_lock);
              return -1;
            }
            freeproc(pp);
            release(&pp->lock);
            release(&wait_lock);
            return cpid;
          }
          release(&pp->lock);
        }
      }
    }

    // No matching child found
    if(!havekids || killed(p)){
      release(&wait_lock);
      printf("sys_wait4: no matching child found for pid=%d\n", pid);
      return -1;
    }
    
    // Wait for the child to exit
    sleep(p, &wait_lock);
  }
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
  uint64 addr;
  uint64 length;
  int prot;
  int flags;
  int fd;
  uint64 offset;
  
  // 获取参数
  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 || 
     argint(2, &prot) < 0 || argint(3, &flags) < 0 || 
     argint(4, &fd) < 0 || argaddr(5, &offset) < 0)
    return -1;

  struct proc *p = myproc();
  struct file *f;

  // 长度为 0 时，兼容性地映射一页
  if(length == 0)
    length = PGSIZE;

  // 对齐起始地址与长度
  uint64 map_size = PGROUNDUP(length);
  uint64 old_sz = p->sz;
  uint64 base = PGROUNDUP(old_sz);
  uint64 new_sz = base + map_size;

  // 计算权限标志：默认可读，可选写/执行
  int perm = PTE_R;
  if(prot & PROT_WRITE)
    perm |= PTE_W;
  if(prot & PROT_EXEC)
    perm |= PTE_X;

  // 匿名映射：忽略 fd/offset，直接扩展地址空间
  if(flags & MAP_ANONYMOUS){
    if((new_sz = uvmalloc(p->pagetable, old_sz, new_sz, perm)) == 0)
      return -1;
    p->sz = new_sz;
    return base;
  }

  // 文件映射：需要有效、可读的文件描述符
  if(fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0)
    return -1;
  if(!f->readable)
    return -1;

  // 分配虚拟内存
  if((new_sz = uvmalloc(p->pagetable, old_sz, new_sz, perm)) == 0)
    return -1;
  p->sz = new_sz;

  if(f->type != FD_INODE){
    uvmdealloc(p->pagetable, new_sz, old_sz);
    p->sz = old_sz;
    return -1;
  }

  ilock(f->ip);
  uint64 read_size = length;
  uint64 file_size = f->ip->size;

  if(offset >= file_size){
    // 偏移超界，留空映射
    iunlock(f->ip);
    return base;
  }
  if(offset + read_size > file_size)
    read_size = file_size - offset;

  int bytes_read = readi(f->ip, 1, base, offset, read_size);
  iunlock(f->ip);

  if(bytes_read < 0){
    uvmdealloc(p->pagetable, new_sz, old_sz);
    p->sz = old_sz;
    return -1;
  }

  // 剩余部分已由 uvmalloc 清零
  return base;
}

uint64
sys_munmap(void)
{
  uint64 addr;
  uint64 length;
  
  // 获取参数
  if(argaddr(0, &addr) < 0 || argaddr(1, &length) < 0)
    return -1;
  
  // 长度必须大于0
  if(length == 0)
    return 0;  // 长度为0时，不做任何操作，返回成功
  
  struct proc *p = myproc();
  
  // 页面对齐
  uint64 start = PGROUNDDOWN(addr);
  uint64 end = PGROUNDUP(addr + length);
  uint64 npages = (end - start) / PGSIZE;
  
  // 简化实现：检查地址是否在进程地址空间内
  if(start >= p->sz) {
    return -1;  // 地址超出进程空间
  }
  
  // 调整end，不能超过进程大小
  if(end > p->sz) {
    end = p->sz;
    npages = (end - start) / PGSIZE;
  }
  
  // 如果没有页面需要解除映射，直接返回成功
  if(npages == 0) {
    return 0;
  }
  
  // 使用uvmunmap解除映射
  uvmunmap(p->pagetable, start, npages, 1);
  
  // 关键修复：如果解除映射的区域在进程末尾，需要调整进程大小
  // 这样可以防止进程退出时再次尝试释放这些页面
  if(end == p->sz) {
    p->sz = start;
  }
  
  return 0;
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

// sys_waitpid () {
  // int pid;
  // uint64 wstatus_addr;
  // int option;
  // if (_arg_int(0, pid) < 0)
  //     return -1;
  // if (_arg_addr(1, wstatus_addr) < 0)
  //     return -1;
  // if (_arg_int(2, option) < 0)
  //     return -1;

  // // 检查无效的PID值
  // // 根据POSIX标准，PID不能是INT_MIN或其他无效值
  // if (pid == INT_MIN)
  // {
  //     return SYS_ESRCH;
  // }

  // // printf("[SyscallHandler::sys_wait4] pid: %d, wstatus_addr: %p, option: %d\n",
  // //    pid, wstatus_addr, option);
  // int waitret = proc::k_pm.wait4(pid, wstatus_addr, option);
  // // printf("[SyscallHandler::sys_wait4] waitret: %d\n",waitret);
  // return waitret;pid != -1 unsupported
// }


uint64
sys_uname(void)
{
  uint64 addr;
  if (argaddr(0, &addr) < 0)
    return -1;

  struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
  } un;

  safestrcpy(un.sysname, "ruos", 65);
  safestrcpy(un.nodename, "ru-node", 65);
  safestrcpy(un.release, "1.0", 65);
  safestrcpy(un.version, "1.0.0", 65);
  safestrcpy(un.machine, "riscv64", 65);
  safestrcpy(un.domainname, "(none)", 65);

  if (copyout(myproc()->pagetable, addr, (char*)&un, sizeof(un)) < 0)
    return -1;

  return 0;
}

uint64
sys_nanosleep(void)
{
  // args: a0 = req (user ptr), a1 = rem (user ptr)
  uint64 req_addr, rem_addr;
  argaddr(0, &req_addr);
  argaddr(1, &rem_addr);

  struct {
    long sec;
    long usec;
  } req;

  if (req_addr == 0)
    return -1;
  if (copyin(myproc()->pagetable, (char*)&req, req_addr, sizeof(req)) != 0)
    return -1;

  if (req.sec < 0 || req.usec < 0 || req.usec >= 1000000L)
    return -1;

  // 精确转换为 tick 数：假定时钟中断为 1ms/tick（HZ=1000）。
  // 将秒/微秒转换为总纳秒并按 1ms 进行向上取整。
  const long TICK_NS = 1000000L;   // 1 ms
  unsigned long long total_ns = (unsigned long long)req.sec * 1000000000ULL
                              + (unsigned long long)req.usec * 1000ULL;
  int n_ticks = (int)((total_ns + TICK_NS - 1) / TICK_NS); // ceil(total_ns / 1ms)

  if (n_ticks <= 0)
    return 0;

  int ret = sleep_ticks(n_ticks);

  // 完成后，若需要，置剩余时间为0
  if (rem_addr) {
    struct { long sec; long usec; } rem = {0, 0};
    if (copyout(myproc()->pagetable, rem_addr, (char*)&rem, sizeof(rem)) < 0)
      return -1;
  }
  return ret;
}

// 设置当前进程的优先级
// 参数：priority (1-40, 数值越小优先级越高)
// 返回：成功返回0，失败返回-1
uint64
sys_setpriority(void)
{
  int priority;
  argint(0, &priority);
  
  // 检查优先级范围
  if (priority < PRIO_MIN || priority > PRIO_MAX) {
    return -1;
  }
  
  struct proc *p = myproc();
  acquire(&p->lock);
  p->priority = priority;
  release(&p->lock);
  
  return 0;
}

// 获取当前进程的优先级
// 返回：当前进程的优先级值
uint64
sys_getpriority(void)
{
  struct proc *p = myproc();
  return p->priority;
}
