
/*
 *  buffer.hh:  Classes for decoder buffer models for mux despatch
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


#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <config.h>
#include "aunit.hh"

class BufferQueue
{
#include <config.h>
#
public:
  unsigned int size;		/* als verkettete Liste implementiert   */
  clockticks DTS;
  BufferQueue *next;
};


class BufferModel
{
public:
  BufferModel ():max_size (0), first (0)
  {
  }
  void Init (unsigned int size);

  void Cleaned (clockticks timenow);
  clockticks NextChange ();
  void Flushed ();
  unsigned int Space ();
  void Queued (unsigned int bytes, clockticks removaltime);
  inline unsigned int Size ()
  {
    return max_size;
  }
private:
  unsigned int max_size;
  BufferQueue *first;
};



#endif // __BUFFER_H__


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
