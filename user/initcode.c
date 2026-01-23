#include "user.h"
#include "memlayout.h"
#include "../src/fcntl.h"
#define TEST_SYSCALLS
#include "../src/syscall/syscall.h"

void test_(char *name);
void test_busybox_musl();
void test_basic();
static void test_cow(void);
static int socket_test();
static int udp_dns_test();
static int udp_host_echo_test();
static int tcp_loopback_test();
static int tcp_host_echo_test();

#define HOST_IP "10.0.2.2"
#define HOST_PORT 12345
#define LOCAL_IP "10.0.2.15"
#define ECHO_PORT 9090
#define DNS_IP "10.0.2.3"
#define DNS_PORT 53
#define TCP_PORT 12346
#define TCP_HOST_PORT 12346
#define TEST_UDP_LOOPBACK 0
#define TEST_UDP_DNS 0
#define TEST_UDP_HOST_ECHO 0
#define TEST_TCP_LOOPBACK 0
#define TEST_TCP_HOST_ECHO 1




int main()
{
    if (open("console", O_RDWR) < 0)
    {
        printf("trying mknod");
        mknod("console", 1, 1);
        open("console", O_RDWR);
    }
    dup(0); // stdout
    dup(0); // stderr
 #if TEST_UDP_LOOPBACK
    socket_test();
 #endif
 #if TEST_UDP_DNS
    udp_dns_test();
 #endif
 #if TEST_UDP_HOST_ECHO
    udp_host_echo_test();
 #endif
 #if TEST_TCP_LOOPBACK
    tcp_loopback_test();
 #endif
 #if TEST_TCP_HOST_ECHO
    tcp_host_echo_test();
 #endif
    printf("All tests done!\n");
    while(1);
    // Provide /bin/sh for script fallback.
    // mkdir("/bin");
    // printf("r1 = %d\n", r1);
    // syscall(SYS_symlink, "/musl/busybox", "/bin/sh");
    // printf("r2 = %d\n", r2)

    // test_cow();
    // test_basic();
    // test_busybox_musl();
    // printf("Hello, xv6 world!\n");

    test_("getppid");

    test_("chdir");
    test_("times");
    test_("sleep");
    test_("fork");
    test_("gettimeofday");

    test_("open");
    test_("read");
    test_("brk");

    test_("getcwd");

    test_("openat");
    test_("getpid");
    test_("exit");
    test_("wait");
    test_("execve");
    test_("clone");
    test_("yield");
    test_("waitpid");

    test_("getcwd");
    test_("dup");
    test_("close");
    test_("mkdir_");

    test_("getdents");
    test_("pipe");
    test_("fstat");
    test_("write");
    test_("uname");
    test_("mmap");
    test_("munmap");

    test_("unlink");
    test_("fstat");
    test_("dup2");

    // test_busybox();
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

void test_busybox_musl()
{
    printf("=== Testing busybox ===\n");
    chdir("/musl/");

    int pid = fork();
    if (pid == 0)
    {
        // 使用动态链接器运行 musl 测试（先尝试 /musl 目录结构）
        // char *argv1[] = {"sh", "/musl/run-dynamic.sh", 0};
        char *argv1[] = {"sh", "/musl/ltp_testcode.sh", 0};
        char *envp1[] = {"PATH=/musl:/bin:/usr/bin", 0};
        execve("/musl/busybox", argv1, envp1);
        printf("Exec busybox failed!\n");
        syscall(SYS_exit, -1);
    }
    else if (pid > 0)
    {
        int status;
        wait(&status);
        printf("busybox child exited, status=0x%x\n", status);
    }
    else
    {
        printf("Fork failed!\n");
    }
}

void test_basic()
{
    printf("=== Testing basic syscalls ===\n");
    chdir("/musl/");

    int pid = fork();
    if (pid == 0)
    {
        char *argv[] = {"sh", "/musl/basic_testcode.sh", 0};
        char *envp[] = {"PATH=/bin:/musl:/usr/bin", 0};
        execve("/musl/busybox", argv, envp);
        printf("Exec busybox failed!\n");
        syscall(SYS_exit, -1);
    }
    else if (pid > 0)
    {
        int status;
        wait(&status);
    }
    else
    {
        printf("Fork failed!\n");
    }
}

static void
test_cow(void)
{
    printf("=== Testing COW ===\n");
    char *p = (char *)(syscall(SYS_xv6_sbrk, 4096));
    if (p == (char *)-1) {
        printf("sbrk failed\n");
        return;
    }

    p[0] = 'P';
    p[1] = '0';

    int pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        return;
    }

    if (pid == 0) {
        p[0] = 'C';
        p[1] = '1';
        printf("child sees %c%c\n", p[0], p[1]);
        syscall(SYS_exit, 0);
    }

    int status;
    wait(&status);
    printf("parent sees %c%c\n", p[0], p[1]);
}

