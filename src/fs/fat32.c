

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

static void write_sector512(uint dev, uint32 lba, uint8 *src)
{
  uint blk = lba / (BSIZE / 512);
  uint offs = (lba % (BSIZE / 512)) * 512;
  struct buf *bp = bread(dev, blk);
  memmove(bp->data + offs, src, 512);
  log_write(bp);
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

static uint32 fat_alloc_clus(void)
{
  // Find free cluster starting from 2
  for(uint32 cl = 2; cl < 0x0FFFFFF8; cl++){
    uint32 val = fat_next_clus(cl);
    if(val == 0){
      // Mark as EOC
      uint32 offset_bytes = cl * 4;
      uint32 fat_sec = fat.fat_start_sec + (offset_bytes / fat.bps);
      uint32 sec_off = offset_bytes % fat.bps;
      uint8 sec[512];
      read_sector512(fat.dev, fat_sec, sec);
      *(uint32 *)(sec + sec_off) = 0x0FFFFFFF;
      write_sector512(fat.dev, fat_sec, sec);
      return cl;
    }
  }
  return 0;
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

// // Find a path component within a directory cluster chain.
// // Returns 0 if not found; else fills type,size,start cluster.
// // Extract ASCII chars from an LFN entry into buf (append order)
// static void lfn_copy_chars(uint8 *de, int ofs, int cnt, char *buf, int *plen)
// {
//   int len = *plen;
//   for(int i=0;i<cnt;i+=2){
//     uint8 lo = de[ofs + i];
//     uint8 hi = de[ofs + i + 1];
//     if(lo == 0x00){
//       // zero terminator
//       break;
//     }
//     if(hi == 0 && lo != 0xFF){
//       buf[len++] = (char)lo;
//     }
//   }
//   *plen = len;
// }

// static void lfn_extract_append(uint8 *de, char *buf, int *plen)
// {
//   // name fields are UCS-2; take low byte if high byte is 0
//   lfn_copy_chars(de, 1, 10, buf, plen);   // name1: 5 chars
//   lfn_copy_chars(de, 14, 12, buf, plen);  // name2: 6 chars
//   lfn_copy_chars(de, 28, 4, buf, plen);   // name3: 2 chars
//   buf[*plen] = 0;
// }

static void lfn_extract_append(uint8 *de, char *buf, int *len)
{
  uint8 seq_byte = de[0];
  uint8 seq = seq_byte & 0x1F;  // 序列号 (1-20)
  int is_last = (seq_byte & 0x40) != 0;  // 最后一个片段标志
  
  // log_info("lfn_extract_append: seq=%d, is_last=%d", seq, is_last);
  
  if(seq == 0 || seq > 20) {
    log_warn("lfn_extract_append: invalid sequence number %d\n\n", seq);
    return;
  }
  
  // 提取13个Unicode字符
  uint8 chars[13];
  chars[0]  = de[1] | (de[2] << 8);
  chars[1]  = de[3] | (de[4] << 8);
  chars[2]  = de[5] | (de[6] << 8);
  chars[3]  = de[7] | (de[8] << 8);
  chars[4]  = de[9] | (de[10] << 8);
  chars[5]  = de[14] | (de[15] << 8);
  chars[6]  = de[16] | (de[17] << 8);
  chars[7]  = de[18] | (de[19] << 8);
  chars[8]  = de[20] | (de[21] << 8);
  chars[9]  = de[22] | (de[23] << 8);
  chars[10] = de[24] | (de[25] << 8);
  chars[11] = de[28] | (de[29] << 8);
  chars[12] = de[30] | (de[31] << 8);
  
  // 计算这个片段应该插入的位置
  // LFN条目是逆序存储的：最后一个片段（seq=1）先被读取
  // 我们需要从后向前填充
  int pos = (seq - 1) * 13;
  
  // 复制字符
  for(int i = 0; i < 13; i++) {
    if(chars[i] == 0 || chars[i] == 0xFFFF) {
      break;  // 字符串结束标记
    }
    
    // 简单的Unicode到ASCII转换
    char c = (chars[i] < 128) ? (char)chars[i] : '?';
    
    // 确保不会越界
    if(pos + i < 259) {
      buf[pos + i] = c;
    }
  }
  
  // 如果是最后一个片段（序列号最高且有0x40标志），设置字符串结束符
  if(is_last) {
    // 找到实际字符串结束的位置
    int total_len = pos + 13;
    for(int i = 0; i < 13; i++) {
      if(chars[i] == 0 || chars[i] == 0xFFFF) {
        total_len = pos + i;
        break;
      }
    }
    
    if(total_len < 260) {
      buf[total_len] = 0;
      *len = total_len;
    } else {
      buf[259] = 0;
      *len = 259;
    }
    
    // 比较重要的 log_info 输出
    log_info("lfn_extract_append: final LFN='%s' (len=%d)\n\n", buf, *len);
  }
}

static int str_eq(const char *a, const char *b)
{
  while(*a && *b){
    if(*a != *b) return 0;
    a++; b++;
  }
  return *a == *b;
}

static int dir_find(uint32 dir_clus, const char *comp, short *out_type, uint32 *out_size, uint32 *out_clus)
{
  uint32 cl = dir_clus;
  // log_info("dir_find: looking for '%s' in dir cluster %d", comp, dir_clus);
  
  for(;;){
    uint32 sec = clus_to_sec(cl);
    for(uint s=0; s<fat.spc; ++s){
      uint8 buf[512];
      read_sector512(fat.dev, sec + s, buf);
      
      // // 打印当前扇区前几个字节，确认读取正确
      // log_info("dir_find: sector %d, first 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x", 
      //          sec + s, 
      //          buf[0], buf[1], buf[2], buf[3], 
      //          buf[4], buf[5], buf[6], buf[7]);
      
      // iterate entries
      char lfn_name[260];
      int lfn_len = 0;
      lfn_name[0] = 0;
      
      for(int off=0; off<512; off+=32){
        uint8 *de = buf + off;
        uint8 first = de[0];
        uint8 attr = de[11];
        
        // 打印每个条目的基本信息
        // log_info("dir_find: offset %d, first=0x%02x, attr=0x%02x", off, first, attr);
        
        if(first == 0x00){ // end of dir
          // log_info("dir_find: end of directory marker");
          return 0;
        }
        if(first == 0xE5){ // deleted
          // reset any accumulated LFN when encountering deleted
          lfn_len = 0; lfn_name[0] = 0;
          // log_info("dir_find: deleted entry");
          continue;
        }
        
        if(attr == 0x0F){ // long name entry
          // log_info("dir_find: LFN entry, accumulating");
          lfn_extract_append(de, lfn_name, &lfn_len);
          // log_info("dir_find: current LFN='%s'", lfn_name);
          continue;
        }
        
        // // 提取并打印SFN
        // char sfn_name[13] = {0};
        // for(int i=0; i<8 && de[i]!=' '; i++){
        //   sfn_name[i] = de[i];
        // }
        // if(de[8] != ' '){
        //   int len = strlen(sfn_name);
        //   sfn_name[len] = '.';
        //   for(int i=0; i<3 && de[8+i]!=' '; i++){
        //     sfn_name[len+1+i] = de[8+i];
        //   }
        // }
        
        uint16 cl_hi = *(uint16 *)(de + 20);
        uint16 cl_lo = *(uint16 *)(de + 26);
        uint32 startc = ((uint32)cl_hi << 16) | cl_lo;
        
        // log_info("dir_find: SFN='%s', LFN='%s', start cluster=%d, size=%d", 
        //          sfn_name, lfn_name, startc, *(uint32 *)(de + 28));
        
        // match name: prefer LFN if accumulated; else match SFN
        int matched = 0;
        if(lfn_len > 0){
          // compare component to long name (case-sensitive)
          // log_info("dir_find: comparing '%s' with LFN '%s'", comp, lfn_name);
          if(str_eq(comp, lfn_name)){
            matched = 1;
            // log_info("dir_find: matched with LFN!");
          }
        }
        if(!matched){
          // log_info("dir_find: comparing '%s' with SFN '%s'", comp, sfn_name);
          if(match_sfn(comp, de)){
            matched = 1;
            // log_info("dir_find: matched with SFN!");
          }
        }
        
        if(matched){
          uint32 fsz = *(uint32 *)(de + 28);
          short type = (attr & 0x10) ? T_DIR : T_FILE;
          *out_type = type;
          *out_size = fsz;
          *out_clus = startc;
          // log_info("dir_find: FOUND! type=%d, size=%d, cluster=%d", type, fsz, startc);
          return 1;
        }
        
        // clear LFN after processing SFN that didn't match
        lfn_len = 0; lfn_name[0] = 0;
        // log_info("dir_find: moving to next entry");
      }
    }
    
    // next cluster in chain
    uint32 nxt = fat_next_clus(cl);
    if(nxt >= 0x0FFFFFF8 || nxt == 0x0FFFFFFF){
      // log_info("dir_find: end of cluster chain");
      break;
    }
    if(nxt < 2){
      log_warn("dir_find: bad next cluster %x", nxt);
      break;
    }
    cl = nxt;
    // log_info("dir_find: moving to next cluster %d", cl);
  }
  
  // log_info("dir_find: file not found");
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
  // log_info("FAT32: bps=%d spc=%d rsvd=%d nfats=%d fatsz=%d root=%x", bps, spc, rsvd, nf, fatsz32, rootclus);
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
      log_warn("fat32_namei: not found '%s'\n", comps[i]);
      return 0;
    }
    cur = st; cur_type = typ; cur_size = sz;
  }
  return make_inode(fat.dev, cur_type, cur_size, cur);
}

  

