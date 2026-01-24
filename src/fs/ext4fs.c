// Glue code to expose lwext4 to the existing xv6-style VFS entry points.
// The interface mirrors the lightweight FAT32 shim: ext4_mode toggles
// whether helpers below are used by fs.c/sysfile.c.

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "buf.h"
#include "file.h"
#include "fs.h"
#include "procfs.h"
#include "stat.h"
#include "ext4fs.h"
#include "string.h"

#include <ext4.h>
#include <ext4_errno.h>
#include <ext4_blockdev.h>

#define EXT4_DEV_NAME "ext4dev"
#define EXT4_SECTOR_SIZE 512

// Block device wrapper for lwext4.
static struct ext4_blockdev ext4_bd;
static struct ext4_blockdev_iface ext4_iface;
static uint8 ext4_bbuf[EXT4_SECTOR_SIZE];
static int ext4_devno = 0;

int ext4_mode = 0;

// Helpers to create runtime symlinks to BusyBox applets ---------------------
static int path_exists(const char *path) {
  ext4_file fchk;
  if(ext4_fopen2(&fchk, path, O_RDONLY) == EOK){
    ext4_fclose(&fchk);
    return 1;
  }
  ext4_dir dchk;
  if(ext4_dir_open(&dchk, path) == EOK){
    ext4_dir_close(&dchk);
    return 1;
  }
  size_t rc = 0;
  char tbuf[2];
  if(ext4_readlink(path, tbuf, sizeof(tbuf), &rc) == EOK)
    return 1;
  return 0;
}

static int ensure_symlink(const char *linkpath, const char *target) {
  if(path_exists(linkpath))
    return 0;
  int rr = ext4_fsymlink(target, linkpath);
  if(rr == EOK){
    log_info("ext4: created symlink %s -> %s", linkpath, target);
    return 1;
  } else {
    log_warn("ext4: symlink create failed %d for %s -> %s", rr, linkpath, target);
    return -1;
  }
}

static void ensure_dir(const char *dirpath) {
  if(path_exists(dirpath))
    return;
  int rr = ext4_dir_mk(dirpath);
  if(rr == EOK)
    log_info("ext4: created dir %s", dirpath);
  else
    log_warn("ext4: mkdir failed %d for %s", rr, dirpath);
}

// Avoid infinite loops when resolving symlinks.
#define EXT4_MAX_SYMLINKS 8

// lwext4 is configured to call these memory hooks.
// 通过内核分配器为 lwext4 分配内存。
void *ext4_user_malloc(size_t size) { return kmalloc(size); }

// 使用 kmalloc 为 lwext4 分配清零内存。
void *ext4_user_calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  void *p = kmalloc(total);
  if(p)
    memset(p, 0, total);
  return p;
}

// 通过复制到新的 kmalloc 缓冲区实现重新分配。
void *ext4_user_realloc(void *ptr, size_t size) {
  void *p = kmalloc(size);
  if(!p)
    return 0;
  if(ptr)
    memmove(p, ptr, size);
  if(ptr)
    kmfree(ptr);
  return p;
}

// 释放 lwext4 钩子分配的内存。
void ext4_user_free(void *ptr) {
  if(ptr)
    kmfree(ptr);
}

// Local helpers -------------------------------------------------------------
extern struct inode* iget_pub(uint dev, uint inum);

// lwext4 块设备的加锁桩函数。
static int bdev_lock(struct ext4_blockdev *bdev) { return EOK; }
// lwext4 块设备的解锁桩函数。
static int bdev_unlock(struct ext4_blockdev *bdev) { return EOK; }
// lwext4 块设备的打开桩函数。
static int bdev_open(struct ext4_blockdev *bdev) { return EOK; }
// lwext4 块设备的关闭桩函数。
static int bdev_close(struct ext4_blockdev *bdev) { return EOK; }

// 将扇区 LBA 转为对应的 xv6 块号。
static inline uint sector_block(uint64 lba) {
  return lba / (BSIZE / EXT4_SECTOR_SIZE);
}

