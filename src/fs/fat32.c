// Minimal FAT32 read-only support to enable exec()
// - Path lookup with short 8.3 names (uppercase)
// - File reading via cluster chain
// - Uses existing bread() (1024-byte blocks) to fetch 512-byte sectors

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs/fat32.h"
// local min macro
#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#include "sleeplock.h" // TODO 和 fs/file.h 捆绑着引入
#include "fs/fs.h"     // TODO 和 fs/file.h 捆绑着引入
#include "fs/file.h"
#include "fs/stat.h"
#include "buf.h"

// FAT32 BPB minimal fields
static struct {
  uint dev;
  uint16 bps;           // bytes per sector
  uint8 spc;            // sectors per cluster
  uint16 rsvd_secs;     // reserved sectors
  uint8 nfats;          // number of FATs
  uint32 fatsz32;       // sectors per FAT
  uint32 totsec32;      // total sectors
  uint32 root_clus;     // root directory cluster
  uint32 first_data_sec;// first data sector (LBA)
  uint32 fat_start_sec; // FAT start sector (LBA)
} fat;

// fat32_mode is defined in fs/fs.c

static void read_sector512(uint dev, uint32 lba, uint8 *dst)
{
  // Map 512-byte sector to 1024-byte bread() block
  uint blk = lba / (BSIZE / 512); // BSIZE=1024 -> divisor 2
  uint offs = (lba % (BSIZE / 512)) * 512;
  struct buf *bp = bread(dev, blk);
  memmove(dst, bp->data + offs, 512);
  brelse(bp);
}

static uint32 clus_to_sec(uint32 clus)
{
  if(clus < 2){
    log_warn("clus_to_sec: illegal cluster %d", clus);
    clus = 2;
  }
  return fat.first_data_sec + (clus - 2) * fat.spc;
}

static uint32 fat_next_clus(uint32 clus)
{
  // FAT32 entry is 4 bytes
  uint32 offset_bytes = clus * 4;
  uint32 fat_sec = fat.fat_start_sec + (offset_bytes / fat.bps);
  uint32 sec_off = offset_bytes % fat.bps;
  uint8 sec[512];
  read_sector512(fat.dev, fat_sec, sec);
  uint32 val = *(uint32 *)(sec + sec_off);
  val &= 0x0FFFFFFF; // 28-bit value
  return val;
}

// Compare path component against short 8.3 dir entry name
static int match_sfn(const char *comp, const uint8 *name11)
{
  // Build comp in 8.3 uppercase padded with spaces
  char sfn[11];
  int i = 0, j = 0;
  // name part
  while(comp[i] && comp[i] != '.' && j < 8){
    char c = comp[i];
    if(c >= 'a' && c <= 'z') c -= 32;
    sfn[j++] = c;
    i++;
  }
  while(j < 8) sfn[j++] = ' ';
  // ext part
  if(comp[i] == '.') i++;
  int k = 0;
  while(comp[i] && k < 3){
    char c = comp[i];
    if(c >= 'a' && c <= 'z') c -= 32;
    sfn[8 + k] = c;
    k++; i++;
  }
  while(k < 3) sfn[8 + k++] = ' ';
  // compare
  for(int t=0; t<11; ++t){
    if((uint8)sfn[t] != name11[t]) return 0;
  }
  return 1;
}

// Find a path component within a directory cluster chain.
// Returns 0 if not found; else fills type,size,start cluster.
static int dir_find(uint32 dir_clus, const char *comp, short *out_type, uint32 *out_size, uint32 *out_clus)
{
  uint32 cl = dir_clus;
  for(;;){
    uint32 sec = clus_to_sec(cl);
    for(uint s=0; s<fat.spc; ++s){
      uint8 buf[512];
      read_sector512(fat.dev, sec + s, buf);
      // iterate entries
      for(int off=0; off<512; off+=32){
        uint8 *de = buf + off;
        uint8 first = de[0];
        if(first == 0x00) // end of dir
          return 0;
        if(first == 0xE5) // deleted
          continue;
        uint8 attr = de[11];
        if(attr == 0x0F) // long name entry
          continue;
        // match name
        if(match_sfn(comp, de)){
          uint16 cl_hi = *(uint16 *)(de + 20);
          uint16 cl_lo = *(uint16 *)(de + 26);
          uint32 startc = ((uint32)cl_hi << 16) | cl_lo;
          uint32 fsz = *(uint32 *)(de + 28);
          short type = (attr & 0x10) ? T_DIR : T_FILE;
          *out_type = type;
          *out_size = fsz;
          *out_clus = startc;
          return 1;
        }
      }
    }
    // next cluster in chain
    uint32 nxt = fat_next_clus(cl);
    if(nxt >= 0x0FFFFFF8 || nxt == 0x0FFFFFFF){
      break;
    }
    if(nxt < 2){
      log_warn("dir_find: bad next cluster %x", nxt);
      break;
    }
    cl = nxt;
  }
  return 0;
}

