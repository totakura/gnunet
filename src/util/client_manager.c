/*
     This file is part of GNUnet.
     Copyright (C) 2013 Christian Grothoff (and other contributing authors)

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
 * @file util/client_manager.c
 * @brief Client manager; higher level client API with transmission queue
 * and message handler registration.
 * @author Gabor X Toth
 */

#include <inttypes.h>

#include "platform.h"
#include "gnunet_util_lib.h"

#define LOG(kind,...) GNUNET_log_from (kind, "util-client-mgr", __VA_ARGS__)


struct OperationListItem
{
  struct OperationListItem *prev;
  struct OperationListItem *next;

  /**
   * Operation ID.
   */
  uint64_t op_id;

  /**
   * Continuation to invoke with the result of an operation.
   */
  GNUNET_ResultCallback result_cb;

  /**
   * Closure for @a result_cb.
   */
  void *cls;
};


/**
 * List of arrays of message handlers.
 */
struct HandlersListItem
{
  struct HandlersListItem *prev;
  struct HandlersListItem *next;

  /**
   * NULL-terminated array of handlers.
   */
  const struct GNUNET_CLIENT_MANAGER_MessageHandler *handlers;
};


struct MessageQueueItem
{
  struct MessageQueueItem *prev;
  struct MessageQueueItem *next;
  struct GNUNET_MessageHeader *msg;
};


struct GNUNET_CLIENT_MANAGER_Connection
{
  /**
   * Configuration to use.
   */
  const struct GNUNET_CONFIGURATION_Handle *cfg;

  /**
   * Client connection to service.
   */
  struct GNUNET_CLIENT_Connection *client;

  /**
   * Currently pending transmission request, or NULL for none.
   */
  struct GNUNET_CLIENT_TransmitHandle *client_tmit;

  /**
   * Service name to connect to.
   */
  const char *service_name;

  /**
   * Head of messages to transmit to the service.
   */
  struct MessageQueueItem *tmit_head;

  /**
   * Tail of messages to transmit to the service.
   */
  struct MessageQueueItem *tmit_tail;

  /**
   * Message handlers.
   */
  const struct GNUNET_CLIENT_MANAGER_MessageHandler *handlers;

  /**
   * First operation in the linked list.
   */
  struct OperationListItem *op_head;

  /**
   * Last operation in the linked list.
   */
  struct OperationListItem *op_tail;

  /**
   * Last operation ID used.
   */
  uint64_t last_op_id;

  /**
   * Disconnect callback.
   */
  void (*disconnect_cb)(void *);

  /**
   * Disconnect closure.
   */
  void *disconnect_cls;

  /**
   * User context value.
   * @see GNUNET_CLIENT_MANAGER_set_user_context()
   * @see GNUNET_CLIENT_MANAGER_get_user_context()
   */
  void *user_ctx;

  /**
   * Last size given when user context was initialized.
   * Used for sanity check.
   */
  size_t user_ctx_size;

  /**
   * Task doing exponential back-off trying to reconnect.
   */
  struct GNUNET_SCHEDULER_Task * reconnect_task;

  /**
   * Time for next connect retry.
   */
  struct GNUNET_TIME_Relative reconnect_delay;

  /**
   * Are we currently polling for incoming messages?
   */
  uint8_t in_receive;

  /**
   * #GNUNET_YES if GNUNET_CLIENT_MANAGER_disconnect() was called
   * and we're transmitting the last messages from the queue.
   */
  uint8_t is_disconnecting;
};


/**
 * Handle received messages from the service.
 */
static void
recv_message (void *cls, const struct GNUNET_MessageHeader *msg)
{
  struct GNUNET_CLIENT_MANAGER_Connection *mgr = cls;
  uint16_t type = 0, size = 0;

  if (NULL != msg)
  {
    type = ntohs (msg->type);
    size = ntohs (msg->size);
    /* FIXME: decrease reconnect_delay gradually after a successful reconnection */
  }

  size_t i = 0;
  while (NULL != mgr->handlers[i].callback)
  {
    const struct GNUNET_CLIENT_MANAGER_MessageHandler *mh = &mgr->handlers[i];
    if ((mh->type == type) || (mh->type == GNUNET_MESSAGE_TYPE_ALL))
    {
      if (0 != mh->expected_size
          && ((GNUNET_NO == mh->is_variable_size && size != mh->expected_size)
              || (GNUNET_YES == mh->is_variable_size && size < mh->expected_size)))
      {
        LOG (GNUNET_ERROR_TYPE_ERROR,
             "Expected %u bytes for message of type %u, got %u.\n",
             mh->expected_size, type, size);
        GNUNET_break_op (0);
        GNUNET_CLIENT_disconnect (mgr->client);
        mgr->client = NULL;
        recv_message (mgr, NULL);
        break;
      }
      mh->callback (mh->callback_cls, mgr, msg);
    }
    i++;
  }
  if (NULL != mgr->client)
  {
    GNUNET_CLIENT_receive (mgr->client, &recv_message, mgr,
                           GNUNET_TIME_UNIT_FOREVER_REL);
  }
}


