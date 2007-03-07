/* GStreamer Wavpack plugin
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstwavpackstreamreader.c: stream reader used for decoding
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
#include <math.h>
#include <gst/gst.h>

#include "gstwavpackstreamreader.h"

GST_DEBUG_CATEGORY_EXTERN (wavpack_debug);
#define GST_CAT_DEFAULT wavpack_debug

static int32_t
gst_wavpack_stream_reader_read_bytes (void *id, void *data, int32_t bcount)
{
  read_id *rid = (read_id *) id;
  uint32_t left = rid->length - rid->position;
  uint32_t to_read = MIN (left, bcount);

  if (to_read > 0) {
    g_memmove (data, rid->buffer + rid->position, to_read);
    rid->position += to_read;
    return to_read;
  } else {
    return 0;
  }
}

static uint32_t
gst_wavpack_stream_reader_get_pos (void *id)
{
  return ((read_id *) id)->position;
}

static int
gst_wavpack_stream_reader_set_pos_abs (void *id, uint32_t pos)
{
  GST_DEBUG ("should not be called");
  return -1;
}

static int
gst_wavpack_stream_reader_set_pos_rel (void *id, int32_t delta, int mode)
{
  GST_DEBUG ("should not be called");
  return -1;
}

static int
gst_wavpack_stream_reader_push_back_byte (void *id, int c)
{
  read_id *rid = (read_id *) id;

  rid->position -= 1;
  if (rid->position < 0)
    rid->position = 0;
  return rid->position;
}

static uint32_t
gst_wavpack_stream_reader_get_length (void *id)
{
  return ((read_id *) id)->length;
}

static int
gst_wavpack_stream_reader_can_seek (void *id)
{
  return FALSE;
}

static int32_t
gst_wavpack_stream_reader_write_bytes (void *id, void *data, int32_t bcount)
{
  GST_DEBUG ("should not be called");
  return 0;
}

WavpackStreamReader *
gst_wavpack_stream_reader_new ()
{
  WavpackStreamReader *stream_reader =
      (WavpackStreamReader *) g_malloc0 (sizeof (WavpackStreamReader));
  stream_reader->read_bytes = gst_wavpack_stream_reader_read_bytes;
  stream_reader->get_pos = gst_wavpack_stream_reader_get_pos;
  stream_reader->set_pos_abs = gst_wavpack_stream_reader_set_pos_abs;
  stream_reader->set_pos_rel = gst_wavpack_stream_reader_set_pos_rel;
  stream_reader->push_back_byte = gst_wavpack_stream_reader_push_back_byte;
  stream_reader->get_length = gst_wavpack_stream_reader_get_length;
  stream_reader->can_seek = gst_wavpack_stream_reader_can_seek;
  stream_reader->write_bytes = gst_wavpack_stream_reader_write_bytes;

  return stream_reader;
}
