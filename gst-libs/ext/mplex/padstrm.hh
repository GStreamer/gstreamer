
/*
 *  padstrm.hh:  Padding stream pseudo input-streamsin
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

#ifndef __PADSTRM_H__
#define __PADSTRM_H__

#include "inputstrm.hh"


class PaddingStream:public MuxStream
{
public:
  PaddingStream ()
  {
    MuxStream::Init (PADDING_STR, 0, 0, 0, false, false);
  }

  unsigned int ReadPacketPayload (uint8_t * dst, unsigned int to_read);
};

class VCDAPadStream:public MuxStream
{
public:
  VCDAPadStream ()
  {
    Init (PADDING_STR, 0, 0, 20, false, false);

  }

  unsigned int ReadPacketPayload (uint8_t * dst, unsigned int to_read);
};

class DVDPriv2Stream:public MuxStream
{
public:
  DVDPriv2Stream ()
  {
    Init (PRIVATE_STR_2, 0, 0, 0, false, false);

  }

  unsigned int ReadPacketPayload (uint8_t * dst, unsigned int to_read);
};


#endif // __PADSTRM_H__


/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
