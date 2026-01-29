#include "../src/types.h"
#include "../src/fs/stat.h"
#include "user.h"

// System V IPC constants
#define IPC_CREAT  01000
#define IPC_EXCL   02000
#define IPC_RMID   0
#define IPC_STAT   2

struct shmid_ds {
  uint uid;
  uint gid;
  uint cuid;
  uint cgid;
  uint mode;
  uint seq;
  uint64 shm_segsz;
  uint64 shm_atime;
  uint64 shm_dtime;
  uint64 shm_ctime;
  int shm_cpid;
  int shm_lpid;
  uint64 shm_nattch;
};

void
test_basic_shm(void)
{
  int shmid;
  char *ptr;
  int key = 0x1234;

  printf("Test 1: Basic shared memory creation\n");

  // Create shared memory segment
  shmid = shmget(key, 100, IPC_CREAT | 0666);
  if(shmid < 0){
    printf("  FAIL: shmget failed\n");
    return;
  }
  printf("  PASS: shmget returned shmid=%d\n", shmid);

  // Attach to shared memory
  ptr = shmat(shmid, 0, 0);
  if(ptr == (void*)-1){
    printf("  FAIL: shmat failed\n");
    return;
  }
  printf("  PASS: shmat returned addr=%p\n", ptr);

  // Write to shared memory
  strcpy(ptr, "Hello from test!");
  printf("  PASS: Wrote data to shared memory\n");

  // Read back
  if(strcmp(ptr, "Hello from test!") != 0){
    printf("  FAIL: Read back incorrect data: %s\n", ptr);
    return;
  }
  printf("  PASS: Read back correct data: %s\n", ptr);

  // Detach
  if(shmdt(ptr) < 0){
    printf("  FAIL: shmdt failed\n");
    return;
  }
  printf("  PASS: shmdt succeeded\n");

  // Delete
  if(shmctl(shmid, IPC_RMID, 0) < 0){
    printf("  FAIL: shmctl IPC_RMID failed\n");
    return;
  }
  printf("  PASS: shmctl IPC_RMID succeeded\n");

  printf("Test 1: SUCCESS\n\n");
}

void
test_fork_shm(void)
{
  int shmid;
  char *ptr;
  int key = 0x5678;
  int pid;

  printf("Test 2: Shared memory between parent and child\n");

  // Create shared memory segment
  shmid = shmget(key, 100, IPC_CREAT | IPC_EXCL | 0666);
  if(shmid < 0){
    printf("  FAIL: shmget failed\n");
    return;
  }
  printf("  PASS: Parent created shmid=%d\n", shmid);

  // Attach to shared memory
  ptr = shmat(shmid, 0, 0);
  if(ptr == (void*)-1){
    printf("  FAIL: Parent shmat failed\n");
    return;
  }
  printf("  PASS: Parent attached at addr=%p\n", ptr);

  // Write to shared memory
  strcpy(ptr, "Parent data");
  printf("  PASS: Parent wrote: %s\n", ptr);

  pid = fork();
  if(pid < 0){
    printf("  FAIL: fork failed\n");
    return;
  }

  if(pid == 0){
    // Child process
    char *child_ptr;

    // Child attaches to same shared memory
    child_ptr = shmat(shmid, 0, 0);
    if(child_ptr == (void*)-1){
      printf("  FAIL: Child shmat failed\n");
      exit(1);
    }
    printf("  PASS: Child attached at addr=%p\n", child_ptr);

    // Child reads data
    printf("  Child reads: %s\n", child_ptr);
    if(strcmp(child_ptr, "Parent data") != 0){
      printf("  FAIL: Child read incorrect data\n");
      exit(1);
    }
    printf("  PASS: Child read correct data\n");

    // Child writes new data
    strcpy(child_ptr, "Child modified");
    printf("  PASS: Child wrote: %s\n", child_ptr);

    shmdt(child_ptr);
    exit(0);
  } else {
    // Parent waits for child
    int status;
    wait(&status);

    if(status != 0){
      printf("  FAIL: Child exited with error\n");
      return;
    }

    // Parent reads modified data
    printf("  Parent reads: %s\n", ptr);
    if(strcmp(ptr, "Child modified") != 0){
      printf("  FAIL: Parent read incorrect data\n");
      return;
    }
    printf("  PASS: Parent sees child's modification\n");

    // Cleanup
    shmdt(ptr);
    shmctl(shmid, IPC_RMID, 0);

    printf("Test 2: SUCCESS\n\n");
  }
}

void
test_shmctl_stat(void)
{
  int shmid;
  struct shmid_ds buf;
  int key = 0xabcd;

  printf("Test 3: shmctl IPC_STAT\n");

  shmid = shmget(key, 100, IPC_CREAT | 0666);
  if(shmid < 0){
    printf("  FAIL: shmget failed\n");
    return;
  }

  if(shmctl(shmid, IPC_STAT, &buf) < 0){
    printf("  FAIL: shmctl IPC_STAT failed\n");
    return;
  }

  printf("  PASS: Got segment info:\n");
  printf("    Size: %d bytes\n", (int)buf.shm_segsz);
  printf("    Creator PID: %d\n", buf.shm_cpid);
  printf("    Attachments: %d\n", (int)buf.shm_nattch);
  printf("    Mode: 0%o\n", buf.mode & 0777);

  if(buf.shm_segsz != 4096){  // Should be rounded to page size
    printf("  WARN: Size is %d, expected 4096 (page size)\n", (int)buf.shm_segsz);
  }

  if(buf.shm_nattch != 0){
    printf("  FAIL: Should have 0 attachments, got %d\n", (int)buf.shm_nattch);
    return;
  }

  shmctl(shmid, IPC_RMID, 0);
  printf("Test 3: SUCCESS\n\n");
}

void
test_excl_flag(void)
{
  int shmid1, shmid2;
  int key = 0xdead;

  printf("Test 4: IPC_EXCL flag\n");

  // Create with IPC_EXCL
  shmid1 = shmget(key, 100, IPC_CREAT | IPC_EXCL | 0666);
  if(shmid1 < 0){
    printf("  FAIL: First shmget failed\n");
    return;
  }
  printf("  PASS: Created shmid=%d\n", shmid1);

  // Try to create again with IPC_EXCL - should fail
  shmid2 = shmget(key, 100, IPC_CREAT | IPC_EXCL | 0666);
  if(shmid2 >= 0){
    printf("  FAIL: Second shmget should have failed but got shmid=%d\n", shmid2);
    shmctl(shmid2, IPC_RMID, 0);
    return;
  }
  printf("  PASS: Second shmget correctly failed (IPC_EXCL)\n");

  // Try without IPC_EXCL - should succeed and return same ID
  shmid2 = shmget(key, 100, IPC_CREAT | 0666);
  if(shmid2 != shmid1){
    printf("  FAIL: Got different shmid: %d vs %d\n", shmid2, shmid1);
    return;
  }
  printf("  PASS: Got same shmid=%d without IPC_EXCL\n", shmid2);

  shmctl(shmid1, IPC_RMID, 0);
  printf("Test 4: SUCCESS\n\n");
}

int
main(int argc, char *argv[])
{
  printf("=== Shared Memory Test Suite ===\n\n");

  test_basic_shm();
  test_fork_shm();
  test_shmctl_stat();
  test_excl_flag();

  printf("=== All Tests Completed ===\n");
  exit(0);
}
