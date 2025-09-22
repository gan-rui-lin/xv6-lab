//
// 格式化控制台输出 -- printf, panic
// 提供内核级别的格式化输出功能，支持基本的格式说明符
// 包括整数、十六进制、指针和字符串输出
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
// #include "sleeplock.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

// 全局 panic 状态标志，用于冻结其他 CPU 的输出
volatile int panicked = 0;

// printf 锁机制，防止并发 printf 调用时输出交错
static struct {
  struct spinlock lock;  // 自旋锁，保护 printf 输出
  int locking;          // 锁定标志：1=启用锁定，0=禁用锁定
} pr;

// 数字转换时使用的字符表（支持 16 进制）
static char digits[] = "0123456789abcdef";

// 打印整数到控制台
// 参数：
//   xx: 要打印的整数值
//   base: 进制基数（10=十进制，16=十六进制）
//   sign: 是否处理符号（1=有符号，0=无符号）
static void
printint(int xx, int base, int sign)
{
  char buf[16];  // 数字字符缓冲区（足够存储 64 位数字）
  int i;
  uint x;

  // 处理负数：如果是有符号数且为负，转换为正数并记录符号
  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  // 将数字转换为字符串（逆序存储）
  i = 0;
  do {
    buf[i++] = digits[x % base];  // 取余数对应的字符
  } while((x /= base) != 0);      // 继续处理商

  // 添加负号（如果需要）
  if(sign)
    buf[i++] = '-';

  // 逆序输出字符（因为之前是逆序存储的）
  while(--i >= 0)
    consputc(buf[i]);
}

// 打印指针地址到控制台（格式：0x[16位十六进制]）
// 参数：
//   x: 要打印的指针值（64位地址）
static void
printptr(uint64 x)
{
  int i;
  
  // 输出 "0x" 前缀
  consputc('0');
  consputc('x');
  
  // 输出 16 个十六进制数字（64位地址）
  // 从最高位开始，每次取4位转换为十六进制字符
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// 格式化输出到控制台
// 支持的格式说明符：%d(十进制), %x(十六进制), %p(指针), %s(字符串), %%(百分号)
// 参数：
//   fmt: 格式字符串
//   ...: 可变参数列表
void
printf(char *fmt, ...)
{
  va_list ap;           // 可变参数列表指针
  int i, c, locking;
  char *s;

  // 获取当前锁定状态
  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);  // 获取 printf 锁，防止输出交错

  // 检查格式字符串有效性
  if (fmt == 0)
    panic("null fmt");

  // 初始化可变参数处理
  va_start(ap, fmt);
  
  // 逐字符处理格式字符串
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      // 普通字符，直接输出
      consputc(c);
      continue;
    }
    
    // 处理格式说明符
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;  // 格式字符串结束
      
    switch(c){
    case 'd':
      // %d: 十进制有符号整数
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      // %x: 十六进制整数
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      // %p: 指针地址
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      // %s: 字符串
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";  // 空指针处理
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      // %%: 输出百分号字面量
      consputc('%');
      break;
    default:
      // 未知格式说明符，输出 %c 以引起注意
      consputc('%');
      consputc(c);
      break;
    }
  }
  
  va_end(ap);  // 清理可变参数

  // 释放锁
  if(locking)
    release(&pr.lock);
}

// 系统 panic 处理函数
// 输出 panic 信息并使系统进入无限循环状态
// 参数：
//   s: panic 消息字符串
void
panic(char *s)
{
  pr.locking = 0;     // 禁用锁定，确保 panic 信息能够输出
  printf("panic: ");   // 输出 panic 前缀
  printf(s);          // 输出具体的 panic 消息
  printf("\n");       // 换行
  panicked = 1;       // 设置全局 panic 标志，冻结其他 CPU 的 UART 输出
  
  // 进入无限循环，停止系统运行
  for(;;)
    ;
}

// 初始化 printf 子系统
// 设置用于保护 printf 输出的自旋锁
void
printfinit(void)
{
  initlock(&pr.lock, "pr");  // 初始化 printf 锁
  pr.locking = 1;            // 启用锁定机制
}
