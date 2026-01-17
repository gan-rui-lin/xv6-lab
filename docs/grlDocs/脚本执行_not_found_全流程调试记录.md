# 脚本执行 not found 全流程调试记录

## 背景现象
- 运行 `basic_testcode.sh` 时，`./run-all.sh` 报 "not found"。
- 日志显示 `exec: bad magic`（脚本非 ELF），随后用户态输出错误。

## 调试路径概览
1) **内核 fallback（最初临时方案）**
   - 在 `sys_execve` 中遇到 `-ENOEXEC` 时，内核强制 `exec("/bin/sh")` 或 `exec("/musl/busybox")`。
   - 缺点：内核替用户态“做决定”，且环境变量可能丢失。

2) **取消内核 fallback，改为传递 envp**
   - `sys_execve` 支持传递 `envp`，用户态设置 `PATH`。
   - 目标：让 busybox `sh` 自己处理脚本回退。

3) **发现 `./run-all.sh` 仍 not found**
   - 继续加 trace，发现 `sys_execve` 报：
     - `ENOENT path='/proc/self/exe'`
   - 说明 busybox 的 `sh` 回退执行的是 `/proc/self/exe`。

4) **路径替换（临时模拟 /proc/self/exe）**
   - 在 `sys_execve` 中，若 path 为 `/proc/self/exe`，直接替换为 `/musl/busybox`。
   - 目的：模拟 Linux 的 `/proc/self/exe` 行为，让 busybox 自己“重启”为解释器。

## 核心原因解释
- busybox/ash 在脚本回退时会优先执行 `/proc/self/exe`，以“当前可执行文件自身”作为解释器。
- Linux 下 `/proc/self/exe` 是链接到当前进程可执行文件。
- 本系统没有 procfs，导致 `ENOENT` → `not found`。

## 当前处理方式
- 内核暂时把 `/proc/self/exe` 映射到 `/musl/busybox`，使回退链条可用。

## 未来方向（暂不实现）
- **完整实现 procfs**，至少提供 `/proc/self/exe`。
- 这样 busybox 的回退逻辑即可按原设计工作，无需内核做路径替换。

## 结论
- not found 的根因不是脚本缺失，而是 `/proc/self/exe` 不存在导致的回退失败。
- 临时映射解决了问题，但长期应通过 procfs 支持或用户态修改回退策略。
