/* GStreamer demultiplexer plugin for Interplay MVE movie files
 *
 * Copyright (C) 2006-2008 Jens Granseuer <jensgr@gmx.net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * For more information about the Interplay MVE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/interplay-mve.txt
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include "gstmvedemux.h"
#include "mve.h"

GST_DEBUG_CATEGORY_STATIC (mvedemux_debug);
#define GST_CAT_DEFAULT mvedemux_debug

enum MveDemuxState
{
  MVEDEMUX_STATE_INITIAL,       /* initial state, header not read */
  MVEDEMUX_STATE_NEXT_CHUNK,    /* parsing chunk/segment header */
  MVEDEMUX_STATE_MOVIE,         /* reading the stream */
  MVEDEMUX_STATE_SKIP           /* skipping chunk */
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-mve")
    );

static GstStaticPadTemplate vidsrc_template = GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ], "
        "framerate = (fraction) [ 0, MAX ], "
        "bpp = (int) 16, "
        "depth = (int) 15, "
        "endianness = (int) BYTE_ORDER, "
        "red_mask = (int) 31744, "
        "green_mask = (int) 992, "
        "blue_mask = (int) 31; "
        "video/x-raw-rgb, "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ], "
        "framerate = (fraction) [ 0, MAX ], "
        "bpp = (int) 8, " "depth = (int) 8, " "endianness = (int) BYTE_ORDER")
    );

static GstStaticPadTemplate audsrc_template = GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 8, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 2 ], "
        "depth = (int) 8, "
        "signed = (boolean) false; "
        "audio/x-raw-int, "
        "width = (int) 16, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 2 ], "
        "depth = (int) 16, "
        "signed = (boolean) true, "
        "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }")
    );

#define MVE_DEFAULT_AUDIO_STREAM 0x01

static void gst_mve_demux_class_init (GstMveDemuxClass * klass);
static void gst_mve_demux_base_init (GstMveDemuxClass * klass);
static void gst_mve_demux_init (GstMveDemux * mve);

#define GST_MVE_SEGMENT_SIZE(data)    (GST_READ_UINT16_LE (data))
#define GST_MVE_SEGMENT_TYPE(data)    (GST_READ_UINT8 (data + 2))
#define GST_MVE_SEGMENT_VERSION(data) (GST_READ_UINT8 (data + 3))

static GstElementClass *parent_class = NULL;

static void
gst_mve_demux_reset (GstMveDemux * mve)
{
  gst_adapter_clear (mve->adapter);

  if (mve->video_stream != NULL) {
    if (mve->video_stream->pad)
      gst_element_remove_pad (GST_ELEMENT (mve), mve->video_stream->pad);
    if (mve->video_stream->caps)
      gst_caps_unref (mve->video_stream->caps);
    if (mve->video_stream->palette)
      gst_buffer_unref (mve->video_stream->palette);
    g_free (mve->video_stream->code_map);
    if (mve->video_stream->buffer)
      gst_buffer_unref (mve->video_stream->buffer);
    g_free (mve->video_stream);
    mve->video_stream = NULL;
  }

  if (mve->audio_stream != NULL) {
    if (mve->audio_stream->pad)
      gst_element_remove_pad (GST_ELEMENT (mve), mve->audio_stream->pad);
    if (mve->audio_stream->caps)
      gst_caps_unref (mve->audio_stream->caps);
    if (mve->audio_stream->buffer)
      gst_buffer_unref (mve->audio_stream->buffer);
    g_free (mve->audio_stream);
    mve->audio_stream = NULL;
  }

  mve->state = MVEDEMUX_STATE_INITIAL;
  mve->needed_bytes = MVE_PREAMBLE_SIZE;
  mve->frame_duration = GST_CLOCK_TIME_NONE;

  mve->chunk_size = 0;
  mve->chunk_offset = 0;
}

static const GstQueryType *
gst_mve_demux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType src_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_SEEKING,
    0
  };

  return src_types;
}

static gboolean
gst_mve_demux_handle_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:{
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      /* we only support TIME */
      if (format == GST_FORMAT_TIME) {
        GstMveDemuxStream *s = gst_pad_get_element_private (pad);

        if (s != NULL) {
          GST_OBJECT_LOCK (s);
          gst_query_set_position (query, GST_FORMAT_TIME, s->last_ts);
          GST_OBJECT_UNLOCK (s);
          res = TRUE;
        }
      }
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat format;

      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      if (format == GST_FORMAT_TIME) {
        gst_query_set_seeking (query, GST_FORMAT_TIME, FALSE, 0, -1);
        res = TRUE;
      }
      break;
    }
    case GST_QUERY_DURATION:{
      /* FIXME: really should implement/estimate this somehow */
      res = FALSE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  return res;
}

