
#include <math.h>
#include "kernel_types.h"
#include "thread.h"
#include "utlist.h"

#include "net/gnrc/netif.h"
#include "net/gnrc/bp.h"
#include "net/gnrc.h"
#include "net/gnrc/pktbuf.h"
#include "net/gnrc/bundle_protocol/config.h"
#include "net/gnrc/bundle_protocol/bundle.h"
#include "net/gnrc/bundle_protocol/bundle_storage.h"
#include "net/gnrc/bundle_protocol/routing.h"

#define ENABLE_DEBUG    (1)
#include "debug.h"

#include "od.h"

static kernel_pid_t _pid = KERNEL_PID_UNDEF;

//Hardcoded right now, find a way to change this soon
int iface = 9;


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
static void print_potential_neighbor_list(struct neighbor_t* neighbors);
static int calculate_size_of_num(uint32_t num);

kernel_pid_t gnrc_bp_init(void)
{
  if(_pid > KERNEL_PID_UNDEF){
      return _pid;
  }

  _pid = thread_create(_stack, sizeof(_stack), GNRC_BP_PRIO,
                        THREAD_CREATE_STACKTEST, _event_loop, NULL, "bp");

  DEBUG("bp: thread created with pid: %d\n",_pid);
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
        DEBUG("bp: dropped message to %" PRIkernel_pid " (%s)\n", sendto->target.pid,
              (ret == 0) ? "receiver queue is full" : "invalid receiver");
    }
    return ret;
  }
  return ERROR;
}

bool check_lifetime_expiry(struct actual_bundle *bundle) {
  struct bundle_canonical_block_t *bundle_age_block = get_block_by_type(bundle, BUNDLE_BLOCK_TYPE_BUNDLE_AGE);

  if (bundle_age_block != NULL) {
    if (bundle->primary_block.lifetime < atoi((char*)bundle_age_block->block_data)) {
      delete_bundle(bundle);
      return true;
    }
  }
  return false;
}

int process_bundle_before_forwarding(struct actual_bundle *bundle) {
  DEBUG("bp: Processing bundle before forwarding.\n");
  (void) bundle;

  /*Processing bundle and updating its bundle age block*/
  struct bundle_canonical_block_t *bundle_age_block = get_block_by_type(bundle, BUNDLE_BLOCK_TYPE_BUNDLE_AGE);

  if (bundle_age_block != NULL) {
    DEBUG("bp: found bundle age block in bundle.\n");
    if (increment_bundle_age(bundle_age_block, bundle) < 0) {
      DEBUG("bp: Error updating bundle age block.\n");
      return ERROR;
    }
  }
  return OK;
}

bool is_packet_ack(gnrc_pktsnip_t *pkt) {
  char temp[ACK_IDENTIFIER_SIZE];
  strncpy(temp, pkt->data, ACK_IDENTIFIER_SIZE);
  if (strcmp(temp, "ack") == 0) {
    return true;
  }
  return false;
}

