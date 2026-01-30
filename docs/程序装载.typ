
= 程序装载

RuOS 支持 ELF 可执行文件的装载与*动态链接*，完善了*用户栈参数传递*与解释器装载等细节，提升了与 Linux 用户态程序的兼容性。

== ELF 文件格式

ELF（Executable and Linkable Format）是一种通用的文件格式，用于存储可执行文件、目标代码和共享库。ELF 文件由多个部分组成，主要包括：

- ELF 头（ELF Header）：包含文件的基本信息，如类型、架构、入口点地址等。
- 程序头表（Program Header Table）：描述了程序的各个段（segments），如代码段、数据段等，以及它们在内存中的加载地址和权限。
- 节头表（Section Header Table）：描述了文件的各个节（sections），如符号表、字符串表等。
- 段（Segments）：用于运行时加载的部分，如可执行代码段、数据段等。
- 节（Sections）：用于链接和调试的部分，如符号表、重定位信息等。

我们选取2024年初赛SD卡上一个ELF 可执行文件（"/musl/ltp/testcases/bin/waitpid01"）作为示例:

在终端中执行：

```bash
readelf -h /musl/ltp/testcases/bin/waitpid01
```

输出结果如下：

```
 Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              DYN (Position-Independent Executable file)
  Machine:                           RISC-V
  Version:                           0x1
  Entry point address:               0x6684
  Start of program headers:          64 (bytes into file)
  Start of section headers:          798296 (bytes into file)
  Flags:                             0x5, RVC, double-float ABI
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         7
  Size of section headers:           64 (bytes)
  Number of section headers:         32
  Section header string table index: 31
```

可以看到 ELF 文件的入口点地址为 `0x6684`，表示程序开始执行的地址。程序头表从文件偏移 `64` 字节处开始，共有 `7` 个程序头，每个程序头大小为 `56` 字节。节头表从文件偏移 `798296` 字节处开始，共有 `32` 个节，每个节大小为 `64` 字节。

在所有段中，我们这里重点关注以下几个段：

- `.text` 段：包含程序的可执行代码。
- `.data` 段：包含已初始化的全局变量和静态变量
- `.bss` 段：包含未初始化的全局变量和静态变量。
- `.rodata` 段：包含只读数据，如字符串常量等。

在动态链接的 ELF 文件中，还会包含一个特殊的段 ：`PT_INTERP` 段。它指定动态链接器的路径，用于在程序加载时进行动态链接。

我们在终端里面执行以下命令查看 `PT_INTERP` 段的信息：

```bash
readelf -l /musl/ltp/testcases/bin/waitpid01 | grep ".interp"
```
`-l` 选项用于显示程序头表，`grep ".interp"` 用于过滤出包含 `.interp` 段的信息。

输出结果如下：
```[Requesting program interpreter: /lib/ld-musl-riscv64.so.1]
01     .interp
02     .interp .hash .gnu.hash .dynsym .dynstr .rela.dyn .rela.plt .plt .text .rodata .eh_frame
```

可以看到 `PT_INTERP` 段指定的动态链接器路径为 `/lib/ld-musl-riscv64.so.1`。这意味着在加载该 ELF 文件时，系统会使用该动态链接器来处理动态链接的需求。

至于 `.dynamic` 段和 `.rela.dyn` 段，它们也在动态链接中起着重要作用，但是相关工作由动态链接器负责处理，因此在这里我们不做过多展开。

== 用户栈的初始化与参数传递

用户栈的初始化与参数传递需要遵循相关 ABI 规范。在 RISCV64 架构下，没有严格规定`ENVP` 和 `AUXV` 的压栈方式(#link(
  "https://github.com/riscv-non-isa/riscv-elf-psabi-doc/releases/download/v1.0/riscv-abi.pdf",
)[riscv-abi.pdf 见此处])，但通常为了保持跨架构的ABI兼容性，我们参考了 Linux x86_64 的 ABI 规范(#link("https://cs61.seas.harvard.edu/site/2025/pdf/x86-64-abi-20210928.pdf")[x86-64-abi-20210928.pdf])。

在 RuOS 中，我们按照以下顺序将参数压入用户栈：

- 将 `argv` 与 `envp` 的字符串内容依次拷贝到用户栈高地址处，并保持 16 字节对齐；
- 记录每个字符串的地址，组成 `argv[]`/`envp[]` 指针数组；
- 在指针数组之后依次放置 `auxv`（键值对形式）并以 `AT_NULL` 结束；
- 最后把 `argc` 放在栈顶，保证栈布局为 `argc | argv[] | envp[] | auxv[]`。

从高地址到低地址的实际栈布局可理解为：

- `argv` 字符串区、`envp` 字符串区（逐个写入，并对齐到 16 字节）；
- `auxv` 键值对数组（`AT_PHDR/...`，以 `AT_NULL` 结束）；
- `argv[]` 指针数组（以 `NULL` 结尾）；
- `envp[]` 指针数组（以 `NULL` 结尾）；
- `argc`。

其中 `auxv` 用于向用户态运行时传递 ELF 关键元信息：`AT_PHDR`/`AT_PHENT`/`AT_PHNUM` 指向程序头表，`AT_PAGESZ` 表示页大小，`AT_ENTRY` 为入口地址；若存在动态链接器，还会额外提供 `AT_BASE`（解释器加载基址）。
栈顶对齐保证 `sp` 满足 RISC‑V ABI 的 16 字节对齐要求。最后在 `exec` 中设置 `a0=argc`、`a1=argv`、`a2=envp`，使用户态入口可以按约定读取参数。

RuOS 用户栈布局如 @ruos-user-stack-layout 所示：

#figure(image("stack_layout.png", height: 70%), caption: "RuOS 用户栈布局图") <ruos-user-stack-layout>

