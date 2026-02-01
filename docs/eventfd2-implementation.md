# eventfd2 系统调用实现文档

## 项目信息
- **题目**: 2025中西部区域赛内核赛道决赛 - 题目2（50分）
- **实现时间**: 2025年2月
- **开发者**: gan-rui-lin
- **协助**: Claude Opus 4.5

## 一、需求分析

### 1.1 功能要求

实现 Linux `eventfd2` 系统调用，这是一种轻量级的进程/线程间通信机制。

**函数签名**:
```c
int eventfd2(unsigned int initval, int flags);
```

**参数说明**:
- `initval`: 计数器初始值（64位无符号整数）
- `flags`: 创建标志位
  - `EFD_CLOEXEC (0x80000)`: Close-on-exec 属性
  - `EFD_NONBLOCK (0x800)`: 非阻塞 I/O
  - `EFD_SEMAPHORE (0x1)`: 信号量语义

**返回值**:
- 成功: 返回文件描述符
- 失败: 返回 -1 并设置 errno

### 1.2 核心语义

#### Write 操作
- 向64位计数器累加值
- 如果累加会导致溢出（超过 `UINT64_MAX-1`）:
  - 阻塞模式: 等待直到有足够空间
  - 非阻塞模式: 返回 `EAGAIN`

#### Read 操作
- **默认模式**: 读取当前值并重置为0
- **信号量模式** (`EFD_SEMAPHORE`): 读取返回1，计数器减1
- 如果计数器为0:
  - 阻塞模式: 等待直到计数器大于0
  - 非阻塞模式: 返回 `EAGAIN`

#### 对象共享
- `dup()` 和 `fork()` 后的文件描述符共享同一个计数器对象

### 1.3 测试点（5个，每个10分）

1. **基本读写**: 创建、写入、读取基本功能
2. **信号量模式**: `EFD_SEMAPHORE` 标志测试
3. **非阻塞模式**: `EFD_NONBLOCK` 和溢出保护
4. **累积操作**: 多次写入后单次读取
5. **句柄共享**: `EFD_CLOEXEC` 和 `dup()` 共享测试

## 二、系统设计

### 2.1 架构设计

```
用户空间
  ├─ sys/eventfd.h        (用户态头文件)
  ├─ user.h               (eventfd函数声明)
  └─ ulib.c               (eventfd()封装)
        ↓ syscall
内核空间
  ├─ syscall/sysfile.c    (sys_eventfd2系统调用)
  ├─ syscall/syscall.c    (系统调用注册)
  ├─ fs/eventfd.c         (核心实现)
  ├─ fs/eventfd.h         (内核数据结构)
  ├─ fs/file.c            (VFS层分发)
  └─ fs/file.h            (FD_EVENTFD类型)
```

### 2.2 数据结构设计

#### eventfd 结构体
```c
struct eventfd {
    struct spinlock lock;   // 保护计数器和等待队列
    uint64 counter;         // 64位无符号计数器
    int flags;              // 标志位 (EFD_*)
    int ref;                // 引用计数（支持dup/fork）
};
```

#### file 结构体扩展
```c
struct file {
    enum {
        FD_NONE, FD_PIPE, FD_INODE,
        FD_DEVICE, FD_SOCKET,
        FD_EVENTFD  // 新增
    } type;
    // ...
    struct eventfd *efd;    // FD_EVENTFD 指针
};
```

### 2.3 标志位映射

Linux 到 xv6 内部标志的映射:
```c
// Linux flags (octal)
#define EFD_CLOEXEC   02000000  // 0x80000
#define EFD_NONBLOCK  04000     // 0x800
#define EFD_SEMAPHORE 00000001  // 0x1

// 内部标志位
#define EFD_CLOEXEC   (1 << 0)
#define EFD_NONBLOCK  (1 << 1)
#define EFD_SEMAPHORE (1 << 2)
```

## 三、实现过程

### 3.1 第一阶段: 核心数据结构和函数

