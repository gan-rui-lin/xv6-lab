#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "errno.h"
#include "procfs.h"
#include <stddef.h>

static struct inode *make_proc_inode(uint dev, short type, uint size) {
  struct inode *ip = iget_pub(dev, 1);
  ip->type = type;
  ip->major = PROCFS_INODE_TAG;
  ip->minor = 0;
  ip->nlink = 1;
  ip->size = size;
  ip->ext_ino = 0;
  ip->ext_size = 0;
  ip->ext4_path[0] = 0;
  ip->valid = 1;
  return ip;
}

static int append_str(char *buf, int buflen, int n, const char *s)
{
  for(; *s && n < buflen; s++) buf[n++] = *s;
  return n;
}

static int append_u64_dec(char *buf, int buflen, int n, unsigned long long v)
{
  char tmp[32];
  int t = 0;
  if(v == 0) tmp[t++] = '0';
  while(v > 0 && t < (int)sizeof(tmp)) { tmp[t++] = '0' + (v % 10ULL); v /= 10ULL; }
  for(int i = t-1; i >= 0 && n < buflen; i--) buf[n++] = tmp[i];
  return n;
}

static int build_meminfo(char *buf, int buflen)
{
  // Rough totals based on physical memory span
  unsigned long long total_bytes = (unsigned long long)((uint64)PHYSTOP - (uint64)KERNBASE);
  unsigned long long total_kb = total_bytes / 1024ULL;
  unsigned long long free_kb = total_kb / 2ULL;
  int n = 0;
  n = append_str(buf, buflen, n, "MemTotal:     ");
  n = append_u64_dec(buf, buflen, n, total_kb);
  n = append_str(buf, buflen, n, " kB\n");
  n = append_str(buf, buflen, n, "MemFree:      ");
  n = append_u64_dec(buf, buflen, n, free_kb);
  n = append_str(buf, buflen, n, " kB\n");
  n = append_str(buf, buflen, n, "MemAvailable: ");
  n = append_u64_dec(buf, buflen, n, free_kb);
  n = append_str(buf, buflen, n, " kB\n");
  n = append_str(buf, buflen, n, "Buffers: 0 kB\n");
  n = append_str(buf, buflen, n, "Cached: 0 kB\n");
  n = append_str(buf, buflen, n, "SwapTotal: 0 kB\n");
  n = append_str(buf, buflen, n, "SwapFree: 0 kB\n");
  if(n > buflen) n = buflen;
  return n;
}

// Build /proc/mounts content
static int build_mounts(char *buf, int buflen)
{
  int n = 0;
  // Standard mount entries that df expects
  n = append_str(buf, buflen, n, "/dev/root / ext4 rw,relatime 0 0\n");
  n = append_str(buf, buflen, n, "proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0\n");
  n = append_str(buf, buflen, n, "tmpfs /tmp tmpfs rw,nosuid,nodev 0 0\n");
  if(n > buflen) n = buflen;
  return n;
}

// Procfs file types for inode identification
#define PROCFS_MEMINFO  1
#define PROCFS_MOUNTS   2

struct inode*
procfs_namei(char *full)
{
  if(!full) return 0;
  // Support /proc/meminfo
  if(strncmp(full, "/proc/meminfo", 13) == 0 && (full[13] == '\0' || full[13] == '/')){
    char tmp[256];
    int len = build_meminfo(tmp, sizeof(tmp));
    struct inode *ip = make_proc_inode(ROOTDEV, T_FILE, (uint)len);
    ip->minor = PROCFS_MEMINFO;
    return ip;
  }
  // Support /proc/mounts
  if(strncmp(full, "/proc/mounts", 12) == 0 && (full[12] == '\0' || full[12] == '/')){
    char tmp[256];
    int len = build_mounts(tmp, sizeof(tmp));
    struct inode *ip = make_proc_inode(ROOTDEV, T_FILE, (uint)len);
    ip->minor = PROCFS_MOUNTS;
    return ip;
  }
  // Optionally, recognize /proc as a directory
  if(strncmp(full, "/proc", 5) == 0 && full[5] == '\0'){
    return make_proc_inode(ROOTDEV, T_DIR, 0);
  }
  return 0;
}

int
procfs_readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  if(!ip || ip->major != PROCFS_INODE_TAG)
    return -1;
  // Only handle files (no dir read implemented)
  if(ip->type == T_DIR)
    return -1;

  char *kbuf = (char *)kalloc();
  if(!kbuf)
    return -ENOMEM;
  
  int len = 0;
  switch(ip->minor) {
    case PROCFS_MEMINFO:
      len = build_meminfo(kbuf, PGSIZE);
      break;
    case PROCFS_MOUNTS:
      len = build_mounts(kbuf, PGSIZE);
      break;
    default:
      kfree(kbuf);
      return -1;
  }
  
  if(off >= (uint)len){
    kfree(kbuf);
    return 0;
  }
  if(off + n > (uint)len)
    n = len - off;
  int r = either_copyout(user_dst, dst, kbuf + off, n);
  kfree(kbuf);
  if(r < 0)
    return -EFAULT;
  return n;
}
