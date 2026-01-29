// System V Message Queue Test Program
// Tests all major features of the message queue implementation

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

// System V IPC constants (matching kernel implementation)
#define IPC_PRIVATE 0
#define IPC_CREAT   01000
#define IPC_EXCL    02000
#define IPC_NOWAIT  04000
#define IPC_RMID    0
#define IPC_STAT    2
#define MSG_NOERROR 010000

// System call numbers (matching syscall.h)
#define SYS_msgget 186
#define SYS_msgsnd 187
#define SYS_msgrcv 188
#define SYS_msgctl 189

// Message buffer structure
struct msgbuf {
    long mtype;       // Message type
    char mtext[256];  // Message data
};

// Message queue stats (simplified)
struct msqid_ds {
    // Only including fields we'll check
    char padding[64];  // Permission structure
    unsigned long msg_qnum;   // Number of messages in queue
    unsigned long msg_qbytes; // Max bytes in queue
};

// System call wrappers
static inline long msgget(int key, int msgflg) {
    register long a7 __asm__("a7") = SYS_msgget;
    register long a0 __asm__("a0") = key;
    register long a1 __asm__("a1") = msgflg;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7), "r"(a1) : "memory");
    return a0;
}

static inline long msgsnd(int msqid, const void *msgp, unsigned long msgsz, int msgflg) {
    register long a7 __asm__("a7") = SYS_msgsnd;
    register long a0 __asm__("a0") = msqid;
    register long a1 __asm__("a1") = (long)msgp;
    register long a2 __asm__("a2") = msgsz;
    register long a3 __asm__("a3") = msgflg;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2), "r"(a3) : "memory");
    return a0;
}

static inline long msgrcv(int msqid, void *msgp, unsigned long msgsz, long msgtyp, int msgflg) {
    register long a7 __asm__("a7") = SYS_msgrcv;
    register long a0 __asm__("a0") = msqid;
    register long a1 __asm__("a1") = (long)msgp;
    register long a2 __asm__("a2") = msgsz;
    register long a3 __asm__("a3") = msgtyp;
    register long a4 __asm__("a4") = msgflg;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2), "r"(a3), "r"(a4) : "memory");
    return a0;
}

static inline long msgctl(int msqid, int cmd, struct msqid_ds *buf) {
    register long a7 __asm__("a7") = SYS_msgctl;
    register long a0 __asm__("a0") = msqid;
    register long a1 __asm__("a1") = cmd;
    register long a2 __asm__("a2") = (long)buf;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : "memory");
    return a0;
}

// Test functions

void test_basic_send_receive() {
    printf("\n=== Test 1: Basic Send/Receive ===\n");

    // Create message queue
    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (msqid < 0) {
        printf("FAIL: msgget() returned %d\n", msqid);
        return;
    }
    printf("PASS: Created message queue %d\n", msqid);

    // Send message
    struct msgbuf msg;
    msg.mtype = 1;
    strcpy(msg.mtext, "Hello, Message Queue!");

    int ret = msgsnd(msqid, &msg, strlen(msg.mtext) + 1, 0);
    if (ret < 0) {
        printf("FAIL: msgsnd() returned %d\n", ret);
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }
    printf("PASS: Sent message: \"%s\"\n", msg.mtext);

    // Receive message
    struct msgbuf rcvmsg;
    ret = msgrcv(msqid, &rcvmsg, sizeof(rcvmsg.mtext), 0, 0);
    if (ret < 0) {
        printf("FAIL: msgrcv() returned %d\n", ret);
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }
    printf("PASS: Received message: \"%s\" (type=%ld, size=%d)\n",
           rcvmsg.mtext, rcvmsg.mtype, ret);

    // Verify content
    if (strcmp(msg.mtext, rcvmsg.mtext) != 0) {
        printf("FAIL: Message content mismatch!\n");
    } else {
        printf("PASS: Message content verified\n");
    }

    // Cleanup
    msgctl(msqid, IPC_RMID, NULL);
    printf("PASS: Test 1 completed successfully\n");
}

