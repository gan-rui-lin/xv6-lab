# 使用 Docker 挂载 ext4 磁盘镜像

## 方法 1: 使用特权容器直接挂载（推荐）

### 步骤 1: 创建挂载点目录
```bash
cd /Users/mac/Desktop/project/xv6-lab
mkdir -p mnt
```

### 步骤 2: 启动特权容器并挂载镜像
```bash
docker run --rm -it \
  --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  -v "$(pwd)/mnt:/mnt" \
  ubuntu:22.04 bash
```

### 步骤 3: 在容器内挂载镜像
```bash
# 在容器内执行
apt-get update && apt-get install -y e2fsprogs

# 创建 loop 设备
losetup -f /image.img

# 查看 loop 设备（通常是 /dev/loop0）
losetup -a

# 挂载到 /mnt
mount -o ro /dev/loop0 /mnt

# 查看内容
ls -la /mnt
ls -la /mnt/glibc/lib
ls -la /mnt/musl/lib

# 使用完毕后卸载
umount /mnt
losetup -d /dev/loop0
exit
```

### 完整的一键命令
```bash
cd /Users/mac/Desktop/project/xv6-lab
mkdir -p mnt

docker run --rm -it \
  --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  -v "$(pwd)/mnt:/mnt" \
  ubuntu:22.04 bash -c "
    apt-get update && apt-get install -y e2fsprogs > /dev/null 2>&1 && \
    losetup -f /image.img && \
    mount -o ro /dev/loop0 /mnt && \
    echo '=== Mounted successfully ===' && \
    echo 'Contents of /mnt:' && \
    ls -lah /mnt && \
    echo '' && \
    echo 'glibc libraries:' && \
    ls -lah /mnt/glibc/lib/*.so* 2>/dev/null | head -10 && \
    echo '' && \
    echo 'musl libraries:' && \
    ls -lah /mnt/musl/lib/*.so* 2>/dev/null | head -10 && \
    echo '' && \
    echo 'Press Ctrl+C to exit, or wait for interactive shell...' && \
    bash
"
```

## 方法 2: 只读检查（快速方案）

如果只是想检查共享库文件是否存在：

```bash
cd /Users/mac/Desktop/project/xv6-lab

docker run --rm -it \
  --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  ubuntu:22.04 bash -c "
    apt-get update && apt-get install -y e2fsprogs > /dev/null 2>&1 && \
    losetup -f /image.img && \
    mount -o ro /dev/loop0 /mnt && \
    echo '=== Checking glibc libraries ===' && \
    find /mnt/glibc/lib -name '*.so*' -type f 2>/dev/null | head -20 && \
    echo '' && \
    echo '=== Checking musl libraries ===' && \
    find /mnt/musl/lib -name '*.so*' -type f 2>/dev/null | head -20 && \
    echo '' && \
    echo '=== Checking dynamic linker ===' && \
    ls -lah /mnt/glibc/lib/ld-linux-*.so* 2>/dev/null && \
    ls -lah /mnt/musl/lib/ld-musl-*.so* 2>/dev/null && \
    ls -lah /mnt/musl/lib/libc.so 2>/dev/null && \
    umount /mnt && \
    losetup -d /dev/loop0
"
```

## 方法 3: 修改镜像内容（高级）

如果需要在镜像中添加或修改文件：

```bash
cd /Users/mac/Desktop/project/xv6-lab
mkdir -p mnt

docker run --rm -it \
  --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img" \
  -v "$(pwd)/mnt:/mnt" \
  ubuntu:22.04 bash
```

在容器内：
```bash
# 安装工具
apt-get update && apt-get install -y e2fsprogs

# 挂载（可读写）
losetup -f /image.img
mount /dev/loop0 /mnt

# 检查磁盘使用情况
df -h /mnt

# 进行修改...
# 例如：复制缺失的库文件
# cp /path/to/libc.so.6 /mnt/glibc/lib/

# 卸载
umount /mnt
losetup -d /dev/loop0
exit
```

## 方法 4: 使用 Alpine Linux 容器（轻量级）

```bash
cd /Users/mac/Desktop/project/xv6-lab

docker run --rm -it \
  --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  alpine:latest sh -c "
    apk add --no-cache e2fsprogs > /dev/null 2>&1 && \
    mkdir -p /mnt && \
    losetup -f /image.img && \
    mount -o ro /dev/loop0 /mnt && \
    echo '=== Image mounted at /mnt ===' && \
    ls -lah /mnt && \
    sh
"
```

## 方法 5: 直接使用 e2tools（无需挂载）

这种方法不需要挂载，直接读取 ext4 文件系统：

