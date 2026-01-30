#include "user.h"
#include "memlayout.h"
#include "net_test.h"
#include "../src/fcntl.h"
#define TEST_SYSCALLS
#include "../src/syscall/syscall.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

// 测试函数声明
void test_(char *name);
void test_basic();
void test_busybox();
void test_lua();
void test_libc();
void test_dynamic();
void test_net();
void test_ltp();
void test_cow();
void test_all_tests();
void run_testcode(const char *script_name);
// static void test_cow(void);

// 网络测试开关
#define TEST_UDP_LOOPBACK 0
#define TEST_TCP_HOST_ECHO 1

// 测试组开关 - 根据需要启用/禁用
#define ENABLE_TEST_FAT32_BASIC 0
#define ENABLE_TEST_EXT4_BASIC 0
#define ENABLE_TEST_BUSYBOX 0
#define ENABLE_TEST_LIBC 0
#define ENABLE_TEST_DYNAMIC 0
#define ENABLE_TEST_SIMPLE_NET 1
#define ENABLE_TEST_COW 0

#define TEST_LUA 0
#define TEST_ALL_TESTS 0

// 测试流程：
// 先测 23 年初赛镜像，测试 FAT32 文件系统下系统调用(支持系统调用)  ENABLE_TEST_FAT32_BASIC
// 再测 25 年初赛镜像，测试 EXT4  文件系统下 busybox basic_testcode.sh(支持系统调用) ENABLE_TEST_EXT4_BASIC
// 再测 25 年初赛镜像，测试 EXT4  文件系统下 busybox busybox_testcode.sh(基本支持 BusyBox 语义，正确返回 errno) ENABLE_TEST_BUSYBOX
// 再测 25 年初赛镜像，测试 EXT4  文件系统下 libctest_testcode.sh(基本支持 glibc 语义) ENABLE_TEST_LIBC
// 再测 25 年初赛镜像，测试 EXT4  文件系统下 run-dynamic.sh (动态链接测试，基本支持动态链接库加载和基本功能) ENABLE_TEST_DYNAMIC
// 分别验证 TCP/UDP 基本网络功能(时间原因，只测试 UDP 回环和 TCP 主机 echo 功能) ENABLE_TEST_SIMPLE_NET
//         UDP 回环                      `-device virtio-net-device,netdev=net,bus=virtio-mmio-bus.1 -netdev user,id=net`
//         TCP 主机 echo 工作在 tap 模式   `--netmode bridge --bridge br0 --tap-ifname tap0`
// 最后验证 Cow + 共享零页功能 通过写入 sbrk 分配的内存页来触发 COW ENABLE_TEST_COW

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

    // 测试写入共享零页和 COW 功能
#if ENABLE_TEST_COW
    test_cow();
#endif

    // 网络测试（如果启用）

#if ENABLE_TEST_SIMPLE_NET
#if TEST_UDP_LOOPBACK
    printf("Running UDP loopback test...\n");
    udp_loopback_test();
    shutdown();
#endif
#if TEST_TCP_HOST_ECHO
    printf("Running TCP host echo test...\n");
    tcp_host_echo_test();
    shutdown();
#endif
#endif
    // 运行测试套件
#if TEST_ALL_TESTS
    test_all_tests(); // 运行所有测试
#else
    // 按需运行单个测试
#if ENABLE_TEST_EXT4_BASIC
    test_basic();
    shutdown();
#endif

#if ENABLE_TEST_BUSYBOX
    test_busybox();
    shutdown();
#endif

#if TEST_LUA
    test_lua();
    shutdown();
#endif

#if ENABLE_TEST_LIBC
    test_libc();
    shutdown();
#endif
#endif

#if ENABLE_TEST_DYNAMIC
    test_dynamic();
    shutdown();
#endif

#if ENABLE_TEST_FAT32_BASIC
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
#endif

#if ENABLE_TEST_LTP
    test_ltp();
    shutdown();
#endif


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
        char *argv[] = {"sh", (char *)script_name, 0};
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

void test_net()
{
    run_testcode("/musl/netperf_testcode.sh");
}

void test_ltp()
{
    run_testcode("/musl/ltp_testcode.sh");
}

void test_dynamic()
{
    chdir("/musl/");

    int pid = fork();
    if (pid == 0)
    {
        // 使用动态链接器运行 musl 测试（先尝试 /musl 目录结构）
        char *argv1[] = {"sh", "/musl/run-dynamic.sh", 0};
        char *envp1[] = {"PATH=/musl:/bin", 0}; // ?应该没用
        execve("/musl/busybox", argv1, envp1);
        // 兼容镜像把文件放在根目录的情况
        char *argv2[] = {"sh", "/run-dynamic.sh", 0};
        char *envp2[] = {"PATH=/:/bin:/musl", 0}; // ?应该没用
        execve("/busybox", argv2, envp2);
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

// 运行所有测试组
void test_all_tests()
{
    printf("\n");
    printf("==========================================\n");
    printf("   Running ALL Test Suites\n");
    printf("==========================================\n\n");

    // 定义所有测试脚本
    const char *test_scripts[] = {
        "/musl/basic_testcode.sh",      // 1. 基础系统调用
        "/musl/busybox_testcode.sh",    // 2. BusyBox 工具
        "/musl/lua_testcode.sh",        // 3. Lua 解释器
        "/musl/libctest_testcode.sh",   // 4. libc 功能测试
        "/musl/iozone_testcode.sh",     // 5. IO 性能测试
        "/musl/unixbench_testcode.sh",  // 6. Unix 基准测试
        "/musl/iperf_testcode.sh",      // 7. 网络吞吐量测试
        "/musl/libcbench_testcode.sh",  // 8. libc 性能测试
        "/musl/lmbench_testcode.sh",    // 9. 延迟基准测试
        "/musl/netperf_testcode.sh",    // 10. 网络性能测试
        "/musl/cyclictest_testcode.sh", // 11. 实时性测试
        "/musl/ltp_testcode.sh",        // 12. Linux 测试项目
        NULL                            // 结束标记
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
            char *argv[] = {"sh", (char *)test_scripts[i], 0};
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

void
test_cow(void)
{
    printf("=== Testing COW ===\n");
    char *p = (char *)(syscall(SYS_xv6_sbrk, 4096));
    if (p == (char *)-1)
    {
        printf("sbrk failed\n");
        return;
    }

    p[0] = 'P';
    p[1] = '0';

    int pid = fork();
    if (pid < 0)
    {
        printf("fork failed\n");
        return;
    }

    if (pid == 0)
    {
        p[0] = 'C';
        p[1] = '1';
        printf("child sees %c%c\n", p[0], p[1]);
        syscall(SYS_exit, 0);
    }

    int status;
    wait(&status);
    printf("parent sees %c%c\n", p[0], p[1]);
}
