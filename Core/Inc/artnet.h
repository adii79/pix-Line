#ifndef INC_ARTNET_H_
#define INC_ARTNET_H_

#include "lwip/opt.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

void artnet_init(void);
void artnet_stop(void);
void artnet_receive(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                    const ip_addr_t *addr, u16_t port);

#endif