static gboolean
gst_mve_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      GST_DEBUG ("seeking not supported");
      res = FALSE;
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;
}


static GstStateChangeReturn
gst_mve_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstMveDemux *mve = GST_MVE_DEMUX (element);

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    GstStateChangeReturn ret;

    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (ret != GST_STATE_CHANGE_SUCCESS)
      return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mve_demux_reset (mve);
      break;
    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
gst_mve_add_stream (GstMveDemux * mve, GstMveDemuxStream * stream,
    GstTagList * list)
{
  GstPadTemplate *templ;
  gboolean ret = FALSE;

  if (stream->pad == NULL) {
    if (stream == mve->video_stream) {
      templ = gst_static_pad_template_get (&vidsrc_template);
      stream->pad = gst_pad_new_from_template (templ, "video");
    } else {
      templ = gst_static_pad_template_get (&audsrc_template);
      stream->pad = gst_pad_new_from_template (templ, "audio");
    }
    gst_object_unref (templ);

    gst_pad_set_query_type_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_mve_demux_get_src_query_types));
    gst_pad_set_query_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_mve_demux_handle_src_query));
    gst_pad_set_event_function (stream->pad,
        GST_DEBUG_FUNCPTR (gst_mve_demux_handle_src_event));
    gst_pad_set_element_private (stream->pad, stream);

    GST_DEBUG_OBJECT (mve, "adding pad %s", GST_PAD_NAME (stream->pad));
    gst_pad_set_active (stream->pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (mve), stream->pad);
    ret = TRUE;
  }

  GST_DEBUG_OBJECT (mve, "setting caps %" GST_PTR_FORMAT, stream->caps);
  gst_pad_set_caps (stream->pad, stream->caps);

  if (list)
    gst_element_found_tags_for_pad (GST_ELEMENT (mve), stream->pad, list);

  return ret;
}

