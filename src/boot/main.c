#include "defs.h"
#include "riscv.h"

volatile static int started = 0;

// --- 使用锁机制的并行加法
volatile static int sum = 0;
static struct spinlock add_lock;

void main(){
    // uart_puts("\nhere!\n");
    if(cpuid() == 0){
        // 只有 hart0 执行系统初始化
        // 而其它 CPU 等待
        consoleinit(); // 初始化控制台, 目前只初始化 uart

        printfinit();        // 初始化printf功能

        uart_puts("\nxv6 is booting!\n");
        plicinit(); // 设置中断控制器（仅一次）
        plicinithart(); // 每个核都要去向 PLIC 请求设备
        __sync_synchronize(); // 确保代码不乱序执行
        started = 1;

        // 确保 CPU0 核 其它核一起竞争
        initlock(&add_lock, "add");
        for(int i = 0; i < 1000000; i++){

            acquire(&add_lock);
            sum++;
            release(&add_lock);
        }
        printf("\nsum = %d in hart %d\n", sum, cpuid());

    }else{
        while (started == 0);

        printf("\nhart %d starting!\n", cpuid());

        for(int i = 0; i < 1000000; i++){

            acquire(&add_lock);
            sum++;
            release(&add_lock);
        }
        printf("\nsum = %d in hart %d\n", sum, cpuid());

        __sync_synchronize();

        plicinithart();
    }
    
    // 设置中断向量表
    // trapinithart();
    
    // 启用 S 模式下的中断
    // intr_on();
    
    while (1) {
        // 在此循环中可以处理中断
        asm volatile("wfi");  // 等待中断（Wait For Interrupt）
    }
}