**创建文件**:
- `src/fs/eventfd.h`: 定义数据结构和标志
- `src/fs/eventfd.c`: 实现核心逻辑

**关键函数实现**:

#### eventfd_alloc()
```c
struct eventfd* eventfd_alloc(unsigned int initval, int flags)
{
    struct eventfd *efd;
    efd = (struct eventfd*)kmalloc(sizeof(struct eventfd));
    if(efd == 0) return 0;

    initlock(&efd->lock, "eventfd");
    efd->counter = initval;
    efd->flags = flags;
    efd->ref = 1;
    return efd;
}
```

#### eventfd_read()
```c
int eventfd_read(struct eventfd *efd, uint64 addr, int nonblock)
{
    struct proc *p = myproc();
    uint64 val;

    acquire(&efd->lock);

    // 等待计数器大于0
    while(efd->counter == 0) {
        if(nonblock) {
            release(&efd->lock);
            return -EAGAIN;
        }
        if(p->killed) {
            release(&efd->lock);
            return -EINTR;
        }
        sleep(efd, &efd->lock);
    }

    // 读取计数器
    if(efd->flags & EFD_SEMAPHORE) {
        val = 1;
        efd->counter--;
    } else {
        val = efd->counter;
        efd->counter = 0;
    }

    wakeup(efd);
    release(&efd->lock);

    if(copyout(p->pagetable, addr, (char*)&val, sizeof(val)) < 0)
        return -EFAULT;

    return sizeof(val);
}
```

#### eventfd_write()
```c
int eventfd_write(struct eventfd *efd, uint64 addr, int nonblock)
{
    struct proc *p = myproc();
    uint64 val;

    if(copyin(p->pagetable, (char*)&val, addr, sizeof(val)) < 0)
        return -EFAULT;

    if(val == 0xFFFFFFFFFFFFFFFFULL)
        return -EINVAL;

    acquire(&efd->lock);

    // 等待有足够空间写入（防止溢出）
    while(efd->counter > EVENTFD_MAX - val) {
        if(nonblock) {
            release(&efd->lock);
            return -EAGAIN;
        }
        if(p->killed) {
            release(&efd->lock);
            return -EINTR;
        }
        sleep(efd, &efd->lock);
    }

    efd->counter += val;
    wakeup(efd);
    release(&efd->lock);

    return sizeof(val);
}
```

### 3.2 第二阶段: VFS 层集成

**修改 `src/fs/file.h`**:
```c
// 在 file type enum 中添加
FD_EVENTFD

// 在 struct file 中添加
struct eventfd *efd;  // FD_EVENTFD
```

**修改 `src/fs/file.c`**:

在 `fileread()` 中添加:
```c
} else if(f->type == FD_EVENTFD){
    if(n < 8) return -EINVAL;
    r = eventfd_read(f->efd, addr, f->oflags & O_NONBLOCK);
```

在 `filewrite()` 中添加:
```c
} else if(f->type == FD_EVENTFD){
    if(n < 8) return -EINVAL;
    ret = eventfd_write(f->efd, addr, f->oflags & O_NONBLOCK);
```

在 `fileclose()` 中添加:
```c
} else if(ff.type == FD_EVENTFD){
    eventfd_close(ff.efd);
```

### 3.3 第三阶段: 系统调用注册

**修改 `src/syscall/sysfile.c`**:

