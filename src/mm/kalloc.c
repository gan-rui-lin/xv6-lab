// Physical memory allocator implementing a buddy system for page-sized blocks
// and a slab allocator for frequently used small kernel objects.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

extern char end[]; // defined by kernel.ld, marks end of kernel image
static void freerange(void *pa_start, void *pa_end);

#define MAX_BUDDY_ORDER 15
#define MAX_BUDDY_PAGES ((PHYSTOP - KERNBASE) / PGSIZE)

#define SLAB_MIN_SIZE 32
#define SLAB_MAX_ALLOC 2048
#define SLAB_CLASS_COUNT 7
#define SLAB_MAX_OBJECTS (PGSIZE / SLAB_MIN_SIZE)
#define SLAB_BITMAP_WORDS ((SLAB_MAX_OBJECTS + 63) / 64)
#define SLAB_EMPTY_RESERVE 1

#define KMALLOC_LARGE_MAGIC 0xBADC0DEDA7ULL

static const uint32 slab_size_classes[SLAB_CLASS_COUNT] = {
    32, 64, 128, 256, 512, 1024, 2048,
};

static const char *slab_cache_names[SLAB_CLASS_COUNT] = {
    "slab32", "slab64", "slab128", "slab256",
    "slab512", "slab1024", "slab2048",
};

struct slab_page;
struct slab_cache;

struct buddy_page {
    struct buddy_page *next;
    struct buddy_page *prev;
    struct slab_page *slab;
    uint32 index;
    uint8 order;
    uint8 is_free;
    uint16 refcnt;
};

struct buddy_area {
    struct buddy_page *head;
};

enum slab_state {
    SLAB_EMPTY = 0,
    SLAB_PARTIAL = 1,
    SLAB_FULL = 2,
};

struct slab_page {
    struct slab_page *next;
    struct slab_cache *cache;
    void *mem;
    uint16 obj_size;
    uint16 obj_count;
    uint16 free_count;
    uint8 order;
    uint8 state;
    uint64 bitmap[SLAB_BITMAP_WORDS];
};

struct slab_cache {
    struct spinlock lock;
    uint32 obj_size;
    uint32 empty_slabs;
    struct slab_page *partial;
    struct slab_page *full;
    struct slab_page *empty;
};

struct kmalloc_large_header {
    uint64 order;
    uint64 magic;
};

static struct buddy_page buddy_pages[MAX_BUDDY_PAGES];
static struct buddy_area buddy_free_areas[MAX_BUDDY_ORDER + 1];
static int buddy_top_order;

static struct slab_page slab_page_pool[MAX_BUDDY_PAGES];
static struct slab_page *slab_page_free;
static struct spinlock slab_meta_lock;
static struct slab_cache slab_caches[SLAB_CLASS_COUNT];

struct {
    struct spinlock lock;
    uint64 managed_start;
    uint64 managed_end;
    uint32 base_idx;
    uint32 page_count;
} kmem;

static inline uint32
pa2index(uint64 pa)
{
    return (pa - KERNBASE) / PGSIZE;
}

static inline uint64
index2pa(uint32 idx)
{
    return KERNBASE + ((uint64)idx << PGSHIFT);
}

static inline struct buddy_page *
pa2page(uint64 pa)
{
    return &buddy_pages[pa2index(pa)];
}

static void buddy_init(void);
static void slab_init(void);
static void *buddy_alloc_pages_internal(int order);
static void buddy_free_pages_internal(void *pa, int order);
static void *kmalloc_large(uint64 size);
static void kmalloc_large_free(void *addr);
static struct slab_page *slab_meta_alloc(void);
static void slab_meta_free(struct slab_page *page);
static void slab_track_pages(struct slab_page *slab, void *mem, int order);
static void slab_untrack_pages(struct slab_page *slab);
static void slab_list_remove(struct slab_page **head, struct slab_page *slab);
static int slab_find_class(uint64 size);
static void *slab_alloc_from_cache(struct slab_cache *cache);
static struct slab_page *slab_create_page(struct slab_cache *cache);
static void slab_destroy_page(struct slab_page *slab);
static struct slab_page *slab_from_addr(void *addr);
static void slab_free_object(struct slab_page *slab, void *addr);

