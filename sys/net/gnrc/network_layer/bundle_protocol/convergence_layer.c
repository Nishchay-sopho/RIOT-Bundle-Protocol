/**
 * @ingroup     Bundle protocol
 * @{
 *
 * @file
 * @brief       Primitive convergence layer for bundle protocol
 *
 * @author      Nishchay Agrawal <agrawal.nishchay5@gmail.com>
 *
 * @}
 */
#include <math.h>
#include "kernel_types.h"
#include "thread.h"
#include "utlist.h"

#include "net/gnrc/netif.h"
#include "net/gnrc/convergence_layer.h"
#include "net/gnrc.h"
#include "net/gnrc/pktbuf.h"
#include "net/gnrc/bundle_protocol/config.h"
#include "net/gnrc/bundle_protocol/bundle.h"
#include "net/gnrc/bundle_protocol/bundle_storage.h"
#include "net/gnrc/bundle_protocol/routing.h"

#define ENABLE_DEBUG    (0)
#include "debug.h"

#include "od.h"

static kernel_pid_t _pid = KERNEL_PID_UNDEF;

int iface = 0;


#if ENABLE_DEBUG
static char _stack[GNRC_BP_STACK_SIZE +THREAD_EXTRA_STACKSIZE_PRINTF];
#else
static char _stack[GNRC_BP_STACK_SIZE];
#endif

static void _receive(gnrc_pktsnip_t *pkt);
static void _send(struct actual_bundle *bundle);
static void _send_packet(gnrc_pktsnip_t *pkt);
static void *_event_loop(void *args);
static void retransmit_timer_callback(void *args);
static int calculate_size_of_num(uint32_t num);
static void net_stats_callback(void *args);

kernel_pid_t gnrc_bp_init(void)
{
  if(_pid > KERNEL_PID_UNDEF){
      return _pid;
  }

  _pid = thread_create(_stack, sizeof(_stack), GNRC_BP_PRIO,
                        THREAD_CREATE_STACKTEST, _event_loop, NULL, "convergence_layer");

  DEBUG("convergence_layer: thread created with pid: %d\n",_pid);
  return _pid;
}

kernel_pid_t gnrc_bp_get_pid(void)
{
    return _pid;
}

int gnrc_bp_dispatch(gnrc_nettype_t type, uint32_t demux_ctx, struct actual_bundle *bundle, uint16_t cmd)
{
  int numof = gnrc_netreg_num(type, demux_ctx);
  if (numof != 0){
    gnrc_netreg_entry_t *sendto = gnrc_netreg_lookup(type, demux_ctx);
    msg_t msg;
    /* set the outgoing message's fields */
    msg.type = cmd;
    msg.content.ptr = (void *)bundle;
    /* send message */
    int ret = msg_try_send(&msg, sendto->target.pid);
    if (ret < 1) {
        DEBUG("convergence_layer: dropped message to %" PRIkernel_pid " (%s)\n", sendto->target.pid,
              (ret == 0) ? "receiver queue is full" : "invalid receiver");
    }
    return ret;
  }
  return ERROR;
}

void deliver_bundle(void *ptr, struct registration_status *application) {
  update_statistics(BUNDLE_DELIVERY);
  msg_t msg;
  msg.content.ptr = ptr;
  msg_try_send(&msg, application->pid);
}

bool check_lifetime_expiry(struct actual_bundle *bundle) {
  struct bundle_canonical_block_t *bundle_age_block = get_block_by_type(bundle, BUNDLE_BLOCK_TYPE_BUNDLE_AGE);

  if (bundle_age_block != NULL) {
    if (bundle->primary_block.lifetime < strtoul((char*)bundle_age_block->block_data, NULL, bundle_age_block->data_len)) {
      set_retention_constraint(bundle, NO_RETENTION_CONSTRAINT);
      delete_bundle(bundle);
      return true;
    }
    else {
      return false;
    }
  }
  else {
    return false;
  }
}