// Resolve path relative to a given base directory inode for FAT32.
// If path is absolute, behaves like fat32_namei.
// Resolve path relative to a given base directory inode for FAT32.
// If path is absolute, behaves like fat32_namei.
struct inode* fat32_nameiat(struct inode *base, char *path)
{
  if(!path || !*path){
    log_warn("fat32_nameiat: empty path");
    return 0;
  }
  if(!fat32_mode){
    log_warn("fat32_nameiat: fat32 disabled");
    return 0;
  }
  
  log_info("fat32_nameiat: start, path='%s'", path);
  if(base){
    log_info("fat32_nameiat: base inode: dev=%d, inum=%d, type=%d, addr[0]=%d", 
             base->dev, base->inum, base->type, base->addrs[0]);
  }else{
    log_info("fat32_nameiat: base is NULL");
  }
  
  if(path[0] == '/'){
    log_info("fat32_nameiat: absolute path, use fat32_namei");
    return fat32_namei(path);
  }
  
  // Split path
  char workbuf[MAXPATH];
  char *comps[32];
  int n = split_path(path, comps, 32, workbuf, sizeof(workbuf));
  // log_info("fat32_nameiat: split_path returned %d components", n);
  // for(int i=0;i<n;i++){
  //   log_info("fat32_nameiat: comps[%d]='%s'", i, comps[i]);
  // }
  
