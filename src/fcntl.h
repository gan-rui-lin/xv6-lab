#define O_RDONLY    0x000
#define O_WRONLY    0x001
#define O_RDWR      0x002
#define O_CREATE    0x200
#define O_TRUNC     0x400
#define O_DIRECTORY 0x10000

// Linux flags
#define LINUX_O_CREAT    0x40
#define LINUX_O_TRUNC    0x200
#define LINUX_O_DIRECTORY 0x0200000

// openat/unlinkat 特殊值：表示使用当前工作目录
#define AT_FDCWD    (-100)

// unlinkat flags
// 与 Linux 保持一致的数值，便于直接使用用户态常量
#define AT_REMOVEDIR 0x200

// mmap protection flags
#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

// mmap flags
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_FILE        0x00

// mmap failed return value
#define MAP_FAILED ((void *) -1)
