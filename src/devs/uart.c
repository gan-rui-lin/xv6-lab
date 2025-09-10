#include "defs.h"
#include "types.h"

void uart_putc(uint8 c){
    while((*(volatile uint8 *)(UART_BASE + LSR) & TX_IDLE) == 0);
    *(volatile uint8*)(UART_BASE + THR) = c;
}

void uart_puts(char * s){
    while (*s != '\0')
    {
        uart_putc(*s);
        s++;
    }
}