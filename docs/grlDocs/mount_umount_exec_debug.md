# mount/umount 调试分析与修复思路

## 现象
- 在运行用户态 `mount`/`umount` 测试时，内核报错：
  - `usertrap(): unexpected scause 0x...0d pid=39/44/45`
  - `sepc=0x0000000000001a68` 附近反汇编为：`lbu a5,0(a1); sb a5,0(a0); ...`，典型 `strcpy()` 序列。
  - `stval=0x746e756f6d`/`0x746e756f6d75`，分别是 ASCII 字符串 "mount" / "umount" 的数值。
- GDB `p/x $a1` 显示 `a1` 寄存器含有该 ASCII 值，说明用户态在把 **字面字符串值当成指针** 使用（即源地址非法），导致访存异常。

## 直觉与假设
- `strcpy(dst, src)` 中 `src` 来自 `argv`，如果 `argv` 指针数组或 `argc` 解析不正确，就可能把字符串常量当作指针值传给 `strcpy`。
- 测试仓 CRT ([user/lib/arch/riscv/crt.S](../../testsuits-for-oskernel/riscv-syscalls-testing/user/lib/arch/riscv/crt.S)) 和启动 ([user/lib/main.c](../../testsuits-for-oskernel/riscv-syscalls-testing/user/lib/main.c)) 约定：
  - `_start` 把 `sp` 传给 `__start_main`；
  - `__start_main` 从 `sp` 读取布局 `[argc][argv指针数组...]`，然后调用 `main(argc, argv)`。
- 若内核 `exec()` 没有把 `argc` 放到栈顶，或 `sp/a1` 未按上述约定指向，就会造成 `main()` 读取到错误的 `argv`，出现把 "mount" 当作指针的现象。

## 调试步骤
1. 复现：在 QEMU 下加载内核并运行 `initcode`，触发 `mount/umount` 测试。
2. 连接 GDB：
   ```bash
   gdb-multiarch kernel-qemu -ex "target remote:1234"
   ```
3. 现场检查：
   - 反汇编 `sepc`：`x/10i 0x1a68`，确认为 `strcpy` 序列；
   - 查看 `a1`：`p/x $a1` 与 `x/s $a1`，发现无法访问且值为 "mount" 的 ASCII。
4. 方向锁定：用户态把字面量当指针 → 进入内核 `exec()` 流程检查参数构造。
5. 阅读 `exec()` ([src/proc/exec.c](../..//src/proc/exec.c))：
   - 发现原实现仅将 `argv[]` 指针数组拷贝到用户栈，并设置 `a1=argv数组地址`，但**没有把 `argc` 写到栈顶**。
   - 而测试 CRT 需要从 `sp` 读取 `[argc][argv指针数组...]`。

## 根因
- 内核 `exec()` 的栈布局与测试 CRT 约定不一致：缺少将 `argc` 放在 `sp` 顶端，导致 `__start_main` 解析错位，`main(argc, argv)` 得到错误的 `argv` 指针。

## 修复方案
- 调整 `exec()` 栈布局：
  1. 先将 `argv[]` 指针数组拷贝到栈，记录其地址 `sp_argv`；
  2. 再把 `argc` 压栈，使 `sp` 指向 `argc`；
  3. 设置寄存器：`a0=argc`（返回值），`a1=sp_argv`，`sp` 指向 `argc`；
  4. 保持 16 字节对齐约束。
- 具体改动点：见 [src/proc/exec.c](../../src/proc/exec.c) 中 `exec()` 的调整。

## 结果验证
- 修复后：
  - 用户态 `strcpy()` 的源指针为有效用户地址，异常消失；
  - `mount()` 和随后 `umount()` 的日志与返回值符合测试期望。

## 关于 mount/umount 的最小支持
- 当前阶段不做完整 VFS，仅为 FAT32 测试提供最小内核支持：
  - `sys_mount()`：拷贝参数，验证挂载点存在且为目录，返回 `0`；
  - `sys_umount2()`：拷贝参数，返回 `0`；
- 这满足测试用例对返回值的要求，后续可逐步扩展到真实挂载流程。

## 复现与调试命令
```bash
# 构建并启动（示例命令）
make all && qemu-system-riscv64 \
  -machine virt -kernel kernel-qemu -m 128M -nographic -smp 2 \
  -bios default -drive file=sdcard.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  -device virtio-net-device,netdev=net -netdev user,id=net -s -S

# 连接 GDB 远程调试
gdb-multiarch kernel-qemu -ex "target remote:1234"

# 现场检查指令与寄存器
x/10i 0x1a68
p/x $a1
x/s $a1
```

## 后续工作建议
- 在 `sys_mount()` 中限制仅当 `fstype=="vfat"` 时返回 0，其他返回 -1；
- 补充挂载点占用检查、挂载表记录与命名空间管理；
- 引入基础 VFS 层以连接具体 FS 的 `.mount()`/`.umount()` 实现。
