#include "riscv.h"


// 需要关中断，以防止内核切换过程中的险态
int
cpuid()
{
  int id = r_tp();
  return id;
}