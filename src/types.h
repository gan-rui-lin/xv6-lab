#ifndef TYPES_H
#define TYPES_H
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char  uint8;
typedef unsigned short uint16;
typedef unsigned int   uint32;
typedef unsigned long  uint64;
typedef long long      int64;
typedef unsigned long  uintptr_t;

// stdint 风格别名
#ifndef HOST_BUILD
typedef uint8   uint8_t;
typedef uint16  uint16_t;
typedef uint32  uint32_t;
typedef uint64  uint64_t;
typedef signed char     int8_t;
typedef short           int16_t;
typedef int             int32_t;
typedef long long       int64_t;

// POSIX 风格
typedef int pid_t;
typedef long ssize_t;
typedef unsigned int mode_t;
typedef long clock_t;
typedef unsigned long size_t;
typedef unsigned int socklen_t;
#else
// 在主机系统上编译时，使用系统的类型定义
#include <stdint.h>
#include <sys/types.h>
#endif

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
