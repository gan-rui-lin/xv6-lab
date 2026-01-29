//claude: System V Message Queue Implementation
//claude: 提供高效的异步IPC机制，支持消息缓冲和类型过滤

#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "msgqueue.h"
#include "defs.h"
#include "memlayout.h"

//claude: 全局消息队列表，最多支持 MSG_MAX_QUEUES (64) 个队列
static struct msg_queue msg_queues[MSG_MAX_QUEUES];
//claude: 保护全局队列表分配的自旋锁
static struct spinlock msg_table_lock;

//claude: ========== 初始化函数 ==========

//claude: 消息队列子系统初始化，在内核启动时调用
void msg_init(void) {
  initlock(&msg_table_lock, "msgtable");

  //claude: 初始化所有队列槽位为空闲状态
  for (int i = 0; i < MSG_MAX_QUEUES; i++) {
    msg_queues[i].valid = 0;         //claude: 标记为未使用
    msg_queues[i].key = 0;
    msg_queues[i].head = 0;          //claude: 消息链表头
    msg_queues[i].tail = 0;          //claude: 消息链表尾
    msg_queues[i].msgcount = 0;      //claude: 当前消息数
    msg_queues[i].bytes_used = 0;    //claude: 当前使用字节数
    initlock(&msg_queues[i].lock, "msgqueue");
  }

  log_info("[msgqueue] Initialized %d queues (max %d msgs, %d bytes/msg)\n",
           MSG_MAX_QUEUES, MSG_MAX_MESSAGES, MSG_MAX_SIZE);
}

//claude: ========== 内部辅助函数 ==========

//claude: 根据队列ID查找消息队列（ID即为msg_queues数组下标）
struct msg_queue* msg_find_by_id(int msqid) {
  if (msqid < 0 || msqid >= MSG_MAX_QUEUES)
    return 0;

  struct msg_queue *mq = &msg_queues[msqid];
  if (!mq->valid)  //claude: 检查槽位是否有效
    return 0;

  return mq;
}

//claude: 根据IPC键值查找消息队列
struct msg_queue* msg_find_by_key(int key) {
  for (int i = 0; i < MSG_MAX_QUEUES; i++) {
    if (msg_queues[i].valid && msg_queues[i].key == key)
      return &msg_queues[i];
  }
  return 0;
}

//claude: 分配新的消息队列
struct msg_queue* msg_alloc(int key) {
  acquire(&msg_table_lock);

  //claude: 查找第一个空闲槽位
  struct msg_queue *mq = 0;
  for (int i = 0; i < MSG_MAX_QUEUES; i++) {
    if (!msg_queues[i].valid) {
      mq = &msg_queues[i];
      break;
    }
  }

  if (!mq) {
    release(&msg_table_lock);
    return 0;  //claude: 所有槽位都被占用
  }

  //claude: 初始化队列结构
  struct proc *p = myproc();
  mq->valid = 1;            //claude: 标记槽位为使用中
  mq->key = key;            //claude: 设置IPC键值
  mq->head = 0;             //claude: 消息链表初始为空
  mq->tail = 0;
  mq->msgcount = 0;
  mq->bytes_used = 0;

  //claude: 初始化权限元数据
  mq->ds.msg_perm.uid = p->uid;      //claude: 所有者UID
  mq->ds.msg_perm.gid = p->gid;      //claude: 所有者GID
  mq->ds.msg_perm.cuid = p->uid;     //claude: 创建者UID
  mq->ds.msg_perm.cgid = p->gid;     //claude: 创建者GID
  mq->ds.msg_perm.mode = 0666;       //claude: 默认权限：所有人可读写
  mq->ds.msg_perm.seq = 0;

  //claude: 初始化统计信息
  mq->ds.msg_stime = 0;              //claude: 最后发送时间
  mq->ds.msg_rtime = 0;              //claude: 最后接收时间
  mq->ds.msg_ctime = 0;              //claude: 最后修改时间
  mq->ds.msg_qnum = 0;               //claude: 队列中消息数
  mq->ds.msg_qbytes = 16384;         //claude: 队列容量限制：默认16KB
  mq->ds.msg_lspid = 0;              //claude: 最后发送进程PID
  mq->ds.msg_lrpid = 0;              //claude: 最后接收进程PID

  //claude: 初始化等待通道（使用队列地址作为通道标识）
  mq->send_chan = mq;                //claude: 发送者等待通道（队列满时阻塞）
  mq->recv_chan = mq;                //claude: 接收者等待通道（队列空时阻塞）