int process_bundle_before_forwarding(struct actual_bundle *bundle) {

  /*Processing bundle and updating its bundle age block*/
  struct bundle_canonical_block_t *bundle_age_block = get_block_by_type(bundle, BUNDLE_BLOCK_TYPE_BUNDLE_AGE);

  if (bundle_age_block != NULL) {
    if (increment_bundle_age(bundle_age_block, bundle) < 0) {
      DEBUG("convergence_layer: Error updating bundle age block.\n");
      return ERROR;
    }
  }
  return OK;
}

bool is_packet_ack(gnrc_pktsnip_t *pkt) {
  char temp[ACK_IDENTIFIER_SIZE];
  strncpy(temp, pkt->data, ACK_IDENTIFIER_SIZE);
  if (strstr(temp, "ack") != NULL) {
    return true;
  }
  return false;
}

static void _receive(gnrc_pktsnip_t *pkt) 
{
  struct router *cur_router;

  cur_router = get_router();

  if(pkt->data == NULL) {
    DEBUG("convergence_layer: No data in packet, dropping it.\n");
    gnrc_pktbuf_release(pkt);
    return ;
  }

  if (is_packet_ack(pkt)) {
    update_statistics(ACK_RECEIVE);
    uint8_t *temp_addr;
    int src_addr_len;
    char *creation_timestamp0, *creation_timestamp1;
    uint32_t src_num;

    src_addr_len = gnrc_netif_hdr_get_srcaddr(pkt, &temp_addr);
    uint8_t src_addr[src_addr_len];
    strncpy((char*)src_addr, (char*)temp_addr, src_addr_len);

    struct neighbor_t *neighbor = get_neighbor_from_l2addr(src_addr);

    if (neighbor == NULL) {
      DEBUG("convergence_layer: Could not find neighbor from whom data is received.\n");
      return ;
    }

    strtok(pkt->data, "_");
    creation_timestamp0 = strtok(NULL, "_");
    creation_timestamp1 = strtok(NULL, "_");
    src_num = strtoul(strtok(NULL, "_"), NULL, 10);
    
    cur_router->received_ack(neighbor, atoi(creation_timestamp0), atoi(creation_timestamp1), src_num);

    gnrc_pktbuf_release(pkt);
    
  }
  else {
    update_statistics(BUNDLE_RECEIVE);
    struct actual_bundle *bundle = create_bundle();
    if (bundle == NULL) {
      DEBUG("convergence_layer: Could not allocate space for this new bundle.\n");
      gnrc_pktbuf_release(pkt);
      return ;
    }
    int res = bundle_decode(bundle, pkt->data, pkt->size);
    if (res == ERROR) {
      DEBUG("convergence_layer: Packet received not for bundle protocol.\n");
      gnrc_pktbuf_release(pkt);
      delete_bundle(bundle);
      return ;
    }
    else if (res == BUNDLE_TOO_LARGE_ERROR) {
      DEBUG("convergence_layer: Bundle too large for bundle protocol.\n");
      gnrc_pktbuf_release(pkt);
      delete_bundle(bundle);
      return ;
    }
    if (check_lifetime_expiry(bundle)) {
      DEBUG("convergence_layer: received bundle's lifetime expired and has been deleted from storage.\n");
      gnrc_pktbuf_release(pkt);
      return ;
    }

    if (is_redundant_bundle(bundle) || verify_bundle_processed(bundle)) {
      DEBUG("convergence_layer: Received this bundle before, discarding bundle");
      if (bundle->primary_block.service_num  != (uint32_t)atoi(CONTACT_MANAGER_SERVICE_NUM)){
        send_non_bundle_ack(bundle, pkt);
      }
      gnrc_pktbuf_release(pkt);
      set_retention_constraint(bundle, NO_RETENTION_CONSTRAINT);
      delete_bundle(bundle);
      return ;
    }

#ifdef MODULE_GNRC_CONTACT_MANAGER
    if (bundle->primary_block.service_num  == (uint32_t)atoi(CONTACT_MANAGER_SERVICE_NUM)) {
      if (!gnrc_bp_dispatch(GNRC_NETTYPE_CONTACT_MANAGER, GNRC_NETREG_DEMUX_CTX_ALL, bundle, GNRC_NETAPI_MSG_TYPE_RCV)) {
        DEBUG("convergence_layer: no contact_manager thread found\n");
        set_retention_constraint(bundle, NO_RETENTION_CONSTRAINT);
        delete_bundle(bundle);
      }
      else {
        update_statistics(DISCOVERY_BUNDLE_RECEIVE);
      }
      gnrc_pktbuf_release(pkt);
    }
#endif
    else {

      uint8_t *temp_addr;
      int src_addr_len;
      src_addr_len = gnrc_netif_hdr_get_srcaddr(pkt, &temp_addr);

      uint8_t src_addr[src_addr_len];
      strncpy((char*)src_addr, (char*)temp_addr, src_addr_len);
      struct neighbor_t *previous_neighbor = get_neighbor_from_l2addr(src_addr);

      if (previous_neighbor == NULL) {
        DEBUG("convergence_layer: Could not find previous neighbor for this received bundle.\n");
        bundle->previous_endpoint_num = 0;
      }
      else {
        /*
          Storing this information so that it can be used as previuos node information while retransmitting
        */
        bundle->previous_endpoint_num = previous_neighbor->endpoint_num;
      }

      /*Sending acknowledgement for received bundle*/
      send_non_bundle_ack(bundle, pkt);

      gnrc_pktbuf_release(pkt);

      /* This bundle is for the current node, send to application that sent it*/
      if (bundle->primary_block.dst_num == (uint32_t)atoi(get_src_num())) {
        set_retention_constraint(bundle, SEND_ACK_PENDING_RETENTION_CONSTRAINT);
        bool delivered = true;
        struct registration_status *application = get_registration(bundle->primary_block.service_num);
        if (application->status == REGISTRATION_ACTIVE) {
          deliver_bundle((void *)(bundle_get_payload_block(bundle)->block_data), application);
          delivered = true;
        }
        else {
          DEBUG("convergence_layer: Couldn't deliver bundle to application.\n");
          delivered = false;
        }
        set_retention_constraint(bundle, NO_RETENTION_CONSTRAINT);
        if (delivered) {
          DEBUG("convergence_layer: Bundle delivered to application layer, deleting from here.\n");
          add_bundle_to_processed_bundle_list(bundle);
          delete_bundle(bundle);
        }
      } /*Bundle not for this node, forward received bundle*/
      else {
        struct router *cur_router = get_router();
        nanocbor_encoder_t enc;
        gnrc_netif_t *netif = NULL;
        struct neighbor_t *temp;
        bool sent = false;

        set_retention_constraint(bundle, FORWARD_PENDING_RETENTION_CONSTRAINT);

        netif = gnrc_netif_get_by_pid(iface);

        struct neighbor_t *neighbors_to_send = cur_router->route_receivers(bundle->primary_block.dst_num);
        if (neighbors_to_send == NULL) {
          DEBUG("convergence_layer: Could not find neighbors to send bundle to.\n");
          return ;
        }

        if(process_bundle_before_forwarding(bundle) < 0) {
          return ;
        }
        nanocbor_encoder_init(&enc, NULL, 0);
        bundle_encode(bundle, &enc);
        size_t required_size = nanocbor_encoded_len(&enc);
        uint8_t *buf = malloc(required_size);
        nanocbor_encoder_init(&enc, buf, required_size);
        bundle_encode(bundle, &enc);

        gnrc_pktsnip_t *forward_pkt = gnrc_pktbuf_add(NULL, buf, (int)required_size, GNRC_NETTYPE_BP);
        if (forward_pkt == NULL) {
          DEBUG("convergence_layer: unable to copy data to packet buffer.\n");
          gnrc_pktbuf_release(forward_pkt);
          free(buf);
          return ;
        }

        /*
          Handling not sending to previous node here since the solution would require more malloc
          and space is problem on these low power nodes
        */
        LL_FOREACH(neighbors_to_send, temp) {
          if (temp->endpoint_scheme == IPN && temp->endpoint_num != bundle->previous_endpoint_num && memcmp(temp->l2addr, previous_neighbor->l2addr, temp->l2addr_len) != 0) {
            sent = true;
            if (netif != NULL) {
              gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, temp->l2addr, temp->l2addr_len);
              gnrc_netif_hdr_set_netif(netif_hdr->data, netif);
              LL_PREPEND(forward_pkt, netif_hdr);
            }
            if (netif->pid != 0) {
              gnrc_netapi_send(netif->pid, forward_pkt);
            }
          }
        }
        set_retention_constraint(bundle, NO_RETENTION_CONSTRAINT);
        if(!sent) {
          DEBUG("convergence_layer: bundle not sent, deleting from encoded buffer.\n");
          free(buf);
        }
      }
    }
  }
  return ;
}

