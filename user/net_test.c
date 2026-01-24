#include "user.h"

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen);

#define IPV4(a,b,c,d) ((uint32)((a) | ((b) << 8) | ((c) << 16) | ((d) << 24)))
#define HOST_IP_U32 IPV4(10,0,2,2)
#define HOST_PORT 12345
#define LOCAL_IP_U32 IPV4(10,0,2,15)
#define ECHO_PORT 9090
#define DNS_IP_U32 IPV4(10,0,2,3)
#define DNS_PORT 53
#define BRIDGE_HOST_IP_U32 IPV4(10,0,2,2)
#define BRIDGE_HOST_PORT 12345
#define TCP_PORT 12346
#define TCP_HOST_PORT 12346

// static int
// u8_to_dec(unsigned char v, char *out)
// {
//   int n = 0;
//   if(v >= 100){
//     out[n++] = '0' + (v / 100);
//     v %= 100;
//   }
//   if(v >= 10 || n > 0){
//     out[n++] = '0' + (v / 10);
//     v %= 10;
//   }
//   out[n++] = '0' + v;
//   return n;
// }

// static void
// ip_to_str(uint32 ip, char *out)
// {
//   unsigned char *p = (unsigned char *)&ip;
//   int n = 0;
//   n += u8_to_dec(p[0], out + n);
//   out[n++] = '.';
//   n += u8_to_dec(p[1], out + n);
//   out[n++] = '.';
//   n += u8_to_dec(p[2], out + n);
//   out[n++] = '.';
//   n += u8_to_dec(p[3], out + n);
//   out[n] = '\0';
// }

// static uint32
// ip_bswap32(uint32 ip)
// {
//   return ((ip & 0x000000ffU) << 24) |
//          ((ip & 0x0000ff00U) << 8) |
//          ((ip & 0x00ff0000U) >> 8) |
//          ((ip & 0xff000000U) >> 24);
// }

