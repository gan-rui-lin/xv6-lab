#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"
#include "errno.h"

#ifndef AT_NULL
#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_ENTRY 9
#endif

#define AUXV_ENTRIES 5

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
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();
  uint64 load_bias = 0;
  int first_load = 1;
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

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph)){
      log_error("exec: read phdr %d failed for %s", i, path);
      err = -EIO;
      goto bad;
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
    uint64 va_start = PGROUNDDOWN(ph.vaddr);
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

  int auxv_idx = envp_idx + envc + 1;
  uint64 phdr = load_bias + elf.phoff;
  ustack[auxv_idx++] = AT_PHDR;   ustack[auxv_idx++] = phdr;
  ustack[auxv_idx++] = AT_PHENT;  ustack[auxv_idx++] = sizeof(struct proghdr);
  ustack[auxv_idx++] = AT_PHNUM;  ustack[auxv_idx++] = elf.phnum;
  ustack[auxv_idx++] = AT_PAGESZ; ustack[auxv_idx++] = PGSIZE;
  ustack[auxv_idx++] = AT_ENTRY;  ustack[auxv_idx++] = elf.entry;
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
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer (points to argc)
  proc_freepagetable(oldpagetable, oldsz);

  return argc; // ends up in a0 (argc)

 bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
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
