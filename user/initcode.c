#include "user.h"
#include "memlayout.h"
#include "../src/fcntl.h"

void test_fork();

void test_sbrk();

void test_uptime();

void test_gettimeofday();

void test_read();

int main()
{
    if (open("console", O_RDWR) < 0)
    {
        mknod("console", 1, 1);
        open("console", O_RDWR);
    }
    dup(0); // stdout
    dup(0); // stderr

    test_sbrk();

    test_fork();

    test_uptime();

    test_gettimeofday();

    test_read();

    shutdown();
    return 0;
}

void test_fork()
{

    printfYellow("===========  Test Fork ===========\n");
    int pid = fork();
    int status;
    if (pid < 0)
    {
        printf("Fork failed!\n");
    }
    else if (pid == 0)
    {
        // Child process
        printf("Hello from child process! PID: %d\n", getpid());
        exit(0);
    }
    else
    {
        // Parent process
        wait(&status);
        printf("Hello from parent process! PID: %d, Child PID: %d\n", getpid(), pid);
        printf("Child exited with status: %d\n", status);
    }
}

void test_sbrk()
{

    printfYellow("===========  Test sbrk ===========\n");

    long long addr = sbrk(0);

    printf("Current break address: %p\n", addr);

    sbrk(addr + 4096); // 增加4KB

    // 测试是否成功增加
    // 写入 'test sbrk'

    char *test_str = "test sbrk";
    int n = 9;
    for (int i = 0; i < n; i++)
    {
        *((char *)addr + i) = test_str[i];
    }
    *((char *)addr + n) = '\0';
    printf("%s\n", (char *)addr);
}

void test_uptime()
{
    printfYellow("===========  Test Uptime ===========\n");

    uint64 start = uptime();
    printf("System has been up for %d ticks\n", start);

    // 睡眠 1 秒（10个滴答）
    sleep(10);

    uint64 end = uptime();
    printf("After sleep: %d ticks elapsed\n", end - start);
}

void test_gettimeofday()
{
    printfYellow("===========  Test GetTimeOfDay ===========\n");

    struct timeval start, end;

    gettimeofday(&start);
    printf("Start: %d seconds, %d microseconds\n", start.tv_sec, start.tv_usec);

    // 做一些工作
    sleep(20); // 睡眠 2 秒

    gettimeofday(&end);
    printf("End: %d seconds, %d microseconds\n", end.tv_sec, end.tv_usec);

    uint64 elapsed = (end.tv_sec - start.tv_sec) * 1000000 +
                     (end.tv_usec - start.tv_usec);
    printf("Elapsed: %d microseconds\n", elapsed);
}

void test_read()
{
    printfYellow("===========  Test Read README ===========\n");
    
    int fd;
    char buffer[512];
    int n;
    
    // 打开 README 文件
    fd = open("README", O_RDONLY);
    if (fd < 0) {
        printfYellow("Failed to open README file\n");
        return;
    }
    
    printf("Successfully opened README file, fd: %d\n", fd);
    
    // 读取文件内容
    printf("File content:\n");
    printfYellow("----------------------------------------\n");
    
    while ((n = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[n] = '\0';  // 添加字符串结束符
        printf("%s", buffer);
    }
    
    printfYellow("----------------------------------------\n");
    
    if (n < 0) {
        printfYellow("Error reading file\n");
    }
    
    close(fd);
    printf("File closed successfully\n");
}