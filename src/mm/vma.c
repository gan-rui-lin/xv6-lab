#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"
#include "mm/vma.h"
#include "sleeplock.h"
#include "fs/file.h"
#include "fs/fs.h"

static struct vma *
vma_alloc(void)
{
  struct vma *v = (struct vma *)kalloc();
  if (v)
    memset(v, 0, PGSIZE);
  return v;
}

static void
vma_free(struct vma *v)
{
  if (v->file)
    fileclose(v->file);
  kfree((void *)v);
}

static struct vma *
vma_clone(struct vma *src)
{
  struct vma *v = vma_alloc();
  if (v == 0)
    return 0;
  v->start = src->start;
  v->end = src->end;
  v->prot = src->prot;
  v->flags = src->flags;
  v->offset = src->offset;
  v->len = src->len;
  v->file = src->file ? filedup(src->file) : 0;
  return v;
}

void
vma_init(struct proc *p)
{
  p->vma = 0;
}

int
vma_add(struct proc *p, uint64 start, uint64 end, int prot, int flags,
        struct file *file, uint64 offset, uint64 len)
{
  struct vma *v;

  if (start >= end)
    return -1;
  v = vma_alloc();
  if (v == 0)
    return -1;
  v->start = start;
  v->end = end;
  v->prot = prot;
  v->flags = flags;
  v->offset = offset;
  v->len = len;
  v->file = file ? filedup(file) : 0;
  v->next = p->vma;
  p->vma = v;
  return 0;
}

struct vma *
vma_find(struct proc *p, uint64 va)
{
  struct vma *v;
  for (v = p->vma; v; v = v->next) {
    if (va >= v->start && va < v->end)
      return v;
  }
  return 0;
}

static struct vma *
vma_split(struct vma *v, uint64 split)
{
  struct vma *nv;
  uint64 left_len;
  uint64 right_len;
  uint64 split_off;

  if (split <= v->start || split >= v->end)
    return 0;

  nv = vma_clone(v);
  if (nv == 0)
    return 0;

  split_off = split - v->start;
  left_len = v->len;
  if (left_len > split_off)
    left_len = split_off;
  right_len = 0;
  if (v->len > split_off)
    right_len = v->len - split_off;

  nv->start = split;
  nv->end = v->end;
  nv->offset = v->offset + split_off;
  nv->len = right_len;

  v->end = split;
  v->len = left_len;

  nv->next = v->next;
  v->next = nv;
  return nv;
}

int
vma_unmap(struct proc *p, uint64 start, uint64 end)
{
  struct vma **pp = &p->vma;
  struct vma *v;

  if (start >= end)
    return 0;

  while ((v = *pp) != 0) {
    if (end <= v->start || start >= v->end) {
      pp = &v->next;
      continue;
    }

    if (start <= v->start && end >= v->end) {
      *pp = v->next;
      vma_free(v);
      continue;
    }

    if (start <= v->start && end < v->end) {
      uint64 cut = end - v->start;
      v->start = end;
      v->offset += cut;
      if (v->len > cut)
        v->len -= cut;
      else
        v->len = 0;
      pp = &v->next;
      continue;
    }

    if (start > v->start && end >= v->end) {
      uint64 cut = v->end - start;
      v->end = start;
      if (v->len > cut)
        v->len -= cut;
      else
        v->len = 0;
      pp = &v->next;
      continue;
    }

    if (start > v->start && end < v->end) {
      struct vma *right = vma_split(v, end);
      if (right == 0) {
        // vma_split 已分配内存但分割失败
        // 不过在当前实现中，vma_split 失败时会返回0且不分配，所以安全
        return -1;
      }
      v->end = start;
      if (v->len > start - v->start)
        v->len = start - v->start;
      else
        v->len = 0;
      pp = &right->next;
      continue;
    }

    pp = &v->next;
  }
  return 0;
}

int
vma_protect(struct proc *p, uint64 start, uint64 end, int prot)
{
  struct vma *v;

  if (start >= end)
    return 0;

  // 第一遍：分割所有需要分割的 VMA
  v = p->vma;
  while (v) {
    if (end <= v->start || start >= v->end) {
      v = v->next;
      continue;
    }

    // 需要在 start 处分割
    if (start > v->start && start < v->end) {
      struct vma *right = vma_split(v, start);
      if (right == 0) {
        // 分割失败，VMA 链表可能已部分修改
        // 但 vma_split 在失败时返回0且不分配，所以安全
        return -1;
      }
      v = right; // 继续处理右侧部分
      continue;
    }

    // 需要在 end 处分割
    if (end > v->start && end < v->end) {
      if (vma_split(v, end) == 0) {
        return -1;
      }
    }

    v = v->next;
  }

  // 第二遍：修改权限
  v = p->vma;
  while (v) {
    if (end <= v->start || start >= v->end) {
      v = v->next;
      continue;
    }

    // 完全覆盖的 VMA
    if (start <= v->start && end >= v->end) {
      v->prot = prot;
    }

    v = v->next;
  }
  return 0;
}

