#include "defs.h"
#include "riscv.h"
#include "types.h"

#include "./fs/thinfat32.h"

#include "sleeplock.h" // TODO 和 fs/file.h 捆绑着引入
#include "fs/fs.h"     // TODO 和 fs/file.h 捆绑着引入
#include "fs/file.h"

volatile static int started = 0;

void main()
{
    // uart_puts("\nhere!\n");
;
    if (cpuid() == 0)
    {
        // 只有 hart0 执行系统初始化
        // 而其它 CPU 等待
        consoleinit(); // 初始化控制台, 目前只初始化 uart

        printfinit(); // 初始化printf功能

        print_ruos();

        kinit(); // 物理页面分配器初始化

        log_info("xv6 is booting!\n");
        
        plicinit();           // 设置中断控制器（仅一次）
        plicinithart();       // 每个核都要去向 PLIC 请求设备
        kvminit();            // 创建内核页表
        kvminithart();        // 开启分页机制
        procinit();          // 进程表初始化
        // 为每一个 hart 设置中断向量表
        trapinithart();
        binit();               // 初始化内存块管理器
        iinit();               // 初始化 inode 管理器
        fileinit();      // 初始化文件表的锁
        virtio_disk_init(minor(ROOTDEV)); // 初始化磁盘驱动

        // 使能 S 模式中断（外部/软件/定时器）并打开全局 SIE
        w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
        intr_on();

        userinit();          // 创建第一个用户进程

        __sync_synchronize(); // 确保代码不乱序执行


        started = 1;
    }
    else
    {
        while (started == 0)
            ;

        // printf("\nhart %d starting!\n", cpuid());
        log_info("hart %d starting!\n", cpuid());
        
        // 为每一个 hart 设置中断向量表
        trapinithart();
        kvminithart();
        plicinithart();

        // 次级核也需要使能 S 模式中断并打开全局 SIE
        w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);
        intr_on();

        __sync_synchronize();
    }



    // 中断已在各自分支中打开，这里不再条件打开。

    // 所有CPU都进入调度器，开始调度用户进程
    scheduler();  

}