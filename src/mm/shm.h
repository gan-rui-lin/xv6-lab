//claude: System V shared memory implementation - 共享内存头文件定义
#ifndef SHM_H
#define SHM_H

#include "types.h"
#include "spinlock.h"

//claude: IPC权限结构体，用于访问控制
struct ipc_perm {
  uint uid;    //claude: 所有者的用户ID
  uint gid;    //claude: 所有者的组ID
  uint cuid;   //claude: 创建者的用户ID
  uint cgid;   //claude: 创建者的组ID
  uint mode;   //claude: 访问权限模式（如0666）
  uint seq;    //claude: 序列号（保留，当前未使用）
};

//claude: 共享内存段描述符，符合System V IPC标准
struct shmid_ds {
  struct ipc_perm shm_perm;    //claude: 操作权限信息
  uint64 shm_segsz;             //claude: 段大小（字节数）
  uint64 shm_atime;             //claude: 最后一次shmat()的时间
  uint64 shm_dtime;             //claude: 最后一次shmdt()的时间
  uint64 shm_ctime;             //claude: 最后一次修改时间（创建或IPC_SET）
  int shm_cpid;                 //claude: 创建者进程的PID
  int shm_lpid;                 //claude: 最后执行shmat/shmdt的进程PID
  uint64 shm_nattch;            //claude: 当前附加到此段的进程数
};

//claude: 内核内部使用的共享内存段结构，扩展了shmid_ds
#define SHM_MAXSEGS 128   //claude: 系统最多支持128个共享内存段
#define SHM_INVALID -1    //claude: 无效的shmid值

struct shm_seg {
  int valid;                    //claude: 标志位：1表示此槽位正在使用，0表示空闲
  int key;                      //claude: IPC密钥，用于多进程查找同一段
  struct shmid_ds ds;           //claude: 段的元数据（权限、时间戳等）
  void *kaddr;                  //claude: 内核虚拟地址，指向实际物理页
  uint64 size;                  //claude: 实际分配的大小（已对齐到页边界）
  int refcount;                 //claude: 引用计数，跟踪附加的进程数
};

#define SHM_MAX_ATTACH 16  //claude: 每个进程最多可附加16个共享内存段

//claude: IPC标志位，用于shmget()函数
#define IPC_CREAT  0001000  //claude: 如果key不存在则创建，否则获取
#define IPC_EXCL   0002000  //claude: 与IPC_CREAT一起使用，key已存在则失败
#define IPC_RMID   0        //claude: shmctl命令：删除共享内存段
#define IPC_STAT   2        //claude: shmctl命令：获取段信息
#define IPC_SET    1        //claude: shmctl命令：设置段信息（未完全实现）

//claude: shmat()标志位
#define SHM_RDONLY 010000   //claude: 只读方式附加共享内存
#define SHM_RND    020000   //claude: 将地址向下对齐到SHMLBA边界（未实现）

//claude: ftok函数声明（未实现，预留接口）
int ftok(const char *path, int id);

//claude: 共享内存系统调用接口（内核态）
uint64 sys_shmget(void);   //claude: 系统调用包装：创建/获取共享内存段
uint64 sys_shmat(void);    //claude: 系统调用包装：附加共享内存到进程
uint64 sys_shmdt(void);    //claude: 系统调用包装：分离共享内存
uint64 sys_shmctl(void);   //claude: 系统调用包装：控制操作

//claude: 内部函数接口（供内核其他模块调用）
void shm_init(void);                              //claude: 初始化共享内存子系统
int shm_get(int key, uint64 size, int flags);   //claude: 内部实现：创建/获取段
uint64 shm_at(int shmid, uint64 addr, int flags); //claude: 内部实现：附加段
int shm_dt(uint64 addr);                          //claude: 内部实现：分离段
int shm_ctl(int shmid, int cmd, uint64 buf_addr); //claude: 内部实现：控制操作
void shm_cleanup_proc(struct proc *p);            //claude: 内部实现：清理进程的所有附加

#endif // SHM_H
