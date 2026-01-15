#!/bin/bash
# 定义镜像文件和挂载点的绝对路径
IMG_FILE="/home/grl/codeRepo/xv6-lab/sdcard-rv.img"
MOUNT_DIR="/mnt/sdcard-rv"

# 确保使用绝对路径。先尝试卸载（如果已挂载），忽略错误信息
sudo umount "$MOUNT_DIR" 2>/dev/null

# 创建挂载点目录（如果不存在）
sudo mkdir -p "$MOUNT_DIR"

# 执行挂载命令
sudo mount -o loop "$IMG_FILE" "$MOUNT_DIR"

# 检查上一条命令（mount）是否执行成功
if [ $? -eq 0 ]; then
    echo "✅ 镜像挂载成功！挂载点: $MOUNT_DIR"
    # 可以尝试列出挂载点内容
    ls -la "$MOUNT_DIR"
else
    echo "❌ 镜像挂载失败，请检查错误信息。"
fi