static GstFlowReturn
gst_mve_stream_error (GstMveDemux * mve, guint16 req, guint16 avail)
{
  GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL),
      ("wanted to read %d bytes from stream, %d available", req, avail));
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_mve_buffer_alloc_for_pad (GstMveDemuxStream * stream,
    guint32 size, GstBuffer ** buffer)
{
  *buffer = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (*buffer, stream->caps);
  GST_BUFFER_TIMESTAMP (*buffer) = stream->last_ts;
  GST_BUFFER_OFFSET (*buffer) = stream->offset;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mve_video_init (GstMveDemux * mve, const guint8 * data)
{
  GST_DEBUG_OBJECT (mve, "init video");

  if (mve->video_stream == NULL) {
    GstMveDemuxStream *stream = g_new0 (GstMveDemuxStream, 1);

    stream->buffer = NULL;
    stream->back_buf1 = NULL;
    stream->back_buf2 = NULL;
    stream->offset = 0;
    stream->width = 0;
    stream->height = 0;
    stream->code_map = NULL;
    stream->code_map_avail = FALSE;
    stream->palette = NULL;
    stream->caps = NULL;
    stream->last_ts = GST_CLOCK_TIME_NONE;
    stream->last_flow = GST_FLOW_OK;
    mve->video_stream = stream;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mve_video_create_buffer (GstMveDemux * mve, guint8 version,
    const guint8 * data, guint16 len)
{
  GstBuffer *buf;
  guint16 w, h, n, true_color, bpp;
  guint required, size;

  GST_DEBUG_OBJECT (mve, "create video buffer");

  if (mve->video_stream == NULL) {
    GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL),
        ("trying to create video buffer for uninitialized stream"));
    return GST_FLOW_ERROR;
  }

  /* need 4 to 8 more bytes */
  required = (version > 1) ? 8 : (version * 2);
  if (len < required)
    return gst_mve_stream_error (mve, required, len);

  w = GST_READ_UINT16_LE (data) << 3;
  h = GST_READ_UINT16_LE (data + 2) << 3;

  if (version > 0)
    n = GST_READ_UINT16_LE (data + 4);
  else
    n = 1;

  if (version > 1)
    true_color = GST_READ_UINT16_LE (data + 6);
  else
    true_color = 0;

  bpp = (true_color ? 2 : 1);
  size = w * h * bpp;

  if (mve->video_stream->buffer != NULL) {
    GST_DEBUG_OBJECT (mve, "video buffer already created");

    if (GST_BUFFER_SIZE (mve->video_stream->buffer) == size * 2)
      return GST_FLOW_OK;

    GST_DEBUG_OBJECT (mve, "video buffer size has changed");
    gst_buffer_unref (mve->video_stream->buffer);
  }

  GST_DEBUG_OBJECT (mve,
      "allocating video buffer, w:%u, h:%u, n:%u, true_color:%u", w, h, n,
      true_color);

  /* we need a buffer to keep the last 2 frames, since those may be
     needed for decoding the next one */
  buf = gst_buffer_new_and_alloc (size * 2);

  mve->video_stream->bpp = bpp;
  mve->video_stream->width = w;
  mve->video_stream->height = h;
  mve->video_stream->buffer = buf;
  mve->video_stream->back_buf1 = GST_BUFFER_DATA (buf);
  mve->video_stream->back_buf2 = mve->video_stream->back_buf1 + size;
  mve->video_stream->max_block_offset = (h - 7) * w - 8;
  memset (mve->video_stream->back_buf1, 0, size * 2);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mve_video_palette (GstMveDemux * mve, const guint8 * data, guint16 len)
{
  GstBuffer *buf;
  guint16 start, count;
  const guint8 *pal;
  guint32 *pal_ptr;
  gint i;

  GST_DEBUG_OBJECT (mve, "video palette");

  if (mve->video_stream == NULL) {
    GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL),
        ("found palette before video stream was initialized"));
    return GST_FLOW_ERROR;
  }

  /* need 4 more bytes now, more later */
  if (len < 4)
    return gst_mve_stream_error (mve, 4, len);

  len -= 4;

  start = GST_READ_UINT16_LE (data);
  count = GST_READ_UINT16_LE (data + 2);
  GST_DEBUG_OBJECT (mve, "found palette start:%u, count:%u", start, count);

  /* need more bytes */
  if (len < count * 3)
    return gst_mve_stream_error (mve, count * 3, len);

  /* make sure we don't exceed the buffer */
  if (start + count > MVE_PALETTE_COUNT) {
    GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL),
        ("palette too large for buffer"));
    return GST_FLOW_ERROR;
  }

  if (mve->video_stream->palette != NULL) {
    /* older buffers floating around might still use the old
       palette, so make sure we can update it */
    buf = gst_buffer_make_writable (mve->video_stream->palette);
  } else {
    buf = gst_buffer_new_and_alloc (MVE_PALETTE_COUNT * 4);
    memset (GST_BUFFER_DATA (buf), 0, GST_BUFFER_SIZE (buf));
  }

  mve->video_stream->palette = buf;

  pal = data + 4;
  pal_ptr = ((guint32 *) GST_BUFFER_DATA (buf)) + start;
  for (i = 0; i < count; ++i) {
    /* convert from 6-bit VGA to 8-bit palette */
    guint8 r, g, b;

    r = (*pal) << 2;
    ++pal;
    g = (*pal) << 2;
    ++pal;
    b = (*pal) << 2;
    ++pal;
    *pal_ptr = (r << 16) | (g << 8) | (b);
    ++pal_ptr;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mve_video_palette_compressed (GstMveDemux * mve, const guint8 * data,
    guint16 len)
{
  guint8 mask;
  gint i, j;
  guint32 *col;

  GST_DEBUG_OBJECT (mve, "compressed video palette");

  if (mve->video_stream == NULL) {
    GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL),
        ("found palette before video stream was initialized"));
    return GST_FLOW_ERROR;
  }

  if (mve->video_stream->palette == NULL) {
    GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL),
        ("no palette available for modification"));
    return GST_FLOW_ERROR;
  }

  /* need at least 32 more bytes */
  if (len < 32)
    return gst_mve_stream_error (mve, 32, len);

  len -= 32;

  for (i = 0; i < 32; ++i) {
    mask = GST_READ_UINT8 (data);
    ++data;

    if (mask != 0) {
      for (j = 0; j < 8; ++j) {
        if (mask & (1 << j)) {
          guint8 r, g, b;

          /* need 3 more bytes */
          if (len < 3)
            return gst_mve_stream_error (mve, 3, len);

          len -= 3;

          r = (*data) << 2;
          ++data;
          g = (*data) << 2;
          ++data;
          b = (*data) << 2;
          ++data;
          col =
              ((guint32 *) GST_BUFFER_DATA (mve->video_stream->palette)) +
              i * 8 + j;
          *col = (r << 16) | (g << 8) | (b);
        }
      }
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mve_video_code_map (GstMveDemux * mve, const guint8 * data, guint16 len)
{
  gint min;

  if (mve->video_stream == NULL || mve->video_stream->code_map == NULL) {
    GST_WARNING_OBJECT (mve, "video stream not initialized");
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (mve, "found code map, size:%u", len);

  /* decoding is done in 8x8 blocks using 4-bit opcodes */
  min = (mve->video_stream->width * mve->video_stream->height) / (8 * 8 * 2);

  if (len < min)
    return gst_mve_stream_error (mve, min, len);

  memcpy (mve->video_stream->code_map, data, min);
  mve->video_stream->code_map_avail = TRUE;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mve_video_data (GstMveDemux * mve, const guint8 * data, guint16 len,
    GstBuffer ** output)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gint16 cur_frame, last_frame;
  gint16 x_offset, y_offset;
  gint16 x_size, y_size;
  guint16 flags;
  gint dec;
  GstBuffer *buf = NULL;
  GstMveDemuxStream *s = mve->video_stream;

  GST_LOG_OBJECT (mve, "video data");

  if (s == NULL) {
    GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL),
        ("trying to decode video data before stream was initialized"));
    return GST_FLOW_ERROR;
  }

  if (GST_CLOCK_TIME_IS_VALID (mve->frame_duration)) {
    if (GST_CLOCK_TIME_IS_VALID (s->last_ts))
      s->last_ts += mve->frame_duration;
    else
      s->last_ts = 0;
  }

  if (!s->code_map_avail) {
    GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL),
        ("no code map available for decoding"));
    return GST_FLOW_ERROR;
  }

  /* need at least 14 more bytes */
  if (len < 14)
    return gst_mve_stream_error (mve, 14, len);

  len -= 14;

  cur_frame = GST_READ_UINT16_LE (data);
  last_frame = GST_READ_UINT16_LE (data + 2);
  x_offset = GST_READ_UINT16_LE (data + 4);
  y_offset = GST_READ_UINT16_LE (data + 6);
  x_size = GST_READ_UINT16_LE (data + 8);
  y_size = GST_READ_UINT16_LE (data + 10);
  flags = GST_READ_UINT16_LE (data + 12);
  data += 14;

  GST_DEBUG_OBJECT (mve,
      "video data hot:%d, cold:%d, xoff:%d, yoff:%d, w:%d, h:%d, flags:%x",
      cur_frame, last_frame, x_offset, y_offset, x_size, y_size, flags);

  if (flags & MVE_VIDEO_DELTA_FRAME) {
    guint8 *temp = s->back_buf1;

    s->back_buf1 = s->back_buf2;
    s->back_buf2 = temp;
  }

  ret = gst_mve_buffer_alloc_for_pad (s, s->width * s->height * s->bpp, &buf);
  if (ret != GST_FLOW_OK)
    return ret;

  if (s->bpp == 2) {
    dec = ipvideo_decode_frame16 (s, data, len);
  } else {
    if (s->palette == NULL) {
      GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL), ("no palette available"));
      goto error;
    }

    dec = ipvideo_decode_frame8 (s, data, len);
  }
  if (dec != 0)
    goto error;

  memcpy (GST_BUFFER_DATA (buf), s->back_buf1, GST_BUFFER_SIZE (buf));
  GST_BUFFER_DURATION (buf) = mve->frame_duration;
  GST_BUFFER_OFFSET_END (buf) = ++s->offset;

  if (s->bpp == 1) {
    GstCaps *caps;

    /* set the palette on the outgoing buffer */
    caps = gst_caps_copy (s->caps);
    gst_caps_set_simple (caps,
        "palette_data", GST_TYPE_BUFFER, s->palette, NULL);
    gst_buffer_set_caps (buf, caps);
    gst_caps_unref (caps);
  }

  *output = buf;
  return GST_FLOW_OK;

