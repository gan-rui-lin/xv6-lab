#ifndef XV6_ERRNO_H
#define XV6_ERRNO_H

// Minimal errno set for Linux-compatible syscall returns.
#define EPERM   1   /* Operation not permitted */
#define ENOENT  2   /* No such file or directory */
#define EIO     5   /* I/O error */
#define ENODEV  19  /* No such device */
#define ENOTDIR 20  /* Not a directory */
#define EISDIR  21  /* Is a directory */
#define EINVAL  22  /* Invalid argument */
#define EMFILE  24  /* Too many open files */
#define ENFILE  23  /* File table overflow */
#define EACCES  13  /* Permission denied */
#define EFAULT  14  /* Bad address */
#define ENOMEM  12  /* Out of memory */
#define EBADF   9   /* Bad file descriptor */
#define ECHILD  10  /* No child processes */
#define ENOEXEC 8   /* Exec format error */
#define E2BIG   7   /* Argument list too long */
#define ENOTSUP 95  /* Operation not supported */

#endif
