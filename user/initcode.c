#include "user.h"
#include "memlayout.h"
#include "../src/fcntl.h"
#define TEST_SYSCALLS
#include "../src/syscall/syscall.h"

//void test_fork();

// void test_sbrk();

// void test_uptime();

// void test_gettimeofday();

// void test_read();

// void test_shell();

void test_(char* name){

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

int main()
{
    if (open("console", O_RDWR) < 0)
    {
        mknod("console", 1, 1);
        open("console", O_RDWR);
    }
    dup(0); // stdout
    dup(0); // stderr


    // test_shell();
    test_("fork");
    //test_("gettimeofday");

    //test_("open");
    //test_("read");
    // test_("brk");
    //test_("mmap");
    //test_("openat");
    test_("getppid");
    shutdown();
    return 0;
}
