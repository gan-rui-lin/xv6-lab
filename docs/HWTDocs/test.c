/*
 * 测试 waitid 系统调用
 * 编译选项：-D TEST_CASE=1-5 选择测试点
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

#define die(fmt_, ...) \
    do { fprintf(stderr, "%s:%d: " fmt_ "\n", __FILE__, __LINE__, ##__VA_ARGS__); exit(EXIT_FAILURE); } while(0)

#define TEST_COMPARE(a_, b_) \
    do { \
        long a__ = (long)(a_); \
        long b__ = (long)(b_); \
        if (a__ != b__) { \
            die("compare failed: %ld != %ld", a__, b__); \
        } \
    } while(0)

#define TEST_VERIFY_EXPR(a_) \
    do { \
        if (!(a_)) { \
            die("verify failed: %s", #a_); \
        } \
    } while(0)

// waitid 包装函数
static int xwaitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
    int ret = waitid(idtype, id, infop, options);
    if (ret < 0) {
        return -errno;
    }
    return ret;
}

// 测试点1：基本功能测试 - 等待子进程退出
static void test_basic_wait(void) {
    printf("Test 1: Basic wait for child process\n");
    
    pid_t child = fork();
    if (child == 0) {
        // 子进程：立即退出
        exit(123);
    }
    
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    
    int ret = xwaitid(P_PID, child, &info, WEXITED);
    TEST_COMPARE(ret, 0);
    
    TEST_COMPARE(info.si_pid, child);
    TEST_COMPARE(info.si_code, CLD_EXITED);
    TEST_COMPARE(info.si_status, 123);
    
    printf("waitid 1 test passed!\n");
}

// 测试点2：WNOHANG非阻塞等待
static void test_nonblocking_wait(void) {
    printf("Test 2: Non-blocking wait with WNOHANG\n");
    
    pid_t child = fork();
    if (child == 0) {
        // 子进程：运行1秒后退出
        sleep(1);
        exit(77);
    }
    
    siginfo_t info;
    
    // 立即检查，应该还没有退出
    memset(&info, 0, sizeof(info));
    int ret = xwaitid(P_PID, child, &info, WEXITED | WNOHANG);
    TEST_COMPARE(ret, 0);
    TEST_COMPARE(info.si_pid, 0); // 没有状态变化
    
    // 等待子进程退出
    sleep(2);
    
    // 再次检查，应该能获取到状态
    memset(&info, 0, sizeof(info));
    ret = xwaitid(P_PID, child, &info, WEXITED | WNOHANG);
    TEST_COMPARE(ret, 0);
    TEST_COMPARE(info.si_pid, child);
    TEST_COMPARE(info.si_status, 77);
    
    printf("waitid 2 test passed!\n");
}

// 测试点3：等待被信号杀死的进程
static void test_wait_killed(void) {
    printf("Test 3: Wait for killed process\n");
    
    pid_t child = fork();
    if (child == 0) {
        // 子进程：无限循环等待被杀死
        while(1) {
            sleep(1);
        }
    }
    
    // 给子进程一点时间启动
    sleep(1);
    
    // 杀死子进程
    kill(child, SIGKILL);
    
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    
    int ret = xwaitid(P_PID, child, &info, WEXITED);
    TEST_COMPARE(ret, 0);
    
    TEST_COMPARE(info.si_pid, child);
    TEST_COMPARE(info.si_code, CLD_KILLED);
    TEST_COMPARE(info.si_status, SIGKILL);
    
    printf("waitid 3 test passed!\n");
}

// 测试点4：等待进程组
static void test_wait_process_group(void) {
    printf("Test 4: Wait for process group\n");
    
    // 创建进程组
    pid_t pgid = getpid();
    setpgid(0, 0);
    
    // 创建几个子进程
    pid_t children[3];
    for (int i = 0; i < 3; i++) {
        pid_t child = fork();
        if (child == 0) {
            // 子进程加入进程组
            setpgid(0, pgid);
            sleep(i + 1); // 不同时间退出
            exit(100 + i);
        }
        children[i] = child;
    }
    
    // 等待进程组中的所有进程
    int collected = 0;
    siginfo_t info;
    
    while (collected < 3) {
        memset(&info, 0, sizeof(info));
        int ret = waitid(P_PGID, pgid, &info, WEXITED | WNOHANG);
        
        if (ret == 0 && info.si_pid != 0) {
            collected++;
            TEST_VERIFY_EXPR(info.si_code == CLD_EXITED);
            printf("  Collected child %d with status %d\n", info.si_pid, info.si_status);
        }
        
        if (collected < 3) {
            sleep(1);
        }
    }
    
    printf("waitid 4 test passed!\n");
}

int main(void) {
    printf("Starting waitid system call tests\n");
    
#if TEST_CASE == 1
    test_basic_wait();
#elif TEST_CASE == 2
    test_nonblocking_wait();
#elif TEST_CASE == 3
    test_wait_killed();
#elif TEST_CASE == 4
    test_wait_process_group();
#else
    // 默认运行所有测试
    test_basic_wait();
    test_nonblocking_wait();
    test_wait_killed();
    test_wait_process_group();
    printf("\nAll waitid tests passed!\n");
#endif
    
    return 0;
}