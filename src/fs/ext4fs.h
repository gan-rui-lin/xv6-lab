// Minimal ext4 glue built on top of lwext4.
// Provides a thin VFS-style facade so the rest of the kernel can
// branch on ext4_mode similarly to fat32_mode.

#pragma once

#include "types.h"
#include "stat.h"
#include "param.h"

struct inode;

#define EXT4_INODE_TAG 0xEF4

extern int ext4_mode;

void ext4fs_init(int dev);

struct inode* ext4_namei(char *path);
struct inode* ext4_nameiat(struct inode *base, char *path);

int ext4_readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n);
int ext4_writei(struct inode *ip, int user_src, uint64 src, uint off, uint n);
int ext4_truncate(struct inode *ip);

struct inode* ext4_createat(struct inode *dp, char *name, short type, short major, short minor);
int ext4_getdents64(struct inode *dp, uint *offp, uint64 uaddr, uint64 maxlen);
int ext4_unlink_path(const char *path, int is_dir);

