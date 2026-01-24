#ifndef XV6_VMA_H
#define XV6_VMA_H

#include "types.h"

struct proc;
struct file;

#define VM_FAULT_READ  0x1
#define VM_FAULT_WRITE 0x2
#define VM_FAULT_EXEC  0x4

struct vma {
  uint64 start;
  uint64 end;
  int prot;
  int flags;
  struct file *file;
  uint64 offset;
  uint64 len;
  struct vma *next;
};

void vma_init(struct proc *p);
int  vma_add(struct proc *p, uint64 start, uint64 end, int prot, int flags,
             struct file *file, uint64 offset, uint64 len);
struct vma *vma_find(struct proc *p, uint64 va);
int  vma_unmap(struct proc *p, uint64 start, uint64 end);
int  vma_protect(struct proc *p, uint64 start, uint64 end, int prot);
int  vma_copy(struct proc *dst, struct proc *src);
void vma_free_all(struct proc *p);
int  vma_handle_fault(struct proc *p, uint64 va, int access);

#endif