// 计算扇区在其 xv6 块内的字节偏移。
static inline uint sector_offset(uint64 lba) {
  return (lba % (BSIZE / EXT4_SECTOR_SIZE)) * EXT4_SECTOR_SIZE;
}

// 通过读取 xv6 缓冲区块为 lwext4 提供扇区读。
static int bdev_read(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id,
                     uint32_t blk_cnt) {
  uint8 *dst = (uint8 *)buf;
  for(uint32 i = 0; i < blk_cnt; i++){
    uint64 lba = blk_id + i;
    uint blk = sector_block(lba);
    uint off = sector_offset(lba);
    struct buf *bp = bread(ext4_devno, blk);
    memmove(dst + i * EXT4_SECTOR_SIZE, bp->data + off, EXT4_SECTOR_SIZE);
    brelse(bp);
  }
  return EOK;
}

// 通过 xv6 缓冲区块为 lwext4 提供扇区写。
static int bdev_write(struct ext4_blockdev *bdev, const void *buf, uint64_t blk_id,
                      uint32_t blk_cnt) {
  const uint8 *src = (const uint8 *)buf;
  for(uint32 i = 0; i < blk_cnt; i++){
    uint64 lba = blk_id + i;
    uint blk = sector_block(lba);
    uint off = sector_offset(lba);
    struct buf *bp = bread(ext4_devno, blk);
    memmove(bp->data + off, src + i * EXT4_SECTOR_SIZE, EXT4_SECTOR_SIZE);
    bwrite(bp);
    brelse(bp);
  }
  return EOK;
}

// 以基路径规范化可能的相对路径。
static void resolve_path(const char *base, const char *path, char *out, int outlen) {
  const char *src;
  const char *parts[64];
  int depth = 0;

  if(path[0] == '/'){
    src = path;
  } else {
    if(base[0] == '\0'){
      parts[depth++] = "";
    } else if(base[0] == '/' && base[1] == '\0'){
      // root
    } else {
      const char *p = base;
      while(*p){
        while(*p == '/') p++;
        if(!*p) break;
        const char *start = p;
        while(*p && *p != '/') p++;
        if(depth < 64)
          parts[depth++] = start;
      }
    }
    src = path;
  }

  while(*src){
    while(*src == '/') src++;
    if(!*src) break;
    const char *start = src;
    while(*src && *src != '/') src++;
    int len = src - start;
    if(len == 0) continue;

    if(len == 1 && start[0] == '.')
      continue;
    if(len == 2 && start[0] == '.' && start[1] == '.'){
      if(depth > 0)
        depth--;
      continue;
    }
    if(depth < 64)
      parts[depth++] = start;
  }

  int pos = 0;
  if(depth == 0){
    if(outlen > 1){
      out[0] = '/';
      out[1] = '\0';
    } else if(outlen > 0){
      out[0] = '\0';
    }
    return;
  }

  out[0] = '\0';
  for(int i = 0; i < depth; i++){
    if(pos + 1 >= outlen) break;
    out[pos++] = '/';
    const char *start = parts[i];
    const char *end = start;
    while(*end && *end != '/') end++;
    while(start < end && pos + 1 < outlen){
      out[pos++] = *start++;
    }
  }
  if(pos < outlen)
    out[pos] = '\0';
  else if(outlen > 0)
    out[outlen-1] = '\0';
}

// Extract directory path from an absolute path ("/a/b/c" -> "/a/b").
static void dirname_from_full(const char *full, char *out, int outlen) {
  int len = strlen(full);
  if(len <= 1){
    safestrcpy(out, "/", outlen);
    return;
  }
  int i = len - 1;
  while(i > 0 && full[i] != '/')
    i--;
  if(i <= 0){
    safestrcpy(out, "/", outlen);
    return;
  }
  if(i >= outlen)
    i = outlen - 1;
  memmove(out, full, i);
  out[i] = '\0';
}

