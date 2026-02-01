# 系统调用 waitid

`waitid` 系统调用提供了比传统的 `wait` 和 `waitpid` 更精细的子进程状态控制接口。它允许父进程等待子进程的特定状态改变（如退出、停止、继续），并通过 `siginfo_t` 结构体返回非常详细的事件信息。

#### 函数原型

```c
#include <sys/wait.h>

int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);
```

#### 参数

- **idtype**: 指定 `id` 参数的解释方式。
  - `P_PID`: 等待 PID 等于 `id` 的子进程。
  - `P_PGID`: 等待进程组 ID 等于 `id` 的任意子进程。
  - `P_ALL`: 等待任意子进程，忽略 `id`。
- **id**: 根据 `idtype` 指定的 PID 或 PGID。
- **infop**: 指向 `siginfo_t` 结构体的指针，用于内核填充子进程的状态信息（如 `si_pid`, `si_status`, `si_code`）。
- **options**: 行为控制掩码。
  - `WEXITED`: 等待已结束的子进程。
  - `WSTOPPED`: 等待被信号暂停的子进程。
  - `WCONTINUED`: 等待被信号恢复的子进程。
  - `WNOHANG`: 非阻塞模式，如果无子进程状态改变则立即返回。
  - `WNOWAIT`: 状态改变后不消耗该状态（保持僵尸状态），可供后续等待。

#### 返回值

- 成功时返回 0。如果设置了 `WNOHANG` 且没有状态改变，也返回 0，此时 `infop->si_pid` 应为 0。
- 出错时返回 -1 并设置 errno。

#### 特殊说明

1. **状态反馈**：内核必须正确填充 `siginfo_t` 中的关键字段：
   - `si_pid`: 发生事件的子进程 ID。
   - `si_uid`: 子进程的用户 ID。
   - `si_code`: 事件类型（`CLD_EXITED` 正常退出, `CLD_KILLED` 被杀, `CLD_STOPPED` 暂停, `CLD_CONTINUED` 继续）。
   - `si_status`: 退出码或导致状态改变的信号值。
2. **进程状态机**：内核需维护进程的 `Stopped` 和 `Continued` 状态，并在状态切换时唤醒等待的父进程。

#### 测试点说明

本题目的评测包含 4 个测试点，每个测试点12.5分：

1. **基本退出等待**：等待子进程正常退出，验证 `si_code` 为 `CLD_EXITED` 及退出码正确。
2. **非阻塞等待**：使用 `WNOHANG`，验证子进程运行时返回 0，退出后成功捕获状态。
3. **被信号杀死**：子进程被 `SIGKILL` 杀死，验证 `si_code` 为 `CLD_KILLED` 及信号值。
4. **进程组等待**：创建属于同一进程组的多个子进程，验证使用 `P_PGID` 能正确回收组内任意进程。
