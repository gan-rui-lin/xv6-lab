# SYS_set_robust_list 系统调用实现

## 问题描述

运行测试时，所有程序都报告：
```
unimplemented sys call SYS_set_robust_list
```

这是 glibc 使用的线程同步机制（robust futex list），用于处理线程突然终止时的互斥锁清理。虽然不影响基本功能，但会产生大量警告信息。

## 解决方案

实现了 `SYS_set_robust_list` 系统调用的基本支持。xv6 不需要完整的 futex 实现，只需要存储用户空间传递的指针，让 glibc 正常运行即可。

## 修改的文件

### 1. `src/proc/proc.h`

在 `struct proc` 中添加两个字段存储 robust list 信息：

```c
struct proc {
  // ... 其他字段 ...

  // robust futex list (glibc 线程支持)
  uint64 robust_list_head;         // pointer to user-space robust_list_head
  uint64 robust_list_len;          // length passed by user
};
```

**位置**: Line 99-101 (在 is_kthread 等字段之后)

### 2. `src/proc/proc.c`

在 `allocproc()` 函数中初始化新字段：

```c
p->priority = PRIO_DEFAULT;
p->clear_child_tid = 0;
p->exit_signal = SIGCHLD;
p->robust_list_head = 0;      // ← 新增
p->robust_list_len = 0;       // ← 新增
signal_init(p);
```

**位置**: Line 149-150

### 3. `src/syscall/sysproc.c`

实现 `sys_set_robust_list()` 函数：

```c
// set_robust_list: 设置 robust futex 列表（glibc 线程支持）
// 参数：
//   a0: head - 指向用户空间 robust_list_head 结构的指针
//   a1: len - 结构体大小
// 返回：0 表示成功
uint64
sys_set_robust_list(void)
{
  uint64 head;
  uint64 len;
  struct proc *p = myproc();

  if(argaddr(0, &head) < 0)
    return -EFAULT;
  if(argaddr(1, &len) < 0)
    return -EFAULT;

  // 简单存储这些值，xv6 不需要实际处理 robust futex
  // 这只是为了让 glibc 程序能够正常运行
  p->robust_list_head = head;
  p->robust_list_len = len;

  return 0;
}
```

**位置**: Line 1304-1327 (文件末尾)

### 4. `src/syscall/syscall.c`

#### 4.1 添加函数原型声明

```c
extern uint64 sys_setpriority(void);
extern uint64 sys_getpriority(void);
extern uint64 sys_set_robust_list(void);  // ← 新增
```

**位置**: Line 193

#### 4.2 在系统调用表中注册

```c
static uint64 (*syscalls[])(void) = {
    // ... 其他系统调用 ...
    [SYS_listen] sys_listen,
    [SYS_accept] sys_accept,
    [SYS_xv6_sbrk] sys_sbrk,
    [SYS_set_robust_list] sys_set_robust_list,  // ← 新增
};
```

**位置**: Line 279

## 技术细节

### Linux set_robust_list 系统调用

```c
long set_robust_list(struct robust_list_head *head, size_t len);
```

- **head**: 指向用户空间 `robust_list_head` 结构的指针
- **len**: 结构体大小（用于版本兼容性检查）
- **返回值**: 成功返回 0，失败返回负的错误码

### Robust Futex 机制

Robust futex 是 Linux 提供的一种机制，用于处理持有互斥锁的线程突然终止的情况。当线程退出时，内核会自动清理该线程持有的 robust mutex，防止死锁。

在 xv6 中，我们不需要实现完整的清理逻辑，只需要：
1. 接受并存储用户空间传递的指针
2. 返回成功，让 glibc 认为系统支持这个功能

## 测试结果

### 修复前
```
7 brk: unimplemented sys call SYS_set_robust_list
========== START test_brk ==========
...
```

每个测试程序都会打印 "unimplemented sys call" 警告。

### 修复后
```
========== START test_brk ==========
Before alloc,heap pos: 200704
After alloc,heap pos: 200768
...
```

不再有 SYS_set_robust_list 警告，测试可以正常运行。

## 注意事项

1. **这是一个最小实现**：xv6 只存储指针，不进行实际的 robust futex 清理
2. **线程支持有限**：xv6 的线程模型简化，不需要完整的 futex 支持
3. **兼容性良好**：glibc 可以正常运行，即使内核不提供完整功能

## 相关系统调用

- `SYS_set_robust_list` (99) - 设置 robust list
- `SYS_get_robust_list` (100) - 获取 robust list（未实现）
- `SYS_futex` (98) - futex 操作（部分实现）

## 参考资料

- Linux man page: `set_robust_list(2)`
- glibc pthread implementation
- Linux kernel: `kernel/futex.c`

---

**修复日期**: 2026-01-28
**影响范围**: 所有使用 glibc 的用户程序
**状态**: ✅ 已完成
