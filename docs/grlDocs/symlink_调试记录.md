# /bin/sh 符号链接调试记录

## 现象
启动时输出：
```
r1 = 0
r2 = 0
open /bin/sh = 3
=== Testing basic syscalls ===
#### OS COMP TEST GROUP START basic-musl ####
/musl/basic_testcode.sh: line 3: ./run-all.sh: not found
#### OS COMP TEST GROUP END basic-musl ####
```

含义：
- `r1=0` 表示 `/bin` 创建成功。
- `r2=0` 表示 `symlink("/musl/busybox", "/bin/sh")` 成功。
- `open /bin/sh = 3` 说明链接已能被解析并打开。
- 但脚本内执行 `./run-all.sh` 依然 `not found`。

## 过程回放（含 symlink 未实现阶段）
1) 初始状态：内核无 `symlink` 实现  
   - `syscall(SYS_symlink, ...)` 无法生效。  
   - busybox `sh` 回退到 `/bin/sh` 时失败，报 `./run-all.sh: not found`。  

2) 增加 `symlink/symlinkat` 系统调用  
   - 在 ext4 侧引入 `ext4_fsymlink` 封装。  
   - `initcode` 中创建 `/bin`，再建 `/bin/sh -> /musl/busybox`。  

3) 发现 symlink 创建成功但仍 `not found`  
   - `r1 = 0`、`r2 = 0`，但 `open /bin/sh = -2`。  
   - 说明链接存在，但路径解析阶段未跟随 symlink。  

4) 扩展 ext4 路径解析以支持 symlink  
   - 在 `ext4_namei`/`ext4_nameiat` 里增加 `ext4_readlink` 解析。  
   - 限制最大递归深度，避免循环链接。  

5) 当前状态  
   - `open /bin/sh = 3` 表明链接可解析。  
   - `./run-all.sh: not found` 仍出现，问题转移到脚本执行回退逻辑。  

## 当前结论
`/bin/sh` 链接已生效，但 `./run-all.sh` 仍无法被执行。说明问题已从“解释器缺失/链接不可解析”转移到“脚本路径/执行回退”的其它环节，需要继续追踪 `execve("./run-all.sh")` 的返回值和后续回退路径。

## 下一步建议
- 打开 syscall trace，确认 `execve("./run-all.sh")` 的返回值是否为 `-ENOEXEC` 或 `-ENOENT`。
- 如果 `-ENOEXEC` 后仍 `not found`，说明 shell 的解释器回退路径仍未正确命中。
- 若 `-ENOENT`，检查当前目录是否确实包含 `./run-all.sh`（可能是 `chdir` 或路径解析问题）。
