// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Now backed by a buddy allocator
// plus a slab allocator for small kernel objects.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

extern char end[]; // first address after kernel.

struct run
{
    struct run *next;
};

#define MAX_PHYS_PAGES ((PHYSTOP - KERNBASE) / PGSIZE)
#define MAX_BUDDY_ORDER 18

struct buddy_allocator
{
    struct spinlock lock;
    char *base;
    char *limit;
    uint64 total_pages;
    int max_order;
    struct run *free_lists[MAX_BUDDY_ORDER + 1];
};

static struct buddy_allocator buddy;
static signed char buddy_page_order[MAX_PHYS_PAGES];

static inline uint64
pa_to_global_index(uint64 pa)
{
    return (pa - KERNBASE) / PGSIZE;
}

static inline uint64
pa_to_local_index(uint64 pa)
{
    return (pa - (uint64)buddy.base) / PGSIZE;
}

static inline uint64
index_to_pa(uint64 index)
{
    return (uint64)buddy.base + index * PGSIZE;
}

static void buddy_add_block_by_index(uint64 index, int order);
static void buddy_remove_block_from_list(struct run *block, int order);
static void *buddy_alloc_locked(int order);
static void buddy_free_locked(void *pa, int order);

#define SLAB_SIGNATURE 0xfeedbabe
#define KMALLOC_LARGE_SIGNATURE 0xdeadbeef

struct slab_cache;

struct slab_page
{
    uint32 signature;
    struct slab_page *next;
    struct slab_page *prev;
    struct slab_cache *cache;
    void *freelist;
    uint16 free_count;
    uint16 total_count;
};

struct slab_cache
{
    struct spinlock lock;
    struct slab_page *partial;
    struct slab_page *full;
    uint32 obj_size;
    uint32 objs_per_page;
    const char *name;
};

static const uint16 slab_cache_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
#define SLAB_CACHE_COUNT (sizeof(slab_cache_sizes) / sizeof(slab_cache_sizes[0]))
static const char *slab_cache_names[] = {
    "slab16", "slab32", "slab64", "slab128",
    "slab256", "slab512", "slab1024", "slab2048"};
static struct slab_cache slab_caches[SLAB_CACHE_COUNT];

struct kmalloc_large_header
{
    uint32 signature;
    uint32 order;
};

static inline uint64
slab_metadata_size(void)
{
    return (sizeof(struct slab_page) + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1);
}

static void slab_init(void);
static struct slab_cache *slab_cache_for_size(uint64 size);
static struct slab_page *slab_create_page(struct slab_cache *cache);
static void slab_page_add(struct slab_page **head, struct slab_page *page);
static void slab_page_remove(struct slab_page **head, struct slab_page *page);
static void *slab_alloc(struct slab_cache *cache);
static void slab_free(struct slab_cache *cache, void *addr);
static int order_for_size(uint64 size);
static void *kmalloc_large(uint64 size);
static uint64 buddy_free_pages(void);
static void kalloc_selftest_internal(void);

static void
buddy_add_block_by_index(uint64 index, int order)
{
    if (order > buddy.max_order || index >= buddy.total_pages)
        panic("buddy_add_block");
    uint64 pa = index_to_pa(index);
    if (pa < (uint64)buddy.base || pa >= (uint64)buddy.limit)
        panic("buddy_add_block range");
    struct run *r = (struct run *)pa;
    r->next = buddy.free_lists[order];
    buddy.free_lists[order] = r;
    uint64 global_index = pa_to_global_index(pa);
    if (global_index >= MAX_PHYS_PAGES)
        panic("buddy_add_block global");
    buddy_page_order[global_index] = order;
}

static void
buddy_remove_block_from_list(struct run *block, int order)
{
    struct run **prev = &buddy.free_lists[order];
    while (*prev && *prev != block)
        prev = &(*prev)->next;
    if (*prev == 0)
        panic("buddy_remove");
    *prev = block->next;
    block->next = 0;
}

