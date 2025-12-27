// Minimal FAT32 (read-only) support for exec
// Scope: path lookup in root/subdirs, short 8.3 names, file read

#pragma once

#include "types.h"

// Exposed toggle so fs.c can branch
extern int fat32_mode;

// Initialize FAT32 by parsing BPB in sector 0
void fat32_init(int dev);

// FAT32 versions of core ops used by exec
struct inode;
struct inode* fat32_namei(char *path);
struct inode* fat32_nameiat(struct inode *base, char *path);
int fat32_readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n);
struct inode* fat32_createat(struct inode *dp, char *name, short type, int major, int minor);
struct inode* fat32_create(char *path, short type, int major, int minor);

// Tag to identify FAT32-backed inodes via ip->major
#define FAT32_INODE_TAG 0xF32