// Split path into components; returns count, fills arr with pointers within buf
static int split_path(const char *path, char **arr, int max, char *work, int wlen)
{
  int n=0; int i=0; int L = strlen(path);
  while(i < L){
    while(i < L && path[i] == '/') i++;
    if(i >= L) break;
    int start = i;
    while(i < L && path[i] != '/') i++;
    int len = i - start;
    if(len <= 0) continue;
    if(n >= max){ log_warn("split_path: too many comps"); break; }
    if(len+1 > wlen){ log_warn("split_path: work buf overflow"); break; }
    memmove(work, path + start, len);
    work[len] = 0;
    arr[n++] = work;
    work += len + 1; wlen -= (len + 1);
  }
  return n;
}

void fat32_init(int dev)
{
  // Read sector 0 BPB
  uint8 sec0[512];
  fat.dev = dev;
  read_sector512(dev, 0, sec0);
  uint16 bps = *(uint16 *)(sec0 + 11);
  uint8  spc = *(uint8  *)(sec0 + 13);
  uint16 rsvd = *(uint16 *)(sec0 + 14);
  uint8  nf = *(uint8  *)(sec0 + 16);
  uint32 fatsz32 = *(uint32 *)(sec0 + 36);
  uint32 totsec32 = *(uint32 *)(sec0 + 32);
  uint32 rootclus = *(uint32 *)(sec0 + 44);
  // heuristic: check bytes/sector & FAT32 signature area (just sanity)
  if(bps != 512 || spc == 0 || nf == 0 || fatsz32 == 0){
    log_warn("fat32_init: BPB invalid bps=%d spc=%d nf=%d fatsz=%d", bps, spc, nf, fatsz32);
    fat32_mode = 0;
    return;
  }
  fat.bps = bps;
  fat.spc = spc;
  fat.rsvd_secs = rsvd;
  fat.nfats = nf;
  fat.fatsz32 = fatsz32;
  fat.totsec32 = totsec32;
  fat.root_clus = rootclus;
  fat.fat_start_sec = fat.rsvd_secs;
  fat.first_data_sec = fat.rsvd_secs + fat.nfats * fat.fatsz32;
  fat32_mode = 1;
  log_info("FAT32: bps=%d spc=%d rsvd=%d nfats=%d fatsz=%d root=%x", bps, spc, rsvd, nf, fatsz32, rootclus);
}

static struct inode *make_device_inode(uint dev, short major, short minor)
{
  struct inode *ip = iget_pub(dev, 0); // synthetic key 0 for device
  ip->type = T_DEVICE;
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  ip->size = 0;
  for(int i=0;i<NDIRECT+1;i++) ip->addrs[i] = 0;
  ip->valid = 1;
  return ip;
}

// Create a synthetic inode via iget and stamp FAT32 fields
// iget wrapper from fs.c
extern struct inode* iget_pub(uint dev, uint inum);

static struct inode *make_inode(uint dev, short type, uint32 size, uint32 start_clus)
{
  // use cluster number as synthetic inum key (not on-disk inode)
  struct inode *ip = iget_pub(dev, start_clus ? start_clus : 1);
  // Initialize basic fields
  ip->type = type;
  ip->major = FAT32_INODE_TAG;
  ip->minor = 0;
  ip->nlink = 1;
  ip->size = size;
  for(int i=0;i<NDIRECT+1;i++) ip->addrs[i] = 0;
  ip->addrs[0] = start_clus; // stash start cluster
  ip->valid = 1; // prevent xv6 ilock from disk-read
  return ip;
}

