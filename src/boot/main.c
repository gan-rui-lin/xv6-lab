#include "defs.h"
#include "riscv.h"
#include "types.h"

volatile static int started = 0;

static uint32 *mem_pages[1024]; // 尝试分配 1024 页内存

volatile static int over_hart0 = 0, over_hart_other = 0;

void main()
{
    // uart_puts("\nhere!\n");
    if (cpuid() == 0)
    {
        // 只有 hart0 执行系统初始化
        // 而其它 CPU 等待
        consoleinit(); // 初始化控制台, 目前只初始化 uart

        printfinit(); // 初始化printf功能

        kinit(); // 物理页面分配器初始化

        uart_puts("\nxv6 is booting!\n");
        plicinit();           // 设置中断控制器（仅一次）
        plicinithart();       // 每个核都要去向 PLIC 请求设备
        __sync_synchronize(); // 确保代码不乱序执行
        started = 1;

        for (int i = 0; i < 512; i++)
        {
            mem_pages[i] = (uint32 *)kalloc();
            if (mem_pages[i] == 0)
            {
                printf("hart0: kalloc failed at page %d\n", i);
                break;
            }
            // 覆盖垃圾值
            memset(mem_pages[i], 1, PGSIZE);
            printf("mem_pages = %p, data = %d\n", mem_pages[i], mem_pages[i][0]);
        }

        printf("cpu %d alloc over\n", cpuid());

        over_hart0 = 1;

        while (over_hart0 == 0 || over_hart_other == 0)
            ;

        for (int i = 0; i < 512; i++)
        {
            kfree((void *)mem_pages[i]);
        }

        printf("cpu %d free over\n", cpuid());

        // kvminit();          // 创建内核页表
        // kvminithart();      // 开启分页机制
    }
    else
    {
        while (started == 0)
            ;

        __sync_synchronize();

        // kvminithart();

        printf("\nhart %d starting!\n", cpuid());

        for (int i = 512; i < 1024; i++)
        {
            mem_pages[i] = (uint32 *)kalloc();
            if (mem_pages[i] == 0)
            {
                printf("hart %d: kalloc failed at page %d\n", cpuid(), i);
                break;
            }

            // 覆盖垃圾值
            memset(mem_pages[i], 1, PGSIZE);
            printf("mem_pages = %p, data = %d\n", mem_pages[i], mem_pages[i][0]);
        }

        printf("cpu %d alloc over\n", cpuid());

        over_hart_other = 1;

        while (over_hart0 == 0 || over_hart_other == 0)
            ;

        for (int i = 512; i < 1024; i++)
        {
            kfree((void *)mem_pages[i]);
        }

        printf("cpu %d free over\n", cpuid());

        plicinithart();
    }

    // 设置中断向量表
    // trapinithart();

    // 启用 S 模式下的中断
    // intr_on();

    while (1)
    {
        // 在此循环中可以处理中断
        asm volatile("wfi"); // 等待中断（Wait For Interrupt）
    }
}