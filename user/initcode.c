#include "user.h"
#include "memlayout.h"

void test_fork();

int main(){
    printf("Hello, World!\n");

    long long addr = sbrk(0);

    printf("Current break address: %p\n", addr);

    sbrk(addr + 4096); // 增加4KB

    // 测试是否成功增加
    // 写入 'test sbrk'

    char *test_str = "test sbrk";
    int n = 9;
    for(int i = 0; i < n; i++) {
        *((char*)addr + i) = test_str[i];
    }
    *((char*)addr + n) = '\0';
    printf("%s\n", (char*)addr);

    int pid = fork();
    int status;
    if (pid < 0) {
        printf("Fork failed!\n");
    } else if (pid == 0) {
        // Child process
        printf("Hello from child process! PID: %d\n", getpid());
        exit(0);
    } else {
        // Parent process
        wait(&status);
        printf("Hello from parent process! PID: %d, Child PID: %d\n", getpid(), pid);
        printf("Child exited with status: %d\n", status);
    }

    shutdown();
    while(1);
    return 0;
}

//TODO 为什么封装为函数就会有访存错误？ 
void test_fork(){

}