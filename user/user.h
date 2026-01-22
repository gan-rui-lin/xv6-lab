# include "types.h"
struct stat;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(const char*, char**);
int execve(const char *name, char *const argv[], char *const argp[]);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
long long sbrk(int);
int sleep(int);
int uptime(void);
int shutdown(void);
int gettimeofday(struct timeval *tv);
int fstat(int fd, struct stat*);
// socket (minimal)
int socket(int domain, int type, int protocol);
int bind(int sockfd, const char *ip, int port);
int connect(int sockfd, const char *ip, int port);
int sendto(int sockfd, const void *buf, int len, const char *ip, int port);
int recvfrom(int sockfd, void *buf, int len, uint32 *ip, uint16 *port);
int listen(int sockfd, int backlog);
int accept(int sockfd, uint32 *ip, uint16 *port, int waitsecs);


// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...);
void printf(const char*, ...);
void printfYellow(const char *fmt, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);