static void _receive(gnrc_pktsnip_t *pkt) 
{
  DEBUG("bp: Receive type: %d with length: %d and data: %s\n",pkt->type, pkt->size, (uint8_t*)pkt->data);
  struct router *cur_router;

  cur_router = get_router();

  if(pkt->data == NULL) {
    DEBUG("bp: No data in packet, dropping it.\n");
    gnrc_pktbuf_release(pkt);
    return ;
  }

  if (is_packet_ack(pkt)) {
    DEBUG("bp: ack received.\n");
    (void) cur_router;
    uint8_t *temp_addr;
    int src_addr_len;

    src_addr_len = gnrc_netif_hdr_get_srcaddr(pkt, &temp_addr);
    uint8_t src_addr[src_addr_len];
    strncpy((char*)src_addr, (char*)temp_addr, src_addr_len);
    DEBUG("bp: src addr from netif hdr %s.\n", src_addr);

    struct neighbor_t *neighbor = get_neighbor_from_l2addr(src_addr);

    DEBUG("bp: ack received from neighbor with endpoint num: %lu and l2addr %s.\n", neighbor->endpoint_num, neighbor->l2addr);
    char *creation_timestamp0, *creation_timestamp1;
    strtok(pkt->data, "_");
    creation_timestamp0 = strtok(NULL, "_");
    creation_timestamp1 = strtok(NULL, "_");
    
    cur_router->received_ack(neighbor, atoi(creation_timestamp0), atoi(creation_timestamp1));
    
  }
  else {
    struct actual_bundle *bundle = create_bundle();
    if (bundle_decode(bundle, pkt->data, pkt->size) == ERROR) {
      DEBUG("bp: Packet received not for bundle protocol.\n");
      delete_bundle(bundle);
      return ;
    }
    if (check_lifetime_expiry(bundle)) {
      DEBUG("bp: received bundle's lifetime expired.\n");
      return ;
    }

#ifdef MODULE_GNRC_CONTACT_MANAGER
    if (bundle->primary_block.service_num  == (uint32_t)atoi(CONTACT_MANAGER_SERVICE_NUM)) {
      // gnrc_pktsnip_t *tmp_pkt = gnrc_pktbuf_add(NULL, bundle, sizeof(bundle), GNRC_NETTYPE_CONTACT_MANAGER);
      if (!gnrc_bp_dispatch(GNRC_NETTYPE_CONTACT_MANAGER, GNRC_NETREG_DEMUX_CTX_ALL, bundle, GNRC_NETAPI_MSG_TYPE_RCV)) {
        DEBUG("bp: no contact_manager thread found\n");
        delete_bundle(bundle);
      }
      gnrc_pktbuf_release(pkt);
    }
#endif
    else {
      DEBUG("bp: Not a discovery packet with destination: %lu, source: %lu and current address: %lu !!!!!!!!!!!!!!!!!!\n", bundle->primary_block.dst_num, bundle->primary_block.src_num, (uint32_t)atoi(get_src_num()));
      DEBUG("bp: ***********Data in bundle.****************\n");
      od_hex_dump(bundle_get_payload_block(bundle)->block_data, bundle_get_payload_block(bundle)->data_len, OD_WIDTH_DEFAULT);
      
      /* This bundle is for the current node, send to application that sent it*/
      if (bundle->primary_block.dst_num == (uint32_t)atoi(get_src_num())) {
        bool delivered = true;
        /* TODO: Deliver bundle to application as per its registration */
        if (!gnrc_bp_dispatch(GNRC_NETTYPE_BP, bundle->primary_block.service_num, bundle, GNRC_NETAPI_MSG_TYPE_RCV)) {
          DEBUG("bp: Couldn't send bundle to registered receivers.\n");
          delivered = false;
          /*TODO: Implement deletion according to registration*/
          // delete_bundle(bundle);
        } /*Bundle received is for this node but not of type acknowledgement*/
        send_non_bundle_ack(bundle);
        if (delivered) {
          delete_bundle(bundle);
        }
      } /*Bundle not for this node, forward received bundle*/
      else {
        struct router *cur_router = get_router();
        nanocbor_encoder_t enc;
        gnrc_netif_t *netif = NULL;
        struct neighbor_t *temp;
        bool sent = false;

        netif = gnrc_netif_get_by_pid(iface);
        DEBUG("bp: Sending bundle to hardcoded interface %d.\n", iface);

        struct neighbor_t *neighbors_to_send = cur_router->route_receivers(bundle->primary_block.dst_num);
        if (neighbors_to_send == NULL) {
          DEBUG("bp: Could not find neighbors to send bundle to.\n");
          delete_bundle(bundle);
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
        printf("Encoded bundle while forwarding: ");
        for(int i=0;i<(int)required_size;i++){
          printf("%02x",buf[i]);
        }
        printf(" at %p\n", bundle);

        gnrc_pktsnip_t *pkt = gnrc_pktbuf_add(NULL, buf, (int)required_size, GNRC_NETTYPE_BP);
        if (pkt == NULL) {
          DEBUG("bp: unable to copy data to packet buffer.\n");
          delete_bundle(bundle);
          free(buf);
          return ;
        }
        /* TODO: Think of the case when the data is sent to no node, then free the space allocated for the 
            encoded bundle*/
        LL_FOREACH(neighbors_to_send, temp) {
          if (temp->endpoint_scheme == IPN && temp->endpoint_num != (uint32_t)atoi(get_src_num())) {
            DEBUG("bp:Forwarding packet to neighbor with eid %lu.\n", temp->endpoint_num);
            sent = true;
            if (netif != NULL) {
              gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, temp->l2addr, temp->l2addr_len);
              DEBUG("bp: netif hdr data is %s.\n",(char *)netif_hdr->data);
              gnrc_netif_hdr_set_netif(netif_hdr->data, netif);
              LL_PREPEND(pkt, netif_hdr);
            }
            if (netif->pid != 0) {
              DEBUG("bp: Sending discovery packet to process with pid %d.\n", netif->pid);
              gnrc_netapi_send(netif->pid, pkt);
            }
          }
        }
        if(!sent) {
          free(buf);
        }
      }
    }
  }
  return ;
}

