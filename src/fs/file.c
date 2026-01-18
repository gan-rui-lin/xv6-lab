//
// 支持所有与文件描述符相关的系统调用的辅助函数。
//


#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "errno.h"
#include "fcntl.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];  // 设备操作表，登记各种设备的操作函数
struct {
  struct spinlock lock;    // 文件表的自旋锁
  struct file file[NFILE]; // 文件结构体数组
} ftable;

// 初始化文件表的锁
void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// 分配一个空闲的文件结构
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);      // 加锁，保护并发分配
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){          // 仅查找引用计数为0的空闲文件
      f->ref = 1;             // 标记被占用
      f->oflags = 0;          // 缺省无标志
      release(&ftable.lock);  // 释放锁
      return f;               // 返回分配好的文件结构体
    }
  }
  release(&ftable.lock);      // 无空闲则释放锁
  return 0;                   // 返回空指针表示分配失败
}

// 增加文件结构体的引用计数
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);     // 加锁，保护引用计数递增
  if(f->ref < 1)
    panic("filedup");        // 异常，引用计数小于1
  f->ref++;
  release(&ftable.lock);     // 释放锁
  return f;
}

// 关闭文件（减少引用计数，到0时才真正清理和释放资源）
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);         // 加锁
  if(f->ref < 1)
    panic("fileclose");          // 异常，引用计数小于1
  if(--f->ref > 0){              // 如果递减后引用计数仍大于0
    release(&ftable.lock);       // 释放锁，直接返回
    return;
  }
  ff = *f;                       // 保存一份副本（因为后续要释放资源，可能涉及链表或其他成员）
  f->ref = 0;                    // 标记为未使用
  f->type = FD_NONE;             // 文件类型设为无效
  release(&ftable.lock);         // 解锁

  // 根据文件类型清理对应资源
  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);  // 管道文件则清理管道
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op(ff.ip->dev);             // 开始文件系统操作（log记录事务边界）
    iput(ff.ip);                      // 释放 inode
    end_op(ff.ip->dev);               // 结束 log 操作
  }
}

// 获取文件 f 的元数据信息（stat），拷贝到用户 addr 指向的空间
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();        // 获取当前进程
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);                   // 加锁 inode，防止并发修改
    stati(f->ip, &st);              // 获取 inode 的元信息
    // TODO 实际修改问题，现在直接修改 nlink
    st.nlink = 1;
    iunlock(f->ip);                 // 解锁
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)  // 拷贝给用户空间
      return -1;
    return 0;
    
  }
  return -1;                        // 非普通文件/设备类型无法 stat
}

// 从文件 f 读取数据到用户空间 addr
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)  // 不可读则返回错误
    return -1;

  if(f->type == FD_PIPE){
    // 非阻塞：若为空且写端仍开，返回 -EAGAIN
    extern int pipe_is_empty(struct pipe *);
    extern int pipe_write_open(struct pipe *);
    if((f->oflags & O_NONBLOCK) && pipe_is_empty(f->pipe) && pipe_write_open(f->pipe))
      return -EAGAIN;
    r = piperead(f->pipe, addr, n);    // 管道读
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(f, 1, addr, n); // 调用设备的读方法
  } else if(f->type == FD_INODE){
    ilock(f->ip);   // 加锁 inode
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)  // 读数据并更新文件偏移
      f->off += r;
    iunlock(f->ip); // 解锁 inode
  } else {
    panic("fileread"); // 未知类型出错
  }

  return r;
}

// 向文件 f 写数据，写入内容来自用户空间 addr
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)  // 不可写则错误
    return -1;

  if(f->type == FD_PIPE){
    // 非阻塞：若已满且读端仍开，返回 -EAGAIN
    extern int pipe_is_full(struct pipe *);
    extern int pipe_read_open(struct pipe *);
    if((f->oflags & O_NONBLOCK) && pipe_is_full(f->pipe) && pipe_read_open(f->pipe))
      return -EAGAIN;
    ret = pipewrite(f->pipe, addr, n);    // 管道写
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(f, 1, addr, n); // 设备写
  } else if(f->type == FD_INODE){
    // 分批次写入，避免一次写操作超过文件系统事务最大容量
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op(f->ip->dev);      // 日志事务：开始操作（保证原子）
      ilock(f->ip);              // 加锁 inode
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)   // 写入成功，更新文件偏移
        f->off += r;
      iunlock(f->ip);            // 解锁 inode
      end_op(f->ip->dev);        // 日志事务：结束操作

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");  // 写入长度不符应抛错
      i += r;
    }
    ret = (i == n ? n : -1);      // 返回写入总长度或 -1
  } else {
    panic("filewrite");            // 未知类型出错
  }

  return ret;
}