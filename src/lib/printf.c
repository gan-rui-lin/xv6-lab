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


// ===== 颜色定义 =====
#define COLOR_RESET   "\033[0m"
#define COLOR_BLACK   "\033[30m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"

#define COLOR_BOLD_RED    "\033[1;31m"
#define COLOR_BOLD_GREEN  "\033[1;32m"
#define COLOR_BOLD_YELLOW "\033[1;33m"
#define COLOR_BOLD_BLUE   "\033[1;34m"


// ===== 提取原有的格式化逻辑到内部函数 =====
static void
vprintf_internal(char *fmt, va_list ap)
{
  int i, c;
  char *s;

  if (fmt == 0)
    panic("null fmt");

  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
      
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      consputc('%');
      consputc(c);
      break;
    }
  }
}

// ===== 颜色输出函数 =====
static void
print_color_seq(char *color_seq)
{
  char *s;
  for(s = color_seq; *s; s++)
    consputc(*s);
}

// ===== 彩色 printf =====
void
printf_color(char *color, char *fmt, ...)
{
  va_list ap;
  int locking;
  
  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  // 设置颜色
  print_color_seq(color);
  
  // 格式化输出
  va_start(ap, fmt);
  vprintf_internal(fmt, ap);
  va_end(ap);
  
  // 重置颜色
  print_color_seq(COLOR_RESET);

  if(locking)
    release(&pr.lock);
}



// ===== 修改原有的 printf 函数 =====
void
printf(char *fmt, ...)
{
  va_list ap;
  int locking;

  locking = pr.locking;
  if(locking)
    acquire(&pr.lock);

  va_start(ap, fmt);
  vprintf_internal(fmt, ap);
  va_end(ap);

  if(locking)
    release(&pr.lock);
}

// ===== 便捷的日志函数 =====
void
log_info(char *fmt, ...)
{
  #ifdef LOG_INFO_ENABLE
    va_list ap;
    va_start(ap, fmt);
    printf_color(COLOR_GREEN, "[INFO] ");
    vprintf_internal(fmt, ap);
    printf_color(COLOR_RESET, "");
    va_end(ap);

  #else
    (void)fmt; // 避免未使用参数的编译警告
  #endif
}

void
log_warn(char *fmt, ...)
{
  #ifdef LOG_WARN_ENABLE
    va_list ap;
    va_start(ap, fmt);
    printf_color(COLOR_YELLOW, "[WARN] ");
    vprintf_internal(fmt, ap);
    printf_color(COLOR_RESET, "");
    va_end(ap);
  #else
    (void)fmt; // 避免未使用参数的编译警告
  #endif
}

void
log_error(char *fmt, ...)
{
  #ifdef LOG_ERROR_ENABLE
    va_list ap;
    va_start(ap, fmt);
    printf_color(COLOR_RED, "[ERROR] ");
    vprintf_internal(fmt, ap);
    printf_color(COLOR_RESET, "");
    va_end(ap);
  #else
    (void)fmt; // 避免未使用参数的编译警告
  #endif
}

void
log_debug(char *fmt, ...)
{
  #ifdef LOG_DEBUG_ENABLE
    va_list ap;
    va_start(ap, fmt);
    printf_color(COLOR_CYAN, "[DEBUG] ");
    vprintf_internal(fmt, ap);
    printf_color(COLOR_RESET, "");
    va_end(ap);
  #else
    (void)fmt; // 避免未使用参数的编译警告
  #endif
}