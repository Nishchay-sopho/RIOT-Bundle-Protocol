/**
 * @ingroup     Bundle protocol
 * @{
 *
 * @file
 * @brief       Discovery manager for bundle protocol
 *
 * @author      Nishchay Agrawal <agrawal.nishchay5@gmail.com>
 *
 * @}
 */
#include "thread.h"
#include "kernel_types.h"
#include "utlist.h"

#include "net/gnrc/netif.h"
#include "net/gnrc/convergence_layer.h"
#include "net/gnrc/bundle_protocol/contact_manager.h"
#include "net/gnrc/bundle_protocol/bundle.h"
#include "net/gnrc/bundle_protocol/bundle_storage.h"
#include "net/gnrc/netif/hdr.h"
#include "net/gnrc.h"

#include <stdio.h>

#define ENABLE_DEBUG  (0)
#include "debug.h"

static kernel_pid_t _pid = KERNEL_PID_UNDEF;

#if ENABLE_DEBUG
static char _stack[GNRC_CONTACT_MANAGER_STACK_SIZE + THREAD_EXTRA_STACKSIZE_PRINTF];
#else
static char _stack[GNRC_CONTACT_MANAGER_STACK_SIZE];
#endif

static gnrc_pktsnip_t *_create_netif_hdr(uint8_t *dst_l2addr, unsigned dst_l2addr_len, gnrc_pktsnip_t *pkt, uint8_t flags);
static void _receive(struct actual_bundle *bundle);
static void _send(gnrc_pktsnip_t *pkt);
static void *_event_loop(void* args);
static int comparator (struct neighbor_t *neighbor, struct neighbor_t *compare_to_neighbor);
static void timer_expiry_callback (void *args);

struct neighbor_t *head_of_neighbors;

kernel_pid_t gnrc_contact_manager_init(void)
{
  if(_pid > KERNEL_PID_UNDEF){
    return _pid;
  }

  _pid = thread_create(_stack, sizeof(_stack), GNRC_CONTACT_MANAGER_PRIO, THREAD_CREATE_STACKTEST, _event_loop, NULL, "contact_manager");

  return _pid;
}

static gnrc_pktsnip_t *_create_netif_hdr(uint8_t *dst_l2addr, unsigned dst_l2addr_len, gnrc_pktsnip_t *pkt, uint8_t flags)
{
  gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, dst_l2addr, dst_l2addr_len);
  gnrc_netif_hdr_t *hdr;

  if (netif_hdr == NULL) {
      gnrc_pktbuf_release(pkt);
      return NULL;
  }
  hdr = netif_hdr->data;
  /* previous netif header might have been allocated by some higher layer
   * to provide some flags (provided to us via netif_flags). */
  hdr->flags = flags;

  /* add netif_hdr to front of the pkt list */
  LL_PREPEND(pkt, netif_hdr);

  return pkt;
}

static void _receive(struct actual_bundle *bundle)
{
  struct bundle_canonical_block_t *payload_block = bundle_get_payload_block(bundle);

  if (payload_block == NULL) {
    DEBUG("contact_manager: Cannot extract payload block from received packet.\n");
    return ;
  }
  update_statistics(DISCOVERY_BUNDLE_RECEIVE);
  struct neighbor_t *neighbor = (struct neighbor_t*)malloc(sizeof(struct neighbor_t));

  if (neighbor == NULL) {
    DEBUG("contact_manager: Could not allocate memory for new neighbor.\n");
    return ;
  }

  neighbor->endpoint_scheme = bundle->primary_block.endpoint_scheme;
  if (neighbor->endpoint_scheme == IPN) {
    neighbor->endpoint_num = bundle->primary_block.src_num;
  }
  else if (neighbor->endpoint_scheme == DTN) {
    neighbor->eid = bundle->primary_block.src_eid;
  }

  memcpy(neighbor->l2addr, payload_block->block_data, payload_block->data_len);
  neighbor->l2addr_len = payload_block->data_len;
  
  create_neighbor_expiry_timer(neighbor);
  xtimer_set(&neighbor->expiry_timer, xtimer_ticks_from_usec(NEIGHBOR_PURGE_TIMER_SECONDS*SECS_TO_MICROSECS).ticks32);

  /* Adding neighbor in front of neighbor list if not present in list*/
  struct neighbor_t *temp;
  LL_SEARCH(head_of_neighbors, temp, neighbor, comparator);
  if(!temp) {
    DEBUG("contact_manager: Adding neighbor which will expire in %d.\n", NEIGHBOR_PURGE_TIMER_SECONDS);
    LL_APPEND(head_of_neighbors, neighbor);
    
#ifdef MODULE_ROUTING_EPIDEMIC
    send_bundles_to_new_neighbor(neighbor);
#endif
  }
  else {
    xtimer_remove(&temp->expiry_timer);
    xtimer_set(&temp->expiry_timer, xtimer_ticks_from_usec(NEIGHBOR_PURGE_TIMER_SECONDS*SECS_TO_MICROSECS).ticks32);
  }
  set_retention_constraint(bundle, NO_RETENTION_CONSTRAINT);
  delete_bundle(bundle);
  return ;
}

