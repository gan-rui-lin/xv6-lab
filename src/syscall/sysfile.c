#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"

#include "sleeplock.h" // TODO 和 fs/file.h 捆绑着引入
#include "fs/fs.h"     // TODO 和 fs/file.h 捆绑着引入
#include "fs/file.h"
#include "fs/stat.h"
#include "fs/fat32.h"
#include "fcntl.h"

extern int fat32_mode;
#// 兼容 Linux open/openat 的 flags 到内核内部标志
static int normalize_open_flags(int flags)
{
  int norm = 0;
  // 基本读写位：Linux 与内核一致（O_RDONLY=0, O_WRONLY=1, O_RDWR=2）
  if(flags & O_WRONLY) norm |= O_WRONLY;
  if(flags & O_RDWR)   norm |= O_RDWR;
  // Linux O_CREAT=0x40 → 内核 O_CREATE=0x200
  if(flags & LINUX_O_CREAT)     norm |= O_CREATE;
  // Linux O_TRUNC=0x200 → 内核 O_TRUNC=0x400
  if(flags & LINUX_O_TRUNC)    norm |= O_TRUNC;
  // Linux O_DIRECTORY=0x200000 → 内核 O_DIRECTORY=0x10000
  if(flags & LINUX_O_DIRECTORY) norm |= O_DIRECTORY;
  // 其余未映射位忽略
  return norm;
}

// 根据当前 cwd 路径和 chdir 传入的 path，
// 计算新的逻辑 cwd 字符串（纯字符串处理，不访问文件系统）。
static void
build_cwd_path(const char *oldcwd, const char *path, char *out, int outlen)
{
  // 简单路径规范化：处理绝对/相对路径，以及 '.'、'..'
  const char *src;
  const char *parts[64];
  int depth = 0;

  if(path[0] == '/'){
    // 从根开始
    src = path;
  } else {
    // 先把 oldcwd 拆成组件
    if(oldcwd[0] == '\0'){
      parts[depth++] = ""; // root
    } else if(oldcwd[0] == '/' && oldcwd[1] == '\0'){
      // root, depth 从 0 开始即可
    } else {
      const char *p = oldcwd;
      while(*p){
        while(*p == '/') p++;
        if(!*p) break;
        const char *start = p;
        while(*p && *p != '/') p++;
        if(depth < 64){
          // 这里仅存指针位置，不复制；重建时直接从 oldcwd 用
          parts[depth++] = start;
        }
      }
    }
    src = path;
  }

  // 处理 path 的组件
  while(*src){
    while(*src == '/') src++;
    if(!*src) break;
    const char *start = src;
    while(*src && *src != '/') src++;
    int len = src - start;
    if(len == 0) continue;

    if(len == 1 && start[0] == '.'){
      // 忽略 '.'
      continue;
    }
    if(len == 2 && start[0] == '.' && start[1] == '.'){
      // 处理 '..'
      if(depth > 0)
        depth--;
      continue;
    }

    if(depth < 64)
      parts[depth++] = start;
  }

  // 重建 out
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

  log_debug("build_cwd_path: old='%s' path='%s' -> '%s'", oldcwd, path, out);
}


// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0){
    log_error("argfd: fd %d invalid\n", fd);
    log_error("myproc()->ofile[fd] is %p\n", myproc()->ofile[fd]);
    return -1;
  }
    
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0){
     log_error("sys_write f is %p, n is %d, p is %p\n", f, n, p);
     return -1;
  }
   
  // log_debug("sys_write: fd=%d, p=%p, n=%d\n", f, p, n); // TODO 调试信息
  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  int fd;
  uint64 kst_addr;
  struct proc *p = myproc();
  
  // 获取参数: fd 和 kstat 指针
  if(argint(0, &fd) < 0 || argaddr(1, &kst_addr) < 0)
    return -1;
  
  // 获取文件描述符对应的文件
  if(argfd(0, &fd, &f) < 0)
    return -1;
  
  // 获取文件状态信息
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    struct stat st;
    struct kstat kst;
    
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    
    // 将 stat 转换为 kstat (Linux格式)
    kst.st_dev = st.dev;
    kst.st_ino = st.ino;
    kst.st_mode = (st.type == T_DIR) ? 0040000 : 0100000; // S_IFDIR : S_IFREG
    kst.st_nlink = st.nlink;
    kst.st_uid = 0;
    kst.st_gid = 0;
    kst.st_rdev = 0;
    kst.__pad = 0;
    kst.st_size = st.size;
    kst.st_blksize = 512;
    kst.__pad2 = 0;
    kst.st_blocks = (st.size + 511) / 512;
    kst.st_atime_sec = 0;
    kst.st_atime_nsec = 0;
    kst.st_mtime_sec = 0;
    kst.st_mtime_nsec = 0;
    kst.st_ctime_sec = 0;
    kst.st_ctime_nsec = 0;
    
    // 复制到用户空间
    if(copyout(p->pagetable, kst_addr, (char *)&kst, sizeof(kst)) < 0)
      return -1;
      
    return 0;
  }
  
  return -1;
}


// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op(ROOTDEV);
  if((ip = namei(old)) == 0){
    end_op(ROOTDEV);
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op(ROOTDEV);

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op(ROOTDEV);
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op(ROOTDEV);
  if((dp = nameiparent(path, name)) == 0){
    end_op(ROOTDEV);
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op(ROOTDEV);

  return 0;

bad:
  iunlockput(dp);
  end_op(ROOTDEV);
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  if(fat32_mode){
    return fat32_create(path, type, major, minor);
  }

  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  int kflags = normalize_open_flags(omode);
  log_info("sys_open: path='%s' omode=0x%x kflags=0x%x", path, omode, kflags);

  begin_op(ROOTDEV);

  if(kflags & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op(ROOTDEV);
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      log_warn("sys_open: namei('%s') failed", path);
      end_op(ROOTDEV);
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR){
      // Allow opening directories when O_DIRECTORY is specified;
      // treat access as read-only.
      if(!(kflags == O_RDONLY || (kflags & O_DIRECTORY))){
        log_warn("sys_open: reject open dir without O_DIRECTORY/O_RDONLY");
        iunlockput(ip);
        end_op(ROOTDEV);
        return -1;
      }
      log_info("sys_open: opened directory");
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
    f->minor = ip->minor;
  } else {
    f->type = FD_INODE;
  }
  f->ip = ip;
  f->off = 0;
  if(ip->type == T_DIR){
    f->readable = 1;
    f->writable = 0;
  } else {
    f->readable = !(kflags & O_WRONLY);
    f->writable = (kflags & O_WRONLY) || (kflags & O_RDWR);
  }

  iunlock(ip);
  end_op(ROOTDEV);

  return fd;
}

// Linux 兼容的 openat(56): a0=dirfd, a1=pathname, a2=flags, a3=mode
uint64
sys_openat(void)
{
  char path[MAXPATH];
  int dirfd, flags;
  int fd;
  struct file *f;
  struct inode *ip;
  int n;

  // 提取参数（Linux 布局）
  if(argint(0, &dirfd) < 0) return -1;
  if((n = argstr(1, path, MAXPATH)) < 0 || argint(2, &flags) < 0)
    return -1;

  int kflags = normalize_open_flags(flags);
  // log_info("sys_openat: dirfd=%d path='%s' flags=0x%x kflags=0x%x", dirfd, path, flags, kflags);

  begin_op(ROOTDEV);

  struct file *dirf = 0;
  if(path[0] != '/' && dirfd != AT_FDCWD && dirfd >= 0){
    struct proc *p = myproc();
    if(dirfd >= NOFILE){
      log_warn("sys_openat: dirfd out of range %d", dirfd);
      end_op(ROOTDEV);
      return -1;
    }
    dirf = p->ofile[dirfd];
    if(dirf == 0 || dirf->type != FD_INODE || dirf->ip->type != T_DIR){
      log_warn("sys_openat: dirfd not a directory fd=%d", dirfd);
      end_op(ROOTDEV);
      return -1;
    }
  }

  // First resolve the target according to dirfd semantics
  if(path[0] == '/' || dirfd == AT_FDCWD || dirfd < 0){
    // if(path[0] == '/') log_info("sys_openat: absolute path");
    // else log_info("sys_openat: relative to cwd");
    ip = namei(path);
  } else {
    // log_info("sys_openat: relative to dirfd=%d", dirfd);
    ip = nameiat(dirf->ip, path);
  }

  if(ip == 0){
    // Not found; if O_CREATE is set, attempt to create when possible
    if(kflags & O_CREATE){
      if(path[0] == '/' || dirfd == AT_FDCWD || dirfd < 0){
        ip = create(path, T_FILE, 0, 0);
        if(ip == 0){
          end_op(ROOTDEV);
          return -1;
        }
      } else {
        ip = createat(dirf->ip, path, T_FILE, 0, 0);
        if(ip == 0){
          log_warn("sys_openat: createat failed for '%s'", path);
          end_op(ROOTDEV);
          return -1;
        }
      }
    } else {
      log_warn("sys_openat: resolve failed for '%s'", path);
      end_op(ROOTDEV);
      return -1;
    }
  }

  ilock(ip);
  if(ip->type == T_DIR && kflags != O_RDONLY){
    if(kflags & O_DIRECTORY){
      // allow open directory with O_DIRECTORY
    } else {
      log_warn("sys_openat: reject opening dir with write flags");
      iunlockput(ip);
      end_op(ROOTDEV);
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
    f->minor = ip->minor;
  } else {
    f->type = FD_INODE;
  }
  f->ip = ip;
  f->off = 0;
  if(ip->type == T_DIR){
    f->readable = 1;
    f->writable = 0;
  } else {
    f->readable = !(kflags & O_WRONLY);
    f->writable = (kflags & O_WRONLY) || (kflags & O_RDWR);
  }

  iunlock(ip);
  end_op(ROOTDEV);

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op(ROOTDEV);
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  iunlockput(ip);
  end_op(ROOTDEV);
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op(ROOTDEV);
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  iunlockput(ip);
  end_op(ROOTDEV);
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op(ROOTDEV);
  int r = argstr(0, path, MAXPATH);
  log_info("sys_chdir: argstr ret=%d, path='%s'", r, path);
  if(r < 0 || (ip = namei(path)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op(ROOTDEV);
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op(ROOTDEV);
  p->cwd = ip;
  // 更新进程的 cwdpath，方便 sys_getcwd 使用
  build_cwd_path(p->cwdpath, path, p->cwdpath, sizeof(p->cwdpath));
  log_debug("sys_chdir: cwdpath now '%s'", p->cwdpath);
  return 0;
}

// !  virtio_disk_intr status
uint64
sys_mkdirat(void)
{
  char path[MAXPATH];
  int dirfd;
  struct inode *ip;
  int n;

  if(argint(0, &dirfd) < 0 || (n = argstr(1, path, MAXPATH)) < 0)
    return -1;

  begin_op(ROOTDEV);

  struct file *dirf = 0;
  if(path[0] != '/' && dirfd != AT_FDCWD && dirfd >= 0){
    struct proc *p = myproc();
    if(dirfd >= NOFILE){
      end_op(ROOTDEV);
      return -1;
    }
    dirf = p->ofile[dirfd];
    if(dirf == 0 || dirf->type != FD_INODE || dirf->ip->type != T_DIR){
      end_op(ROOTDEV);
      return -1;
    }
  }

  if(path[0] == '/' || dirfd == AT_FDCWD || dirfd < 0){
    ip = create(path, T_DIR, 0, 0);
  } else {
    ip = createat(dirf->ip, path, T_DIR, 0, 0);
  }

  if(ip == 0){
    end_op(ROOTDEV);
    return -1;
  }

  // iunlockput(ip);
  end_op(ROOTDEV);
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      panic("sys_exec kalloc");
    if(fetchstr(uarg, argv[i], PGSIZE) < 0){
      goto bad;
    }
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

uint64
sys_dup3(void)
{
  int oldfd, newfd, flags;
  struct file *f;
  struct proc *p = myproc();

  // 获取参数
  if(argint(0, &oldfd) < 0 || argint(1, &newfd) < 0 || argint(2, &flags) < 0)
    return -1;

  // 检查oldfd是否有效
  if(oldfd < 0 || oldfd >= NOFILE || (f = p->ofile[oldfd]) == 0)
    return -1;

  // dup2行为（flags=0）：如果oldfd==newfd，直接返回newfd
  // dup3行为（flags!=0）：如果oldfd==newfd，返回错误
  if(oldfd == newfd) {
    if(flags != 0)
      return -1;  // dup3要求oldfd和newfd不能相同
    // dup2行为：oldfd和newfd相同时，直接返回newfd
    return newfd;
  }

  // 检查newfd范围是否有效
  if(newfd < 0 || newfd >= NOFILE)
    return -1;

  // 如果newfd已经打开，先关闭它
  if(p->ofile[newfd])
    fileclose(p->ofile[newfd]);

  // 复制文件描述符
  p->ofile[newfd] = f;
  filedup(f);

  return newfd;
}

uint64
sys_getdents64(void)
{
  int fd;
  uint64 buf, len;
  struct file *f;

  if(argint(0, &fd) < 0 || argaddr(1, &buf) < 0 || argaddr(2, &len) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if(f->type != FD_INODE || f->ip->type != T_DIR)
    return -1;
  int n = 0;

  // Currently only FAT32-backed directories are supported
  if(fat32_mode && f->ip->major == FAT32_INODE_TAG){
    uint off_entries = f->off; // logical directory entry index
    n = fat32_getdents64(f->ip, &off_entries, buf, len);
    if(n > 0)
      f->off = off_entries;
  } else {
    // non-FAT32 directories not yet supported
    return -1;
  }

  return n;
}

uint64
sys_mount(void)
{
  // TODO: implement mount
  return 0;
}

uint64
sys_getcwd(void)
{
  uint64 buf, size;
  if(argaddr(0, &buf) < 0 || argaddr(1, &size) < 0)
    return -1;
  struct proc *p = myproc();
  if(p->cwd == 0)
    return -1;
  const char *cwd = p->cwdpath[0] ? p->cwdpath : "/";
  int len = strlen(cwd) + 1;
  if(len > size)
    return -1;
  if(copyout(p->pagetable, buf, cwd, len) < 0)
    return -1;
  return buf;
}

uint64
sys_pipe2(void)
{
  // For simplicity, same as sys_pipe
  return sys_pipe();
}