struct inode* fat32_namei(char *path)
{
  if(!path || !*path){
    log_warn("fat32_namei: empty path");
    return 0;
  }
  if(!fat32_mode){
    log_warn("fat32_namei: fat32 disabled");
    return 0;
  }
  // Split path
  char workbuf[MAXPATH];
  char *comps[32];
  int n = split_path(path, comps, 32, workbuf, sizeof(workbuf));
  // base: root for absolute; cwd for relative
  uint32 cur = (path[0] == '/') ? fat.root_clus : fat.root_clus;
  if(path[0] != '/'){
    struct proc *p = myproc();
    if(p && p->cwd){
      uint32 cwdclus = p->cwd->addrs[0];
      if(cwdclus != 0) cur = cwdclus;
    }
  }
  short cur_type = T_DIR;
  uint32 cur_size = 0;
  if(n == 0){
    return make_inode(fat.dev, T_DIR, 0, cur);
  }
  // 单个 "." 解析为当前工作目录
  if(n == 1 && comps[0][0] == '.' && comps[0][1] == 0){
    struct proc *p = myproc();
    if(p && p->cwd){
      uint32 cwdclus = p->cwd->addrs[0];
      if(cwdclus == 0) cwdclus = fat.root_clus;
      return make_inode(fat.dev, T_DIR, 0, cwdclus);
    }
    return make_inode(fat.dev, T_DIR, 0, fat.root_clus);
  }
  // special device: console
  if(n == 1){
    // case-insensitive check
    char *p0 = comps[0];
    int ok = 0;
    if(strlen(p0) == 7){
      char tmp[8];
      for(int i=0;i<7;i++){ char c=p0[i]; if(c>='a'&&c<='z') c-=32; tmp[i]=c; }
      tmp[7]=0;
      if(tmp[0]=='C'&&tmp[1]=='O'&&tmp[2]=='N'&&tmp[3]=='S'&&tmp[4]=='O'&&tmp[5]=='L'&&tmp[6]=='E') ok=1;
    }
    if(ok){
      return make_device_inode(fat.dev, CONSOLE, 0);
    }
  }
  for(int i=0;i<n;i++){
    // skip '.' components
    if(comps[i][0] == '.' && comps[i][1] == 0){
      continue;
    }
    short typ; uint32 sz; uint32 st;
    int ok = dir_find(cur, comps[i], &typ, &sz, &st);
    if(!ok){
      log_warn("fat32_namei: not found '%s'", comps[i]);
      return 0;
    }
    cur = st; cur_type = typ; cur_size = sz;
  }
  return make_inode(fat.dev, cur_type, cur_size, cur);
}

int fat32_readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  if(!ip || ip->major != FAT32_INODE_TAG){
    log_warn("fat32_readi: non-fat inode");
    return -1;
  }
  if(off > ip->size) return -1;
  if(off + n > ip->size) n = ip->size - off;
  uint32 start_clus = ip->addrs[0];
  // Walk cluster chain to reach 'off'
  uint32 cl = start_clus;
  uint32 remain_off = off;
  uint32 bytes_per_cluster = fat.spc * fat.bps;
  while(remain_off >= bytes_per_cluster){
    uint32 nxt = fat_next_clus(cl);
    if(nxt >= 0x0FFFFFF8){ log_warn("fat32_readi: EOC before offset"); return -1; }
    cl = nxt;
    remain_off -= bytes_per_cluster;
  }
  // Start copying
  uint tot = 0;
  uint32 cur_off_in_cluster = remain_off;
  while(tot < n){
    uint32 sec0 = clus_to_sec(cl);
    // sector index within cluster
    uint32 sec_idx = cur_off_in_cluster / fat.bps;
    uint32 sec_off = cur_off_in_cluster % fat.bps;
    uint8 secbuf[512];
    read_sector512(fat.dev, sec0 + sec_idx, secbuf);
    uint m = MIN(n - tot, fat.bps - sec_off);
    if(user_dst){
      if(either_copyout(1, dst + tot, secbuf + sec_off, m) == -1){
        return -1;
      }
    } else {
      memmove((void *)(dst + tot), secbuf + sec_off, m);
    }
    tot += m;
    cur_off_in_cluster += m;
    if(cur_off_in_cluster >= bytes_per_cluster && tot < n){
      // move to next cluster
      uint32 nxt = fat_next_clus(cl);
      if(nxt >= 0x0FFFFFF8){ break; }
      cl = nxt;
      cur_off_in_cluster = 0;
    } else if((cur_off_in_cluster / fat.bps) >= fat.spc && tot < n){
      // next cluster (safety)
      uint32 nxt = fat_next_clus(cl);
      if(nxt >= 0x0FFFFFF8){ break; }
      cl = nxt; cur_off_in_cluster = 0;
    }
  }
  return tot;
}
