#include "riscv.h"
#include "proc.h"
#include "param.h"

extern struct cpu cpus[NCPU];
// 需要关中断，以防止内核切换过程中的险态
int
cpuid()
{
  int id = r_tp();
  return id;
}

struct cpu* mycpu(){

  int my_id = cpuid();

  return &cpus[my_id];
}