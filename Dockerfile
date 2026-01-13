# xv6-lab RISC-V 开发环境
# 基于 Ubuntu 22.04，包含 RISC-V 工具链和 QEMU

FROM ubuntu:22.04

LABEL maintainer="xv6-lab team"
LABEL description="Development environment for xv6-lab RISC-V OS"
LABEL version="1.0"

# 避免交互式安装提示
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

# 更新包管理器并安装必要工具
RUN apt-get update && apt-get install -y \
    # 基础工具
    build-essential \
    git \
    wget \
    curl \
    vim \
    # RISC-V 工具链
    gcc-riscv64-unknown-elf \
    binutils-riscv64-unknown-elf \
    # QEMU RISC-V 模拟器
    qemu-system-misc \
    # 其他开发工具
    gdb-multiarch \
    make \
    python3 \
    # 清理缓存
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# 如果 riscv64-unknown-elf 工具链不可用，安装 riscv64-linux-gnu 作为备选
RUN if ! command -v riscv64-unknown-elf-gcc &> /dev/null; then \
        apt-get update && apt-get install -y \
        gcc-riscv64-linux-gnu \
        binutils-riscv64-linux-gnu \
        && apt-get clean \
        && rm -rf /var/lib/apt/lists/*; \
    fi

# 创建工作目录
WORKDIR /xv6-lab

# 设置环境变量
ENV PATH="/usr/bin:${PATH}"

# 默认使用 bash
SHELL ["/bin/bash", "-c"]

# 容器启动时进入 bash
CMD ["/bin/bash"]
