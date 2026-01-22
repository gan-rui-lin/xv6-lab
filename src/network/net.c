#include "types.h"
#include "defs.h"
#include "net.h"

#include "onps.h"
#include "onps_errors.h"
#include "onps_utils.h"
#include "netif/netif.h"
#include "ethernet/ethernet.h"
#include "mmu/buddy.h"
#include "mmu/buf_list.h"

#include "virtIO/virtio_net.h"

static PST_NETIF g_netif = NULL;

static int
net_emac_send(SHORT sBufListHead, UCHAR *pubErr)
{
  if (!g_netif) {
    log_warn("net: send dropped (no netif)\n");
    if (pubErr)
      *pubErr = (UCHAR)ERRNETIFNOTFOUND;
    return -1;
  }

  UINT len = buf_list_get_len(sBufListHead);
  if (len == 0 || len > 2048) {
    log_warn("net: send dropped (len=%u)\n", (uint)len);
    if (pubErr)
      *pubErr = (UCHAR)ERRPACKETTOOLARGE;
    return -1;
  }

  void *pkt = kmalloc(len);
  if (!pkt) {
    log_warn("net: send dropped (no mem, len=%u)\n", (uint)len);
    if (pubErr)
      *pubErr = (UCHAR)ERRNOFREEMEM;
    return -1;
  }

  buf_list_merge_packet(sBufListHead, (UCHAR *)pkt);
  int ret = virtio_net_transmit((const uint8 *)pkt, (int)len);
  if (ret < 0) {
    kmfree(pkt);
    log_warn("net: virtio tx failed (len=%u)\n", (uint)len);
    if (pubErr)
      *pubErr = (UCHAR)ERRNETIFSEND;
    return -1;
  }

  log_info("net: tx len=%u\n", (uint)len);
  return (int)len;
}

static void
net_start_eth_recv_thread(void *pvParam)
{
  (void)pvParam;
  log_info("net: starting eth_rx thread\n");
  kthread_create(thread_ethernet_ii_recv, pvParam, "eth_rx", PRIO_DEFAULT);
}

void
net_rx_deliver(const uint8 *data, int len)
{
  if (!g_netif || !data || len <= 0)
    return;

  log_info("net: rx len=%d\n", len);
  EN_ONPSERR err = ERRNO;
  PST_SLINKEDLIST_NODE node = (PST_SLINKEDLIST_NODE)buddy_alloc(sizeof(ST_SLINKEDLIST_NODE) + (UINT)len, &err);
  if (!node) {
    log_warn("net: rx drop (alloc failed, len=%d, err=%d)\n", len, err);
    return;
  }

  node->uniData.unVal = (UINT)len;
  memmove(((UCHAR *)node) + sizeof(ST_SLINKEDLIST_NODE), data, len);
  ethernet_put_packet(g_netif, node);
}

void
net_init(void)
{
  EN_ONPSERR err = ERRNO;

  if (!open_npstack_load(&err)) {
    log_error("net: onps load failed (%d)\n", err);
    return;
  }

  uint8 mac[6] = {0};
  if (virtio_net_init(1, mac) < 0) {
    log_error("net: virtio-net init failed\n");
    return;
  }

  ST_IPV4 ip;
  ip.unAddr = inet_addr("10.0.2.15");
  ip.unSubnetMask = inet_addr("255.255.255.0");
  ip.unGateway = inet_addr("10.0.2.2");
  ip.unPrimaryDNS = inet_addr("10.0.2.3");
  ip.unSecondaryDNS = inet_addr("8.8.8.8");
  ip.unBroadcast = broadcast_addr(ip.unAddr, ip.unSubnetMask);

  g_netif = NULL;
  PST_NETIF ret = ethernet_add("eth0", mac, &ip, net_emac_send, net_start_eth_recv_thread, &g_netif, &err);
  if (!ret) {
    log_error("net: ethernet_add failed (%d)\n", err);
    return;
  }

  g_netif = ret;
  log_info("net: eth0 up, ip=10.0.2.15\n");
}
