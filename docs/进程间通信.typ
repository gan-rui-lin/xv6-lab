= 进程间通信

== 信号机制

RuOS 实现了类 Linux 的信号子系统，支持 pending 信号、屏蔽字以及用户态信号处理函数。内核提供了 `rt_sigaction`、`rt_sigprocmask`、`rt_sigtimedwait`、`rt_sigreturn` 和 `kill_signal` 等系统调用，允许用户进程注册信号处理函数、修改信号屏蔽字、等待信号以及发送信号。信号处理过程遵循 Linux 语义，确保与现有用户态程序的兼容性。

=== 信号来源与语义

信号可以看作是软件层面对中断机制的抽象，主要来源包括：
- 程序错误：如除零、非法内存访问等；
- 外部事件：如终端 `Ctrl-C` 产生 `SIGINT`、定时器到期产生 `SIGALRM`；
- 显式请求：进程通过 `kill` 发送信号给指定进程或进程组。

与 Linux 保持一致，信号号范围为 `1..64`，并保留非实时信号 (`1..31`) 与实时信号 (`34..64`) 的划分语义。在当前实现中，待处理信号使用*位图*表示，使用`sigpending` 字段进行记录。因此同一信号可能被合并（非实时语义），后续*可扩展为队列*以完善实时信号特性。

=== 关键数据结构

+ `struct proc`

  在 `proc` 结构体中，与信号相关的字段如下：

  ```c
      // 信号处理相关
      uint64 sigpending;           // pending signals bitmap
      uint64 sigmask;              // blocked signals bitmap
      struct sigaction sigactions[NSIG]; // per-signal handler settings

    ```
  其中：
  - `sigpending`：`uint64` 位图，记录待处理信号；
  - `sigmask`：屏蔽字，表示被阻塞的信号；
  - `sigactions[NSIG]`：每个信号的处理动作，包含 `sa_handler`、`sa_mask`、`sa_flags`、`sa_restorer`；

+ `struct sigaction` 与 `struct sigset`

  在 `sigaction` 结构体中，定义了每个信号的处理动作：
  ```c
    struct sigset {
      uint64 bits;
    };

    struct sigaction {
      uint64 sa_handler;
      uint64 sa_flags;
      uint64 sa_restorer;
      struct sigset sa_mask;
    };
    ```

  - `sa_handler`：信号处理函数地址，或 `SIG_DFL`/`SIG_IGN`；
  - `sa_mask`：处理该信号时额外屏蔽的信号集合；
  - `sa_flags`：控制信号处理行为的标志，如 `SA_RESETHAND`、`SA_NODEFER` 等；
  - `sa_restorer`：用户态返回内核的地址，通常指向 `rt_sigreturn`。

  - `sigframe`：用户栈上的信号帧，保存旧的屏蔽字与 `trapframe`，用于 `rt_sigreturn` 恢复上下文。

  用户态可以通过 `rt_sigaction` 系统调用对进程的信号处理行为进行注册。

+ 信号常量

  ```c
    #define SIG_DFL  ((uint64)0)   // 默认处理
    #define SIG_IGN  ((uint64)1)   // 忽略信号
    ...
    #define SIGKILL  9              // 强制终止进程
    #define SIGSTOP  19             // 停止进程执行
    ...
    ```
`SIGKILL` 与 `SIGSTOP` 在任何情况下都不可屏蔽，内核在更新屏蔽字时强制清除这两位。

=== 发送与递送流程

发送信号时，内核通过 `signal_send`/`signal_send_pid` 校验信号号并设置 `sigpending` 位图，同时将 `SLEEPING` 进程唤醒为 `RUNNABLE`。
递送发生在用户态陷入返回之前：`usertrap` 中调用 `signal_handle` 检查并派发信号。

处理逻辑如下：
- 从 `sigpending` 中挑选一个未被 `sigmask` 屏蔽的信号（优先级按信号号递增）；
- `SIG_IGN` 直接忽略；
- `SIG_DFL` 执行默认动作：`SIGCHLD`/`SIGURG`/`SIGWINCH` 默认忽略，其余触发进程终止，并对核心转储类信号设置退出状态 `0x80`；
- 对于用户自定义处理函数，进入用户态 handler。

