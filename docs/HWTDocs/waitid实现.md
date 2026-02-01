## waitid 系统调用实现详解

### 一、需求分析

`waitid` 是 POSIX 标准中用于等待子进程状态变化的系统调用。相比传统的 `wait` 和 `waitpid`，它提供了更丰富的功能：

**核心能力：**

- 可以等待特定的一个子进程（通过 PID）
- 可以等待某个进程组内的任意子进程（通过 PGID）
- 可以等待当前进程的任意子进程
- 支持非阻塞模式，调用后立即返回
- 能够区分子进程是正常退出还是被信号杀死，并返回详细信息

---

### 二、整体设计思路

#### 第一步：理解调用接口

```c
int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);
```

这个函数有四个参数：

1. **idtype**：告诉内核要等待什么类型的目标

   - `P_ALL`：等待任意子进程，忽略 id 参数
   - `P_PID`：等待 PID 等于 id 的那个子进程
   - `P_PGID`：等待进程组 ID 等于 id 的任意子进程
2. **id**：配合 idtype 使用的标识符
3. **infop**：指向用户空间的 `siginfo_t` 结构，内核会把子进程的详细状态信息写入这里
4. **options**：控制等待行为的标志位

   - `WEXITED`：关心已退出的子进程
   - `WNOHANG`：非阻塞模式，没有可收集的子进程时立即返回
   - `WNOWAIT`：不真正回收子进程，只是查看状态

#### 第二步：理解返回信息

`siginfo_t` 结构中最重要的几个字段：

- `si_pid`：发生状态变化的子进程 PID
- `si_code`：状态变化的原因
  - `CLD_EXITED`：子进程调用 `exit()` 正常退出
  - `CLD_KILLED`：子进程被信号杀死
- `si_status`：
  - 如果是正常退出，这里是退出码（`exit(123)` 中的 123）
  - 如果被信号杀死，这里是杀死它的信号号（如 `SIGKILL` = 9）

#### 第三步：理解进程退出状态的编码

xv6 中进程退出时，会把退出状态保存在 `xstate` 字段中。这个字段的编码方式与 Linux 兼容：

**正常退出时**（调用 `exit(status)`）：

```
xstate = (status & 0xff) << 8
```

也就是把退出码放在高 8 位，低 7 位全是 0。

**被信号杀死时**：

```
xstate = signal_number & 0x7f
```

信号号直接放在低 7 位。

所以解析时，只需要检查 `xstate & 0x7f`：

- 如果是 0，说明是正常退出，退出码在 `(xstate >> 8) & 0xff`
- 如果不是 0，说明被信号杀死，信号号就是 `xstate & 0x7f`

---

### 三、实现步骤详解

#### 步骤一：添加进程组支持

要支持 `P_PGID` 类型的等待，首先需要让每个进程都有一个进程组 ID（pgid）。

**在 `proc` 结构体中添加 `pgid` 字段**：每个进程都要记住自己属于哪个进程组。

**初始化规则**：

- 新创建的进程，默认的进程组 ID 就是它自己的 PID（自成一组）
- `fork` 出来的子进程，继承父进程的进程组 ID
- 进程可以通过 `setpgid` 系统调用改变自己或子进程的进程组

#### 步骤二：实现 waitid 的核心逻辑

整个函数的执行流程如下：

**1. 参数获取与验证**

首先从用户态获取四个参数，然后进行合法性检查：

- `idtype` 必须是 `P_ALL`、`P_PID` 或 `P_PGID` 之一
- `options` 至少要包含 `WEXITED`（目前只支持这个，`WSTOPPED` 和 `WCONTINUED` 暂不实现）

如果参数非法，直接返回 `-EINVAL`。

**2. 进入等待循环**

这是一个可能阻塞的循环，直到找到符合条件的子进程或确定没有可等待的子进程：

```
循环开始：
    havekids = 0  // 标记是否有匹配的子进程

    遍历系统中的所有进程：
        如果这个进程的父进程是当前进程：
            根据 idtype 检查是否匹配：
                P_ALL：任何子进程都匹配
                P_PID：只有 PID == id 的子进程匹配
                P_PGID：只有 pgid == id 的子进程匹配
        
            如果匹配：
                havekids = 1  // 至少有一个匹配的子进程
            
                如果这个子进程是 ZOMBIE 状态（已退出但未被回收）：
                    找到了！收集它的信息并返回

    如果 havekids == 0：
        // 没有任何匹配的子进程存在
        返回 -ECHILD（没有子进程错误）
  
    如果设置了 WNOHANG 标志：
        // 非阻塞模式，有子进程但都还没退出
        把 siginfo_t 清零（特别是 si_pid = 0 表示没有状态变化）
        返回 0
  
    // 阻塞模式：睡眠等待，直到有子进程退出时被唤醒
    sleep(当前进程)
  
循环继续...
```

**3. 找到 ZOMBIE 子进程后的处理**

当找到一个已退出（ZOMBIE 状态）的匹配子进程时：

```
解析 xstate：
    termsig = xstate & 0x7f
  
    如果 termsig == 0：
        // 正常退出
        si_code = CLD_EXITED
        si_status = (xstate >> 8) & 0xff  // 退出码
    否则：
        // 被信号杀死
        si_code = CLD_KILLED
        si_status = termsig  // 信号号

填充 siginfo_t 结构：
    si_signo = SIGCHLD
    si_pid = 子进程的 PID
    si_uid = 子进程的 UID
    si_code = 如上计算
    si_status = 如上计算

如果没有设置 WNOWAIT 标志：
    // 真正回收子进程
    释放子进程的内核栈、页表等资源
    把子进程状态设为 UNUSED

把 siginfo_t 结构复制到用户空间

返回 0
```