  uint32 cur;
  if(base && base->major == FAT32_INODE_TAG){
    cur = base->addrs[0];
    if(cur == 0) cur = fat.root_clus;
    // log_info("fat32_nameiat: start from base cluster %d", cur);
  } else {
    cur = fat.root_clus;
    // log_info("fat32_nameiat: start from root cluster %d", cur);
  }
  
  short cur_type = T_DIR;
  uint32 cur_size = 0;
  if(n == 0){
    // log_info("fat32_nameiat: empty path after splitting");
    return make_inode(fat.dev, T_DIR, 0, cur);
  }
  
  for(int i=0;i<n;i++){
    if(comps[i][0] == '.' && comps[i][1] == 0){
      // log_info("fat32_nameiat: skip '.' component");
      continue;
    }
    
    short typ; uint32 sz; uint32 st;
    int ok = dir_find(cur, comps[i], &typ, &sz, &st);
    // log_info("fat32_nameiat: dir_find('%s') returned %d, type=%d, size=%d, cluster=%d", 
    //          comps[i], ok, typ, sz, st);
    
    if(!ok){
      log_warn("fat32_nameiat: not found '%s'", comps[i]);
      return 0;
    }
    cur = st; cur_type = typ; cur_size = sz;
  }
  
  struct inode *ip = make_inode(fat.dev, cur_type, cur_size, cur);
  if(ip){
    // log_info("fat32_nameiat: success, created inode dev=%d, inum=%d, type=%d, cluster=%d", 
    //          ip->dev, ip->inum, ip->type, ip->addrs[0]);
  }else{
    log_warn("fat32_nameiat: make_inode failed");
  }
  return ip;
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

struct inode* fat32_createat(struct inode *dp, char *name, short type, int major, int minor)
{
  if(!dp || dp->major != FAT32_INODE_TAG || dp->type != T_DIR){
    log_warn("fat32_createat: invalid dp");
    return NULL;
  }
  log_info("fat32_createat: name='%s', type=%d", name, type);
  uint32 dir_clus = dp->addrs[0];
  uint32 new_clus = fat_alloc_clus();
  if(new_clus == 0){
    log_warn("fat32_createat: no free cluster");
    return NULL;
  }
   log_info("fat32_createat: allocated cluster %u", new_clus);