=== 用户态 handler 构造与返回

由于信号处理程序是由用户提供的，所以信号处理程序的代码是在用户态的。而从系统调用返回到用户态前还是属于内核态，CPU是禁止内核态执行用户态代码的。因此我们需要在用户栈上构造一个信号帧 `sigframe`，并修改 `trapframe` 使得返回到用户态时跳转到用户态的信号处理函数。

`signal_setup_frame` 在用户栈上构造 `sigframe`：
- 16 字节对齐栈指针，写入 `magic`、`old_mask` 与完整 `trapframe`；
- 将 `epc` 指向用户态 `handler`，`a0` 传入信号号，`ra` 设置为 `sa_restorer`；
- 更新 `sigmask`：自动屏蔽当前信号与 `sa_mask`，若设置 `SA_NODEFER` 则不屏蔽当前信号；
- 若设置 `SA_RESETHAND`，处理后自动恢复为默认动作。

用户态处理函数返回后，返回到 `act->sa_restorer`，再通过 `rt_sigreturn` 进入内核，`signal_return` 校验 `sigframe.magic` 并恢复 `trapframe` 与旧屏蔽字，保证控制流回到原用户态执行点。

RuOS 的信号处理流程如 @ruos-signal-flowchart 所示：

#figure(image("sig_handle.png"), caption: "RuOS 信号处理流程图") <ruos-signal-flowchart>


== 共享内存

=== 设计背景与目标

共享内存（Shared Memory）是进程间通信（IPC）最高效的方式之一。与管道、消息队列等需要数据在内核态和用户态之间复制不同，共享内存允许多个进程直接访问同一块物理内存，从而实现零拷贝通信。

我们实现了System V IPC标准的共享内存接口，提供以下特性：

- *标准兼容*：遵循System V IPC规范（shmget/shmat/shmdt/shmctl）
- *多段支持*：最多128个共享内存段
- *权限控制*：基于uid/gid的访问控制
- *自动清理*：进程退出时自动分离附加的共享内存
- *引用计数*：支持多进程同时附加同一内存段

=== 内存共享原理

共享内存的核心思想是让不同进程的虚拟地址映射到同一物理页，如 @ipc-shm-mapping-diagram 所示：

#figure(
  image("diagrams/ipc-shm-mapping.png"),
  caption: [共享内存映射图]
) <ipc-shm-mapping-diagram>

当进程A写入共享内存时，进程B立即可见，无需任何内核干预。

=== 核心数据结构

*共享内存段描述符*（src/mm/shm.h）：

```c
struct shm_seg {
  int valid;                 // 槽位是否使用中
  int key;                   // IPC密钥，用于多进程查找
  struct shmid_ds ds;        // 元数据（权限、时间戳等）
  void* kaddr;               // 内核虚拟地址（指向物理页）
  uint64 size;               // 实际大小（页对齐）
  int refcount;              // 当前附加的进程数
};
```

*进程附加记录*（src/proc/proc.h）：

```c
struct shm_attach {
  int shmid;       // 共享内存段ID
  void* vaddr;     // 附加的虚拟地址
  int valid;       // 记录是否有效
};

struct proc {
  ...
  struct shm_attach shm_attach[16];  // 最多附加16个共享内存段
  uint uid, gid;                     // IPC权限字段
  ...
};
```

*全局管理表*（src/mm/shm.c）：

```c
struct {
  struct spinlock lock;              // 保护并发访问
  struct shm_seg segs[SHM_MAXSEGS]; // 最多128个段
  int next_id;
} shm_table;
```

=== API接口实现

*shmget - 创建/获取共享内存段*

```c
int shmget(int key, size_t size, int flags);
```

功能：
- 根据key查找现有段，若存在则返回shmid
- 若不存在且设置了`IPC_CREAT`标志，则创建新段
- 创建时分配物理页（`kalloc()`）并清零
- 初始化元数据（uid、gid、权限、创建时间等）

*shmat - 附加到进程地址空间*

```c
void* shmat(int shmid, void* addr, int flags);
```

