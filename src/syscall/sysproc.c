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
  if (stack != 0)
    panic("sys_clone: stack unsupported");
  if (ptid != 0 || tls != 0 || ctid != 0){
    //TODO 实现 ptid/tls/ctid 支持
    // 不过目前先打印调试信息
    #ifdef LOG_DEBUG
    log_debug("sys_clone: ptid/tls/ctid unsupported (ptid=%p, tls=%p, ctid=%p)\n", ptid, tls, ctid);
    #endif
    return fork();
  }
    

  return fork();
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
// We currently do not support envp; panic if envp != 0 to avoid silent ignore.
uint64
sys_execve(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg, uenvp;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0 || argaddr(2, &uenvp) < 0)
    return -1;
  if(uenvp != 0)
    panic("sys_execve: envp unsupported");

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

sys_mmap()
{
    // u64 addr;
    // size_t map_size;
    // int prot;
    // int flags;
    // int fd;
    // size_t offset;
    // if (_arg_addr(0, addr) < 0 || _arg_addr(1, map_size) < 0 || _arg_int(2, prot) < 0 ||
    //     _arg_int(3, flags) < 0 || _arg_int(4, fd) < 0 || _arg_addr(5, offset) < 0)
    // {
    //     printfRed("[SyscallHandler::sys_mmap] Error fetching mmap arguments\n");
    //     return -syscall::SYS_EINVAL;
    // }
    // printfYellow("[SyscallHandler::sys_mmap] addr: %p, map_size: %u, prot: %d, flags: %d, fd: %d, offset: %u\n",
    //               (void *)addr, map_size, prot, flags, fd, offset);
    // fs::file *f = nullptr;
    // proc::Pcb *p = proc::k_pm.get_cur_pcb();
    // f = p->get_open_file(fd);
    // if (!(flags & MAP_ANONYMOUS))
    // {
    //     if (f == nullptr)
    //     {
    //         printfRed("[SyscallHandler::sys_mmap] Invalid file descriptor: %d\n", fd);
    //         return -EBADF; // 返回无效文件描述符错误
    //     }
    //     if (f->_attrs.u_read == 0)
    //     {
    //         printfRed("[SyscallHandler::sys_mmap] File descriptor %d is not open for reading\n", fd);
    //         return -EACCES; // 返回权限错误
    //     }
    // }
    // if (map_size == 0)
    // {
    //     printfRed("[SyscallHandler::sys_mmap] Invalid map_size: %zu\n", map_size);
    //     return -EINVAL; // 返回无效参数错误
    // }
    // if (!(flags & MAP_SHARED) && !(flags & MAP_PRIVATE) && !(flags & MAP_SHARED_VALIDATE))
    // {
    //     printfRed("[SyscallHandler::sys_mmap] Invalid flags: %d\n", flags);
    //     return -EINVAL; // 返回无效参数错误
    // }
    // // 处理 memfd 文件的特殊情况
    // if (!(flags & MAP_ANONYMOUS) && f != nullptr)
    // {
    //     // 检查是否是 memfd 文件
    //     if (f->_path_name.find("memfd:") == 0)
    //     {
    //         printfCyan("[SyscallHandler::sys_mmap] Handling memfd file: %s\n", f->_path_name.c_str());

    //         // 检查 memfd 的 seals
    //         if ((flags & MAP_SHARED) && (prot & PROT_WRITE) && (f->_seals & F_SEAL_WRITE))
    //         {
    //             printfRed("[SyscallHandler::sys_mmap] memfd文件被F_SEAL_WRITE密封，无法创建共享写映射\n");
    //             return -EPERM;
    //         }

    //         // 对于 memfd 文件，我们需要直接处理内存映射，而不是通过文件系统路径
    //         // 因为 memfd 文件的路径在文件系统中不存在

    //         // 验证偏移量和大小
    //         if (offset > f->lwext4_file_struct.fsize)
    //         {
    //             printfRed("[SyscallHandler::sys_mmap] offset %zu exceeds file size %llu\n",
    //                       offset, f->lwext4_file_struct.fsize);
    //             return -ENXIO;
    //         }

    //         if (offset + map_size > f->lwext4_file_struct.fsize)
    //         {
    //             printfYellow("[SyscallHandler::sys_mmap] mapping extends beyond file size, will be zero-filled\n");
    //         }
    //     }
    // }

    // int mmap_errno = 0;
    // void *result = proc::k_pm.mmap((void *)addr, map_size, prot, flags, fd, offset, &mmap_errno);

    // if (result == MAP_FAILED)
    // {
    //     printfRed("[SyscallHandler::sys_mmap] mmap failed with errno: %d\n", mmap_errno);
    //     return -mmap_errno; // 返回负的错误码
    // }
    // // if(addr==0&&map_size==1024&&prot==2&&flags==2&&fd==3&&offset==0)
    // // return -1;
    // return (uint64)result; // 调用进程管理器的 mmap 函数
}