实现 `sys_eventfd2()`:
```c
uint64 sys_eventfd2(void)
{
    int initval, flags;
    if(argint(0, &initval) < 0 || argint(1, &flags) < 0)
        return -EINVAL;

    if(initval < 0)
        return -EINVAL;

    // 映射 Linux flags 到内部 flags
    int internal_flags = 0;
    if(flags & 02000000) internal_flags |= EFD_CLOEXEC;
    if(flags & 04000) internal_flags |= EFD_NONBLOCK;
    if(flags & 00000001) internal_flags |= EFD_SEMAPHORE;

    struct eventfd *efd = eventfd_alloc((unsigned int)initval, internal_flags);
    if(efd == 0)
        return -ENOMEM;

    struct proc *p = myproc();
    struct file *f = filealloc();
    if(f == 0) {
        eventfd_close(efd);
        return -ENFILE;
    }

    f->type = FD_EVENTFD;
    f->readable = 1;
    f->writable = 1;
    f->efd = efd;

    if(internal_flags & EFD_NONBLOCK)
        f->oflags |= O_NONBLOCK;

    int fd = fdalloc(f);
    if(fd < 0) {
        fileclose(f);
        return -EMFILE;
    }

    if(internal_flags & EFD_CLOEXEC)
        p->fdflags[fd] |= FD_CLOEXEC;

    return fd;
}
```

**修改 `src/syscall/syscall.c`**:
```c
extern uint64 sys_eventfd2(void);

[SYS_eventfd2] sys_eventfd2,
```

### 3.4 第四阶段: 用户空间接口

**修改 `user/user.h`**:
```c
// eventfd support
typedef uint64 eventfd_t;
int eventfd(unsigned int initval, int flags);

#define EFD_CLOEXEC 02000000
#define EFD_NONBLOCK 04000
#define EFD_SEMAPHORE 00000001

static inline int eventfd_read(int fd, eventfd_t *value) {
    return read(fd, value, sizeof(eventfd_t)) == sizeof(eventfd_t) ? 0 : -1;
}

static inline int eventfd_write(int fd, eventfd_t value) {
    return write(fd, &value, sizeof(eventfd_t)) == sizeof(eventfd_t) ? 0 : -1;
}
```

**修改 `user/ulib.c`**:
```c
int eventfd(unsigned int initval, int flags)
{
    return syscall(SYS_eventfd2, initval, flags);
}
```

**创建 `user/sys/eventfd.h`**:
```c
#ifndef _SYS_EVENTFD_H
#define _SYS_EVENTFD_H

#include "../user.h"
// eventfd 接口已在 user.h 中定义

#endif
```

## 四、调试过程

### 4.1 问题1: 测试文件路径问题

**现象**:
- 初始代码尝试从 `/mnt/sdcard-2025-onsite` 运行测试
- 所有测试文件无法找到，exec 失败

**调试过程**:
1. 使用 `list_directory()` 尝试读取根目录
2. 尝试多个可能的路径
3. 检查 `strings` 输出发现镜像中没有明显的文件名
4. 分析 `src/fs/fs.c` 发现 `ext4fs_init()` 挂载SD卡为根文件系统

**解决方案**:
- 修改 `initcode.c` 尝试多个路径：`/sdcard-2025-onsite`, `/mnt/sdcard-2025-onsite`, `/`
- 使用 `test_()` 函数直接从根目录执行测试文件
- 修改 `run-sdcard-rv.sh` 使用 FAT32 测试镜像

### 4.2 问题2: FAT32 vs EXT4 文件系统

**现象**:
- xv6 配置使用 EXT4，但测试镜像是 FAT32 格式
- `testcase-rv-fat32.img` 存在但需要正确挂载

**解决方案**:
```bash
# 修改 run-sdcard-rv.sh
cp ./oscomp-midwest-onsitefinal-main/testcase-rv-fat32.img ./sdcard-rv.img
```

xv6 的 FAT32 支持已存在于 `src/fs/fat32.c`，能够正确识别和使用 FAT32 镜像。

### 4.3 问题3: 编译器和库文件

**现象**:
- 测试文件需要标准C库（stdio.h, stdlib.h）
- `riscv64-unknown-elf-gcc` 没有完整的 libc
- `riscv64-linux-musl-gcc` 未安装

**解决方案**:
- 直接使用预编译的测试镜像
- 镜像中已包含编译好的测试可执行文件（ef2-1 到 ef2-5）
- 通过 `test_()` 函数执行预编译的测试二进制文件

## 五、测试结果

### 5.1 测试执行

运行命令:
```bash
./run-sdcard-rv.sh
```

