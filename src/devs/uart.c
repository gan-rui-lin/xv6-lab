#include "defs.h"
#include "types.h"
#include "memlayout.h"

void uart_putc(uint8 c){
    while((*(volatile uint8 *)(UART0 + LSR) & TX_IDLE) == 0);
    *(volatile uint8*)(UART0 + THR) = c;
}

void uart_puts(char * s){
    while (*s != '\0')
    {
        uart_putc(*s);
        s++;
    }
}