核心步骤：
1. *选择虚拟地址*：若addr为0，自动选择地址（0x70000000 + offset）
2. *映射页表*：调用`mappages()`将共享内存映射到进程页表
3. *记录附加*：在进程的`shm_attach`数组中记录{shmid, vaddr}
4. *更新元数据*：增加引用计数（`refcount++`），记录附加时间

权限处理：
- 默认读写权限（PTE_R | PTE_W | PTE_U）
- 若设置`SHM_RDONLY`标志，则只读（PTE_R | PTE_U）

*shmdt - 分离共享内存*

```c
int shmdt(void* addr);
```

核心步骤：
1. *查找附加记录*：遍历进程的`shm_attach`数组
2. *取消映射*：调用`uvmunmap()`从页表移除（不释放物理页）
3. *更新元数据*：减少引用计数（`refcount--`）
4. *延迟删除*：若段标记为删除且无附加，则释放物理页

*shmctl - 控制操作*

```c
int shmctl(int shmid, int cmd, struct shmid_ds* buf);
```

支持的命令：
- *IPC_STAT*：获取段信息（大小、附加数、权限等）
- *IPC_RMID*：标记删除，当所有进程分离后释放物理页

=== 生命周期管理

共享内存段的完整生命周期如 @ipc-shm-lifecycle-diagram 所示：

#figure(
  image("diagrams/ipc-shm-lifecycle.png"),
  caption: [共享内存生命周期]
) <ipc-shm-lifecycle-diagram>

*自动清理机制*：

进程退出时（`exit()`），调用`shm_cleanup_proc(p)`自动分离所有附加的共享内存，防止内存泄漏。

=== 使用示例

*示例1：父子进程通信*

```c
int main() {
  // 父进程创建共享内存
  int shmid = shmget(0x1234, 4096, IPC_CREAT | 0666);
  char* ptr = shmat(shmid, 0, 0);
  strcpy(ptr, "Message from parent");

  int pid = fork();
  if (pid == 0) {
    // 子进程附加相同共享内存
    char* child_ptr = shmat(shmid, 0, 0);
    printf("Child reads: %s\n", child_ptr);  // 立即可见父进程的数据

    strcpy(child_ptr, "Reply from child");
    shmdt(child_ptr);
    exit(0);
  } else {
    wait(0);
    printf("Parent reads: %s\n", ptr);  // 看到子进程的修改

    shmdt(ptr);
    shmctl(shmid, IPC_RMID, 0);  // 删除共享内存
  }
  exit(0);
}
```

*示例2：查询段信息*

```c
struct shmid_ds buf;
shmctl(shmid, IPC_STAT, &buf);

printf("Size: %d bytes\n", buf.shm_segsz);
printf("Creator PID: %d\n", buf.shm_cpid);
printf("Attachments: %d\n", buf.shm_nattch);
printf("Permissions: 0%o\n", buf.shm_perm.mode & 0777);
```

=== 实现亮点

1. *零拷贝通信*：进程间数据交换无需系统调用和内存复制
2. *灵活映射*：支持自动地址分配和指定地址附加
3. *安全隔离*：基于uid/gid的权限控制
4. *资源管理*：引用计数+延迟删除+自动清理，防止内存泄漏
5. *标准兼容*：遵循System V IPC规范，API与Linux兼容

=== 性能优势

与其他IPC机制对比：

#figure(
  table(
    align: center,
    columns: (auto, auto, auto, auto),
    row-gutter: auto,
    inset: 10pt,
    [IPC机制],
    [数据复制次数],
    [系统调用次数],
    [典型延迟],
    [管道（Pipe）],
    [2次（用户→内核→用户）],
    [2次（write+read）],
    [~10 µs],
    [信号（Signal）],
    [0次],
    [1次（kill）],
    [~5 µs],
    [*共享内存*],
    [*0次（直接访问）*],
    [*0次（使用时）*],
    [*~0.1 µs*],
  ),
  caption: [共享内存 vs 其他IPC性能对比]
)

使用共享内存后，进程间通信带宽可达数GB/s（仅受内存带宽限制），延迟降低至纳秒级。


== 消息队列

=== 设计背景与目标

