//claude: System V shared memory implementation - 共享内存IPC机制实现
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc/proc.h"
#include "defs.h"
#include "mm/shm.h"

//claude: 全局共享内存段管理表，保存系统中所有共享内存段
struct {
  struct spinlock lock;              //claude: 自旋锁，保护并发访问共享内存表
  struct shm_seg segs[SHM_MAXSEGS]; //claude: 共享内存段数组，最多128个段
  int next_id;                       //claude: 下一个可用的shmid（当前未使用）
} shm_table;

//claude: 初始化共享内存子系统，在系统启动时调用（main.c中）
void
shm_init(void)
{
  initlock(&shm_table.lock, "shm");    //claude: 初始化全局表的自旋锁
  for(int i = 0; i < SHM_MAXSEGS; i++){
    shm_table.segs[i].valid = 0;      //claude: 标记所有槽位为空闲（未使用）
    shm_table.segs[i].kaddr = 0;      //claude: 清空内核地址指针
  }
  shm_table.next_id = 0;               //claude: 初始化ID计数器
}

//claude: 根据IPC key查找共享内存段，返回shmid或-1
static int
shm_find_by_key(int key)
{
  for(int i = 0; i < SHM_MAXSEGS; i++){
    if(shm_table.segs[i].valid && shm_table.segs[i].key == key) //claude: 检查槽位有效且key匹配
      return i;  //claude: 返回shmid（即数组索引）
  }
  return -1;  //claude: 未找到返回-1
}

//claude: 分配一个空闲的共享内存段槽位，返回shmid或-1
static int
shm_alloc(void)
{
  for(int i = 0; i < SHM_MAXSEGS; i++){
    if(!shm_table.segs[i].valid)  //claude: 找到第一个未使用的槽位
      return i;  //claude: 返回槽位索引作为shmid
  }
  return -1;  //claude: 没有空闲槽位，返回-1
}

//claude: shmget内部实现 - 创建或获取共享内存段
int
shm_get(int key, uint64 size, int flags)
{
  struct proc *p = myproc();  //claude: 获取当前进程
  int shmid;

  acquire(&shm_table.lock);  //claude: 获取全局表锁，保证操作原子性

  //claude: 第一步：检查指定key的共享内存段是否已存在
  shmid = shm_find_by_key(key);

  if(shmid >= 0){
    //claude: 段已存在的情况
    if((flags & IPC_CREAT) && (flags & IPC_EXCL)){
      //claude: IPC_EXCL标志要求独占创建，已存在则失败
      release(&shm_table.lock);
      return -1;  //claude: 返回EEXIST错误
    }
    release(&shm_table.lock);
    return shmid;  //claude: 返回现有段的ID
  }

  //claude: 第二步：段不存在，检查是否允许创建
  if(!(flags & IPC_CREAT)){
    release(&shm_table.lock);
    return -1;  //claude: 没有IPC_CREAT标志，返回ENOENT错误
  }

  //claude: 第三步：分配新的共享内存段
  shmid = shm_alloc();
  if(shmid < 0){
    release(&shm_table.lock);
    return -1;  //claude: 没有空闲槽位，返回ENOSPC错误
  }

  //claude: 第四步：处理大小参数，向上取整到页边界
  size = PGROUNDUP(size);  //claude: 对齐到4KB页边界
  if(size == 0)
    size = PGSIZE;  //claude: 至少分配一页

  //claude: 第五步：分配物理页作为共享内存
  void *kaddr = kalloc();  //claude: 从内核堆分配一页物理内存
  if(kaddr == 0){
    release(&shm_table.lock);
    return -1;  //claude: 内存不足，返回ENOMEM错误
  }

  //claude: 当前限制：只支持单页共享内存（4KB）
  if(size > PGSIZE){
    //claude: 请求的大小超过一页，暂不支持多页共享内存
    kfree(kaddr);  //claude: 释放已分配的页
    release(&shm_table.lock);
    return -1;  //claude: 返回ENOMEM错误
  }

  memset(kaddr, 0, size);  //claude: 将分配的内存清零

  //claude: 第六步：初始化共享内存段结构
  struct shm_seg *seg = &shm_table.segs[shmid];
  seg->valid = 1;       //claude: 标记槽位为有效
  seg->key = key;       //claude: 保存IPC key
  seg->kaddr = kaddr;   //claude: 保存内核虚拟地址
  seg->size = size;     //claude: 保存实际大小
  seg->refcount = 0;    //claude: 初始引用计数为0

  //claude: 第七步：初始化元数据（符合System V标准）
  seg->ds.shm_perm.uid = p->uid;    //claude: 所有者用户ID
  seg->ds.shm_perm.gid = p->gid;    //claude: 所有者组ID
  seg->ds.shm_perm.cuid = p->uid;   //claude: 创建者用户ID
  seg->ds.shm_perm.cgid = p->gid;   //claude: 创建者组ID
  seg->ds.shm_perm.mode = flags & 0777;  //claude: 访问权限（如0666）
  seg->ds.shm_segsz = size;         //claude: 段大小
  seg->ds.shm_cpid = p->pid;        //claude: 创建者进程ID
  seg->ds.shm_lpid = 0;             //claude: 最后操作的进程ID（初始为0）
  seg->ds.shm_nattch = 0;           //claude: 当前附加数（初始为0）
  seg->ds.shm_atime = 0;            //claude: 最后附加时间（初始为0）
  seg->ds.shm_dtime = 0;            //claude: 最后分离时间（初始为0）
  seg->ds.shm_ctime = r_time() / 10000000;  //claude: 创建/修改时间（当前时间）

  release(&shm_table.lock);  //claude: 释放锁
  return shmid;  //claude: 返回新创建的段ID
}

