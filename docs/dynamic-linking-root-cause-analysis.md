# 动态链接失败根本原因分析

## 日期
2026-01-28

## 重大发现：共享库文件存在！

通过 Docker 挂载镜像检查，发现共享库文件**确实存在**：

```
=== Checking glibc libraries ===
-rwxr-xr-x 1 root root 134K Jun 14  2025 /mnt/glibc/lib/ld-linux-riscv64-lp64d.so.1
-rwxr-xr-x 1 root root 1.2M Jun 14  2025 /mnt/glibc/lib/libc.so
-rwxr-xr-x 1 root root 1.2M Jun 14  2025 /mnt/glibc/lib/libc.so.6

=== Checking musl libraries ===
lrwxrwxrwx 1 root root 17 Jan  1  1970 /mnt/musl/lib/ld-musl-riscv64.so.1 -> /musl/lib/libc.so
```

**结论**: 问题不是共享库缺失，而是**动态链接器运行时失败**。

## 重新分析问题

### 执行流程确认

1. ✅ **exec 加载程序**: 正常
   ```
   [exec] ./clone: sz=0x0000000000031000 entry=0x0000000000001000
          (interp=1 interp_entry=0x00000000000165a4)
   ```

2. ✅ **动态链接器被加载**: 正常
   - 识别到 interp=1
   - 入口点: 0x165a4

3. ✅ **跳转到动态链接器**: 正常
   - trapframe->epc 被设置为 interp_entry

4. ❌ **动态链接器运行失败**: **这里出问题了**
   - 动态链接器启动后应该：
     - 打开 /glibc/lib/libc.so.6
     - 加载到内存（通过 mmap）
     - 解析符号
     - 重定位 GOT/PLT 表
     - 跳转到主程序入口

5. ❌ **符号未解析**: 函数指针指向错误地址
   ```
   [trap] Illegal instruction at sepc=0x000000000007fa78
   [trap] Instruction bytes: 00000000
   ```

### 为什么动态链接器失败了？

#### 可能原因 1: 系统调用失败（最可能）

**假设**: 动态链接器尝试打开库文件时失败

**验证方法**: 检查是否有 open/openat 系统调用的 ENOENT 错误

从之前的日志中我们看到：
```
[syscall] pid=31 name=pipe num=48 (SYS_faccessat)
  args=[0xffffffffffffff9c,0x000000000001fb90,0x0000000000000004,
        0x0000000000000000,0x0000000000025108,0x000000000001fb90],
  ret = -2
```

`ret = -2` 是 `-ENOENT`，说明文件访问失败！

**问题**: 为什么文件明明存在，但 faccessat 返回 ENOENT？

#### 可能原因 2: 路径问题

**绝对路径 vs 相对路径**

动态链接器可能在错误的目录搜索库：
- 如果当前工作目录不是根目录
- 相对路径 `/glibc/lib/libc.so.6` 可能找不到

**解决方案**: 确保测试程序的工作目录正确

#### 可能原因 3: 符号链接问题

musl 的动态链接器是软链接：
```
lrwxrwxrwx 1 root root 17 Jan  1  1970 /mnt/musl/lib/ld-musl-riscv64.so.1 -> /musl/lib/libc.so
```

**问题**: xv6 是否支持软链接的解析？

如果 exec 试图加载 `/musl/lib/ld-musl-riscv64.so.1`，但 xv6 不能解析软链接，就会失败。

#### 可能原因 4: 文件描述符限制

动态链接器需要：
1. 打开动态链接器本身
2. 打开主程序
3. 打开 libc.so
4. 可能打开其他库

如果文件描述符不够，可能失败。

#### 可能原因 5: AT_BASE 传递错误

动态链接器需要通过 auxv 获取自己的加载基址 (AT_BASE)。

检查 exec.c 中的实现：
```c
if(have_interp){
  ustack[auxv_idx++] = AT_BASE;
  ustack[auxv_idx++] = interp_base;
}
```

如果 interp_base 计算错误，动态链接器可能无法正确重定位自己。

## 诊断策略

### 第一步：检查文件访问失败的具体路径

需要添加日志，输出 faccessat/openat 尝试访问的完整路径。