  release(&msg_table_lock);

  int msqid = mq - msg_queues;       //claude: 计算队列ID
  log_debug("[msgqueue] Allocated queue %d key=%d uid=%d gid=%d mode=0%o\n",
            msqid, key, mq->ds.msg_perm.uid, mq->ds.msg_perm.gid, mq->ds.msg_perm.mode);

  return mq;
}

//claude: 释放消息队列及其所有消息
void msg_free(struct msg_queue *mq) {
  if (!mq)
    return;

  acquire(&mq->lock);

  //claude: 释放链表中所有消息
  struct msg *m = mq->head;
  while (m) {
    struct msg *next = m->next;
    kfree(m->mdata);           //claude: 释放消息数据缓冲区
    kfree((char*)m);           //claude: 释放消息结构体
    m = next;
  }

  //claude: 重置队列状态
  mq->head = 0;
  mq->tail = 0;
  mq->msgcount = 0;
  mq->bytes_used = 0;
  mq->valid = 0;                     //claude: 标记槽位为空闲

  release(&mq->lock);

  //claude: 唤醒所有等待在该队列上的进程
  wakeup(mq->send_chan);             //claude: 唤醒等待发送的进程
  wakeup(mq->recv_chan);             //claude: 唤醒等待接收的进程
}

//claude: 从队列中移除匹配类型的消息
//claude: 返回：0=成功，-1=未找到匹配消息
int msg_remove_message(struct msg_queue *mq, long msgtyp, struct msg **out) {
  struct msg *prev = 0;
  struct msg *curr = mq->head;

  //claude: 消息类型过滤规则：
  //claude: - msgtyp == 0: 取队首消息（FIFO）
  //claude: - msgtyp > 0: 取第一个类型等于msgtyp的消息
  //claude: - msgtyp < 0: 取类型 <= |msgtyp| 的最小类型消息

  if (msgtyp == 0) {
    //claude: 类型为0，移除队首消息
    if (!curr)
      return -1;

    mq->head = curr->next;
    if (!mq->head)
      mq->tail = 0;              //claude: 队列变空，清空尾指针

    *out = curr;
    return 0;
  }

  if (msgtyp > 0) {
    //claude: 类型大于0，查找精确匹配的消息
    while (curr) {
      if (curr->mtype == msgtyp) {
        //claude: 从链表中移除该节点
        if (prev)
          prev->next = curr->next;
        else
          mq->head = curr->next;

        if (curr == mq->tail)
          mq->tail = prev;       //claude: 更新尾指针

        *out = curr;
        return 0;
      }
      prev = curr;
      curr = curr->next;
    }
    return -1;
  }

  //claude: 类型小于0，查找类型 <= |msgtyp| 的最小类型消息（优先级模式）
  long target = -msgtyp;
  struct msg *best = 0;
  struct msg *best_prev = 0;
  prev = 0;
  curr = mq->head;

  while (curr) {
    if (curr->mtype <= target) {
      if (!best || curr->mtype < best->mtype) {
        best = curr;
        best_prev = prev;
      }
    }
    prev = curr;
    curr = curr->next;
  }

  if (!best)
    return -1;

  //claude: 移除找到的最小类型消息
  if (best_prev)
    best_prev->next = best->next;
  else
    mq->head = best->next;

  if (best == mq->tail)
    mq->tail = best_prev;

  *out = best;
  return 0;
}

//claude: ========== 核心操作函数 ==========

//claude: msgget - 创建或访问消息队列
//claude: 参数：key=IPC键值，msgflg=标志（IPC_CREAT/IPC_EXCL/权限位）
//claude: 返回：队列ID（>=0）或 -1（失败）
int msgget(int key, int msgflg) {
  struct msg_queue *mq;

  //claude: 检查是否为私有队列（IPC_PRIVATE=0）或查找现有队列
  if (key != IPC_PRIVATE) {
    mq = msg_find_by_key(key);

    if (mq) {
      //claude: 队列已存在
      if ((msgflg & IPC_CREAT) && (msgflg & IPC_EXCL)) {
        //claude: 请求独占创建但队列已存在，返回错误
        return -1;
      }

      //claude: 返回现有队列的ID
      return mq - msg_queues;
    }

    //claude: 队列不存在
    if (!(msgflg & IPC_CREAT)) {
      //claude: 未设置IPC_CREAT标志，无法创建，返回错误
      return -1;
    }
  }

  //claude: 分配新队列
  mq = msg_alloc(key);
  if (!mq)
    return -1;

  return mq - msg_queues;
}

