#!/bin/bash
# 使用 sdcard-rv.img 启动 xv6 的快捷脚本

# 默认配置
IMAGE_NAME="sdcard-rv.img"
BUILD_TYPE="all"
LOG_FILE="runsh.log"

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -d)
            GDB_FLAG="-d"
            shift
            ;;
        -n|--netforward)
            NET_FLAG="-n"
            shift
            ;;
        *)
            echo "用法: $0 [-d] [-n]"
            echo "  -d  启用 GDB 调试"
            echo "  -n  启用网络转发"
            exit 1
            ;;
    esac
done
# cp ./oscomp-midwest-onsitefinal-main/testcase-rv-fat32.img ./sdcard-rv.img
cp ./sdcard-rv.img.backup ./sdcard-rv.img
echo "===================================="
echo "使用 testcase-rv-fat32.img (FAT32) 启动 xv6"
echo "日志将保存到: $LOG_FILE"
echo "===================================="

# 使用 bash -c 在子shell中执行，并使用 exec 重定向
# 这样可以捕获所有输出，包括 QEMU 的输出
bash -c "
    set -o pipefail
    exec > >(tee '$LOG_FILE') 2>&1
    ./run.sh -f '$IMAGE_NAME' -t '$BUILD_TYPE' $GDB_FLAG $NET_FLAG
    exit_code=\$?
    # 等待 tee 进程完成
    wait
    exit \$exit_code
"

EXIT_CODE=$?

# 再等一下确保文件写入完成
sleep 0.5

echo ""
echo "===================================="
if [ -f "$LOG_FILE" ]; then
    echo "✓ 日志已保存到: $LOG_FILE"
    LOG_SIZE=$(ls -lh "$LOG_FILE" | awk '{print $5}')
    echo "✓ 文件大小: $LOG_SIZE"
else
    echo "✗ 警告: 日志文件未创建"
fi
echo "===================================="

exit $EXIT_CODE
