#include "defs.h"

#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // Control-x

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
      consputc(0x100);
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

//
// user write()s to the console go here.
//
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    uart_putc(c);
  }

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while(cons.r == cons.w){
      // if(killed(myproc())){
      //   release(&cons.lock);
      //   return -1;
      // }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if(c == C('D')){  // end-of-file
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}