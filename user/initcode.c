#include "user.h"
#include "memlayout.h"
#include "net_test.h"
#include "../src/fcntl.h"
#define TEST_SYSCALLS
#include "../src/syscall/syscall.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

void test_(char *name);
void test_busybox();
void test_lua();
void test_libc();
void test_basic();
void test_all_tests();

int main()
{
    // 初始化标准输入输出
    if (open("console", O_RDWR) < 0)
    {
        mknod("console", 1, 1);
        open("console", O_RDWR);
    }
    dup(0); // stdout
    dup(0); // stderr

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

// 通用的测试脚本运行函数
void run_testcode(const char *script_name)
{
    printf("=== Running %s ===\n", script_name);

    int pid = fork();
    if (pid < 0)
    {
        printf("Fork failed!\n");
        return;
    }

    if (pid == 0)
    {
        // 子进程：切换到 musl 目录并执行测试脚本
        chdir("/musl/");
        char *argv[] = {"sh", (char*)script_name, 0};
        char *envp[] = {"PATH=/bin:/musl:/usr/bin", 0};
        execve("/musl/busybox", argv, envp);

        printf("Exec %s failed!\n", script_name);
        syscall(SYS_exit, -1);
    }
    else
    {
        // 父进程：等待子进程完成
        int status;
        wait(&status);
        printf("=== %s completed (status=0x%x) ===\n\n", script_name, status);
    }
}

// BusyBox 测试
void test_busybox()
{
    run_testcode("/musl/busybox_testcode.sh");
}

// Lua 测试
void test_lua()
{
    run_testcode("/musl/lua_testcode.sh");
}

// libc-test 测试
void test_libc()
{
    run_testcode("/musl/libctest_testcode.sh");
}

void test_basic()
{
    run_testcode("/musl/basic_testcode.sh");
}

// 运行所有测试组
void test_all_tests()
{
    printf("\n");
    printf("==========================================\n");
    printf("   Running ALL Test Suites\n");
    printf("==========================================\n\n");

    // 定义所有测试脚本
    const char *test_scripts[] = {
        "/musl/basic_testcode.sh",          // 1. 基础系统调用
        "/musl/busybox_testcode.sh",        // 2. BusyBox 工具
        "/musl/lua_testcode.sh",            // 3. Lua 解释器
        "/musl/libctest_testcode.sh",       // 4. libc 功能测试
        "/musl/iozone_testcode.sh",         // 5. IO 性能测试
        "/musl/unixbench_testcode.sh",      // 6. Unix 基准测试
        "/musl/iperf_testcode.sh",          // 7. 网络吞吐量测试
        "/musl/libcbench_testcode.sh",      // 8. libc 性能测试
        "/musl/lmbench_testcode.sh",        // 9. 延迟基准测试
        "/musl/netperf_testcode.sh",        // 10. 网络性能测试
        "/musl/cyclictest_testcode.sh",     // 11. 实时性测试
        "/musl/ltp_testcode.sh",            // 12. Linux 测试项目
        NULL  // 结束标记
    };

    int total = 0;
    int passed = 0;
    int failed = 0;

    // 依次运行每个测试
    for (int i = 0; test_scripts[i] != NULL; i++)
    {
        total++;
        printf("\n[%d/%d] Running test: %s\n", i + 1, 12, test_scripts[i]);
        printf("------------------------------------------\n");

        int pid = fork();
        if (pid < 0)
        {
            printf("ERROR: Fork failed for %s\n", test_scripts[i]);
            failed++;
            continue;
        }

        if (pid == 0)
        {
            // 子进程：执行测试
            chdir("/musl/");
            char *argv[] = {"sh", (char*)test_scripts[i], 0};
            char *envp[] = {"PATH=/bin:/musl:/usr/bin", 0};
            execve("/musl/busybox", argv, envp);

            printf("ERROR: Failed to exec %s\n", test_scripts[i]);
            syscall(SYS_exit, -1);
        }
        else
        {
            // 父进程：等待并统计结果
            int status;
            wait(&status);

            if (status == 0)
            {
                printf("✓ Test PASSED: %s\n", test_scripts[i]);
                passed++;
            }
            else
            {
                printf("✗ Test FAILED: %s (status=0x%x)\n", test_scripts[i], status);
                failed++;
            }
        }

        printf("------------------------------------------\n");
    }

    // 打印测试总结
    printf("\n");
    printf("==========================================\n");
    printf("   Test Suite Summary\n");
    printf("==========================================\n");
    printf("Total:  %d tests\n", total);
    printf("Passed: %d tests\n", passed);
    printf("Failed: %d tests\n", failed);
    printf("==========================================\n\n");
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