void kref_inc(uint64 pa);
int  kref_dec(uint64 pa);
int  kref_get(uint64 pa);

void
kinit(void)
{
    initlock(&kmem.lock, "kmem");
    buddy_init();
    freerange(end, (void *)PHYSTOP);
    slab_init();
}

static void
buddy_init(void)
{
    kmem.managed_start = PGROUNDUP((uint64)end);
    kmem.managed_end = PGROUNDDOWN((uint64)PHYSTOP);
    if (kmem.managed_start >= kmem.managed_end)
        panic("kinit: no physical memory");

    kmem.base_idx = pa2index(kmem.managed_start);
    kmem.page_count = (kmem.managed_end - kmem.managed_start) / PGSIZE;
    if (kmem.page_count == 0)
        panic("kinit: zero managed pages");

    for (int i = 0; i <= MAX_BUDDY_ORDER; i++)
        buddy_free_areas[i].head = 0;

    for (uint32 i = 0; i < MAX_BUDDY_PAGES; i++) {
        buddy_pages[i].next = 0;
        buddy_pages[i].prev = 0;
        buddy_pages[i].slab = 0;
        buddy_pages[i].index = i;
        buddy_pages[i].order = 0;
        buddy_pages[i].is_free = 0;
        buddy_pages[i].refcnt = 0;
    }

    buddy_top_order = 0;
    while (buddy_top_order < MAX_BUDDY_ORDER &&
           ((uint64)1 << buddy_top_order) <= kmem.page_count)
        buddy_top_order++;
    if (buddy_top_order > 0)
        buddy_top_order--;
}

static void
freerange(void *pa_start, void *pa_end)
{
    char *p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
        kfree(p);
}

static void
buddy_list_push(int order, struct buddy_page *page)
{
    page->next = buddy_free_areas[order].head;
    page->prev = 0;
    if (page->next)
        page->next->prev = page;
    buddy_free_areas[order].head = page;
    page->is_free = 1;
    page->order = order;
}

static void
buddy_list_remove(int order, struct buddy_page *page)
{
    if (page->prev)
        page->prev->next = page->next;
    else
        buddy_free_areas[order].head = page->next;
    if (page->next)
        page->next->prev = page->prev;
    page->next = page->prev = 0;
    page->is_free = 0;
}

static void *
buddy_alloc_pages_internal(int order)
{
    if (order > buddy_top_order)
        return 0;

    acquire(&kmem.lock);
    int current = order;
    struct buddy_page *page = 0;

    while (current <= buddy_top_order) {
        page = buddy_free_areas[current].head;
        if (page)
            break;
        current++;
    }

    if (page == 0) {
        release(&kmem.lock);
        return 0;
    }

    buddy_list_remove(current, page);

    while (current > order) {
        current--;
        uint32 buddy_index = page->index + (1u << current);
        struct buddy_page *buddy = &buddy_pages[buddy_index];
        buddy->slab = 0;
        buddy_list_push(current, buddy);
    }

    page->order = order;
    page->is_free = 0;
    void *addr = (void *)index2pa(page->index);
    release(&kmem.lock);
    return addr;
}

static void
buddy_free_pages_internal(void *pa, int order)
{
    uint64 addr = (uint64)pa;
    if ((addr % PGSIZE) != 0)
        panic("kfree");
    if (addr < kmem.managed_start || addr >= kmem.managed_end)
        panic("kfree");

    uint32 idx = pa2index(addr);
    struct buddy_page *page = &buddy_pages[idx];

    acquire(&kmem.lock);

    if (page->is_free)
        panic("kfree double");

    if (order < 0 || order > buddy_top_order)
        order = page->order;

    page->order = order;
    page->slab = 0;

    while (order < buddy_top_order) {
        uint32 rel = idx - kmem.base_idx;
        uint32 buddy_rel = rel ^ (1u << order);
        if (buddy_rel >= kmem.page_count)
            break;
        uint32 buddy_idx = kmem.base_idx + buddy_rel;
        struct buddy_page *buddy = &buddy_pages[buddy_idx];
        if (!buddy->is_free || buddy->order != order)
            break;
        buddy_list_remove(order, buddy);
        if (buddy_idx < idx) {
            idx = buddy_idx;
            page = buddy;
        }
        order++;
    }

    page->order = order;
    buddy_list_push(order, page);
    release(&kmem.lock);
}

