#ifndef XV6_NET_H
#define XV6_NET_H

#include "types.h"

void net_init(void);
void net_rx_deliver(const uint8 *data, int len);

#endif
