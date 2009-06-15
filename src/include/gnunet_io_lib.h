/*
     This file is part of GNUnet.
     (C) 2009 Christian Grothoff (and other contributing authors)

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
 * @file include/gnunet_io_lib.h
 * @brief Helper functions for abstract IO handles
 * @author Nils Durner
 */

#ifndef IO_HANDLE_H_
#define IO_HANDLE_H_

struct GNUNET_IO_Handle;

/**
 * Checks whether a handle is invalid
 * @param h handle to check
 * @return GNUNET_YES if invalid, GNUNET_NO if valid
 */
int GNUNET_IO_handle_invalid (const struct GNUNET_IO_Handle *h);

/**
 * Mark a handle as invalid
 * @param h file handle
 */
void GNUNET_IO_handle_invalidate (struct GNUNET_IO_Handle *h);


#endif /* IO_HANDLE_H_ */
