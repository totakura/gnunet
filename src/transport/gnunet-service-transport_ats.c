/*
     This file is part of GNUnet.
     Copyright (C) 2015 Christian Grothoff (and other contributing authors)

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
 * @file transport/gnunet-service-transport_ats.h
 * @brief interfacing between transport and ATS service
 * @author Christian Grothoff
 */
#include "platform.h"
#include "gnunet-service-transport.h"
#include "gnunet-service-transport_ats.h"
#include "gnunet-service-transport_manipulation.h"
#include "gnunet-service-transport_plugins.h"
#include "gnunet_ats_service.h"

#define LOG(kind,...) GNUNET_log_from(kind, "transport-ats", __VA_ARGS__)


/**
 * Information we track for each address known to ATS.
 */
struct AddressInfo
{

  /**
   * The address (with peer identity).
   */
  struct GNUNET_HELLO_Address *address;

  /**
   * Session (can be NULL)
   */
  struct Session *session;

  /**
   * Record with ATS API for the address.
   */
  struct GNUNET_ATS_AddressRecord *ar;

  /**
   * Performance properties of this address.
   */
  struct GNUNET_ATS_Properties properties;

  /**
   * Time until when this address is blocked and should thus not be
   * made available to ATS (@e ar should be NULL until this time).
   * Used when transport determines that for some reason it
   * (temporarily) cannot use an address, even though it has been
   * validated.
   */
  struct GNUNET_TIME_Absolute blocked;

  /**
   * If an address is blocked as part of an exponential back-off,
   * we track the current size of the backoff here.
   */
  struct GNUNET_TIME_Relative back_off;

  /**
   * Task scheduled to unblock an ATS-blocked address at
   * @e blocked time, or NULL if the address is not blocked
   * (and thus @e ar is non-NULL).
   */
  struct GNUNET_SCHEDULER_Task *unblock_task;

  /**
   * Set to #GNUNET_YES if the address has expired but we could
   * not yet remove it because we still have a valid session.
   */
  int expired;

};


/**
 * Map from peer identities to one or more `struct AddressInfo` values
 * for the peer.
 */
static struct GNUNET_CONTAINER_MultiPeerMap *p2a;

/**
 * Number of blocked addresses.
 */
static unsigned int num_blocked;

/**
 * Closure for #find_ai().
 */
struct FindClosure
{

  /**
   * Session to look for (only used if the address is inbound).
   */
  struct Session *session;

  /**
   * Address to look for.
   */
  const struct GNUNET_HELLO_Address *address;

  /**
   * Where to store the result.
   */
  struct AddressInfo *ret;

};


/**
 * Provide an update on the `p2a` map size to statistics.
 * This function should be called whenever the `p2a` map
 * is changed.
 */
static void
publish_p2a_stat_update ()
{
  GNUNET_STATISTICS_set (GST_stats,
			 gettext_noop ("# Addresses given to ATS"),
			 GNUNET_CONTAINER_multipeermap_size (p2a) - num_blocked,
			 GNUNET_NO);
  GNUNET_STATISTICS_set (GST_stats,
                         "# blocked addresses",
                         num_blocked,
                         GNUNET_NO);
}


/**
 * Find matching address info.
 *
 * @param cls the `struct FindClosure`
 * @param key which peer is this about
 * @param value the `struct AddressInfo`
 * @return #GNUNET_YES to continue to iterate, #GNUNET_NO if we found the value
 */
static int
find_ai_cb (void *cls,
            const struct GNUNET_PeerIdentity *key,
            void *value)
{
  struct FindClosure *fc = cls;
  struct AddressInfo *ai = value;

  if ( (0 ==
        GNUNET_HELLO_address_cmp (fc->address,
                                  ai->address) ) &&
       (fc->session == ai->session) )
  {
    fc->ret = ai;
    return GNUNET_NO;
  }
  GNUNET_assert ( (fc->session != ai->session) ||
                  (NULL == ai->session) );
  return GNUNET_YES;
}


/**
 * Find the address information struct for the
 * given address and session.
 *
 * @param address address to look for
 * @param session session to match for inbound connections
 * @return NULL if this combination is unknown
 */
static struct AddressInfo *
find_ai (const struct GNUNET_HELLO_Address *address,
         struct Session *session)
{
  struct FindClosure fc;

  fc.address = address;
  fc.session = session;
  fc.ret = NULL;
  GNUNET_CONTAINER_multipeermap_get_multiple (p2a,
                                              &address->peer,
                                              &find_ai_cb,
                                              &fc);
  return fc.ret;
}