void test_type_filtering() {
    printf("\n=== Test 2: Message Type Filtering ===\n");

    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (msqid < 0) {
        printf("FAIL: msgget() returned %d\n", msqid);
        return;
    }

    // Send messages of different types
    struct msgbuf msg;
    for (int i = 1; i <= 3; i++) {
        msg.mtype = i;
        snprintf(msg.mtext, sizeof(msg.mtext), "Message type %d", i);
        if (msgsnd(msqid, &msg, strlen(msg.mtext) + 1, 0) < 0) {
            printf("FAIL: msgsnd() type %d failed\n", i);
            msgctl(msqid, IPC_RMID, NULL);
            return;
        }
        printf("PASS: Sent message type %d\n", i);
    }

    // Receive type 2 message (should skip type 1)
    struct msgbuf rcvmsg;
    int ret = msgrcv(msqid, &rcvmsg, sizeof(rcvmsg.mtext), 2, 0);
    if (ret < 0 || rcvmsg.mtype != 2) {
        printf("FAIL: Expected type 2, got type %ld\n", rcvmsg.mtype);
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }
    printf("PASS: Correctly received type 2: \"%s\"\n", rcvmsg.mtext);

    // Receive remaining messages (type 0 = any type)
    ret = msgrcv(msqid, &rcvmsg, sizeof(rcvmsg.mtext), 0, 0);
    if (ret < 0 || rcvmsg.mtype != 1) {
        printf("FAIL: Expected type 1, got type %ld\n", rcvmsg.mtype);
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }
    printf("PASS: Received type 1: \"%s\"\n", rcvmsg.mtext);

    ret = msgrcv(msqid, &rcvmsg, sizeof(rcvmsg.mtext), 0, 0);
    if (ret < 0 || rcvmsg.mtype != 3) {
        printf("FAIL: Expected type 3, got type %ld\n", rcvmsg.mtype);
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }
    printf("PASS: Received type 3: \"%s\"\n", rcvmsg.mtext);

    msgctl(msqid, IPC_RMID, NULL);
    printf("PASS: Test 2 completed successfully\n");
}

void test_nowait() {
    printf("\n=== Test 3: Non-Blocking Operations ===\n");

    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (msqid < 0) {
        printf("FAIL: msgget() returned %d\n", msqid);
        return;
    }

    // Try to receive from empty queue with IPC_NOWAIT
    struct msgbuf msg;
    int ret = msgrcv(msqid, &msg, sizeof(msg.mtext), 0, IPC_NOWAIT);
    if (ret == -1) {
        printf("PASS: msgrcv() correctly returned -1 for empty queue\n");
    } else {
        printf("FAIL: msgrcv() should fail on empty queue with IPC_NOWAIT\n");
    }

    // Send a message
    msg.mtype = 1;
    strcpy(msg.mtext, "Test nowait");
    if (msgsnd(msqid, &msg, strlen(msg.mtext) + 1, 0) < 0) {
        printf("FAIL: msgsnd() failed\n");
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }

    // Now receive should succeed with IPC_NOWAIT
    ret = msgrcv(msqid, &msg, sizeof(msg.mtext), 0, IPC_NOWAIT);
    if (ret > 0) {
        printf("PASS: msgrcv() with IPC_NOWAIT succeeded: \"%s\"\n", msg.mtext);
    } else {
        printf("FAIL: msgrcv() should succeed when message available\n");
    }

    msgctl(msqid, IPC_RMID, NULL);
    printf("PASS: Test 3 completed successfully\n");
}

