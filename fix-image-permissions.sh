#!/bin/bash
# 修复 sdcard-rv.img 镜像文件权限的脚本

set -e

echo "=== 修复测试镜像权限 ==="
echo ""

# 检查镜像文件是否存在
if [ ! -f "sdcard-rv.img" ]; then
    if [ -f "sdcard-rv.img.xz" ]; then
        echo "解压镜像文件..."
        xz -d -k sdcard-rv.img.xz
    else
        echo "错误: 找不到 sdcard-rv.img 或 sdcard-rv.img.xz"
        exit 1
    fi
fi

echo "当前镜像文件: sdcard-rv.img"
ls -lh sdcard-rv.img
echo ""

# 检查 Docker 是否可用
if ! command -v docker &> /dev/null; then
    echo "错误: Docker 未安装或未运行"
    echo "请安装 Docker Desktop for Mac: https://www.docker.com/products/docker-desktop"
    exit 1
fi

echo "使用 Docker 修复权限..."
echo ""

# 创建临时目录
TEMP_DIR=$(mktemp -d)
echo "临时目录: $TEMP_DIR"

# 复制镜像到临时目录
cp sdcard-rv.img "$TEMP_DIR/sdcard-rv.img"

# 使用 Docker 挂载并修复权限
docker run --rm --privileged \
    -v "$TEMP_DIR:/work" \
    alpine:latest sh -c "
    apk add --no-cache e2fsprogs

    echo '挂载镜像...'
    mkdir -p /mnt/img
    mount -o loop /work/sdcard-rv.img /mnt/img

    echo '当前权限状态:'
    ls -la /mnt/img/musl/basic/ | head -n 10

    echo ''
    echo '修复 /musl/ 目录权限...'
    chmod -R 755 /mnt/img/musl/

    echo '修复 /glibc/ 目录权限（如果存在）...'
    if [ -d /mnt/img/glibc ]; then
        chmod -R 755 /mnt/img/glibc/
    fi

    echo ''
    echo '修复后的权限:'
    ls -la /mnt/img/musl/basic/ | head -n 10

    echo ''
    echo '同步并卸载...'
    sync
    umount /mnt/img

    echo '权限修复完成！'
"

if [ $? -eq 0 ]; then
    echo ""
    echo "备份原镜像..."
    mv sdcard-rv.img sdcard-rv.img.backup

    echo "使用修复后的镜像..."
    mv "$TEMP_DIR/sdcard-rv.img" sdcard-rv.img

    echo ""
    echo "✅ 镜像权限修复成功！"
    echo ""
    echo "原镜像已备份为: sdcard-rv.img.backup"
    echo "现在可以运行测试了: ./run-sdcard-rv.sh"
else
    echo ""
    echo "❌ 权限修复失败"
    rm -rf "$TEMP_DIR"
    exit 1
fi

# 清理临时目录
rm -rf "$TEMP_DIR"
