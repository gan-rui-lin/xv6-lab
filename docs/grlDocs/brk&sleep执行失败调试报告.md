# brk & chdir 执行失败调试报告（ext4 模式）

## 现象
- `ext4_mode=1` 时，`exec chdir` / `exec brk` 失败，`sleep` 正常。
- 日志（节选）：
  - `[INFO] ext4: mounted device ext4dev`
  - `[INFO] sys_chdir: ... cwdpath now '/musl/basic'`
  - `[INFO] ext4_readi: open '/musl/basic/chdir' inode 0x12 size 0xe0d0 off 0 len 64`
  - `[ERROR] ext4_readi: fread '/musl/basic/chdir' off 0 len 64 ret 22 rcnt 0`
  - `[ERROR] exec: read elf header failed for chdir`
  - `sleep` 执行时能持续读多个 block（多次 ext4_readi 日志），最终成功。
- FAT32 分支执行同样镜像正常，说明镜像内容无误。

## 排查过程
1) 核查镜像内容：
   - `debugfs -R 'ls /musl/basic' sdcard-rv.img`，确认 `brk`/`chdir` 文件存在。
   - `debugfs -R 'stat /musl/basic/brk' sdcard-rv.img`，inode=17，size≈56KB，extents `(0-13):699353-699366`，block size=4096。
2) 在 `ext4_readi` 加日志，发现 rcnt=0，ret=22(EINVAL)，且 off=0、len=64，即读取 ELF header 被拒。
3) 追踪到 `ext4_block_readbytes`，检测条件：
   - `off + len > bdev->part_size` 则返回 EINVAL。
   - 我们在 `ext4fs_init` 把虚拟 blockdev 的 `part_size` 设为 1 GiB，导致高块号（~699k * 4KB ≈ 2.8 GiB）被判越界。
4) FAT32 正常的原因：FAT32 路径没有使用 lwext4 的 blockdev 边界检查；ext4 才有这个逻辑。

## 解决方案
- 将 ext4 虚拟块设备的逻辑容量放宽至 8 GiB，避免高块号被人为拦截。
  - 修改：`src/fs/ext4fs.c` 中 `ext4_iface.ph_bcnt` / `ext4_bd.part_size` 设置为 `8ull * 1024 * 1024 * 1024`。
- 保留 ext4 自身的块读取逻辑，真实设备大小仍由底层 virtio/镜像决定。

## 结果
- `chdir`、`brk` 在 ext4 模式下可正常 exec；`sleep` 本就正常。
- 日志中不再出现 `ext4_readi: fread ... ret 22 rcnt 0`。

## 其他相关调整
- 加入 ext4 malloc hook：`CONFIG_USE_USER_MALLOC=1`，并实现 `ext4_user_malloc/calloc/realloc/free` 映射到 kmalloc/kmfree。
- 特殊设备处理：ext4 namei/nameiat 识别 `"console"`，生成 T_DEVICE inode，保证 initcode 打开 fd0/1/2。
- 日志增强：`exec.c` 对 ELF 头/PHDR 读取失败给出具体提示；`ext4_readi` 打开文件时打印 inode/size/off/len。

## 经验教训
- 嵌入第三方 FS 时，虚拟 blockdev 的逻辑容量需要覆盖镜像实际可见范围，否则会在高块号访问时无声返回 EINVAL。
- 对于早期加载路径（exec 读取 ELF 头），添加足够日志能快速定位 “读不到第一段” 的原因。
- 与 FAT32 并存时，测试同一镜像在两条路径都跑一遍，可以快速区分镜像/权限问题和实现细节问题。
