/*
 * Virtual filesystem core that manages inode caching and delegates
 * all real work to the active filesystem driver (currently FAT32).
 */

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fs/vfs.h"
#include "fs/fat32.h"

static const struct vfs_driver *root_driver;
int fat32_mode = 0;

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

static struct inode* iget(uint dev, uint inum);

void
vfs_mount_root(const struct vfs_driver *driver)
{
  root_driver = driver;
  if(driver)
    log_info("vfs: mounted root filesystem '%s'\n", driver->name);
}

const struct vfs_driver *
vfs_current_driver(void)
{
  return root_driver;
}

void
fsinit(int dev)
{
  initlock(&icache.lock, "icache");
  for(struct inode *ip = icache.inode; ip < &icache.inode[NINODE]; ip++){
    initsleeplock(&ip->lock, "inode");
  }

  fat32_init(dev);
  if(root_driver == 0)
    panic("fsinit: no root filesystem");
}

struct inode*
iget_pub(uint dev, uint inum)
{
  return iget(dev, inum);
}

static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty = 0;

  acquire(&icache.lock);

  for(ip = icache.inode; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)
      empty = ip;
  }

  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  ip->ops = 0;
  ip->fs_private = 0;
  ip->nlink = 0;
  ip->size = 0;
  release(&icache.lock);

  return ip;
}

struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

void
ilock(struct inode *ip)
{
  if(ip == 0 || ip->ref < 1)
    panic("ilock");
  acquiresleep(&ip->lock);
  if(!ip->valid)
    ip->valid = 1;
}

void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock))
    panic("iunlock");
  releasesleep(&ip->lock);
}

void
iupdate(struct inode *ip)
{
  // VFS-managed inodes update themselves via driver callbacks;
  // nothing to do here.
  (void)ip;
}

void
iput(struct inode *ip)
{
  acquire(&icache.lock);
  if(ip->ref == 1 && ip->nlink == 0){
    ip->valid = 0;
    ip->ops = 0;
    ip->fs_private = 0;
  }
  ip->ref--;
  release(&icache.lock);
}

void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

void
stati(struct inode *ip, struct stat *st)
{
  if(ip == 0 || st == 0)
    return;

  if(ip->ops && ip->ops->stat){
    ip->ops->stat(ip, st);
    return;
  }

  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  if(ip == 0 || ip->ops == 0 || ip->ops->read == 0)
    panic("readi: missing ops");
  return ip->ops->read(ip, user_dst, dst, off, n);
}

int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  if(ip == 0 || ip->ops == 0 || ip->ops->write == 0)
    panic("writei: missing ops");
  return ip->ops->write(ip, user_src, src, off, n);
}

int
getdents64(struct inode *dp, uint *offp, uint64 uaddr, uint64 maxlen)
{
  if(dp == 0 || dp->ops == 0 || dp->ops->getdents64 == 0)
    return -1;
  return dp->ops->getdents64(dp, offp, uaddr, maxlen);
}

struct inode*
namei(char *path)
{
  if(root_driver == 0 || root_driver->ops == 0 || root_driver->ops->namei == 0)
    return 0;
  return root_driver->ops->namei(path);
}

struct inode*
nameiat(struct inode *base, char *path)
{
  if(root_driver == 0 || root_driver->ops == 0 || root_driver->ops->nameiat == 0)
    return 0;
  return root_driver->ops->nameiat(base, path);
}

struct inode*
nameiparent(char *path, char *name)
{
  if(root_driver == 0 || root_driver->ops == 0 || root_driver->ops->nameiparent == 0)
    return 0;
  return root_driver->ops->nameiparent(path, name);
}

struct inode*
create(char *path, short type, int major, int minor)
{
  if(root_driver == 0 || root_driver->ops == 0 || root_driver->ops->create == 0)
    return 0;
  return root_driver->ops->create(path, type, major, minor);
}

struct inode*
createat(struct inode *dp, char *name, short type, int major, int minor)
{
  if(root_driver == 0 || root_driver->ops == 0 || root_driver->ops->createat == 0)
    return 0;
  return root_driver->ops->createat(dp, name, type, major, minor);
}

int
vfs_unlink_path(char *path, int want_dir)
{
  if(root_driver == 0 || root_driver->ops == 0 || root_driver->ops->unlink_path == 0)
    return -1;
  return root_driver->ops->unlink_path(path, want_dir);
}