/**
 * Schedule transmission of the next message from our queue.
 *
 * @param mgr  Client manager connection.
 */
static void
transmit_next (struct GNUNET_CLIENT_MANAGER_Connection *mgr);


static void
schedule_disconnect (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_CLIENT_MANAGER_Connection *mgr = cls;
  GNUNET_CLIENT_MANAGER_disconnect (mgr, GNUNET_NO,
                                    mgr->disconnect_cb, mgr->disconnect_cls);
}


/**
 * Transmit next message to service.
 *
 * @param cls
 *        struct GNUNET_CLIENT_MANAGER_Connection
 * @param buf_size
 *        Number of bytes available in @a buf.
 * @param buf
 *        Where to copy the message.
 *
 * @return Number of bytes copied to @a buf.
 */
static size_t
send_next_message (void *cls, size_t buf_size, void *buf)
{
  LOG (GNUNET_ERROR_TYPE_DEBUG, "send_next_message()\n");
  struct GNUNET_CLIENT_MANAGER_Connection *mgr = cls;

  if (NULL == buf)
  {
    /* disconnected */
    recv_message (mgr, NULL);
    return 0;
  }

  struct MessageQueueItem *mqi = mgr->tmit_head;
  if (NULL == mqi)
    return 0;

  uint16_t size = ntohs (mqi->msg->size);
  mgr->client_tmit = NULL;
  GNUNET_assert (size <= buf_size);
  memcpy (buf, mqi->msg, size);

  GNUNET_CONTAINER_DLL_remove (mgr->tmit_head, mgr->tmit_tail, mqi);
  GNUNET_free (mqi->msg);
  GNUNET_free (mqi);

  if (NULL != mgr->tmit_head)
  {
    transmit_next (mgr);
  }
  else if (GNUNET_YES == mgr->is_disconnecting)
  {
    GNUNET_SCHEDULER_add_now (&schedule_disconnect, mgr);
    return size;
  }

  if (GNUNET_NO == mgr->in_receive)
  {
    mgr->in_receive = GNUNET_YES;
    GNUNET_CLIENT_receive (mgr->client, &recv_message, mgr,
                           GNUNET_TIME_UNIT_FOREVER_REL);
  }
  return size;
}


/**
 * Schedule transmission of the next message from our queue.
 *
 * @param mgr  Client manager connection.
 */
static void
transmit_next (struct GNUNET_CLIENT_MANAGER_Connection *mgr)
{
  LOG (GNUNET_ERROR_TYPE_DEBUG, "transmit_next()\n");
  if (NULL != mgr->client_tmit || NULL == mgr->client)
    return;

  if (NULL == mgr->tmit_head)
  {
    if (GNUNET_YES == mgr->is_disconnecting)
      GNUNET_CLIENT_MANAGER_disconnect (mgr, GNUNET_NO,
                                        mgr->disconnect_cb, mgr->disconnect_cls);
    return;
  }

  mgr->client_tmit
    = GNUNET_CLIENT_notify_transmit_ready (mgr->client,
                                           ntohs (mgr->tmit_head->msg->size),
                                           GNUNET_TIME_UNIT_FOREVER_REL,
                                           GNUNET_NO,
                                           &send_next_message,
                                           mgr);
}


/**
 * Try again to connect to the service.
 *
 * @param cls
 *        Channel handle.
 * @param tc
 *        Scheduler context.
 */
static void
schedule_reconnect (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_CLIENT_MANAGER_Connection *mgr = cls;
  mgr->reconnect_task = NULL;

  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Connecting to %s service.\n", mgr->service_name);
  GNUNET_assert (NULL == mgr->client);
  mgr->client = GNUNET_CLIENT_connect (mgr->service_name, mgr->cfg);
  GNUNET_assert (NULL != mgr->client);

  transmit_next (mgr);
}


/**
 * Connect to service.
 *
 * @param cfg
 *        Configuration to use.
 * @param service_name
 *        Service name to connect to.
 * @param handlers
 *        Message handlers.
 *
 * @return Client manager connection handle.
 */
