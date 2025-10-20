#include "defs.h"
#include "types.h"
#include "memlayout.h"

extern volatile int panicked; // from printf.c

/* register offsets (from UART0 base) */
#define UART_REG_RHR 0   // Receiver Holding Register (read)
#define UART_REG_THR 0   // Transmitter Holding Register (write)
#define UART_REG_IER 1   // Interrupt Enable Register
#define UART_REG_FCR 2   // FIFO Control Register (write)
#define UART_REG_LCR 3   // Line Control Register
#define UART_REG_MCR 4   // Modem Control Register
#define UART_REG_LSR 5   // Line Status Register

/* LCR bits */
#define LCR_BAUD_LATCH 0x80
#define LCR_EIGHT_BITS 0x03

/* FCR bits */
#define FCR_FIFO_ENABLE 0x07

/* IER bits */
#define IER_RX_ENABLE 0x01
#define IER_TX_ENABLE 0x02

/* helper macro to access UART registers */
#define UART_WRITE_REG(off, v) (*(volatile uint8 *)(UART0 + (off)) = (v))
#define UART_READ_REG(off) (*(volatile uint8 *)(UART0 + (off)))

static struct spinlock uart_tx_lock;

/* forward declaration: console input handler in console.c */
void consoleintr(int);

// 非阻塞的 uart 输出
static void
uart_write_byte_nolock(uint8 c)
{
    while((UART_READ_REG(UART_REG_LSR) & TX_IDLE) == 0)
        ;
    UART_WRITE_REG(UART_REG_THR, c);
}

// 高级写入函数：获取锁并写入缓冲区
void
uart_write(const char *s, int n)
{
    acquire(&uart_tx_lock);
    if(panicked){
        for(;;) ;
    }
    for(int i = 0; i < n; i++)
        uart_write_byte_nolock((uint8)s[i]);
    release(&uart_tx_lock);
}

/* simple putc without locking (keeps API backward compatible) */
void
uart_putc(uint8 c)
{
    uart_write_byte_nolock(c);
}


// TODO: 缓冲区机制解决外设速度不匹配
// 同步的 uart 输出，通过锁机制保证顺序
void
uartputc_sync(uint8 c)
{
    acquire(&uart_tx_lock);
    if(panicked){
        for(;;) ;
    }
    uart_write_byte_nolock(c);
    release(&uart_tx_lock);
}

void
uart_puts(char *s)
{
  // simple wrapper: write whole string under lock
  int len = 0;
  for(char *p = s; p && *p; p++) len++;
  if(len > 0)
    uart_write(s, len);
}

void uartinit(){

    // 暂时禁用中断
    UART_WRITE_REG(UART_REG_IER, 0x00); // disable interrupts

    // 设置波特率
    UART_WRITE_REG(UART_REG_LCR, LCR_BAUD_LATCH);
    UART_WRITE_REG(0, 0x03); // divisor LSB
    UART_WRITE_REG(1, 0x00); // divisor MSB
    UART_WRITE_REG(UART_REG_LCR, LCR_EIGHT_BITS);

    // 启用 FIFO
    UART_WRITE_REG(UART_REG_FCR, FCR_FIFO_ENABLE);
    // 启用发送中断和接收中断
    UART_WRITE_REG(UART_REG_IER, IER_TX_ENABLE | IER_RX_ENABLE);

    initlock(&uart_tx_lock, "uart");
}

// // write a buffer to UART while holding uart_tx_lock once
// void
// uart_write(const char *s, int n)
// {
//         acquire(&uart_tx_lock);
//         if(panicked){
//              // if panicked, spin forever to avoid further corruption
//              for(;;) ;
//         }
//         for(int i = 0; i < n; i++){
//                 while((*(volatile uint8 *)(UART0 + LSR) & TX_IDLE) == 0);
//                 *(volatile uint8*)(UART0 + THR) = s[i];
//         }
//         release(&uart_tx_lock);
// }

void
uartintr(void)
{
    // printf("uartintr\n");
    // Read all available characters from UART RHR and pass to console
    for(;;){
        if((UART_READ_REG(UART_REG_LSR) & 0x01) == 0)
            break;
        int c = UART_READ_REG(UART_REG_RHR);
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