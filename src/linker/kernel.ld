OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
    . = 0x80000000;

    .text : {
        *(.text .text.*)
        . = ALIGN(0x1000); 

        _trampoline = .;
        *(trampsec)
        . = ALIGN(0x1000);
        /* ASSERT(. - _trampoline == 0x0, "error: trampoline larger than one page"); */
        PROVIDE(etext = .);
    }


    .rodata : {
        . = ALIGN(0x1000);           /* 增加对齐要求确保段边界对齐 */
        *(.srodata .srodata.*)
        . = ALIGN(16);
        *(.rodata .rodata.*)
        . = ALIGN(0x1000);           /* 确保下一个段从新的页开始 */
    }

    .data : {
        . = ALIGN(0x1000);           /* 页对齐确保段正确对齐 */
        *(.sdata .sdata.*)
        . = ALIGN(16);
        *(.data .data.*)
        . = ALIGN(0x1000);           /* 确保下一个段从新的页开始 */
    }

    .bss : {
        . = ALIGN(16);
        _bss_start = .;     /* 记录bss段的开始地址 */
        *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
        . = ALIGN(16);
        *(.bss .bss.*)
        _bss_end = .;       /* 记录bss段的结束地址 */
    }


    /* 预留堆区空间 */
    . = ALIGN(16);
    __heap_start = .;
    . = . + 0x10000; /* 64KB 堆 */
    __heap_end = .;

    PROVIDE(end = .);
}


/*
    只有赋值语句（如 . = ALIGN(0x1000);）才需要分号来结束。
*/