static void _send(gnrc_pktsnip_t *pkt)
{
  gnrc_netif_t *netif = NULL;
  gnrc_pktsnip_t *tmp_pkt;
  uint8_t netif_hdr_flags = 0U;
  int iface = 0;

  if(pkt->type == GNRC_NETTYPE_NETIF) {
    const gnrc_netif_hdr_t *netif_hdr = pkt->data;

    netif = gnrc_netif_hdr_get_netif(pkt->data);
    /* discard broadcast and multicast flags because those could be
     * potentially wrong (dst is later checked to assure that multicast is
     * set if dst is a multicast address) */
    netif_hdr_flags = netif_hdr->flags &
                      ~(GNRC_NETIF_HDR_FLAGS_BROADCAST |
                        GNRC_NETIF_HDR_FLAGS_MULTICAST);

    tmp_pkt = gnrc_pktbuf_start_write(pkt);
    if (tmp_pkt == NULL) {
        DEBUG("contact_manager: unable to get write access to netif header, dropping packet\n");
        gnrc_pktbuf_release(pkt);
        return;
    }
    pkt = gnrc_pktbuf_remove_snip(tmp_pkt, tmp_pkt);
  }
  /* TODO: Add broadcast destination l2adrr and addr length here instead of NULL and 0.
   * This happens automatically inside gnrc_netif.c, so not needed
   */
  if ((pkt = _create_netif_hdr(NULL, 0, pkt, netif_hdr_flags | GNRC_NETIF_HDR_FLAGS_BROADCAST)) == NULL) {
    return ;
  }
  iface = netif->pid;

  if(iface != 0) {
    /*Setting netif to old value */
    gnrc_netif_hdr_set_netif(pkt->data, netif);
    update_statistics(DISCOVERY_BUNDLE_SEND);
    gnrc_netapi_dispatch_send(GNRC_NETTYPE_BP, GNRC_NETREG_DEMUX_CTX_ALL, pkt);
  }
}

static void *_event_loop(void *args)
{
  msg_t msg, msg_q[GNRC_CONTACT_MANAGER_MSG_QUEUE_SIZE];

  gnrc_netreg_entry_t me_reg = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, sched_active_pid);
  (void)args;

  msg_init_queue(msg_q, GNRC_CONTACT_MANAGER_MSG_QUEUE_SIZE);
  
  gnrc_netreg_register(GNRC_NETTYPE_CONTACT_MANAGER, &me_reg);
  while(1){
    DEBUG("contact_manager: waiting for incoming message.\n");
    msg_receive(&msg);
    switch(msg.type){
      case GNRC_NETAPI_MSG_TYPE_SND:
          DEBUG("contact_manager: GNRC_NETDEV_MSG_TYPE_SND received\n");
          _send(msg.content.ptr);
          break;
      case GNRC_NETAPI_MSG_TYPE_RCV:
          DEBUG("contact_manager: GNRC_NETDEV_MSG_TYPE_RCV received\n");
          _receive(msg.content.ptr);
          break;
      default:
        DEBUG("contact_manager: Successfully entered contact manager, yayyyyyy!!\n");
        break;
    }
  }
  return NULL;
}

static int comparator (struct neighbor_t *neighbor, struct neighbor_t *compare_to_neighbor)
{
  int res = -1;
  if (neighbor->endpoint_scheme == compare_to_neighbor->endpoint_scheme) {
    if (neighbor->endpoint_scheme == IPN) {
      if(neighbor->endpoint_num == compare_to_neighbor->endpoint_num) {
        if(neighbor-> l2addr_len == compare_to_neighbor->l2addr_len && memcmp(neighbor->l2addr, compare_to_neighbor->l2addr, neighbor->l2addr_len) == 0){
          return 0;
        }
      }
    }
    else if (neighbor->endpoint_scheme == DTN ) {
      if(strcmp((char*)neighbor->eid, (char*)compare_to_neighbor->eid) == 0){
        if(neighbor-> l2addr_len == compare_to_neighbor->l2addr_len && memcmp(neighbor->l2addr, compare_to_neighbor->l2addr, neighbor->l2addr_len) == 0){
          return 0;
        }
      }
    }
  }
  return res;
}

struct neighbor_t *get_neighbor_from_endpoint_num(uint32_t endpoint_num) {
  struct neighbor_t * temp = NULL;
  LL_SEARCH_SCALAR(head_of_neighbors, temp, endpoint_num, endpoint_num);
  return temp;
}

struct neighbor_t *get_neighbor_from_l2addr(uint8_t *addr) {
  bool found = false;
  struct neighbor_t *temp = NULL;
  LL_FOREACH(head_of_neighbors, temp) {
    if(memcmp(temp->l2addr, addr, temp->l2addr_len) == 0) {
      found = true;
      break;
    }
  }
  if (found){
    return temp;
  }
  else {
    return NULL;
  }
}

struct neighbor_t *get_neighbor_list(void) {
  return head_of_neighbors;
}

void create_neighbor_expiry_timer(struct neighbor_t *neighbor) {
  neighbor->expiry_timer.callback = &timer_expiry_callback;
  neighbor->expiry_timer.arg = neighbor;
  neighbor->expiry_timer.next = NULL;
}

static void timer_expiry_callback (void *args) {
  ((struct neighbor_t*)args)->endpoint_num = 0;
  ((struct neighbor_t*)args)->l2addr_len = 0;
  LL_DELETE(head_of_neighbors, ((struct neighbor_t*)args));
}

bool is_same_neighbor(struct neighbor_t *neighbor, struct neighbor_t *compare_to_neighbor) {
  if (neighbor->endpoint_scheme == IPN && compare_to_neighbor->endpoint_scheme == IPN) { 
    if (neighbor->endpoint_num == compare_to_neighbor->endpoint_num) {
      if (neighbor-> l2addr_len == compare_to_neighbor->l2addr_len && memcmp(neighbor->l2addr, compare_to_neighbor->l2addr, neighbor->l2addr_len) == 0) {
        return true;
      }
    }
  }
  return false;
}