static void _send(struct actual_bundle *bundle)
{
  uint8_t registration_status = get_registration_status(bundle->primary_block.service_num);
  if (registration_status == REGISTRATION_ACTIVE) {
    set_retention_constraint(bundle, DISPATCH_PENDING_RETENTION_CONSTRAINT);
    struct router *cur_router = get_router();
    struct neighbor_t *temp;
    struct bundle_canonical_block_t *bundle_age_block;
    struct neighbor_t *neighbor_list_to_send;
    uint32_t original_bundle_age = 0;

    gnrc_netif_t *netif = NULL;
    nanocbor_encoder_t enc;

    netif = gnrc_netif_get_by_pid(iface);

    neighbor_list_to_send = cur_router->route_receivers(bundle->primary_block.dst_num);
    if (neighbor_list_to_send == NULL) {
      DEBUG("convergence_layer: Could not find neighbors to send bundle to.\n");
      return ;
    }

    bundle_age_block = get_block_by_type(bundle, BUNDLE_BLOCK_TYPE_BUNDLE_AGE);
    if(bundle_age_block != NULL) {
      original_bundle_age = atoi((char*)bundle_age_block->block_data);
      if(increment_bundle_age(bundle_age_block, bundle) < 0) {
        DEBUG("convergence_layer: Bundle expired.\n");
        set_retention_constraint(bundle, NO_RETENTION_CONSTRAINT);
        delete_bundle(bundle);
        return;
      }
    }
    nanocbor_encoder_init(&enc, NULL, 0);
    bundle_encode(bundle, &enc);
    size_t required_size = nanocbor_encoded_len(&enc);
    uint8_t *buf = malloc(required_size);
    nanocbor_encoder_init(&enc, buf, required_size);
    bundle_encode(bundle, &enc);

    gnrc_pktsnip_t *pkt = gnrc_pktbuf_add(NULL, buf, (int)required_size, GNRC_NETTYPE_BP);
    if (pkt == NULL) {
      DEBUG("convergence_layer: unable to copy data to discovery packet buffer.\n");
      gnrc_pktbuf_release(pkt);
      free(buf);
      return ;
    }

    LL_FOREACH(neighbor_list_to_send, temp) {
      struct delivered_bundle_list *ack_list, *temp_ack_list;
      ack_list = cur_router->get_delivered_bundle_list();
      /*
        Sending bundle for the first time from this node
      */
      if (ack_list == NULL && temp->endpoint_scheme == IPN && temp->endpoint_num != bundle->previous_endpoint_num) {
        if (netif != NULL) {
          gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, temp->l2addr, temp->l2addr_len);
          gnrc_netif_hdr_set_netif(netif_hdr->data, netif);
          LL_PREPEND(pkt, netif_hdr);
        }
        if (netif->pid != 0) {
          gnrc_netapi_send(netif->pid, pkt);
          update_statistics(BUNDLE_SEND);
        }
      } 
      else {
        bool found = false;
        LL_FOREACH(ack_list, temp_ack_list) {
          if ((is_same_bundle(bundle, temp_ack_list->bundle) && is_same_neighbor(temp, temp_ack_list->neighbor))) {
            DEBUG("convergence_layer: Already delivered bundle with creation time %lu to %lu, breaking out of loop of ack_list.\n", bundle->local_creation_time, temp->endpoint_num);
            found = true;
            break;
          }
        }
        if (!found && temp->endpoint_scheme == IPN && temp->endpoint_num != bundle->previous_endpoint_num) {
          if (netif != NULL) {
            gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, temp->l2addr, temp->l2addr_len);
            gnrc_netif_hdr_set_netif(netif_hdr->data, netif);
            LL_PREPEND(pkt, netif_hdr);
          }
          if (netif->pid != 0) {
            gnrc_netapi_send(netif->pid, pkt);
            update_statistics(BUNDLE_SEND);
          }
        }
      }
    }
    if (reset_bundle_age(bundle_age_block, original_bundle_age) < 0) {
      DEBUG("convergence_layer: Error resetting bundle age to original.\n");
    }
    set_retention_constraint(bundle, NO_RETENTION_CONSTRAINT);
    return ;
  }
  else if (registration_status == REGISTRATION_PASSIVE){
    DEBUG("convergence_layer: Application not active to send bundles.\n");
    return ;
  }
  else {
    DEBUG("convergence_layer: Application not registered .\n");
    return;
  }
}

