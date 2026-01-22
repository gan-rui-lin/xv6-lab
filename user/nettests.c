#include "user.h"
#include "types.h"

#define HOST_IP "10.0.2.2"
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
  int n = sendto(fd, msg, strlen(msg), HOST_IP, HOST_PORT);
  if(n < 0){
    printf("sendto failed\n");
    close(fd);
    exit(1);
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
  exit(0);
}
