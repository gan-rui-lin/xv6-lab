#ifndef PROCFS_H
#define PROCFS_H

#include "types.h"
#include "fs.h"

#define PROCFS_INODE_TAG 0x9C0

struct inode* procfs_namei(char *full);
int procfs_readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n);

#endif