static void _send_packet(gnrc_pktsnip_t *pkt)
{
  gnrc_netif_t *netif = NULL;
  netif = gnrc_netif_hdr_get_netif(pkt->data);

  if (netif->pid != 0) {
    gnrc_netapi_send(netif->pid, pkt);
    update_statistics(BUNDLE_SEND);
    update_statistics(DISCOVERY_BUNDLE_SEND);
  }
}

static void *_event_loop(void *args)
{
  msg_t msg, msg_q[GNRC_BP_MSG_QUEUE_SIZE];
  xtimer_t *timer = malloc(sizeof(xtimer_t));

  gnrc_netreg_entry_t me_reg = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL, sched_active_pid);
  (void)args;

  msg_init_queue(msg_q, GNRC_BP_MSG_QUEUE_SIZE);

  gnrc_netreg_register(GNRC_NETTYPE_BP, &me_reg);

  timer->callback = &retransmit_timer_callback;
  timer->arg = timer;
  xtimer_set(timer, xtimer_ticks_from_usec(RETRANSMIT_TIMER_SECONDS).ticks32);

  xtimer_t *net_stats_timer = malloc(sizeof(xtimer_t));
  net_stats_timer->callback = &net_stats_callback;
  net_stats_timer->arg = net_stats_timer;
  xtimer_set(net_stats_timer, xtimer_ticks_from_usec(NET_STATS_SECONDS).ticks32);

  while(1){
    DEBUG("convergence_layer: waiting for incoming message.\n");
    msg_receive(&msg);
    switch(msg.type){
      case GNRC_NETAPI_MSG_TYPE_SND:
          DEBUG("convergence_layer: GNRC_NETDEV_MSG_TYPE_SND received\n");
          if(strcmp(thread_get(msg.sender_pid)->name, "contact_manager") == 0) {
            _send_packet(msg.content.ptr);
            break;
          }
          _send(msg.content.ptr);
          break;
      case GNRC_NETAPI_MSG_TYPE_RCV:
          DEBUG("convergence_layer: GNRC_NETDEV_MSG_TYPE_RCV received\n");
          _receive(msg.content.ptr);
          break;
      default:
        DEBUG("convergence_layer: Successfully entered bp, yayyyyyy!!\n");
        break;
    }
  }
  return NULL;
}


