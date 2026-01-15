# ext4 兼容 & VFS 整合报告（2025-01-14）

## 背景与目标
- 在 xv6 栈中引入 ext4 支持，形成 `ext4_mode / fat32_mode / xv6fs` 三态，优先尝试 ext4。
- 依赖内置 `lwext4`（`src/fs/lwext4`），只用其高层接口（mount、open/dir、blockdev），避免侵入原有元数据结构。

## 关键代码路径（函数级说明）
- `ext4fs_init(dev)`（`src/fs/ext4fs.c`）
  - 构造 lwext4 blockdev：`ext4_iface` 绑定 `bread/bwrite`，物理扇区 512B，借助 1024B buf cache 做半扇区映射（`sector_block/sector_offset`）。
  - `ext4_device_register` -> `ext4_mount(EXT4_DEV_NAME, "/")`，成功后置 `ext4_mode=1`；失败则让 `fsinit` 自动回退 FAT32/xv6fs。
  - malloc hook：`CONFIG_USE_USER_MALLOC=1`（`ext4_config.h`），实现 `ext4_user_malloc/calloc/realloc/free` 映射到 kmalloc/kmfree。
- 路径与 inode
  - `resolve_path`：纯字符串规范化（绝对/相对、.、..），所有 ext4 操作用绝对路径。
  - `make_inode`：生成 `EXT4_INODE_TAG` 的合成 inode，写入 `ext_ino/ext_size/ext4_path`，`valid=1` 避免 xv6 ilock 读盘。
  - `fs.c`：ilock/readi/writei/itrunc/stati/namei/nameiat/createat 均加入 ext4 分支；log begin/end 在 ext4 下直接返回。
- I/O 适配
  - `ext4_readi`：`ext4_fopen2` + `ext4_fread`，拷入临时内核缓冲后 copyout。
  - `ext4_writei`：`ext4_fopen2` + `ext4_fwrite`，写后更新 `ext_size/size`。
  - `ext4_truncate`：`ext4_ftruncate` 到 0，清零大小。
  - `ext4_getdents64`：`ext4_dir_open` + `ext4_dir_entry_next`，填充 `linux_dirent64`，映射类型 REG/DIR/SYMLINK，其他置 0。
  - `ext4_unlink_path`：根据 is_dir 选择 `ext4_dir_rm` 或 `ext4_fremove`。
- 系统调用桥接
  - `sys_getdents64`、`sys_unlink/sys_unlinkat`、`create()` 识别 ext4；`sys_link` 在 ext4 模式下返回 -1（硬链接未做）。
  - `log.c`：`begin_op/end_op` 在 `ext4_mode` 时 no-op，避免与 lwext4 事务重复。
- inode 扩展字段（`src/fs/file.h`）
  - 增加 `ext_ino/ext_size/ext4_path`，iget 时清零，ext4 合成 inode 时填充。

## ext4 特性与当前取舍
- extents & 目录哈希：`CONFIG_EXTENTS_ENABLE`、`CONFIG_DIR_INDEX_ENABLE` 默认开启，适合大文件/大目录。
- 日志：`CONFIG_JOURNALING_ENABLE=1`，lwext4 内部有 JBD 路径；因此关闭 xv6 log（ext4 模式下 begin/end 直接返回）。
- 块大小：物理 512B，逻辑块大小由超级块决定（mount 时 `ext4_block_set_lb_size`），可适配 1K/2K/4K。
- 未接入：硬链接、设备文件、权限/uid/gid/ACL；rename/link 后路径缓存未刷新。

## 使用与测试要点
1) 准备 ext4 镜像（默认挂载 "/"），`fsinit` 成功则 `ext4_mode=1`，否则自动回退 FAT32/xv6fs。
2) `getdents64` 返回 Linux 结构，类型映射仅 REG/DIR/SYMLINK；目录删除需 AT_REMOVEDIR。
3) 写路径用 512B 分段在 1024B 缓存上读改写，关注非对齐/跨扇区写的行为和性能。
4) 路径缓存：inode 存绝对路径，频繁 rename/link 可能产生陈旧路径，可重点压测此场景。
5) 调试：xv6 日志不生效，关注 lwext4 返回码（EOK/ENOENT/EIO 等）。

## 风险与 TODO
- 硬链接/设备文件未实现；权限模型未接。
- rename/link 场景下路径缓存未刷新。
- 非对齐写放大及潜在一致性问题需实测。
- mount/umount 系统调用未传递 ext4 参数，当前只默认挂载 "/"。

## 相关文件
- `src/fs/ext4fs.c`, `src/fs/ext4fs.h`
- `src/fs/fs.c`, `src/fs/file.h`, `src/fs/log.c`, `src/syscall/sysfile.c`
- `src/fs/lwext4/include/ext4_config.h`

回退策略：`ext4fs_init` 失败自动继续 FAT32/xv6fs，无需额外配置。
