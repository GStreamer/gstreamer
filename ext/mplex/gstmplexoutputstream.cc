/* GStreamer mplex (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmplexoutputstream.hh: gstreamer/mplex output stream wrapper
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstmplexoutputstream.hh"

/*
 * Class init functions.
 */

GstMplexOutputStream::GstMplexOutputStream (GstElement * _element, GstPad * _pad):
OutputStream ()
{
  element = _element;
  pad = _pad;
  size = 0;
}

/*
 * Open/close. Basically 'no-op's (close() sets EOS).
 *
 * Open (): -1 means failure, 0 means success.
 */

int
GstMplexOutputStream::Open (void)
{
  return 0;
}

void
GstMplexOutputStream::Close (void)
{
  gst_pad_push (pad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
  gst_element_set_eos (element);
}

/*
 * Get size of current segment.
 */

off_t GstMplexOutputStream::SegmentSize (void)
{
  return size;
}

/*
 * Next segment.
 */

void
GstMplexOutputStream::NextSegment (void)
{
  size = 0;

  /* send EOS. The filesink (or whatever) handles that
   * and opens a new file. */
  gst_pad_push (pad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
}

/*
 * Write data.
 */

void
GstMplexOutputStream::Write (guint8 * data, guint len)
{
  GstBuffer *buf;

  buf = gst_buffer_new_and_alloc (len);
  memcpy (GST_BUFFER_DATA (buf), data, len);

  size += len;
  gst_pad_push (pad, GST_DATA (buf));
}
