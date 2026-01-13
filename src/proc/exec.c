#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

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
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz, sp, ustack[MAXARG + 4], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op(ROOTDEV);

  if((ip = namei(path)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    // Map the segment, allowing non-page-aligned vaddr by rounding.
    uint64 va_start = PGROUNDDOWN(ph.vaddr);
    uint64 va_end = PGROUNDUP(ph.vaddr + ph.memsz);
    if((sz = uvmalloc(pagetable, sz, va_end, flags2perm(ph.flags))) == 0)
      goto bad;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op(ROOTDEV);
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate two pages at the next page boundary.
  // Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = uvmalloc(pagetable, sz, sz + 2*PGSIZE, PTE_R|PTE_W)) == 0)
    goto bad;
  uvmclear(pagetable, sz-2*PGSIZE);
  sp = sz;
  stackbase = sp - PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;
  // 空的环境变量列表 envp[0] = 0
  ustack[argc + 1] = 0;
  // 追加一个 AT_NULL 的 auxv，防止 libc/ldso 读取垃圾
  ustack[argc + 2] = 0; // a_type = AT_NULL
  ustack[argc + 3] = 0; // a_val  = 0

  // push the array of argv[] pointers.
  // 额外的 3 项：envp[0]，auxv.type=AT_NULL，auxv.val=0
  sp -= (argc + 4) * sizeof(uint64);
  // 调整，使最终栈指针（含 argc）保持 16 字节对齐，同时保证 argv 紧跟在 argc 之后
  if(((sp - sizeof(uint64)) & 15) != 0){
    sp -= sizeof(uint64);
  }
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc + 4) * sizeof(uint64)) < 0)
    goto bad;
  // Place argc on stack so user CRT can read [argc][argv*...] from SP
  uint64 argc64 = argc;
  uint64 sp_argv = sp;
  sp -= sizeof(uint64); // argc slot
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)&argc64, sizeof(uint64)) < 0)
    goto bad;
  // Set registers for main(argc, argv)
  p->trapframe->a1 = sp_argv;

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
  return -1;
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
