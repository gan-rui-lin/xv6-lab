#ifndef XV6_VIRTIO_NET_H
#define XV6_VIRTIO_NET_H

#include "types.h"

int virtio_net_init(int n, uint8 mac[6]);
int virtio_net_transmit(const uint8 *data, int len);
void virtio_net_intr(void);

#endif