// 为 ext4 路径/ino 构造一个 xv6 inode 包装。
static struct inode *make_inode(const char *path, short type, uint64 size, uint64 inum) {
  struct inode *ip = iget_pub(ext4_devno, inum ? (uint)inum : 1);
  ip->type = type;
  ip->major = EXT4_INODE_TAG;
  ip->minor = 0;
  ip->nlink = 1;
  ip->size = size > 0xFFFFFFFF ? 0xFFFFFFFF : (uint)size;
  ip->ext_size = size;
  ip->ext_ino = inum;
  safestrcpy(ip->ext4_path, path, sizeof(ip->ext4_path));
  ip->valid = 1;
  return ip;
}

// 为 ext4 路径创建伪造的设备 inode（如 console）。
static struct inode *make_device_inode(uint dev, short major, short minor) {
  struct inode *ip = iget_pub(dev, 0);
  ip->type = T_DEVICE;
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  ip->size = 0;
  ip->ext_ino = 0;
  ip->ext_size = 0;
  ip->ext4_path[0] = 0;
  ip->valid = 1;
  return ip;
}

// Public API ----------------------------------------------------------------

// 初始化 lwext4、注册块设备并挂载根目录。
void ext4fs_init(int dev) {
  ext4_mode = 0;
  ext4_devno = dev;

  memset(&ext4_iface, 0, sizeof(ext4_iface));
  ext4_iface.lock = bdev_lock;
  ext4_iface.unlock = bdev_unlock;
  ext4_iface.open = bdev_open;
  ext4_iface.bread = bdev_read;
  ext4_iface.bwrite = bdev_write;
  ext4_iface.close = bdev_close;
  ext4_iface.ph_bsize = EXT4_SECTOR_SIZE;
  ext4_iface.ph_bbuf = ext4_bbuf;

  // Use a large logical span so lwext4 doesn't reject high block numbers.
  uint64 part_size = 8ull * 1024ull * 1024ull * 1024ull; // 8 GiB span
  ext4_iface.ph_bcnt = part_size / EXT4_SECTOR_SIZE;

  memset(&ext4_bd, 0, sizeof(ext4_bd));
  ext4_bd.bdif = &ext4_iface;
  ext4_bd.part_offset = 0;
  ext4_bd.part_size = part_size;

  ext4_device_unregister_all();
  int r = ext4_device_register(&ext4_bd, EXT4_DEV_NAME);
  if(r != EOK){
    log_warn("ext4: device_register failed %d\n", r);
    return;
  }

  r = ext4_mount(EXT4_DEV_NAME, "/", false);
  if(r != EOK){
    log_warn("ext4: mount failed %d\n", r);
    ext4_device_unregister(EXT4_DEV_NAME);
    return;
  }

  ext4_mode = 1;
  log_info("ext4: mounted device %s\n", EXT4_DEV_NAME);

  // Ensure essential applets are reachable even if the image lacks symlinks.
  // We don't modify the image contents; we create runtime symlinks via lwext4.
  // This specifically addresses the missing 'basename' used by ltp_testcode.sh.
  const char *bb_candidates[] = { "/bin/busybox", "/usr/bin/busybox", "/musl/busybox", "/busybox" };
  char bb_path[MAXPATH];
  bb_path[0] = '\0';
  for(int i = 0; i < (int)(sizeof(bb_candidates)/sizeof(bb_candidates[0])); i++){
    if(path_exists(bb_candidates[i])){
      safestrcpy(bb_path, bb_candidates[i], sizeof(bb_path));
      break;
    }
  }
  if(bb_path[0] == '\0'){
    log_warn("ext4: busybox not found; cannot create applet symlinks\n");
    return;
  }

  // Create common locations for 'basename'.
  ensure_dir("/bin");
  ensure_dir("/usr");
  ensure_dir("/usr/bin");
  // Common shells and applets
  ensure_symlink("/bin/sh", bb_path);
  ensure_symlink("/bin/basename", bb_path);
  ensure_symlink("/usr/bin/basename", bb_path);
  ensure_symlink("/musl/basename", bb_path);

  // Ensure dynamic loader paths for musl-linked binaries.
  // Many binaries reference /lib/ld-musl-riscv64.so.1. On this image, musl ships libc.so as the loader.
  ensure_dir("/lib");
  ensure_dir("/musl/lib");
  // Create symlinks pointing loader name to libc.so so exec can open it.
  ensure_symlink("/musl/lib/ld-musl-riscv64.so.1", "/musl/lib/libc.so");
  ensure_symlink("/lib/ld-musl-riscv64.so.1", "/musl/lib/libc.so");
}

