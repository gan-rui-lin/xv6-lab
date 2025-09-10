

#include "types.h"

#define RHR 0                 // receive holding register (for input bytes)
#define THR 0  
#define LSR 5
#define TX_IDLE 0x20

void            uart_putc(uint8 c);
void            uart_puts(char *s);

// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// 设置异常向量表
void trapinithart(void);