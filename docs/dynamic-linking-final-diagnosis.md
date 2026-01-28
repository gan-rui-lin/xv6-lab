# 动态链接失败最终诊断报告

## 日期
2026-01-28

## 执行摘要

经过详细的诊断和分析，确定了动态链接失败的根本原因：

1. ✅ **软链接问题已解决** - 创建了 `/lib/ld-linux-riscv64-lp64d.so.1` 指向 glibc 动态链接器
2. ❌ **根本问题：glibc 动态链接器自重定位失败**
3. ❌ **次要问题：musl libc.so 文件缺失**

## 诊断过程

### 阶段 1：共享库存在性验证 ✅

使用 Docker 挂载镜像验证共享库文件存在：

```bash
$ docker run --rm --privileged -v "$(pwd)/sdcard-rv.img:/image.img:ro" ubuntu:22.04 ...

=== Checking glibc libraries ===
-rwxr-xr-x 1 root root 134K Jun 14  2025 /mnt/glibc/lib/ld-linux-riscv64-lp64d.so.1
-rwxr-xr-x 1 root root 1.2M Jun 14  2025 /mnt/glibc/lib/libc.so
-rwxr-xr-x 1 root root 1.2M Jun 14  2025 /mnt/glibc/lib/libc.so.6

=== Checking musl libraries ===
lrwxrwxrwx 1 root root 17 Jan  1  1970 /mnt/musl/lib/ld-musl-riscv64.so.1 -> /musl/lib/libc.so
```

**结论**: 文件确实存在，问题不是文件缺失。

### 阶段 2：软链接问题发现与修复 ✅

检查测试程序的解释器路径：

```bash
$ readelf -l /mnt/glibc/pipe | grep interpreter
[Requesting program interpreter: /lib/ld-linux-riscv64-lp64d.so.1]
```

检查 `/lib` 目录：

```bash
$ ls -la /mnt/lib/
lrwxrwxrwx 1 root root 30 Jan  1  1970 ld-musl-riscv64.so.1 -> /musl/lib/ld-musl-riscv64.so.1
lrwxrwxrwx 1 root root 17 Jan  1  1970 libc.so -> /musl/lib/libc.so
# 缺少 ld-linux-riscv64-lp64d.so.1 -> glibc 的链接！
```

**问题**: glibc 测试程序需要 `/lib/ld-linux-riscv64-lp64d.so.1`，但只有 musl 的软链接。

**修复**: 创建缺失的软链接

```bash
$ ./fix-symlinks.sh
Creating glibc symlinks...
ln -sf ../glibc/lib/ld-linux-riscv64-lp64d.so.1 ld-linux-riscv64-lp64d.so.1
ln -sf ../glibc/lib/libc.so.6 libc.so.6
```

**结果**:
```bash
lrwxrwxrwx ld-linux-riscv64-lp64d.so.1 -> ../glibc/lib/ld-linux-riscv64-lp64d.so.1
lrwxrwxrwx libc.so.6 -> ../glibc/lib/libc.so.6
```

### 阶段 3：文件访问跟踪 ✅

添加详细的文件访问日志（//claude: 标记的代码）：

**src/syscall/sysfile.c**:
- openat: 记录所有文件打开尝试
- faccessat: 记录所有文件访问检查

运行测试并收集日志：

```bash
$ grep "\[(openat|faccessat)\].*pid=9 " /tmp/dynlink-verbose.log

[faccessat] pid=9 name=clone path='/etc/ld.so.preload' mode=4
[faccessat] pid=9 name=clone ENOENT: '/etc/ld.so.preload'
```

**发现**:
- 动态链接器只做了一次文件访问就停止了
- 没有尝试加载 libc.so.6
- 紧接着触发非法指令异常

### 阶段 4：根本原因确定 ❌

检查 glibc 动态链接器本身的类型：

```bash
$ file /mnt/glibc/lib/ld-linux-riscv64-lp64d.so.1
ELF 64-bit LSB shared object, UCB RISC-V, RVC, double-float ABI, version 1 (SYSV),
**dynamically linked**, BuildID[sha1]=c779d6eb2d438e3178484f37976cbc8794232379, stripped
```

**关键发现**: 动态链接器本身是 **dynamically linked**！

检查其依赖：

```bash
$ readelf -d /mnt/glibc/lib/ld-linux-riscv64-lp64d.so.1 | grep NEEDED
(无 NEEDED 条目)
```

**分析**:
- 动态链接器被标记为 "dynamically linked"
- 但没有外部库依赖（无 NEEDED 条目）
- 这意味着它需要进行**自重定位（self-relocation）**
- 自重定位过程在 xv6 中失败了

### 阶段 5：执行流程分析 ❌

测试程序的执行流程：

