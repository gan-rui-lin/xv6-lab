#include "defs.h"

#define BACKSPACE 0x100

struct {
  struct spinlock lock;
  
//   // input
// #define INPUT_BUF_SIZE 128
//   char buf[INPUT_BUF_SIZE];
//   uint r;  // Read index
//   uint w;  // Write index
//   uint e;  // Edit index
} cons;


//
// send one character to the uart.
// called by printf(), and to echo input characters,
// but not from write().
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // if the user typed backspace, overwrite with a space.
    // uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    // uartputc_sync(c);
  }
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  uartinit();

  // devsw[CONSOLE].read = consoleread;
  // devsw[CONSOLE].write = consolewrite;
}

