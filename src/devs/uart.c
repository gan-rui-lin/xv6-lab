#include "defs.h"
#include "types.h"
#include "memlayout.h"

extern volatile int panicked; // from printf.c


struct spinlock uart_tx_lock; 

void uart_putc(uint8 c){
    while((*(volatile uint8 *)(UART0 + LSR) & TX_IDLE) == 0);
    *(volatile uint8*)(UART0 + THR) = c;
}


// TODO: 缓冲区机制解决外设速度不匹配
void uartputc_sync(uint8 c){

    acquire(&uart_tx_lock);

    if(panicked){
       for(;;) ;
    }

    while((*(volatile uint8 *)(UART0 + LSR) & TX_IDLE) == 0);
    *(volatile uint8*)(UART0 + THR) = c;

    release(&uart_tx_lock);
}

void uart_puts(char * s){
    // acquire --> 交给 printf 的功能
    while (*s != '\0')
    {
        //  字符锁，保证不会掉字符，但不保证句间完整性
        uartputc_sync(*s);
        s++;
    }
}

void uartinit(){

  initlock(&uart_tx_lock, "uart");
}