error:
  gst_buffer_unref (buf);
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_mve_audio_init (GstMveDemux * mve, guint8 version, const guint8 * data,
    guint16 len)
{
  GstMveDemuxStream *stream;
  guint16 flags;
  guint32 requested_buffer;
  GstTagList *list;
  gchar *name;

  GST_DEBUG_OBJECT (mve, "init audio");

  /* need 8 more bytes */
  if (len < 8)
    return gst_mve_stream_error (mve, 8, len);

  if (mve->audio_stream == NULL) {
    stream = g_new0 (GstMveDemuxStream, 1);
    stream->offset = 0;
    stream->last_ts = 0;
    stream->last_flow = GST_FLOW_OK;
    mve->audio_stream = stream;
  } else {
    stream = mve->audio_stream;
    gst_caps_unref (stream->caps);
  }

  flags = GST_READ_UINT16_LE (data + 2);
  stream->sample_rate = GST_READ_UINT16_LE (data + 4);
  requested_buffer = GST_READ_UINT32_LE (data + 6);

  /* bit 0: 0 = mono, 1 = stereo */
  stream->n_channels = (flags & MVE_AUDIO_STEREO) + 1;
  /* bit 1: 0 = 8 bit, 1 = 16 bit */
  stream->sample_size = (((flags & MVE_AUDIO_16BIT) >> 1) + 1) * 8;
  /* bit 2: 0 = uncompressed, 1 = compressed */
  stream->compression = ((version > 0) && (flags & MVE_AUDIO_COMPRESSED)) ?
      TRUE : FALSE;

  GST_DEBUG_OBJECT (mve, "audio init, sample_rate:%d, channels:%d, "
      "bits_per_sample:%d, compression:%d, buffer:%u",
      stream->sample_rate, stream->n_channels,
      stream->sample_size, stream->compression, requested_buffer);

  stream->caps = gst_caps_from_string ("audio/x-raw-int");
  if (stream->caps == NULL)
    return GST_FLOW_ERROR;

  gst_caps_set_simple (stream->caps,
      "signed", G_TYPE_BOOLEAN, (stream->sample_size == 8) ? FALSE : TRUE,
      "depth", G_TYPE_INT, stream->sample_size,
      "width", G_TYPE_INT, stream->sample_size,
      "channels", G_TYPE_INT, stream->n_channels,
      "rate", G_TYPE_INT, stream->sample_rate, NULL);
  if (stream->sample_size > 8) {
    /* for uncompressed audio we can simply copy the incoming buffer
       which is always in little endian format */
    gst_caps_set_simple (stream->caps, "endianness", G_TYPE_INT,
        (stream->compression ? G_BYTE_ORDER : G_LITTLE_ENDIAN), NULL);
  } else if (stream->compression) {
    GST_WARNING_OBJECT (mve,
        "compression is only supported for 16-bit samples");
    stream->compression = FALSE;
  }

  list = gst_tag_list_new ();
  name = g_strdup_printf ("Raw %d-bit PCM audio", stream->sample_size);
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_AUDIO_CODEC, name, NULL);
  g_free (name);

  if (gst_mve_add_stream (mve, stream, list))
    return gst_pad_push_event (mve->audio_stream->pad,
        gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
            0, GST_CLOCK_TIME_NONE, 0)) ? GST_FLOW_OK : GST_FLOW_ERROR;
  else
    return GST_FLOW_OK;
}