**修改 sysfile.c**:
```c
//claude: 输出文件访问的完整路径以诊断动态链接问题
sys_faccessat(void) {
  // ... 获取参数 ...
  char path_buf[MAXPATH];
  if (copyinstr(p->pagetable, path_buf, path, MAXPATH) < 0) {
    return -EFAULT;
  }
  printf("[faccessat] pid=%d path='%s' mode=%d => ", p->pid, path_buf, mode);
  // ... 执行逻辑 ...
  printf("ret=%d\n", ret);
  return ret;
}
```

### 第二步：跟踪所有 open/openat 调用

看动态链接器尝试打开哪些文件，哪些成功，哪些失败。

### 第三步：检查工作目录

```c
//claude: 在 exec 中输出当前工作目录
printf("[exec] cwd=%s\n", p->cwd->path);
```

### 第四步：验证软链接支持

尝试直接加载 `/musl/lib/libc.so` 而不是通过软链接。

### 第五步：验证 AT_BASE

在动态链接器入口添加调试输出，验证 auxv 传递是否正确。

## 最可能的问题

根据日志 `ret = -2` (ENOENT)，**最可能的原因是路径问题**：

### 场景 1: 当前工作目录问题

- 测试程序从 `/glibc/` 目录执行
- 当前工作目录 (cwd) = `/glibc/`
- 动态链接器尝试打开相对路径的库
- 例如，`open("lib/libc.so.6")` 实际打开 `/glibc/lib/libc.so.6`
- 但库实际在 `/glibc/lib/libc.so.6`... 等等，这应该是对的

让我重新思考...

### 场景 2: 库搜索路径

动态链接器的默认搜索路径可能不包含 `/glibc/lib`。

标准搜索路径：
- /lib
- /usr/lib
- /lib64
- /usr/lib64

但我们的库在：
- /glibc/lib
- /musl/lib

**解决方案**:
1. 在镜像中创建软链接 `/lib -> /glibc/lib`
2. 或设置 LD_LIBRARY_PATH 环境变量
3. 或修改动态链接器的搜索路径

### 场景 3: RPATH/RUNPATH

测试程序编译时可能设置了 RPATH，指定了错误的库路径。

检查方法：
```bash
readelf -d /path/to/test_program | grep RPATH
readelf -d /path/to/test_program | grep RUNPATH
```

## 验证脚本

```bash
# 检查测试程序的动态链接配置
docker run --rm --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img:ro" \
  ubuntu:22.04 bash -c '
    apt-get update && apt-get install -y e2fsprogs binutils > /dev/null 2>&1 && \
    losetup -f /image.img && \
    mount -o ro /dev/loop0 /mnt && \

    echo "=== Test program dynamic section ===" && \
    readelf -d /mnt/glibc/clone 2>/dev/null || echo "File not found" && \

    echo "" && \
    echo "=== Interpreter ===" && \
    readelf -l /mnt/glibc/clone 2>/dev/null | grep interpreter || echo "No interpreter" && \

    umount /mnt && losetup -d /dev/loop0
'
```

## 快速验证：创建 /lib 软链接

如果是搜索路径问题，创建软链接应该能解决：

```bash
docker run --rm --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img" \
  ubuntu:22.04 bash -c '
    apt-get update && apt-get install -y e2fsprogs > /dev/null 2>&1 && \
    losetup -f /image.img && \
    mount /dev/loop0 /mnt && \

    echo "Creating /lib symlink..." && \
    cd /mnt && \
    ln -sf glibc/lib lib && \
    ls -la /mnt/lib && \

    umount /mnt && \
    losetup -d /dev/loop0
'
```

然后重新测试！

## 总结

**问题已确认**：
- ✅ 共享库文件存在
- ✅ 动态链接器被加载和启动
- ❌ 动态链接器无法找到/加载共享库

**最可能原因**：
1. 库搜索路径不包含 `/glibc/lib` 和 `/musl/lib`
2. 需要创建 `/lib` 软链接指向实际的库目录

**下一步行动**：
1. 在镜像中创建 `/lib` 软链接
2. 添加文件访问日志确认路径
3. 重新测试验证
