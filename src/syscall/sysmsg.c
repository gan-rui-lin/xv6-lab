//claude: System V 消息队列系统调用包装器
//claude: 提供用户空间到内核空间的接口桥接

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "proc.h"
#include "../ipc/msgqueue.h"

//claude: sys_msgget - 创建或访问消息队列的系统调用包装器
//claude: 参数：
//claude:   a0: key (int) - IPC 键值
//claude:   a1: msgflg (int) - 标志（IPC_CREAT, IPC_EXCL, 权限位）
//claude: 返回值：
//claude:   成功：消息队列ID（>=0）
//claude:   失败：-1
uint64
sys_msgget(void)
{
  int key, msgflg;

  argint(0, &key);       //claude: 从寄存器 a0 读取 IPC 键值
  argint(1, &msgflg);    //claude: 从寄存器 a1 读取标志

  int msqid = msgget(key, msgflg);  //claude: 调用内核实现函数
  return (uint64)msqid;
}

//claude: sys_msgsnd - 向消息队列发送消息的系统调用包装器
//claude: 参数：
//claude:   a0: msqid (int) - 消息队列ID
//claude:   a1: msgp (void*) - 指向消息缓冲区的指针
//claude:   a2: msgsz (size_t) - 消息数据大小（不包含类型字段）
//claude:   a3: msgflg (int) - 标志（IPC_NOWAIT）
//claude: 返回值：
//claude:   成功：0
//claude:   失败：-1
uint64
sys_msgsnd(void)
{
  int msqid, msgflg;
  uint64 msgp, msgsz;

  argint(0, &msqid);     //claude: 从寄存器 a0 读取队列ID
  argaddr(1, &msgp);     //claude: 从寄存器 a1 读取消息缓冲区地址
  argaddr(2, &msgsz);    //claude: 从寄存器 a2 读取消息大小
  argint(3, &msgflg);    //claude: 从寄存器 a3 读取标志

  int ret = msgsnd(msqid, (const void*)msgp, (uint)msgsz, msgflg);  //claude: 调用内核实现
  return (uint64)ret;
}

//claude: sys_msgrcv - 从消息队列接收消息的系统调用包装器
//claude: 参数：
//claude:   a0: msqid (int) - 消息队列ID
//claude:   a1: msgp (void*) - 指向接收缓冲区的指针
//claude:   a2: msgsz (size_t) - 缓冲区最大大小
//claude:   a3: msgtyp (long) - 消息类型过滤器
//claude:   a4: msgflg (int) - 标志（IPC_NOWAIT, MSG_NOERROR）
//claude: 返回值：
//claude:   成功：接收的字节数（>0）
//claude:   失败：-1
uint64
sys_msgrcv(void)
{
  int msqid, msgflg;
  uint64 msgp, msgsz;
  int msgtyp_int;

  argint(0, &msqid);         //claude: 从寄存器 a0 读取队列ID
  argaddr(1, &msgp);         //claude: 从寄存器 a1 读取接收缓冲区地址
  argaddr(2, &msgsz);        //claude: 从寄存器 a2 读取缓冲区大小
  argint(3, &msgtyp_int);    //claude: 从寄存器 a3 读取消息类型（注意：Linux使用long，这里简化为int）
  argint(4, &msgflg);        //claude: 从寄存器 a4 读取标志

  long msgtyp = (long)msgtyp_int;  //claude: 转换为 long 类型

  int ret = msgrcv(msqid, (void*)msgp, (uint)msgsz, msgtyp, msgflg);  //claude: 调用内核实现
  return (uint64)ret;
}

//claude: sys_msgctl - 消息队列控制操作的系统调用包装器
//claude: 参数：
//claude:   a0: msqid (int) - 消息队列ID
//claude:   a1: cmd (int) - 控制命令（IPC_STAT, IPC_SET, IPC_RMID）
//claude:   a2: buf (struct msqid_ds*) - 用于 stat/set 操作的缓冲区指针
//claude: 返回值：
//claude:   成功：0
//claude:   失败：-1
uint64
sys_msgctl(void)
{
  int msqid, cmd;
  uint64 buf;

  argint(0, &msqid);     //claude: 从寄存器 a0 读取队列ID
  argint(1, &cmd);       //claude: 从寄存器 a1 读取控制命令
  argaddr(2, &buf);      //claude: 从寄存器 a2 读取缓冲区地址

  int ret = msgctl(msqid, cmd, (struct msqid_ds*)buf);  //claude: 调用内核实现
  return (uint64)ret;
}
