# busybox exec 调试记录

> 这里是我在尝试让 xv6-lab 运行 busybox 二进制时的调试记录，使用的是 2023-final 分支的 busybox。

### 背景
- initcode 里新增 `test_busybox()`：`fork` 后子进程执行 `busybox echo "Hello from busybox!"`，父进程等待。
- 运行时最初直接 panic/store page fault，随后在缺失/错误的系统调用接口上不断折返。

### 调试思路与步骤
- **加轻量 syscall trace**：在 `syscall_handler` 里仅对进程名为 busybox 的进程打印号、名、6 个原始参数与返回值，便于看到 exec 后的第一批调用。
- **修正用户栈尾布局**：`exec.c` 给 RISC-V CRT 提供的栈尾改为 `argc @ sp` 紧接 `argv[]`、空 `envp`、`AT_NULL`，并保持 16 字节对齐；**避免 libc 误读 argv 造成早期 SIGSEGV**。
- **补缺少的 Linux 兼容 syscall**：busybox 早期会调用 `set_tid_address`、`getuid`、`exit_group`、`gettid`、`fstatat` 等，之前未实现导致直接 “unimplemented sys call”。补了最小 stub：
  - `set_tid_address` 记录/写回 tid 并返回 pid。
  - `getuid/geteuid/getgid/getegid/gettid` 返回 0/ pid。
  - `exit_group` 复用 `exit`。
  - `fstatat` 复用现有 namei/nameiat 逻辑 + 复用 `stat_to_kstat`。
- **保持 kstat 转换一致**：抽出 `stat_to_kstat` 供 `fstat`/`fstatat` 共用，减少重复错误。

### 现状与残留问题
- busybox 现在能进入主逻辑，不再崩溃或报 “applet not found”；但在 `mmap`(len=0, fd=-1) 被返回 -1 后打印 “echo: out of memory” 并 `exit_group`.
- 需要进一步看 `SYS_mmap` 的语义：目前实现拒绝 `MAP_ANONYMOUS`，len=0 直接失败；busybox 的 malloc/堆初始化可能依赖匿名映射或 `brk` 以外的路径。
- 下一步建议：最小实现 `MAP_ANONYMOUS` (或将 len=0 视为按页对齐扩堆) 以满足 busybox malloc；同时确保 `brk` 返回的 `p->sz` 随 exec 的初始堆顶更新。

### “applet not found” 的根因与修复
- 现象：补齐一批 syscall 之后，busybox 打印 “applet not found” 就直接 `exit_group`，没有真正执行 `echo`。
- 根因：`exec` 构造的初始用户栈布局不符合 Linux/RISC-V 约定，`argc`/`argv[]`/`envp`/`auxv` 摆放和对齐错误，导致 busybox 读到的 argv[0]/argv[1] 异常，无法识别子命令（认为没有找到 applet）。
- 修复：重新排布栈尾——
  - `argc` 放在最终 `sp`，紧接着是 `argv[]` 指针数组，数组末尾 NULL。
  - 追加空 `envp` (envp[0]=NULL) 与 `AT_NULL` auxv。
  - 调整使 `sp`（含 argc）16 字节对齐，但不再把对齐填充夹在 `argc` 与 `argv` 之间。
  修复后 busybox 能正确解析 argv[0]="busybox"、argv[1]="echo"，不再报 “applet not found”，进入后续内存分配路径。

### 经验要点
- 先让 syscall trace 只盯目标进程，避免刷屏；关键时刻可通过全局开关打开全部进程的 trace。
- RISC-V 用户栈对齐和 argc/argv/envp/auxv 布局要精准，glibc/BusyBox 会读 argv[0]/envp[0]/auxv，不对齐极易早期崩。
- 补齐常见 Linux stub（{get,set}*id、exit_group、set_tid_address、fstatat）是让第三方二进制启动的基础。
- 磁盘/文件系统没问题时，busybox 早期失败多半在 exec/栈布局和基础 syscalls。逐步消掉“未实现”与对齐问题后，再看内存接口（brk/mmap）。 
