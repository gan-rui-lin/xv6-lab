# 内核栈溢出 store page fault 修复记录

## 现象
- 运行 basic 测试时出现：
  - `store page fault: scause=0xf ... stval=0x3fffff8000`
  - 随后内核 panic。

## 结论
- `stval` 落在某个进程的 KSTACK guard page（`KSTACK(p)` 的保护页范围），说明内核栈使用超过 1 页。
- 返回地址被破坏，`sepc` 偏移指向 `initcode` 数据区，符合栈溢出特征。

## 修复措施
- 将内核栈从 1 页扩展为 2 页（仍保留 1 页 guard）：
  - `src/memlayout.h`: 引入 `KSTACK_PAGES/KSTACK_SIZE`，KSTACK 预留 2 页栈 + 1 页 guard。
  - `src/proc/proc.c`: `proc_mapstacks` 映射 2 页；`context.sp` 指向 `KSTACK_SIZE` 顶部。
  - `src/trap/trap.c`: `kernel_sp` 指向 `KSTACK_SIZE` 顶部。

## 结果
- 预期可避免深度调用（如 ext4 路径解析/execve 回退）导致的内核栈溢出。
- 后续若仍出现同类 fault，需要进一步分析具体函数的栈占用并做瘦身。