消息队列（Message Queue）是典型的*异步* IPC 机制：发送者只需把消息写入内核缓冲区即可返回，接收者按需读取，二者在时间上解耦。与共享内存强调“共享一块地址空间”不同，消息队列更像“内核托管的邮箱”，它提供：

- *有界缓冲*：内核维护容量上限，避免无限制积压；
- *消息类型过滤*：接收者可以选择接收特定类型或按优先级取消息；
- *阻塞与非阻塞语义*：队列满/空时，按标志阻塞或立即返回；
- *权限控制与统计信息*：提供与 System V IPC 兼容的元数据。

在 RuOS 中，消息队列主要用于“进程间松耦合通知 + 小数据传递”的场景，既避免了共享内存的并发协作复杂度，又比信号携带更丰富的信息。

=== 机制概览

消息队列由*全局队列表*管理，每个队列包含一个链表形式的消息缓冲区。发送方调用 `msgsnd`，内核将用户态消息拷贝到内核缓冲区并追加到队尾；接收方调用 `msgrcv`，按类型过滤规则从链表中取出合适的消息并拷贝回用户态。

系统约束：
- 最多支持 64 个队列（`MSG_MAX_QUEUES`）；
- 每个队列最多 32 条消息（`MSG_MAX_MESSAGES`）；
- 单条消息最大 4KB（`MSG_MAX_SIZE`）；
- 队列字节容量默认 16KB（`msg_qbytes`）。

队列的核心机制图如 @ruos-msgqueue-core 所示：

#figure(
  image("msg_queue.png"),
  caption: [RuOS 消息队列核心机制图]
) <ruos-msgqueue-core>

=== 关键数据结构

消息队列遵循 System V IPC 结构化元数据的风格，其核心结构如下：

*用户空间消息结构*：
```c
struct msgbuf {
  long mtype;   // 消息类型（必须>0）
  char mtext[1];
};
```

*内核消息节点*（链表节点）：
```c
struct msg {
  struct msg *next;
  long mtype;
  uint msize;
  char *mdata;  // 由 kalloc 分配的数据缓冲区
};
```

*队列描述符*：
```c
struct msg_queue {
  int valid;                // 槽位是否使用中
  int key;                  // IPC 键值
  struct msqid_ds ds;       // 权限与统计信息
  struct spinlock lock;     // 队列自旋锁
  struct msg *head, *tail;  // 消息链表头/尾
  uint msgcount;            // 当前消息数
  uint bytes_used;          // 当前字节数
  void *send_chan;          // 发送者等待通道
  void *recv_chan;          // 接收者等待通道
};
```

其中 `msqid_ds` 保存了权限、时间戳、发送/接收进程 PID 以及容量限制等元信息，方便用户态通过 `msgctl(IPC_STAT)` 观察队列状态。

=== 发送与接收流程

消息队列的发送与接收流程可以概括为“校验 → 拷贝 → 链表操作 → 唤醒”。整体流程可用如下 DOT 描述（可直接用于渲染工具）：

```dot
digraph MsgQueueFlow {
  rankdir=LR;
  node [shape=box, style=rounded];
  Send [label="msgsnd\n(copyin + enqueue)"];
  Recv [label="msgrcv\n(dequeue + copyout)"];
  Q [label="Kernel Queue\n(head/tail)"];
  WaitS [label="sleep(send_chan)"];
  WaitR [label="sleep(recv_chan)"];

  Send -> Q [label="append"];
  Q -> Recv [label="match type"];
  Send -> WaitS [label="full & !NOWAIT"];
  Recv -> WaitR [label="empty & !NOWAIT"];
  WaitS -> Send [label="wakeup"];
  WaitR -> Recv [label="wakeup"];
}
```

*发送（msgsnd）*：
- 校验 `mtype > 0` 与 `msgsz <= MSG_MAX_SIZE`；
- 若队列已满或容量不足：`IPC_NOWAIT` 直接返回错误；否则睡眠等待；
- 拷贝用户态消息到内核缓冲区，追加到队尾；
- 更新 `msg_qnum`、`bytes_used` 与发送 PID；
- 唤醒等待接收的进程。