void *
kalloc(void)
{
    void *pa = buddy_alloc_pages_internal(0);
    if (pa)
        memset(pa, 5, PGSIZE);
    if (pa)
        kref_inc((uint64)pa);
    return pa;
}

void
kfree(void *pa)
{
    if (pa == 0)
        panic("kfree");

    if (((uint64)pa % PGSIZE) != 0)
        panic("kfree");
    if ((uint64)pa < kmem.managed_start || (uint64)pa >= kmem.managed_end)
        panic("kfree");

    struct buddy_page *page = pa2page((uint64)pa);
    if (page->slab)
        panic("kfree slab page");

    int ref = kref_get((uint64)pa);
    if (ref > 1) {
        kref_dec((uint64)pa);
        return;
    }
    if (ref == 1)
        kref_dec((uint64)pa);

    memset(pa, 1, PGSIZE);
    buddy_free_pages_internal(pa, 0);
}

void
kref_inc(uint64 pa)
{
    if ((pa % PGSIZE) != 0 || pa < kmem.managed_start || pa >= kmem.managed_end)
        panic("kref_inc");

    acquire(&kmem.lock);
    struct buddy_page *page = pa2page(pa);
    if (page->refcnt == 0 && page->is_free)
        panic("kref_inc free");
    page->refcnt++;
    release(&kmem.lock);
}

int
kref_dec(uint64 pa)
{
    if ((pa % PGSIZE) != 0 || pa < kmem.managed_start || pa >= kmem.managed_end)
        panic("kref_dec");

    acquire(&kmem.lock);
    struct buddy_page *page = pa2page(pa);
    if (page->refcnt == 0)
        panic("kref_dec underflow");
    page->refcnt--;
    int ref = page->refcnt;
    release(&kmem.lock);
    return ref;
}

int
kref_get(uint64 pa)
{
    if ((pa % PGSIZE) != 0 || pa < kmem.managed_start || pa >= kmem.managed_end)
        panic("kref_get");

    acquire(&kmem.lock);
    struct buddy_page *page = pa2page(pa);
    int ref = page->refcnt;
    release(&kmem.lock);
    return ref;
}

static struct slab_page *
slab_meta_alloc(void)
{
    acquire(&slab_meta_lock);
    struct slab_page *page = slab_page_free;
    if (page)
        slab_page_free = page->next;
    release(&slab_meta_lock);
    if (page)
        memset(page, 0, sizeof(*page));
    return page;
}

static void
slab_meta_free(struct slab_page *page)
{
    acquire(&slab_meta_lock);
    page->next = slab_page_free;
    slab_page_free = page;
    release(&slab_meta_lock);
}

static void
slab_track_pages(struct slab_page *slab, void *mem, int order)
{
    slab->mem = mem;
    slab->order = order;
    uint32 start = pa2index((uint64)mem);
    uint32 pages = 1u << order;
    for (uint32 i = 0; i < pages; i++)
        buddy_pages[start + i].slab = slab;
}

static void
slab_untrack_pages(struct slab_page *slab)
{
    uint32 start = pa2index((uint64)slab->mem);
    uint32 pages = 1u << slab->order;
    for (uint32 i = 0; i < pages; i++)
        buddy_pages[start + i].slab = 0;
}

static void
slab_list_remove(struct slab_page **head, struct slab_page *slab)
{
    struct slab_page **cur = head;
    while (*cur) {
        if (*cur == slab) {
            *cur = slab->next;
            slab->next = 0;
            return;
        }
        cur = &(*cur)->next;
    }
}