//claude: msgsnd - 向队列发送消息
//claude: 参数：msqid=队列ID，msgp=消息缓冲区，msgsz=数据大小，msgflg=标志
//claude: 返回：0=成功，-1=失败
int msgsnd(int msqid, const void *msgp, uint msgsz, int msgflg) {
  struct msg_queue *mq = msg_find_by_id(msqid);
  if (!mq)
    return -1;

  //claude: 验证消息大小不超过限制
  if (msgsz > MSG_MAX_SIZE)
    return -1;

  //claude: 从用户空间拷贝消息类型
  struct msgbuf *ubuf = (struct msgbuf*)msgp;
  long mtype;
  if (copyin(myproc()->pagetable, (char*)&mtype, (uint64)&ubuf->mtype, sizeof(long)) < 0)
    return -1;

  //claude: 验证消息类型（必须大于0）
  if (mtype < MSG_MIN_TYPE)
    return -1;

  acquire(&mq->lock);

  //claude: 检查队列容量，等待空间可用
  while (mq->msgcount >= MSG_MAX_MESSAGES ||
         mq->bytes_used + msgsz > mq->ds.msg_qbytes) {

    if (msgflg & IPC_NOWAIT) {
      release(&mq->lock);
      return -1;  //claude: 非阻塞模式，立即返回错误
    }

    //claude: 阻塞等待空间，持有锁时sleep（自动释放锁并等待唤醒）
    sleep(mq->send_chan, &mq->lock);

    //claude: 唤醒后检查队列是否被删除
    if (!mq->valid) {
      release(&mq->lock);
      return -1;
    }
  }

  //claude: 分配内核消息结构
  struct msg *m = (struct msg*)kalloc();
  if (!m) {
    release(&mq->lock);
    return -1;
  }

  //claude: 分配消息数据缓冲区（使用kalloc获取一个页）
  m->mdata = kalloc();
  if (!m->mdata) {
    kfree((char*)m);
    release(&mq->lock);
    return -1;
  }

  //claude: 从用户空间拷贝消息数据
  if (copyin(myproc()->pagetable, m->mdata, (uint64)ubuf->mtext, msgsz) < 0) {
    kfree(m->mdata);
    kfree((char*)m);
    release(&mq->lock);
    return -1;
  }

  //claude: 初始化消息字段
  m->mtype = mtype;
  m->msize = msgsz;
  m->next = 0;

  //claude: 将消息追加到队列尾部
  if (!mq->head) {
    mq->head = m;
    mq->tail = m;
  } else {
    mq->tail->next = m;
    mq->tail = m;
  }

  //claude: 更新队列统计信息
  mq->msgcount++;
  mq->bytes_used += msgsz;
  mq->ds.msg_qnum = mq->msgcount;

  //claude: 更新最后发送进程PID
  mq->ds.msg_lspid = myproc()->pid;
  //claude: TODO: 更新发送时间戳 mq->ds.msg_stime = get_current_time();

  release(&mq->lock);

  //claude: 唤醒等待接收消息的进程
  wakeup(mq->recv_chan);

  log_debug("[msgqueue] Sent to queue %d: type=%ld size=%d (qnum=%d bytes=%d)\n",
            msqid, mtype, msgsz, mq->msgcount, mq->bytes_used);

  return 0;
}

