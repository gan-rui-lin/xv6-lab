#ifndef _MSGQUEUE_H
#define _MSGQUEUE_H

#include "types.h"
#include "spinlock.h"

//claude: System V 消息队列实现头文件
//claude: 提供高效的异步IPC机制，支持消息缓冲和类型过滤

//claude: ========== 系统限制常量 ==========

#define MSG_MAX_QUEUES    64      //claude: 系统最多支持的消息队列数量
#define MSG_MAX_MESSAGES  32      //claude: 每个队列最多可容纳的消息数
#define MSG_MAX_SIZE      4096    //claude: 单条消息最大字节数（4KB，与页面大小一致）
#define MSG_MIN_TYPE      1       //claude: 最小有效消息类型（必须>0）

//claude: msgctl() 控制命令
#define IPC_RMID    0   //claude: 删除消息队列标识符
#define IPC_SET     1   //claude: 设置队列选项（权限、容量等）
#define IPC_STAT    2   //claude: 获取队列状态信息
#define IPC_INFO    3   //claude: 获取系统级信息

//claude: msgget() 创建标志
#define IPC_CREAT   01000   //claude: 如果键值不存在则创建新队列
#define IPC_EXCL    02000   //claude: 与IPC_CREAT结合使用，键值存在时返回错误
#define IPC_NOWAIT  04000   //claude: 非阻塞模式（队列满/空时立即返回）

//claude: msgrcv() 接收标志
#define MSG_NOERROR 010000  //claude: 消息过大时截断而非返回错误

//claude: 特殊键值
#define IPC_PRIVATE ((int)0)  //claude: 私有队列键值（每次创建新队列）

//claude: ========== IPC 权限结构 ==========
//claude: 与 System V IPC 标准兼容（与 shm.h 共用）

struct ipc_perm {
  uint uid;       //claude: 所有者用户ID
  uint gid;       //claude: 所有者组ID
  uint cuid;      //claude: 创建者用户ID
  uint cgid;      //claude: 创建者组ID
  uint mode;      //claude: 访问权限位（如 0666）
  uint seq;       //claude: 序列号（用于唯一性标识）
};

//claude: ========== 消息结构 ==========

//claude: 用户空间消息结构（用于 msgsnd/msgrcv 系统调用）
struct msgbuf {
  long mtype;         //claude: 消息类型（必须大于0）
  char mtext[1];      //claude: 消息数据（可变长度数组技巧）
};

//claude: 内核空间消息结构（内部使用）
struct msg {
  struct msg *next;   //claude: 指向队列中下一条消息的指针（链表节点）
  long mtype;         //claude: 消息类型
  uint msize;         //claude: 消息数据大小（字节数）
  char *mdata;        //claude: 指向消息数据的指针（通过 kalloc 分配）
};

//claude: ========== 消息队列描述符 ==========

struct msqid_ds {
  struct ipc_perm msg_perm;    //claude: 权限信息（uid/gid/mode）

  //claude: 统计信息
  uint64 msg_stime;            //claude: 最后一次 msgsnd 的时间戳
  uint64 msg_rtime;            //claude: 最后一次 msgrcv 的时间戳
  uint64 msg_ctime;            //claude: 最后一次修改的时间戳

  uint64 msg_qnum;             //claude: 队列中当前消息数量
  uint64 msg_qbytes;           //claude: 队列最大字节数（默认16KB）

  int msg_lspid;               //claude: 最后一次发送消息的进程PID
  int msg_lrpid;               //claude: 最后一次接收消息的进程PID
};

//claude: ========== 内核消息队列结构 ==========

struct msg_queue {
  int valid;                   //claude: 槽位是否被使用（1=使用中，0=空闲）
  int key;                     //claude: IPC 键值（用于多进程共享访问）
  struct msqid_ds ds;          //claude: 队列元数据（权限、统计信息等）

  struct spinlock lock;        //claude: 保护队列操作的自旋锁

  //claude: 消息队列（链表实现）
  struct msg *head;            //claude: 链表头指针（队列中第一条消息）
  struct msg *tail;            //claude: 链表尾指针（队列中最后一条消息）
  uint msgcount;               //claude: 队列中当前消息数量
  uint bytes_used;             //claude: 队列中当前使用的字节数

  //claude: 阻塞操作的睡眠通道
  void *send_chan;             //claude: 发送者等待通道（队列满时阻塞）
  void *recv_chan;             //claude: 接收者等待通道（队列空时阻塞）
};

//claude: ========== 函数原型 ==========

//claude: 初始化函数
void msg_init(void);                    //claude: 消息队列子系统初始化（在内核启动时调用）

//claude: 核心操作函数（System V IPC 标准接口）
int msgget(int key, int msgflg);       //claude: 创建或访问消息队列
int msgsnd(int msqid, const void *msgp, uint msgsz, int msgflg);  //claude: 向队列发送消息
int msgrcv(int msqid, void *msgp, uint msgsz, long msgtyp, int msgflg);  //claude: 从队列接收消息
int msgctl(int msqid, int cmd, struct msqid_ds *buf);  //claude: 消息队列控制操作

//claude: 内部辅助函数
struct msg_queue* msg_find_by_id(int msqid);          //claude: 根据队列ID查找队列
struct msg_queue* msg_find_by_key(int key);           //claude: 根据IPC键值查找队列
struct msg_queue* msg_alloc(int key);                 //claude: 分配新的消息队列
void msg_free(struct msg_queue *mq);                  //claude: 释放消息队列及其所有资源
int msg_remove_message(struct msg_queue *mq, long msgtyp, struct msg **out);  //claude: 从队列中移除匹配类型的消息

#endif // _MSGQUEUE_H