  // decide whether we need LFN entries (name not pure 8.3)
  int namelen = strlen(name);
  int dot = -1;
  for(int i = 0; i < namelen; i++){
    if(name[i] == '.') dot = i;
  }
  int base_len = (dot >= 0) ? dot : namelen;
  int ext_len  = (dot >= 0) ? (namelen - dot - 1) : 0;
  int need_lfn = 0;
  if(base_len > 8 || ext_len > 3){
    need_lfn = 1;
  } else {
    // check for lowercase or other chars that would be mangled in SFN
    for(int i = 0; i < namelen; i++){
      char c = name[i];
      if(c == '.') continue;
      if(c >= 'a' && c <= 'z') { need_lfn = 1; break; }
      // very simple check; ignore full FAT charset here
    }
  }
  int lfn_len = namelen;
  if(lfn_len > 255) lfn_len = 255;
  int lfn_entries = need_lfn ? ((lfn_len + 12) / 13) : 0;
  int needed_slots = (need_lfn ? (lfn_entries + 1) : 1); // LFN + SFN
  if(need_lfn){
    log_info("fat32_createat: will write %d LFN entries for '%s'", lfn_entries, name);
  }
  // Find free directory entry
  uint32 cl = dir_clus;
  for(;;){
    uint32 sec = clus_to_sec(cl);
    for(uint s = 0; s < fat.spc; ++s){
      uint8 buf[512];
      read_sector512(fat.dev, sec + s, buf);
      int run_len = 0;
      int run_start_off = -1;
      for(int off = 0; off < 512; off += 32){
        uint8 *de = buf + off;
        int free = (de[0] == 0x00 || de[0] == 0xE5);
        if(free){
          if(run_len == 0) run_start_off = off;
          run_len++;
          if(run_len >= needed_slots){
            // We found enough consecutive free entries starting at run_start_off
            uint8 *base = buf + run_start_off;

            // First, prepare SFN from name (8.3 upper)
            char sfn[11];
            memset(sfn, ' ', 11);
            int len = namelen;
            int dot2 = dot;
            int name_len = base_len;
            int ext_len2 = ext_len;
            for(int i2 = 0; i2 < 8 && i2 < name_len; i2++){
              char c = name[i2];
              if(c >= 'a' && c <= 'z') c -= 32;
              sfn[i2] = c;
            }
            for(int i2 = 0; i2 < 3 && i2 < ext_len2; i2++){
              char c = name[dot2 + 1 + i2];
              if(c >= 'a' && c <= 'z') c -= 32;
              sfn[8 + i2] = c;
            }

            // Optionally write LFN entries before SFN
            if(need_lfn){
              for(int e = 0; e < lfn_entries; e++){
                uint8 *lde = base + e * 32;
                memset(lde, 0, 32);
                uint8 seq = (uint8)(lfn_entries - e); // highest seq first
                if(e == 0) seq |= 0x40; // last logical entry flag
                lde[0] = seq;
                lde[11] = 0x0F; // LFN attribute
                lde[12] = 0x00; // type
                lde[13] = 0x00; // checksum; our reader ignores it
                // lde[26-27] cluster low = 0

                // Fill 13 UTF-16 chars for this fragment
                int seq_no = seq & 0x1F;
                int base_index = (seq_no - 1) * 13;
                int char_pos[13] = {1,3,5,7,9, 14,16,18,20,22,24, 28,30};
                for(int i2 = 0; i2 < 13; i2++){
                  int gi = base_index + i2;
                  uint16 ch;
                  if(gi < lfn_len){
                    ch = (uint8)name[gi];
                  } else if(gi == lfn_len){
                    ch = 0x0000; // terminator
                  } else {
                    ch = 0xFFFF; // padding
                  }
                  lde[char_pos[i2]]     = (uint8)(ch & 0xFF);
                  lde[char_pos[i2] + 1] = (uint8)(ch >> 8);
                }
              }
            }

            // Now write SFN at the last slot in this run
            uint8 *sde = base + (needed_slots - 1) * 32;
            memset(sde, 0, 32);
            memmove(sde, sfn, 11);
            sde[11] = (type == T_DIR) ? 0x10 : 0x00;
            *(uint16 *)(sde + 20) = new_clus >> 16;
            *(uint16 *)(sde + 26) = new_clus & 0xFFFF;
            *(uint32 *)(sde + 28) = 0; // size

            write_sector512(fat.dev, sec + s, buf);
            log_info("fat32_createat: wrote %s entry at cluster %u sector %u offset %d (LFN=%d)",
                     (type == T_DIR) ? "DIR" : "FILE", cl, sec + s, run_start_off, need_lfn);

            // For directory, initialize . and ..
            if(type == T_DIR){
              uint32 data_sec = clus_to_sec(new_clus);
              uint8 data_buf[512] = {0};
              // .
              uint8 *de_dot = data_buf;
              memset(de_dot, 0, 32);
              de_dot[0] = '.';
              memset(de_dot + 1, ' ', 10);
              de_dot[11] = 0x10;
              *(uint16 *)(de_dot + 20) = new_clus >> 16;
              *(uint16 *)(de_dot + 26) = new_clus & 0xFFFF;
              // ..
              uint8 *de_dotdot = data_buf + 32;
              memset(de_dotdot, 0, 32);
              de_dotdot[0] = '.';
              de_dotdot[1] = '.';
              memset(de_dotdot + 2, ' ', 9);
              de_dotdot[11] = 0x10;
              *(uint16 *)(de_dotdot + 20) = dir_clus >> 16;
              *(uint16 *)(de_dotdot + 26) = dir_clus & 0xFFFF;
              write_sector512(fat.dev, data_sec, data_buf);
              log_info("fat32_createat: initialized '.' and '..' for new dir cluster %u", new_clus);
            }

            struct inode *ip = make_inode(fat.dev, type, 0, new_clus);
            log_info("fat32_createat: created inode for cluster %u", new_clus);
            return ip;
          }
        } else {
          run_len = 0;
          run_start_off = -1;
        }
      }
    }
    // Next cluster
    uint32 nxt = fat_next_clus(cl);
    if(nxt >= 0x0FFFFFF8) break;
    cl = nxt;
  }
  log_warn("fat32_createat: no free directory entry");
  return NULL;
}
struct inode* fat32_create(char *path, short type, int major, int minor)
{
  if(!path || !*path){
    log_warn("fat32_create: empty path");
    return 0;
  }
  if(!fat32_mode){
    log_warn("fat32_create: fat32 disabled");
    return 0;
  }
  begin_op(fat.dev);
  // Split path
  char workbuf[MAXPATH];
  char *comps[32];
  int n = split_path(path, comps, 32, workbuf, sizeof(workbuf));
  if(n == 0){
    log_warn("fat32_create: invalid path");
    end_op(fat.dev);
    return 0;
  }
  // Find parent
  uint32 cur = fat.root_clus;
  short cur_type = T_DIR;
  for(int i = 0; i < n - 1; i++){
    short typ; uint32 sz; uint32 st;
    int ok = dir_find(cur, comps[i], &typ, &sz, &st);
    if(!ok || typ != T_DIR){
      log_warn("fat32_create: parent not found '%s'", comps[i]);
      end_op(fat.dev);
      return 0;
    }
    cur = st;
  }
  // Parent inode
  struct inode *dp = make_inode(fat.dev, T_DIR, 0, cur);
  // Create
  struct inode *ip = fat32_createat(dp, comps[n-1], type, major, minor);
  // Clean up dp
  iput(dp);
  end_op(fat.dev);
  return ip;
}


// FAT32目录遍历，仿Linux getdents64，返回写入的字节数
// 参数：ip=目录inode，off=目录偏移（以字节为单位），dst=用户空间buf，len=buf大小
// 返回：实际写入的字节数，或0表示结尾，-1表示错误
int fat32_getdents64(struct inode *ip, uint off, uint64 dst, uint len) {
  if (!ip || ip->major != FAT32_INODE_TAG || ip->type != T_DIR) return -1;
  struct proc *p = myproc();
  uint32 cl = ip->addrs[0];
  uint32 bytes_per_cluster = fat.spc * fat.bps;
  uint32 dir_off = off; // 当前目录偏移
  uint written = 0;
  char lfn_buf[256];
  int lfn_len = 0;
  int reclen;
  while (written + sizeof(struct dirent) < len) {
    // 计算当前偏移对应的cluster和sector
    uint32 cur_cl = cl;
    uint32 remain = dir_off;
    while (remain >= bytes_per_cluster) {
      cur_cl = fat_next_clus(cur_cl);
      if (cur_cl >= 0x0FFFFFF8) return written; // 结尾
      remain -= bytes_per_cluster;
    }
    uint32 sec = clus_to_sec(cur_cl);
    uint32 sec_off = remain / 512;
    uint32 ent_off = remain % 512;
    uint8 secbuf[512];
    read_sector512(fat.dev, sec + sec_off, secbuf);
    if (ent_off + 32 > 512) {
      // 跨扇区不处理
      break;
    }
    uint8 *de = secbuf + ent_off;
    // 目录项结束
    if (de[0] == 0x00) break;
    // 跳过无效项
    if (de[0] == 0xE5) {
      dir_off += 32;
      continue;
    }
    // LFN项
    if ((de[11] & 0x0F) == 0x0F) {
      // 累积LFN片段
      lfn_extract_append(de, lfn_buf, &lfn_len);
      dir_off += 32;
      continue;
    }
    // SFN项
    struct dirent dent;
    memset(&dent, 0, sizeof(dent));
    // inode号用起始cluster
    dent.d_ino = ((uint32)de[26] | ((uint32)de[27] << 8) | ((uint32)de[20] << 16) | ((uint32)de[21] << 24));
    dent.d_off = dir_off + 32; // 下一个dirent偏移
    dent.d_reclen = sizeof(struct dirent);
    // 类型
    if (de[11] & 0x10) dent.d_type = 4; // DT_DIR
    else dent.d_type = 8; // DT_REG
    // 文件名
    if (lfn_len > 0) {
      int n = lfn_len < sizeof(dent.d_name) - 1 ? lfn_len : sizeof(dent.d_name) - 1;
      memmove(dent.d_name, lfn_buf, n);
      dent.d_name[n] = 0;
      lfn_len = 0;
    } else {
      // 8.3转字符串
      int i = 0, j = 0;
      while (i < 8 && de[i] != ' ') dent.d_name[j++] = de[i++];
      if (de[8] != ' ') {
        dent.d_name[j++] = '.';
        int k = 8;
        while (k < 11 && de[k] != ' ') dent.d_name[j++] = de[k++];
      }
      dent.d_name[j] = 0;
    }
    // 拷贝到用户空间
    if (copyout(p->pagetable, dst + written, (char *)&dent, sizeof(dent)) < 0) return -1;
    written += sizeof(dent);
    dir_off += 32;
  }
  return written;
}
