# 软链接持久性说明

## 问题
软链接是否永久保存在磁盘镜像中？还是需要每次重新创建？

## 答案：永久保存 ✅

### 验证

```bash
$ docker run --rm --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  alpine sh -c '...'

=== Current /lib directory ===
lrwxrwxrwx  ld-linux-riscv64-lp64d.so.1 -> ../glibc/lib/ld-linux-riscv64-lp64d.so.1
lrwxrwxrwx  libc.so.6 -> ../glibc/lib/libc.so.6
```

时间戳显示：`Jan 28 09:26` - 这是创建时间，软链接已经写入磁盘。

### 原理

1. **ext4 文件系统**
   - sdcard-rv.img 是一个 ext4 格式的磁盘镜像文件
   - 所有文件系统操作（创建文件、目录、软链接等）都会持久化
   - 类似于修改一个真实的硬盘

2. **Docker 挂载机制**
   - Docker 通过 loop 设备挂载磁盘镜像
   - 写入操作直接修改宿主机上的 .img 文件
   - 容器退出后，修改保留在 .img 文件中

3. **软链接的存储**
   - 软链接存储在文件系统的 inode 中
   - 占用的空间很小（只存路径字符串）
   - 和普通文件一样持久

### 工作流程

```
宿主机文件系统
    ↓
sdcard-rv.img（ext4 镜像文件）
    ↓
Docker 容器通过 loop 设备挂载
    ↓
在 /mnt/lib 创建软链接
    ↓
umount 卸载，写入 sdcard-rv.img
    ↓
修改永久保存在 sdcard-rv.img 文件中
```

## 跨机器移植

### 情况 1：复制整个项目目录（推荐）✅

如果您复制整个 `/Users/mac/Desktop/project/xv6-lab` 目录到另一台机器：

```bash
# 在新机器上
cd /path/to/xv6-lab
./run-sdcard-rv.sh
```

**不需要重新创建软链接**，因为 sdcard-rv.img 文件已经包含了软链接。

### 情况 2：只复制 sdcard-rv.img ✅

如果您只复制 `sdcard-rv.img` 文件：

```bash
# 在新机器上
qemu-system-riscv64 -drive file=sdcard-rv.img,format=raw,...
```

**仍然不需要重新创建软链接**，软链接在镜像内部。

### 情况 3：从源重新构建镜像 ❌

如果您从源代码重新生成 `sdcard-rv.img`（使用 mkfs 等工具）：

```bash
# 重新构建镜像
make clean
make sdcard-rv.img
```

**这种情况下需要重新创建软链接**，因为是全新的镜像文件。

## 验证软链接是否存在

### 方法 1：使用 Docker（推荐）

```bash
docker run --rm --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  alpine sh -c '
    apk add e2fsprogs > /dev/null 2>&1
    losetup -f /image.img
    mount -o ro /dev/loop0 /mnt
    ls -la /mnt/lib/ld-linux-riscv64-lp64d.so.1
    ls -la /mnt/lib/libc.so.6
    umount /mnt
    losetup -d /dev/loop0
'
```

### 方法 2：在 xv6 运行时验证

```bash
# 启动 xv6
./run-sdcard-rv.sh

# 在 xv6 shell 中
$ ls -l /lib/
```

## 常见误解

### ❌ 误解 1：容器内的修改不会保存
**真相**：通过 `-v` 挂载的宿主机文件会被修改保存

### ❌ 误解 2：软链接只在内存中
**真相**：软链接是文件系统对象，存储在磁盘上

### ❌ 误解 3：需要特殊命令才能持久化
**真相**：umount 时自动写回，无需特殊操作

## 技术细节

### ext4 Inode 结构

软链接的存储方式：
1. **短路径**（< 60 字节）：直接存储在 inode 的 `i_block` 字段中
2. **长路径**（≥ 60 字节）：分配数据块存储路径字符串

我们的软链接：
```
ld-linux-riscv64-lp64d.so.1 -> ../glibc/lib/ld-linux-riscv64-lp64d.so.1
                                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                                 40 字节 → 存储在 inode 中
```

### 写入时机

```c
// Linux 内核中的软链接创建
sys_symlink()
  → vfs_symlink()
    → ext4_symlink()
      → 分配 inode
      → 存储目标路径
      → 标记 inode 为 dirty
      → 等待 sync/umount 时写回磁盘
```

Docker 容器 umount 时触发写回：
```bash
umount /mnt  # 触发 sync，所有脏数据写入磁盘
```

## 如何验证写入成功

### 检查镜像文件修改时间

```bash
$ ls -l sdcard-rv.img
-rw-r--r-- 1 mac staff 4.0G Jan 28 09:26 sdcard-rv.img
                                 ^^^^^^^^
                                 修改时间 = 软链接创建时间
```

### 使用 debugfs 查看（高级）

```bash
docker run --rm -it \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  ubuntu bash -c '
    apt-get update && apt-get install -y e2fsprogs
    debugfs -R "ls -l lib" /image.img
'
```

## 总结

| 场景 | 是否需要重新创建软链接 |
|-----|-------------------|
| 同一台机器重启 | ❌ 不需要 |
| 复制整个项目到新机器 | ❌ 不需要 |
| 只复制 sdcard-rv.img | ❌ 不需要 |
| 从源重新构建镜像 | ✅ 需要 |
| Git clone 项目（如果 .img 在仓库中） | ❌ 不需要 |
| Git clone 项目（.img 不在仓库） | ✅ 需要 |

## 建议

### 如果 sdcard-rv.img 在版本控制中

```bash
# .gitignore
# 不要忽略镜像文件，这样软链接会一起分发
# sdcard-rv.img  # 注释掉这行
```

### 如果 sdcard-rv.img 不在版本控制中

在 README 或 setup 脚本中添加：

```bash
#!/bin/bash
# setup.sh

# 检查软链接是否存在
if ! docker run --rm --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  alpine sh -c '
    apk add e2fsprogs > /dev/null 2>&1 &&
    losetup -f /image.img &&
    mount -o ro /dev/loop0 /mnt &&
    test -L /mnt/lib/ld-linux-riscv64-lp64d.so.1
  ' 2>/dev/null; then
  echo "Creating missing symlinks..."
  ./fix-symlinks.sh
fi
```

## 参考

- ext4 文件系统文档：https://www.kernel.org/doc/html/latest/filesystems/ext4/
- Linux 软链接实现：fs/ext4/symlink.c
- Docker volume 机制：https://docs.docker.com/storage/volumes/