static GstFlowReturn
gst_mve_audio_data (GstMveDemux * mve, guint8 type, const guint8 * data,
    guint16 len, GstBuffer ** output)
{
  GstFlowReturn ret;
  GstMveDemuxStream *s = mve->audio_stream;
  GstBuffer *buf = NULL;
  guint16 stream_mask;
  guint16 size;

  GST_LOG_OBJECT (mve, "audio data");

  if (s == NULL) {
    GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL),
        ("trying to queue samples with no audio stream"));
    return GST_FLOW_ERROR;
  }

  /* need at least 6 more bytes */
  if (len < 6)
    return gst_mve_stream_error (mve, 6, len);

  len -= 6;

  stream_mask = GST_READ_UINT16_LE (data + 2);
  size = GST_READ_UINT16_LE (data + 4);
  data += 6;

  if (stream_mask & MVE_DEFAULT_AUDIO_STREAM) {
    guint16 n_samples = size / s->n_channels / (s->sample_size / 8);
    GstClockTime duration = (GST_SECOND / s->sample_rate) * n_samples;

    if (type == MVE_OC_AUDIO_DATA) {
      guint16 required = (s->compression ? size / 2 + s->n_channels : size);

      if (len < required)
        return gst_mve_stream_error (mve, required, len);

      ret = gst_mve_buffer_alloc_for_pad (s, size, &buf);

      if (ret != GST_FLOW_OK)
        return ret;

      if (s->compression)
        ipaudio_uncompress ((gint16 *) GST_BUFFER_DATA (buf), size,
            data, s->n_channels);
      else
        memcpy (GST_BUFFER_DATA (buf), data, size);

      GST_DEBUG_OBJECT (mve, "created audio buffer, size:%u, stream_mask:%x",
          size, stream_mask);
    } else {
      /* silence - create a minimal buffer with no sound */
      size = s->n_channels * (s->sample_size / 8);
      ret = gst_mve_buffer_alloc_for_pad (s, size, &buf);
      memset (GST_BUFFER_DATA (buf), 0, size);
    }

    GST_BUFFER_DURATION (buf) = duration;
    GST_BUFFER_OFFSET_END (buf) = s->offset + n_samples;
    *output = buf;

    s->offset += n_samples;
    s->last_ts += duration;
  } else {
    /* alternate audio streams not supported.
       are there any movies which use them? */
    if (type == MVE_OC_AUDIO_DATA)
      GST_WARNING_OBJECT (mve, "found non-empty alternate audio stream");
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_mve_timer_create (GstMveDemux * mve, const guint8 * data, guint16 len,
    GstBuffer ** buf)
{
  guint32 t_rate;
  guint16 t_subdiv;
  GstMveDemuxStream *s;
  GstTagList *list;
  gint rate_nom, rate_den;

  g_return_val_if_fail (mve->video_stream != NULL, GST_FLOW_ERROR);

  /* need 6 more bytes */
  if (len < 6)
    return gst_mve_stream_error (mve, 6, len);

  t_rate = GST_READ_UINT32_LE (data);
  t_subdiv = GST_READ_UINT16_LE (data + 4);

  GST_DEBUG_OBJECT (mve, "found timer:%ux%u", t_rate, t_subdiv);
  mve->frame_duration = t_rate * t_subdiv * GST_USECOND;

  /* now really start rolling... */
  s = mve->video_stream;

  if ((s->buffer == NULL) || (s->width == 0) || (s->height == 0)) {
    GST_ELEMENT_ERROR (mve, STREAM, DECODE, (NULL),
        ("missing or invalid create-video-buffer segment (%dx%d)",
            s->width, s->height));
    return GST_FLOW_ERROR;
  }

  if (s->pad != NULL) {
    if (s->caps != NULL) {
      gst_caps_unref (s->caps);
      s->caps = NULL;
    }
    if (s->code_map != NULL) {
      g_free (s->code_map);
      s->code_map = NULL;
    }
    list = NULL;
  } else {
    list = gst_tag_list_new ();
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_VIDEO_CODEC, "Raw RGB video", NULL);
  }

  s->caps = gst_caps_from_string ("video/x-raw-rgb");
  if (s->caps == NULL)
    return GST_FLOW_ERROR;

  rate_nom = GST_SECOND / GST_USECOND;
  rate_den = mve->frame_duration / GST_USECOND;

  gst_caps_set_simple (s->caps,
      "bpp", G_TYPE_INT, s->bpp * 8,
      "depth", G_TYPE_INT, (s->bpp == 1) ? 8 : 15,
      "width", G_TYPE_INT, s->width,
      "height", G_TYPE_INT, s->height,
      "framerate", GST_TYPE_FRACTION, rate_nom, rate_den,
      "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);
  if (s->bpp > 1) {
    gst_caps_set_simple (s->caps, "red_mask", G_TYPE_INT, 0x7C00,       /* 31744 */
        "green_mask", G_TYPE_INT, 0x03E0,       /*   992 */
        "blue_mask", G_TYPE_INT, 0x001F,        /*    31 */
        NULL);
  }

  s->code_map = g_malloc ((s->width * s->height) / (8 * 8 * 2));

  if (gst_mve_add_stream (mve, s, list))
    return gst_pad_push_event (s->pad,
        gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
            0, GST_CLOCK_TIME_NONE, 0)) ? GST_FLOW_OK : GST_FLOW_ERROR;
  else
    return GST_FLOW_OK;
}

