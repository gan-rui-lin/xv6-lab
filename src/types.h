#ifndef TYPES_H
#define TYPES_H
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;

typedef long long int64;

// POSIX 类型定义
typedef int pid_t;
typedef long ssize_t;
typedef unsigned int mode_t;
typedef long clock_t;
typedef unsigned long size_t;

typedef char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

// typedef char int8_t;
// typedef short int16_t;
// typedef int  int32_t;


typedef uint32 uint32_t;
typedef uint16 uint16_t;
typedef uint8 uint8_t;
typedef unsigned long uint64_t;
typedef unsigned int   uint;
typedef long int64;
typedef unsigned long uintptr_t;
typedef signed char __int8_t;
typedef unsigned char __uint8_t;
typedef signed short int __int16_t;
typedef unsigned short int __uint16_t;
typedef signed int __int32_t;
typedef unsigned int __uint32_t;
typedef signed long int __int64_t;
typedef unsigned long int __uint64_t;
typedef __int8_t int8_t;
typedef __int16_t int16_t;
typedef __int32_t int32_t;
typedef __int64_t int64_t;

typedef uint64 pde_t;

// 检查系统是否已经定义了 timeval
#ifndef HOST_TIMEVAL_DEFINED
// Match Linux ABI on RV64: fields are long (64-bit)
struct timeval {
  long tv_sec;
  long tv_usec;
};
#endif

#endif // TYPES_H