static void net_stats_callback(void *args) {
  print_network_statistics();
  xtimer_set(args, xtimer_ticks_from_usec(NET_STATS_SECONDS).ticks32);
}

static void retransmit_timer_callback(void *args) {
  struct bundle_list *bundle_storage_list = get_bundle_list(), *temp;
  uint8_t active_bundles = get_current_active_bundles(), i = 0;
  temp = bundle_storage_list;
  while (temp != NULL && i < active_bundles && get_retention_constraint(&temp->current_bundle) == NO_RETENTION_CONSTRAINT 
          && temp->current_bundle.primary_block.dst_num != strtoul(get_src_num(), NULL, 10) 
          && temp->current_bundle.primary_block.service_num != strtoul(CONTACT_MANAGER_SERVICE_NUM, NULL, 10) ) {

    if(!gnrc_bp_dispatch(GNRC_NETTYPE_BP, GNRC_NETREG_DEMUX_CTX_ALL, &temp->current_bundle, GNRC_NETAPI_MSG_TYPE_SND)) {
      printf("convergence_layer: Unable to find BP thread.\n");
      return ;
    } 
    /*update statistics if bundle successfully dispatched for retransmission to BP thread
     *This is although ignoring the case where the bundle transmission is stopped due to lifetime expiry
     *or any other reason
     */
    else {
      update_statistics(BUNDLE_RETRANSMIT);
    }
    temp = temp->next;
    i++;
  }
  xtimer_set(args, xtimer_ticks_from_usec(RETRANSMIT_TIMER_SECONDS).ticks32);
}