static void
gst_mve_end_chunk (GstMveDemux * mve)
{
  GST_LOG_OBJECT (mve, "end of chunk");

  if (mve->video_stream != NULL)
    mve->video_stream->code_map_avail = FALSE;
}

/* parse segment */
static GstFlowReturn
gst_mve_parse_segment (GstMveDemux * mve, GstMveDemuxStream ** stream,
    GstBuffer ** send)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const guint8 *buffer, *data;
  guint8 type, version;
  guint16 len;

  buffer = gst_adapter_peek (mve->adapter, mve->needed_bytes);

  type = GST_MVE_SEGMENT_TYPE (buffer);

  /* check whether to handle the segment */
  if (type < 32) {
    version = GST_MVE_SEGMENT_VERSION (buffer);
    len = GST_MVE_SEGMENT_SIZE (buffer);
    data = buffer + 4;

    switch (type) {

      case MVE_OC_END_OF_CHUNK:
        gst_mve_end_chunk (mve);
        break;
      case MVE_OC_CREATE_TIMER:
        ret = gst_mve_timer_create (mve, data, len, send);
        *stream = mve->audio_stream;
        break;
      case MVE_OC_AUDIO_BUFFERS:
        ret = gst_mve_audio_init (mve, version, data, len);
        break;
      case MVE_OC_VIDEO_BUFFERS:
        ret = gst_mve_video_create_buffer (mve, version, data, len);
        break;
      case MVE_OC_AUDIO_DATA:
      case MVE_OC_AUDIO_SILENCE:
        ret = gst_mve_audio_data (mve, type, data, len, send);
        *stream = mve->audio_stream;
        break;
      case MVE_OC_VIDEO_MODE:
        ret = gst_mve_video_init (mve, data);
        break;
      case MVE_OC_PALETTE:
        ret = gst_mve_video_palette (mve, data, len);
        break;
      case MVE_OC_PALETTE_COMPRESSED:
        ret = gst_mve_video_palette_compressed (mve, data, len);
        break;
      case MVE_OC_CODE_MAP:
        ret = gst_mve_video_code_map (mve, data, len);
        break;
      case MVE_OC_VIDEO_DATA:
        ret = gst_mve_video_data (mve, data, len, send);
        *stream = mve->video_stream;
        break;

      case MVE_OC_END_OF_STREAM:
      case MVE_OC_PLAY_AUDIO:
      case MVE_OC_PLAY_VIDEO:
        /* these are chunks we don't need to handle */
        GST_LOG_OBJECT (mve, "ignored segment type:0x%02x, version:0x%02x",
            type, version);
        break;
      case 0x13:               /* ??? */
      case 0x14:               /* ??? */
      case 0x15:               /* ??? */
        /* these are chunks we know exist but we don't care about */
        GST_DEBUG_OBJECT (mve,
            "known but unhandled segment type:0x%02x, version:0x%02x", type,
            version);
        break;
      default:
        GST_WARNING_OBJECT (mve,
            "unhandled segment type:0x%02x, version:0x%02x", type, version);
        break;
    }
  }

  gst_adapter_flush (mve->adapter, mve->needed_bytes);
  return ret;
}

