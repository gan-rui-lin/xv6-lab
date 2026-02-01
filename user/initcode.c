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

// 运行评测目录中的测试文件
void run_onsite_tests()
{
    printf("\n");
    printf("==========================================\n");
    printf("   Running Onsite Tests\n");
    printf("==========================================\n\n");

    // 尝试不同的测试目录路径
    const char *test_dirs[] = {
        "/sdcard-2025-onsite",
        "/mnt/sdcard-2025-onsite",
        "/",  // 根目录
        NULL
    };

    const char *working_dir = NULL;
    for (int i = 0; test_dirs[i] != NULL; i++) {
        if (chdir(test_dirs[i]) == 0) {
            printf("Found tests directory: %s\n\n", test_dirs[i]);
            working_dir = test_dirs[i];
            break;
        }
    }

    if (working_dir == NULL) {
        printf("ERROR: Cannot find test directory\n");
        return;
    }

    // 定义所有测试文件（使用相对路径）
    const char *test_files[] = {
        "cr-1", "cr-2", "cr-3", "cr-4", "cr-5",
        "ef2-1", "ef2-2", "ef2-3", "ef2-4", "ef2-5",
        "wi-1", "wi-2", "wi-3", "wi-4",
        NULL  // 结束标记
    };

    int total = 0;
    int passed = 0;
    int failed = 0;

    // 依次运行每个测试
    for (int i = 0; test_files[i] != NULL; i++)
    {
        total++;
        printf("\n[%d] Running test: %s\n", i + 1, test_files[i]);
        printf("------------------------------------------\n");

        int pid = fork();
        if (pid < 0)
        {
            printf("ERROR: Fork failed for %s\n", test_files[i]);
            failed++;
            continue;
        }

        if (pid == 0)
        {
            // 子进程：执行测试
            // 构造完整路径
            char fullpath[128];
            fullpath[0] = '.';
            fullpath[1] = '/';
            int j;
            for (j = 0; test_files[i][j]; j++) {
                fullpath[j+2] = test_files[i][j];
            }
            fullpath[j+2] = 0;

            char *argv[] = {(char*)test_files[i], NULL};
            execve(fullpath, argv, NULL);

            printf("ERROR: Failed to exec %s (tried %s)\n", test_files[i], fullpath);
            syscall(SYS_exit, -1);
        }
        else
        {
            // 父进程：等待并统计结果
            int status;
            wait(&status);

            if (status == 0)
            {
                printf("PASSED: %s\n", test_files[i]);
                passed++;
            }
            else
            {
                printf("FAILED: %s (status=%d)\n", test_files[i], status);
                failed++;
            }
        }

        printf("------------------------------------------\n");
    }

    // 打印测试总结
    printf("\n");
    printf("==========================================\n");
    printf("   Test Summary\n");
    printf("==========================================\n");
    printf("Total:  %d tests\n", total);
    printf("Passed: %d tests\n", passed);
    printf("Failed: %d tests\n", failed);
    printf("==========================================\n\n");
}

// 列出目录内容的辅助函数
void list_directory(const char *path)
{
    printf("Listing directory: %s\n", path);
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        printf("  ERROR: Cannot open directory\n");
        return;
    }

    char buf[512];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        // 简单打印前100字节
        for (int i = 0; i < n && i < 100; i++) {
            if (buf[i] >= 32 && buf[i] < 127) {
                printf("%c", buf[i]);
            } else {
                printf(".");
            }
        }
        printf("\n");
        break;  // 只读第一块
    }
    close(fd);
}

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

    // Ensure /tmp exists for close_range tests that create temp files.
    // mkdir("/tmp");

    // test_("cr-1");
    // test_("cr-2");
    // test_("cr-3");
    // test_("cr-4");
    // test_("cr-5");

    test_("ef2-1");
    test_("ef2-2");
    test_("ef2-3");
    test_("ef2-4");
    test_("ef2-5");

    // test_("wi-1");
    // test_("wi-2");
    // test_("wi-3");
    // test_("wi-4");

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

