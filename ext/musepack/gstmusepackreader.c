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

static mpc_int32_t
gst_musepack_reader_peek (void *this, void *ptr, mpc_int32_t size)
{
  GstByteStream *bs = this;
  guint8 *data;
  gint read;

  do {
    read = gst_bytestream_peek_bytes (bs, &data, size);

    if (read != size) {
      GstEvent *event;
      guint32 remaining;

      gst_bytestream_get_status (bs, &remaining, &event);
      if (!event) {
        GST_ELEMENT_ERROR (gst_pad_get_parent (bs->pad),
            RESOURCE, READ, (NULL), (NULL));
        goto done;
      }

      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_INTERRUPT:
          gst_event_unref (event);
          goto done;
        case GST_EVENT_EOS:
          gst_event_unref (event);
          goto done;
        case GST_EVENT_FLUSH:
          gst_event_unref (event);
          break;
        case GST_EVENT_DISCONTINUOUS:
          gst_event_unref (event);
          break;
        default:
          gst_pad_event_default (bs->pad, event);
          break;
      }
    }
  } while (read != size);

done:
  if (read != 0) {
    memcpy (ptr, data, read);
  }

  return read;
}

static mpc_int32_t
gst_musepack_reader_read (void *this, void *ptr, mpc_int32_t size)
{
  GstByteStream *bs = this;
  gint read;

  /* read = peek + flush */
  if ((read = gst_musepack_reader_peek (this, ptr, size)) > 0) {
    gst_bytestream_flush_fast (bs, read);
  }

  return read;
}

static BOOL
gst_musepack_reader_seek (void *this, mpc_int32_t offset)
{
  GstByteStream *bs = this;
  guint8 dummy;

  /* hacky hack - if we're after typefind, we'll fail because
   * typefind is still typefinding (heh :) ). So read first. */
  gst_musepack_reader_peek (this, &dummy, 1);

  /* seek */
  if (!gst_bytestream_seek (bs, offset, GST_SEEK_METHOD_SET))
    return FALSE;

  /* get discont */
  if (gst_musepack_reader_peek (this, &dummy, 1) != 1)
    return FALSE;

  return TRUE;
}

static mpc_int32_t
gst_musepack_reader_tell (void *this)
{
  GstByteStream *bs = this;

  return gst_bytestream_tell (bs);
}

static mpc_int32_t
gst_musepack_reader_get_size (void *this)
{
  GstByteStream *bs = this;

  return gst_bytestream_length (bs);
}

static BOOL
gst_musepack_reader_canseek (void *this)
{
  return TRUE;
}

void
gst_musepack_init_reader (mpc_reader * r, GstByteStream * bs)
{
  r->data = bs;

  r->read = gst_musepack_reader_read;
  r->seek = gst_musepack_reader_seek;
  r->tell = gst_musepack_reader_tell;
  r->get_size = gst_musepack_reader_get_size;
  r->canseek = gst_musepack_reader_canseek;
}