struct GNUNET_CLIENT_MANAGER_Connection *
GNUNET_CLIENT_MANAGER_connect (const struct GNUNET_CONFIGURATION_Handle *cfg,
                               const char *service_name,
                               const struct
                               GNUNET_CLIENT_MANAGER_MessageHandler *handlers)
{
  struct GNUNET_CLIENT_MANAGER_Connection *
    mgr = GNUNET_malloc (sizeof (*mgr));
  mgr->cfg = cfg;
  mgr->service_name = service_name;
  mgr->handlers = handlers;
  mgr->reconnect_delay = GNUNET_TIME_UNIT_ZERO;
  mgr->reconnect_task = GNUNET_SCHEDULER_add_now (&schedule_reconnect, mgr);
  return mgr;
}


/**
 * Disconnect from the service.
 *
 * @param mgr
 *        Client manager connection.
 * @param transmit_queue
 *        Transmit pending messages in queue before disconnecting.
 * @param disconnect_cb
 *        Function called after disconnected from the service.
 * @param cls
 *        Closure for @a disconnect_cb.
 */
void
GNUNET_CLIENT_MANAGER_disconnect (struct GNUNET_CLIENT_MANAGER_Connection *mgr,
                                  int transmit_queue,
                                  GNUNET_ContinuationCallback disconnect_cb,
                                  void *cls)
{
  LOG (GNUNET_ERROR_TYPE_DEBUG, "Disconnecting (%d)\n", transmit_queue);
  mgr->disconnect_cb = disconnect_cb;
  mgr->disconnect_cls = cls;
  if (NULL != mgr->tmit_head)
  {
    if (GNUNET_YES == transmit_queue)
    {
      mgr->is_disconnecting = GNUNET_YES;
      transmit_next (mgr);
      return;
    }
    else
    {
      LOG (GNUNET_ERROR_TYPE_DEBUG,
           "Disconnecting while there are still messages "
           "in the transmission queue.\n");
      GNUNET_CLIENT_MANAGER_drop_queue (mgr);
    }
  }
  if (mgr->reconnect_task != NULL)
  {
    GNUNET_SCHEDULER_cancel (mgr->reconnect_task);
    mgr->reconnect_task = NULL;
  }
  if (NULL != mgr->client_tmit)
  {
    GNUNET_CLIENT_notify_transmit_ready_cancel (mgr->client_tmit);
    mgr->client_tmit = NULL;
  }
  if (NULL != mgr->client)
  {
    GNUNET_CLIENT_disconnect (mgr->client);
    mgr->client = NULL;
  }
  if (NULL != mgr->disconnect_cb)
    mgr->disconnect_cb (mgr->disconnect_cls);
  GNUNET_free (mgr);
  LOG (GNUNET_ERROR_TYPE_DEBUG, "Disconnected.\n");
}


/**
 * Reschedule connect to the service using exponential back-off.
 *
 * @param mgr
 *        Client manager connection.
 */
void
GNUNET_CLIENT_MANAGER_reconnect (struct GNUNET_CLIENT_MANAGER_Connection *mgr)
{
  if (NULL != mgr->reconnect_task)
    return;

  if (NULL != mgr->client_tmit)
  {
    GNUNET_CLIENT_notify_transmit_ready_cancel (mgr->client_tmit);
    mgr->client_tmit = NULL;
  }
  if (NULL != mgr->client)
  {
    GNUNET_CLIENT_disconnect (mgr->client);
    mgr->client = NULL;
  }
  mgr->in_receive = GNUNET_NO;
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Scheduling task to reconnect to service in %s.\n",
       GNUNET_STRINGS_relative_time_to_string (mgr->reconnect_delay, GNUNET_YES));
  mgr->reconnect_task =
    GNUNET_SCHEDULER_add_delayed (mgr->reconnect_delay, &schedule_reconnect, mgr);
  mgr->reconnect_delay = GNUNET_TIME_STD_BACKOFF (mgr->reconnect_delay);
}


/**
 * Add a message to the end of the transmission queue.
 *
 * @param mgr
 *        Client manager connection.
 * @param msg
 *        Message to transmit, should be allocated with GNUNET_malloc() or
 *        GNUNET_new(), as it is freed with GNUNET_free() after transmission.
 */