/**
 * Find matching address info, ignoring sessions.
 *
 * @param cls the `struct FindClosure`
 * @param key which peer is this about
 * @param value the `struct AddressInfo`
 * @return #GNUNET_YES to continue to iterate, #GNUNET_NO if we found the value
 */
static int
find_ai_no_session_cb (void *cls,
                       const struct GNUNET_PeerIdentity *key,
                       void *value)
{
  struct FindClosure *fc = cls;
  struct AddressInfo *ai = value;

  if (0 ==
      GNUNET_HELLO_address_cmp (fc->address,
                                ai->address))
  {
    fc->ret = ai;
    return GNUNET_NO;
  }
  return GNUNET_YES;
}


/**
 * Find the address information struct for the
 * given address (ignoring sessions)
 *
 * @param address address to look for
 * @return NULL if this combination is unknown
 */
static struct AddressInfo *
find_ai_no_session (const struct GNUNET_HELLO_Address *address)
{
  struct FindClosure fc;

  fc.address = address;
  fc.session = NULL;
  fc.ret = NULL;
  GNUNET_CONTAINER_multipeermap_get_multiple (p2a,
                                              &address->peer,
                                              &find_ai_no_session_cb,
                                              &fc);
  return fc.ret;
}


/**
 * Test if ATS knows about this address.
 *
 * @param address the address
 * @param session the session
 * @return #GNUNET_YES if address is known, #GNUNET_NO if not.
 */
int
GST_ats_is_known (const struct GNUNET_HELLO_Address *address,
                  struct Session *session)
{
  return (NULL != find_ai (address, session)) ? GNUNET_YES : GNUNET_NO;
}


/**
 * The blocking time for an address has expired, allow ATS to
 * suggest it again.
 *
 * @param cls the `struct AddressInfo` of the address to unblock
 * @param tc unused
 */
static void
unblock_address (void *cls,
                 const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct AddressInfo *ai = cls;

  ai->unblock_task = NULL;
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Unblocking address %s of peer %s\n",
       GST_plugins_a2s (ai->address),
       GNUNET_i2s (&ai->address->peer));
  ai->ar = GNUNET_ATS_address_add (GST_ats,
                                   ai->address,
                                   ai->session,
                                   &ai->properties);
  GNUNET_break (NULL != ai->ar);
  num_blocked--;
  publish_p2a_stat_update ();
}


/**
 * Temporarily block a valid address for use by ATS for address
 * suggestions.  This function should be called if an address was
 * suggested by ATS but failed to perform (i.e. failure to establish a
 * session or to exchange the PING/PONG).
 *
 * @param address the address to block
 * @param session the session (can be NULL)
 */
void
GST_ats_block_address (const struct GNUNET_HELLO_Address *address,
                       struct Session *session)
{
  struct AddressInfo *ai;

  ai = find_ai (address, session);
  if (NULL == ai)
  {
    GNUNET_assert (0);
    return;
  }
  if (NULL == ai->ar)
  {
    /* already blocked, how did it get used!? */
    GNUNET_break (0);
    return;
  }
  ai->back_off = GNUNET_TIME_STD_BACKOFF (ai->back_off);
  if (GNUNET_YES ==
      GNUNET_HELLO_address_check_option (address,
                                         GNUNET_HELLO_ADDRESS_INFO_INBOUND))
    LOG (GNUNET_ERROR_TYPE_DEBUG,
         "Removing address %s of peer %s from use (inbound died)\n",
         GST_plugins_a2s (address),
         GNUNET_i2s (&address->peer));
  else
    LOG (GNUNET_ERROR_TYPE_INFO,
         "Blocking address %s of peer %s from use for %s\n",
         GST_plugins_a2s (address),
         GNUNET_i2s (&address->peer),
         GNUNET_STRINGS_relative_time_to_string (ai->back_off,
                                                 GNUNET_YES));
  /* destroy session and address */
  if ( (NULL == session) ||
       (GNUNET_NO ==
        GNUNET_ATS_address_del_session (ai->ar,
                                        session)) )
    GNUNET_ATS_address_destroy (ai->ar);
  ai->ar = NULL;

  /* determine when the address should come back to life */
  ai->blocked = GNUNET_TIME_relative_to_absolute (ai->back_off);
  ai->unblock_task = GNUNET_SCHEDULER_add_delayed (ai->back_off,
                                                   &unblock_address,
                                                   ai);
  num_blocked++;
  publish_p2a_stat_update ();
}


