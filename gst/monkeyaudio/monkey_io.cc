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


#include <string.h>
#include <fcntl.h>
#include "monkey_io.h"

sinkpad_CIO::sinkpad_CIO (void)
{
  return;
}

int sinkpad_CIO::Open (const char * pName)
{
  position = 0;
  return 1;
}

int sinkpad_CIO::GetName (char * pBuffer)
{
  strcpy (pBuffer, "");
  return 0;
}

int sinkpad_CIO::GetSize ()
{
  return gst_bytestream_length (bs);
}

int sinkpad_CIO::GetPosition ()
{
  return gst_bytestream_tell (bs);
}

int sinkpad_CIO::SetEOF ()
{
  /* FIXME, hack, pull final EOS from peer */
  gst_bytestream_flush (bs, 1);
  return 0;
}

int sinkpad_CIO::Close ()
{
  return 0;
}

int sinkpad_CIO::Read(void * pBuffer, unsigned int nBytesToRead, unsigned int * pBytesRead)
{
  guint insize = 0;
  guint8 *indata;

  while (insize == 0) {
    insize = gst_bytestream_peek_bytes (bs, &indata, nBytesToRead);
    if (insize < nBytesToRead) {
      GstEvent *event;
      guint32 avail;
			    
      gst_bytestream_get_status (bs, &avail, &event);

      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_EOS:
          GST_DEBUG (0, "eos");
          eos = TRUE; 
	        gst_event_unref (event);
          if (avail == 0) {
            return 0;
          }
          break;
        case GST_EVENT_DISCONTINUOUS:
          GST_DEBUG (0, "discont");
   	      /* we are not yet sending the discont, we'll do that in the next write operation */
	        need_discont = TRUE;
	        gst_event_unref (event);
	        break;
        default:
	        gst_pad_event_default (sinkpad, event);
          break;
      }

      if (avail > 0)
        insize = gst_bytestream_peek_bytes (bs, &indata, avail);
      else
        insize = 0;
    }
  }

  memcpy (pBuffer, indata, insize);
  *pBytesRead = insize;
  gst_bytestream_flush_fast (bs, insize);

  if (*pBytesRead == nBytesToRead)
    return 0;
  else
    return 1;
}


int sinkpad_CIO::Write (const void*, unsigned int, unsigned   int*)
{
  return 0;
}

int sinkpad_CIO::Seek (int nDistance, unsigned int nMoveMode)
{
GstSeekType seek_type;

  switch (nMoveMode)
  {
    case FILE_BEGIN :
      seek_type = GST_SEEK_METHOD_SET;
      break;
    case FILE_CURRENT :
      seek_type = GST_SEEK_METHOD_CUR;
      break;
    case FILE_END :
      seek_type = GST_SEEK_METHOD_END;
      break;
    default :
      g_print ("wrong seek type\n");
      return -1;
      break;
  }

  if (gst_bytestream_seek (bs, nDistance, seek_type))
      return 0;

  return -1;
}

int sinkpad_CIO::Create (const char * pName) 
{ 
  return 0;
}

int sinkpad_CIO::Delete ()
{
  return 0;
}




srcpad_CIO::srcpad_CIO (void)
{
  return;
}

int srcpad_CIO::Open (const char * pName)
{
  position = 0;
  return 0;
}

int srcpad_CIO::GetName (char * pBuffer)
{
  strcpy (pBuffer, "");
  return 0;
}

int srcpad_CIO::GetSize ()
{
  return 0;
}

int srcpad_CIO::GetPosition ()
{
  return position;
}

int srcpad_CIO::SetEOF()
{
  GstEvent *event;

  event = gst_event_new (GST_EVENT_EOS);
  gst_pad_push (srcpad, GST_BUFFER (event));

  return 0;
}

int srcpad_CIO::Close ()
{
  return 0;
}

int srcpad_CIO::Read (void * pBuffer, unsigned int nBytesToRead, unsigned int * pBytesRead)
{

  memcpy (pBuffer, header, nBytesToRead);
  *pBytesRead = nBytesToRead;

  return 0;
}

int srcpad_CIO::Write (const void * data, unsigned int nBytesToWrite, unsigned int * pBytesWritten)
{
  GstBuffer *buffer;
  
  /* Save the header for future use */
  if (position == 0)
    header = (APE_HEADER *) g_memdup (data, nBytesToWrite);

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = (guint8 *) g_memdup (data, nBytesToWrite);
  GST_BUFFER_SIZE (buffer) = nBytesToWrite;
  position += nBytesToWrite;

  *pBytesWritten = nBytesToWrite;

  gst_pad_push (srcpad, buffer);

  return 0;
}


int srcpad_CIO::Seek (int nDistance, unsigned int nMoveMode)
{
  GstEvent *event;

  switch (nMoveMode)
  {
    case FILE_BEGIN :
      event = gst_event_new_seek ((GstSeekType)(int)(GST_FORMAT_BYTES | GST_SEEK_METHOD_SET), nDistance);
      position = nDistance;
      break;
    case FILE_CURRENT :
      event = gst_event_new_seek ((GstSeekType)(int)(GST_FORMAT_BYTES | GST_SEEK_METHOD_CUR), nDistance);
      position += nDistance;
      break;
    case FILE_END :
      event = gst_event_new_seek ((GstSeekType)(int)(GST_FORMAT_BYTES | GST_SEEK_METHOD_END), nDistance);
      break;
    default :
      event = NULL;
      break;
  }

  if (event)  
    gst_pad_push (srcpad, GST_BUFFER (event));

  return 0;
}

int srcpad_CIO::Create(const char * pName) 
{ 
  return 0;
}

int srcpad_CIO::Delete()
{
  return 0;
}