void send_bundles_to_new_neighbor(struct neighbor_t *neighbor) {
    struct bundle_list *bundle_store_list, *temp_bundle;
    struct delivered_bundle_list *ack_list, *temp_ack_list;
    uint8_t active_bundles = get_current_active_bundles(), i = 0;

    ack_list = get_router()->get_delivered_bundle_list();

    bundle_store_list = get_bundle_list();
    temp_bundle = bundle_store_list;
    while(temp_bundle != NULL && i < active_bundles) {
      if(temp_bundle->current_bundle.primary_block.dst_num != (uint32_t)atoi(BROADCAST_EID)) {

        /*
          Checking if the bundle was already delivered to this neighbor earlier
        */
        LL_FOREACH(ack_list, temp_ack_list){
          if ((is_same_bundle(&temp_bundle->current_bundle, temp_ack_list->bundle) && is_same_neighbor(neighbor, temp_ack_list->neighbor))){
            DEBUG("convergence_layer: Already delivered bundle with creation time %lu to %lu.\n", temp_bundle->current_bundle.local_creation_time, neighbor->endpoint_num);
            continue;
          } 
        }
        gnrc_netif_t *netif = NULL;
        nanocbor_encoder_t enc;
        uint32_t original_bundle_age = 0;

        netif = gnrc_netif_get_by_pid(iface);

        struct bundle_canonical_block_t *bundle_age_block = get_block_by_type(&temp_bundle->current_bundle, BUNDLE_BLOCK_TYPE_BUNDLE_AGE);
        if(bundle_age_block != NULL) {
          original_bundle_age = atoi((char*)bundle_age_block->block_data);
          if(increment_bundle_age(bundle_age_block, &temp_bundle->current_bundle) < 0) {
            DEBUG("convergence_layer: Cannot send this bundle to the new neighbor, it has expired.\n");
            struct bundle_list *next_bundle = temp_bundle->next;
            set_retention_constraint(&temp_bundle->current_bundle, NO_RETENTION_CONSTRAINT);
            delete_bundle(&temp_bundle->current_bundle);
            temp_bundle = next_bundle;
            continue;
          }
        }

        nanocbor_encoder_init(&enc, NULL, 0);
        bundle_encode(&temp_bundle->current_bundle, &enc);
        size_t required_size = nanocbor_encoded_len(&enc);
        uint8_t *buf = malloc(required_size);
        nanocbor_encoder_init(&enc, buf, required_size);
        bundle_encode(&temp_bundle->current_bundle, &enc);

        gnrc_pktsnip_t *pkt = gnrc_pktbuf_add(NULL, buf, (int)required_size, GNRC_NETTYPE_BP);
        if (pkt == NULL) {
          DEBUG("convergence_layer: unable to copy data to packet buffer.\n");
          gnrc_pktbuf_release(pkt);
          free(buf);
          return ;
        }
        
       if (netif != NULL) {
            gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, neighbor->l2addr, neighbor->l2addr_len);
            gnrc_netif_hdr_set_netif(netif_hdr->data, netif);
            LL_PREPEND(pkt, netif_hdr);
        }
        if (netif->pid != 0) {
          DEBUG("convergence_layer: Sending stored packet to process with pid %d.\n", netif->pid);
          gnrc_netapi_send(netif->pid, pkt);
          update_statistics(BUNDLE_SEND);
        }
        /*Will reset bundle age to original so that the bundle's age can be correctly identified
          when updating the bundle age the next time when sending to someone else.
          Also, cannot do this by simply updating the local creation time since that is used for 
          bundle purging to identify the oldest bundle*/
        if (original_bundle_age != 0) {
          if (reset_bundle_age(bundle_age_block, original_bundle_age) < 0) {
            DEBUG("convergence_layer: Error resetting bundle age to original.\n");
          }
        }
      }
      temp_bundle = temp_bundle->next;
      i++;
    }
    return ;
}

