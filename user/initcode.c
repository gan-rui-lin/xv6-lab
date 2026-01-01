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

void test_busybox(){
    printf("=== Testing busybox ===\n");
    int pid = fork();
    if(pid == 0){
        char *argv[] = {"busybox", "echo", "Hello from busybox!", 0};
        exec("busybox", argv);
        printf("Exec busybox failed!\n");
        syscall(SYS_exit, -1);
    } else if(pid > 0){
        int status;
        wait(&status);
    } else {
        printf("Fork failed!\n");
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

    test_("getppid");

    test_("chdir");
    // test_shell();
    test_("times");
    test_("sleep");
    test_("fork");
    test_("gettimeofday");

    test_("open");
    test_("read");
    test_("brk");

    test_("openat");
    test_("getpid");
    test_("exit");
    test_("wait");
    test_("execve");
    test_("clone");
    test_("yield");
    test_("waitpid");

    test_("dup");
    test_("close");
    test_("mkdir_"); //! failed
    // // test_("chdir"); //! another syscall called, not completed
    // test_("dup2");  //! failed
    test_("getdents"); //! wrong getdents fd:1
    // test_("mount"); //! exception
    test_("pipe");  //? maybe right
    test_("fstat"); //! wrong
    test_("write");
    test_("uname");
        test_("mmap");
    test_("munmap");

    // test_busybox();
    shutdown();
    return 0;
}