void
vma_free_all(struct proc *p)
{
  struct vma *v = p->vma;
  while (v) {
    struct vma *next = v->next;
    vma_free(v);
    v = next;
  }
  p->vma = 0;
}

int
vma_copy(struct proc *dst, struct proc *src)
{
  struct vma *v;
  struct vma *head = 0;
  struct vma *tail = 0;

  for (v = src->vma; v; v = v->next) {
    struct vma *nv = vma_clone(v);
    if (nv == 0) {
      struct vma *tmp = head;
      while (tmp) {
        struct vma *next = tmp->next;
        vma_free(tmp);
        tmp = next;
      }
      return -1;
    }
    nv->next = 0;
    if (tail)
      tail->next = nv;
    else
      head = nv;
    tail = nv;
  }
  dst->vma = head;
  return 0;
}

static int
vma_check_access(struct vma *v, int access)
{
  if ((access & VM_FAULT_READ) && !(v->prot & PROT_READ))
    return -1;
  if ((access & VM_FAULT_WRITE) && !(v->prot & PROT_WRITE))
    return -1;
  if ((access & VM_FAULT_EXEC) && !(v->prot & PROT_EXEC))
    return -1;
  return 0;
}

static int
vma_map_zero_page(struct proc *p, struct vma *v, uint64 va, int access)
{
  uint64 pa = get_zero_page_pa();
  int perm = PTE_U;

  // TODO 处理 access 参数
  (void)access;
  if (pa == 0)
    return -1;

  if (v->prot & PROT_READ)
    perm |= PTE_R;
  if (v->prot & PROT_EXEC)
    perm |= PTE_X;
  if ((v->prot & PROT_WRITE) && !(v->flags & MAP_SHARED))
    perm |= PTE_COW;
  if ((v->prot & PROT_WRITE) && (v->flags & MAP_SHARED))
    perm |= PTE_W;

  if (mappages(p->pagetable, va, PGSIZE, pa, perm) != 0)
    return -1;
  kref_inc(pa);
  return 0;
}

static int
vma_map_file_page(struct proc *p, struct vma *v, uint64 va, int access)
{
  char *mem;
  uint64 file_off;
  uint64 page_off;
  uint64 n;
  int perm = PTE_U;

  mem = kalloc();
  if (mem == 0)
    return -1;
  memset(mem, 0, PGSIZE);

  page_off = va - v->start;
  file_off = v->offset + page_off;
  if (v->file && file_off < v->offset + v->len) {
    n = v->offset + v->len - file_off;
    if (n > PGSIZE)
      n = PGSIZE;
    if (v->file->type != FD_INODE) {
      kfree(mem);
      return -1;
    }
    ilock(v->file->ip);
    if (readi(v->file->ip, 0, (uint64)mem, file_off, n) != n) {
      iunlock(v->file->ip);
      kfree(mem);
      return -1;
    }
    iunlock(v->file->ip);
  }

  if (v->prot & PROT_READ)
    perm |= PTE_R;
  if (v->prot & PROT_EXEC)
    perm |= PTE_X;

  if (v->flags & MAP_SHARED) {
    if (v->prot & PROT_WRITE)
      perm |= PTE_W;
  } else {
    if (v->prot & PROT_WRITE) {
      if (access & VM_FAULT_WRITE)
        perm |= PTE_W;
      else
        perm |= PTE_COW;
    }
  }

  if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0) {
    kfree(mem);
    return -1;
  }
  return 0;
}

int
vma_handle_fault(struct proc *p, uint64 va, int access)
{
  struct vma *v;
  uint64 va0;
  pte_t *pte;

  v = vma_find(p, va);
  if (v == 0)
    return -1;
  if (vma_check_access(v, access) < 0)
    return -1;

  va0 = PGROUNDDOWN(va);
  pte = walk(p->pagetable, va0, 0);
  if (pte && (*pte & PTE_V))
    return 0;

  if (v->file == 0) {
    if (access & VM_FAULT_WRITE) {
      char *mem = kalloc();
      if (mem == 0)
        return -1;
      memset(mem, 0, PGSIZE);
      int perm = PTE_U;
      if (v->prot & PROT_READ)
        perm |= PTE_R;
      if (v->prot & PROT_EXEC)
        perm |= PTE_X;
      if (v->prot & PROT_WRITE)
        perm |= PTE_W;
      if (mappages(p->pagetable, va0, PGSIZE, (uint64)mem, perm) != 0) {
        kfree(mem);
        return -1;
      }
      return 0;
    }
    return vma_map_zero_page(p, v, va0, access);
  }

  return vma_map_file_page(p, v, va0, access);
}
