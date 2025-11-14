#include "defs.h"
#include "riscv.h"
#include "types.h"

volatile static int started = 0;

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
        kvminit();            // 创建内核页表
        kvminithart();        // 开启分页机制
        
        procinit();          // 进程表初始化
        // 为每一个 hart 设置中断向量表
        trapinithart();
        userinit();          // 创建第一个用户进程
        __sync_synchronize(); // 确保代码不乱序执行
        started = 1;
    }
    else
    {
        while (started == 0)
            ;

        printf("\nhart %d starting!\n", cpuid());
            // 为每一个 hart 设置中断向量表
        trapinithart();
        kvminithart();
        plicinithart();

        __sync_synchronize();
    }



#ifdef TICKER_DEBUG
    // 启用 S 模式下的中断
    intr_on();
#endif
#ifdef CONSOLE_DEBUG
    intr_on();
#endif

    // 所有CPU都进入调度器，开始调度用户进程
    scheduler();  

}