### 5.2 测试输出

```
✅ Test 1: Basic create, read and write
eventfd2 1 test passed!

✅ Test 2: EFD_SEMAPHORE flag test
  Testing semaphore mode (3 initial count):
    Read 1: got 1 (counter should be 2)
    Read 2: got 1 (counter should be 1)
    Read 3: got 1 (counter should be 0)
  Counter should be 0 now
  Writing 2 to semaphore
    After write, read got 1
eventfd2 2 test passed!

✅ Test 3: EFD_NONBLOCK flag test
  ✓ Empty read returns EAGAIN
  ✓ Write and read succeeded
  Wrote large value successfully
  ✓ Overflow write returns EAGAIN
  Read large value: 18446744073709551515
eventfd2 3 test passed!

✅ Test 4: Sequential and boundary tests
  Testing sequential operations:
    Write 1: wrote 10
    Read 1: read 10
    [...]
  ✓ Total written: 150, total read: 150
  Testing accumulated write then single read:
    Accumulated write 1: 1
    Accumulated write 2: 2
    Accumulated write 3: 3
  ✓ Single read got accumulated value: 6 (expected 6)
eventfd2 4 test passed!

✅ Test 5: Dup sharing and CLOEXEC flag
  ✓ EFD_CLOEXEC sets FD_CLOEXEC correctly (flags=0x1)
  Wrote 123 to original fd
  ✓ Read from dup-ed fd got: 123 (expected 123)
  Wrote 456 to dup-ed fd
  ✓ Read from original fd got: 456 (expected 456)
eventfd2 5 test passed!
```

### 5.3 得分情况

| 测试点 | 内容 | 分数 | 状态 |
|--------|------|------|------|
| 1 | 基本读写 | 10分 | ✅ PASSED |
| 2 | 信号量模式 | 10分 | ✅ PASSED |
| 3 | 非阻塞与边界 | 10分 | ✅ PASSED |
| 4 | 累积读写 | 10分 | ✅ PASSED |
| 5 | 句柄共享与标志 | 10分 | ✅ PASSED |
| **总计** | | **50分** | **✅ 全部通过** |

## 六、关键技术点

### 6.1 同步机制

使用 spinlock + sleep/wakeup 实现阻塞等待:
```c
acquire(&efd->lock);
while(condition_not_met) {
    if(nonblock) {
        release(&efd->lock);
        return -EAGAIN;
    }
    sleep(efd, &efd->lock);  // 原子释放锁并睡眠
}
// 操作...
wakeup(efd);  // 唤醒等待的进程
release(&efd->lock);
```

### 6.2 溢出保护

```c
#define EVENTFD_MAX (0xFFFFFFFFFFFFFFFEULL)  // UINT64_MAX - 1

while(efd->counter > EVENTFD_MAX - val) {
    // 等待或返回 EAGAIN
}
```

### 6.3 引用计数

支持 `dup()` 和 `fork()`:
```c
struct eventfd {
    int ref;  // 引用计数
};

void eventfd_close(struct eventfd *efd) {
    acquire(&efd->lock);
    efd->ref--;
    int should_free = (efd->ref == 0);
    release(&efd->lock);

    if(should_free)
        kmfree((char*)efd);
}
```

### 6.4 VFS 层抽象

通过统一的文件接口访问 eventfd:
```c
struct file {
    enum { ..., FD_EVENTFD } type;
    union {
        struct pipe *pipe;
        struct inode *ip;
        struct eventfd *efd;  // eventfd 对象
    };
};

// 统一的 read/write/close 接口
int fileread(struct file *f, uint64 addr, int n);
int filewrite(struct file *f, uint64 addr, int n);
void fileclose(struct file *f);
```

## 七、文件清单

### 7.1 新增文件
- `src/fs/eventfd.c` (142行) - eventfd核心实现
- `src/fs/eventfd.h` (27行) - eventfd数据结构和接口
- `user/sys/eventfd.h` (27行) - 用户空间头文件