// Resolve ext4 path (absolute) and follow symlinks.
static struct inode* ext4_namei_internal(const char *full, int depth) {
  if(depth > EXT4_MAX_SYMLINKS)
    return 0;

  // procfs interception: handle /proc/* paths via procfs
  if(full && full[0] == '/' && full[1] == 'p' && full[2] == 'r' && full[3] == 'o' && full[4] == 'c'){
    struct inode *pip = procfs_namei((char *)full);
    if(pip) return pip;
  }

  // special device: console (accept "console" or "/dev/console")
  if(strcmp(full, "console") == 0 || strcmp(full, "/console") == 0 ||
     strcmp(full, "/dev/console") == 0){
    return make_device_inode(ext4_devno, CONSOLE, 0);
  }

  ext4_dir dir;
  if(ext4_dir_open(&dir, full) == EOK){
    uint64 inum = dir.f.inode;
    uint64 sz = dir.f.fsize;
    ext4_dir_close(&dir);
    return make_inode(full, T_DIR, sz, inum);
  }

  ext4_file f;
  if(ext4_fopen2(&f, full, O_RDONLY) == EOK){
    struct inode *ip = make_inode(full, T_FILE, f.fsize, f.inode);
    ext4_fclose(&f);
    return ip;
  }

  // Try symlink resolution.
  char linkbuf[MAXPATH];
  size_t rcnt = 0;
  if(ext4_readlink(full, linkbuf, sizeof(linkbuf) - 1, &rcnt) == EOK){
    linkbuf[rcnt] = '\0';
    char base[MAXPATH];
    char resolved[MAXPATH];
    if(linkbuf[0] == '/'){
      safestrcpy(resolved, linkbuf, sizeof(resolved));
    } else {
      dirname_from_full(full, base, sizeof(base));
      resolve_path(base, linkbuf, resolved, sizeof(resolved));
    }
    return ext4_namei_internal(resolved, depth + 1);
  }

  return 0;
}

// 解析 ext4 路径并返回对应的 xv6 inode 包装。
struct inode* ext4_namei(char *path) {
  if(!ext4_mode || !path)
    return 0;

  char full[MAXPATH];
  struct proc *p = myproc();
  const char *cwd = (p && p->cwdpath[0]) ? p->cwdpath : "/";
  resolve_path(cwd, path, full, sizeof(full));

  return ext4_namei_internal(full, 0);
}

// 类似 ext4_namei，但相对路径从给定目录 inode 开始解析。
struct inode* ext4_nameiat(struct inode *base, char *path) {
  if(!ext4_mode || !path)
    return 0;

  if(path[0] == '/')
    return ext4_namei(path);

  char full[MAXPATH];
  const char *start = "/";
  struct proc *p = myproc();
  if(base && base->major == EXT4_INODE_TAG){
    start = base->ext4_path;
  } else if(p && p->cwdpath[0]){
    start = p->cwdpath;
  }
  resolve_path(start, path, full, sizeof(full));

  if(strcmp(full, "console") == 0 || strcmp(full, "/console") == 0 ||
     strcmp(full, "/dev/console") == 0){
    return make_device_inode(ext4_devno, CONSOLE, 0);
  }
  return ext4_namei_internal(full, 0);
}

