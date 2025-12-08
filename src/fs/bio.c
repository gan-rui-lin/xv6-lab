/*
 * @Author: zjy whuzjy@qq.com
 * @Date: 2025-11-17 11:36:26
 * @Description: 
 * 
 */
// 缓冲区缓存
//
// 缓冲区缓存是一个 buf 结构体的链表，保存了磁盘块数据的缓存副本。
// 在内存中缓存磁盘块可以减少磁盘读操作，也为多个进程同步访问磁盘块提供了一个同步点。
//
// 接口：
// * 要获取某个磁盘块对应的缓冲区，调用 bread。
// * 修改缓冲区数据后，调用 bwrite 将数据写回磁盘。
// * 使用完缓冲区后，调用 brelse。
// * brelse 后不能再使用该缓冲区。
// * 同一时刻只能有一个进程使用某个缓冲区，
//    所以不要长时间占用缓冲区。

#include "../types.h"
#include "../param.h"
#include "../sync/spinlock.h"
#include "../sync/sleeplock.h"
#include "../riscv.h"
#include "../defs.h"
#include "fs.h"
#include "buf.h"

// bcache 结构体保存了 NBUF 个缓冲区；buf 本身通过双向链表连接。
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // 所有缓冲区的链表，通过 prev/next 指针连接。
  // head.next 指向最近使用过的缓冲区。
  struct buf head;
} bcache;

// 初始化缓冲区缓存
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // 创建缓冲区链表
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// 在缓冲区缓存中查找设备 dev 上的块 blockno。
// 如果没有找到，则分配一个新的缓冲区。
// 无论哪种情况，都会返回一个加锁的缓冲区。
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // 是否已经被缓存？
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 没有缓存；回收一个未被使用的缓冲区。
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// 返回一个被锁定的缓冲区，内容为指定块的数据。
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);
    b->valid = 1;
  }
  return b;
}

// 将缓冲区数据写回磁盘。必须加锁使用。
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b->dev, b, 1);
}

// 释放一个加锁的缓冲区。
// 移动到链表头部，成为最近最常用缓冲区（MRU）。
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // 没有人再等待该缓冲区。
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

// 对缓冲区进行“加锁”，增加引用计数
void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

// 对缓冲区进行“解锁”，减少引用计数
void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}