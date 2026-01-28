
#ifndef STAT_H
#define STAT_H

#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint mode;   // File permissions
  uint64 size; // Size of file in bytes
};

// Linux-compatible kstat structure for fstat syscall
struct kstat {
  uint64 st_dev;        // Device ID
  uint64 st_ino;        // Inode number
  uint32 st_mode;       // File mode
  uint32 st_nlink;      // Number of hard links
  uint32 st_uid;        // User ID
  uint32 st_gid;        // Group ID
  uint64 st_rdev;       // Device ID (if special file)
  uint64 __pad;
  uint64 st_size;       // Total size in bytes
  uint32 st_blksize;    // Block size for filesystem I/O
  uint32 __pad2;
  uint64 st_blocks;     // Number of 512B blocks allocated
  long st_atime_sec;    // Last access time (seconds)
  long st_atime_nsec;   // Last access time (nanoseconds)
  long st_mtime_sec;    // Last modification time (seconds)
  long st_mtime_nsec;   // Last modification time (nanoseconds)
  long st_ctime_sec;    // Last status change time (seconds)
  long st_ctime_nsec;   // Last status change time (nanoseconds)
};

#endif // STAT_H
