#ifndef _BUUNDLE_ROUTING_EPIDEMIC_H
#define _BUUNDLE_ROUTING_EPIDEMIC_H

#include <stdlib.h>
#include "net/gnrc/bundle_protocol/routing.h"

void routing_epidemic_init(void);
struct neighbor_t *route_receivers(uint32_t dst_num);
void notify_bundle_deletion (struct actual_bundle *bundle);
void received_ack(struct neighbor_t *src_neighbor, uint32_t creation_timestamp0, uint32_t creation_timestamp1, uint32_t src_num);
void print_delivered_bundle_list (void);
struct delivered_bundle_list *get_delivered_bundle_list(void);

#endif
