#include "defs.h"

#define BACKSPACE 0x100

struct {
  struct spinlock lock;

  // input buffer
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
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
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
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

// handle a input character received from UART
// called from uartintr()
void
consoleintr(int c)
{
  acquire(&cons.lock);
  if(c == 0x100) {
    // ignore
  } else if(c == 0x03){ // Ctrl-C placeholder -> ignore or send to proc
    // TODO: send signal to process
  } else if(c == '\r'){
    // translate CR to NL
    cons.buf[cons.e++ % INPUT_BUF_SIZE] = '\n';
    consputc('\n');
    cons.w = cons.e;
    wakeup(&cons.r);
  } else if(c == 0x7f || c == '\b'){
    // backspace
    if(cons.e != cons.w){
      cons.e--;
      consputc('\b');
    }
  } else {
    if((cons.e - cons.r) < INPUT_BUF_SIZE){
      cons.buf[cons.e % INPUT_BUF_SIZE] = c;
      cons.e++;
      consputc(c);
      if(c == '\n' || (cons.e - cons.r) == INPUT_BUF_SIZE){
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
  }
  release(&cons.lock);
}