void
GNUNET_CLIENT_MANAGER_transmit (struct GNUNET_CLIENT_MANAGER_Connection *mgr,
                                struct GNUNET_MessageHeader *msg)
{
  struct MessageQueueItem *mqi = GNUNET_malloc (sizeof (*mqi));
  mqi->msg = msg;
  GNUNET_CONTAINER_DLL_insert_tail (mgr->tmit_head, mgr->tmit_tail, mqi);
  transmit_next (mgr);
}


/**
 * Add a message to the beginning of the transmission queue.
 *
 * @param mgr
 *        Client manager connection.
 * @param msg
 *        Message to transmit, should be allocated with GNUNET_malloc() or
 *        GNUNET_new(), as it is freed with GNUNET_free() after transmission.
 */
void
GNUNET_CLIENT_MANAGER_transmit_now (struct GNUNET_CLIENT_MANAGER_Connection *mgr,
                                    struct GNUNET_MessageHeader *msg)
{
  struct MessageQueueItem *mqi = GNUNET_malloc (sizeof (*mqi));
  mqi->msg = msg;
  GNUNET_CONTAINER_DLL_insert (mgr->tmit_head, mgr->tmit_tail, mqi);
  transmit_next (mgr);
}


/**
 * Drop all queued messages.
 *
 * @param mgr
 *        Client manager connection.
 */
void
GNUNET_CLIENT_MANAGER_drop_queue (struct GNUNET_CLIENT_MANAGER_Connection *mgr)
{
  struct MessageQueueItem *cur, *next = mgr->tmit_head;
  while (NULL != next)
  {
    cur = next;
    next = cur->next;
    GNUNET_free (cur->msg);
    GNUNET_free (cur);
  }
}


/**
 * Obtain client connection handle.
 *
 * @param mgr
 *        Client manager connection.
 *
 * @return Client connection handle.
 */
struct GNUNET_CLIENT_Connection *
GNUNET_CLIENT_MANAGER_get_client (struct GNUNET_CLIENT_MANAGER_Connection *mgr)
{
  return mgr->client;
}


/**
 * Return user context associated with the given client.
 * Note: you should probably use the macro (call without the underscore).
 *
 * @param mgr
 *        Client manager connection.
 * @param size
 *        Number of bytes in user context struct (for verification only).
 *
 * @return User context.
 */
void *
GNUNET_CLIENT_MANAGER_get_user_context_ (struct GNUNET_CLIENT_MANAGER_Connection *mgr,
                                         size_t size)
{
  if ((0 == mgr->user_ctx_size) &&
      (NULL == mgr->user_ctx))
    return NULL; /* never set */
  GNUNET_assert (size == mgr->user_ctx_size);
  return mgr->user_ctx;
}


/**
 * Set user context to be associated with the given client.
 * Note: you should probably use the macro (call without the underscore).
 *
 * @param mgr
 *        Client manager connection.
 * @param ctx
 *        User context.
 * @param size
 *        Number of bytes in user context struct (for verification only).
 */
void
GNUNET_CLIENT_MANAGER_set_user_context_ (struct GNUNET_CLIENT_MANAGER_Connection *mgr,
                                         void *ctx,
                                         size_t size)
{
  if (NULL == ctx)
  {
    mgr->user_ctx_size = 0;
    mgr->user_ctx = ctx;
    return;
  }
  mgr->user_ctx_size = size;
  mgr->user_ctx = ctx;
}


/**
 * Get a unique operation ID to distinguish between asynchronous requests.
 *
 * @param mgr
 *        Client manager connection.
 *
 * @return Operation ID to use.
 */
uint64_t
GNUNET_CLIENT_MANAGER_op_get_next_id (struct GNUNET_CLIENT_MANAGER_Connection *mgr)
{
  return ++mgr->last_op_id;
}


/**
 * Find operation by ID.
 *
 * @param mgr
 *        Client manager connection.
 * @param op_id
 *        Operation ID to look up.
 *
 * @return Operation, or NULL if not found.
 */
static struct OperationListItem *
op_find (struct GNUNET_CLIENT_MANAGER_Connection *mgr, uint64_t op_id)
{
  struct OperationListItem *op = mgr->op_head;
  while (NULL != op)
  {
    if (op->op_id == op_id)
      return op;
    op = op->next;
  }
  return NULL;
}


/**
 * Find operation by ID.
 *
 * @param mgr
 *        Client manager connection.
 * @param op_id
 *        Operation ID to look up.
 * @param[out] result_cb
 *        If an operation was found, its result callback is returned here.
 * @param[out] cls
 *        If an operation was found, its closure is returned here.
 *
 * @return #GNUNET_YES if an operation was found,
 *         #GNUNET_NO  if not found.
 */
