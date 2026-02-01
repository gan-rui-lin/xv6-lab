# musl 编译工具链安装和镜像构建指南

## 方法一：使用 Docker + Colima（推荐）

### 1. 启动 Colima

Colima 是 macOS 上的轻量级 Docker 替代品。

```bash
# 如果还没安装 Colima，先安装
brew install colima

# 启动 Colima（使用默认配置）
colima start

# 或者使用更多资源启动
colima start --cpu 4 --memory 8 --disk 60

# 检查状态
colima status

# 验证 Docker 是否可用
docker ps
```

**如果 Colima 启动失败**，尝试以下步骤：

```bash
# 删除旧的配置
colima delete

# 重新启动
colima start --vm-type=qemu --cpu 4 --memory 8

# 或者使用 VZ（仅 macOS 13+ 支持）
colima start --vm-type=vz --cpu 4 --memory 8
```

### 2. 使用 Docker 容器编译测试

启动 Colima 后，使用包含 musl 工具链的 Docker 镜像：

```bash
cd /Users/mac/Desktop/project/xv6-lab/oscomp-midwest-onsitefinal-main

# 使用预构建的 RISC-V musl 工具链镜像
docker run --rm -v "$(pwd)":/work -w /work \
    muslcc/x86_64:riscv64-linux-musl \
    make ARCH=rv FS=fat32 all

# 构建镜像（需要 sudo，会挂载本地文件系统）
# 注意：这个步骤需要在 macOS 上运行，不能在容器内
make ARCH=rv FS=fat32 image
```

**可用的 musl Docker 镜像**:
- `muslcc/x86_64:riscv64-linux-musl` - RISC-V 64 位
- `muslcc/x86_64:loongarch64-linux-musl` - LoongArch 64 位

---

## 方法二：直接安装 musl-cross-make（本地安装）

如果不想使用 Docker，可以从源码构建工具链。

### 1. 安装依赖

```bash
brew install wget make gcc gmp mpfr libmpc
```

### 2. 下载并构建 musl-cross-make

```bash
# 创建工作目录
mkdir -p ~/musl-cross
cd ~/musl-cross

# 克隆 musl-cross-make
git clone https://github.com/richfelker/musl-cross-make.git
cd musl-cross-make

# 配置 RISC-V 64 位工具链
cat > config.mak << 'EOF'
TARGET = riscv64-linux-musl
OUTPUT = /usr/local
COMMON_CONFIG += CFLAGS="-g0 -O2" CXXFLAGS="-g0 -O2" LDFLAGS="-s"
GCC_CONFIG += --disable-libquadmath --disable-decimal-float
GCC_CONFIG += --disable-multilib
EOF

# 编译（耗时约 30-60 分钟）
make -j$(sysctl -n hw.ncpu)

# 安装（需要 sudo）
sudo make install

# 验证安装
riscv64-linux-musl-gcc --version
```

### 3. 编译测试

```bash
cd /Users/mac/Desktop/project/xv6-lab/oscomp-midwest-onsitefinal-main

# 编译所有测试
make ARCH=rv FS=fat32 all

# 查看生成的二进制文件
ls -lh */*-*
```

---

## 方法三：使用预构建的二进制包（最快）

### 选项 A: 使用 Homebrew Tap

```bash
# 添加第三方 tap
brew tap messense/macos-cross-toolchains

# 安装 RISC-V musl 工具链
brew install riscv64-unknown-linux-musl

# 验证
riscv64-unknown-linux-musl-gcc --version
```

### 选项 B: 下载预编译包

从以下网站下载预编译的工具链：
- https://musl.cc/ (推荐，官方预编译版本)
- https://github.com/richfelker/musl-cross-make/releases

```bash
# 示例：下载并安装 musl.cc 的预编译包
cd ~/Downloads
wget https://musl.cc/riscv64-linux-musl-cross.tgz
tar xf riscv64-linux-musl-cross.tgz
sudo mv riscv64-linux-musl-cross /opt/

# 添加到 PATH
echo 'export PATH="/opt/riscv64-linux-musl-cross/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc

# 验证
riscv64-linux-musl-gcc --version
```