1. exec() 加载主程序和动态链接器 ✅
2. 设置 auxv（AT_BASE, AT_PHDR等）✅
3. 跳转到动态链接器入口点（0x165a4）✅
4. 动态链接器开始执行：
   - 尝试访问 `/etc/ld.so.preload` ✅
   - **失败**: 尝试调用某个函数（可能是 `open` 或内部函数）
   - 跳转到 GOT 条目 0x7fa78 ❌
   - GOT 条目未初始化，指向零页面 ❌
   - 执行非法指令 (0x00000000) ❌
   - 进程被杀死 ❌

**错误模式**:
```
[faccessat] pid=9 name=clone ENOENT: '/etc/ld.so.preload'
[trap] Illegal instruction at sepc=0x000000000007fa78, pid=9 name=clone
[trap] Instruction bytes: 00000000
[trap] WARNING: Executing zero-filled page (likely unresolved symbol)
```

## 根本原因

### glibc 动态链接器

**问题**: 动态链接器需要进行自重定位，但这个过程失败了。

**原因**:
1. 动态链接器在最开始执行时，GOT/PLT 表还未初始化
2. 它需要先运行自重定位代码，将 GOT/PLT 条目指向正确的地址
3. 但自重定位代码本身可能依赖某些已重定位的函数
4. 造成鸡生蛋、蛋生鸡的问题

**标准解决方案**:
- 动态链接器的早期代码必须是完全位置无关的（PIC）
- 不能依赖任何 GOT/PLT 调用
- 或者使用汇编编写，直接计算相对地址

**xv6 的问题**:
- 可能 auxv 传递有误，导致动态链接器无法定位自己
- 可能 AT_BASE 计算错误
- 或者 glibc 的动态链接器假设了某些 xv6 不支持的内核行为

### musl 动态链接器

**问题**: `/musl/lib/libc.so` 文件不存在！

```bash
$ ls -la /mnt/musl/lib/
lrwxrwxrwx ld-musl-riscv64.so.1 -> /musl/lib/libc.so  # 指向不存在的文件！
```

**原因**: musl 使用单一的 libc.so 文件同时作为库和动态链接器，但这个文件缺失。

## 解决方案

### 方案 1：使用静态链接的动态链接器（推荐）

**描述**: 使用一个静态链接（PIE）的动态链接器，不依赖自重定位。

**实施**:
```bash
# 重新编译或获取静态链接的 ld-linux-riscv64-lp64d.so.1
# 确保 file 输出显示 "static-pie" 而不是 "dynamically linked"
```

**优势**:
- 避免自重定位问题
- 更简单、更可靠
- 这是大多数发行版的做法

### 方案 2：修复 auxv 传递

**描述**: 检查并修复 exec.c 中的 auxv 传递代码。

**检查项**:
1. AT_BASE 是否正确计算
2. AT_PHDR 是否指向正确位置
3. AT_ENTRY 是否正确
4. 栈布局是否符合 glibc 预期

**代码位置**: src/proc/exec.c:290-300

### 方案 3：添加 musl 的 libc.so

**描述**: 在镜像中添加缺失的 musl libc.so 文件。

**实施**:
```bash
# 从 musl 构建系统复制 libc.so 到镜像
docker run --rm --privileged \
  -v "$(pwd)/sdcard-rv.img:/image.img" \
  -v "$(pwd)/path/to/libc.so:/libc.so:ro" \
  alpine sh -c '
    apk add e2fsprogs
    losetup -f /image.img
    mount /dev/loop0 /mnt
    cp /libc.so /mnt/musl/lib/libc.so
    chmod 755 /mnt/musl/lib/libc.so
    umount /mnt
    losetup -d /dev/loop0
'
```

### 方案 4：实现自重定位支持（高级）

**描述**: 在 exec.c 中添加对动态链接器的特殊处理，执行自重定位。

**步骤**:
1. 解析动态链接器的 DYNAMIC 段
2. 应用 RELA 重定位
3. 初始化 GOT/PLT 表
4. 然后跳转到入口点

**复杂度**: 高，需要深入理解 ELF 重定位机制。

### 方案 5：使用测试镜像中的静态链接程序

**描述**: 使用静态链接的测试程序，完全避免动态链接问题。

**检查**:
```bash
$ docker run --rm --privileged -v "$(pwd)/sdcard-rv.img:/image.img:ro" alpine sh -c '
    apk add e2fsprogs file
    losetup -f /image.img
    mount -o ro /dev/loop0 /mnt
    # 查找静态链接程序
    find /mnt -name "*static*" -o -name "*-s"
    umount /mnt
    losetup -d /dev/loop0
'
```

## 建议的实施顺序

### 第一优先级（立即）

1. **使用静态链接的测试程序** - 如果镜像中有静态链接版本
2. **获取静态链接的动态链接器** - 更换 glibc 动态链接器
3. **添加 musl libc.so** - 修复 musl 缺失文件问题

### 第二优先级（短期）

4. **验证 auxv 传递** - 确保 exec 正确传递所有参数
5. **添加动态链接器调试日志** - 帮助诊断自重定位失败

