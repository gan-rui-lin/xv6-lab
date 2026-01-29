# 共享内存测试计划

## 测试程序说明

测试程序：`user/test_shm.c`

## 测试用例

### Test 1: 基本共享内存操作
- **目的**: 验证基本的创建、附加、读写、分离、删除功能
- **步骤**:
  1. shmget() 创建 100 字节共享内存
  2. shmat() 附加到进程地址空间
  3. 写入字符串 "Hello from test!"
  4. 读取并验证数据正确性
  5. shmdt() 分离
  6. shmctl(IPC_RMID) 删除
- **预期结果**: 所有操作成功，数据读写正确

### Test 2: 父子进程共享内存
- **目的**: 验证 fork 后父子进程可以共享内存
- **步骤**:
  1. 父进程创建共享内存并写入 "Parent data"
  2. fork() 创建子进程
  3. 子进程附加相同的共享内存
  4. 子进程读取并验证父进程写入的数据
  5. 子进程修改为 "Child modified"
  6. 父进程读取并验证子进程的修改
- **预期结果**: 父子进程能看到对方的修改

### Test 3: IPC_STAT 元数据查询
- **目的**: 验证 shmctl(IPC_STAT) 能正确返回段信息
- **步骤**:
  1. 创建共享内存段
  2. 调用 shmctl(IPC_STAT) 获取元数据
  3. 验证大小、创建者 PID、附加数等信息
- **预期结果**: 返回正确的元数据

### Test 4: IPC_EXCL 标志
- **目的**: 验证 IPC_EXCL 标志的独占创建语义
- **步骤**:
  1. 用 IPC_CREAT|IPC_EXCL 创建段
  2. 再次用 IPC_CREAT|IPC_EXCL 创建相同key的段（应失败）
  3. 用 IPC_CREAT（不带 EXCL）获取段（应成功并返回相同ID）
- **预期结果**: EXCL 标志正确阻止重复创建

## 运行测试

```bash
# 编译内核和用户程序
make all

# 启动 QEMU（如果需要）
make run

# 在 xv6 shell 中运行测试
$ test_shm
```

## 预期输出

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

## 实现限制

当前实现的限制：
- 每个共享内存段限制为 4KB（1页）
- 全局最多 128 个共享内存段
- 每个进程最多附加 16 个段

## 调试建议

如果测试失败：

1. **检查系统调用是否正确注册**
   - 查看 `src/syscall/syscall.c` 中的系统调用表
   - 确认 SYS_shmget (194), SYS_shmat (196), SYS_shmdt (197), SYS_shmctl (195)

2. **检查内核初始化**
   - 确认 `src/boot/main.c` 中调用了 `shm_init()`

3. **添加调试输出**
   - 在 `src/mm/shm.c` 中添加 printf 语句
   - 检查共享内存段的创建、附加、分离过程

4. **检查进程清理**
   - 确认 `src/proc/proc.c` 的 `exit()` 中调用了 `shm_cleanup_proc()`