---

## 构建测试镜像

### 1. 编译测试二进制

```bash
cd /Users/mac/Desktop/project/xv6-lab/oscomp-midwest-onsitefinal-main

# 编译所有测试（包括新的 VMA 测试）
make ARCH=rv FS=fat32 all

# 验证生成的文件
ls -lh close_range/cr-*
ls -lh eventfd2/ef2-*
ls -lh waitid/wi-*
ls -lh vma/vma-*
```

### 2. 构建 FAT32 镜像

```bash
# 创建镜像并复制测试文件
make ARCH=rv FS=fat32 image

# 查看生成的镜像
ls -lh testcase-rv-fat32.img
```

**注意事项**:
- `make image` 需要 sudo 权限（挂载/卸载镜像）
- macOS 需要安装 `dosfstools`：`brew install dosfstools`
- 如果 `mkfs.vfat` 不可用，安装：`brew install dosfstools`

### 3. 替换现有镜像

```bash
# 备份旧镜像
cp testcase-rv-fat32.img testcase-rv-fat32.img.bak

# 新镜像已生成，无需额外操作
```

---

## 运行测试

### 1. 更新 initcode.c

修改 `/Users/mac/Desktop/project/xv6-lab/user/initcode.c` 添加 VMA 测试：

```c
test_("ef2-1");
test_("ef2-2");
test_("ef2-3");
test_("ef2-4");
test_("ef2-5");

// 添加 VMA 测试
test_("vma-1");
test_("vma-2");
test_("vma-3");
test_("vma-4");
test_("vma-5");
```

### 2. 重新编译 xv6

```bash
cd /Users/mac/Desktop/project/xv6-lab
make clean && make all
```

### 3. 运行测试

```bash
./run-sdcard-rv.sh
```

---

## 故障排除

### Colima 启动失败

```bash
# 查看日志
colima logs

# 尝试不同的 VM 类型
colima start --vm-type=qemu    # QEMU（兼容性最好）
colima start --vm-type=vz      # VZ（macOS 13+，更快）

# 清理并重试
colima delete
rm -rf ~/.colima
colima start
```

### Docker 连接失败

```bash
# 设置 Docker 上下文
docker context use colima

# 或者手动设置环境变量
export DOCKER_HOST="unix://$HOME/.colima/default/docker.sock"
```

### 编译错误

```bash
# 检查编译器版本
riscv64-linux-musl-gcc --version

# 清理并重试
cd oscomp-midwest-onsitefinal-main
make clean
make ARCH=rv FS=fat32 all
```

### 镜像挂载失败

```bash
# macOS 需要 dosfstools
brew install dosfstools

# 检查 mkfs.vfat 是否可用
which mkfs.vfat

# 如果不可用，可能需要创建符号链接
sudo ln -sf /opt/homebrew/bin/mkfs.fat /usr/local/bin/mkfs.vfat
```

---

## 快速开始（推荐流程）

```bash
# 1. 启动 Colima
colima start

# 2. 使用 Docker 编译测试
cd /Users/mac/Desktop/project/xv6-lab/oscomp-midwest-onsitefinal-main
docker run --rm -v "$(pwd)":/work -w /work \
    muslcc/x86_64:riscv64-linux-musl \
    sh -c "make ARCH=rv FS=fat32 clean && make ARCH=rv FS=fat32 all"

# 3. 安装 dosfstools（如果还没安装）
brew install dosfstools

# 4. 构建镜像
make ARCH=rv FS=fat32 image

# 5. 复制到 xv6-lab 根目录
cd ..
# 镜像已经在正确位置

# 6. 运行测试
./run-sdcard-rv.sh
```

---

## 参考链接

- Colima: https://github.com/abiosoft/colima
- musl-cross-make: https://github.com/richfelker/musl-cross-make
- musl.cc: https://musl.cc/
- Docker Hub musl 镜像: https://hub.docker.com/r/muslcc/x86_64