int
socket_test()
{
  printf("======== test socket (UDP loopback) =========="
         "\n");
  int srv = socket(2, 2, 0); // AF_INET=2, SOCK_DGRAM=2
  if(srv < 0){
    printf("socket failed\n");
    return -1;
  }

  struct sockaddr_in srv_addr;
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(ECHO_PORT);
  srv_addr.sin_addr.s_addr = htonl(0);
  if(bind(srv, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0){
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
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    int r = recvfrom(srv, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
    if(r > 0){
      int sret = sendto(srv, buf, r, 0, (struct sockaddr *)&from, fromlen);
      if(sret < 0)
        printf("server sendto failed\n");
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
  struct sockaddr_in dst;
  dst.sin_family = AF_INET;
  dst.sin_port = htons(ECHO_PORT);
  dst.sin_addr.s_addr = htonl(LOCAL_IP_U32);
  int n = sendto(cli, msg, strlen(msg), 0, (struct sockaddr *)&dst, sizeof(dst));
  if(n < 0){
    printf("sendto failed\n");
    close(cli);
    return -1;
  }

  char rbuf[256];
  struct sockaddr_in src;
  socklen_t srclen = sizeof(src);
  int r = recvfrom(cli, rbuf, sizeof(rbuf) - 1, 0, (struct sockaddr *)&src, &srclen);
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
int
udp_dns_test()
{
  printf("======== test socket (UDP DNS) =========="
         "\n");
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

  struct sockaddr_in from;
  socklen_t fromlen = sizeof(from);
  struct sockaddr_in dns;
  dns.sin_family = AF_INET;
  dns.sin_port = htons(DNS_PORT);
  dns.sin_addr.s_addr = htonl(DNS_IP_U32);
  int n = sendto(fd, buf, i, 0, (struct sockaddr *)&dns, sizeof(dns));
  if(n < 0){
    printf("sendto failed\n");
    close(fd);
    return -1;
  }

  int r = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
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
int
udp_host_echo_test()
{
  printf("======== test socket (UDP host echo) =========="
         "\n");
  int fd = socket(2, 2, 0); // AF_INET=2, SOCK_DGRAM=2
  if(fd < 0){
    printf("socket failed\n");
    return -1;
  }

  struct sockaddr_in bind_addr;
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(HOST_PORT);
  bind_addr.sin_addr.s_addr = htonl(0);
  if(bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0){
    printf("bind failed\n");
    close(fd);
    return -1;
  }

  const char *msg = "hello from xv6";
  struct sockaddr_in host;
  host.sin_family = AF_INET;
  host.sin_port = htons(HOST_PORT);
  host.sin_addr.s_addr = htonl(HOST_IP_U32);
  int n = sendto(fd, msg, strlen(msg), 0, (struct sockaddr *)&host, sizeof(host));
  if(n < 0){
    printf("sendto failed\n");
    close(fd);
    return -1;
  }

  char buf[256];
  struct sockaddr_in from;
  socklen_t fromlen = sizeof(from);
  int r = recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &fromlen);
  if(r > 0){
    buf[r] = '\0';
    printf("recv %d bytes: %s\n", r, buf);
  } else {
    printf("recvfrom failed\n");
  }

  close(fd);
  return 0;
}

// 桥接模式下同网段 host 回显测试
// 期望行为：发送字符串 "hello from xv6" 到 BRIDGE_HOST_IP:BRIDGE_HOST_PORT，recvfrom 能收到相同内容
int
udp_bridge_host_echo_test()
{
  printf("======== test socket (UDP bridge host echo) =========="
         "\n");
  int fd = socket(2, 2, 0); // AF_INET=2, SOCK_DGRAM=2
  if(fd < 0){
    printf("socket failed\n");
    return -1;
  }

  struct sockaddr_in bind_addr;
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(BRIDGE_HOST_PORT);
  bind_addr.sin_addr.s_addr = htonl(0);
  if(bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0){
    printf("bind failed\n");
    close(fd);
    return -1;
  }

  const char *msg = "hello from xv6";
  struct sockaddr_in host;
  host.sin_family = AF_INET;
  host.sin_port = htons(BRIDGE_HOST_PORT);
  host.sin_addr.s_addr = htonl(BRIDGE_HOST_IP_U32);
  int n = sendto(fd, msg, strlen(msg), 0, (struct sockaddr *)&host, sizeof(host));
  if(n < 0){
    printf("sendto failed\n");
    close(fd);
    return -1;
  }

  char buf[256];
  struct sockaddr_in from;
  socklen_t fromlen = sizeof(from);
  int r = recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &fromlen);
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
int
tcp_loopback_test()
{
  printf("======== test socket (TCP loopback) =========="
         "\n");
  int srv = socket(2, 1, 0); // AF_INET=2, SOCK_STREAM=1
  if(srv < 0){
    printf("socket failed\n");
    return -1;
  }

  struct sockaddr_in srv_addr;
  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = htons(TCP_PORT);
  srv_addr.sin_addr.s_addr = htonl(LOCAL_IP_U32);
  if(bind(srv, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0){
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
    struct sockaddr_in dst;
    dst.sin_family = AF_INET;
    dst.sin_port = htons(TCP_PORT);
    dst.sin_addr.s_addr = htonl(LOCAL_IP_U32);
    if(connect(cli, (struct sockaddr *)&dst, sizeof(dst)) < 0){
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

  struct sockaddr_in from;
  socklen_t fromlen = sizeof(from);
  int ns = accept(srv, (struct sockaddr *)&from, &fromlen);
  if(ns < 0){
    printf("accept failed\n");
    close(srv);
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
int
tcp_host_echo_test()
{
  printf("======== test socket (TCP host echo) =========="
         "\n");
  int fd = socket(2, 1, 0);
  if(fd < 0){
    printf("socket failed\n");
    return -1;
  }

  struct sockaddr_in host;
  host.sin_family = AF_INET;
  host.sin_port = htons(TCP_HOST_PORT);
  host.sin_addr.s_addr = htonl(HOST_IP_U32);
  if(connect(fd, (struct sockaddr *)&host, sizeof(host)) < 0){
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
