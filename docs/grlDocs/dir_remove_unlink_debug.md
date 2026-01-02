# FAT32 unlink 调试记录：LFN/SFN 长短文件名差异与修复

本文记录一次 FAT32 上 `unlinkat` 的问题定位与修复过程：为什么长文件名（如 `test_unlink`）的删除最初失败，而短文件名（如 `tes1`）的删除成功；最终如何通过支持 LFN（Long File Name）匹配与清理解决问题。同时补充调试工具与日志插桩方法，方便后续复现与扩展。

## 背景与症状
- 场景：实现 `SYS_unlinkat(35)` 的 FAT32 版本，用户态测试会先 `open(O_CREAT)` 创建文件，再 `unlink` 删除，然后尝试重新 `open` 验证删除效果。
- 现象：
	- 对长文件名 `./test_unlink`：创建成功，但 `unlink` 失败（返回 -1）。
	- 对短文件名 `./tes1`：创建成功，`unlink` 成功（返回 0）。
	- 删除后再打开同名文件返回 -1（预期，如文件确实被删除）。

## FAT32 基础：SFN 与 LFN
- SFN（Short File Name）：8.3 格式（最多 8 字节主名 + 3 字节扩展），大写、字符集有限。目录里必有一个 SFN 条目代表文件/目录。
- LFN（Long File Name）：当名字超出 8.3 或需保留大小写/字符时，FAT32 会生成一个或多个 LFN 条目，紧挨在对应 SFN 之前，以 UCS-2 片段逆序存储完整长名。
- 直接用“原始长名”查找时，应优先用 LFN 比较；若没有 LFN，则退回 SFN 比较。删除时需将 SFN 与其前置的所有 LFN 一并标记为删除（首字节 0xE5）。

## 根因分析
- 初始的 `dir_remove()` 仅用 SFN 比较：
	- 文件名 `test_unlink` 长度 11，不是 8.3，FAT32 会生成 LFN。我们的删除函数只做 `match_sfn()`，因此找不到对应 SFN（SFN 可能是截断/生成的别名，而非完整长名字符串）。
	- 文件名 `tes1` 长度短且满足 8.3，SFN 直接匹配，因此删除成功。

## 修复方案与实现要点
文件：src/fs/fat32.c

- 在 `dir_remove()` 中加入对 LFN 的支持：
	- 遍历扇区条目时，遇到 LFN 条目先用 `lfn_extract_append()` 累积 UCS-2 片段为 ASCII（简化），得到完整 LFN 字符串；
	- 到达 SFN 时，先用累积的 LFN 与目标组件比较（`str_eq`），若不匹配再用 `match_sfn()` 比较 SFN；
	- 匹配成功后：
		- 校验类型（文件/目录）一致；保护 `"."`、`".."`；
		- 标记 SFN 为删除（首字节 0xE5），并向后回溯清理紧邻在 SFN 之前的所有 LFN 条目；
		- 处理跨扇区的 LFN 链：从当前扇区向前回溯，如果到头则读取前一个扇区继续回溯，直至遇到非 LFN 或抵达簇头。

- 相关辅助函数：
	- `lfn_extract_append(de, buf, &len)`：按 LFN 片段规范提取 13 个 UCS-2 字符，逆序累积到缓冲区，最终得到完整 LFN；
	- `str_eq(a,b)`：简单的 C 字符串相等比较；
	- `match_sfn(comp, name11)`：将组件按 8.3 上大写/空格填充，逐字节与目录项的 11 字节短名比较。

- FAT32 unlink 入口：
	- `fat32_unlinkat(struct inode *dir, const char *name, unsigned int flags)`：根据 `AT_REMOVEDIR` 判断类型，得到目录起始簇后调用 `dir_remove()`；
	- 保护特殊名 `"."` 和 `".."`。

## 相关内核路径处理
文件：src/syscall/sysfile.c

- `sys_unlinkat()`：
	- 解析 `dirfd` 与路径；为兼容测试，做了相对路径前缀 `"./"` 的规范化；
	- FAT32 模式下，`AT_FDCWD` 的相对路径使用 FAT32 根作为锚点（避免混合非 FAT32 的 cwd 干扰）；
	- 计算父路径与叶子名，调用 FAT32 的 `fat32_unlinkat(base, leaf, flags)`。

## 调试过程
1) 打开日志宏：在编译选项中启用 `LOG_DEBUG/INFO/WARN/ERROR` 宏，定位关键路径。

2) 插桩位置：
	- `sys_unlinkat()`：打印 `npath`、`last_slash`、`dirfd`、选择的 `base` 与 `leaf`。
	- FAT32 名字解析：在 `fat32_namei()`、`fat32_nameiat()` 中打印分解的组件与起始目录簇；
	- 目录遍历：在 `dir_find()` 与 `dir_remove()` 中打印每个扇区的 LFN 累积结果（在 LFN 完整时打印 `final LFN='...'`），便于确认条目是否正确识别；
	- 创建路径：`fat32_createat()` 打印是否需要 LFN、写入位置与簇号、创建的 inode 信息。

3) 运行与复现：
- 更新测试程序到镜像（基于 `mtools`）：
```bash
mcopy -s -i fat32.img /home/grl/codeRepo/testsuits-for-oskernel/riscv-syscalls-testing/user/build/riscv64/* ::
```

- 启动 QEMU（RISC-V）：
```bash
make debug && qemu-system-riscv64 -machine virt -kernel kernel-qemu -m 128M -nographic -smp 2 -bios default \
	-drive file=fat32.img,if=none,format=raw,id=x0 \
	-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
	-device virtio-net-device,netdev=net -netdev user,id=net
```

- 连接 GDB 并下断点：
```bash
gdb-multiarch kernel-qemu -ex "target remote:1234" -ex "b sys_unlinkat" -ex "b sys_open"
```

4) 观察日志与行为：
- 长名案例（`test_unlink`）最初：只做 SFN 匹配，`dir_remove` 报“not found”。
- 修复后：`dir_remove` 会打印若干 `lfn_extract_append: final LFN='...'`，当匹配到完整长名时，回溯删除 LFN 链 + SFN；`sys_unlinkat` 返回 0。
- 短名案例（`tes1`）：
	- 创建阶段可能会写 1 个 LFN（取决于我们生成 SFN 的简化逻辑与字符集），因此删除时同样会出现 `final LFN='tes1'`，最后成功删除。

## 结果验证
- `unlink('./tes1')` 返回 0；随后 `open('./tes1')` 返回 -1（预期，文件不存在）。
- `unlink('./test_unlink')`：修复后，能够匹配并删除包含 LFN 的条目，返回 0；随后 `open('./test_unlink')` 返回 -1。

## 仍需完善与下一步
- LFN 链的更强健清理：跨簇链更复杂情况需更多测试覆盖；目前已经实现跨扇区向后回溯，但仍建议增加一致性校验。
- SFN 别名生成：标准推荐“波浪号”风格（如 `TEST_U~1`）并带校验避免冲突；当前实现仍为简化的截断/大写，建议后续完善，减少潜在同名冲突。
- `AT_REMOVEDIR` 完整支持：目录非空检查与引用计数/链接数维护，当前有基础保护，建议补全。

## 结论
问题的关键在于 FAT32 的双表示（LFN 与 SFN）。仅按 SFN 匹配会导致对长文件名的删除失败；通过在 `dir_remove()` 中累积并匹配 LFN，然后与 SFN 一并删除，问题得到解决。配合 `sys_unlinkat` 的路径规范化与 FAT32 相对路径解析策略，本轮测试中的 `unlink` 行为已符合预期。

