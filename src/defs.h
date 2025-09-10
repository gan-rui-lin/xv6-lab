

#include "types.h"

#define RHR 0                 // receive holding register (for input bytes)
#define THR 0  
#define LSR 5
#define TX_IDLE 0x20
#define UART_BASE 0x10000000

void            uart_putc(uint8 c);
void            uart_puts(char *s);