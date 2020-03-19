#ifndef _BUNDLE_BP_H
#define _BUNDLE_BP_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>

#include "nanocbor/nanocbor.h"
#include "fmt.h"

#define DUMMY_EID "test"
#define DUMMY_SRC_NUM "40"
#define BROADCAST_EID "11111111"

#define CONTACT_MANAGER_SERVICE_NUM "12"

#define ERROR -1

//Codes to segregate between primary and canonical block
#define BUNDLE_BLOCK_TYPE_PRIMARY 0x88
#define BUNDLE_BLOCK_TYPE_CANONICAL 0x89

//Bundle type codes
#define BUNDLE_BLOCK_TYPE_PAYLOAD 0x01
#define BUNDLE_BLOCK_TYPE_PREVIOUS_NODE 0x07
#define BUNDLE_BLOCK_TYPE_HOP_COUNT 0x09
#define BUNDLE_BLOCK_TYPE_BUNDLE_AGE 0x08

// CRC type codes
#define NOCRC 0x00
#define CRC_16 0x01
#define CRC_32 0x02

#define FRAGMENT_IDENTIFICATION_MASK 0x0000000000000001

#define BLOCK_DATA_BUF_SIZE 100

#define MAX_NUM_OF_BLOCKS 3
#define MAX_ENDPOINT_SIZE 32

//Primary block defines
enum primary_block_elements{
  VERSION,
  FLAGS_PRIMARY,
  ENDPOINT_SCHEME,
  CRC_TYPE_PRIMARY,
  EID,
  SRC_EID,
  DST_EID,
  REPORT_EID,
  SRC_NUM,
  DST_NUM,
  REPORT_NUM,
  SERVICE_NUM,
  CREATION_TIMESTAMP,
  LIFETIME,
  FRAGMENT_OFFSET,
  TOTAL_APPLICATION_DATA_LENGTH,
  CRC_PRIMARY
};
//Canonical block defines
enum canonical_block_elements{
  TYPE,
  BLOCK_NUMBER,
  FLAGS_CANONICAL,
  CRC_TYPE_CANONICAL,
  BLOCK_DATA,
  CRC_CANONICAL
};

enum endpoint_scheme{
  DTN,
  IPN
};
//proposed structure for endpoint if each endpoint can follow different endpoint scheme
struct endpoint{
  uint8_t endpoint_scheme;
  char* eid;
};


struct bundle_primary_block_t{ // This is the order in which the elements of the block are encoded
  uint8_t version;
  uint64_t flags;
  uint8_t endpoint_scheme; // TODO: Assuming the dest and src nodes will have same endpoint scheme
  uint8_t crc_type;
  uint8_t* dest_eid;
  uint8_t* src_eid;
  uint8_t* report_eid;
  uint32_t dst_num;
  uint32_t src_num;
  uint32_t report_num;
  uint32_t service_num;
  uint32_t creation_timestamp[2];
  uint8_t lifetime;
  uint32_t fragment_offset;
  uint32_t total_application_data_length;
  uint32_t crc;
};

struct bundle_canonical_block_t{
  uint8_t type;
  uint8_t block_number;
  uint64_t flags;
  uint8_t crc_type;
  uint8_t block_data[BLOCK_DATA_BUF_SIZE];
  uint32_t crc;
  size_t data_len;
  struct bundle_canonical_block_t* next;
};

struct actual_bundle{
  struct bundle_primary_block_t primary_block;
  struct bundle_canonical_block_t other_blocks[MAX_NUM_OF_BLOCKS];
  int num_of_blocks;
};

// void insert_block_in_bundle(struct actual_bundle* bundle, struct bundle_canonical_block_t* block);
bool is_same_bundle(struct actual_bundle* current_bundle, struct actual_bundle* compare_to_bundle);
uint16_t calculate_crc_16(uint8_t type);
uint32_t calculate_crc_32(uint8_t type);
void calculate_primary_flag(uint64_t *flag, bool is_fragment, bool dont_fragment);
int calculate_payload_flag(uint64_t *flag, bool replicate_block);

struct actual_bundle* create_bundle(void);
void fill_bundle(struct actual_bundle* bundle, int version, uint8_t endpoint_scheme, char* dest_eid, char* report_eid, int lifetime, int crc_type, char* service_num);
int bundle_encode(struct actual_bundle* bundle, nanocbor_encoder_t *enc);
int bundle_decode(struct actual_bundle* bundle, uint8_t *buffer, size_t buf_len);

struct bundle_primary_block_t* bundle_get_primary_block(struct actual_bundle* bundle);
struct bundle_canonical_block_t* bundle_get_payload_block(struct actual_bundle* bundle);
struct bundle_canonical_block_t* get_block_by_type(struct actual_bundle* bundle, uint8_t block_type);

//main api to be used to add blocks to bundle
int bundle_add_block(struct actual_bundle* bundle, uint8_t type, uint64_t flags, uint8_t *data, uint8_t crc_type, size_t data_len);
uint8_t bundle_get_attribute(struct actual_bundle* bundle, uint8_t type, void* val);
uint8_t bundle_set_attribute(struct actual_bundle* bundle, uint8_t type, void* val);

void print_bundle(struct actual_bundle* bundle);

char *get_src_eid(void);
char *get_src_num(void);
bool check_if_fragment_bundle(void);
bool check_if_node_has_clock(void);

#endif