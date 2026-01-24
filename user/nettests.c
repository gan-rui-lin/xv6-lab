#include "user.h"
#include "types.h"

#define IPV4(a,b,c,d) ((uint32)((a) | ((b) << 8) | ((c) << 16) | ((d) << 24)))
#define HOST_IP_U32 IPV4(10,0,2,2)
#define HOST_PORT 12345

int
main(int argc, char **argv)
{
  int fd = socket(2, 2, 0); // AF_INET=2, SOCK_DGRAM=2
  if(fd < 0){
    printf("socket failed\n");
    exit(1);
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
    exit(1);
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
  exit(0);
}