### 7.2 修改文件
- `src/fs/file.h` - 添加 FD_EVENTFD 类型和 efd 指针
- `src/fs/file.c` - 添加 eventfd 的 read/write/close 处理
- `src/syscall/syscall.c` - 注册 SYS_eventfd2
- `src/syscall/sysfile.c` - 实现 sys_eventfd2() 系统调用
- `user/user.h` - 添加 eventfd 用户接口
- `user/ulib.c` - 实现 eventfd() 封装函数
- `user/initcode.c` - 添加测试运行代码
- `run-sdcard-rv.sh` - 修改使用 FAT32 镜像
- `.gitignore` - 忽略构建产物

### 7.3 代码统计
```
新增: 463 行
删除: 42 行
修改: 12 个文件
```

## 八、经验总结

### 8.1 成功要点

1. **理解 Linux 语义**: 仔细研究 eventfd 的行为规范，包括阻塞/非阻塞、信号量模式、溢出保护等
2. **VFS 抽象设计**: 通过统一的文件接口集成新的文件类型
3. **同步原语使用**: 正确使用 spinlock 和 sleep/wakeup 实现阻塞操作
4. **边界条件处理**: 仔细处理计数器溢出、空读取、非阻塞等边界情况
5. **引用计数**: 正确实现对象生命周期管理，支持 dup/fork

### 8.2 踩过的坑

1. **文件系统类型混淆**: EXT4 vs FAT32，需要使用正确的镜像格式
2. **测试文件路径**: 需要理解 SD 卡如何挂载到文件系统
3. **标志位映射**: Linux 的八进制标志需要正确映射到内部表示
4. **编译环境**: 测试文件需要完整的 libc，使用预编译二进制更简单

### 8.3 改进空间

1. **错误处理**: 可以添加更详细的错误日志和诊断信息
2. **性能优化**: 可以考虑使用更高效的同步机制
3. **测试覆盖**: 可以添加更多压力测试和并发测试
4. **文档注释**: 代码中可以添加更多注释说明设计意图

## 九、参考资料

### 9.1 相关文档
- [Linux eventfd(2) man page](https://man7.org/linux/man-pages/man2/eventfd.2.html)
- xv6 book: Chapter 5 (Locking), Chapter 6 (File system)
- [2025中西部区域赛决赛题目说明](../oscomp-midwest-onsitefinal-main/README.md)

### 9.2 关键代码位置
- eventfd 核心实现: [src/fs/eventfd.c](../src/fs/eventfd.c)
- 系统调用入口: [src/syscall/sysfile.c](../src/syscall/sysfile.c)
- 测试代码: [oscomp-midwest-onsitefinal-main/eventfd2/test.c](../oscomp-midwest-onsitefinal-main/eventfd2/test.c)

## 十、附录

### A. 完整测试日志

详见运行日志文件: `runsh.log`

### B. Git 提交记录

```bash
commit 7a5b3fe
Author: gan-rui-lin
Date:   2025-02-01

    feat: 实现 eventfd2 系统调用（题目2）

    - 新增 eventfd.c/h 实现核心功能
    - 支持 EFD_CLOEXEC、EFD_NONBLOCK、EFD_SEMAPHORE 标志
    - 实现 64 位计数器的读写操作
    - 支持阻塞/非阻塞模式和信号量语义
    - 通过所有 5 个测试点（ef2-1 到 ef2-5）
    - 修改 initcode.c 使用 FAT32 镜像运行测试

    Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
```

### C. 测试镜像信息

- **镜像文件**: `oscomp-midwest-onsitefinal-main/testcase-rv-fat32.img`
- **文件系统**: FAT32
- **大小**: 128 MB
- **包含测试**:
  - close_range: cr-1 到 cr-5
  - eventfd2: ef2-1 到 ef2-5
  - waitid: wi-1 到 wi-4

---

**文档版本**: 1.0
**最后更新**: 2025-02-01
**维护者**: gan-rui-lin
