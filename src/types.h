#ifndef TYPES_H
#define TYPES_H
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;

typedef uint64 pde_t;

// 检查系统是否已经定义了 timeval
#ifndef HOST_TIMEVAL_DEFINED
struct timeval {
  int tv_sec;
  int tv_usec;
};
#endif

#endif // TYPES_H

