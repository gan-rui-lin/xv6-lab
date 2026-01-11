# xv6-lab Docker 开发环境

## Docker 配置文件

| 文件 | 说明 |
|------|------|
| `Dockerfile` | Docker 镜像定义，包含 RISC-V 工具链和 QEMU |
| `docker-compose.yml` | Docker Compose 配置，方便启动和管理容器 |
| `.dockerignore` | 构建镜像时忽略的文件 |
| `docker-run.sh` | 快速启动脚本 |

## 使用方法

### 方式一：使用 docker-compose（推荐）
```bash
# 构建并启动容器
docker-compose up -d

# 进入容器
docker-compose exec xv6 bash

# 在容器内编译运行
make clean && make all
bash run.sh
```

### 方式二：使用快速脚本
```bash
# 进入交互式 shell
./docker-run.sh

# 直接运行命令
./docker-run.sh make
./docker-run.sh bash run.sh
```

### 方式三：手动 Docker 命令
```bash
# 构建镜像
docker build -t xv6-lab-dev .

# 运行容器
docker run -it --rm -v $(pwd):/xv6-lab -w /xv6-lab xv6-lab-dev bash
```

