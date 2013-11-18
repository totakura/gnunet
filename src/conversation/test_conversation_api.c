/*
     This file is part of GNUnet.
     (C) 2013 Christian Grothoff (and other contributing authors)

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
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/
/**
 * @file conversation/test_conversation_api.c
 * @brief testcase for conversation_api.c
 *
 * This test performs the operations of a call to a phone
 * where the phone user picks up and then the call is
 * terminated by the party that initiated the call.  The
 * actual transmission of voice data is not tested.
 */
#include "platform.h"
#include "gnunet_util_lib.h"
#include "gnunet_testing_lib.h"
#include "gnunet_gnsrecord_lib.h"
#include "gnunet_conversation_service.h"
#include "gnunet_identity_service.h"
#include "gnunet_namestore_service.h"

static int ok = 1;

static const struct GNUNET_CONFIGURATION_Handle *cfg;

static struct GNUNET_IDENTITY_Handle *id;

static struct GNUNET_IDENTITY_Operation *op;

static struct GNUNET_CONVERSATION_Phone *phone;

static struct GNUNET_NAMESTORE_Handle *ns;

static struct GNUNET_CONVERSATION_Call *call;

static struct GNUNET_NAMESTORE_QueueEntry *qe;

static struct GNUNET_CONVERSATION_Caller *active_caller;

static char *gns_name;

static char *gns_caller_id;


static int
enable_speaker (void *cls)
{
  const char *origin = cls;

  fprintf (stderr,
           "Speaker %s enabled\n",
           origin);
  return GNUNET_OK;
}


static void
disable_speaker (void *cls)
{
  const char *origin = cls;

  fprintf (stderr,
           "Speaker %s disabled\n",
           origin);
}


static void
play (void *cls,
      size_t data_size,
      const void *data)
{
  const char *origin = cls;

  fprintf (stderr,
           "Speaker %s plays %u bytes\n",
           origin,
           (unsigned int) data_size);
}


static void
destroy_speaker (void *cls)
{
  const char *origin = cls;

  fprintf (stderr, "Speaker %s destroyed\n", origin);
}


static struct GNUNET_SPEAKER_Handle caller_speaker = {
  &enable_speaker,
  &play,
  &disable_speaker,
  &destroy_speaker,
  "caller"
};


static struct GNUNET_SPEAKER_Handle phone_speaker = {
  &enable_speaker,
  &play,
  &disable_speaker,
  &destroy_speaker,
  "caller"
};


static int
enable_mic (void *cls,
            GNUNET_MICROPHONE_RecordedDataCallback rdc,
            void *rdc_cls)
{
  const char *origin = cls;

  fprintf (stderr,
           "Mic %s enabled\n",
           origin);
  return GNUNET_OK;
}


static void
disable_mic (void *cls)
{
  const char *origin = cls;

  fprintf (stderr,
           "Mic %s disabled\n",
           origin);
}


static void
destroy_mic (void *cls)
{
  const char *origin = cls;

  fprintf (stderr,
           "Mic %s destroyed\n",
           origin);
}


static struct GNUNET_MICROPHONE_Handle caller_mic = {
  &enable_mic,
  &disable_mic,
  &destroy_mic,
  "caller"
};


static struct GNUNET_MICROPHONE_Handle phone_mic = {
  &enable_mic,
  &disable_mic,
  &destroy_mic,
  "caller"
};


/**
 * Signature of the main function of a task.
 *
 * @param cls closure
 * @param tc context information (why was this task triggered now)
 */
static void
end_test (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_SCHEDULER_shutdown ();
  if (NULL != op)
  {
    GNUNET_IDENTITY_cancel (op);
    op = NULL;
  }
  if (NULL != call)
  {
    GNUNET_CONVERSATION_call_stop (call);
    call = NULL;
  }
  if (NULL != phone)
  {
    GNUNET_log (GNUNET_ERROR_TYPE_DEBUG, "Disconnecting from PHONE service.\n");
    GNUNET_CONVERSATION_phone_destroy (phone);
    phone = NULL;
  }
  if (NULL != id)
  {
    GNUNET_IDENTITY_disconnect (id);
    id = NULL;
  }
  if (NULL != qe)
  {
    GNUNET_NAMESTORE_cancel (qe);
    qe = NULL;
  }
  if (NULL != ns)
  {
    GNUNET_NAMESTORE_disconnect (ns);
    ns = NULL;
  }
}


static void
caller_event_handler (void *cls,
                      enum GNUNET_CONVERSATION_CallerEventCode code)
{
  switch (code)
  {
  case GNUNET_CONVERSATION_EC_CALLER_SUSPEND:
  case GNUNET_CONVERSATION_EC_CALLER_RESUME:
    fprintf (stderr, "Unexpected caller code: %d\n", code);
    break;
  }
}