static void _send(struct actual_bundle *bundle)
{
    (void) bundle;
    struct router *cur_router = get_router();
    struct neighbor_t *temp;
    DEBUG("bp: Send type: %d\n",bundle->primary_block.version);

    gnrc_netif_t *netif = NULL;
    nanocbor_encoder_t enc;

    netif = gnrc_netif_get_by_pid(iface);
    DEBUG("bp: Sending bundle to hardcoded interface %d.\n", iface);

    struct neighbor_t *neighbor_list_to_send = cur_router->route_receivers(bundle->primary_block.dst_num);
    print_potential_neighbor_list(neighbor_list_to_send);
    if (neighbor_list_to_send == NULL) {
      DEBUG("bp: Could not find neighbors to send bundle to.\n");
      // delete_bundle(bundle);
      return ;
    }

    nanocbor_encoder_init(&enc, NULL, 0);
    bundle_encode(bundle, &enc);
    size_t required_size = nanocbor_encoded_len(&enc);
    uint8_t *buf = malloc(required_size);
    nanocbor_encoder_init(&enc, buf, required_size);
    bundle_encode(bundle, &enc);
    printf("Encoded bundle: ");
    for(int i=0;i<(int)required_size;i++){
      printf("%02x",buf[i]);
    }
    printf(" at %p\n", bundle);

    gnrc_pktsnip_t *pkt = gnrc_pktbuf_add(NULL, buf, (int)required_size, GNRC_NETTYPE_BP);
    if (pkt == NULL) {
      DEBUG("bp: unable to copy data to discovery packet buffer.\n");
      delete_bundle(bundle);
      free(buf);
      return ;
    }

    LL_FOREACH(neighbor_list_to_send, temp) {
      struct delivered_bundle_list *ack_list, *temp_ack_list;
      ack_list = cur_router->get_delivered_bundle_list();
      if (ack_list == NULL) {
        DEBUG("bp: Sending bundle to neighbor since ack list is null.\n");
        if (netif != NULL) {
          gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, temp->l2addr, temp->l2addr_len);
          DEBUG("bp: netif hdr data is %s.\n",(char *)netif_hdr->data);
          gnrc_netif_hdr_set_netif(netif_hdr->data, netif);
          LL_PREPEND(pkt, netif_hdr);
        }
        if (netif->pid != 0) {
          DEBUG("bp: Sending discovery packet to process with pid %d.\n", netif->pid);
          gnrc_netapi_send(netif->pid, pkt);
        }
      } 
      LL_FOREACH(ack_list, temp_ack_list) {
        if (!(is_same_bundle(bundle, temp_ack_list->bundle) && is_same_neighbor(temp, temp_ack_list->neighbor))) {
          DEBUG("bp: Already delivered bundle with creation time %lu to %s", bundle->local_creation_time, temp->l2addr);
        }
        else {
          DEBUG("bp: Sending bundle to neighbor.\n");
          if (netif != NULL) {
            gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, temp->l2addr, temp->l2addr_len);
            DEBUG("bp: netif hdr data is %s.\n",(char *)netif_hdr->data);
            gnrc_netif_hdr_set_netif(netif_hdr->data, netif);
            LL_PREPEND(pkt, netif_hdr);
          }
          if (netif->pid != 0) {
            DEBUG("bp: Sending discovery packet to process with pid %d.\n", netif->pid);
            gnrc_netapi_send(netif->pid, pkt);
          }
        }
      }
    }
    // delete_bundle(bundle);
    return ;
}

