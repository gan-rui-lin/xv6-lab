#include "types.h"
#include "defs.h"
#include "param.h"
#include "proc.h"
#include "syscall.h"
#include "errno.h"
#include "sleeplock.h"
#include "fs/file.h"


#include "bsd/socket.h"
#include "onps_errors.h"
#include "onps_utils.h"

static int
getsockfd(int fd, struct file **pf)
{
  struct proc *p = myproc();
  if(fd < 0 || fd >= NOFILE)
    return -EBADF;
  struct file *f = p->ofile[fd];
  if(f == 0 || f->type != FD_SOCKET)
    return -ENOTSUP;
  if(pf)
    *pf = f;
  return 0;
}

static int
onps_err_to_errno(EN_ONPSERR err)
{
  switch(err){
  case ERRNO:
    return 0;
  case ERRNOPAGENODE:
  case ERRREQMEMTOOLARGE:
  case ERRNOFREEMEM:
    return -ENOMEM;
  case ERRSOCKETTYPE:
    return -ENOTSUP;
  case ERRADDRFAMILIES:
  case ERRUNSUPPORTEDFAMILY:
  case ERRFAMILYINCONSISTENT:
    return -ENOTSUP;
  case ERRPORTOCCUPIED:
    return -EINVAL;
  case ERRNOTBINDADDR:
  case ERRPORTEMPTY:
    return -EINVAL;
  case ERRADDRESSING:
  case ERRNETUNREACHABLE:
    return -EIO;
  case ERRROUTEADDRMATCH:
    return -EINVAL;
  case ERRTCPCONNTIMEOUT:
    return -EAGAIN;
  case ERRTCPCONNRESET:
    return -EIO;
  case ERRTCPCONNCLOSED:
  case ERRTCPNOTCONNECTED:
    return -EIO;
  case ERRTCPBACKLOGFULL:
    return -EAGAIN;
  case ERRDATAEMPTY:
  case ERRSENDZEROBYTES:
    return -EINVAL;
  case ERRNETIFNOTFOUND:
    return -ENODEV;
  default:
    return -EIO;
  }
}

uint64
sys_socket(void)
{
  int domain, type, protocol;
  if(argint(0, &domain) < 0 || argint(1, &type) < 0 || argint(2, &protocol) < 0)
    return -EINVAL;

  struct file *f = filealloc();
  if(!f)
    return -EMFILE;

  EN_ONPSERR err = ERRNO;
  SOCKET s = socket(domain, type, protocol, &err);
  if(s < 0){
    f->ref = 0;
    f->type = FD_NONE;
    return (err != ERRNO) ? onps_err_to_errno(err) : -EIO;
  }

  f->type = FD_SOCKET;
  f->readable = 1;
  f->writable = 1;
  f->sock = (int)s;

  int fd = -1;
  for(int i = 0; i < NOFILE; i++){
    if(myproc()->ofile[i] == 0){
      myproc()->ofile[i] = f;
      fd = i;
      break;
    }
  }
  if(fd < 0){
    fileclose(f);
    return -EMFILE;
  }

  return fd;
}

uint64
sys_bind(void)
{
  int fd, port;
  uint64 uip;
  if(argint(0, &fd) < 0 || argaddr(1, &uip) < 0 || argint(2, &port) < 0)
    return -EINVAL;

  struct file *f;
  int r = getsockfd(fd, &f);
  if(r < 0)
    return r;

  char ipbuf[64];
  const char *ip = 0;
  if(uip != 0){
    if(fetchstr(uip, ipbuf, sizeof(ipbuf)) < 0)
      return -EFAULT;
    ip = ipbuf;
  }

  int ret = bind((SOCKET)f->sock, ip, (USHORT)port);
  if(ret < 0){
    EN_ONPSERR err = socket_get_last_error_code((SOCKET)f->sock);
    int kerr = onps_err_to_errno(err);
    return (kerr != 0) ? kerr : -EIO;
  }
  return 0;
}

uint64
sys_connect(void)
{
  int fd, port;
  uint64 uip;
  if(argint(0, &fd) < 0 || argaddr(1, &uip) < 0 || argint(2, &port) < 0)
    return -EINVAL;

  struct file *f;
  int r = getsockfd(fd, &f);
  if(r < 0)
    return r;

  char ipbuf[64];
  if(fetchstr(uip, ipbuf, sizeof(ipbuf)) < 0)
    return -EFAULT;

  log_info("sys_connect: fd=%d dst=%s:%d timeout=%d\n", fd, ipbuf, port, 1);
  int ret = connect((SOCKET)f->sock, ipbuf, (USHORT)port, 1);
  if(ret < 0){
    EN_ONPSERR err = socket_get_last_error_code((SOCKET)f->sock);
    const CHAR *msg = onps_error(err);
    log_warn("sys_connect: err=%d (%s)\n", err, msg ? msg : "unknown");
    int kerr = onps_err_to_errno(err);
    return (kerr != 0) ? kerr : -EIO;
  }
  log_info("sys_connect: ok\n");
  return 0;
}

