#!/bin/bash
# xv6-lab Docker 快速启动脚本
# 用法: ./docker-run.sh [命令]
# 示例:
#   ./docker-run.sh          # 进入交互式 shell
#   ./docker-run.sh make     # 运行 make
#   ./docker-run.sh bash run.sh  # 编译并运行

set -e

IMAGE_NAME="xv6-lab-dev"
CONTAINER_NAME="xv6-lab-container"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查 Docker 是否安装
if ! command -v docker &> /dev/null; then
    echo_error "Docker 未安装，请先安装 Docker"
    exit 1
fi

# 检查镜像是否存在，不存在则构建
if [[ "$(docker images -q $IMAGE_NAME 2> /dev/null)" == "" ]]; then
    echo_info "镜像不存在，正在构建..."
    docker build -t $IMAGE_NAME .
fi

# 检查是否有正在运行的容器
if docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    # 容器存在
    if docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
        # 容器正在运行，直接进入
        echo_info "容器正在运行，进入容器..."
    else
        # 容器已停止，启动它
        echo_info "启动已停止的容器..."
        docker start $CONTAINER_NAME
    fi
else
    # 创建新容器
    echo_info "创建新容器..."
    docker run -d \
        --name $CONTAINER_NAME \
        -v "$(pwd)":/xv6-lab \
        -w /xv6-lab \
        --network host \
        --privileged \
        -it \
        $IMAGE_NAME \
        tail -f /dev/null
fi

# 执行命令
if [ $# -eq 0 ]; then
    # 没有参数，进入交互式 shell
    echo_info "进入交互式 shell..."
    docker exec -it $CONTAINER_NAME /bin/bash
else
    # 有参数，执行指定命令
    echo_info "执行命令: $@"
    docker exec -it $CONTAINER_NAME "$@"
fi