static void _send_packet(gnrc_pktsnip_t *pkt)
{
  gnrc_netif_t *netif = NULL;
  netif = gnrc_netif_hdr_get_netif(pkt->data);

  if (netif->pid != 0) {
    DEBUG("bp: Sending discovery packet to process with pid %d.\n", netif->pid);
    gnrc_netapi_send(netif->pid, pkt);
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

  while(1){
    DEBUG("bp: waiting for incoming message.\n");
    msg_receive(&msg);
    switch(msg.type){
      case GNRC_NETAPI_MSG_TYPE_SND:
          DEBUG("bp: GNRC_NETDEV_MSG_TYPE_SND received\n");
          if(strcmp(thread_get(msg.sender_pid)->name, "contact_manager") == 0) {
            _send_packet(msg.content.ptr);
            break;
          }
          _send(msg.content.ptr);
          break;
      case GNRC_NETAPI_MSG_TYPE_RCV:
          DEBUG("bp: GNRC_NETDEV_MSG_TYPE_RCV received\n");
          _receive(msg.content.ptr);
          break;
      default:
        DEBUG("bp: Successfully entered bp, yayyyyyy!!\n");
        break;
    }
  }
  return NULL;
}

static void retransmit_timer_callback(void *args) {
  printf("bp: inside retransmit_timer_callback.\n");
  (void) args;
  struct bundle_list *bundle_storage_list = get_bundle_list(), *temp;
  uint8_t active_bundles = get_current_active_bundles(), i = 0;
  temp = bundle_storage_list;
  while (temp != NULL && i < active_bundles) {
    if(!gnrc_bp_dispatch(GNRC_NETTYPE_BP, GNRC_NETREG_DEMUX_CTX_ALL, &temp->current_bundle, GNRC_NETAPI_MSG_TYPE_SND)) {
      printf("Unable to find BP thread.\n");
      // gnrc_pktbuf_release(pkt);
      // delete_bundle(bundle1);
      return ;
    }
    temp = temp->next;
    i++;
  }
  xtimer_set(args, xtimer_ticks_from_usec(RETRANSMIT_TIMER_SECONDS).ticks32);
}

static void print_potential_neighbor_list(struct neighbor_t* neighbors) {
  char addr_str[GNRC_NETIF_HDR_L2ADDR_PRINT_LEN];
  struct neighbor_t *temp;
  DEBUG("bp: Printing neighbor list: ");
  LL_FOREACH(neighbors, temp) {
    DEBUG("(%lu, %s )-> ", temp->endpoint_num, gnrc_netif_addr_to_str(temp->l2addr, temp->l2addr_len, addr_str));
  }
  DEBUG(".\n");
}

void send_bundles_to_new_neighbor(struct neighbor_t *neighbor) {
    DEBUG("bp: sending bundles to new neighbor.\n");
    struct bundle_list *bundle_store_list, *temp_bundle;

    bundle_store_list = get_bundle_list();
    print_bundle_storage();
    temp_bundle = bundle_store_list;
    while(temp_bundle != NULL) {
    // LL_FOREACH(bundle_store_list, temp_bundle){
      print_bundle_storage();
      DEBUG("bp: Sending this bundle to new neighbor with destination: %lu and local_creation_time: %lu.\n", temp_bundle->current_bundle.primary_block.dst_num, temp_bundle->current_bundle.local_creation_time);
      if(temp_bundle->current_bundle.primary_block.dst_num != (uint32_t)atoi(BROADCAST_EID)) {

        gnrc_netif_t *netif = NULL;
        nanocbor_encoder_t enc;
        uint32_t original_bundle_age = 0;

        netif = gnrc_netif_get_by_pid(iface);

        struct bundle_canonical_block_t *bundle_age_block = get_block_by_type(&temp_bundle->current_bundle, BUNDLE_BLOCK_TYPE_BUNDLE_AGE);
        if(bundle_age_block != NULL) {
          original_bundle_age = atoi((char*)bundle_age_block->block_data);
          if(increment_bundle_age(bundle_age_block, &temp_bundle->current_bundle) < 0) {
            DEBUG("bp: Error updating bundle age.\n");
            struct bundle_list *next_bundle = temp_bundle->next;
            delete_bundle(&temp_bundle->current_bundle);
            temp_bundle = next_bundle;
            continue;
          }
        }

        print_bundle(&temp_bundle->current_bundle);

        nanocbor_encoder_init(&enc, NULL, 0);
        bundle_encode(&temp_bundle->current_bundle, &enc);
        size_t required_size = nanocbor_encoded_len(&enc);
        uint8_t *buf = malloc(required_size);
        nanocbor_encoder_init(&enc, buf, required_size);
        bundle_encode(&temp_bundle->current_bundle, &enc);
        printf("Encoded bundle: ");
        for(int i=0;i<(int)required_size;i++){
          printf("%02x",buf[i]);
        }
        printf(" at %p\n", &temp_bundle->current_bundle);

        gnrc_pktsnip_t *pkt = gnrc_pktbuf_add(NULL, buf, (int)required_size, GNRC_NETTYPE_BP);
        if (pkt == NULL) {
          DEBUG("bp: unable to copy data to packet buffer.\n");
          delete_bundle(&temp_bundle->current_bundle);
          free(buf);
          return ;
        }
        
       if (netif != NULL) {
            gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, neighbor->l2addr, neighbor->l2addr_len);
            // DEBUG("bp: netif hdr data is %s.\n",(char *)netif_hdr->data);
            gnrc_netif_hdr_set_netif(netif_hdr->data, netif);
            LL_PREPEND(pkt, netif_hdr);
        }
        if (netif->pid != 0) {
          DEBUG("bp: Sending stored packet to process with pid %d.\n", netif->pid);
          gnrc_netapi_send(netif->pid, pkt);
        }
        /*Will reset bundle age to original so that the bundle is identifiable 
          with the orignal information for cross checking with data in the 
          acknowledgement packet*/
        if (original_bundle_age != 0) {
          if (reset_bundle_age(bundle_age_block, original_bundle_age) < 0) {
            DEBUG("bp: Error resetting bundle age to original.\n");
          }
        }
      }
      temp_bundle = temp_bundle->next;
    }
    return ;
}