static void
phone_event_handler (void *cls,
                     enum GNUNET_CONVERSATION_PhoneEventCode code,
                     struct GNUNET_CONVERSATION_Caller *caller,
                     const char *caller_id)
{
  static enum GNUNET_CONVERSATION_PhoneEventCode expect
    = GNUNET_CONVERSATION_EC_PHONE_RING;

  GNUNET_break (0 == strcmp (caller_id,
                             gns_caller_id));
  GNUNET_break (code == expect);
  switch (code)
  {
  case GNUNET_CONVERSATION_EC_PHONE_RING:
    active_caller = caller;
    GNUNET_CONVERSATION_caller_pick_up (caller,
                                        &caller_event_handler,
                                        NULL,
                                        &phone_speaker,
                                        &phone_mic);
    expect = GNUNET_CONVERSATION_EC_PHONE_HUNG_UP;
    break;
  case GNUNET_CONVERSATION_EC_PHONE_HUNG_UP:
    GNUNET_break (caller == active_caller);
    active_caller = NULL;
    ok = 0;
    GNUNET_SCHEDULER_shutdown ();
    break;
  default:
    fprintf (stderr, "Unexpected phone code: %d\n", code);
    break;
  }
}


static void
call_event_handler (void *cls,
                    enum GNUNET_CONVERSATION_CallEventCode code)
{
  static enum GNUNET_CONVERSATION_CallEventCode expect
    = GNUNET_CONVERSATION_EC_CALL_RINGING;

  GNUNET_break (code == expect);
  switch (code)
  {
  case GNUNET_CONVERSATION_EC_CALL_RINGING:
    expect = GNUNET_CONVERSATION_EC_CALL_PICKED_UP;
    break;
  case GNUNET_CONVERSATION_EC_CALL_PICKED_UP:
    expect = -1;
    GNUNET_CONVERSATION_call_stop (call);
    call = NULL;
    break;
  case GNUNET_CONVERSATION_EC_CALL_GNS_FAIL:
  case GNUNET_CONVERSATION_EC_CALL_HUNG_UP:
  case GNUNET_CONVERSATION_EC_CALL_SUSPENDED:
  case GNUNET_CONVERSATION_EC_CALL_RESUMED:
    fprintf (stderr, "Unexpected call code: %d\n", code);
    break;
  }
}


static void
caller_ego_create_cont (void *cls,
                        const char *emsg)
{
  op = NULL;
  GNUNET_assert (NULL == emsg);
}


static void
namestore_put_cont (void *cls,
                    int32_t success,
                    const char *emsg)
{
  qe = NULL;
  GNUNET_assert (GNUNET_YES == success);
  GNUNET_assert (NULL == emsg);
  GNUNET_assert (NULL == op);
  op = GNUNET_IDENTITY_create (id, "caller-ego", &caller_ego_create_cont, NULL);
}


static void
identity_cb (void *cls,
             struct GNUNET_IDENTITY_Ego *ego,
             void **ctx,
             const char *name)
{
  struct GNUNET_GNSRECORD_Data rd;
  struct GNUNET_CRYPTO_EcdsaPublicKey pub;

  if (NULL == name)
    return;
  if (NULL == ego)
    return;
  if (0 == strcmp (name, "phone-ego"))
  {
    GNUNET_IDENTITY_ego_get_public_key (ego, &pub);
    GNUNET_asprintf (&gns_name,
                     "phone.%s",
                     GNUNET_GNSRECORD_pkey_to_zkey (&pub));
    phone = GNUNET_CONVERSATION_phone_create (cfg,
                                              ego,
                                              &phone_event_handler,
                                              NULL);
    GNUNET_assert (NULL != phone);
    memset (&rd, 0, sizeof (rd));
    GNUNET_CONVERSATION_phone_get_record (phone,
                                          &rd);
    GNUNET_assert (rd.record_type == GNUNET_GNSRECORD_TYPE_PHONE);
    rd.expiration_time = UINT64_MAX;
    qe = GNUNET_NAMESTORE_records_store (ns,
                                         GNUNET_IDENTITY_ego_get_private_key (ego),
                                         "phone" /* GNS label */,
                                         1,
                                         &rd,
                                         &namestore_put_cont,
                                         NULL);
    return;
  }
  if (0 == strcmp (name, "caller-ego"))
  {
    GNUNET_IDENTITY_ego_get_public_key (ego, &pub);
    GNUNET_asprintf (&gns_caller_id,
                     "%s",
                     GNUNET_GNSRECORD_pkey_to_zkey (&pub));
    call = GNUNET_CONVERSATION_call_start (cfg,
                                           ego,
                                           gns_name,
                                           &caller_speaker,
                                           &caller_mic,
                                           &call_event_handler,
                                           NULL);
    return;
  }
}


static void
phone_ego_create_cont (void *cls,
                       const char *emsg)
{
  op = NULL;
  GNUNET_assert (NULL == emsg);
}


static void
run (void *cls,
     const struct GNUNET_CONFIGURATION_Handle *c,
     struct GNUNET_TESTING_Peer *peer)
{
  cfg = c;
  GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_relative_multiply
                                (GNUNET_TIME_UNIT_MINUTES, 1), &end_test,
                                NULL);
  id = GNUNET_IDENTITY_connect (cfg,
                                &identity_cb,
                                NULL);
  op = GNUNET_IDENTITY_create (id, "phone-ego", &phone_ego_create_cont, NULL);
  ns = GNUNET_NAMESTORE_connect (cfg);
}


int
main (int argc, char *argv[])
{
  if (0 != GNUNET_TESTING_peer_run ("test_conversation_api",
				    "test_conversation.conf",
				    &run, NULL))
    return 1;
  return ok;
}

/* end of test_conversation_api.c */