/**
 * Reset address blocking time.  Resets the exponential
 * back-off timer for this address to zero.  Done when
 * an address was used to create a successful connection.
 *
 * @param address the address to reset the blocking timer
 * @param session the session (can be NULL)
 */
void
GST_ats_block_reset (const struct GNUNET_HELLO_Address *address,
                     struct Session *session)
{
  struct AddressInfo *ai;

  ai = find_ai (address, session);
  if (NULL == ai)
  {
    GNUNET_break (0);
    return;
  }
  /* address is in successful use, so it should not be blocked right now */
  GNUNET_break (NULL == ai->unblock_task);
  ai->back_off = GNUNET_TIME_UNIT_ZERO;
}


/**
 * Notify ATS about the a new inbound address. We may already
 * know the address (as this is called each time we receive
 * a message from an inbound connection).  If the address is
 * indeed new, make it available to ATS.
 *
 * @param address the address
 * @param session the session
 * @param prop performance information
 */
void
GST_ats_add_inbound_address (const struct GNUNET_HELLO_Address *address,
                             struct Session *session,
                             const struct GNUNET_ATS_Properties *prop)
{
  struct GNUNET_ATS_AddressRecord *ar;
  struct AddressInfo *ai;

  /* valid new address, let ATS know! */
  if (NULL == address->transport_name)
  {
    GNUNET_break(0);
    return;
  }
  GNUNET_assert (GNUNET_YES ==
                 GNUNET_HELLO_address_check_option (address,
                                                    GNUNET_HELLO_ADDRESS_INFO_INBOUND));
  GNUNET_assert (NULL != session);
  ai = find_ai (address, session);
  if (NULL != ai)
  {
    /* This should only be called for new sessions, and thus
       we should not already have the address */
    GNUNET_break (0);
    return;
  }
  GNUNET_break (GNUNET_ATS_NET_UNSPECIFIED != prop->scope);
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Notifying ATS about peer `%s''s new inbound address `%s' session %p in network %s\n",
       GNUNET_i2s (&address->peer),
       (0 == address->address_length)
       ? "<inbound>"
       : GST_plugins_a2s (address),
       session,
       GNUNET_ATS_print_network_type (prop->scope));
  ar = GNUNET_ATS_address_add (GST_ats,
                               address,
                               session,
                               prop);
  GNUNET_break (NULL != ar);
  ai = GNUNET_new (struct AddressInfo);
  ai->address = GNUNET_HELLO_address_copy (address);
  ai->session = session;
  ai->properties = *prop;
  ai->ar = ar;
  (void) GNUNET_CONTAINER_multipeermap_put (p2a,
                                            &ai->address->peer,
                                            ai,
                                            GNUNET_CONTAINER_MULTIHASHMAPOPTION_MULTIPLE);
  publish_p2a_stat_update ();
}


/**
 * Notify ATS about the new address including the network this address is
 * located in.  The address must NOT be inbound and must be new to ATS.
 *
 * @param address the address
 * @param prop performance information
 */
void
GST_ats_add_address (const struct GNUNET_HELLO_Address *address,
                     const struct GNUNET_ATS_Properties *prop)
{
  struct GNUNET_ATS_AddressRecord *ar;
  struct AddressInfo *ai;

  /* valid new address, let ATS know! */
  if (NULL == address->transport_name)
  {
    GNUNET_break(0);
    return;
  }
  GNUNET_assert (GNUNET_YES !=
                 GNUNET_HELLO_address_check_option (address,
                                                    GNUNET_HELLO_ADDRESS_INFO_INBOUND));
  ai = find_ai_no_session (address);
  GNUNET_assert (NULL == ai);
  LOG (GNUNET_ERROR_TYPE_INFO,
       "Notifying ATS about peer `%s''s new address `%s'\n",
       GNUNET_i2s (&address->peer),
       (0 == address->address_length)
       ? "<inbound>"
       : GST_plugins_a2s (address));
  ar = GNUNET_ATS_address_add (GST_ats,
                               address,
                               NULL,
                               prop);
  GNUNET_break (NULL != ar);
  ai = GNUNET_new (struct AddressInfo);
  ai->address = GNUNET_HELLO_address_copy (address);
  ai->ar = ar;
  ai->properties = *prop;
  (void) GNUNET_CONTAINER_multipeermap_put (p2a,
                                            &ai->address->peer,
                                            ai,
                                            GNUNET_CONTAINER_MULTIHASHMAPOPTION_MULTIPLE);
  publish_p2a_stat_update ();
}


