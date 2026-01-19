#define O_RDONLY    0x000
#define O_WRONLY    0x001
#define O_RDWR      0x002
#define O_CREATE    0x200
#define O_TRUNC     0x400
#define O_DIRECTORY 0x10000
// 非阻塞标志（与 Linux 保持一致的位值，便于用户态传递）
#define O_NONBLOCK  0x00000800
// close-on-exec 标志（用于 pipe2/fcntl，但当前内核不强制实现）
#define O_CLOEXEC   0x02000000

// Linux flags
#define LINUX_O_CREAT    0x40
#define LINUX_O_TRUNC    0x200
#define LINUX_O_DIRECTORY 0x0200000

// openat 特殊值：表示使用当前工作目录
#define AT_FDCWD    (-100)
// fstatat/faccessat 标志：不跟随符号链接（目前忽略）
#define AT_SYMLINK_NOFOLLOW 0x100
// 兼容 Linux 的其他标志位（当前忽略）
#define AT_NO_AUTOMOUNT 0x800
#define AT_EMPTY_PATH   0x1000

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

// fcntl commands (Linux)
#define F_DUPFD    0
#define F_GETFD    1
#define F_SETFD    2
#define F_GETFL    3
#define F_SETFL    4

// fcntl flags
#define FD_CLOEXEC 1

// lseek whence
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