void send_non_bundle_ack(struct actual_bundle *bundle) {

  gnrc_netif_t *netif = NULL;
  gnrc_pktsnip_t *ack_payload;
  
  char data[MAX_ACK_SIZE];
  struct neighbor_t *neighbor_src;

  netif = gnrc_netif_get_by_pid(iface);

  sprintf(data, "ack_%lu_%lu", bundle->primary_block.creation_timestamp[0], bundle->primary_block.creation_timestamp[1]);

  ack_payload = gnrc_pktbuf_add(NULL, data, strlen(data), GNRC_NETTYPE_UNDEF);

  neighbor_src = get_neighbor_from_endpoint_num(bundle->primary_block.src_num);
  if (netif != NULL) {
      puts("Adding netif header");
      gnrc_pktsnip_t *netif_hdr = gnrc_netif_hdr_build(NULL, 0, neighbor_src->l2addr, neighbor_src->l2addr_len);

      gnrc_netif_hdr_set_netif(netif_hdr->data, netif);
      LL_PREPEND(ack_payload, netif_hdr);
  }
  if (netif->pid != 0) {
    DEBUG("bp: Sending stored packet to process with pid %d.\n", netif->pid);
    gnrc_netapi_send(netif->pid, ack_payload);
  }
}

void send_ack(struct actual_bundle *bundle) {
  // DEBUG("bp: Inside send ack function.\n");
  int lifetime = 1;
  struct actual_bundle *ack_bundle;
  uint64_t payload_flag;
  uint8_t *payload_data;
  size_t data_len, dst_len, report_len, service_len;
  // DEBUG("bp: dst_num = %lu, report_num = %lu, service_num = %lu.\n", bundle->primary_block.src_num, bundle->primary_block.report_num, bundle->primary_block.service_num);
  // DEBUG("bp: will start calculting dst_len.\n");
  dst_len = calculate_size_of_num(bundle->primary_block.src_num);
  // DEBUG("bp: dst_len = %u\n", dst_len); 
  report_len = calculate_size_of_num(bundle->primary_block.report_num);
  // DEBUG("bp: report_len = %u.\n", report_len);
  service_len = calculate_size_of_num(bundle->primary_block.service_num);
  // DEBUG("bp: service_len = %u.\n", service_len);

  char buf_dst[dst_len], buf_report[report_len], buf_service[service_len];

  data_len = 4;
  payload_data = (uint8_t*)malloc(data_len);
  payload_data = (uint8_t*)"ack";

  if (calculate_canonical_flag(&payload_flag, false) < 0) {
    printf("Error creating payload flag.\n");
    return;
  }
  DEBUG("bp: sprinting for ackssssss.\n");  
  // sscanf(buf_dst, "%lu", &bundle->primary_block.src_num);
  sprintf(buf_dst, "%lu", bundle->primary_block.src_num);
  sprintf(buf_report, "%lu", bundle->primary_block.report_num);
  sprintf(buf_service, "%lu", bundle->primary_block.service_num);
  // DEBUG("bp: sprinted values: %s, %s, %s; actual nums: %lu, %lu, %lu.\n", buf_dst, buf_report, buf_service, bundle->primary_block.src_num, bundle->primary_block.report_num, bundle->primary_block.service_num);
  // DEBUG("bp: Creating bundle for ack");
  ack_bundle = create_bundle();
  fill_bundle(ack_bundle, 7, IPN, buf_dst, buf_report, lifetime, bundle->primary_block.crc_type, buf_service);
  bundle_add_block(ack_bundle, BUNDLE_BLOCK_TYPE_PAYLOAD, payload_flag, payload_data, NOCRC, data_len);

  if(!gnrc_bp_dispatch(GNRC_NETTYPE_BP, GNRC_NETREG_DEMUX_CTX_ALL, ack_bundle, GNRC_NETAPI_MSG_TYPE_SND)) {
    printf("Unable to find BP thread.\n");
    // gnrc_pktbuf_release(pkt);
    return ;
  }
  delete_bundle(ack_bundle);  
}

static int calculate_size_of_num(uint32_t num) {
  if(num == 0) {
    return 0;
  }
  int a = ((ceil(log10(num))+1)*sizeof(char)); 
  // DEBUG("bp:size = %d.\n",a );
  return a;
}

