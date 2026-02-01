#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "eventfd.h"
#include "errno.h"

#define EVENTFD_MAX (0xFFFFFFFFFFFFFFFEULL)  // UINT64_MAX - 1

// 分配并初始化一个 eventfd 对象
struct eventfd*
eventfd_alloc(unsigned int initval, int flags)
{
  struct eventfd *efd;

  // 分配内存
  efd = (struct eventfd*)kmalloc(sizeof(struct eventfd));
  if(efd == 0)
    return 0;

  // 初始化
  initlock(&efd->lock, "eventfd");
  efd->counter = initval;
  efd->flags = flags;
  efd->ref = 1;

  return efd;
}

// 关闭 eventfd（减少引用计数，引用计数为0时释放）
void
eventfd_close(struct eventfd *efd)
{
  int should_free = 0;

  acquire(&efd->lock);
  efd->ref--;
  if(efd->ref == 0) {
    should_free = 1;
  }
  release(&efd->lock);

  if(should_free) {
    kmfree((char*)efd);
  }
}

// 从 eventfd 读取
// 返回值：成功返回8（读取的字节数），失败返回负数错误码
int
eventfd_read(struct eventfd *efd, uint64 addr, int nonblock)
{
  struct proc *p = myproc();
  uint64 val;

  acquire(&efd->lock);

  // 等待计数器大于0
  while(efd->counter == 0) {
    // 非阻塞模式：直接返回 EAGAIN
    if(nonblock) {
      release(&efd->lock);
      return -EAGAIN;
    }

    // 阻塞模式：睡眠等待
    if(p->killed) {
      release(&efd->lock);
      return -EINTR;
    }
    sleep(efd, &efd->lock);
  }

  // 读取计数器
  if(efd->flags & EFD_SEMAPHORE) {
    // 信号量模式：返回 1，计数器减 1
    val = 1;
    efd->counter--;
  } else {
    // 默认模式：返回当前值，计数器重置为 0
    val = efd->counter;
    efd->counter = 0;
  }

  // 唤醒等待写入的进程
  wakeup(efd);
  release(&efd->lock);

  // 将值拷贝到用户空间
  if(copyout(p->pagetable, addr, (char*)&val, sizeof(val)) < 0)
    return -EFAULT;

  return sizeof(val);  // 成功返回8字节
}

// 向 eventfd 写入
// 返回值：成功返回8（写入的字节数），失败返回负数错误码
int
eventfd_write(struct eventfd *efd, uint64 addr, int nonblock)
{
  struct proc *p = myproc();
  uint64 val;

  // 从用户空间读取要写入的值
  if(copyin(p->pagetable, (char*)&val, addr, sizeof(val)) < 0)
    return -EFAULT;

  // 检查写入值的有效性
  if(val == 0xFFFFFFFFFFFFFFFFULL) {  // UINT64_MAX
    return -EINVAL;
  }

  acquire(&efd->lock);

  // 等待有足够空间写入（防止溢出）
  while(efd->counter > EVENTFD_MAX - val) {
    // 非阻塞模式：直接返回 EAGAIN
    if(nonblock) {
      release(&efd->lock);
      return -EAGAIN;
    }

    // 阻塞模式：睡眠等待
    if(p->killed) {
      release(&efd->lock);
      return -EINTR;
    }
    sleep(efd, &efd->lock);
  }

  // 累加到计数器
  efd->counter += val;

  // 唤醒等待读取的进程
  wakeup(efd);
  release(&efd->lock);

  return sizeof(val);  // 成功返回8字节
}
