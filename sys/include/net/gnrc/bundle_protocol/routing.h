#ifndef _ROUTING_BP_H
#define _ROUTING_BP_H

#include <stdint.h>

#include "net/gnrc/bundle_protocol/contact_manager.h"

struct router{
//	int result;
	struct neighbor_t* (*route_receivers) (uint32_t dst_num);
};

extern struct router *this_router;

static inline struct router *get_router(void) {
	return this_router;
}

#endif