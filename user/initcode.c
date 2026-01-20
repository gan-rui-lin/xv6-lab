#include "user.h"
#include "memlayout.h"
#include "../src/fcntl.h"
#define TEST_SYSCALLS
#include "../src/syscall/syscall.h"

void test_(char *name);
void test_busybox_musl();
void test_basic();
static void test_cow(void);

int main()
{
    if (open("console", O_RDWR) < 0)
    {
        mknod("console", 1, 1);
        open("console", O_RDWR);
    }
    dup(0); // stdout
    dup(0); // stderr

    // Provide /bin/sh for script fallback.
    // mkdir("/bin");
    // printf("r1 = %d\n", r1);
    // syscall(SYS_symlink, "/musl/busybox", "/bin/sh");
    // printf("r2 = %d\n", r2)

    // test_cow();
    // test_basic();
    // test_busybox_musl();
    // printf("Hello, xv6 world!\n");

    test_("getppid");

    test_("chdir");
    test_("times");
    test_("sleep");
    test_("fork");
    test_("gettimeofday");

    test_("open");
    test_("read");
    test_("brk");

    test_("getcwd");

    test_("openat");
    test_("getpid");
    test_("exit");
    test_("wait");
    test_("execve");
    test_("clone");
    test_("yield");
    test_("waitpid");

    test_("getcwd");
    test_("dup");
    test_("close");
    test_("mkdir_");

    test_("getdents");
    test_("pipe");
    test_("fstat");
    test_("write");
    test_("uname");
    test_("mmap");
    test_("munmap");

    test_("unlink");
    test_("fstat");
    test_("dup2");

    // test_busybox();
    shutdown();
    return 0;
}


void test_(char *name)
{

    char *argv[] = {name, 0};

    int pid = fork();
    if (pid < 0)
    {
        printf("Fork failed!\n");
        return;
    }
    else if (pid == 0)
    {
        // Child process
        // exec("wait", argv);
        // exec("fork", argv);
        exec(name, argv);
        // exec("waitpid", argv);
        printf("Exec failed!\n");
        syscall(SYS_exit, -1);
    }
    else
    {
        // Parent process
        int status;
        wait(&status);
    }
}

void test_busybox_musl()
{
    printf("=== Testing busybox ===\n");
    chdir("/musl/");

    int pid = fork();
    if (pid == 0)
    {
        // 使用动态链接器运行 musl 测试（先尝试 /musl 目录结构）
        // char *argv1[] = {"sh", "/musl/run-dynamic.sh", 0};
        char *argv1[] = {"sh", "/musl/ltp_testcode.sh", 0};
        char *envp1[] = {"PATH=/musl:/bin:/usr/bin", 0};
        execve("/musl/busybox", argv1, envp1);
        printf("Exec busybox failed!\n");
        syscall(SYS_exit, -1);
    }
    else if (pid > 0)
    {
        int status;
        wait(&status);
        printf("busybox child exited, status=0x%x\n", status);
    }
    else
    {
        printf("Fork failed!\n");
    }
}

void test_basic()
{
    printf("=== Testing basic syscalls ===\n");
    chdir("/musl/");

    int pid = fork();
    if (pid == 0)
    {
        char *argv[] = {"sh", "/musl/basic_testcode.sh", 0};
        char *envp[] = {"PATH=/bin:/musl:/usr/bin", 0};
        execve("/musl/busybox", argv, envp);
        printf("Exec busybox failed!\n");
        syscall(SYS_exit, -1);
    }
    else if (pid > 0)
    {
        int status;
        wait(&status);
    }
    else
    {
        printf("Fork failed!\n");
    }
}

static void
test_cow(void)
{
    printf("=== Testing COW ===\n");
    char *p = (char *)(syscall(SYS_xv6_sbrk, 4096));
    if (p == (char *)-1) {
        printf("sbrk failed\n");
        return;
    }

    p[0] = 'P';
    p[1] = '0';

    int pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        return;
    }

    if (pid == 0) {
        p[0] = 'C';
        p[1] = '1';
        printf("child sees %c%c\n", p[0], p[1]);
        syscall(SYS_exit, 0);
    }

    int status;
    wait(&status);
    printf("parent sees %c%c\n", p[0], p[1]);
}