void send_non_bundle_ack(struct actual_bundle *bundle, gnrc_pktsnip_t *pkt) {
  DEBUG("convergence_layer: Sending non bundle acknowledgement.\n");
  gnrc_netif_t *netif = NULL;
  gnrc_pktsnip_t *ack_payload;
  uint8_t *temp_addr;
  uint8_t src_addr_len;
  
  char data[MAX_ACK_SIZE];

  netif = gnrc_netif_get_by_pid(iface);

  sprintf(data, "ack_%lu_%lu_%lu", bundle->primary_block.creation_timestamp[0], bundle->primary_block.creation_timestamp[1], bundle->primary_block.src_num);

  ack_payload = gnrc_pktbuf_add(NULL, data, strlen(data), GNRC_NETTYPE_UNDEF);

  //TODO: Change the src_num to the node from which the packet has just been received
  src_addr_len = gnrc_netif_hdr_get_srcaddr(pkt, &temp_addr);

  if (netif != NULL) {
      gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, temp_addr, src_addr_len);

      gnrc_netif_hdr_set_netif(netif_hdr->data, netif);
      LL_PREPEND(ack_payload, netif_hdr);
  }
  if (netif->pid != 0) {
    gnrc_netapi_send(netif->pid, ack_payload);
    update_statistics(ACK_SEND);
  }
}

/* Not used for now but an provides option to send acks in form of bundles.
 * Note: Takes more space than non bundle acks
 */
void send_ack(struct actual_bundle *bundle) {
  int lifetime = 1;
  struct actual_bundle *ack_bundle;
  uint64_t payload_flag;
  uint8_t *payload_data;
  size_t data_len, dst_len, report_len, service_len;
  dst_len = calculate_size_of_num(bundle->primary_block.src_num);
  report_len = calculate_size_of_num(bundle->primary_block.report_num);
  service_len = calculate_size_of_num(bundle->primary_block.service_num);

  char buf_dst[dst_len], buf_report[report_len], buf_service[service_len];

  data_len = 4;
  payload_data = (uint8_t*)malloc(data_len);
  payload_data = (uint8_t*)"ack";

  if (calculate_canonical_flag(&payload_flag, false) < 0) {
    DEBUG("convergence_layer: Error creating payload flag.\n");
    return;
  }
  sprintf(buf_dst, "%lu", bundle->primary_block.src_num);
  sprintf(buf_report, "%lu", bundle->primary_block.report_num);
  sprintf(buf_service, "%lu", bundle->primary_block.service_num);
  ack_bundle = create_bundle();
  fill_bundle(ack_bundle, 7, IPN, buf_dst, buf_report, lifetime, bundle->primary_block.crc_type, buf_service);
  bundle_add_block(ack_bundle, BUNDLE_BLOCK_TYPE_PAYLOAD, payload_flag, payload_data, NOCRC, data_len);

  if(!gnrc_bp_dispatch(GNRC_NETTYPE_BP, GNRC_NETREG_DEMUX_CTX_ALL, ack_bundle, GNRC_NETAPI_MSG_TYPE_SND)) {
    DEBUG("convergence_layer: Unable to find BP thread.\n");
    return ;
  }
  delete_bundle(ack_bundle);  
}

int deliver_bundles_to_application(struct registration_status *application)
{
  struct bundle_list *list, *temp;
  list = get_bundle_list();
  LL_FOREACH(list, temp) {
    if (list->current_bundle.primary_block.dst_num == strtoul(get_src_num(), NULL, 10) && list->current_bundle.primary_block.service_num == application->service_num) {
      deliver_bundle((void *)(bundle_get_payload_block(&list->current_bundle)->block_data), application);
      set_retention_constraint(&temp->current_bundle, NO_RETENTION_CONSTRAINT);
      delete_bundle(&temp->current_bundle);
    }
  }
  return OK;
}

static int calculate_size_of_num(uint32_t num) {
  if(num == 0) {
    return 0;
  }
  int a = ((ceil(log10(num))+1)*sizeof(char)); 
  return a;
}

