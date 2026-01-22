#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "virtio.h"
#include "virtio_net.h"
#include "network/net.h"

#define VIRTIO_NET_MAX_PKT 2048

// the address of virtio mmio register r.
#define R(n, r) ((volatile uint32 *)(VIRTION(n) + (r)))

__attribute__ ((packed))
struct virtio_net_hdr {
  uint8 flags;
  uint8 gso_type;
  uint16 hdr_len;
  uint16 gso_size;
  uint16 csum_start;
  uint16 csum_offset;
  uint16 num_buffers;
};

struct vnet {
  char rx_pages[2 * PGSIZE];
  char tx_pages[2 * PGSIZE];

  struct VRingDesc *rx_desc;
  uint16 *rx_avail;
  struct UsedArea *rx_used;

  struct VRingDesc *tx_desc;
  uint16 *tx_avail;
  struct UsedArea *tx_used;

  uint16 rx_used_idx;
  uint16 tx_used_idx;

  char rx_buf[NUM][sizeof(struct virtio_net_hdr) + VIRTIO_NET_MAX_PKT];
  struct virtio_net_hdr tx_hdr[NUM];

  char free[NUM];
  struct {
    void *data;
    int len;
  } tx_info[NUM];

  int init;
  int devid;
  struct spinlock lock;
} __attribute__ ((aligned (PGSIZE)));

static struct vnet vnet;

static void
setup_queue(int n, int qidx, char *pages, struct VRingDesc **desc, uint16 **avail, struct UsedArea **used)
{
  *R(n, VIRTIO_MMIO_QUEUE_SEL) = qidx;
  uint32 max = *R(n, VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0)
    panic("virtio net has no queue");
  if (max < NUM)
    panic("virtio net max queue too short");

  *R(n, VIRTIO_MMIO_QUEUE_NUM) = NUM;
  memset(pages, 0, 2 * PGSIZE);
  *R(n, VIRTIO_MMIO_QUEUE_PFN) = ((uint64)pages) >> PGSHIFT;

  *desc = (struct VRingDesc *)pages;
  *avail = (uint16 *)(((char *)(*desc)) + NUM * sizeof(struct VRingDesc));
  *used = (struct UsedArea *)(pages + PGSIZE);
}

static int
alloc_desc(void)
{
  for (int i = 0; i < NUM; i++) {
    if (vnet.free[i]) {
      vnet.free[i] = 0;
      return i;
    }
  }
  return -1;
}

static void
free_desc(int i)
{
  if (i >= NUM)
    panic("virtio_net free_desc");
  if (vnet.free[i])
    panic("virtio_net double free");
  vnet.tx_desc[i].addr = 0;
  vnet.free[i] = 1;
  wakeup(&vnet.free[0]);
}

static void
free_chain(int i)
{
  while (1) {
    int next = vnet.tx_desc[i].next;
    int has_next = vnet.tx_desc[i].flags & VRING_DESC_F_NEXT;
    free_desc(i);
    if (has_next)
      i = next;
    else
      break;
  }
}

