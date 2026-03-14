#include "user.h"
#include "memlayout.h"
#include "net_test.h"
#include "../src/fcntl.h"
#define TEST_SYSCALLS
#include "../src/syscall/syscall.h"

void test_(char *name);
void test_busybox_musl();
void test_basic();
// static void test_cow(void);

#define TEST_UDP_HOST_ECHO 0
#define TEST_UDP_BRIDGE_HOST_ECHO 0
#define TEST_TCP_LOOPBACK 0
#define TEST_TCP_HOST_ECHO 0

void test_ltp(char *name);

int main()
{
    if (open("console", O_RDWR) < 0)
    {
        mknod("console", 1, 1);
        open("console", O_RDWR);
    }
    dup(0); // stdout
    dup(0); // stderr

    // 确保 /tmp 存在（LTP 框架需要）
    mkdir("/tmp");
    chdir("/musl");

    printf("=== LTP Test Start ===\n");

    test_ltp("getpid01");
    test_ltp("getpid02");
    test_ltp("getppid01");
    test_ltp("getppid02");
    test_ltp("fork01");
    test_ltp("fork02");
    test_ltp("fork03");
    test_ltp("fork04");
    test_ltp("wait01");
    test_ltp("wait02");
    test_ltp("wait401");
    test_ltp("wait402");
    test_ltp("waitpid01");
    test_ltp("waitpid02");
    test_ltp("waitpid03");
    test_ltp("exit01");
    test_ltp("exit02");
    test_ltp("execve01");
    test_ltp("execve02");
    test_ltp("execve03");
    test_ltp("execve05");
    test_ltp("clone01");
    test_ltp("clone02");
    test_ltp("clone03");
    test_ltp("brk01");
    test_ltp("brk02");
    test_ltp("mmap01");
    test_ltp("mmap02");
    test_ltp("mmap03");
    test_ltp("mmap04");
    test_ltp("mmap05");
    test_ltp("munmap01");
    test_ltp("munmap02");
    test_ltp("open01");
    test_ltp("open02");
    test_ltp("open03");
    test_ltp("openat01");
    test_ltp("openat02");
    test_ltp("openat03");
    test_ltp("close01");
    test_ltp("close02");
    test_ltp("read01");
    test_ltp("read02");
    test_ltp("read03");
    test_ltp("read04");
    test_ltp("write01");
    test_ltp("write02");
    test_ltp("write03");
    test_ltp("write05");
    test_ltp("dup01");
    test_ltp("dup02");
    test_ltp("dup201");
    test_ltp("dup202");
    test_ltp("dup203");
    test_ltp("pipe01");
    test_ltp("pipe02");
    test_ltp("mkdir01");
    test_ltp("mkdir02");
    test_ltp("mkdir03");
    test_ltp("chdir01");
    test_ltp("chdir02");
    test_ltp("chdir03");
    test_ltp("getcwd01");
    test_ltp("getcwd02");
    test_ltp("getcwd03");
    test_ltp("fstat01");
    test_ltp("fstat02");
    test_ltp("stat01");
    test_ltp("stat02");
    test_ltp("unlink01");
    test_ltp("unlink02");
    test_ltp("uname01");
    test_ltp("uname02");
    test_ltp("gettimeofday01");
    test_ltp("gettimeofday02");
    test_ltp("times01");
    test_ltp("times03");
    test_ltp("getdents01");
    test_ltp("getdents02");
    test_ltp("fchdir01");
    test_ltp("fchdir02");
    test_ltp("kill01");
    test_ltp("kill02");
    test_ltp("pipe2_01");
    test_ltp("pipe2_02");
    test_ltp("lseek01");
    test_ltp("lseek02");
    test_ltp("nanosleep01");
    test_ltp("nanosleep02");
    test_ltp("clock_gettime01");
    test_ltp("clock_nanosleep01");
    test_ltp("sched_yield01");

    printf("=== LTP Test End ===\n");
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
        exec(name, argv);
        printf("Exec failed!\n");
        syscall(SYS_exit, -1);
    }
    else
    {
        int status;
        wait(&status);
    }
}

void test_ltp(char *name)
{
    char path[256];
    // 拼路径: /musl/ltp/testcases/bin/<name>
    char *prefix = "/musl/ltp/testcases/bin/";
    char *p = path;
    while (*prefix) *p++ = *prefix++;
    char *n = name;
    while (*n) *p++ = *n++;
    *p = 0;

    printf("[LTP] RUN  %s\n", name);

    int pid = fork();
    if (pid == 0)
    {
        char *argv[] = {name, 0};
        char *envp[] = {"PATH=/musl:/bin:/usr/bin", "TMPDIR=/tmp", "HOME=/tmp", "LTPROOT=/musl/ltp", 0};
        execve(path, argv, envp);
        printf("[LTP] EXEC_FAIL %s\n", name);
        syscall(SYS_exit, -1);
    }
    else if (pid > 0)
    {
        int status;
        wait(&status);
        if (status == 0)
            printf("[LTP] PASS %s\n", name);
        else
            printf("[LTP] FAIL %s (status=0x%x)\n", name, status);
    }
    else
    {
        printf("[LTP] FORK_FAIL %s\n", name);
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

// static void
// test_cow(void)
// {
//     printf("=== Testing COW ===\n");
//     char *p = (char *)(syscall(SYS_xv6_sbrk, 4096));
//     if (p == (char *)-1) {
//         printf("sbrk failed\n");
//         return;
//     }

//     p[0] = 'P';
//     p[1] = '0';

//     int pid = fork();
//     if (pid < 0) {
//         printf("fork failed\n");
//         return;
//     }

//     if (pid == 0) {
//         p[0] = 'C';
//         p[1] = '1';
//         printf("child sees %c%c\n", p[0], p[1]);
//         syscall(SYS_exit, 0);
//     }

//     int status;
//     wait(&status);
//     printf("parent sees %c%c\n", p[0], p[1]);
// }