static void *
buddy_alloc_locked(int order)
{
    if (order > buddy.max_order)
        return 0;

    for (int current = order; current <= buddy.max_order; current++)
    {
        struct run *block = buddy.free_lists[current];
        if (block == 0)
            continue;

        buddy.free_lists[current] = block->next;
        uint64 block_pa = (uint64)block;
        uint64 global_index = pa_to_global_index(block_pa);
        buddy_page_order[global_index] = -1;

        while (current > order)
        {
            current--;
            uint64 split_pa = block_pa + ((uint64)1 << current) * PGSIZE;
            uint64 split_index = pa_to_local_index(split_pa);
            buddy_add_block_by_index(split_index, current);
        }
        return (void *)block_pa;
    }
    return 0;
}

static void
buddy_free_locked(void *pa, int order)
{
    uint64 addr = (uint64)pa;
    if (order > buddy.max_order)
        panic("buddy_free order");
    if (addr < (uint64)buddy.base || addr >= (uint64)buddy.limit)
        panic("buddy_free range");
    if (addr % PGSIZE)
        panic("buddy_free align");

    uint64 size_pages = (uint64)1 << order;
    uint64 index = pa_to_local_index(addr);
    if (index >= buddy.total_pages)
        panic("buddy_free index");
    if ((index & (size_pages - 1)) != 0)
        panic("buddy_free unaligned block");

    uint64 global_index = pa_to_global_index(addr);
    if (global_index >= MAX_PHYS_PAGES)
        panic("buddy_free metadata");
    if (buddy_page_order[global_index] != -1)
        panic("buddy_free double");

    while (order < buddy.max_order)
    {
        uint64 buddy_index = index ^ (1ULL << order);
        if (buddy_index >= buddy.total_pages)
            break;
        uint64 buddy_pa = index_to_pa(buddy_index);
        uint64 buddy_global = pa_to_global_index(buddy_pa);
        if (buddy_global >= MAX_PHYS_PAGES)
            panic("buddy_free buddy meta");
        if (buddy_page_order[buddy_global] != order)
            break;

        buddy_remove_block_from_list((struct run *)buddy_pa, order);
        buddy_page_order[buddy_global] = -1;

        if (buddy_index < index)
            index = buddy_index;
        order++;
        size_pages <<= 1;
    }

    buddy_add_block_by_index(index, order);
}

static void
buddy_init_allocator(void)
{
    char *start = (char *)PGROUNDUP((uint64)end);
    char *end_pa = (char *)PGROUNDDOWN(PHYSTOP);
    if (end_pa <= start)
        panic("kinit: no memory");

    buddy.base = start;
    buddy.limit = end_pa;
    buddy.total_pages = ((uint64)buddy.limit - (uint64)buddy.base) / PGSIZE;
    if (buddy.total_pages == 0)
        panic("kinit: zero pages");

    buddy.max_order = 0;
    while (buddy.max_order < MAX_BUDDY_ORDER &&
           ((uint64)1 << buddy.max_order) <= buddy.total_pages)
    {
        buddy.max_order++;
    }
    if (buddy.max_order == 0)
        panic("kinit: order zero");
    buddy.max_order--;

    memset(buddy_page_order, -1, sizeof(buddy_page_order));
    for (int i = 0; i <= buddy.max_order; i++)
        buddy.free_lists[i] = 0;

    uint64 remaining = buddy.total_pages;
    uint64 index = 0;
    for (int order = buddy.max_order; order >= 0; order--)
    {
        uint64 block_pages = 1ULL << order;
        while (remaining >= block_pages)
        {
            buddy_add_block_by_index(index, order);
            index += block_pages;
            remaining -= block_pages;
        }
    }
}

static uint64
buddy_free_pages(void)
{
    uint64 free_pages = 0;
    acquire(&buddy.lock);
    for (int order = 0; order <= buddy.max_order; order++)
    {
        struct run *r = buddy.free_lists[order];
        while (r)
        {
            free_pages += 1ULL << order;
            r = r->next;
        }
    }
    release(&buddy.lock);
    return free_pages;
}

static void
slab_page_add(struct slab_page **head, struct slab_page *page)
{
    page->prev = 0;
    page->next = *head;
    if (*head)
        (*head)->prev = page;
    *head = page;
}

static void
slab_page_remove(struct slab_page **head, struct slab_page *page)
{
    if (page->prev)
        page->prev->next = page->next;
    else
        *head = page->next;
    if (page->next)
        page->next->prev = page->prev;
    page->next = page->prev = 0;
}

