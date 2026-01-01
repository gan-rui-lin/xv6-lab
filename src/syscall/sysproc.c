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
  // u64 addr;
  //   size_t map_size;
  //   int prot;
  //   int flags;
  //   int fd;
  //   size_t offset;
  //   if (_arg_addr(0, addr) < 0 || _arg_addr(1, map_size) < 0 || _arg_int(2, prot) < 0 ||
  //       _arg_int(3, flags) < 0 || _arg_int(4, fd) < 0 || _arg_addr(5, offset) < 0)
  //   {
  //       printfRed("[SyscallHandler::sys_mmap] Error fetching mmap arguments\n");
  //       return -syscall::SYS_EINVAL;
  //   }
  //   printfYellow("[SyscallHandler::sys_mmap] addr: %p, map_size: %u, prot: %d, flags: %d, fd: %d, offset: %u\n",
  //                 (void *)addr, map_size, prot, flags, fd, offset);
  //   fs::file *f = nullptr;
  //   proc::Pcb *p = proc::k_pm.get_cur_pcb();
  //   f = p->get_open_file(fd);
  //   if (!(flags & MAP_ANONYMOUS))
  //   {
  //       if (f == nullptr)
  //       {
  //           printfRed("[SyscallHandler::sys_mmap] Invalid file descriptor: %d\n", fd);
  //           return -EBADF; // 返回无效文件描述符错误
  //       }
  //       if (f->_attrs.u_read == 0)
  //       {
  //           printfRed("[SyscallHandler::sys_mmap] File descriptor %d is not open for reading\n", fd);
  //           return -EACCES; // 返回权限错误
  //       }
  //   }
  //   if (map_size == 0)
  //   {
  //       printfRed("[SyscallHandler::sys_mmap] Invalid map_size: %zu\n", map_size);
  //       return -EINVAL; // 返回无效参数错误
  //   }
  //   if (!(flags & MAP_SHARED) && !(flags & MAP_PRIVATE) && !(flags & MAP_SHARED_VALIDATE))
  //   {
  //       printfRed("[SyscallHandler::sys_mmap] Invalid flags: %d\n", flags);
  //       return -EINVAL; // 返回无效参数错误
  //   }
  //   // 处理 memfd 文件的特殊情况
  //   if (!(flags & MAP_ANONYMOUS) && f != nullptr)
  //   {
  //       // 检查是否是 memfd 文件
  //       if (f->_path_name.find("memfd:") == 0)
  //       {
  //           printfCyan("[SyscallHandler::sys_mmap] Handling memfd file: %s\n", f->_path_name.c_str());

  //           // 检查 memfd 的 seals
  //           if ((flags & MAP_SHARED) && (prot & PROT_WRITE) && (f->_seals & F_SEAL_WRITE))
  //           {
  //               printfRed("[SyscallHandler::sys_mmap] memfd文件被F_SEAL_WRITE密封，无法创建共享写映射\n");
  //               return -EPERM;
  //           }

  //           // 对于 memfd 文件，我们需要直接处理内存映射，而不是通过文件系统路径
  //           // 因为 memfd 文件的路径在文件系统中不存在

  //           // 验证偏移量和大小
  //           if (offset > f->lwext4_file_struct.fsize)
  //           {
  //               printfRed("[SyscallHandler::sys_mmap] offset %zu exceeds file size %llu\n",
  //                         offset, f->lwext4_file_struct.fsize);
  //               return -ENXIO;
  //           }

  //           if (offset + map_size > f->lwext4_file_struct.fsize)
  //           {
  //               printfYellow("[SyscallHandler::sys_mmap] mapping extends beyond file size, will be zero-filled\n");
  //           }
  //       }
  //   }

  //   int mmap_errno = 0;
  //   void *result = proc::k_pm.mmap((void *)addr, map_size, prot, flags, fd, offset, &mmap_errno);

  //   if (result == MAP_FAILED)
  //   {
  //       printfRed("[SyscallHandler::sys_mmap] mmap failed with errno: %d\n", mmap_errno);
  //       return -mmap_errno; // 返回负的错误码
  //   }
  //   // if(addr==0&&map_size==1024&&prot==2&&flags==2&&fd==3&&offset==0)
  //   // return -1;
  //   return (uint64)result; // 调用进程管理器的 mmap 函数
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