#### 步骤三：处理边界情况

**非阻塞模式下没有退出的子进程**：

- 根据 POSIX 标准，此时返回 0，但 `siginfo_t` 中的 `si_pid` 应该是 0
- 这样调用者可以区分"成功收集到子进程"和"暂时没有子进程退出"

**没有匹配的子进程**：

- 返回 `-ECHILD` 错误码
- 这表示要等待的目标根本不存在（比如指定的 PID 不是自己的子进程）

---

### 四. 核心实现逻辑

```
waitid(idtype, id, infop, options)
    │
    ├── 1. 参数验证
    │       ├── idtype 必须是 P_ALL/P_PID/P_PGID
    │       └── options 必须包含 WEXITED/WSTOPPED/WCONTINUED 之一
    │
    ├── 2. 进入等待循环
    │       │
    │       ├── 遍历所有进程，找到匹配的子进程
    │       │       ├── 检查 parent == 当前进程
    │       │       └── 根据 idtype 匹配：P_ALL(任意) / P_PID(指定PID) / P_PGID(进程组)
    │       │
    │       ├── 若找到 ZOMBIE 状态的子进程
    │       │       ├── 解析 xstate 确定退出原因
    │       │       │       ├── (xstate & 0x7f) == 0 → 正常退出，si_code = CLD_EXITED
    │       │       │       └── (xstate & 0x7f) != 0 → 被信号杀死，si_code = CLD_KILLED
    │       │       ├── 填充 siginfo_t 结构（si_pid, si_status, si_code 等）
    │       │       ├── 若无 WNOWAIT 标志，回收子进程资源
    │       │       └── 返回 0
    │       │
    │       ├── 若有子进程但无 ZOMBIE
    │       │       ├── WNOHANG 模式：清零 siginfo_t，立即返回 0
    │       │       └── 阻塞模式：sleep 等待子进程状态变化
    │       │
    │       └── 若无匹配的子进程 → 返回 -ECHILD
    │
    └── 3. 将 siginfo_t 复制到用户空间
```

#### 3. xstate 编码规则（与 Linux 兼容）

```c
// 正常退出: exit(status)
xstate = (status & 0xff) << 8;  // 退出码在高8位，低7位为0

// 被信号杀死: killed by signal
xstate = sig & 0x7f;            // 信号号在低7位
```

解析时：

```c
int termsig = xstate & 0x7f;
if (termsig == 0) {
    // 正常退出
    si_code = CLD_EXITED;
    si_status = (xstate >> 8) & 0xff;  // 退出码
} else {
    // 被信号杀死
    si_code = CLD_KILLED;
    si_status = termsig;  // 杀死进程的信号
}
```

---

### 五、所有改动点

#### 1. proc.h - 进程结构体添加 pgid 字段

```c
struct proc {
    // ... 其他字段
    int pgid;                  // 进程组 ID (新增)
    // ...
};
```

**作用**：支持 `P_PGID` 类型的进程组等待

#### 2. proc.c - 进程初始化

**allocproc 函数**（分配新进程时初始化 pgid）：

```c
p->pgid = p->pid;  // 默认进程组 ID = 自己的 PID
```

**clone_fork 函数**（fork 时继承父进程的 pgid）：

```c
np->pgid = p->pgid;  // 子进程继承父进程的进程组
```

**freeproc 函数**（释放进程时重置 pgid）：

```c
p->pgid = 0;
```

#### 3. sysproc.c - 类型定义和 waitid 实现

**新增类型定义**：

```c
// waitid 相关类型定义
typedef enum { P_ALL = 0, P_PID = 1, P_PGID = 2 } idtype_t;
typedef int id_t;

// siginfo_t 结构体（用于 waitid 返回详细信息）
typedef struct {
    int si_signo;      // 信号号
    int si_errno;      // 错误码
    int si_code;       // 信号代码 (CLD_EXITED, CLD_KILLED 等)
    int si_pid;        // 发送信号的进程 ID
    int si_uid;        // 发送信号的用户 ID
    int si_status;     // 退出值或信号
    // ... 其他字段用于对齐
} siginfo_t;

// si_code 常量
#define CLD_EXITED    1   // 子进程正常退出
#define CLD_KILLED    2   // 子进程被信号杀死
#define CLD_STOPPED   5   // 子进程被停止
#define CLD_CONTINUED 6   // 子进程继续执行
```

**sys_setpgid 修复**：

```c
// pgid == 0 时使用目标进程的 PID 作为 PGID
if(pgid == 0)
    pgid = target->pid;
```

**sys_waitid 完整实现**：约 100 行代码，包含：

- `waitid_match()` - 匹配等待条件
- `waitid_fill_siginfo()` - 填充 siginfo_t 结构
- `sys_waitid()` - 主函数，实现等待循环

#### 4. syscall.c - 系统调用注册

**外部声明**：

```c
extern uint64 sys_waitid(void);
```

**系统调用表**（已存在，确认注册）：

```c
[SYS_waitid]    sys_waitid,
```

---

### 六、测试点对应

| 测试点 | 功能         | 对应实现                                     |
| ------ | ------------ | -------------------------------------------- |
| 1      | 基本退出等待 | `P_PID` + `WEXITED`，验证 `CLD_EXITED` |
| 2      | 非阻塞等待   | `WNOHANG` 标志，无状态变化时 `si_pid=0`  |
| 3      | 被信号杀死   | 解析 `xstate & 0x7f`，返回 `CLD_KILLED`  |
| 4      | 进程组等待   | `P_PGID` 匹配 `child->pgid == id`        |
