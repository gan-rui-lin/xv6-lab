#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"
#include "errno.h"
#include "fcntl.h"
#include "mm/vma.h"

#ifndef AT_NULL
#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_ENTRY 9
#endif

#define AUXV_ENTRIES 6

// map ELF program header flags (PF_X=0x1, PF_W=0x2, PF_R=0x4)
// to PTE permission bits.
int flags2perm(int flags)
{
    int perm = 0;
    if(flags & 0x4)
      perm |= PTE_R;
    if(flags & 0x2)
      perm |= PTE_W;
    if(flags & 0x1)
      perm |= PTE_X;
    return perm;
}

static int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz);

int
exec(char *path, char **argv, char **envp)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz, sp, stackbase;
  uint64 ustack[MAXARG * 2 + 2 + (AUXV_ENTRIES + 1) * 2];
  struct elfhdr elf;
  struct inode *ip;
  struct inode *ip_interp = 0;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();
  uint64 load_bias = 0;
  int first_load = 1;
  int interp_op = 0;
  int have_interp = 0;
  char interp_path[MAXPATH];
  uint64 interp_base = 0;
  uint64 interp_entry = 0;
  int err = -EIO;

  begin_op(ROOTDEV);

  if((ip = namei(path)) == 0){
    end_op(ROOTDEV);
    return -ENOENT;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf)){
    log_error("exec: read elf header failed for %s", path);
    err = -EIO;
    goto bad;
  }
  if(elf.magic != ELF_MAGIC){
    log_error("exec: bad magic for %s (0x%x)", path, elf.magic);
    err = -ENOEXEC;
    goto bad;
  }

  if((pagetable = proc_pagetable(p)) == 0)
    { err = -ENOMEM; goto bad; }

  memset(interp_path, 0, sizeof(interp_path));

  // 加载主程序，同时读取 .interp 以便支持动态链接器。
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph)){
      log_error("exec: read phdr %d failed for %s", i, path);
      err = -EIO;
      goto bad;
    }
    if(ph.type == ELF_PROG_INTERP){
      if(ph.filesz == 0 || ph.filesz >= sizeof(interp_path)){
        log_error("exec: bad interp size for %s", path);
        err = -ENOEXEC;
        goto bad;
      }
      if(readi(ip, 0, (uint64)interp_path, ph.off, ph.filesz) != ph.filesz){
        log_error("exec: read interp failed for %s", path);
        err = -EIO;
        goto bad;
      }
      interp_path[ph.filesz] = '\0';
      have_interp = 1;
      continue;
    }
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(first_load){
      load_bias = ph.vaddr - ph.off;
      first_load = 0;
    }
    if(ph.memsz < ph.filesz){
      log_error("exec: memsz < filesz for %s", path);
      err = -ENOEXEC;
      goto bad;
    }
    if(ph.vaddr + ph.memsz < ph.vaddr){
      log_error("exec: vaddr overflow for %s", path);
      err = -ENOEXEC;
      goto bad;
    }
    // Map the segment, allowing non-page-aligned vaddr by rounding.
    uint64 va_end = PGROUNDUP(ph.vaddr + ph.memsz);
    if((sz = uvmalloc(pagetable, sz, va_end, flags2perm(ph.flags))) == 0)
      { err = -ENOMEM; goto bad; }
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      { err = -EIO; goto bad; }
  }
  iunlockput(ip);
  end_op(ROOTDEV);
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  if(have_interp){
    struct elfhdr interp_elf;
    begin_op(ROOTDEV);
    interp_op = 1;
    log_debug("exec: try interp %s\n", interp_path);
    ip_interp = namei(interp_path);
    if(ip_interp == 0 && strncmp(interp_path, "/lib/", 5) == 0){
      // 优先尝试 glibc: 把 /lib/ld-linux-* 重定向到 /glibc/lib/ld-linux-*
      if(strncmp(interp_path, "/lib/ld-linux-", 14) == 0){
        char alt_path[MAXPATH];
        int n = 0;
        const char *prefix = "/glibc/lib/";
        for(const char *c = prefix; *c && n < MAXPATH - 1; c++)
          alt_path[n++] = *c;
        for(const char *c = interp_path + 5; *c && n < MAXPATH - 1; c++)
          alt_path[n++] = *c;
        alt_path[n] = '\0';
        log_debug("exec: try glibc interp %s", alt_path);
        ip_interp = namei(alt_path);
        if(ip_interp != 0){
          safestrcpy(interp_path, alt_path, sizeof(interp_path));
        }
      }
      // 如果还找不到，尝试 musl: 把 /lib/ld-musl-* 重定向到 /musl/lib/ld-musl-*
      if(ip_interp == 0){
        char alt_path[MAXPATH];
        int n = 0;
        const char *prefix = "/musl/lib/";
        for(const char *c = prefix; *c && n < MAXPATH - 1; c++)
          alt_path[n++] = *c;
        for(const char *c = interp_path + 5; *c && n < MAXPATH - 1; c++)
          alt_path[n++] = *c;
        alt_path[n] = '\0';
        log_debug("exec: try musl interp %s", alt_path);
        ip_interp = namei(alt_path);
        if(ip_interp != 0){
          safestrcpy(interp_path, alt_path, sizeof(interp_path));
        }
      }
    }
    // Fallback: 若是 musl 解释器族（/lib/ld-musl-*.so.1）仍未找到，使用 /musl/lib/libc.so 作为动态链接器。
    if(ip_interp == 0){
      int musl_ld = 0;
      const char *p = interp_path;
      if(strncmp(p, "/lib/ld-musl-", 12) == 0){
        int L = strlen(p);
        if(L > 8 && strcmp(p + (L - 5), ".so.1") == 0)
          musl_ld = 1;
      }
      if(musl_ld){
        const char *fallback = "/musl/lib/libc.so";
        log_debug("exec: try musl fallback %s for %s", fallback, interp_path);
        ip_interp = namei((char *)fallback);
        if(ip_interp != 0){
          safestrcpy(interp_path, fallback, sizeof(interp_path));
        }
      }
    }
    if(ip_interp == 0){
      log_error("exec: interp not found: %s", interp_path);
      err = -ENOENT;
      goto bad;
    }
    ilock(ip_interp);
    if(readi(ip_interp, 0, (uint64)&interp_elf, 0, sizeof(interp_elf)) != sizeof(interp_elf)){
      log_error("exec: read interp elf failed: %s", interp_path);
      err = -EIO;
      goto bad;
    }
    if(interp_elf.magic != ELF_MAGIC){
      log_error("exec: bad interp magic: %s", interp_path);
      err = -ENOEXEC;
      goto bad;
    }

    // 动态链接器通常是 ET_DYN，这里选择一个简单的基址直接映射。
    interp_base = PGROUNDUP(sz);
    for(i = 0, off = interp_elf.phoff; i < interp_elf.phnum; i++, off += sizeof(ph)){
      if(readi(ip_interp, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph)){
        log_error("exec: read interp phdr %d failed: %s", i, interp_path);
        err = -EIO;
        goto bad;
      }
      if(ph.type != ELF_PROG_LOAD)
        continue;
      if(ph.memsz < ph.filesz){
        log_error("exec: interp memsz < filesz: %s", interp_path);
        err = -ENOEXEC;
        goto bad;
      }
      if(ph.vaddr + ph.memsz < ph.vaddr){
        log_error("exec: interp vaddr overflow: %s", interp_path);
        err = -ENOEXEC;
        goto bad;
      }
      uint64 va_end = PGROUNDUP(interp_base + ph.vaddr + ph.memsz);
      if((sz = uvmalloc(pagetable, sz, va_end, flags2perm(ph.flags))) == 0)
        { err = -ENOMEM; goto bad; }
      if(loadseg(pagetable, interp_base + ph.vaddr, ip_interp, ph.off, ph.filesz) < 0)
        { err = -EIO; goto bad; }
    }
    iunlockput(ip_interp);
    end_op(ROOTDEV);
    interp_op = 0;
    ip_interp = 0;
    interp_entry = interp_base + interp_elf.entry;
  }

  // Allocate user stack pages plus a guard page at the bottom.
  sz = PGROUNDUP(sz);
  uint64 stack_pages = USERSTACK_PAGES;
  uint64 stack_total = (stack_pages + 1) * PGSIZE;
  if((sz = uvmalloc(pagetable, sz, sz + stack_total, PTE_R|PTE_W)) == 0)
    { err = -ENOMEM; goto bad; }
  uvmclear(pagetable, sz - stack_total);
  sp = sz;
  stackbase = sp - stack_pages * PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      { err = -EFAULT; goto bad; }
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      { err = -EFAULT; goto bad; }
    ustack[argc] = sp;
  }
  ustack[argc] = 0; // argv[argc] = NULL

  int envc = 0;
  int envp_idx = argc + 1;
  if(envp){
    for(envc = 0; envp[envc]; envc++){
      if(envc >= MAXARG)
        goto bad;
      sp -= strlen(envp[envc]) + 1;
      sp -= sp % 16;
      if(sp < stackbase)
        goto bad;
      if(copyout(pagetable, sp, envp[envc], strlen(envp[envc]) + 1) < 0)
        goto bad;
      ustack[envp_idx + envc] = sp;
    }
  }
  ustack[envp_idx + envc] = 0; // envp[envc] = NULL

  // ! 可能这里的位置还有问题
  int auxv_idx = envp_idx + envc + 1;
  uint64 phdr = load_bias + elf.phoff;
  ustack[auxv_idx++] = AT_PHDR;   ustack[auxv_idx++] = phdr;
  ustack[auxv_idx++] = AT_PHENT;  ustack[auxv_idx++] = sizeof(struct proghdr);
  ustack[auxv_idx++] = AT_PHNUM;  ustack[auxv_idx++] = elf.phnum;
  ustack[auxv_idx++] = AT_PAGESZ; ustack[auxv_idx++] = PGSIZE;
  ustack[auxv_idx++] = AT_ENTRY;  ustack[auxv_idx++] = elf.entry;
  if(have_interp){
    ustack[auxv_idx++] = AT_BASE;  ustack[auxv_idx++] = interp_base;
  }
  ustack[auxv_idx++] = AT_NULL;   ustack[auxv_idx++] = 0;

  // push the array of argv[] pointers.
  int stack_entries = auxv_idx;
  sp -= stack_entries * sizeof(uint64);
  // 调整，使最终栈指针（含 argc）保持 16 字节对齐，同时保证 argv 紧跟在 argc 之后
  if(((sp - sizeof(uint64)) & 15) != 0){
    sp -= sizeof(uint64);
  }
  if(sp < stackbase)
    { err = -EFAULT; goto bad; }
  if(copyout(pagetable, sp, (char *)ustack, stack_entries * sizeof(uint64)) < 0)
    { err = -EFAULT; goto bad; }
  // Place argc on stack so user CRT can read [argc][argv*...] from SP
  uint64 argc64 = argc;
  uint64 sp_argv = sp;
  sp -= sizeof(uint64); // argc slot
  if(sp < stackbase)
    { err = -EFAULT; goto bad; }
  if(copyout(pagetable, sp, (char *)&argc64, sizeof(uint64)) < 0)
    { err = -EFAULT; goto bad; }
  // Set registers for main(argc, argv, envp)
  p->trapframe->a0 = argc;
  p->trapframe->a1 = sp_argv;
  p->trapframe->a2 = sp_argv + (argc + 1) * sizeof(uint64);

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image.
  vma_free_all(p);
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  log_debug("[exec] %s: sz=%p entry=%p (interp=%d interp_entry=%p)\n",
         path, sz, elf.entry, have_interp, interp_entry);

  // Create VMA for a large address range to handle lazy page faults.
  // This is important because dynamically linked programs may reference
  // addresses beyond sz (e.g., for libraries loaded by mmap that aren't
  // tracked properly). We create a VMA up to 2GB to catch all possible
  // addresses the program might try to access.
  uint64 vma_end = 0x80000000UL;  // 2GB
  if (vma_end < sz)
    vma_end = sz;
  if (vma_add(p, 0, vma_end, PROT_READ | PROT_WRITE | PROT_EXEC,
              MAP_PRIVATE | MAP_ANONYMOUS, 0, 0, sz) < 0) {
    log_error("[exec] Warning: failed to create VMA for address space\n");
  }

  // 如果是动态链接，先跳转到解释器入口，解释器负责加载主程序。
  p->trapframe->epc = have_interp ? interp_entry : elf.entry;
  p->trapframe->sp = sp; // initial stack pointer (points to argc)
  proc_freepagetable(oldpagetable, oldsz);

  // Close file descriptors marked close-on-exec.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] && (p->fdflags[fd] & FD_CLOEXEC)){
      struct file *f = p->ofile[fd];
      p->ofile[fd] = 0;
      p->fdflags[fd] = 0;
      fileclose(f);
    }
  }

  return argc; // ends up in a0 (argc)

 bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op(ROOTDEV);
  }
  if(ip_interp){
    iunlockput(ip_interp);
    if(interp_op)
      end_op(ROOTDEV);
  } else if(interp_op){
    end_op(ROOTDEV);
  }
  return err;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint64 i = 0;
  while(i < sz){
    uint64 va0 = PGROUNDDOWN(va + i);
    uint64 pa = walkaddr(pagetable, va0);
    if(pa == 0)
      panic("loadseg: address should exist");
    uint64 pageoff = (va + i) - va0;
    uint64 n = PGSIZE - pageoff;
    if(n > sz - i)
      n = sz - i;
    if(readi(ip, 0, (uint64)(pa + pageoff), offset + i, n) != n)
      return -1;
    i += n;
  }
  return 0;
}