static GstFlowReturn
gst_mve_demux_chain (GstPad * sinkpad, GstBuffer * inbuf)
{
  GstMveDemux *mve = GST_MVE_DEMUX (GST_PAD_PARENT (sinkpad));
  GstFlowReturn ret = GST_FLOW_OK;

  gst_adapter_push (mve->adapter, inbuf);

  GST_DEBUG_OBJECT (mve, "queuing buffer, needed:%d, available:%u",
      mve->needed_bytes, gst_adapter_available (mve->adapter));

  while ((gst_adapter_available (mve->adapter) >= mve->needed_bytes) &&
      (ret == GST_FLOW_OK)) {
    GstMveDemuxStream *stream = NULL;
    GstBuffer *outbuf = NULL;

    switch (mve->state) {
      case MVEDEMUX_STATE_INITIAL:
        gst_adapter_flush (mve->adapter, mve->needed_bytes);

        mve->chunk_offset += mve->needed_bytes;
        mve->needed_bytes = 4;
        mve->state = MVEDEMUX_STATE_NEXT_CHUNK;
        break;

      case MVEDEMUX_STATE_NEXT_CHUNK:{
        const guint8 *data;
        guint16 size;

        data = gst_adapter_peek (mve->adapter, mve->needed_bytes);
        size = GST_MVE_SEGMENT_SIZE (data);

        if (mve->chunk_offset >= mve->chunk_size) {
          /* new chunk, flush buffer and proceed with next segment */
          guint16 chunk_type = GST_READ_UINT16_LE (data + 2);

          gst_adapter_flush (mve->adapter, mve->needed_bytes);
          mve->chunk_size = size;
          mve->chunk_offset = 0;

          if (chunk_type > MVE_CHUNK_END) {
            GST_WARNING_OBJECT (mve,
                "skipping unknown chunk type 0x%02x of size:%u", chunk_type,
                size);
            mve->needed_bytes += size;
            mve->state = MVEDEMUX_STATE_SKIP;
          } else {
            GST_DEBUG_OBJECT (mve, "found new chunk type 0x%02x of size:%u",
                chunk_type, size);
          }
        } else if (mve->chunk_offset <= mve->chunk_size) {
          /* new segment */
          GST_DEBUG_OBJECT (mve, "found segment type 0x%02x of size:%u",
              GST_MVE_SEGMENT_TYPE (data), size);

          mve->needed_bytes += size;
          mve->state = MVEDEMUX_STATE_MOVIE;
        }
      }
        break;

      case MVEDEMUX_STATE_MOVIE:
        ret = gst_mve_parse_segment (mve, &stream, &outbuf);

        if ((ret == GST_FLOW_OK) && (outbuf != NULL)) {
          /* send buffer */
          GST_DEBUG_OBJECT (mve,
              "pushing buffer with time %" GST_TIME_FORMAT
              " (%u bytes) on pad %s",
              GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
              GST_BUFFER_SIZE (outbuf), GST_PAD_NAME (stream->pad));

          ret = gst_pad_push (stream->pad, outbuf);
          stream->last_flow = ret;
        }

        if (ret == GST_FLOW_NOT_LINKED) {
          if (mve->audio_stream
              && mve->audio_stream->last_flow != GST_FLOW_NOT_LINKED)
            ret = GST_FLOW_OK;
          if (mve->video_stream
              && mve->video_stream->last_flow != GST_FLOW_NOT_LINKED)
            ret = GST_FLOW_OK;
        }

        /* update current offset */
        mve->chunk_offset += mve->needed_bytes;

        mve->state = MVEDEMUX_STATE_NEXT_CHUNK;
        mve->needed_bytes = 4;
        break;

      case MVEDEMUX_STATE_SKIP:
        mve->chunk_offset += mve->needed_bytes;
        gst_adapter_flush (mve->adapter, mve->needed_bytes);
        mve->state = MVEDEMUX_STATE_NEXT_CHUNK;
        mve->needed_bytes = 4;
        break;

      default:
        GST_ERROR_OBJECT (mve, "invalid state: %d", mve->state);
        break;
    }
  }

  return ret;
}

