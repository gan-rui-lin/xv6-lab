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

// openat 特殊值：表示使用当前工作目录
#define AT_FDCWD    (-100)