static struct slab_page *
slab_create_page(struct slab_cache *cache)
{
    char *mem = (char *)kalloc_order(0);
    if (mem == 0)
        return 0;

    struct slab_page *page = (struct slab_page *)mem;
    page->signature = SLAB_SIGNATURE;
    page->cache = cache;
    page->next = 0;
    page->prev = 0;
    page->total_count = cache->objs_per_page;
    page->free_count = cache->objs_per_page;

    uint64 offset = slab_metadata_size();
    char *object_area = mem + offset;
    page->freelist = object_area;
    char *cursor = object_area;
    for (uint32 i = 0; i < cache->objs_per_page; i++)
    {
        char *next = cursor + cache->obj_size;
        if (i == cache->objs_per_page - 1)
        {
            *((void **)cursor) = 0;
        }
        else
        {
            *((void **)cursor) = next;
        }
        cursor = next;
    }

    return page;
}

static void *
slab_alloc(struct slab_cache *cache)
{
    struct slab_page *page = cache->partial;
    if (page == 0)
    {
        page = slab_create_page(cache);
        if (page == 0)
            return 0;
        slab_page_add(&cache->partial, page);
    }

    void *obj = page->freelist;
    page->freelist = *((void **)obj);
    page->free_count--;
    if (page->free_count == 0)
    {
        slab_page_remove(&cache->partial, page);
        slab_page_add(&cache->full, page);
    }
    return obj;
}

static void
slab_free(struct slab_cache *cache, void *addr)
{
    struct slab_page *page = (struct slab_page *)PGROUNDDOWN((uint64)addr);
    if (page->signature != SLAB_SIGNATURE || page->cache != cache)
        panic("kmfree slab");

    *((void **)addr) = page->freelist;
    page->freelist = addr;
    page->free_count++;

    if (page->free_count == 1)
    {
        // Page was full, move back to partial list.
        slab_page_remove(&cache->full, page);
        slab_page_add(&cache->partial, page);
    }

    if (page->free_count == page->total_count)
    {
        slab_page_remove(&cache->partial, page);
        kfree_order(page, 0);
    }
}

static struct slab_cache *
slab_cache_for_size(uint64 size)
{
    for (int i = 0; i < SLAB_CACHE_COUNT; i++)
    {
        if (size <= slab_caches[i].obj_size)
            return &slab_caches[i];
    }
    return 0;
}

static uint32
slab_objects_per_page(uint32 obj_size)
{
    uint64 usable = PGSIZE - slab_metadata_size();
    uint32 count = usable / obj_size;
    if (count == 0)
        count = 1;
    return count;
}

static void
slab_init(void)
{
    for (int i = 0; i < SLAB_CACHE_COUNT; i++)
    {
        slab_caches[i].obj_size = slab_cache_sizes[i];
        slab_caches[i].objs_per_page = slab_objects_per_page(slab_cache_sizes[i]);
        slab_caches[i].partial = 0;
        slab_caches[i].full = 0;
        slab_caches[i].name = slab_cache_names[i];
        initlock(&slab_caches[i].lock, slab_caches[i].name);
    }
}

static int
order_for_size(uint64 size)
{
    int order = 0;
    uint64 block_size = PGSIZE;
    while (block_size < size && order < buddy.max_order)
    {
        order++;
        block_size <<= 1;
    }
    if (block_size < size)
        return -1;
    return order;
}

static void *
kmalloc_large(uint64 size)
{
    uint64 total = size + sizeof(struct kmalloc_large_header);
    int order = order_for_size(total);
    if (order < 0)
        return 0;
    void *block = kalloc_order(order);
    if (block == 0)
        return 0;

    struct kmalloc_large_header *hdr = (struct kmalloc_large_header *)block;
    hdr->signature = KMALLOC_LARGE_SIGNATURE;
    hdr->order = order;
    void *mem = (char *)block + sizeof(struct kmalloc_large_header);
    uint64 usable = ((uint64)PGSIZE << order) - sizeof(struct kmalloc_large_header);
    memset(mem, 5, usable);
    return mem;
}

