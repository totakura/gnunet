/*
     This file is part of GNUnet.
     Copyright (C) 2012 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
     Boston, MA 02110-1301, USA.
*/

/**
 * @file include/gnunet_lockmanager_service.h
 * @brief API for the lockmanger service
 * @author Sree Harsha Totakura
 */

#ifndef GNUNET_LOCKMANAGER_SERVICE_H
#define GNUNET_LOCKMANAGER_SERVICE_H

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif

#include "gnunet_configuration_lib.h"

/**
 * Opaque handle for the lockmanager service
 */
struct GNUNET_LOCKMANAGER_Handle;


/**
 * Connect to the lockmanager service
 *
 * @param cfg the configuration to use
 *
 * @return upon success the handle to the service; NULL upon error
 */
struct GNUNET_LOCKMANAGER_Handle *
GNUNET_LOCKMANAGER_connect (const struct GNUNET_CONFIGURATION_Handle *cfg);


/**
 * Disconnect from the lockmanager service
 *
 * @param handle the handle to the lockmanager service
 */
void
GNUNET_LOCKMANAGER_disconnect (struct GNUNET_LOCKMANAGER_Handle *handle);


/**
 * Enumeration for status
 */
enum GNUNET_LOCKMANAGER_Status
  {
    /**
     * Signifies a successful operation
     */
    GNUNET_LOCKMANAGER_SUCCESS = 1,

    /**
     * Used to signal that a lock is no longer valid. It must then be released
     */
    GNUNET_LOCKMANAGER_RELEASE
  };


/**
 * This callback will be called when a lock has been successfully acquired or
 * when an acquired lock has been lost (happens when the lockmanager service
 * crashes/restarts).
 *
 * @param cls the closure from GNUNET_LOCKMANAGER_lock call
 *
 * @param domain_name the locking domain of the lock
 *
 * @param lock the lock for which this status is relevant
 *
 * @param status GNUNET_LOCKMANAGER_SUCCESS if the lock has been successfully
 *          acquired; GNUNET_LOCKMANAGER_RELEASE when the acquired lock is
 *          lost.
 */
typedef void
(*GNUNET_LOCKMANAGER_StatusCallback) (void *cls,
                                      const char *domain_name,
                                      uint32_t lock,
                                      enum GNUNET_LOCKMANAGER_Status
                                      status);


/**
 * Opaque handle to locking request
 */
struct GNUNET_LOCKMANAGER_LockingRequest;


/**
 * Tries to acquire the given lock(even if the lock has been lost) until the
 * request is called. If the lock is available the status_cb will be
 * called. If the lock is busy then the request is queued and status_cb
 * will be called when the lock has been made available and acquired by us.
 *
 * @param handle the handle to the lockmanager service
 *
 * @param domain_name name of the locking domain. Clients who want to share
 *          locks must use the same name for the locking domain. Also the
 *          domain_name should be selected with the prefix
 *          "GNUNET_<PROGRAM_NAME>_" to avoid domain name collisions.
 *
 *
 * @param lock which lock to lock
 *
 * @param status_cb the callback for signalling when the lock is acquired and
 *          when it is lost
 *
 * @param status_cb_cls the closure to the above callback
 *
 * @return the locking request handle for this request
 */
struct GNUNET_LOCKMANAGER_LockingRequest *
GNUNET_LOCKMANAGER_acquire_lock (struct GNUNET_LOCKMANAGER_Handle *handle,
                                 const char *domain_name,
                                 uint32_t lock,
                                 GNUNET_LOCKMANAGER_StatusCallback
                                 status_cb,
                                 void *status_cb_cls);


/**
 * Function to cancel the locking request generated by
 * GNUNET_LOCKMANAGER_acquire_lock. If the lock is acquired by us then the lock
 * is released. GNUNET_LOCKMANAGER_StatusCallback will not be called upon any
 * status changes resulting due to this call.
 *
 * @param request the LockingRequest to cancel
 */
void
GNUNET_LOCKMANAGER_cancel_request (struct GNUNET_LOCKMANAGER_LockingRequest
                                   *request);


#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif

/* ifndef GNUNET_LOCKMANAGER_SERVICE_H */
#endif
/* end of gnunet_lockmanager_service.h */
