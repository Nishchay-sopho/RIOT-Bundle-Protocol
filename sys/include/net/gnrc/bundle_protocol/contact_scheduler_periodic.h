/**
 * @ingroup     Bundle protocol
 * @{
 *
 * @file
 * @brief       Periodic discovery packet scheduler header
 *
 * @author      Nishchay Agrawal <agrawal.nishchay5@gmail.com>
 *
 * @}
 */
#ifndef _CONTACT_SCHEDULER_PERIODIC_H
#define _CONTACT_SCHEDULER_PERIODIC_H

#include <stdbool.h>

#include "net/gnrc/bundle_protocol/contact_manager_config.h"

#define CONTACT_PERIOD_SECONDS 30

#ifdef __cplusplus
extern "C" {
#endif

extern int iface;		

/**
 * @brief   Default priority for the _CONTACT_MANAGER_BP thread.
 */
#ifndef GNRC_CONTACT_SCHEDULER_PRIO
#define GNRC_CONTACT_SCHEDULER_PRIO                 (THREAD_PRIORITY_MAIN - 2)
#endif

/**
 * @brief   Initialization of the CONTACT_SCHEDULER_PERIODIC thread.
 *
 * @details If CONTACT_SCHEDULER_PERIODIC was already initialized, it will just return the PID of
 *          the CONTACT_SCHEDULER_PERIODIC thread.
 *
 * @return  The PID to the CONTACT_SCHEDULER_PERIODIC thread, on success.
 * @return  -EINVAL, if @ref GNRC_CONTACT_SCHEDULER_PERIODIC_PRIO was greater than or equal to
 *          @ref SCHED_PRIO_LEVELS
 * @return  -EOVERFLOW, if there are too many threads running already in general
 */
kernel_pid_t gnrc_contact_scheduler_periodic_init(void);
int send(int data);

#ifdef __cplusplus
}
#endif

#endif