void
kinit(void)
{
    initlock(&buddy.lock, "buddy");
    buddy_init_allocator();
    slab_init();
}

void *
kalloc_order(int order)
{
    void *pa;
    acquire(&buddy.lock);
    pa = buddy_alloc_locked(order);
    release(&buddy.lock);

    if (pa)
        memset(pa, 5, ((uint64)PGSIZE) << order);
    return pa;
}

void
kfree_order(void *pa, int order)
{
    if (pa == 0)
        panic("kfree_order");

    memset(pa, 1, ((uint64)PGSIZE) << order);
    acquire(&buddy.lock);
    buddy_free_locked(pa, order);
    release(&buddy.lock);
}

void *
kalloc(void)
{
    return kalloc_order(0);
}

void
kfree(void *pa)
{
    kfree_order(pa, 0);
}

void *
kmalloc(uint64 size)
{
    if (size == 0)
        return 0;

    struct slab_cache *cache = slab_cache_for_size(size);
    if (cache == 0)
        return kmalloc_large(size);

    acquire(&cache->lock);
    void *obj = slab_alloc(cache);
    release(&cache->lock);

    if (obj)
        memset(obj, 5, cache->obj_size);
    return obj;
}

void
kmfree(void *addr)
{
    if (addr == 0)
        return;

    uint64 base = PGROUNDDOWN((uint64)addr);
    struct slab_page *page = (struct slab_page *)base;
    if (page->signature == SLAB_SIGNATURE)
    {
        struct slab_cache *cache = page->cache;
        acquire(&cache->lock);
        slab_free(cache, addr);
        release(&cache->lock);
        return;
    }

    struct kmalloc_large_header *hdr = (struct kmalloc_large_header *)((char *)addr - sizeof(struct kmalloc_large_header));
    if (hdr->signature != KMALLOC_LARGE_SIGNATURE)
        panic("kmfree unknown");
    kfree_order((void *)hdr, hdr->order);
}

static void
buddy_round_trip_test(uint64 initial_free_pages)
{
    void *pages[8];
    for (int i = 0; i < 8; i++)
    {
        pages[i] = kalloc();
        if (pages[i] == 0)
            panic("kalloc_selftest: page alloc failed");
        memset(pages[i], i, PGSIZE);
    }
    for (int i = 0; i < 8; i++)
        kfree(pages[i]);

    void *big = kalloc_order(3);
    if (big == 0)
        panic("kalloc_selftest: order-3 alloc failed");
    memset(big, 0x5a, 8 * PGSIZE);
    kfree_order(big, 3);

    if (buddy_free_pages() != initial_free_pages)
        panic("kalloc_selftest: buddy free pages mismatch");
}

static void
slab_round_trip_test(uint64 initial_free_pages)
{
    enum
    {
        OBJS = 64,
        SMALL_SZ = 48
    };
    void *objs[OBJS];
    for (int i = 0; i < OBJS; i++)
    {
        objs[i] = kmalloc(SMALL_SZ);
        if (objs[i] == 0)
            panic("kalloc_selftest: slab alloc failed");
        memset(objs[i], 0xa0 | i, SMALL_SZ);
    }
    for (int i = 0; i < OBJS; i++)
        kmfree(objs[i]);

    char *large = (char *)kmalloc(PGSIZE * 3);
    if (large == 0)
        panic("kalloc_selftest: large kmalloc failed");
    for (uint64 i = 0; i < PGSIZE * 3; i++)
        large[i] = (char)(i & 0xff);
    for (uint64 i = 0; i < PGSIZE * 3; i++)
    {
        if (large[i] != (char)(i & 0xff))
            panic("kalloc_selftest: large buffer corrupted");
    }
    kmfree(large);

    if (buddy_free_pages() != initial_free_pages)
        panic("kalloc_selftest: slab test leaked pages");
}

static void
kalloc_selftest_internal(void)
{
    uint64 baseline = buddy_free_pages();
    buddy_round_trip_test(baseline);
    slab_round_trip_test(baseline);
}

void
kalloc_selftest(void)
{
    log_info("kalloc: running allocator self-test...\n");
    kalloc_selftest_internal();
    log_info("kalloc: self-test completed.\n");
}
