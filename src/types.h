#ifndef TYPES_H
#define TYPES_H
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;

typedef char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

// typedef char int8_t;
// typedef short int16_t;
// typedef int  int32_t;



typedef uint64 pde_t;

// 检查系统是否已经定义了 timeval
#ifndef HOST_TIMEVAL_DEFINED
struct timeval {
  int tv_sec;
  int tv_usec;
};
#endif

#endif // TYPES_H