/**
 * Notify ATS about a new session now existing for the given
 * address.
 *
 * @param address the address
 * @param session the session
 */
void
GST_ats_new_session (const struct GNUNET_HELLO_Address *address,
                     struct Session *session)
{
  struct AddressInfo *ai;

  ai = find_ai (address, NULL);
  if (NULL == ai)
  {
    /* We may already be aware of the session, even if some other part
       of the code could not tell if it just created a new session or
       just got one recycled from the plugin; hence, we may be called
       with "new" session even for an "old" session; in that case,
       check that this is the case, but just ignore it. */
    GNUNET_assert (NULL != (find_ai (address, session)));
    return;
  }
  GNUNET_break (NULL == ai->session);
  ai->session = session;
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Telling ATS about new session for peer %s\n",
       GNUNET_i2s (&address->peer));
  if (NULL != ai->ar)
    GNUNET_ATS_address_add_session (ai->ar,
                                    session);
}


/**
 * Notify ATS that the session (but not the address) of
 * a given address is no longer relevant.
 *
 * @param address the address
 * @param session the session
 */
void
GST_ats_del_session (const struct GNUNET_HELLO_Address *address,
                     struct Session *session)
{
  struct AddressInfo *ai;

  if (NULL == session)
  {
    GNUNET_break (0);
    return;
  }
  ai = find_ai (address,
                session);
  if (NULL == ai)
  {
    /* We sometimes create sessions just for sending a PING,
       and if those are destroyed they were never known to
       ATS which means we end up here (however, in this
       case, the address must be an outbound address). */
    GNUNET_break (GNUNET_YES !=
                  GNUNET_HELLO_address_check_option (address,
                                                     GNUNET_HELLO_ADDRESS_INFO_INBOUND));

    return;
  }
  GNUNET_assert (session == ai->session);
  ai->session = NULL;
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Telling ATS to destroy session %p from peer %s\n",
       session,
       GNUNET_i2s (&address->peer));
  if (NULL == ai->ar)
  {
    /* If ATS doesn't know about the address/session, and this was an
       inbound session or one that expired, then we must forget about
       the address as well.  Otherwise, we are done as we have set
       `ai->session` to NULL already. */
    if ( (GNUNET_YES == ai->expired) ||
         (GNUNET_YES ==
          GNUNET_HELLO_address_check_option (address,
                                             GNUNET_HELLO_ADDRESS_INFO_INBOUND)) )
      GST_ats_expire_address (address);
    return;
  }
  if (GNUNET_YES ==
      GNUNET_ATS_address_del_session (ai->ar,
                                      session))
  {
    ai->ar = NULL;
    GST_ats_expire_address (address);
  }
}


/**
 * Notify ATS about DV distance change to an address's.
 *
 * @param address the address
 * @param distance new distance value
 */
void
GST_ats_update_distance (const struct GNUNET_HELLO_Address *address,
                         uint32_t distance)
{
  struct AddressInfo *ai;

  ai = find_ai_no_session (address);
  if (NULL == ai)
    return;
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Updated distance for peer `%s' to %u\n",
       GNUNET_i2s (&address->peer),
       distance);
  ai->properties.distance = distance;
  GST_manipulation_manipulate_metrics (address,
                                       ai->session,
                                       &ai->properties);
  if (NULL != ai->ar)
    GNUNET_ATS_address_update (ai->ar,
                               &ai->properties);
}


/**
 * Notify ATS about property changes to an address's properties.
 *
 * @param address the address
 * @param delay new delay value
 */
void
GST_ats_update_delay (const struct GNUNET_HELLO_Address *address,
                      struct GNUNET_TIME_Relative delay)
{
  struct AddressInfo *ai;

  ai = find_ai_no_session (address);
  if (NULL == ai)
    return;
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Updated latency for peer `%s' to %s\n",
       GNUNET_i2s (&address->peer),
       GNUNET_STRINGS_relative_time_to_string (delay,
                                               GNUNET_YES));
  ai->properties.delay = delay;
  GST_manipulation_manipulate_metrics (address,
                                       ai->session,
                                       &ai->properties);
  if (NULL != ai->ar)
    GNUNET_ATS_address_update (ai->ar,
                               &ai->properties);
}


