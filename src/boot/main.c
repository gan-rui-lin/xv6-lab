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
    print_ruos();
    if (cpuid() == 0)
    {
        // 只有 hart0 执行系统初始化
        // 而其它 CPU 等待
        consoleinit(); // 初始化控制台, 目前只初始化 uart

        printfinit(); // 初始化printf功能

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

        userinit();          // 创建第一个用户进程

        //添加代码：测试文件系统
        // 添加测试代码
//-------------------------------
printf("[FAT32 TEST] Start...\n");

// 初始化 FAT32 文件系统
// if (tf_init() == 0) {
//     printf("[FAT32 TEST] tf_init success!\n");
// } else {
//     printf("[FAT32 TEST] tf_init failed!\n");
//     // 可以选择 return 或 panic，这里继续
// }

// 创建目录
if (tf_mkdir("/testdir", 0) == 0) {
    printf("[FAT32 TEST] mkdir /testdir success!\n");
} else {
    printf("[FAT32 TEST] mkdir /testdir fail!\n");
}

// 创建并写入新文件
TFFile *fp = tf_fopen("/testdir/hello.txt", "w");
if (fp) {
    char *msg = "Hello, FAT32!";
    if (tf_fwrite((uint8_t*)msg, 1, strlen(msg), fp) == strlen(msg)) {
        printf("[FAT32 TEST] Write success\n");
    } else {
        printf("[FAT32 TEST] Write fail\n");
    }
    tf_fclose(fp);
} else {
    printf("[FAT32 TEST] open /testdir/hello.txt fail!\n");
}

// 再次读取验证
fp = tf_fopen("/testdir/hello.txt", "r");
if (fp) {
    char buf[32] = {0};
    tf_fread((uint8_t*)buf, strlen("Hello, FAT32!"), fp);
    printf("[FAT32 TEST] Read: '%s'\n", buf);
    tf_fclose(fp);
} else {
    printf("[FAT32 TEST] open /testdir/hello.txt for read fail!\n");
}

printf("[FAT32 TEST] End.\n");
//-------------------------------
        //end

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