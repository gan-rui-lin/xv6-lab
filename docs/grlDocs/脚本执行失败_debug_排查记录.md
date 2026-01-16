# 脚本执行失败 debug 排查记录

## 现象
- 运行 `basic_testcode.sh` 时提示：`./run-all.sh: not found`。
- 日志里同时出现：`exec: bad magic for ./run-all.sh`。

## 原因链路
1) `basic_testcode.sh` 内部执行 `./run-all.sh`。
2) `run-all.sh` 是脚本，不是 ELF，内核 `exec()` 返回 `-ENOEXEC` 是正确行为。
3) shell 看到 `ENOEXEC` 会回退到默认解释器（通常是 `/bin/sh`）。
4) 系统里没有 `/bin/sh`，于是报 `./run-all.sh: not found`。

结论：不是脚本本身找不到，而是解释器路径缺失导致回退失败。

## 关键定位步骤
- 观察 `SYS_execve` 返回值是否为 `-ENOEXEC`。
- 如果紧随其后出现 `not found`，说明回退解释器不可用。
- 直接测试：`/musl/busybox sh /musl/basic_testcode.sh`，可验证脚本在 busybox sh 下可正常执行。

## 解决方案（两类）
**方案 A：内核回退（已实现）**
- 在 `sys_execve` 中检测 `exec()` 返回 `-ENOEXEC`，自动改为执行 `/musl/busybox sh <script>`。
- 好处：对所有脚本生效，不依赖 `/bin/sh`。

**方案 B：提供 `/bin/sh`（可选）**
- 在文件系统里准备 `/bin/sh`，指向 `/musl/busybox`。
- 需要系统支持符号链接，或在镜像中直接放置拷贝/硬链接。

## 当前结果
- 采用方案 A 后，`./run-all.sh` 能被 busybox sh 正确解释执行，`not found` 消失。
