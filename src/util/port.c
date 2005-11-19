/*
     This file is part of GNUnet

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
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
 * @file util/port.c
 * @brief functions for GNUnet clients to establish connection with gnunetd
 * @author Christian Grothoff
 */

#include "gnunet_util.h"
#include "platform.h"

/**
 * Return the port-number (in host byte order)
 */
unsigned short getGNUnetPort() {
  static unsigned short port;
  const char *setting;

  if (port != 0)
    return port;
  if (testConfigurationString("GNUNETD",
			      "_MAGIC_",
			      "YES"))
    setting = "PORT";
  else
    setting = "CLIENT-PORT";

  port = (unsigned short) getConfigurationInt("NETWORK",
					      setting);
  if (port == 0) {
    errexit(_("Cannot determine port of gnunetd server. "
	      "Define in configuration file in section `%s' under `%s'.\n"),
	    "NETWORK",
	    setting);
  }
  return port;
}

/**
 * Configuration: get the GNUnetd host where the client
 * should connect to (via TCP)
 * @return the name of the host
 */
static const char * getGNUnetdHost() {
  static char * res;

  if (res != NULL)
    return res;
  res = getConfigurationString("NETWORK",
			       "HOST");
  if (res == NULL)
    res = "localhost";
  return res;
}

/**
 * Get a GNUnet TCP socket that is connected to gnunetd.
 */
GNUNET_TCP_SOCKET * getClientSocket() {
  GNUNET_TCP_SOCKET * sock;
  const char * host;

  sock = MALLOC(sizeof(GNUNET_TCP_SOCKET));
  host = getGNUnetdHost();
  if (SYSERR == initGNUnetClientSocket(getGNUnetPort(),
				       host,
				       sock)) {
    LOG(LOG_ERROR,
	_("Could not connect to gnunetd.\n"));
    FREE(sock);
    return NULL;
  }
  return sock;
}

/**
 * Free a Client socket.
 */
void releaseClientSocket(GNUNET_TCP_SOCKET * sock) {
  if (sock != NULL) {
    destroySocket(sock);
    FREE(sock);
  }
}

/* end of port.c */