//claude: msgrcv - 从队列接收消息
//claude: 参数：msqid=队列ID，msgp=接收缓冲区，msgsz=缓冲区大小，msgtyp=类型过滤，msgflg=标志
//claude: 返回：接收的字节数（>0）或 -1（失败）
int msgrcv(int msqid, void *msgp, uint msgsz, long msgtyp, int msgflg) {
  struct msg_queue *mq = msg_find_by_id(msqid);
  if (!mq)
    return -1;

  acquire(&mq->lock);

  struct msg *m = 0;

  //claude: 循环等待匹配的消息
  while (1) {
    if (msg_remove_message(mq, msgtyp, &m) == 0)
      break;  //claude: 找到匹配消息，退出循环

    //claude: 未找到匹配消息
    if (msgflg & IPC_NOWAIT) {
      release(&mq->lock);
      return -1;  //claude: 非阻塞模式，立即返回错误
    }

    //claude: 阻塞等待消息到达
    sleep(mq->recv_chan, &mq->lock);

    //claude: 唤醒后检查队列是否被删除
    if (!mq->valid) {
      release(&mq->lock);
      return -1;
    }
  }

  //claude: 获取到消息，检查缓冲区大小
  uint actual_size = m->msize;

  //claude: 检查接收缓冲区是否足够大
  if (msgsz < actual_size) {
    if (!(msgflg & MSG_NOERROR)) {
      //claude: 缓冲区太小且不允许截断，将消息放回队列头部
      m->next = mq->head;
      mq->head = m;
      if (!mq->tail)
        mq->tail = m;

      release(&mq->lock);
      return -1;
    }
    //claude: 允许截断消息（MSG_NOERROR标志）
    actual_size = msgsz;
  }

  //claude: 拷贝消息类型到用户空间
  struct msgbuf *ubuf = (struct msgbuf*)msgp;
  if (copyout(myproc()->pagetable, (uint64)&ubuf->mtype, (char*)&m->mtype, sizeof(long)) < 0) {
    //claude: 拷贝失败，将消息放回队列
    m->next = mq->head;
    mq->head = m;
    if (!mq->tail)
      mq->tail = m;

    release(&mq->lock);
    return -1;
  }

  //claude: 拷贝消息数据到用户空间
  if (copyout(myproc()->pagetable, (uint64)ubuf->mtext, m->mdata, actual_size) < 0) {
    //claude: 拷贝失败，将消息放回队列
    m->next = mq->head;
    mq->head = m;
    if (!mq->tail)
      mq->tail = m;

    release(&mq->lock);
    return -1;
  }

  //claude: 更新队列状态
  mq->msgcount--;
  mq->bytes_used -= m->msize;
  mq->ds.msg_qnum = mq->msgcount;

  //claude: 更新统计信息
  mq->ds.msg_lrpid = myproc()->pid;
  //claude: TODO: 更新接收时间戳 mq->ds.msg_rtime = get_current_time();

  int ret = actual_size;

  //claude: 释放消息内存
  kfree(m->mdata);
  kfree((char*)m);

  release(&mq->lock);

  //claude: 唤醒等待发送的进程（队列现在有空间了）
  wakeup(mq->send_chan);

  log_debug("[msgqueue] Received from queue %d: type=%ld size=%d (qnum=%d bytes=%d)\n",
            msqid, m->mtype, ret, mq->msgcount, mq->bytes_used);

  return ret;
}

//claude: msgctl - 消息队列控制操作
//claude: 参数：msqid=队列ID，cmd=命令（IPC_STAT/IPC_SET/IPC_RMID），buf=数据缓冲区
//claude: 返回：0=成功，-1=失败
int msgctl(int msqid, int cmd, struct msqid_ds *buf) {
  struct msg_queue *mq = msg_find_by_id(msqid);
  if (!mq)
    return -1;

  struct proc *p = myproc();

  switch (cmd) {
    case IPC_STAT:
      //claude: 获取队列状态，拷贝到用户空间
      acquire(&mq->lock);
      if (copyout(p->pagetable, (uint64)buf, (char*)&mq->ds, sizeof(struct msqid_ds)) < 0) {
        release(&mq->lock);
        return -1;
      }
      release(&mq->lock);
      log_debug("[msgqueue] IPC_STAT queue %d: qnum=%ld qbytes=%ld\n",
                msqid, mq->ds.msg_qnum, mq->ds.msg_qbytes);
      return 0;

    case IPC_SET:
      //claude: 设置队列参数，从用户空间读取
      {
        struct msqid_ds new_ds;
        if (copyin(p->pagetable, (char*)&new_ds, (uint64)buf, sizeof(struct msqid_ds)) < 0)
          return -1;

        acquire(&mq->lock);
        //claude: 只允许修改特定字段
        mq->ds.msg_perm.uid = new_ds.msg_perm.uid;
        mq->ds.msg_perm.gid = new_ds.msg_perm.gid;
        mq->ds.msg_perm.mode = new_ds.msg_perm.mode & 0777;
        mq->ds.msg_qbytes = new_ds.msg_qbytes;
        release(&mq->lock);

        log_debug("[msgqueue] IPC_SET queue %d: mode=0%o qbytes=%ld\n",
                  msqid, mq->ds.msg_perm.mode, mq->ds.msg_qbytes);
      }
      return 0;

    case IPC_RMID:
      //claude: 删除消息队列
      log_info("[msgqueue] IPC_RMID queue %d (had %d msgs, %d bytes)\n",
               msqid, mq->msgcount, mq->bytes_used);
      msg_free(mq);
      return 0;

    default:
      return -1;
  }
}
