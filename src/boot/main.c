#include "defs.h"
#include "riscv.h"



void main(){
    uart_puts("\nhere!\n");
    
    // 设置中断向量表
    // trapinithart();
    
    // 启用 S 模式下的中断
    // intr_on();
    
    while (1) {
        // 在此循环中可以处理中断
        asm volatile("wfi");  // 等待中断（Wait For Interrupt）
    }
}