static void
gst_mve_demux_dispose (GObject * obj)
{
  GstMveDemux *mve = GST_MVE_DEMUX (obj);

  if (mve->adapter) {
    g_object_unref (mve->adapter);
    mve->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_mve_demux_base_init (GstMveDemuxClass * klass)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &vidsrc_template);
  gst_element_class_add_static_pad_template (element_class, &audsrc_template);

  gst_element_class_set_static_metadata (element_class, "MVE Demuxer",
      "Codec/Demuxer",
      "Demultiplex an Interplay movie (MVE) stream into audio and video",
      "Jens Granseuer <jensgr@gmx.net>");
}

static void
gst_mve_demux_class_init (GstMveDemuxClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_mve_demux_dispose);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mve_demux_change_state);
}

static void
gst_mve_demux_init (GstMveDemux * mve)
{
  mve->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (mve->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mve_demux_chain));
  gst_element_add_pad (GST_ELEMENT (mve), mve->sinkpad);

  mve->adapter = gst_adapter_new ();
  gst_mve_demux_reset (mve);
}

GType
gst_mve_demux_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    const GTypeInfo plugin_info = {
      sizeof (GstMveDemuxClass),
      (GBaseInitFunc) gst_mve_demux_base_init,
      NULL,
      (GClassInitFunc) gst_mve_demux_class_init,
      NULL,
      NULL,
      sizeof (GstMveDemux),
      0,
      (GInstanceInitFunc) gst_mve_demux_init,
    };

    GST_DEBUG_CATEGORY_INIT (mvedemux_debug, "mvedemux",
        0, "Interplay MVE movie demuxer");

    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstMveDemux", &plugin_info, 0);
  }
  return plugin_type;
}
