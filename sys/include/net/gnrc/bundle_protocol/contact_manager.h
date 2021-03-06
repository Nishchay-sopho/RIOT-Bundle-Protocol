/**
 * @ingroup     Bundle protocol
 * @{
 *
 * @file
 * @brief       Discovery manager header file
 *
 * @author      Nishchay Agrawal <agrawal.nishchay5@gmail.com>
 *
 * @}
 */
#ifndef _CONTACT_MANAGER_BP_H
#define _CONTACT_MANAGER_BP_H

#include <stdbool.h>

#include "thread.h"
#include "kernel_types.h"
#include "xtimer.h"

#include "net/gnrc/bundle_protocol/contact_manager_config.h"
#include "net/gnrc/bundle_protocol/contact_scheduler_periodic.h"
#include "net/gnrc/ipv6/nib/conf.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MODULE_GNRC_CONTACT_SCHEDULER_PERIODIC
#define NEIGHBOR_PURGE_TIMER_SECONDS (2*CONTACT_PERIOD_SECONDS)
#elif
#define NEIGHBOR_PURGE_TIMER_SECONDS (40) 	
#endif

#define SECS_TO_MICROSECS 1000000

struct neighbor_t{
  uint8_t endpoint_scheme;
  uint32_t endpoint_num;
  uint8_t *eid;
  uint8_t l2addr [GNRC_IPV6_NIB_L2ADDR_MAX_LEN];
  uint8_t 	l2addr_len;
  xtimer_t expiry_timer;
  struct neighbor_t *next;
};



/**
 * @brief   Initialization of the CONTACT_MANAGER thread.
 *
 * @details If CONTACT_MANAGER was already initialized, it will just return the PID of
 *          the CONTACT_MANAGER thread.
 *
 * @return  The PID to the CONTACT_MANAGER thread, on success.
 * @return  -EINVAL, if @ref GNRC_CONTACT_MANAGER_PRIO was greater than or equal to
 *          @ref SCHED_PRIO_LEVELS
 * @return  -EOVERFLOW, if there are too many threads running already in general
 */
kernel_pid_t gnrc_contact_manager_init(void);
void print_neighbor_list(void);
struct neighbor_t *get_neighbor_from_endpoint_num(uint32_t endpoint_num);
struct neighbor_t *get_neighbor_from_l2addr(uint8_t *addr);
struct neighbor_t *get_neighbor_list(void);
void create_neighbor_expiry_timer(struct neighbor_t *neighbor);
bool is_same_neighbor(struct neighbor_t *neighbor, struct neighbor_t *compare_to_neighbor);

#ifdef __cplusplus
}
#endif

#endif
