/* GStreamer mplex (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmplexibitstream.hh: gstreamer/mplex input bitstream wrapper
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

#include "gstmplexibitstream.hh"

/*
 * Class init/exit functions.
 */

GstMplexIBitStream::GstMplexIBitStream (GstPad * _pad, guint buf_size):
IBitStream ()
{
  guint8 *data;

  pad = _pad;
  bs = gst_bytestream_new (pad);
  eos = FALSE;

  streamname = g_strdup (gst_pad_get_name (_pad));

  SetBufSize (buf_size);
  eobs = false;
  byteidx = 0;

  /* we peek 1 byte (not even caring about the return value) so we
   * are sure that we have data and thus capsnego must be completed
   * when we return. */
  gst_bytestream_peek_bytes (bs, &data, 1);

  if (!ReadIntoBuffer () && buffered == 0) {
    GST_ELEMENT_ERROR (gst_pad_get_parent (_pad), RESOURCE, READ, (NULL),
        ("Failed to read from input pad %s", gst_pad_get_name (pad)));
  }
}

GstMplexIBitStream::~GstMplexIBitStream (void)
{
  gst_bytestream_destroy (bs);
}

/*
 * Read data.
 */

size_t GstMplexIBitStream::ReadStreamBytes (uint8_t * buf, size_t size)
{
  guint8 *
      data;

  guint
      read;

  if (eos)
    return 0;

  if ((read = gst_bytestream_peek_bytes (bs, &data, size)) != size) {
    GstEvent *
        event;

    guint
        pending;

    gst_bytestream_get_status (bs, &pending, &event);
    if (event) {
      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_EOS:
          eos = TRUE;
          break;
        default:
          break;
      }
      gst_event_unref (event);
    }
  }

  memcpy (buf, data, read);
  gst_bytestream_flush_fast (bs, read);

  return read;
}

/*
 * Are we at EOS?
 */

bool GstMplexIBitStream::EndOfStream (void)
{
  return eos;
}
