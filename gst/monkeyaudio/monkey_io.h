/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __MONKEY_IO_H__
#define __MONKEY_IO_H__

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>

#include "libmonkeyaudio/All.h"
#include "libmonkeyaudio/GlobalFunctions.h"
#include "libmonkeyaudio/MACLib.h"
#include "libmonkeyaudio/IO.h"

class sinkpad_CIO : public CIO
{
public :
  GstByteStream *bs;
  guint64 position;
  GstPad *sinkpad;

  gboolean eos;
  gboolean need_discont;
 
  sinkpad_CIO();
  int Open(const char * pName);
  int Close();
  int Read(void * pBuffer, unsigned int nBytesToRead, unsigned int * pBytesRead);
  int Write(const void*, unsigned int, unsigned   int*);
  int Seek(int nDistance, unsigned int nMoveMode);
  int Create(const char * pName);
  int Delete();
  int SetEOF();
  int GetPosition();
  int GetSize();
  int GetName(char * pBuffer);
};


class srcpad_CIO : public CIO
{
public :
  GstPad *srcpad;
  guint64 position;
  APE_HEADER *header;
 
  srcpad_CIO();
  int Open(const char * pName);
  int Close();
  int Read(void * pBuffer, unsigned int nBytesToRead, unsigned int * pBytesRead);
  int Write(const void*, unsigned int, unsigned   int*);
  int Seek(int nDistance, unsigned int nMoveMode);
  int Create(const char * pName);
  int Delete();
  int SetEOF();
  int GetPosition();
  int GetSize();
  int GetName(char * pBuffer);
};

#endif
