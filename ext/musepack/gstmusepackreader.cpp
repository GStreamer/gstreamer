/* GStreamer Musepack decoder plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <gst/gst.h>
#include <string.h>

#include "gstmusepackreader.h"

GstMusepackReader::GstMusepackReader (GstByteStream *bs)
{
  this->bs = bs;
  this->eos = false;
}

GstMusepackReader::~GstMusepackReader (void)
{
}

mpc_int32_t
GstMusepackReader::read (void * ptr, mpc_int32_t size)
{
  guint8 *data;
  gint read;

  do {
    read = gst_bytestream_peek_bytes (this->bs, &data, size);

    if (read != size) {
      GstEvent *event;
      guint32 remaining;

      gst_bytestream_get_status (this->bs, &remaining, &event);
      if (!event) {
        GST_ELEMENT_ERROR (gst_pad_get_parent (this->bs->pad),
            RESOURCE, READ, (NULL), (NULL));
        goto done;
      }

      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_INTERRUPT:
          gst_event_unref (event);
          goto done;
        case GST_EVENT_EOS:
          this->eos = true;
          gst_event_unref (event);
          goto done;
        case GST_EVENT_FLUSH:
          gst_event_unref (event);
          break;
        default:
          gst_pad_event_default (this->bs->pad, event);
          break;
      }
    }
  } while (read != size);

done:
  if (read != 0) {
    memcpy (ptr, data, read);
    gst_bytestream_flush_fast (this->bs, read);
  }

  return read;
}

bool
GstMusepackReader::seek (mpc_int32_t offset)
{
  guint8 *dummy;

  /* hacky hack - if we're after typefind, we'll fail because
   * typefind is still typefinding (heh :) ). So read first. */
  if (this->tell () != this->get_size ()) {
    guint8 dummy2[1];
    this->read (dummy2, 1);
  }

  if (!gst_bytestream_seek (this->bs, offset, GST_SEEK_METHOD_SET))
    return FALSE;

  /* get discont */
  while (gst_bytestream_peek_bytes (this->bs, &dummy, 1) != 1) {
    GstEvent *event;
    guint32 remaining;

    gst_bytestream_get_status (this->bs, &remaining, &event);
    if (!event) {
      GST_ELEMENT_ERROR (gst_pad_get_parent (this->bs->pad),
          RESOURCE, SEEK, (NULL), (NULL));
      return false;
    }
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        g_warning ("EOS!");
        gst_event_unref (event);
        return false;
      case GST_EVENT_DISCONTINUOUS:
        gst_event_unref (event);
        return true;
      case GST_EVENT_INTERRUPT:
        g_warning ("interrupt!");
        return false;
      case GST_EVENT_FLUSH:
        gst_event_unref (event);
        break;
      default:
        gst_pad_event_default (this->bs->pad, event);
        break;
    }
  }

  return false;
}

mpc_int32_t
GstMusepackReader::tell (void)
{
  return gst_bytestream_tell (this->bs);
}

mpc_int32_t
GstMusepackReader::get_size (void)
{
  return gst_bytestream_length (this->bs);
}

bool
GstMusepackReader::canseek (void)
{
  return true;
}
