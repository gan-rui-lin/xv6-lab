# 移除 xv6 系统调用与类型定义修复

**日期**: 2026-01-04  
**目的**: 移除绝大部分 xv6 式系统调用，统一使用 Linux 系统调用接口；添加 POSIX 类型定义

## 主要改动

### 1. 移除 xv6 系统调用依赖

**问题**: 项目混用 xv6 和 Linux 风格的系统调用，导致维护困难

**解决方案**:
- 移除对 `usys.S` 的依赖
- 所有用户态系统调用包装函数统一在 `user/ulib.c` 中实现
- 使用 `syscall.h` 中定义的内联汇编宏进行系统调用

**修改文件**:
- `user/ulib.c`: 添加所有必需的系统调用包装函数
  - `fork()`: 通过 `SYS_clone` 实现
  - `exit()`: 调用 `SYS_exit`，添加 `__builtin_unreachable()`
  - `wait()`: 通过 `waitpid(-1, ...)` 实现
  - `waitpid()`: 新增，调用 `SYS_wait4`
  - `read()`, `write()`, `open()`, `close()` 等基础 I/O 函数
  - `exec()`: 接受 `argv` 参数，调用 `SYS_execve`
  - `dup()`: 新增，调用 `SYS_dup`
  - `sys_unlinkat()`: 新增辅助函数
  - 其他辅助函数：`mkdir()`, `chdir()`, `getpid()`, `mknod()` 等

- `Makefile`: 修改 `user/initcode` 链接规则
  - 添加 `user/ulib.o` 和 `user/umalloc.o` 到链接依赖
  - 确保所有函数符号能正确解析

### 2. 添加 POSIX 类型定义

**问题**: 缺少标准 POSIX 类型导致编译错误

**解决方案**: 在 `src/types.h` 中添加类型定义

```c
typedef int pid_t;
typedef long ssize_t;
typedef unsigned int mode_t;
typedef long clock_t;
typedef unsigned long size_t;
```

**修改文件**:
- `src/types.h`: 添加 POSIX 类型定义
- `user/ulib.c`: 函数签名统一使用 `int` 等基本类型（与 `user.h` 保持一致）

### 3. 修复头文件包含问题

**问题**: `fs.h` 被用户态代码包含，但缺少类型定义

**解决方案**: 在 `src/fs/fs.h` 开头添加 `#include "types.h"`

**影响**: 
- 解决了 `uint`, `uint64`, `ushort` 等类型未定义的错误
- 用户态和内核态都能正确使用文件系统头文件

### 4. 修复 SBI 函数链接问题

**问题**: `sbi_shutdown()` 和 `sbi_set_timer()` 被标记为 `inline`，导致链接器找不到符号

**解决方案**: 移除 `src/lib/sbi.c` 中的 `inline` 关键字

**修改**:
```c
// 修改前
inline void sbi_shutdown(void)
inline void sbi_set_timer(uint64_t stime)

// 修改后
void sbi_shutdown(void)
void sbi_set_timer(uint64_t stime)
```

**原因**: `inline` 函数在每个编译单元内联展开，不会生成外部符号，导致链接失败

## 编译验证

**编译命令**: `make all`

**结果**: 编译成功，退出码 0

**验证点**:
1. 所有系统调用包装函数正确链接
2. 类型定义一致性检查通过
3. `user/initcode` 成功链接 `ulib.o` 和 `umalloc.o`
4. SBI 函数符号正确解析

## 注意事项

1. **函数签名一致性**: `user/ulib.c` 中的实现必须与 `user/user.h` 中的声明完全匹配
2. **exit() 特殊处理**: 标记为 `noreturn`，需要 `__builtin_unreachable()` 避免编译器警告
3. **exec() 参数**: 现在接受 `argv` 参数，符合 POSIX 标准
4. **类型使用**: 虽然定义了 POSIX 类型，但为保持兼容性，函数签名仍使用基本类型（如 `int` 而非 `pid_t`）

## 后续工作

- [ ] 逐步将函数签名迁移到 POSIX 类型（如使用 `pid_t` 代替 `int`）
- [ ] 完全移除 `SYS_xv6_*` 系列系统调用号
- [ ] 统一错误处理机制
- [ ] 添加更多 POSIX 兼容的系统调用包装

## 相关文件

- `user/ulib.c` - 系统调用包装实现
- `user/user.h` - 用户态 API 声明
- `src/types.h` - 类型定义
- `src/fs/fs.h` - 文件系统头文件
- `src/lib/sbi.c` - SBI 接口实现
- `Makefile` - 构建规则
