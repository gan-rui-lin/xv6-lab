# FAT32 目录满导致用例失败的调试记录

日期：2026-01-19

## 现象
- `test_mkdir` / `test_unlink` / `test_mmap` 触发 `fat32_createat: no free directory entry`。
- 随后出现 `argfd: fd -13 invalid`，并在 `mmap` 处触发 usertrap（用户态非法访问）。

## 复现方式
- 运行全量测试：`bash run.sh -f sdcard.img -t all`。
- 观察到 FAT32 创建日志与断言失败。

## 关键日志
- `fat32_createat: will write 1 LFN entries for 'test_mmap.txt'`
- `fat32_createat: no free directory entry`
- `argfd: fd -13 invalid`（因 `open` 返回负 errno 被当成 fd 使用）

## 原因分析
- FAT32 目录项扫描逻辑只在当前目录簇内寻找连续空闲项。
- 当目录簇已满时，逻辑没有扩展目录链，直接返回失败。
- 创建失败后上层用例仍继续使用返回值（负 errno），触发后续 `argfd` 与 `mmap` 异常。

## 解决方案
- 在 FAT32 目录项扫描过程中，如果到达目录链末尾且无可用槽，
  **自动分配新目录簇并挂链**（更新 FAT 表、清零新簇）。
- 将“文件数据簇分配”延后到确认找到空目录项之后，避免无意义分配。

## 代码改动
- 文件：`src/fs/fat32.c`
- 新增：`fat_set_clus()`、`fat_zero_cluster()`。
- `fat32_createat()` 在目录满时扩展目录链，并在写目录项前才分配文件簇。

## 验证
- 重新构建并复跑测试。
- 预期：`test_mkdir` / `test_unlink` / `test_mmap` 不再因目录项不足失败。

## 结论
- 根因是 FAT32 目录链未扩展导致的“目录项耗尽”。
- 修复后应消除 FAT32 线相关断言失败与 `argfd` 异常。