// 从 ext4 inode 读取数据到用户或内核内存。
int ext4_readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n) {
  if(!ext4_mode || !ip || ip->major != EXT4_INODE_TAG)
    return -1;

  ext4_file f;
  memset(&f, 0, sizeof(f));
  int fr = ext4_fopen2(&f, ip->ext4_path, O_RDONLY);
  if(fr != EOK){
    log_error("ext4_readi: fopen '%s' failed %d", ip->ext4_path, fr);
    return -1;
  }
  // log_info("ext4_readi: open '%s' inode 0x%x size 0x%x off %d len %d\n", ip->ext4_path, (uint)f.inode, (uint)f.fsize, (int)off, (int)n);

  if(off > f.fsize){
    ext4_fclose(&f);
    return -1;
  }
  if(off + n > f.fsize)
    n = f.fsize - off;

  if(n == 0){
    ext4_fclose(&f);
    return 0;
  }

  if(ext4_fseek(&f, off, SEEK_SET) != EOK){
    ext4_fclose(&f);
    return -1;
  }

  char *kbuf = (char *)kmalloc(n);
  if(!kbuf){
    ext4_fclose(&f);
    return -1;
  }

  size_t rcnt = 0;
  int r = ext4_fread(&f, kbuf, n, &rcnt);
  ext4_fclose(&f);
  if(rcnt == 0){
    log_error("ext4_readi: fread '%s' off %d len %d ret %d rcnt %d\n", ip->ext4_path, (int)off, (int)n, r, (int)rcnt);
    kmfree(kbuf);
    return -1;
  }
  if(r != EOK){
    log_warn("ext4_readi: fread nonzero ret %d but rcnt %d for '%s'\n", r, (int)rcnt, ip->ext4_path);
  }

  if(either_copyout(user_dst, dst, kbuf, rcnt) < 0){
    log_error("ext4_readi: copyout failed for '%s' rcnt %d", ip->ext4_path, rcnt);
    kmfree(kbuf);
    return -1;
  }

  kmfree(kbuf);
  return rcnt;
}

// 向 ext4 inode 写入数据，并在需要时更新长度元数据。
int ext4_writei(struct inode *ip, int user_src, uint64 src, uint off, uint n) {
  if(!ext4_mode || !ip || ip->major != EXT4_INODE_TAG)
    return -1;

  ext4_file f;
  memset(&f, 0, sizeof(f));
  if(ext4_fopen2(&f, ip->ext4_path, O_RDWR) != EOK)
    return -1;

  if(ext4_fseek(&f, off, SEEK_SET) != EOK){
    ext4_fclose(&f);
    return -1;
  }

  char *kbuf = (char *)kmalloc(n);
  if(!kbuf){
    ext4_fclose(&f);
    return -1;
  }

  if(either_copyin(kbuf, user_src, src, n) < 0){
    kmfree(kbuf);
    ext4_fclose(&f);
    return -1;
  }

  size_t wcnt = 0;
  int r = ext4_fwrite(&f, kbuf, n, &wcnt);
  ext4_fclose(&f);
  kmfree(kbuf);

  if(r != EOK)
    return -1;

  uint64 end = off + wcnt;
  if(end > ip->ext_size){
    ip->ext_size = end;
    ip->size = end > 0xFFFFFFFF ? 0xFFFFFFFF : (uint)end;
  }
  return wcnt;
}

// 将 ext4 文件截断为零长度。
int ext4_truncate(struct inode *ip) {
  if(!ext4_mode || !ip || ip->major != EXT4_INODE_TAG)
    return -1;

  ext4_file f;
  if(ext4_fopen2(&f, ip->ext4_path, O_RDWR) != EOK)
    return -1;
  int r = ext4_ftruncate(&f, 0);
  ext4_fclose(&f);
  if(r != EOK)
    return -1;
  ip->ext_size = 0;
  ip->size = 0;
  return 0;
}

