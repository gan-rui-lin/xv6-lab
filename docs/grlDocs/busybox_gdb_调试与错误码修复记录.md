# busybox 调试与错误码修复记录

## 现象与定位路径
- busybox 启动后出现 "Operation not permitted"，且报错行不直观。
- gdb 回溯显示用户态函数（如 netlink_msg_to_ifaddr）触发 syscall。
- 实际原因是内核返回 -1，musl 解释为 EPERM(1)，导致错误提示误导。

## gdb 调试要点（内核 + 用户）
1) QEMU 启动时打开 gdb stub（例如加 -s -S）。
2) gdb 先加载内核符号，再加载用户符号：
```
gdb-multiarch -ex "file /home/grl/codeRepo/xv6-lab/kernel-qemu" \
  -ex "set architecture riscv:rv64" \
  -ex "target remote localhost:1234" \
  -ex "add-symbol-file /home/grl/codeRepo/oskernel2025-fos/busybox/riscv/musl/busybox 0x0"
```

3) 内核态无法直接 disas 用户 VA，需转换 VA->PA：
```
p/x walkaddr(myproc()->pagetable, 0x用户地址)
# 返回物理地址后:
# x/20i 物理地址
```

4) 手动计算用户程序符号基址（示例）：
- VA 页基 = PGROUNDDOWN(0x119b64) = 0x119000
- PA = 0x87d8a000
- base = PA - VA页基 = 0x87c71000

然后：
```
add-symbol-file /home/grl/codeRepo/oskernel2025-fos/busybox/riscv/musl/busybox 0x87c71000
```

## syscall 跟踪辅助
- 通过 gdb 自定义命令打印当前 syscall 参数：
```
define nn
  ni
  p/x myproc()->trapframe->a7
  p/x myproc()->trapframe->a0
  p/x myproc()->trapframe->a1
  p/x myproc()->trapframe->a2
  p/x myproc()->trapframe->a3
  p/x myproc()->trapframe->a4
  p/x myproc()->trapframe->a5
  p sysname(myproc()->trapframe->a7)
end
```

## 关键修复点
1) execve envp 触发 panic
- 旧行为：envp 非空直接 panic。
- 修复：忽略 envp，避免 busybox 直接崩溃。

2) 错误码统一为 Linux 风格负 errno
- 新增 `src/errno.h`，在 syscalls 中返回 -errno。
- 典型修复：
  - `argfd` -> `-EBADF/-EINVAL`
  - `sys_open/sys_openat` -> `-ENOENT/-EISDIR/-ENOTDIR/-ENODEV/-EACCES/-EMFILE`
  - `sys_fcntl/sys_writev/sys_read/sys_write/sys_close/sys_fstat/sys_fstatat` -> `-EINVAL/-EBADF/-EFAULT/-EIO/-EMFILE`
  - `sys_mmap(len==0)` -> `-EINVAL`

3) wait4 options 支持 WNOHANG
- 旧行为：options!=0 直接 panic。
- 修复：支持 `WNOHANG`，无子进程返回 0，有僵尸返回 pid。
- 不支持的 option 返回 `-EINVAL`，无子进程返回 `-ECHILD`。

4) 补齐关键 syscall stub
- `sys_fcntl`（F_GETFL/F_SETFL/F_DUPFD 等）
- `sys_writev`
- `sys_rt_sigaction` / `sys_rt_sigprocmask`（最小 stub）

## 结论
- “Operation not permitted” 多数是 errno 映射错误导致的假象。
- 先追踪 syscall 参数，再补 errno 与缺失 syscall，能显著提升定位效率。

## 追加：脚本执行 ENOEXEC 修复
现象：
- `execve("./run-all.sh")` 报 `bad magic`，随后 busybox 报 `Operation not permitted`。

原因：
- `run-all.sh` 是脚本，ELF magic 不匹配时应返回 `-ENOEXEC`，这样 shell 才会回退到脚本解释流程。
- 之前返回 -1（或映射成 EPERM）导致误报。

修复：
- `src/errno.h` 增加 `ENOEXEC`。
- `src/proc/exec.c` 在 ELF magic 不匹配时直接返回 `-ENOEXEC`。

效果：
- busybox 能正确识别脚本并交由 `sh` 解释执行，不再报 "Operation not permitted"。

## 追加：run-all.sh 仍报 Operation not permitted 的观测日志
现象日志节选：
- `exec: bad magic for ./run-all.sh` 后返回 `-ENOEXEC`（0xfffffffffffffff8）。
- 紧接着再次 `execve` 返回 `-1`，随后 `./run-all.sh: Operation not permitted`。

判断要点：
- 第一次 `execve` 已按预期返回 `-ENOEXEC`，说明内核识别到脚本并按 Linux 语义返回。
- 后续仍出现 `Operation not permitted`，说明脚本解释回退路径中还有其它 syscall 返回了错误码（可能仍是 `-1` 映射到 EPERM，或某个测试项未实现）。需要继续用 syscall trace 追踪具体返回值与 errno。
