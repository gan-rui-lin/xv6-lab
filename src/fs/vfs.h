#ifndef VFS_H
#define VFS_H

#include "types.h"

struct inode;
struct stat;

struct vfs_node_ops
{
    int (*read)(struct inode *ip, int user_dst, uint64 dst, uint off, uint n);
    int (*write)(struct inode *ip, int user_src, uint64 src, uint off, uint n);
    void (*stat)(struct inode *ip, struct stat *st);
    int (*getdents64)(struct inode *dp, uint *offp, uint64 uaddr, uint64 maxlen);
};

struct vfs_fs_ops
{
    struct inode *(*namei)(char *path);
    struct inode *(*nameiat)(struct inode *base, char *path);
    struct inode *(*nameiparent)(char *path, char *name);
    struct inode *(*create)(char *path, short type, int major, int minor);
    struct inode *(*createat)(struct inode *dp, char *name, short type, int major, int minor);
    int (*unlink_path)(char *path, int want_dir);
};

struct vfs_driver
{
    const char *name;
    const struct vfs_fs_ops *ops;
};

void vfs_mount_root(const struct vfs_driver *driver);
const struct vfs_driver *vfs_current_driver(void);
int vfs_unlink_path(char *path, int want_dir);

#endif