/**
 * Notify ATS about utilization changes to an address.
 *
 * @param address our information about the address
 * @param bps_in new utilization inbound
 * @param bps_out new utilization outbound
 */
void
GST_ats_update_utilization (const struct GNUNET_HELLO_Address *address,
                            uint32_t bps_in,
                            uint32_t bps_out)
{
  struct AddressInfo *ai;

  ai = find_ai_no_session (address);
  if (NULL == ai)
    return;
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Updating utilization for peer `%s' address %s: %u/%u\n",
       GNUNET_i2s (&address->peer),
       GST_plugins_a2s (address),
       (unsigned int) bps_in,
       (unsigned int) bps_out);
  ai->properties.utilization_in = bps_in;
  ai->properties.utilization_out = bps_out;
  GST_manipulation_manipulate_metrics (address,
                                       ai->session,
                                       &ai->properties);
  if (NULL != ai->ar)
    GNUNET_ATS_address_update (ai->ar,
                               &ai->properties);
}


/**
 * Notify ATS that the address has expired and thus cannot
 * be used any longer.  This function must only be called
 * if the corresponding session is already gone.
 *
 * @param address the address
 */
void
GST_ats_expire_address (const struct GNUNET_HELLO_Address *address)
{
  struct AddressInfo *ai;

  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Address %s of peer %s expired\n",
       GST_plugins_a2s (address),
       GNUNET_i2s (&address->peer));
  ai = find_ai_no_session (address);
  if (NULL == ai)
  {
    GNUNET_assert (0);
    return;
  }
  if (NULL != ai->unblock_task)
  {
    GNUNET_SCHEDULER_cancel (ai->unblock_task);
    ai->unblock_task = NULL;
    num_blocked--;
  }
  if (NULL != ai->session)
  {
    ai->expired = GNUNET_YES;
    if (NULL != ai->ar)
    {
      GNUNET_ATS_address_destroy (ai->ar);
      ai->ar = NULL;
    }
    return;
  }
  GNUNET_assert (GNUNET_YES ==
                 GNUNET_CONTAINER_multipeermap_remove (p2a,
                                                       &address->peer,
                                                       ai));
  LOG (GNUNET_ERROR_TYPE_DEBUG,
       "Telling ATS to destroy address from peer %s\n",
       GNUNET_i2s (&address->peer));
  if (NULL != ai->ar)
  {
    /* We usually should not have a session here when we
       expire an address, but during shutdown a session
       may be active while validation causes the address
       to 'expire'.  So clean up both if necessary. */
    if ( (NULL == ai->session) ||
         (GNUNET_NO ==
          GNUNET_ATS_address_del_session (ai->ar,
                                          ai->session)) )
      GNUNET_ATS_address_destroy (ai->ar);
    ai->ar = NULL;
  }
  publish_p2a_stat_update ();
  GNUNET_HELLO_address_free (ai->address);
  GNUNET_free (ai);
}


/**
 * Initialize ATS subsystem.
 */
void
GST_ats_init ()
{
  p2a = GNUNET_CONTAINER_multipeermap_create (4, GNUNET_YES);
}


/**
 * Release memory used by the given address data.
 *
 * @param cls NULL
 * @param key which peer is this about
 * @param value the `struct AddressInfo`
 * @return #GNUNET_OK (continue to iterate)
 */
static int
destroy_ai (void *cls,
            const struct GNUNET_PeerIdentity *key,
            void *value)
{
  struct AddressInfo *ai = value;

  GNUNET_assert (GNUNET_YES ==
                 GNUNET_CONTAINER_multipeermap_remove (p2a,
                                                       key,
                                                       ai));
  if (NULL != ai->unblock_task)
  {
    GNUNET_SCHEDULER_cancel (ai->unblock_task);
    ai->unblock_task = NULL;
    num_blocked--;
  }
  if (NULL != ai->ar)
  {
    GNUNET_ATS_address_destroy (ai->ar);
    ai->ar = NULL;
  }
  GNUNET_HELLO_address_free (ai->address);
  GNUNET_free (ai);
  return GNUNET_OK;
}


/**
 * Shutdown ATS subsystem.
 */
void
GST_ats_done ()
{
  GNUNET_CONTAINER_multipeermap_iterate (p2a,
                                         &destroy_ai,
                                         NULL);
  publish_p2a_stat_update ();
  GNUNET_CONTAINER_multipeermap_destroy (p2a);
  p2a = NULL;
}

/* end of gnunet-service-transport_ats.c */