//claude: shmat内部实现 - 将共享内存段附加到进程地址空间
uint64
shm_at(int shmid, uint64 addr, int flags)
{
  struct proc *p = myproc();  //claude: 获取当前进程

  //claude: 第一步：验证shmid有效性
  if(shmid < 0 || shmid >= SHM_MAXSEGS)
    return -1;  //claude: shmid越界，返回错误

  acquire(&shm_table.lock);  //claude: 获取全局表锁

  struct shm_seg *seg = &shm_table.segs[shmid];
  if(!seg->valid){
    release(&shm_table.lock);
    return -1;
  }

  // Find free slot in process attachment table
  int slot = -1;
  for(int i = 0; i < SHM_MAX_ATTACH; i++){
    if(!p->shm_attach[i].valid){
      slot = i;
      break;
    }
  }

  if(slot < 0){
    release(&shm_table.lock);
    return -1;  // Too many attachments
  }

  // Choose virtual address
  // If addr is 0, choose address automatically
  if(addr == 0){
    // Place shared memory above heap, below stack
    // Use a region starting at 0x70000000
    addr = 0x70000000UL + (shmid * 0x10000);
  }

  addr = PGROUNDDOWN(addr);

  // Map the shared memory into process page table
  int perm = PTE_U;
  if(!(flags & SHM_RDONLY))
    perm |= PTE_W;
  perm |= PTE_R;  // Always readable

  if(mappages(p->pagetable, addr, seg->size, (uint64)seg->kaddr, perm) < 0){
    release(&shm_table.lock);
    return -1;
  }

  // Record attachment
  p->shm_attach[slot].valid = 1;
  p->shm_attach[slot].shmid = shmid;
  p->shm_attach[slot].vaddr = (void*)addr;

  // Update segment metadata
  seg->refcount++;
  seg->ds.shm_nattch++;
  seg->ds.shm_lpid = p->pid;
  seg->ds.shm_atime = r_time() / 10000000;

  release(&shm_table.lock);
  return addr;
}