static void
slab_init(void)
{
    initlock(&slab_meta_lock, "slabmeta");
    slab_page_free = 0;
    for (int i = 0; i < MAX_BUDDY_PAGES; i++) {
        slab_page_pool[i].next = slab_page_free;
        slab_page_free = &slab_page_pool[i];
    }

    for (int i = 0; i < SLAB_CLASS_COUNT; i++) {
        initlock(&slab_caches[i].lock, (char *)slab_cache_names[i]);
        slab_caches[i].obj_size = slab_size_classes[i];
        slab_caches[i].partial = 0;
        slab_caches[i].full = 0;
        slab_caches[i].empty = 0;
        slab_caches[i].empty_slabs = 0;
    }
}

static inline void
slab_set_bit(struct slab_page *slab, uint32 idx)
{
    slab->bitmap[idx >> 6] |= (1ULL << (idx & 63));
}

static inline void
slab_clear_bit(struct slab_page *slab, uint32 idx)
{
    slab->bitmap[idx >> 6] &= ~(1ULL << (idx & 63));
}

static inline int
slab_test_bit(struct slab_page *slab, uint32 idx)
{
    return (slab->bitmap[idx >> 6] >> (idx & 63)) & 1ULL;
}

static int
slab_find_free_index(struct slab_page *slab)
{
    for (uint32 i = 0; i < slab->obj_count; i++) {
        if (!slab_test_bit(slab, i))
            return i;
    }
    return -1;
}

static struct slab_page *
slab_create_page(struct slab_cache *cache)
{
    struct slab_page *slab = slab_meta_alloc();
    if (slab == 0)
        return 0;

    void *mem = buddy_alloc_pages_internal(0);
    if (mem == 0) {
        slab_meta_free(slab);
        return 0;
    }

    slab_track_pages(slab, mem, 0);
    slab->cache = cache;
    slab->obj_size = cache->obj_size;
    slab->obj_count = (PGSIZE << slab->order) / slab->obj_size;
    if (slab->obj_count > SLAB_MAX_OBJECTS)
        slab->obj_count = SLAB_MAX_OBJECTS;
    slab->free_count = slab->obj_count;
    slab->state = SLAB_EMPTY;
    memset(slab->bitmap, 0, sizeof(slab->bitmap));
    return slab;
}

static void
slab_destroy_page(struct slab_page *slab)
{
    slab_untrack_pages(slab);
    buddy_free_pages_internal(slab->mem, slab->order);
    slab_meta_free(slab);
}

static int
slab_find_class(uint64 size)
{
    for (int i = 0; i < SLAB_CLASS_COUNT; i++) {
        if (size <= slab_size_classes[i])
            return i;
    }
    return -1;
}

static void *
slab_alloc_from_cache(struct slab_cache *cache)
{
    struct slab_page *created = 0;

    for (;;) {
        acquire(&cache->lock);
        struct slab_page *slab = cache->partial;
        if (slab == 0 && cache->empty) {
            slab = cache->empty;
            cache->empty = slab->next;
            cache->empty_slabs--;
            slab->next = cache->partial;
            cache->partial = slab;
            slab->state = SLAB_PARTIAL;
        }
        if (slab == 0 && created) {
            slab = created;
            created = 0;
            slab->next = cache->partial;
            cache->partial = slab;
            slab->state = SLAB_PARTIAL;
        }
        if (slab) {
            int idx = slab_find_free_index(slab);
            if (idx < 0) {
                // shouldn't happen but guard by moving to full list
                cache->partial = slab->next;
                slab->next = cache->full;
                cache->full = slab;
                slab->state = SLAB_FULL;
                release(&cache->lock);
                if (created)
                    slab_destroy_page(created);
                continue;
            }
            slab_set_bit(slab, idx);
            slab->free_count--;
            void *addr = (void *)((char *)slab->mem + (uint64)idx * slab->obj_size);
            if (slab->free_count == 0) {
                cache->partial = slab->next;
                slab->next = cache->full;
                cache->full = slab;
                slab->state = SLAB_FULL;
            }
            release(&cache->lock);
            if (created)
                slab_destroy_page(created);
            return addr;
        }
        release(&cache->lock);

        if (created == 0) {
            created = slab_create_page(cache);
            if (created == 0)
                return 0;
        } else {
            slab_destroy_page(created);
            return 0;
        }
    }
}

