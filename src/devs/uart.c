#include "defs.h"
#include "types.h"
#include "memlayout.h"

extern volatile int panicked; // from printf.c


struct spinlock uart_tx_lock; 

void uart_putc(uint8 c){
    while((*(volatile uint8 *)(UART0 + LSR) & TX_IDLE) == 0);
    *(volatile uint8*)(UART0 + THR) = c;
}

void uart_puts(char * s){
    // accquire
    while (*s != '\0')
    {
        uart_putc(*s);
        s++;
    }
    // release
}

void uartinit(){

  initlock(&uart_tx_lock, "uart");
}