static int
u8_to_dec(unsigned char v, char *out)
{
  int n = 0;
  if(v >= 100){
    out[n++] = '0' + (v / 100);
    v %= 100;
  }
  if(v >= 10 || n > 0){
    out[n++] = '0' + (v / 10);
    v %= 10;
  }
  out[n++] = '0' + v;
  return n;
}

static void
ip_to_str(uint32 ip, char *out)
{
  unsigned char *p = (unsigned char *)&ip;
  int n = 0;
  n += u8_to_dec(p[0], out + n);
  out[n++] = '.';
  n += u8_to_dec(p[1], out + n);
  out[n++] = '.';
  n += u8_to_dec(p[2], out + n);
  out[n++] = '.';
  n += u8_to_dec(p[3], out + n);
  out[n] = '\0';
}

static uint32
ip_bswap32(uint32 ip)
{
  return ((ip & 0x000000ffU) << 24) |
         ((ip & 0x0000ff00U) << 8) |
         ((ip & 0x00ff0000U) >> 8) |
         ((ip & 0xff000000U) >> 24);
}

int
socket_test()
{
  printf("======== test socket (UDP loopback) ==========\n");
  int srv = socket(2, 2, 0); // AF_INET=2, SOCK_DGRAM=2
  if(srv < 0){
    printf("socket failed\n");
    return -1;
  }

  if(bind(srv, "0.0.0.0", ECHO_PORT) < 0){
    printf("bind failed\n");
    close(srv);
    return -1;
  }

  int pid = fork();
  if(pid < 0){
    printf("fork failed\n");
    close(srv);
    return -1;
  }

  if(pid == 0){
    unsigned char buf[256];
    uint32 from_ip = 0;
    uint16 from_port = 0;
    int r = recvfrom(srv, buf, sizeof(buf), &from_ip, &from_port);
    if(r > 0){
      char ipbuf[32];
      ip_to_str(from_ip, ipbuf);
      int sret = sendto(srv, buf, r, ipbuf, from_port);
      if(sret < 0){
        uint32 swapped = ip_bswap32(from_ip);
        ip_to_str(swapped, ipbuf);
        sret = sendto(srv, buf, r, ipbuf, from_port);
        if(sret < 0)
          printf("server sendto failed\n");
      }
    }
    close(srv);
    exit(0);
  }

  close(srv);
  int cli = socket(2, 2, 0);
  if(cli < 0){
    printf("socket failed\n");
    return -1;
  }

  const char *msg = "udp loopback hhh";
  int n = sendto(cli, msg, strlen(msg), LOCAL_IP, ECHO_PORT);
  if(n < 0){
    printf("sendto failed\n");
    close(cli);
    return -1;
  }

  char rbuf[256];
  uint32 rip = 0;
  uint16 rport = 0;
  int r = recvfrom(cli, rbuf, sizeof(rbuf) - 1, &rip, &rport);
  if(r > 0){
    rbuf[r] = '\0';
    printf("recv %d bytes: %s\n", r, rbuf);
  } else {
    printf("recvfrom failed\n");
  }

  close(cli);
  wait(0);
  return 0;
}

// 测试 xv6 UDP 栈向 DNS 服务器发送查询并接收应答
// 期望行为：成功发送一个最小 A 记录查询到 DNS_IP (10.0.2.3)，recvfrom 能收到正确的 DNS reply，ID 和 QR 位匹配
static int
udp_dns_test()
{
  printf("======== test socket (UDP DNS) ==========\n");
  int fd = socket(2, 2, 0); // AF_INET=2, SOCK_DGRAM=2
  if(fd < 0){
    printf("socket failed\n");
    return -1;
  }

  // Minimal DNS query for A record of "example.com".
  unsigned char buf[256];
  int i = 0;
  unsigned short id = 0x1234;
  buf[i++] = (id >> 8) & 0xff;
  buf[i++] = id & 0xff;
  buf[i++] = 0x01; // flags: recursion desired
  buf[i++] = 0x00;
  buf[i++] = 0x00; // qdcount = 1
  buf[i++] = 0x01;
  buf[i++] = 0x00; // ancount
  buf[i++] = 0x00;
  buf[i++] = 0x00; // nscount
  buf[i++] = 0x00;
  buf[i++] = 0x00; // arcount
  buf[i++] = 0x00;

  buf[i++] = 7; // "example"
  memmove(&buf[i], "example", 7);
  i += 7;
  buf[i++] = 3; // "com"
  memmove(&buf[i], "com", 3);
  i += 3;
  buf[i++] = 0; // end of name
  buf[i++] = 0x00; // qtype A
  buf[i++] = 0x01;
  buf[i++] = 0x00; // qclass IN
  buf[i++] = 0x01;

  int n = sendto(fd, buf, i, DNS_IP, DNS_PORT);
  if(n < 0){
    printf("sendto failed\n");
    close(fd);
    return -1;
  }

  uint32 from_ip = 0;
  uint16 from_port = 0;
  int r = recvfrom(fd, buf, sizeof(buf), &from_ip, &from_port);
  if(r >= 12){
    unsigned short rid = ((unsigned short)buf[0] << 8) | buf[1];
    int qr = (buf[2] & 0x80) != 0;
    if(rid == id && qr){
      printf("dns reply ok: %d bytes\n", r);
    } else {
      printf("dns reply invalid: id=0x%x qr=%d\n", rid, qr);
    }
  } else {
    printf("recvfrom failed\n");
  }

  close(fd);
  return 0;
}

