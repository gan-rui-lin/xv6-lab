#include "types.h"

static uint64 rand_state = 1;

void
srand(unsigned int seed)
{
  rand_state = seed ? seed : 1;
}

int
rand(void)
{
  // simple LCG
  rand_state = rand_state * 6364136223846793005ULL + 1;
  return (int)((rand_state >> 33) & 0x7fffffff);
}
