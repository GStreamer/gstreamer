
/*
 *  padstrm.cc:  Padding stream pseudo-input streams
 *
 *  Copyright (C) 2001 Andrew Stevens <andrew.stevens@philips.com>
 *
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of version 2 of the GNU General Public License
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */


#include "padstrm.hh"



//
// Generator for padding packets in a padding stream...
//


unsigned int
PaddingStream::ReadPacketPayload (uint8_t * dst, unsigned int to_read)
{
  memset (dst, STUFFING_BYTE, to_read);
  return to_read;
}

unsigned int
VCDAPadStream::ReadPacketPayload (uint8_t * dst, unsigned int to_read)
{
  memset (dst, STUFFING_BYTE, to_read);
  return to_read;
}

unsigned int
DVDPriv2Stream::ReadPacketPayload (uint8_t * dst, unsigned int to_read)
{
  memset (dst, 0, to_read);
  return to_read;
}

/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
