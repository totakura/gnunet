/*
     This file is part of GNUnet.
     Copyright (C) 2009 Christian Grothoff (and other contributing authors)

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
 * @file util/test_server_with_client_unix.c
 * @brief tests for server.c and client.c,
 *       specifically disconnect_notify,
 *       client_get_address and receive_done (resume processing)
 */
#include "platform.h"
#include "gnunet_util_lib.h"

#define MY_TYPE 128


static struct GNUNET_SERVER_Handle *server;

static struct GNUNET_CLIENT_Connection *client;

static struct GNUNET_CONFIGURATION_Handle *cfg;

static int ok;


static void
send_done (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct GNUNET_SERVER_Client *argclient = cls;

  GNUNET_assert (ok == 3);
  ok++;
  GNUNET_SERVER_receive_done (argclient, GNUNET_OK);
}


static void
recv_cb (void *cls, struct GNUNET_SERVER_Client *argclient,
         const struct GNUNET_MessageHeader *message)
{
  switch (ok)
  {
  case 2:
    ok++;
    GNUNET_SCHEDULER_add_delayed (GNUNET_TIME_relative_multiply
                                  (GNUNET_TIME_UNIT_MILLISECONDS, 50),
                                  &send_done, argclient);
    break;
  case 4:
    ok++;
    GNUNET_CLIENT_disconnect (client);
    GNUNET_SERVER_receive_done (argclient, GNUNET_OK);
    break;
  default:
    GNUNET_assert (0);
  }

}


static void
clean_up (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  GNUNET_SERVER_destroy (server);
  server = NULL;
  GNUNET_CONFIGURATION_destroy (cfg);
  cfg = NULL;
}


/**
 * Functions with this signature are called whenever a client
 * is disconnected on the network level.
 *
 * @param cls closure
 * @param client identification of the client
 */
static void
notify_disconnect (void *cls, struct GNUNET_SERVER_Client *client)
{
  if (client == NULL)
    return;
  GNUNET_assert (ok == 5);
  ok = 0;
  GNUNET_SCHEDULER_add_now (&clean_up, NULL);
}


static size_t
notify_ready (void *cls, size_t size, void *buf)
{
  struct GNUNET_MessageHeader *msg;

  GNUNET_assert (size >= 256);
  GNUNET_assert (1 == ok);
  ok++;
  msg = buf;
  msg->type = htons (MY_TYPE);
  msg->size = htons (sizeof (struct GNUNET_MessageHeader));
  msg++;
  msg->type = htons (MY_TYPE);
  msg->size = htons (sizeof (struct GNUNET_MessageHeader));
  return 2 * sizeof (struct GNUNET_MessageHeader);
}


static struct GNUNET_SERVER_MessageHandler handlers[] = {
  {&recv_cb, NULL, MY_TYPE, sizeof (struct GNUNET_MessageHeader)},
  {NULL, NULL, 0, 0}
};


static void
task (void *cls, const struct GNUNET_SCHEDULER_TaskContext *tc)
{
  struct sockaddr_un un;
  const char *unixpath = "/tmp/testsock";
  struct sockaddr *sap[2];
  socklen_t slens[2];

  memset (&un, 0, sizeof (un));
  un.sun_family = AF_UNIX;
  strncpy(un.sun_path, unixpath, sizeof (un.sun_path) - 1);
#if HAVE_SOCKADDR_IN_SIN_LEN
  un.sun_len = (u_char) sizeof (un);
#endif

  sap[0] = (struct sockaddr *) &un;
  slens[0] = sizeof (un);
  sap[1] = NULL;
  slens[1] = 0;
  server =
      GNUNET_SERVER_create (NULL, NULL, sap, slens,
                            GNUNET_TIME_relative_multiply
                            (GNUNET_TIME_UNIT_MILLISECONDS, 250), GNUNET_NO);
  GNUNET_assert (server != NULL);
  handlers[0].callback_cls = cls;
  GNUNET_SERVER_add_handlers (server, handlers);
  GNUNET_SERVER_disconnect_notify (server, &notify_disconnect, cls);
  cfg = GNUNET_CONFIGURATION_create ();

  GNUNET_CONFIGURATION_set_value_string (cfg, "test", "UNIXPATH", unixpath);
  GNUNET_CONFIGURATION_set_value_string (cfg, "resolver", "HOSTNAME",
                                         "localhost");

  client = GNUNET_CLIENT_connect ("test", cfg);
  GNUNET_assert (client != NULL);
  GNUNET_CLIENT_notify_transmit_ready (client, 256,
                                       GNUNET_TIME_relative_multiply
                                       (GNUNET_TIME_UNIT_MILLISECONDS, 250),
                                       GNUNET_NO, &notify_ready, NULL);
}


int
main (int argc, char *argv[])
{
  GNUNET_log_setup ("test_server_with_client_unix",
                    "WARNING",
                    NULL);
  ok = 1;
  GNUNET_SCHEDULER_run (&task, NULL);
  return ok;
}

/* end of test_server_with_client_unix.c */