// 测试 xv6 UDP 栈向 host 发送回显消息并接收应答
// 期望行为：发送字符串 "hello from xv6" 到 HOST_IP:HOST_PORT，recvfrom 能收到同样的内容，说明 UDP TX/RX 正常
// 回包到达 guest，但 socket table 没匹配 → recvfrom failed
static int
udp_host_echo_test()
{
  printf("======== test socket (UDP host echo) ==========\n");
  int fd = socket(2, 2, 0); // AF_INET=2, SOCK_DGRAM=2
  if(fd < 0){
    printf("socket failed\n");
    return -1;
  }

  if(bind(fd, "0.0.0.0", HOST_PORT) < 0){
    printf("bind failed\n");
    close(fd);
    return -1;
  }

  const char *msg = "hello from xv6";
  int n = sendto(fd, msg, strlen(msg), HOST_IP, HOST_PORT);
  if(n < 0){
    printf("sendto failed\n");
    close(fd);
    return -1;
  }

  char buf[256];
  uint32 from_ip = 0;
  uint16 from_port = 0;
  int r = recvfrom(fd, buf, sizeof(buf) - 1, &from_ip, &from_port);
  if(r > 0){
    buf[r] = '\0';
    printf("recv %d bytes: %s\n", r, buf);
  } else {
    printf("recvfrom failed\n");
  }

  close(fd);
  return 0;
}

// 测试 xv6 TCP 栈的本机回环能力（server+client 同机）
// 期望行为：server 监听 TCP_PORT，client 连接并发送 "tcp loopback"，server 回显，client 能收到相同内容
static int
tcp_loopback_test()
{
  printf("======== test socket (TCP loopback) ==========\n");
  int srv = socket(2, 1, 0); // AF_INET=2, SOCK_STREAM=1
  if(srv < 0){
    printf("socket failed\n");
    return -1;
  }

  if(bind(srv, LOCAL_IP, TCP_PORT) < 0){
    printf("bind failed\n");
    close(srv);
    return -1;
  }

  if(listen(srv, 1) < 0){
    printf("listen failed\n");
    close(srv);
    return -1;
  }

  int pid = fork();
  if(pid < 0){
    printf("fork failed\n");
    close(srv);
    return -1;
  }

  if(pid == 0){
    int cli = socket(2, 1, 0);
    if(cli < 0){
      printf("socket failed\n");
      exit(1);
    }
    if(connect(cli, LOCAL_IP, TCP_PORT) < 0){
      printf("connect failed\n");
      close(cli);
      exit(1);
    }
    const char *msg = "tcp loopback";
    if(write(cli, msg, strlen(msg)) < 0)
      printf("write failed\n");
    char buf[64];
    int r = read(cli, buf, sizeof(buf) - 1);
    if(r > 0){
      buf[r] = '\0';
      printf("recv %d bytes: %s\n", r, buf);
    } else {
      printf("read failed\n");
    }
    close(cli);
    exit(0);
  }

  uint32 from_ip = 0;
  uint16 from_port = 0;
  int ns = accept(srv, &from_ip, &from_port, 5);
  if(ns < 0){
    printf("accept failed\n");
    close(srv);
    kill(pid);
    wait(0);
    return -1;
  }

  char buf[64];
  int r = read(ns, buf, sizeof(buf));
  if(r > 0){
    if(write(ns, buf, r) < 0)
      printf("write failed\n");
  } else {
    printf("read failed\n");
  }

  close(ns);
  close(srv);
  wait(0);
  return 0;
}

// 测试 xv6 TCP 栈向 host 发起连接并接收回显
// 期望行为：连接 HOST_IP:TCP_HOST_PORT，发送 "hello from xv6"，read 能收到相同内容
static int
tcp_host_echo_test()
{
  printf("======== test socket (TCP host echo) ==========\n");
  int fd = socket(2, 1, 0);
  if(fd < 0){
    printf("socket failed\n");
    return -1;
  }

  if(connect(fd, HOST_IP, TCP_HOST_PORT) < 0){
    printf("connect failed\n");
    close(fd);
    return -1;
  }

  const char *msg = "hello from xv6";
  if(write(fd, msg, strlen(msg)) < 0)
    printf("write failed\n");

  char buf[256];
  int r = read(fd, buf, sizeof(buf) - 1);
  if(r > 0){
    buf[r] = '\0';
    printf("recv %d bytes: %s\n", r, buf);
  } else {
    printf("read failed\n");
  }

  close(fd);
  return 0;
}
