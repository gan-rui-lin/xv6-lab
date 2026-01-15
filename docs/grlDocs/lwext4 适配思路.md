# lwext4 适配思路（xv6-lab）

## 目标与原则
- 在 xv6 原有 fs 栈（xv6fs / FAT32）中插入 ext4 支持，形成 `ext4_mode / fat32_mode / xv6fs` 三态，优先尝试 ext4。
- 尽量只调用 lwext4 的高层 API（mount、open/dir、blockdev），不修改 lwext4 源码，必要改动放在 glue 层。
- 复用现有 buf cache/virtio 驱动，避免重复实现块设备。

## 核心改动概览
- 新增 glue：`src/fs/ext4fs.c|h`，对外暴露 ext4 版 namei/readi/writei/createat/getdents/unlink。
- inode 扩展：`src/fs/file.h` 增加 `ext_ino/ext_size/ext4_path`，用 `EXT4_INODE_TAG` 标识 ext4 inode，`iget` 时清零，ext4 make_inode 时填充。
- fs 桥接：`fs.c` 在 ilock/readi/writei/itrunc/stati/namei/nameiat/createat 等处分支 ext4；`fsinit` 优先 ext4 → FAT32 → xv6fs；`log` begin/end 在 ext4 模式 no-op。
- 系统调用：`sys_getdents64/sys_unlink/sys_unlinkat/create` 识别 ext4；`sys_link` 在 ext4 模式下返回 -1；console 特判。
- lwext4 配置：`CONFIG_USE_USER_MALLOC=1`（ext4_config.h），hook 到 kmalloc/kmfree；补齐缺失的 libc/断言在 `src/lib/ext4_shim.c`。

## 适配步骤（逐步）
1) **接入 blockdev**
   - 定义 `ext4_blockdev_iface ext4_iface`，实现 `bread/bwrite` 调用现有 `bread/bwrite`（1024B），内部按 512B 扇区拆分：`sector_block = lba / 2`，`sector_offset = (lba % 2)*512`。
   - 设置 `ph_bsize=512`，`ph_bbuf=ext4_bbuf`，`ph_bcnt` 按虚拟容量计算（最终放宽到 8 GiB，避免高块号被拒）。
   - 组装 `ext4_blockdev ext4_bd`，`ext4_device_register(ext4_bd, "ext4dev")`，`ext4_mount("ext4dev", "/")`。
2) **路径与 inode**
   - `resolve_path`：纯字符串规范化（绝对/相对、.、..），供 lwext4 路径使用。
   - `make_inode`：合成 inode（EXT4_INODE_TAG），填 `ext_ino/ext_size/ext4_path`，`valid=1`，避免 xv6 ilock 读盘。
   - Console 特判：在 ext4 namei/nameiat 识别 `"console"`/`"/dev/console"`，返回 T_DEVICE inode（CONSOLE）。
3) **读写接口**
   - `ext4_readi`: `ext4_fopen2(O_RDONLY)` → `ext4_fseek(off)` → `ext4_fread` 到临时缓冲 → `either_copyout`；读到数据即返回 rcnt（即便 ret 非 0 也警告）；rcnt==0 视为失败。
   - `ext4_writei`: `ext4_fopen2(O_RDWR)` → `ext4_fseek(off)` → `ext4_fwrite`；更新 `ext_size/size`。
   - `ext4_truncate`: `ext4_ftruncate(file, 0)`。
4) **目录与创建**
   - `ext4_getdents64`: `ext4_dir_open` + `ext4_dir_entry_next`，构造 `linux_dirent64`，映射类型 DIR/REG/SYMLINK，其余置 0。
   - `ext4_createat`: 目录走 `ext4_dir_mk`；文件走 `ext4_fopen2(O_RDWR|O_CREAT|O_TRUNC)`，再 make_inode。
   - `ext4_unlink_path`: 目录用 `ext4_dir_rm`，文件用 `ext4_fremove`。
5) **fs & syscall 改动**
   - `fsinit`: 尝试 ext4 → FAT32 → xv6fs。
   - `fs.c`: ilock/readi/writei/itrunc/stati/namei/nameiat/createat 增加 ext4 分支；icache 初始化扩展字段；`begin_op/end_op` 在 ext4 模式下直接返回。
   - `sysfile.c`: getdents64/unlink/unlinkat/create 识别 ext4；console 由 namei 特判；硬链接 ext4 返回 -1。
6) **lwext4 配置与补丁**
   - `ext4_config.h`: `CONFIG_USE_USER_MALLOC=1`，声明 `ext4_assert`；包含 types.h。
   - `src/lib/ext4_shim.c`: 实现 `ext4_assert(panic)`、`strcmp/strcpy`、简易 `qsort`，供 lwext4 调用。

## 调试重点
- **块设备容量**：ext4 镜像块号高（~70 万），最初虚拟容量 1GiB 导致 `ext4_block_readbytes` 越界返回 EINVAL=22，rcnt=0，exec 读 ELF 头失败；放宽到 8GiB 解决。
- **日志**：在 `exec.c` 和 `ext4_readi` 打印文件名、inode、size、off/len、ret/rcnt，快速定位读失败原因。
- **console**：缺少特判会导致 initcode 打开 fd0/1/2 失败，后续 printf/exec 报错；已与 FAT32 逻辑对齐。

## 目录索引
- 代码：`src/fs/ext4fs.c`, `src/fs/ext4fs.h`, `src/fs/file.h`, `src/fs/fs.c`, `src/fs/log.c`, `src/syscall/sysfile.c`, `src/fs/lwext4/include/ext4_config.h`, `src/lib/ext4_shim.c`
- 调试记录：`docs/grlDocs/brk&sleep执行失败调试报告.md`（块设备容量问题）

## 现存限制 / TODO
- 硬链接、设备文件未实现；权限/uid/gid/ACL 未接。
- rename/link 后的路径缓存未刷新。
- 非对齐写的性能/一致性待进一步压测。
- mount/umount 系统调用未传递 ext4 参数，默认挂载 "/"。
