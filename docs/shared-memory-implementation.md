# xv6 共享内存 (System V IPC) 实现文档

## 目录
1. [概述](#概述)
2. [基本原理](#基本原理)
3. [API 接口](#api-接口)
4. [实现细节](#实现细节)
5. [文件修改清单](#文件修改清单)
6. [使用示例](#使用示例)
7. [测试验证](#测试验证)

---

## 概述

本文档介绍了在 xv6 操作系统中实现的 System V 共享内存机制。共享内存允许多个进程访问同一块物理内存，是进程间通信 (IPC) 最快速的方式。

### 特性

- ✅ 支持 System V IPC 标准接口
- ✅ 支持多个共享内存段（最多 128 个）
- ✅ 支持进程间共享和父子进程继承
- ✅ 自动清理：进程退出时自动分离
- ✅ 权限控制：uid/gid 和访问模式
- ✅ 元数据管理：创建时间、附加次数、最后操作 PID 等

### 限制

- 每个共享内存段大小限制为 4KB（单页）
- 全局最多 128 个共享内存段
- 每个进程最多附加 16 个共享内存段
- 不支持多页共享内存段（可扩展）

---

## 基本原理

### 1. 内存共享机制

```
进程 A 的页表              物理内存              进程 B 的页表
┌─────────────┐          ┌─────────────┐        ┌─────────────┐
│ 0x70000000  │─────────▶│  共享页面   │◀───────│ 0x70000000  │
│   (虚拟地址) │          │ (物理地址)  │        │  (虚拟地址)  │
└─────────────┘          └─────────────┘        └─────────────┘

两个进程的页表映射到同一物理页，实现内存共享
```

### 2. 数据结构设计

#### 全局共享内存段表

```c
struct {
  struct spinlock lock;           // claude: 保护全局表的自旋锁
  struct shm_seg segs[128];      // claude: 共享内存段数组
  int next_id;                    // claude: 下一个可用ID
} shm_table;
```

每个共享内存段包含：
- **key**: IPC 密钥，用于多进程查找同一段
- **kaddr**: 内核虚拟地址，指向实际物理页
- **size**: 段大小（向上取整到页大小）
- **metadata**: 元数据（权限、时间戳、PID等）
- **refcount**: 引用计数，跟踪附加的进程数

#### 进程附加表

每个进程维护一个附加表：
```c
struct proc {
  ...
  struct shm_attach shm_attach[16];  // claude: 进程的共享内存附加记录
  uint uid;                          // claude: 用户ID（IPC权限）
  uint gid;                          // claude: 组ID（IPC权限）
  ...
};
```

### 3. 生命周期管理

```
创建 (shmget)
   │
   ├─→ 分配物理页
   │   └─→ memset 清零
   │
   ├─→ 初始化元数据
   │   └─→ uid, gid, mode, cpid, ctime
   │
   └─→ 返回 shmid

附加 (shmat)
   │
   ├─→ 选择虚拟地址（自动或指定）
   │
   ├─→ 映射到进程页表
   │   └─→ mappages(pagetable, va, size, pa, perm)
   │
   ├─→ 记录附加信息
   │   └─→ p->shm_attach[i] = {shmid, vaddr, valid}
   │
   └─→ 更新元数据
       └─→ nattch++, lpid, atime

分离 (shmdt)
   │
   ├─→ 从页表取消映射
   │   └─→ uvmunmap(pagetable, va, npages, 0)
   │
   ├─→ 清除附加记录
   │   └─→ p->shm_attach[i].valid = 0
   │
   └─→ 更新元数据
       └─→ nattch--, lpid, dtime

删除 (shmctl IPC_RMID)
   │
   ├─→ 有附加？
   │   ├─ Yes → 标记删除 (mode |= 0x8000)
   │   └─ No  → 立即释放
   │
   └─→ 释放物理页
       └─→ kfree(kaddr)
```

---

## API 接口

### 1. shmget - 创建/获取共享内存段

```c
int shmget(int key, size_t size, int flags);
```

**参数**：
- `key`: IPC 密钥，用于标识共享内存段
- `size`: 请求的大小（字节），会向上取整到页大小
- `flags`: 标志位
  - `IPC_CREAT`: 如果不存在则创建
  - `IPC_EXCL`: 与 IPC_CREAT 一起使用，如果已存在则失败
  - `0666`: 访问权限（八进制）

**返回值**：
- 成功：共享内存段标识符 (shmid >= 0)
- 失败：-1

**示例**：
```c
// 创建新段，如果已存在则获取
int shmid = shmget(0x1234, 4096, IPC_CREAT | 0666);

// 独占创建，如果已存在则失败
int shmid = shmget(0x5678, 4096, IPC_CREAT | IPC_EXCL | 0666);
```

### 2. shmat - 附加共享内存到进程地址空间

```c
void* shmat(int shmid, void* addr, int flags);
```

**参数**：
- `shmid`: 共享内存段标识符
- `addr`: 期望的附加地址
  - `0`: 内核自动选择地址（推荐）
  - 非零：尝试附加到指定地址
- `flags`: 标志位
  - `0`: 读写权限
  - `SHM_RDONLY`: 只读权限

**返回值**：
- 成功：附加的虚拟地址
- 失败：(void*)-1

**示例**：
```c
// 自动选择地址，读写权限
void* ptr = shmat(shmid, 0, 0);

// 只读附加
void* ptr = shmat(shmid, 0, SHM_RDONLY);
```

### 3. shmdt - 分离共享内存

```c
int shmdt(void* addr);
```

**参数**：
- `addr`: shmat 返回的地址

**返回值**：
- 成功：0
- 失败：-1

**示例**：
```c
shmdt(ptr);
```

### 4. shmctl - 共享内存控制操作

```c
int shmctl(int shmid, int cmd, struct shmid_ds* buf);
```

**参数**：
- `shmid`: 共享内存段标识符
- `cmd`: 控制命令
  - `IPC_STAT`: 获取段信息
  - `IPC_RMID`: 删除段
  - `IPC_SET`: 设置段信息（未完全实现）
- `buf`: 数据缓冲区

**返回值**：
- 成功：0
- 失败：-1

**示例**：
```c
// 获取段信息
struct shmid_ds buf;
shmctl(shmid, IPC_STAT, &buf);
printf("Size: %d, Attachments: %d\n", buf.shm_segsz, buf.shm_nattch);

// 删除段
shmctl(shmid, IPC_RMID, 0);
```

---

## 实现细节

### 核心函数实现

#### 1. shm_get - 创建/获取共享内存段

```c
int shm_get(int key, uint64 size, int flags)
{
  // 1. 检查是否已存在
  shmid = shm_find_by_key(key);
  if (shmid >= 0) {
    // 存在：检查 IPC_EXCL 标志
    if (flags & IPC_CREAT && flags & IPC_EXCL)
      return -1;  // EEXIST
    return shmid;
  }

  // 2. 不存在：检查 IPC_CREAT 标志
  if (!(flags & IPC_CREAT))
    return -1;  // ENOENT

  // 3. 分配新段
  shmid = shm_alloc();
  size = PGROUNDUP(size);  // 向上取整到页大小
  void* kaddr = kalloc();  // 分配物理页
  memset(kaddr, 0, size);  // 清零

  // 4. 初始化元数据
  seg->valid = 1;
  seg->key = key;
  seg->kaddr = kaddr;
  seg->size = size;
  seg->ds.shm_perm.uid = p->uid;
  seg->ds.shm_cpid = p->pid;
  seg->ds.shm_ctime = r_time() / 10000000;

  return shmid;
}
```

#### 2. shm_at - 附加到进程地址空间

```c
uint64 shm_at(int shmid, uint64 addr, int flags)
{
  // 1. 选择虚拟地址
  if (addr == 0) {
    // 自动分配：使用固定区域 0x70000000 + offset
    addr = 0x70000000UL + (shmid * 0x10000);
  }
  addr = PGROUNDDOWN(addr);

  // 2. 映射到页表
  int perm = PTE_U;
  if (!(flags & SHM_RDONLY))
    perm |= PTE_W;
  perm |= PTE_R;

  mappages(p->pagetable, addr, seg->size, (uint64)seg->kaddr, perm);

  // 3. 记录附加
  p->shm_attach[slot].valid = 1;
  p->shm_attach[slot].shmid = shmid;
  p->shm_attach[slot].vaddr = (void*)addr;

  // 4. 更新元数据
  seg->refcount++;
  seg->ds.shm_nattch++;
  seg->ds.shm_lpid = p->pid;
  seg->ds.shm_atime = r_time() / 10000000;

  return addr;
}
```

#### 3. shm_dt - 分离共享内存

```c
int shm_dt(uint64 addr)
{
  // 1. 查找附加记录
  for (int i = 0; i < 16; i++) {
    if (p->shm_attach[i].valid && p->shm_attach[i].vaddr == addr) {
      // 2. 取消页表映射（不释放物理页）
      uvmunmap(p->pagetable, addr, seg->size / PGSIZE, 0);

      // 3. 清除附加记录
      p->shm_attach[i].valid = 0;

      // 4. 更新元数据
      seg->refcount--;
      seg->ds.shm_nattch--;
      seg->ds.shm_lpid = p->pid;
      seg->ds.shm_dtime = r_time() / 10000000;

      return 0;
    }
  }
  return -1;
}
```

#### 4. shm_cleanup_proc - 进程退出时清理

```c
void shm_cleanup_proc(struct proc* p)
{
  // 遍历所有附加
  for (int i = 0; i < 16; i++) {
    if (p->shm_attach[i].valid) {
      // 取消映射
      uvmunmap(p->pagetable, addr, seg->size / PGSIZE, 0);

      // 更新引用计数
      seg->refcount--;
      seg->ds.shm_nattch--;

      // 如果标记为删除且无附加，释放物理页
      if ((seg->ds.shm_perm.mode & 0x8000) && seg->refcount == 0) {
        kfree(seg->kaddr);
        seg->valid = 0;
      }
    }
  }
}
```

---

## 文件修改清单

### 1. src/mm/shm.h (新建)

**作用**：共享内存头文件，定义数据结构和常量

**新增内容**：

```c
// claude: IPC 权限结构体，用于访问控制
struct ipc_perm {
  uint uid, gid;      // claude: 所有者的用户ID和组ID
  uint cuid, cgid;    // claude: 创建者的用户ID和组ID
  uint mode;          // claude: 访问权限（如 0666）
  uint seq;           // claude: 序列号（未使用）
};

// claude: 共享内存段元数据，对应 System V 标准
struct shmid_ds {
  struct ipc_perm shm_perm;  // claude: 权限信息
  uint64 shm_segsz;          // claude: 段大小（字节）
  uint64 shm_atime;          // claude: 最后 shmat() 时间
  uint64 shm_dtime;          // claude: 最后 shmdt() 时间
  uint64 shm_ctime;          // claude: 最后修改时间
  int shm_cpid;              // claude: 创建者进程ID
  int shm_lpid;              // claude: 最后操作的进程ID
  uint64 shm_nattch;         // claude: 当前附加的进程数
};

// claude: 内核维护的共享内存段，扩展了 shmid_ds
struct shm_seg {
  int valid;                 // claude: 1表示此槽位正在使用
  int key;                   // claude: IPC 密钥
  struct shmid_ds ds;        // claude: 段元数据
  void* kaddr;               // claude: 内核虚拟地址（指向物理页）
  uint64 size;               // claude: 实际分配的大小（页对齐）
  int refcount;              // claude: 引用计数（附加数）
};

// claude: IPC 常量定义
#define IPC_CREAT  0001000   // claude: 不存在则创建
#define IPC_EXCL   0002000   // claude: 独占创建，已存在则失败
#define IPC_RMID   0         // claude: 删除标识符
#define IPC_STAT   2         // claude: 获取状态信息

// claude: shmat 标志
#define SHM_RDONLY 010000    // claude: 只读附加
```

**位置**：新建文件

---

### 2. src/mm/shm.c (新建)

**作用**：共享内存核心实现

**新增内容**：

#### 全局共享内存表
```c
// claude: 全局共享内存段管理表，保存所有共享内存段
struct {
  struct spinlock lock;              // claude: 自旋锁，保护并发访问
  struct shm_seg segs[SHM_MAXSEGS]; // claude: 共享内存段数组（最多128个）
  int next_id;                       // claude: 下一个可用ID（未使用）
} shm_table;
```

#### 初始化函数
```c
// claude: 初始化共享内存子系统，在系统启动时调用
void shm_init(void)
{
  initlock(&shm_table.lock, "shm");  // claude: 初始化自旋锁
  for(int i = 0; i < SHM_MAXSEGS; i++){
    shm_table.segs[i].valid = 0;     // claude: 标记所有槽位为空闲
    shm_table.segs[i].kaddr = 0;     // claude: 清空内核地址
  }
}
```

#### 核心函数（已在"实现细节"章节详细说明）

**位置**：新建文件，约 400 行代码

---

### 3. src/proc/proc.h

**作用**：进程结构体定义

**新增内容**：

```c
// claude: 共享内存附加记录，每个进程维护
struct shm_attach {
  int shmid;       // claude: 共享内存段ID
  void* vaddr;     // claude: 附加的虚拟地址
  int valid;       // claude: 1表示此记录有效
};

struct proc {
  ...
  // claude: 进程的共享内存附加表（最多16个）
  struct shm_attach shm_attach[16];

  // claude: 用于 IPC 权限检查的用户ID和组ID
  uint uid;
  uint gid;
  ...
};
```

**位置**：`src/proc/proc.h` 第 9-15 行（结构体定义），第 104-109 行（进程结构体）

---

### 4. src/proc/proc.c

**作用**：进程管理

#### allocproc() - 进程初始化
```c
// 在 allocproc() 函数中，第 156-163 行

  // claude: 初始化共享内存附加表，所有槽位标记为未使用
  for(int i = 0; i < 16; i++) {  // SHM_MAX_ATTACH
    p->shm_attach[i].valid = 0;
  }

  // claude: 初始化 IPC 权限字段为 root（uid=0, gid=0）
  p->uid = 0;
  p->gid = 0;
```

**位置**：`src/proc/proc.c` 第 156-163 行

#### exit() - 进程退出清理
```c
// 在 exit() 函数中，第 774 行（在关闭文件后）

  // claude: 清理进程的所有共享内存附加
  // claude: 自动分离所有附加的共享内存段
  shm_cleanup_proc(p);
```

**位置**：`src/proc/proc.c` 第 774 行

---

### 5. src/boot/main.c

**作用**：系统启动初始化

**新增内容**：

```c
// 在 main() 函数中，第 26-27 行

kinit();     // 物理页面分配器初始化
shm_init();  // claude: 初始化共享内存子系统（在 kinit 之后）
```

**位置**：`src/boot/main.c` 第 27 行

**说明**：必须在 `kinit()` 之后调用，因为共享内存需要使用 `kalloc()` 分配物理页。

---

### 6. src/syscall/syscall.c

**作用**：系统调用注册和分发

#### 声明系统调用
```c
// 第 194-197 行

// claude: System V 共享内存系统调用声明
extern uint64 sys_shmget(void);   // claude: 创建/获取共享内存段
extern uint64 sys_shmat(void);    // claude: 附加共享内存到进程
extern uint64 sys_shmdt(void);    // claude: 分离共享内存
extern uint64 sys_shmctl(void);   // claude: 控制操作（查询/删除）
```

**位置**：`src/syscall/syscall.c` 第 194-197 行

#### 注册到系统调用表
```c
// 在 syscalls[] 数组中，第 282-285 行

static uint64 (*syscalls[])(void) = {
  ...
  [SYS_shmget] sys_shmget,   // claude: 系统调用号 194
  [SYS_shmat] sys_shmat,     // claude: 系统调用号 196
  [SYS_shmdt] sys_shmdt,     // claude: 系统调用号 197
  [SYS_shmctl] sys_shmctl,   // claude: 系统调用号 195
};
```

**位置**：`src/syscall/syscall.c` 第 282-285 行

---

### 7. src/defs.h

**作用**：全局函数声明

**新增内容**：

```c
// 在 kalloc.c 部分之后，第 134-144 行

// claude: shm.c - System V 共享内存功能
void     shm_init(void);                         // claude: 初始化共享内存子系统
int      shm_get(int, uint64, int);             // claude: 内部：创建/获取段
uint64   shm_at(int, uint64, int);              // claude: 内部：附加段
int      shm_dt(uint64);                         // claude: 内部：分离段
int      shm_ctl(int, int, uint64);             // claude: 内部：控制操作
void     shm_cleanup_proc(struct proc*);         // claude: 内部：清理进程附加
uint64   sys_shmget(void);                       // claude: 系统调用：shmget
uint64   sys_shmat(void);                        // claude: 系统调用：shmat
uint64   sys_shmdt(void);                        // claude: 系统调用：shmdt
uint64   sys_shmctl(void);                       // claude: 系统调用：shmctl
```

**位置**：`src/defs.h` 第 134-144 行

---

### 8. user/user.h

**作用**：用户态 API 接口声明

**新增内容**：

```c
// 在文件末尾，#endif 之前，第 92-96 行

// claude: System V 共享内存用户态接口
int shmget(int key, uint64 size, int flags);    // claude: 创建/获取共享内存段
void* shmat(int shmid, void* addr, int flags);  // claude: 附加到进程地址空间
int shmdt(void* addr);                           // claude: 分离共享内存
int shmctl(int shmid, int cmd, void* buf);      // claude: 控制操作（查询/删除）
```

**位置**：`user/user.h` 第 92-96 行

---

### 9. user/test_shm.c (新建)

**作用**：共享内存测试程序

**包含测试**：
1. **test_basic_shm()** - 基本操作测试
2. **test_fork_shm()** - 父子进程共享测试
3. **test_shmctl_stat()** - 元数据查询测试
4. **test_excl_flag()** - IPC_EXCL 标志测试

**位置**：新建文件，约 250 行代码

---

### 10. Makefile

**作用**：编译配置

**新增内容**：

```makefile
# 第 212 行，UPROGS 列表中添加

UPROGS=\
	$U/_sh \
	$U/_cat \
	$U/_echo \
	$U/_mkdir \
	$U/_ls \
	$U/_test_shm \   # claude: 添加共享内存测试程序
```

**位置**：`Makefile` 第 212 行

---

## 使用示例

### 示例 1：基本使用

```c
#include "user.h"

int main()
{
  // 1. 创建共享内存段
  int shmid = shmget(0x1234, 4096, IPC_CREAT | 0666);
  if (shmid < 0) {
    printf("shmget failed\n");
    exit(1);
  }

  // 2. 附加到进程地址空间
  char* ptr = shmat(shmid, 0, 0);
  if (ptr == (void*)-1) {
    printf("shmat failed\n");
    exit(1);
  }

  // 3. 写入数据
  strcpy(ptr, "Hello, shared memory!");

  // 4. 读取数据
  printf("Data: %s\n", ptr);

  // 5. 分离
  shmdt(ptr);

  // 6. 删除
  shmctl(shmid, IPC_RMID, 0);

  exit(0);
}
```

### 示例 2：父子进程通信

```c
int main()
{
  // 父进程创建共享内存
  int shmid = shmget(0x5678, 4096, IPC_CREAT | 0666);
  char* ptr = shmat(shmid, 0, 0);

  strcpy(ptr, "Message from parent");

  int pid = fork();
  if (pid == 0) {
    // 子进程附加相同的共享内存
    char* child_ptr = shmat(shmid, 0, 0);

    // 读取父进程的消息
    printf("Child reads: %s\n", child_ptr);

    // 修改数据
    strcpy(child_ptr, "Reply from child");

    shmdt(child_ptr);
    exit(0);
  } else {
    wait(0);

    // 父进程读取子进程的回复
    printf("Parent reads: %s\n", ptr);

    shmdt(ptr);
    shmctl(shmid, IPC_RMID, 0);
  }

  exit(0);
}
```

### 示例 3：查询段信息

```c
#include "user.h"

// claude: 需要定义 shmid_ds 结构体（与内核一致）
struct shmid_ds {
  uint uid, gid, cuid, cgid, mode, seq;
  uint64 shm_segsz, shm_atime, shm_dtime, shm_ctime;
  int shm_cpid, shm_lpid;
  uint64 shm_nattch;
};

int main()
{
  int shmid = shmget(0xabcd, 4096, IPC_CREAT | 0666);

  struct shmid_ds buf;
  shmctl(shmid, IPC_STAT, &buf);

  printf("Segment info:\n");
  printf("  Size: %d bytes\n", buf.shm_segsz);
  printf("  Creator PID: %d\n", buf.shm_cpid);
  printf("  Attachments: %d\n", buf.shm_nattch);
  printf("  Permissions: 0%o\n", buf.mode & 0777);

  shmctl(shmid, IPC_RMID, 0);
  exit(0);
}
```

---

## 测试验证

### 运行测试

```bash
# 编译内核和测试程序
make all

# 启动 xv6
make run

# 在 xv6 shell 中运行测试
$ test_shm
```

### 预期输出

```
=== Shared Memory Test Suite ===

Test 1: Basic shared memory creation
  PASS: shmget returned shmid=0
  PASS: shmat returned addr=0x70000000
  PASS: Wrote data to shared memory
  PASS: Read back correct data: Hello from test!
  PASS: shmdt succeeded
  PASS: shmctl IPC_RMID succeeded
Test 1: SUCCESS

Test 2: Shared memory between parent and child
  PASS: Parent created shmid=0
  PASS: Parent attached at addr=0x70000000
  PASS: Parent wrote: Parent data
  PASS: Child attached at addr=0x70000000
  Child reads: Parent data
  PASS: Child read correct data
  PASS: Child wrote: Child modified
  Parent reads: Child modified
  PASS: Parent sees child's modification
Test 2: SUCCESS

Test 3: shmctl IPC_STAT
  PASS: Got segment info:
    Size: 4096 bytes
    Creator PID: 2
    Attachments: 0
    Mode: 0666
Test 3: SUCCESS

Test 4: IPC_EXCL flag
  PASS: Created shmid=0
  PASS: Second shmget correctly failed (IPC_EXCL)
  PASS: Got same shmid=0 without IPC_EXCL
Test 4: SUCCESS

=== All Tests Completed ===
```

### 测试说明

- **Test 1** 验证基本的创建、附加、读写、分离、删除功能
- **Test 2** 验证父子进程可以共享内存并看到对方的修改
- **Test 3** 验证元数据查询功能正确
- **Test 4** 验证 IPC_EXCL 标志的独占创建语义

如果所有测试通过，说明共享内存实现正确。

---

## 总结

本实现提供了功能完整的 System V 共享内存机制：

1. **标准兼容**：遵循 System V IPC 接口规范
2. **进程隔离**：每个进程独立的附加表
3. **自动清理**：进程退出时自动分离
4. **引用计数**：支持多进程同时使用
5. **权限控制**：uid/gid 和访问模式

### 扩展方向

如需扩展，可以考虑：
- 支持多页共享内存段
- 实现 shmctl IPC_SET 功能
- 添加更细粒度的权限检查
- 支持共享内存段的持久化
- 实现信号量配合使用

---

## 参考资料

- [System V IPC 标准](https://pubs.opengroup.org/onlinepubs/7908799/xsh/sysvipc.html)
- [Linux man pages: shmget(2)](https://man7.org/linux/man-pages/man2/shmget.2.html)
- [xv6 book](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)
