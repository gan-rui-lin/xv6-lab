#include "defs.h"
#include "riscv.h"

volatile static int started = 0;

void main(){
    // uart_puts("\nhere!\n");
    if(cpuid() == 0){
        // 只有 hart0 执行系统初始化
        // 而其它 CPU 等待
        uart_puts("\nxv6 is booting!\n");
        plicinit(); // 设置中断控制器（仅一次）
        plicinithart(); // 每个核都要去向 PLIC 请求设备
        __sync_synchronize(); // 确保多个核?
        started = 1;

    }else{
        while (started == 0);

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