```bash
cd /Users/mac/Desktop/project/xv6-lab

docker run --rm -it \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  -v "$(pwd):/work" \
  ubuntu:22.04 bash -c "
    apt-get update && apt-get install -y e2tools > /dev/null 2>&1 && \
    echo '=== Listing glibc/lib ===' && \
    e2ls -l /image.img:/glibc/lib && \
    echo '' && \
    echo '=== Listing musl/lib ===' && \
    e2ls -l /image.img:/musl/lib && \
    echo '' && \
    echo '=== Extracting file (example) ===' && \
    # e2cp /image.img:/glibc/lib/libc.so.6 /work/libc.so.6 && \
    bash
"
```

## 常见问题排查

### 1. 检查镜像文件格式
```bash
docker run --rm \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  ubuntu:22.04 bash -c "
    apt-get update && apt-get install -y file > /dev/null 2>&1 && \
    file /image.img
"
```

### 2. 检查 ext4 文件系统
```bash
docker run --rm \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  ubuntu:22.04 bash -c "
    apt-get update && apt-get install -y e2fsprogs > /dev/null 2>&1 && \
    e2fsck -n /image.img
"
```

### 3. 查看超级块信息
```bash
docker run --rm \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  ubuntu:22.04 bash -c "
    apt-get update && apt-get install -y e2fsprogs > /dev/null 2>&1 && \
    dumpe2fs /image.img | head -30
"
```

## 建议用法

### 快速检查共享库
```bash
cd /Users/mac/Desktop/project/xv6-lab

# 一键检查脚本
docker run --rm \
  --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  ubuntu:22.04 bash -c '
    apt-get update && apt-get install -y e2fsprogs > /dev/null 2>&1 && \
    losetup -f /image.img && \
    mount -o ro /dev/loop0 /mnt && \

    echo "========================================" && \
    echo "Checking shared libraries in image..." && \
    echo "========================================" && \
    echo "" && \

    echo "1. glibc dynamic linker:" && \
    ls -lh /mnt/glibc/lib/ld-linux-*.so* 2>/dev/null || echo "   NOT FOUND" && \
    echo "" && \

    echo "2. glibc libc.so:" && \
    ls -lh /mnt/glibc/lib/libc.so* 2>/dev/null || echo "   NOT FOUND" && \
    echo "" && \

    echo "3. musl dynamic linker:" && \
    ls -lh /mnt/musl/lib/ld-musl-*.so* 2>/dev/null || echo "   NOT FOUND" && \
    echo "" && \

    echo "4. musl libc.so:" && \
    ls -lh /mnt/musl/lib/libc.so 2>/dev/null || echo "   NOT FOUND" && \
    echo "" && \

    echo "5. All glibc libraries:" && \
    ls -lh /mnt/glibc/lib/*.so* 2>/dev/null | wc -l && \
    echo "" && \

    echo "6. All musl libraries:" && \
    ls -lh /mnt/musl/lib/*.so* 2>/dev/null | wc -l && \
    echo "" && \

    umount /mnt && \
    losetup -d /dev/loop0
'
```

### 交互式探索
```bash
cd /Users/mac/Desktop/project/xv6-lab

# 进入交互式 shell
docker run --rm -it \
  --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  -v "$(pwd):/host" \
  ubuntu:22.04 bash -c "
    apt-get update && apt-get install -y e2fsprogs file tree > /dev/null 2>&1 && \
    losetup -f /image.img && \
    mount -o ro /dev/loop0 /mnt && \
    echo 'Image mounted at /mnt' && \
    echo 'Your project directory is at /host' && \
    cd /mnt && \
    bash
"
```

## 注意事项

1. **只读挂载**: 使用 `-v "$(pwd)/sdcard-rv.img:/image.img:ro"` 中的 `:ro` 确保镜像文件不会被修改
2. **特权模式**: `--privileged` 是必需的，因为需要访问 loop 设备
3. **清理**: 容器退出后自动清理（`--rm` 选项）
4. **macOS 特殊性**: 由于 Docker Desktop for Mac 运行在虚拟机中，所有操作实际上是在 Linux 虚拟机中进行的

## 快速参考

```bash
# 最简单的检查命令
cd /Users/mac/Desktop/project/xv6-lab
docker run --rm --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  ubuntu:22.04 bash -c \
  "apt-get update && apt-get install -y e2fsprogs >/dev/null 2>&1 && \
   losetup -f /image.img && mount -o ro /dev/loop0 /mnt && \
   ls -la /mnt && ls -la /mnt/glibc/lib && ls -la /mnt/musl/lib"
```