static struct slab_page *
slab_from_addr(void *addr)
{
    uint64 pa = (uint64)addr;
    if (pa < kmem.managed_start || pa >= kmem.managed_end)
        return 0;
    uint32 idx = pa2index(pa);
    if (idx >= MAX_BUDDY_PAGES)
        return 0;
    return buddy_pages[idx].slab;
}

static void
slab_free_object(struct slab_page *slab, void *addr)
{
    struct slab_cache *cache = slab->cache;
    uint64 offset = (uint64)addr - (uint64)slab->mem;
    if (offset % slab->obj_size)
        panic("kmfree");
    uint32 idx = offset / slab->obj_size;
    if (idx >= slab->obj_count)
        panic("kmfree");

    struct slab_page *to_release = 0;

    acquire(&cache->lock);
    if (!slab_test_bit(slab, idx)) {
        release(&cache->lock);
        panic("kmfree");
    }
    slab_clear_bit(slab, idx);
    slab->free_count++;

    if (slab->free_count == slab->obj_count) {
        if (slab->state == SLAB_FULL)
            slab_list_remove(&cache->full, slab);
        else if (slab->state == SLAB_PARTIAL)
            slab_list_remove(&cache->partial, slab);
        slab->state = SLAB_EMPTY;
        slab->next = cache->empty;
        cache->empty = slab;
        cache->empty_slabs++;
        if (cache->empty_slabs > SLAB_EMPTY_RESERVE) {
            struct slab_page **cur = &cache->empty;
            while ((*cur) && (*cur)->next)
                cur = &(*cur)->next;
            to_release = *cur;
            if (to_release)
                *cur = 0;
            if (to_release)
                cache->empty_slabs--;
        }
    } else if (slab->state == SLAB_FULL) {
        slab_list_remove(&cache->full, slab);
        slab->next = cache->partial;
        cache->partial = slab;
        slab->state = SLAB_PARTIAL;
    }

    release(&cache->lock);

    if (to_release)
        slab_destroy_page(to_release);
}

// Allocate size bytes of kernel memory.
void *
kmalloc(uint64 size)
{
    if (size == 0)
        return 0;

    size = (size + sizeof(uint64) - 1) & ~(sizeof(uint64) - 1);

    if (size <= SLAB_MAX_ALLOC) {
        int cid = slab_find_class(size);
        if (cid >= 0)
            return slab_alloc_from_cache(&slab_caches[cid]);
    }

    return kmalloc_large(size);
}

static void *
kmalloc_large(uint64 size)
{
    uint64 total = size + sizeof(struct kmalloc_large_header);
    int order = 0;
    uint64 block = PGSIZE;
    while (block < total) {
        order++;
        block <<= 1;
        if (order > buddy_top_order)
            return 0;
    }
    void *mem = buddy_alloc_pages_internal(order);
    if (mem == 0)
        return 0;
    struct kmalloc_large_header *hdr = (struct kmalloc_large_header *)mem;
    hdr->order = order;
    hdr->magic = KMALLOC_LARGE_MAGIC;
    return (void *)(hdr + 1);
}

static void
kmalloc_large_free(void *addr)
{
    struct kmalloc_large_header *hdr = ((struct kmalloc_large_header *)addr) - 1;
    if (hdr->magic != KMALLOC_LARGE_MAGIC)
        panic("kmfree");
    hdr->magic = 0;
    buddy_free_pages_internal((void *)hdr, hdr->order);
}

void
kmfree(void *addr)
{
    if (addr == 0)
        return;

    struct slab_page *slab = slab_from_addr(addr);
    if (slab) {
        slab_free_object(slab, addr);
        return;
    }

    kmalloc_large_free(addr);
}