*接收（msgrcv）*：
- 按类型过滤规则从链表中找到匹配消息；
- 若队列空或无匹配消息：`IPC_NOWAIT` 直接返回错误；否则睡眠等待；
- 若缓冲区过小：`MSG_NOERROR` 允许截断，否则返回错误并将消息放回队列；
- 将消息拷贝回用户态，释放内核缓冲区；
- 更新 `msg_qnum`、`bytes_used` 与接收 PID；
- 唤醒等待发送的进程。

=== 类型过滤语义

RuOS 兼容 System V 消息队列的接收语义：

- `msgtyp == 0`：按 FIFO 取队首消息；
- `msgtyp > 0`：取第一个类型等于 `msgtyp` 的消息；
- `msgtyp < 0`：取类型 `<= |msgtyp|` 的*最小类型*消息（优先级模式）。

这一语义允许系统在同一队列中承载多种“消息通道”，并提供简单的优先级选择能力，适合事件驱动模型中的“高优先级任务先处理”场景。

=== 阻塞、唤醒与并发安全

队列操作由自旋锁保护，同时配合睡眠/唤醒实现阻塞语义：

- *队列满*：发送者在 `send_chan` 上睡眠，等待接收方取走消息并唤醒；
- *队列空*：接收者在 `recv_chan` 上睡眠，等待发送方投递消息并唤醒；
- 队列删除后，内核会唤醒所有阻塞线程并返回错误，避免永久阻塞。

这种模式保留了“内核可控的调度点”，不会造成 busy-wait，对 CPU 更友好。

=== 生命周期管理

消息队列的生命周期包含：

1. *创建/打开*：`msgget(key, IPC_CREAT)` 创建或获取队列；
2. *使用*：通过 `msgsnd`/`msgrcv` 收发消息；
3. *查询/配置*：`msgctl(IPC_STAT/IPC_SET)` 获取或修改权限与容量；
4. *删除*：`msgctl(IPC_RMID)` 删除队列，释放消息链表与内存。

删除时，所有队列内消息都会被回收；若有进程阻塞在该队列上，会被唤醒并返回失败。

=== 权限与资源约束

与共享内存类似，消息队列也遵循 `ipc_perm` 权限模型：

- `uid/gid`：所有者身份；
- `mode`：访问权限位（如 0666）；
- `seq`：序列号用于唯一性标识；
- `msg_qbytes`：队列最大字节容量，可通过 `IPC_SET` 调整。

从资源角度，消息队列在“系统级上限”与“队列级上限”之间做双重约束，保证高负载场景下不会被单个队列耗尽内存。

=== 典型用法（精简示意）

```c
// 发送方
int qid = msgget(0x3344, IPC_CREAT | 0666);
struct {
  long type;
  char text[32];
} msg = { .type = 1, .text = "ping" };
msgsnd(qid, &msg, sizeof(msg.text), 0);

// 接收方
struct {
  long type;
  char text[32];
} out;
msgrcv(qid, &out, sizeof(out.text), 1, 0);
```

实际文档中更强调接口语义而非示例代码，原因在于消息队列的价值主要体现在“解耦与调度策略”上：发送者无需等待对端准备好，接收者也无需轮询。

=== 与其他 IPC 的权衡

- *信号*：开销极低，但信息承载有限，更多用作“事件提示”；
- *消息队列*：适合小数据异步传递，具备顺序与类型过滤；
- *共享内存*：性能最强，但需要显式同步（锁、信号量等）。

消息队列在“复杂度/性能/可靠性”的折中上更均衡：避免共享内存的并发同步负担，也避免信号“只有发生/未发生”的信息贫乏问题。

=== 实现亮点

1. *异步缓冲*：发送与接收时序解耦，支持阻塞与非阻塞；
2. *类型过滤*：兼容 System V 语义，支持优先级接收；
3. *可控容量*：双重限额防止单队列占用过多内存；
4. *明确生命周期*：统一由 `msgctl` 管理资源释放；
5. *易于观测*：`IPC_STAT` 可直接查看队列状态与统计信息。

综合来看，RuOS 的消息队列是一种*面向工程可用性*的 IPC 机制：在保持理论语义清晰的同时，也兼顾了调度、安全与资源管理的工程需求。

