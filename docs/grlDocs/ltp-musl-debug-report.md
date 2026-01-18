# LTP 脚本调试报告（xv6-lab + musl）

## 背景
- 目标：在本 OS 上直接运行镜像内置脚本 `/musl/ltp_testcode.sh`，遍历并执行 `ltp/testcases/bin` 下的所有用例。
- 约束：不修改镜像脚本与文件内容；问题需通过 OS 侧兼容性修复解决。

## 现象与问题演进
- 初始现象：脚本在 `$(basename ...)` 处失败（`basename: not found`）。
- 修复后进展：暴露 BusyBox applets 与 PATH 后，目录遍历成功，仍出现卡在父进程 `read` 上的阻塞；后确认是软阻塞。
- 新现象：解除阻塞后，脚本打印 `RUN LTP CASE ...`，但执行每个用例报 `not found (127)`，例如：
  - `ltp/testcases/bin/abs01: not found`

## 诊断过程
1. BusyBox applet 缺失
   - 根因：镜像 BusyBox 未通过常见路径暴露，脚本依赖 `basename`。
   - 处理：ext4 挂载后动态创建 runtime 符号链接，确保 `/bin/sh`、`/bin/basename`、`/usr/bin/basename`、`/musl/basename` 指向 BusyBox。

2. getdents64 兼容性
   - 现象：目录遍历对海量文件的扫描产生大量 `SYS_getdents64` 调用。
   - 根因：原实现为定长项，用户态解析不兼容 Linux。
   - 处理：实现 Linux 变长 `linux_dirent64` 记录（对齐、d_reclen、d_type、d_off 等），EOF 返回 0。
   - 结果：`/musl/ltp/testcases/bin` 成功遍历至 EOF（偏移到达 90112）。

3. 父读阻塞（read(fd=3)）
   - 现象：父进程进入 `SYS_read` 阻塞；控制台可交互。
   - 分析：这是 `$(basename ...)` 的命令替换流程，父通过管道读子进程输出；阻塞是正常的，需在子关闭写端或写入后返回。
   - 根因：进程退出未关闭打开的文件描述符导致管道写端保持开启，读端无法 EOF。
   - 处理：恢复 `exit()` 里“关闭所有 FD”的逻辑，确保管道写端关闭能唤醒读端（文件：src/proc/proc.c）。

4. 用例执行 `not found (127)`
   - 现象：`RUN LTP CASE foo` 后，`ltp/testcases/bin/foo: not found`，但目录中文件确实存在。
   - 分析：`execve` 成功找到目标文件，但其 ELF 的 PT_INTERP 指定的动态链接器（如 `/lib/ld-musl-riscv64.so.1`）在系统中不可打开，Linux 语义下此时 `execve` 返回 `ENOENT`，shell 报 `not found`。
   - 镜像特点：musl 布局通常将动态加载器通过 `ld-musl-*.so.1` 链接到 `libc.so`；镜像中存在 `/musl/lib/libc.so`，但缺少常见 loader 名称。

## 修复项
1. 进程退出时关闭所有 FD（已完成）
   - 文件：src/proc/proc.c
   - 作用：确保管道写端关闭，读端获得 EOF 或数据，避免父读永久阻塞。

2. ext4 挂载后创建 musl loader 符号链接（已完成）
   - 文件：src/fs/ext4fs.c
   - 逻辑：
     - 确保目录 `/lib` 与 `/musl/lib` 存在；
     - 创建：
       - `/musl/lib/ld-musl-riscv64.so.1` -> `/musl/lib/libc.so`
       - `/lib/ld-musl-riscv64.so.1` -> `/musl/lib/libc.so`
   - 效果：当 ELF 的 PT_INTERP 为 `/lib/ld-musl-riscv64.so.1` 时，能被成功打开。

3. 扩展 exec 对 musl 解释器的回退（已完成）
   - 文件：src/proc/exec.c
   - 逻辑：
     - 若 PT_INTERP 以 `/lib/ld-musl-` 开头、以 `.so.1` 结尾，且 `/lib/...` 与 `/musl/lib/...` 均不存在，则回退到 `/musl/lib/libc.so`。
   - 效果：不依赖特定符号链接，保证 musl 动态链接器解析成功。

## 结果预期与验证
- 目录遍历结束后应打印首个 `RUN LTP CASE ...`；随后用例通过 musl 动态加载器正常启动，`not found (127)` 消失。
- 如某些用例为 glibc 链接（PT_INTERP = `/lib/ld-linux-*.so.1`），在缺少 glibc 时仍无法运行；该类不在本次 musl 兼容修复范围内。

## 后续建议
- 增量观测：必要时在 `execve`、`namei`、以及 `pipeclose/piperead` 添加轻量 trace，辅助定位少数个例失败。
- 兼容增强：若后续发现 glibc 用例，可在镜像或挂载后提供 glibc loader 与必要的运行库，或跳过该类用例。

## 变更清单
- src/proc/proc.c：恢复并执行退出时关闭所有 FD。
- src/fs/ext4fs.c：挂载后创建 `/lib/ld-musl-riscv64.so.1` 和 `/musl/lib/ld-musl-riscv64.so.1` 指向 `/musl/lib/libc.so` 的符号链接。
- src/proc/exec.c：PT_INTERP 解析新增 musl 动态链接器回退到 `/musl/lib/libc.so`。