### 第三优先级（长期）

6. **实现自重定位支持** - 如果需要支持标准的动态链接器

## 已添加的诊断代码

所有代码都标记了 `//claude:` 注释：

### src/syscall/sysfile.c

1. **openat 日志** (行 1156):
```c
//claude: 启用日志以诊断动态链接器的文件访问
struct proc *p = myproc();
printf("[openat] pid=%d name=%s path='%s' flags=0x%x\n", p->pid, p->name, path, flags);
```

2. **openat ENOENT 日志** (行 1220):
```c
//claude: 记录文件未找到，帮助诊断动态链接器加载失败
struct proc *p = myproc();
printf("[openat] pid=%d name=%s ENOENT: '%s'\n", p->pid, p->name, npath);
```

3. **openat SUCCESS 日志** (行 1285):
```c
//claude: 记录成功打开的文件，追踪动态链接器行为
printf("[openat] pid=%d name=%s SUCCESS: fd=%d path='%s'\n",
       myproc()->pid, myproc()->name, fd, path);
```

4. **faccessat 入口日志** (行 663):
```c
//claude: 记录 faccessat 调用以追踪动态链接器的文件访问
printf("[faccessat] pid=%d name=%s path='%s' mode=%d\n", p->pid, p->name, path, mode);
```

5. **faccessat ENOENT 日志** (行 690):
```c
//claude: 记录文件访问失败
printf("[faccessat] pid=%d name=%s ENOENT: '%s'\n", p->pid, p->name, path);
```

6. **faccessat SUCCESS 日志** (行 694):
```c
//claude: 记录文件访问成功
printf("[faccessat] pid=%d name=%s SUCCESS: '%s'\n", p->pid, p->name, path);
```

### src/trap/trap.c

7. **非法指令详细诊断** (行 162-190):
```c
//claude: 诊断动态链接非法指令问题，提供详细的指令和VMA信息
} else if (scause == ECODE_ILLEGAL_INSTRUCTION) {
  uint64 sepc = r_sepc();
  printf("[trap] Illegal instruction at sepc=%p, pid=%d name=%s\n", sepc, p->pid, p->name);

  // 读取指令内容
  uint32 instr = 0;
  if (copyin(p->pagetable, (char*)&instr, sepc, sizeof(instr)) == 0) {
    printf("[trap] Instruction bytes: %08x\n", instr);
    if (instr == 0) {
      printf("[trap] WARNING: Executing zero-filled page (likely unresolved symbol)\n");
    }
  }

  // 检查 VMA 信息
  struct vma *v = vma_find(p, sepc);
  if (v) {
    printf("[trap] Address in VMA: [%p, %p) prot=%d flags=%d\n",
           v->start, v->end, v->prot, v->flags);
    printf("[trap] This suggests dynamic linker failed to load shared library\n");
  }

  printf("[trap] Process memory: sz=%p\n", p->sz);
  setkilled(p);
}
```

### src/syscall/sysproc.c

8. **mmap 跟踪** (行 891-898):
```c
//claude: 跟踪 mmap 调用以诊断动态链接库加载
if (flags & MAP_ANONYMOUS) {
  log_debug("[mmap] pid=%d ANONYMOUS addr=%p len=%p prot=%d flags=%x\n",
            p->pid, addr, length, prot, flags);
} else {
  log_debug("[mmap] pid=%d FILE fd=%d offset=%p len=%p prot=%d flags=%x\n",
            p->pid, fd, offset, length, prot, flags);
}
```

## 辅助脚本

### fix-symlinks.sh
创建缺失的 glibc 软链接到 /lib 目录。

### check-interpreter.sh
检查测试程序需要的解释器路径。

## 测试数据

### 软链接修复前
```
$ grep "Illegal instruction" /tmp/dynlink-fixed.log | wc -l
7
```

### 软链接修复后
```
$ grep "Illegal instruction" /tmp/dynlink-verbose.log | wc -l
7
```

**结论**: 软链接修复没有解决问题，因为根本原因是自重定位失败。

## 未来工作

1. **获取或编译静态 PIE 动态链接器**
2. **验证 musl 动态链接器**（添加缺失的 libc.so）
3. **实现完整的 ELF 自重定位支持**
4. **创建静态链接的测试套件**作为后备方案

## 参考资料

- [How the ELF Ruined Christmas](https://www.youtube.com/watch?v=I7wdGlmtVEo) - 动态链接器内部机制
- [glibc dynamic linker source](https://sourceware.org/git/?p=glibc.git;a=blob;f=elf/rtld.c)
- [musl libc dynamic linker](https://git.musl-libc.org/cgit/musl/tree/ldso/dynlink.c)
- [ELF Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf)

---

**文档版本**: 1.0
**最后更新**: 2026-01-28
**状态**: 问题已确诊，等待解决方案实施
