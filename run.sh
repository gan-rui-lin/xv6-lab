#!/bin/bash

# 默认配置
BUILD_TYPE="debug"
IMAGE_FILE="sdcard-final.img"
GDB_DEBUG="0"
GDB_FLAGS=""
NET_FORWARD="0"
NET_DUMP_FILE=""
NET_DUMP_OBJ=""

# 显示用法信息
usage() {
    echo "用法: $0 [选项]"
    echo "选项:"
    echo "  -t, --type TYPE    构建类型 (debug/all), 默认: $BUILD_TYPE"
    echo "  -f, --file FILE    镜像文件名, 默认: $IMAGE_FILE"
    echo "  -d                 启用 GDB 调试 (为 QEMU 添加 -s -S)"
    echo "  -n, --netforward   启用 user net hostfwd (UDP 12345)"
    echo "  --netdump FILE     启用 QEMU 抓包到 FILE (filter-dump)"
    echo "  -h, --help         显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 -t debug -f sdcard-final.img"
    echo "  $0 --type all --file sdcard.img"
    echo "  $0 -t debug -f sdcard-final.img -d"
    echo "  $0 -t debug -f sdcard-final.img --netforward"
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -f|--file)
            IMAGE_FILE="$2"
            shift 2
            ;;
        -d)
            GDB_DEBUG="1"
            shift
            ;;
        -n|--netforward)
            NET_FORWARD="1"
            shift
            ;;
        --netdump)
            NET_DUMP_FILE="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "错误: 未知选项 $1"
            usage
            exit 1
            ;;
    esac
done

# 验证构建类型
if [[ "$BUILD_TYPE" != "debug" && "$BUILD_TYPE" != "all" ]]; then
    echo "错误: 构建类型必须是 'debug' 或 'all'"
    exit 1
fi

# 验证镜像文件存在
if [[ ! -f "$IMAGE_FILE" ]]; then
    echo "错误: 镜像文件 '$IMAGE_FILE' 不存在"
    exit 1
fi

echo "开始构建: make $BUILD_TYPE"
echo "使用镜像: $IMAGE_FILE"
if [[ "$GDB_DEBUG" == "1" ]]; then
    GDB_FLAGS="-s -S"
    echo "GDB 调试: 开启 (-s -S)"
else
    echo "GDB 调试: 关闭"
fi

NETDEV_OPTS="user,id=net"
if [[ "$NET_FORWARD" == "1" ]]; then
    NETDEV_OPTS="user,id=net,hostfwd=udp::12345-:12345"
    echo "NET 转发: 开启 (udp 12345 -> guest 12345)"
else
    echo "NET 转发: 关闭"
fi
if [[ -n "$NET_DUMP_FILE" ]]; then
    NET_DUMP_OBJ="-object filter-dump,id=netdump,netdev=net,file=${NET_DUMP_FILE}"
    echo "NET 抓包: ${NET_DUMP_FILE}"
fi

# 执行构建[1](@ref)
if make "$BUILD_TYPE"; then
    echo "构建成功!"
else
    echo "错误: 构建失败"
    exit 1
fi

# 运行QEMU
echo "启动QEMU模拟器..."
qemu-system-riscv64 -machine virt \
  -kernel kernel-qemu \
  -m 128M \
  -nographic \
  -smp 2 \
  -bios default \
  -drive file="$IMAGE_FILE",if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  -device virtio-net-device,netdev=net,bus=virtio-mmio-bus.1 \
    -netdev "$NETDEV_OPTS" \
        $NET_DUMP_OBJ \
    $GDB_FLAGS

echo "QEMU已退出"