// 将 ext4 文件截断/扩展到指定长度。
int ext4_truncate_to(struct inode *ip, uint64 size) {
  if(!ext4_mode || !ip || ip->major != EXT4_INODE_TAG)
    return -1;

  ext4_file f;
  if(ext4_fopen2(&f, ip->ext4_path, O_RDWR) != EOK)
    return -1;
  int r = ext4_ftruncate(&f, size);
  ext4_fclose(&f);
  if(r != EOK)
    return -1;
  ip->ext_size = size;
  ip->size = size > 0xFFFFFFFF ? 0xFFFFFFFF : (uint)size;
  return 0;
}

// Create a symlink at path pointing to target.
int ext4_symlink(const char *target, char *path) {
  if(!ext4_mode || !target || !path)
    return -1;

  char full[MAXPATH];
  struct proc *p = myproc();
  const char *cwd = (p && p->cwdpath[0]) ? p->cwdpath : "/";
  resolve_path(cwd, path, full, sizeof(full));

  int r = ext4_fsymlink(target, full);
  return (r == EOK) ? 0 : -1;
}

// Create a symlink at path relative to dp (directory inode).
int ext4_symlinkat(struct inode *dp, const char *target, char *path) {
  if(!ext4_mode || !target || !path)
    return -1;

  if(path[0] == '/')
    return ext4_symlink(target, path);

  char full[MAXPATH];
  const char *base = "/";
  struct proc *p = myproc();
  if(dp && dp->major == EXT4_INODE_TAG){
    base = dp->ext4_path;
  } else if(p && p->cwdpath[0]){
    base = p->cwdpath;
  }
  resolve_path(base, path, full, sizeof(full));

  int r = ext4_fsymlink(target, full);
  return (r == EOK) ? 0 : -1;
}

// 在给定目录 inode 的相对路径下创建文件或目录。
struct inode* ext4_createat(struct inode *dp, char *name, short type, short major, short minor) {
  (void)major;
  (void)minor;
  if(!ext4_mode || !name)
    return 0;

  char full[MAXPATH];
  const char *base = "/";
  struct proc *p = myproc();
  if(dp && dp->major == EXT4_INODE_TAG){
    base = dp->ext4_path;
  } else if(p && p->cwdpath[0]){
    base = p->cwdpath;
  }
  resolve_path(base, name, full, sizeof(full));

  int r = -1;
  if(type == T_DIR){
    r = ext4_dir_mk(full);
  } else if(type == T_FILE){
    ext4_file f;
    r = ext4_fopen2(&f, full, O_RDWR | O_CREAT | O_TRUNC);
    if(r == EOK)
      ext4_fclose(&f);
  } else {
    return 0;
  }

  if(r != EOK)
    return 0;

  return ext4_namei(full);
}

// 将 lwext4 目录项类型映射为 linux_dirent64 的类型值。
static uint8 map_dir_type(uint8 t) {
  switch(t){
    case EXT4_DE_DIR: return 4;      // DT_DIR
    case EXT4_DE_REG_FILE: return 8; // DT_REG
    case EXT4_DE_SYMLINK: return 10; // DT_LNK
    default: return 0;
  }
}

