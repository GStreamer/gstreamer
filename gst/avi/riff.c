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

#include "riff.h"

GstRiffParse*
gst_riff_parse_new (GstPad *pad)
{
  GstRiffParse *parse;

  parse = g_new0 (GstRiffParse, 1);
  parse->pad = pad;
  parse->bs = gst_bytestream_new (pad);

  return parse;
}

void
gst_riff_parse_free (GstRiffParse *parse)
{
  gst_bytestream_destroy (parse->bs);
  g_free (parse);
}


static GstRiffReturn
gst_riff_parse_handle_sink_event (GstRiffParse *parse)
{
  guint32 remaining;
  GstEvent *event;
  GstEventType type;
  GstRiffReturn ret = GST_RIFF_OK;

  gst_bytestream_get_status (parse->bs, &remaining, &event);

  type = event? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_EOS:
      ret = GST_RIFF_EOS;
      break;
    default:
      g_warning ("unhandled event %d", type);
      break;
  }

  gst_event_unref (event);

  return ret;
}

GstRiffReturn
gst_riff_parse_next_chunk (GstRiffParse *parse, guint32 *id, GstBuffer **buf)
{
  GstByteStream *bs;
  guint32 got_bytes;
  gint skipsize;
  gst_riff_chunk *chunk;

  bs = parse->bs;

  do {
    got_bytes = gst_bytestream_peek_bytes (bs, (guint8 **) &chunk, sizeof (gst_riff_chunk));
    if (got_bytes < sizeof (gst_riff_chunk)) {
      GstRiffReturn ret;
      
      ret = gst_riff_parse_handle_sink_event (parse);

      if (ret == GST_RIFF_EOS)
	return ret;
    }
  } while (got_bytes != sizeof (gst_riff_chunk));

  *id = chunk->id;

  switch (chunk->id) {
    case GST_RIFF_TAG_RIFF:
    case GST_RIFF_TAG_LIST:
      skipsize = sizeof (gst_riff_list);
      break;
    default:
      skipsize = (chunk->size + 8 + 1) & ~1;
      break;
  }

  do {
    got_bytes = gst_bytestream_read (bs, buf, skipsize);
    if (got_bytes < skipsize) {
      GstRiffReturn ret;
      
      ret = gst_riff_parse_handle_sink_event (parse);

      if (ret == GST_RIFF_EOS)
	return ret;
    }
  } while (got_bytes != skipsize);

  return GST_RIFF_OK;
}
