#include "types.h"
#include "defs.h"
#include "param.h"

// Simple assertion hook for lwext4
void ext4_assert(int expr) {
  if(!expr){
    panic("ext4_assert");
  }
}

// Minimal libc helpers used by lwext4
int strcmp(const char *a, const char *b) {
  while(*a && (*a == *b)){
    a++; b++;
  }
  return (uchar)*a - (uchar)*b;
}

char *strcpy(char *dst, const char *src) {
  char *ret = dst;
  while((*dst++ = *src++) != 0)
    ;
  return ret;
}

// Naive qsort implementation (insertion sort) suitable for small arrays
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
  char *arr = (char *)base;
  for(size_t i = 1; i < nmemb; i++){
    size_t j = i;
    while(j > 0){
      char *p1 = arr + (j - 1) * size;
      char *p2 = arr + j * size;
      if(compar(p1, p2) <= 0)
        break;
      // swap
      for(size_t k = 0; k < size; k++){
        char tmp = p1[k];
        p1[k] = p2[k];
        p2[k] = tmp;
      }
      j--;
    }
  }
}
