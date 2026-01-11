#include "types.h"
#include "spinlock.h"
// #include "sleeplock.h"
#include "riscv.h"
#include "proc/proc.h"

#define RHR 0                 // receive holding register (for input bytes)
#define THR 0  
#define LSR 5
#define TX_IDLE 0x20
#define NULL (void*)0

struct buf;
struct context;
struct file;
struct inode;
struct vfs_driver;
struct pipe;
struct proc;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;

void            uart_putc(uint8 c);
void            uart_puts(char *s);
void            uartinit(void);
void            uartputc_sync(uint8 c);
void            uart_write(const char *s, int n);

// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// 设置异常向量表
void trapinithart(void);

// proc.c
int cpuid();
struct cpu* mycpu(void);

// spinlock.c
void            acquire(struct spinlock*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            push_off(void);
void            pop_off(void);

// printf.c
void            printf(char*, ...);
void            panic(char*) __attribute__((noreturn));
void            printfinit(void);
void            printf_color(char *color, char *fmt, ...);
void            log_info(char *fmt, ...);
void            log_warn(char *fmt, ...);
void            log_error(char *fmt, ...);
void            log_debug(char *fmt, ...);
void            print_ruos(void);

// console.c
void consputc(int c);
void consoleinit(void);
// console input interrupt handler (from uart)
void consoleintr(int c);
// int consoleread(int user_dst, uint64 dst, int n);
// int consolewrite(int user_src, uint64 src, int n);
int consolewrite(struct file *f, int user_src, uint64 src, int n);
int consoleread(struct file *f, int user_dst, uint64 dst, int n);

// sleep/wakeup primitives used across the kernel
void sleep(void*, struct spinlock*);
void wakeup(void*);

// uart interrupt handler (called from trap.c when UART IRQ arrives)
void uartintr(void);

// vm.c
void            kvminit(void);
void            kvminithart(void);
void            kvmmap(pagetable_t, uint64, uint64, uint64, int);
uint64          kvmpa(uint64);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     uvmcreate(void);
uint64          uvmfirst(pagetable_t, uchar *, uint);
uint64          uvmalloc(pagetable_t, uint64, uint64, int);
uint64          uvmdealloc(pagetable_t, uint64, uint64);
int             uvmcopy(pagetable_t, pagetable_t, uint64);
void            uvmfree(pagetable_t, uint64);
void            uvmunmap(pagetable_t, uint64, uint64, int);
void            uvmclear(pagetable_t, uint64);
pte_t *         walk(pagetable_t, uint64, int);
uint64          walkaddr(pagetable_t, uint64);
int             copyout(pagetable_t, uint64, char *, uint64);
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);

// string.c
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
void*           memcpy(void *dst, const void *src, uint n);
char*           strchr(const char*, char c);
char*           strrchr(const char*, char c);
int             strncasecmp(const char *s1, const char *s2, uint n);
int             snprintf ( char * str, uint size, const char * format, ... );
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);

// kalloc.c
void*           kalloc(void);
void            kfree(void *);
void*           kalloc_order(int);
void            kfree_order(void *, int);
void*           kmalloc(uint64);
void            kmfree(void *);
void            kalloc_selftest(void);
void            kinit(void);

// proc.c
int             cpuid(void);
void            exit(int);
int             fork(void);
int             clone_fork(uint64 stack);  // fork with custom stack support
int             growproc(int);
void            proc_mapstacks(pagetable_t);
pagetable_t     proc_pagetable(struct proc *);
void            proc_freepagetable(pagetable_t, uint64);
void            freeproc(struct proc *);  // free process resources
int             kill(int);
int             killed(struct proc*);
void            setkilled(struct proc*);
struct cpu*     mycpu(void);
struct cpu*     getmycpu(void);
struct proc*    myproc();
void            procinit(void);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             wait(uint64);
void            wakeup(void*);
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);
void            procdump(void);

// proc.c - global process table
extern struct proc proc[];  // array of NPROC processes
extern struct spinlock wait_lock;  // lock for wait/wakeup
void            scheduler(void);
void            sched(void);
void            reparent(struct proc *);
void            yield(void);
int             getpid(void);


// swtch.S
void            swtch(struct context*, struct context*);

// trap.c
void            usertrapret(void);
extern struct spinlock tickslock;
extern uint ticks;

// syscall.c
int             argint(int, int*);
int             argstr(int, char*, int);
int             argaddr(int, uint64 *);
int             fetchstr(uint64, char*, int);
int             fetchaddr(uint64, uint64*);
void            syscall_handler(void);

// bio.c
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);
void            bpin(struct buf*);
void            bunpin(struct buf*);

// sleeplock.c
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);
int             holdingsleep(struct sleeplock*);
void            initsleeplock(struct sleeplock*, char*);

// pipe.c
int             pipealloc(struct file**, struct file**);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, uint64, int);
int             pipewrite(struct pipe*, uint64, int);

// fs.c
void            fsinit(int);
struct inode*   idup(struct inode*);
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
struct inode*   namei(char*);
struct inode*   nameiat(struct inode*, char*);
struct inode*   nameiparent(char*, char*);
struct inode*   create(char*, short, int, int);
struct inode*   createat(struct inode*, char*, short, int, int);
int             readi(struct inode*, int, uint64, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, int, uint64, uint, uint);
int             getdents64(struct inode*, uint*, uint64, uint64);
void            vfs_mount_root(const struct vfs_driver *);
const struct vfs_driver *vfs_current_driver(void);
int             vfs_unlink_path(char *path, int want_dir);

struct inode*   iget_pub(uint dev, uint inum);

// virtio_disk.c
void            virtio_disk_init(int);
void            virtio_disk_rw(int, struct buf *, int);
void            virtio_disk_intr(int);

// log.c
void            initlog(int, struct superblock*);
void            log_write(struct buf*);
void            begin_op(int);
void            end_op(int);
void            crash_op(int,int);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);
void            fileinit(void);
int             fileread(struct file*, uint64, int n);
int             filestat(struct file*, uint64 addr);
int             filewrite(struct file*, uint64, int n);

//thinfat32.c
int tf_attach_image(const char *path);
int tf_detach_image(void);
int read_sector(uint8_t *data, uint32_t sector);
int write_sector(uint8_t *data, uint32_t blocknum);
int tf_fetch(uint32_t sector);
int tf_store(void);
int tf_init(void);

// exec.c
int exec(char *path, char **argv);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