int
GNUNET_CLIENT_MANAGER_op_find (struct GNUNET_CLIENT_MANAGER_Connection *mgr,
                               uint64_t op_id,
                               GNUNET_ResultCallback *result_cb,
                               void **cls)
{
  struct OperationListItem *op = op_find (mgr, op_id);
  if (NULL != op)
  {
    *result_cb = op->result_cb;
    *cls = op->cls;
    return GNUNET_YES;
  }
  return GNUNET_NO;
}


/**
 * Add a new operation.
 *
 * @param mgr
 *        Client manager connection.
 * @param result_cb
 *        Function to call with the result of the operation.
 * @param cls
 *        Closure for @a result_cb.
 *
 * @return ID of the new operation.
 */
uint64_t
GNUNET_CLIENT_MANAGER_op_add (struct GNUNET_CLIENT_MANAGER_Connection *mgr,
                              GNUNET_ResultCallback result_cb,
                              void *cls)
{
  if (NULL == result_cb)
    return 0;

  struct OperationListItem *op = GNUNET_malloc (sizeof (*op));
  op->op_id = GNUNET_CLIENT_MANAGER_op_get_next_id (mgr);
  op->result_cb = result_cb;
  op->cls = cls;
  GNUNET_CONTAINER_DLL_insert_tail (mgr->op_head, mgr->op_tail, op);

  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "%p Added operation #%" PRIu64 "\n", mgr, op->op_id);
  return op->op_id;
}


/**
 * Remove an operation, and call its result callback (unless it was cancelled).
 *
 *
 * @param mgr
 *        Client manager connection.
 * @param op_id
 *        Operation ID.
 * @param result_code
 *        Result of the operation.
 * @param data
 *        Data result of the operation.
 * @param data_size
 *        Size of @a data.
 * @param cancel
 *        Is the operation cancelled?
 *        #GNUNET_NO  Not cancelled, result callback is called.
 *        #GNUNET_YES Cancelled, result callback is not called.
 *
 * @return #GNUNET_YES if the operation was found and removed,
 *         #GNUNET_NO  if the operation was not found.
 */
static int
op_result (struct GNUNET_CLIENT_MANAGER_Connection *mgr,
           uint64_t op_id, int64_t result_code,
           const void *data, uint16_t data_size, uint8_t cancel)
{
  if (0 == op_id)
    return GNUNET_NO;

  struct OperationListItem *op = op_find (mgr, op_id);
  if (NULL == op)
  {
    LOG (GNUNET_ERROR_TYPE_WARNING,
         "Could not find operation #%" PRIu64 "\n", op_id);
    return GNUNET_NO;
  }

  GNUNET_CONTAINER_DLL_remove (mgr->op_head, mgr->op_tail, op);

  if (GNUNET_YES != cancel && NULL != op->result_cb)
    op->result_cb (op->cls, result_code, data, data_size);

  GNUNET_free (op);
  return GNUNET_YES;
}


/**
 * Call the result callback of an operation and remove it.
 *
 * @param mgr
 *        Client manager connection.
 * @param op_id
 *        Operation ID.
 * @param result_code
 *        Result of the operation.
 * @param data
 *        Data result of the operation.
 * @param data_size
 *        Size of @a data.
 *
 * @return #GNUNET_YES if the operation was found and removed,
 *         #GNUNET_NO  if the operation was not found.
 */
int
GNUNET_CLIENT_MANAGER_op_result (struct GNUNET_CLIENT_MANAGER_Connection *mgr,
                                 uint64_t op_id, int64_t result_code,
                                 const void *data, uint16_t data_size)
{
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "%p Received result for operation #%" PRIu64 ": %" PRId64 " (size: %u)\n",
       mgr, op_id, result_code, data_size);
  return op_result (mgr, op_id, result_code, data, data_size, GNUNET_NO);
}


/**
 * Cancel an operation.
 *
 * @param mgr
 *        Client manager connection.
 * @param op_id
 *        Operation ID.
 *
 * @return #GNUNET_YES if the operation was found and removed,
 *         #GNUNET_NO  if the operation was not found.
 */
int
GNUNET_CLIENT_MANAGER_op_cancel (struct GNUNET_CLIENT_MANAGER_Connection *mgr,
                                 uint64_t op_id)
{
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "%p Cancelling operation #%" PRIu64  "\n", mgr, op_id);
  return op_result (mgr, op_id, 0, NULL, 0, GNUNET_YES);
}