// 为 getdents64 读取目录项并写入 linux_dirent64 数组。
int ext4_getdents64(struct inode *dp, uint *offp, uint64 uaddr, uint64 maxlen) {
  if(!ext4_mode || !dp || dp->major != EXT4_INODE_TAG || dp->type != T_DIR)
    return -1;
  struct proc *p = myproc();
  if(!p)
    return -1;

  // log_debug("ext4_getdents64: start off=%u maxlen=%p path=%s\n",
  //           offp ? *offp : 0, (void *)maxlen, dp->ext4_path);

  ext4_dir dir;
  if(ext4_dir_open(&dir, dp->ext4_path) != EOK)
    return -1;
  // 使用文件偏移作为 ext4 目录偏移，避免每次从头跳过导致 O(n^2)
  uint64 start_off = offp ? *offp : 0;
  uint64 dir_size = (dp->ext_size > 0) ? dp->ext_size : dir.f.fsize;
  // 如果偏移已超过目录大小，直接返回 EOF
  if(start_off >= dir_size){
    log_warn("ext4_getdents64:偏移超过目录大小, start_off = %ld, fsize = %ld\n", start_off, dir_size);
    ext4_dir_close(&dir);
    return 0;
  }
  dir.next_off = start_off;

  // 变长 linux_dirent64：
  // header 布局为 d_ino(8) + d_off(8) + d_reclen(2) + d_type(1) + d_name[]
  // 其中 d_name 以 '\0' 结尾，总记录长度按 8 字节对齐。
  int written = 0;
  const ext4_direntry *de;
  while((de = ext4_dir_entry_next(&dir)) != 0){
    int namelen = (de->name_length > 0) ? de->name_length : 0;
    // 不截断名称，按真实长度返回
    // 计算记录长度：19(头部) + namelen + 1(终止符)，并按 8 对齐
    int reclen = 19 + namelen + 1;
    reclen = (reclen + 7) & ~7;
    if(written + reclen > (int)maxlen)
      break;

    uint64 entry_uaddr = uaddr + written;
    uint64 d_ino = de->inode;
    // 使用 lwext4 的 next_off 作为 d_off；若为 -1 则用目录大小表示 EOF
    uint64 next_off = (dir.next_off == (uint64)-1) ? dir_size : dir.next_off;
    uint16 d_reclen = (uint16)reclen;
    uint8 d_type = map_dir_type(de->inode_type);

    // 写入头部字段
    if(copyout(p->pagetable, entry_uaddr + 0, (char *)&d_ino, sizeof(d_ino)) < 0)
      goto copy_fail;
    if(copyout(p->pagetable, entry_uaddr + 8, (char *)&next_off, sizeof(next_off)) < 0)
      goto copy_fail;
    if(copyout(p->pagetable, entry_uaddr + 16, (char *)&d_reclen, sizeof(d_reclen)) < 0)
      goto copy_fail;
    if(copyout(p->pagetable, entry_uaddr + 18, (char *)&d_type, sizeof(d_type)) < 0)
      goto copy_fail;

    // 写入名称和终止符
    if(namelen > 0){
      if(copyout(p->pagetable, entry_uaddr + 19, (char *)de->name, namelen) < 0)
        goto copy_fail;
    }
    char nul = 0;
    if(copyout(p->pagetable, entry_uaddr + 19 + namelen, &nul, 1) < 0)
      goto copy_fail;

    // 将对齐填充区域置零（如果有）
    int pad = reclen - (19 + namelen + 1);
    if(pad > 0){
      // 为了避免多次系统调用，限制 pad 较小场景下的零填充；pad 最大为 7
      char zeros[8] = {0};
      if(copyout(p->pagetable, entry_uaddr + 19 + namelen + 1, zeros, pad) < 0)
        goto copy_fail;
    }

    written += reclen;
    continue;

copy_fail:
    ext4_dir_close(&dir);
    return -1;
  }

  ext4_dir_close(&dir);
  if(offp){
    if(dir.next_off == (uint64)-1)
      *offp = dir_size;
    else
      *offp = dir.next_off;
  }
  log_debug("ext4_getdents64: end off=%u written=%d\n", offp ? *offp : 0, written);
  return written;
}

// 删除指定路径的文件或目录。
int ext4_unlink_path(const char *path, int is_dir) {
  if(!ext4_mode || !path)
    return -1;

  char full[MAXPATH];
  struct proc *p = myproc();
  const char *base = (p && p->cwdpath[0]) ? p->cwdpath : "/";
  resolve_path(base, path, full, sizeof(full));

  int r = is_dir ? ext4_dir_rm(full) : ext4_fremove(full);
  return (r == EOK) ? 0 : -1;
}