static int
alloc2_desc(int *idx)
{
  for (int i = 0; i < 2; i++) {
    idx[i] = alloc_desc();
    if (idx[i] < 0) {
      for (int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

int
virtio_net_init(int n, uint8 mac[6])
{
  uint32 status = 0;

  __sync_synchronize();
  if (vnet.init)
    return 0;

  initlock(&vnet.lock, "virtio_net");
  vnet.devid = n;

  if (*R(n, VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
      *R(n, VIRTIO_MMIO_DEVICE_ID) != 1 ||
      *R(n, VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
    return -1;
  }

  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(n, VIRTIO_MMIO_STATUS) = status;

  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(n, VIRTIO_MMIO_STATUS) = status;

  uint64 features = *R(n, VIRTIO_MMIO_DEVICE_FEATURES);
  features &= (1 << VIRTIO_NET_F_MAC);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(n, VIRTIO_MMIO_DRIVER_FEATURES) = features;

  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(n, VIRTIO_MMIO_STATUS) = status;

  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(n, VIRTIO_MMIO_STATUS) = status;

  *R(n, VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;

  setup_queue(n, 0, vnet.rx_pages, &vnet.rx_desc, &vnet.rx_avail, &vnet.rx_used);
  setup_queue(n, 1, vnet.tx_pages, &vnet.tx_desc, &vnet.tx_avail, &vnet.tx_used);

  vnet.rx_used_idx = 0;
  vnet.tx_used_idx = 0;

  for (int i = 0; i < NUM; i++) {
    vnet.free[i] = 1;
    vnet.tx_info[i].data = 0;
    vnet.tx_info[i].len = 0;

    vnet.rx_desc[i].addr = (uint64)kvmpa((uint64)vnet.rx_buf[i]);
    vnet.rx_desc[i].len = sizeof(vnet.rx_buf[i]);
    vnet.rx_desc[i].flags = VRING_DESC_F_WRITE;
    vnet.rx_desc[i].next = 0;

    vnet.rx_avail[2 + (vnet.rx_avail[1] % NUM)] = i;
    vnet.rx_avail[1] = vnet.rx_avail[1] + 1;
  }

  __sync_synchronize();
  *R(n, VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  if (features & (1 << VIRTIO_NET_F_MAC)) {
    volatile uint8 *cfg = (volatile uint8 *)(VIRTION(n) + VIRTIO_MMIO_CONFIG);
    for (int i = 0; i < 6; i++)
      mac[i] = cfg[i];
  }

  vnet.init = 1;
  return 0;
}

int
virtio_net_transmit(const uint8 *data, int len)
{
  if (!vnet.init)
    return -1;
  if (len <= 0 || len > VIRTIO_NET_MAX_PKT)
    return -1;

  acquire(&vnet.lock);

  int idx[2];
  if (alloc2_desc(idx) < 0) {
    release(&vnet.lock);
    return -1;
  }

  memset(&vnet.tx_hdr[idx[0]], 0, sizeof(struct virtio_net_hdr));

  vnet.tx_desc[idx[0]].addr = (uint64)kvmpa((uint64)&vnet.tx_hdr[idx[0]]);
  vnet.tx_desc[idx[0]].len = sizeof(struct virtio_net_hdr);
  vnet.tx_desc[idx[0]].flags = VRING_DESC_F_NEXT;
  vnet.tx_desc[idx[0]].next = idx[1];

  vnet.tx_desc[idx[1]].addr = (uint64)kvmpa((uint64)data);
  vnet.tx_desc[idx[1]].len = len;
  vnet.tx_desc[idx[1]].flags = 0;
  vnet.tx_desc[idx[1]].next = 0;

  vnet.tx_info[idx[0]].data = (void *)data;
  vnet.tx_info[idx[0]].len = len;

  vnet.tx_avail[2 + (vnet.tx_avail[1] % NUM)] = idx[0];
  __sync_synchronize();
  vnet.tx_avail[1] = vnet.tx_avail[1] + 1;

  *R(vnet.devid, VIRTIO_MMIO_QUEUE_NOTIFY) = 1;

  release(&vnet.lock);
  return len;
}

static void
virtio_net_rx(void)
{
  while ((vnet.rx_used_idx % NUM) != (vnet.rx_used->id % NUM)) {
    int id = vnet.rx_used->elems[vnet.rx_used_idx].id;
    int len = vnet.rx_used->elems[vnet.rx_used_idx].len;

    if (len > (int)sizeof(struct virtio_net_hdr)) {
      uint8 *pkt = (uint8 *)vnet.rx_buf[id] + sizeof(struct virtio_net_hdr);
      int pkt_len = len - sizeof(struct virtio_net_hdr);
      net_rx_deliver(pkt, pkt_len);
    }

    vnet.rx_used_idx = (vnet.rx_used_idx + 1) % NUM;

    vnet.rx_avail[2 + (vnet.rx_avail[1] % NUM)] = id;
    __sync_synchronize();
    vnet.rx_avail[1] = vnet.rx_avail[1] + 1;
  }

  *R(vnet.devid, VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
}

static void
virtio_net_tx_complete(void)
{
  while ((vnet.tx_used_idx % NUM) != (vnet.tx_used->id % NUM)) {
    int id = vnet.tx_used->elems[vnet.tx_used_idx].id;

    if (vnet.tx_info[id].data) {
      kmfree(vnet.tx_info[id].data);
      vnet.tx_info[id].data = 0;
    }

    free_chain(id);
    vnet.tx_used_idx = (vnet.tx_used_idx + 1) % NUM;
  }
}

void
virtio_net_intr(void)
{
  if (!vnet.init)
    return;

  acquire(&vnet.lock);

  uint32 status = *R(vnet.devid, VIRTIO_MMIO_INTERRUPT_STATUS);
  if (status)
    *R(vnet.devid, VIRTIO_MMIO_INTERRUPT_ACK) = status;

  virtio_net_rx();
  virtio_net_tx_complete();

  release(&vnet.lock);
}