// shmdt - detach shared memory segment from process address space
int
shm_dt(uint64 addr)
{
  struct proc *p = myproc();

  addr = PGROUNDDOWN(addr);

  acquire(&shm_table.lock);

  // Find attachment
  int slot = -1;
  for(int i = 0; i < SHM_MAX_ATTACH; i++){
    if(p->shm_attach[i].valid && (uint64)p->shm_attach[i].vaddr == addr){
      slot = i;
      break;
    }
  }

  if(slot < 0){
    release(&shm_table.lock);
    return -1;  // Not attached
  }

  int shmid = p->shm_attach[slot].shmid;
  struct shm_seg *seg = &shm_table.segs[shmid];

  // Unmap from process page table
  uvmunmap(p->pagetable, addr, seg->size / PGSIZE, 0);  // Don't free physical pages

  // Remove attachment record
  p->shm_attach[slot].valid = 0;

  // Update segment metadata
  seg->refcount--;
  seg->ds.shm_nattch--;
  seg->ds.shm_lpid = p->pid;
  seg->ds.shm_dtime = r_time() / 10000000;

  release(&shm_table.lock);
  return 0;
}

// shmctl - control operations on shared memory
int
shm_ctl(int shmid, int cmd, uint64 buf_addr)
{
  struct proc *p = myproc();

  if(shmid < 0 || shmid >= SHM_MAXSEGS)
    return -1;

  acquire(&shm_table.lock);

  struct shm_seg *seg = &shm_table.segs[shmid];
  if(!seg->valid){
    release(&shm_table.lock);
    return -1;
  }

  switch(cmd){
    case IPC_STAT:
      // Copy segment info to user space
      if(copyout(p->pagetable, buf_addr, (char*)&seg->ds, sizeof(seg->ds)) < 0){
        release(&shm_table.lock);
        return -1;
      }
      break;

    case IPC_RMID:
      // Mark segment for deletion
      // Actual deletion happens when refcount reaches 0
      if(seg->refcount == 0){
        // No attachments, delete immediately
        kfree(seg->kaddr);
        seg->valid = 0;
        seg->kaddr = 0;
      } else {
        // Has attachments, mark for deletion
        // Will be deleted when last process detaches
        seg->ds.shm_perm.mode |= 0x8000;  // Mark as deleted
      }
      break;

    case IPC_SET:
      // Set segment info (not fully implemented)
      // Would need to update permissions from user buffer
      break;

    default:
      release(&shm_table.lock);
      return -1;
  }

  release(&shm_table.lock);
  return 0;
}

// Clean up process shared memory attachments on exit
void
shm_cleanup_proc(struct proc *p)
{
  acquire(&shm_table.lock);

  for(int i = 0; i < SHM_MAX_ATTACH; i++){
    if(p->shm_attach[i].valid){
      int shmid = p->shm_attach[i].shmid;
      uint64 addr = (uint64)p->shm_attach[i].vaddr;
      struct shm_seg *seg = &shm_table.segs[shmid];

      // Unmap from process page table
      uvmunmap(p->pagetable, addr, seg->size / PGSIZE, 0);

      // Update segment metadata
      seg->refcount--;
      seg->ds.shm_nattch--;

      // If marked for deletion and no more attachments, free it
      if((seg->ds.shm_perm.mode & 0x8000) && seg->refcount == 0){
        kfree(seg->kaddr);
        seg->valid = 0;
        seg->kaddr = 0;
      }

      p->shm_attach[i].valid = 0;
    }
  }

  release(&shm_table.lock);
}

// System call wrappers

uint64
sys_shmget(void)
{
  int key, flags;
  uint64 size;

  argint(0, &key);
  argaddr(1, &size);
  argint(2, &flags);

  return (uint64)shm_get(key, size, flags);
}

uint64
sys_shmat(void)
{
  int shmid, flags;
  uint64 addr;

  argint(0, &shmid);
  argaddr(1, &addr);
  argint(2, &flags);

  return shm_at(shmid, addr, flags);
}

uint64
sys_shmdt(void)
{
  uint64 addr;
  argaddr(0, &addr);

  return (uint64)shm_dt(addr);
}

uint64
sys_shmctl(void)
{
  int shmid, cmd;
  uint64 buf;

  argint(0, &shmid);
  argint(1, &cmd);
  argaddr(2, &buf);

  return (uint64)shm_ctl(shmid, cmd, buf);
}