uint64
sys_sendto(void)
{
  int fd, len, port;
  uint64 ubuf, uip;
  if(argint(0, &fd) < 0 || argaddr(1, &ubuf) < 0 || argint(2, &len) < 0 ||
     argaddr(3, &uip) < 0 || argint(4, &port) < 0)
    return -EINVAL;

  if(len < 0)
    return -EINVAL;

  struct file *f;
  int r = getsockfd(fd, &f);
  if(r < 0)
    return r;

  char ipbuf[64];
  if(fetchstr(uip, ipbuf, sizeof(ipbuf)) < 0)
    return -EFAULT;

  void *kbuf = 0;
  if(len > 0){
    kbuf = kmalloc(len);
    if(!kbuf)
      return -ENOMEM;
    if(copyin(myproc()->pagetable, kbuf, ubuf, len) < 0){
      kmfree(kbuf);
      return -EFAULT;
    }
  }

  int ret = sendto((SOCKET)f->sock, ipbuf, (USHORT)port, (UCHAR *)kbuf, len);
  if(kbuf)
    kmfree(kbuf);

  if(ret < 0){
    EN_ONPSERR err = ERRNO;
    const CHAR *msg = socket_get_last_error((SOCKET)f->sock, &err);
    log_warn("sys_sendto: dst=%s:%d len=%d err=%d (%s)\n", ipbuf, port, len, err, msg ? msg : "unknown");
    return (err != ERRNO) ? onps_err_to_errno(err) : -EIO;
  }
  return ret;
}

uint64
sys_recvfrom(void)
{
  int fd, len;
  uint64 ubuf, uip_out, uport_out;
  if(argint(0, &fd) < 0 || argaddr(1, &ubuf) < 0 || argint(2, &len) < 0 ||
     argaddr(3, &uip_out) < 0 || argaddr(4, &uport_out) < 0)
    return -EINVAL;

  if(len < 0)
    return -EINVAL;

  struct file *f;
  int r = getsockfd(fd, &f);
  if(r < 0)
    return r;

  void *kbuf = 0;
  if(len > 0){
    kbuf = kmalloc(len);
    if(!kbuf)
      return -ENOMEM;
  }

  in_addr_t from_ip = 0;
  USHORT from_port = 0;
  int ret = recvfrom((SOCKET)f->sock, (UCHAR *)kbuf, len, &from_ip, &from_port);
  if(ret > 0){
    if(copyout(myproc()->pagetable, ubuf, kbuf, ret) < 0){
      if(kbuf)
        kmfree(kbuf);
      return -EFAULT;
    }
  }
  if(kbuf)
    kmfree(kbuf);

  if(uip_out != 0)
    copyout(myproc()->pagetable, uip_out, (char *)&from_ip, sizeof(from_ip));
  if(uport_out != 0)
    copyout(myproc()->pagetable, uport_out, (char *)&from_port, sizeof(from_port));

  if(ret < 0){
    EN_ONPSERR err = ERRNO;
    const CHAR *msg = socket_get_last_error((SOCKET)f->sock, &err);
    log_warn("sys_recvfrom: err=%d (%s)\n", err, msg ? msg : "unknown");
    return (err != ERRNO) ? onps_err_to_errno(err) : -EIO;
  }
  return ret;
}

uint64
sys_listen(void)
{
  int fd, backlog;
  if(argint(0, &fd) < 0 || argint(1, &backlog) < 0)
    return -EINVAL;

  struct file *f;
  int r = getsockfd(fd, &f);
  if(r < 0)
    return r;

  int ret = 
  listen((SOCKET)f->sock, (USHORT)backlog);
  if(ret < 0){
    EN_ONPSERR err = socket_get_last_error_code((SOCKET)f->sock);
    const CHAR *msg = onps_error(err);
    log_warn("sys_listen: backlog=%d err=%d (%s)\n", backlog, err, msg ? msg : "unknown");
    int kerr = onps_err_to_errno(err);
    return (kerr != 0) ? kerr : -EIO;
  }
  return 0;
}

uint64
sys_accept(void)
{
  int fd, waitsecs;
  uint64 uip_out, uport_out;
  if(argint(0, &fd) < 0 || argaddr(1, &uip_out) < 0 || argaddr(2, &uport_out) < 0 || argint(3, &waitsecs) < 0)
    return -EINVAL;

  struct file *f;
  int r = getsockfd(fd, &f);
  if(r < 0)
    return r;

  in_addr_t from_ip = 0;
  USHORT from_port = 0;
  EN_ONPSERR err = ERRNO;
  SOCKET ns = accept((SOCKET)f->sock, &from_ip, &from_port, waitsecs, &err);
  if(ns < 0){
    const CHAR *msg = onps_error(err);
    log_warn("sys_accept: err=%d (%s)\n", err, msg ? msg : "unknown");
    int kerr = onps_err_to_errno(err);
    return (kerr != 0) ? kerr : -EIO;
  }

  struct file *nf = filealloc();
  if(!nf){
    close(ns);
    return -EMFILE;
  }
  nf->type = FD_SOCKET;
  nf->readable = 1;
  nf->writable = 1;
  nf->sock = (int)ns;

  int nfd = -1;
  for(int i = 0; i < NOFILE; i++){
    if(myproc()->ofile[i] == 0){
      myproc()->ofile[i] = nf;
      nfd = i;
      break;
    }
  }
  if(nfd < 0){
    fileclose(nf);
    return -EMFILE;
  }

  if(uip_out != 0)
    copyout(myproc()->pagetable, uip_out, (char *)&from_ip, sizeof(from_ip));
  if(uport_out != 0)
    copyout(myproc()->pagetable, uport_out, (char *)&from_port, sizeof(from_port));

  return nfd;
}
