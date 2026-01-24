#ifndef _USER_H_
#define _USER_H_
# include "types.h"
struct stat;

// socket types (POSIX-compatible minimal)
typedef unsigned short sa_family_t;

struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};

struct in_addr {
	uint32 s_addr;
};

struct sockaddr_in {
	sa_family_t sin_family;
	uint16 sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};

#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2

static inline uint16 htons(uint16 v) { return (uint16)((v << 8) | (v >> 8)); }
static inline uint16 ntohs(uint16 v) { return htons(v); }
static inline uint32 htonl(uint32 v)
{
	return ((v & 0x000000ffU) << 24) |
				 ((v & 0x0000ff00U) << 8) |
				 ((v & 0x00ff0000U) >> 8) |
				 ((v & 0xff000000U) >> 24);
}
static inline uint32 ntohl(uint32 v) { return htonl(v); }

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
int sched_yield(void);
int shutdown(void);
int gettimeofday(struct timeval *tv);
int fstat(int fd, struct stat*);

// socket calls
int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
			  const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
				struct sockaddr *src_addr, socklen_t *addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);


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
#endif // _USER_H_