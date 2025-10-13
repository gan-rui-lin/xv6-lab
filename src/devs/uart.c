#include "defs.h"
#include "types.h"
#include "memlayout.h"

extern volatile int panicked; // from printf.c


struct spinlock uart_tx_lock; 

// forward declaration: console input handler in console.c
void consoleintr(int);

void uart_putc(uint8 c){
    while((*(volatile uint8 *)(UART0 + LSR) & TX_IDLE) == 0);
    *(volatile uint8*)(UART0 + THR) = c;
}


// TODO: 缓冲区机制解决外设速度不匹配
void uartputc_sync(uint8 c){

    acquire(&uart_tx_lock);

    // 如果 panicked, 所有核都不在输出任何信息，并死循环
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

// write a buffer to UART while holding uart_tx_lock once
void
uart_write(const char *s, int n)
{
        acquire(&uart_tx_lock);
        if(panicked){
             // if panicked, spin forever to avoid further corruption
             for(;;) ;
        }
        for(int i = 0; i < n; i++){
                while((*(volatile uint8 *)(UART0 + LSR) & TX_IDLE) == 0);
                *(volatile uint8*)(UART0 + THR) = s[i];
        }
        release(&uart_tx_lock);
}

void
uartintr(void)
{
    // Read all available characters from UART RHR and pass to console
    for(;;){
        // Check Line Status Register for RX ready (bit 0)
        if((*(volatile uint8 *)(UART0 + LSR) & 0x01) == 0)
            break;

        int c = *(volatile uint8 *)(UART0 + RHR);
        // forward to console input handler
        consoleintr(c);
    }
}

// helper: get one character from UART, or -1 if none
int
uartgetc(void)
{
    if((*(volatile uint8 *)(UART0 + LSR) & 0x01) != 0)
        return *(volatile uint8 *)(UART0 + RHR);
    else
        return -1;
}