#include "types.h"
#include "spinlock.h"

#define RHR 0                 // receive holding register (for input bytes)
#define THR 0  
#define LSR 5
#define TX_IDLE 0x20

void            uart_putc(uint8 c);
void            uart_puts(char *s);
void            uartinit(void);
void            uartputc_sync(uint8 c);

// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// 设置异常向量表
void trapinithart(void);

// proc.c
int cpuid();
struct cpu* mycpu(void);

// spinlock.c
void            acquire(struct spinlock*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            push_off(void);
void            pop_off(void);

// printf.c
void            printf(char*, ...);
void            panic(char*) __attribute__((noreturn));
void            printfinit(void);

// console.c
void consputc(int c);
void consoleinit(void);