void test_multiprocess() {
    printf("\n=== Test 4: Multi-Process Communication ===\n");

    // Create message queue with specific key
    int key = 0x1234;
    int msqid = msgget(key, IPC_CREAT | IPC_EXCL | 0666);
    if (msqid < 0) {
        printf("FAIL: msgget() returned %d\n", msqid);
        return;
    }
    printf("PASS: Created message queue %d with key 0x%x\n", msqid, key);

    int pid = fork();
    if (pid < 0) {
        printf("FAIL: fork() failed\n");
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }

    if (pid == 0) {
        // Child process: receiver
        printf("[Child] Waiting for message...\n");

        struct msgbuf msg;
        int ret = msgrcv(msqid, &msg, sizeof(msg.mtext), 0, 0);
        if (ret < 0) {
            printf("[Child] FAIL: msgrcv() returned %d\n", ret);
            exit(1);
        }

        printf("[Child] PASS: Received from parent: \"%s\"\n", msg.mtext);

        // Send reply
        msg.mtype = 2;
        strcpy(msg.mtext, "Child says hello!");
        if (msgsnd(msqid, &msg, strlen(msg.mtext) + 1, 0) < 0) {
            printf("[Child] FAIL: msgsnd() failed\n");
            exit(1);
        }
        printf("[Child] PASS: Sent reply to parent\n");
        exit(0);
    } else {
        // Parent process: sender
        sleep(1);  // Give child time to start waiting

        struct msgbuf msg;
        msg.mtype = 1;
        strcpy(msg.mtext, "Parent says hello!");

        if (msgsnd(msqid, &msg, strlen(msg.mtext) + 1, 0) < 0) {
            printf("[Parent] FAIL: msgsnd() failed\n");
            msgctl(msqid, IPC_RMID, NULL);
            wait(NULL);
            return;
        }
        printf("[Parent] PASS: Sent message to child\n");

        // Wait for reply
        int ret = msgrcv(msqid, &msg, sizeof(msg.mtext), 2, 0);
        if (ret < 0) {
            printf("[Parent] FAIL: msgrcv() returned %d\n", ret);
            msgctl(msqid, IPC_RMID, NULL);
            wait(NULL);
            return;
        }
        printf("[Parent] PASS: Received reply: \"%s\"\n", msg.mtext);

        // Wait for child and cleanup
        wait(NULL);
        msgctl(msqid, IPC_RMID, NULL);
        printf("PASS: Test 4 completed successfully\n");
    }
}

void test_stat() {
    printf("\n=== Test 5: Queue Statistics ===\n");

    int msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (msqid < 0) {
        printf("FAIL: msgget() returned %d\n", msqid);
        return;
    }

    // Send 3 messages
    struct msgbuf msg;
    for (int i = 1; i <= 3; i++) {
        msg.mtype = i;
        snprintf(msg.mtext, sizeof(msg.mtext), "Message %d", i);
        msgsnd(msqid, &msg, strlen(msg.mtext) + 1, 0);
    }

    // Get statistics
    struct msqid_ds stat;
    if (msgctl(msqid, IPC_STAT, &stat) < 0) {
        printf("FAIL: msgctl(IPC_STAT) failed\n");
        msgctl(msqid, IPC_RMID, NULL);
        return;
    }

    printf("PASS: Queue statistics:\n");
    printf("  - Messages in queue: %lu\n", stat.msg_qnum);
    printf("  - Max bytes allowed: %lu\n", stat.msg_qbytes);

    if (stat.msg_qnum == 3) {
        printf("PASS: Message count is correct\n");
    } else {
        printf("FAIL: Expected 3 messages, got %lu\n", stat.msg_qnum);
    }

    msgctl(msqid, IPC_RMID, NULL);
    printf("PASS: Test 5 completed successfully\n");
}

int main() {
    printf("========================================\n");
    printf("System V Message Queue Test Suite\n");
    printf("========================================\n");

    test_basic_send_receive();
    test_type_filtering();
    test_nowait();
    test_multiprocess();
    test_stat();

    printf("\n========================================\n");
    printf("All tests completed!\n");
    printf("========================================\n");

    return 0;
}
