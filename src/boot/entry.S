        # 入口说明：
        # - 在使用 OpenSBI (即 QEMU 的 -bios default) 时，固件会以 S 模式跳转到内核入口，
        #   并按约定将 a0=hartid, a1=FDT 地址传入。
        # - 在这种模式下，不能访问 M 模式的 CSR；因此入口不再读取 mhartid CSR，
        #   而是使用 a0 中的 hartid 完成 bss 清零与栈初始化，然后直接调用 main()。

        .section .text
        .global _entry
        _entry:
                # a0 保存当前硬件线程ID (由 OpenSBI 传入)。
                # 仅在 hart 0 上清零 bss 段。
                beqz a0, 1f          # 如果 a0 == 0 (hart0)，跳到清零逻辑
                j 2f                 # 其他 hart 跳过 bss 清零

        1:      # 清零bss段（只在 hart0 执行一次）
                la a2, _bss_start    # 加载bss段起始地址
                la a3, _bss_end      # 加载bss段结束地址
        0:      sd zero, (a2)        # 存储0到当前地址
                addi a2, a2, 8       # 地址加8(64位=8字节)
                bltu a2, a3, 0b      # 如果还没到结束地址，继续循环

        2:      # 设置每个CPU的栈空间
                # stack0 声明在 start.c 中，每个 CPU 分配 4096 字节的栈空间。
                # 计算公式: sp = stack0基地址 + ((hartid + 1) * 4096)
                la sp, stack0        # 加载 stack0 的基地址到栈指针 sp
                li t0, 1024*4        # 4096 (每个CPU的栈大小)
                addi t1, a0, 1       # hartid + 1 (初始化到该CPU栈的栈顶)
                mul t0, t0, t1       # t0 = 4096 * (hartid+1)
                add sp, sp, t0       # 设置当前CPU的栈顶指针

                # 设置 tp 为当前 hartid，供 C 代码中的 cpuid() 使用
                mv  t2, a0
                mv  tp, t2

                # 直接跳转到 C 入口 main()（S 模式下不再调用含 M 模式 CSR 的 start()）
                call main

        spin:
                # 如果 main() 返回，则进入无限循环（正常情况下不会执行到这里）
                j spin

#         # QEMU通过 -kernel 参数将内核加载到物理地址0x80000000
#         # 并使每个硬件线程(hart，即CPU核心)跳转到此处执行
#         # 链接脚本kernel.ld确保以下代码被放置在0x80000000地址
# .section .text
# .global _entry
# _entry:
#         # 只让hart 0执行清零bss段的操作
#         csrr a1, mhartid     # 读取当前硬件线程ID到a1
#         bnez a1, 2f         # 如果不是hart 0，跳转到标签2

#         # 清零bss段
#         la a2, _bss_start   # 加载bss段起始地址
#         la a3, _bss_end     # 加载bss段结束地址
# 1:      # 循环清零
#         sd zero, (a2)       # 存储0到当前地址
#         addi a2, a2, 8      # 地址加8(64位=8字节)
#         bltu a2, a3, 1b     # 如果还没到结束地址，继续循环

# 2:      # 为C语言代码设置栈空间
#         # stack0声明在start.c中，每个CPU分配4096字节的栈空间
#         # 计算公式: sp = stack0基地址 + (硬件线程ID * 4096)
#         la sp, stack0        # 加载stack0的基地址到栈指针sp
#         li a0, 1024*4       # 加载4096(每个CPU的栈大小)到a0
#         csrr a1, mhartid    # 读取当前硬件线程ID到a1
#         addi a1, a1, 1      # hartid+1(栈指针初始化到该CPU栈的栈顶)
#         mul a0, a0, a1      # 计算偏移量: 4096 * (hartid+1)
#         add sp, sp, a0      # 设置当前CPU的栈顶指针
#         # 跳转到start.c中的start()函数继续初始化
#         call start
# spin:
#         # 如果start()函数返回，则进入无限循环(正常情况下不会执行到这里)
#         j spin