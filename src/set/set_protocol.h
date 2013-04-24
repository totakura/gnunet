/*
     This file is part of GNUnet.
     (C) 2013 Christian Grothoff (and other contributing authors)

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
 * @author Florian Dold
 * @file set/set_protocol.h
 * @brief Peer-to-Peer messages for gnunet set
 */
#ifndef SET_PROTOCOL_H
#define SET_PROTOCOL_H

#include "platform.h"
#include "gnunet_common.h"


GNUNET_NETWORK_STRUCT_BEGIN

struct OperationRequestMessage
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_SET_P2P_OPERATION_REQUEST
   */
  struct GNUNET_MessageHeader header;

  /**
   * Operation to request, values from 'enum GNUNET_SET_OperationType'
   */
  uint32_t operation;

  /**
   * Application-specific identifier of the request.
   */
  struct GNUNET_HashCode app_id;

  /* rest: optional message */
};

struct ElementRequestMessage
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_SET_P2P_ELEMENT_REQUESTS
   */
  struct GNUNET_MessageHeader header;

  /**
   * Salt the keys in the body use
   */
  uint8_t salt;
};


struct IBFMessage
{
  /**
   * Type: GNUNET_MESSAGE_TYPE_SET_P2P_IBF
   */
  struct GNUNET_MessageHeader header;

  /**
   * Order of the whole ibf, where
   * num_buckets = 2^order
   */
  uint8_t order;

  /**
   * Salt used when hashing elements for this IBF.
   */
  uint8_t salt;

  /**
   * Offset of the strata in the rest of the message
   */
  uint16_t offset GNUNET_PACKED;

  /* rest: strata */
};


GNUNET_NETWORK_STRUCT_END

#endif
