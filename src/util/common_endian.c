/*
     This file is part of GNUnet.
     (C) 2001, 2002, 2003, 2004, 2006 Christian Grothoff (and other contributing authors)

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
 * @file util/common_endian.c
 * @brief endian conversion helpers
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_common.h"

#define LOG(kind,...) GNUNET_log_from (kind, "util",__VA_ARGS__)

uint64_t
GNUNET_ntohll (uint64_t n)
{
#if __BYTE_ORDER == __BIG_ENDIAN
  return n;
#else
  return (((uint64_t) ntohl (n)) << 32) + ntohl (n >> 32);
#endif
}

uint64_t
GNUNET_htonll (uint64_t n)
{
#if __BYTE_ORDER == __BIG_ENDIAN
  return n;
#else
  return (((uint64_t) htonl (n)) << 32) + htonl (n >> 32);
#endif
}


double 
GNUNET_hton_double (double d) 
{
  double res;
  unsigned int *in = (unsigned int *) &d;
  unsigned int *out = (unsigned int *) &res;

  out[0] = htonl(in[0]);
  out[1] = htonl(in[1]);
 
  return res;
}


double 
GNUNET_ntoh_double (double d) 
{
  double res;
  unsigned int *in = (unsigned int *) &d;
  unsigned int *out = (unsigned int *) &res;

  out[0] = ntohl(in[0]);
  out[1] = ntohl(in[1]);
 
  return res;
}



/* end of common_endian.c */
