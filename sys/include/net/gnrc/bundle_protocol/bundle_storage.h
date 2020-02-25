#ifndef _BUNDLE_STORAGE_BP_H
#define _BUNDLE_STORAGE_BP_H

#include <stdlib.h>
#include <stdint.h>

#include "net/gnrc/bundle_protocol/bundle.h"

//Implemented bundle storage as a linkedlist of bundles
struct bundle_list{
  struct actual_bundle current_bundle;
  struct bundle_list* next;
  uint8_t num_of_bundles;
  // uint8_t next_block_number;
  // uint8_t next_empty_index;
};


#define MAX_BUNDLES 5


struct bundle_list* bundle_storage_init(void);
struct actual_bundle* get_space_for_bundle(void);
bool delete_bundle(struct actual_bundle* bundle);
uint8_t get_next_block_number(void);
struct bundle_list* find_bundle_in_list(struct actual_bundle* bundle);

#endif
