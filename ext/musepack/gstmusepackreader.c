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
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (this);
  GstBuffer *buf = NULL;
  gint read;

  if (musepackdec->eos) {
    return 0;
  }

  do {
    if (GST_FLOW_OK != gst_pad_pull_range (musepackdec->sinkpad,
            musepackdec->offset, size, &buf)) {
      return 0;
    }

    read = GST_BUFFER_SIZE (buf);

    if (musepackdec->eos ||
        musepackdec->flush_pending || musepackdec->seek_pending) {
      break;
    }


    /* FIX ME: do i have to handle those event in sink_event? */
    /* we pipeline doesnt stop after receive EOS */
    /*

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
     */
  } while (read != size);

  if (read != 0) {
    memcpy (ptr, GST_BUFFER_DATA (buf), read);
  }
  gst_buffer_unref (buf);

  return read;
}

static mpc_int32_t
gst_musepack_reader_read (void *this, void *ptr, mpc_int32_t size)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (this);
  gint read;

  /* read = peek + flush */
  if ((read = gst_musepack_reader_peek (this, ptr, size)) > 0) {
    musepackdec->offset += read;
  }

  return read;
}

static mpc_bool_t
gst_musepack_reader_seek (void *this, mpc_int32_t offset)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (this);
  guint8 dummy;

  /* hacky hack - if we're after typefind, we'll fail because
   * typefind is still typefinding (heh :) ). So read first. */
  gst_musepack_reader_peek (this, &dummy, 1);

  /* seek */
  musepackdec->offset = offset;

  /* get discont */
  if (gst_musepack_reader_peek (this, &dummy, 1) != 1)
    return FALSE;

  return TRUE;
}

static mpc_int32_t
gst_musepack_reader_tell (void *this)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (this);
  GstQuery *query;
  gint64 position;
  GstFormat format = GST_FORMAT_BYTES;

  query = gst_query_new_position (GST_FORMAT_BYTES);
  if (gst_pad_query (musepackdec->sinkpad, query)) {

    gst_query_parse_position (query, &format, &position);

    if (format != GST_FORMAT_BYTES) {
      GstFormat dest_format = GST_FORMAT_BYTES;

      if (!gst_musepackdec_src_convert (musepackdec->srcpad,
              format, position, &dest_format, &position)) {
        position = -1;
      }

    }

  } else {
    position = -1;
  }
  gst_query_unref (query);

  return position;
}

static mpc_int32_t
gst_musepack_reader_get_size (void *this)
{
  GstMusepackDec *musepackdec = GST_MUSEPACK_DEC (this);
  GstQuery *query;
  gint64 duration;
  GstFormat format = GST_FORMAT_BYTES;

  query = gst_query_new_duration (GST_FORMAT_BYTES);
  if (gst_pad_query (musepackdec->sinkpad, query)) {

    gst_query_parse_duration (query, &format, &duration);

    if (format != GST_FORMAT_BYTES) {
      GstFormat dest_format = GST_FORMAT_BYTES;

      if (!gst_musepackdec_src_convert (musepackdec->srcpad,
              format, duration, &dest_format, &duration)) {
        duration = -1;
      }

    }

  } else {
    duration = -1;
  }
  gst_query_unref (query);


  return duration;
}

static mpc_bool_t
gst_musepack_reader_canseek (void *this)
{
  return TRUE;
}

void
gst_musepack_init_reader (mpc_reader * r, GstMusepackDec * musepackdec)
{
  r->data = musepackdec;

  r->read = gst_musepack_reader_read;
  r->seek = gst_musepack_reader_seek;
  r->tell = gst_musepack_reader_tell;
  r->get_size = gst_musepack_reader_get_size;
  r->canseek = gst_musepack_reader_canseek;
}
