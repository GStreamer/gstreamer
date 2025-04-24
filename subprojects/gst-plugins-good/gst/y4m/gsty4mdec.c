/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
 * Copyright (C) 2025 Igalia, S.L.
 *                    author Victor Jaquez <vjaquez@igalia.com>
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
 */
/**
 * SECTION:element-y4mdec
 * @title: gsty4mdec
 *
 * The gsty4mdec element decodes uncompressed video in YUV4MPEG format.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v filesrc location=file.y4m ! y4mdec ! xvimagesink
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/gstbytereader.h>

#include <stdlib.h>
#include <string.h>

#include "gsty4mdec.h"
#include "gsty4mformat.h"

#define MAX_SIZE 32768
#define MAX_STREAM_HEADER_LENGTH 128

#define Y4M_STREAM_MAGIC "YUV4MPEG2"
#define Y4M_STREAM_MAGIC_LEN 9
#define Y4M_FRAME_MAGIC "FRAME"
#define Y4M_FRAME_MAGIC_LEN 5

GST_DEBUG_CATEGORY (y4mdec_debug);
#define GST_CAT_DEFAULT y4mdec_debug

/* prototypes */

static gboolean gst_y4m_dec_stop (GstBaseParse * parse);
static gboolean gst_y4m_dec_start (GstBaseParse * parse);
static GstFlowReturn gst_y4m_dec_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);
static gboolean gst_y4m_dec_sink_query (GstBaseParse * parse, GstQuery * query);
static gboolean gst_y4m_dec_sink_event (GstBaseParse * parse, GstEvent * event);
static gboolean gst_y4m_dec_src_event (GstBaseParse * parse, GstEvent * event);

/* pad templates */

static GstStaticPadTemplate gst_y4m_dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-yuv4mpeg, y4mversion=2")
    );

static GstStaticPadTemplate gst_y4m_dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ \
        I420,Y41B,Y42B,Y444,                 \
        I420_10LE,I422_10LE,Y444_10LE,       \
        I420_12LE,I422_12LE,Y444_12LE,       \
        Y444_16LE,GRAY8,GRAY16_LE            \
    }")));

/* class initialization */
#define gst_y4m_dec_parent_class parent_class
G_DEFINE_TYPE (GstY4mDec, gst_y4m_dec, GST_TYPE_BASE_PARSE);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (y4mdec, "y4mdec", GST_RANK_SECONDARY,
    gst_y4m_dec_get_type (), GST_DEBUG_CATEGORY_INIT (y4mdec_debug, "y4mdec", 0,
        "y4mdec element"));

enum ParserState
{
  PARSER_STATE_NONE = 1 << 0,
  PARSER_STATE_GOT_HEADER = 1 << 1,
  PARSER_STATE_GOT_FRAME = 2 << 1,
};

static void
gst_y4m_dec_class_init (GstY4mDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  parse_class->stop = GST_DEBUG_FUNCPTR (gst_y4m_dec_stop);
  parse_class->start = GST_DEBUG_FUNCPTR (gst_y4m_dec_start);
  parse_class->handle_frame = GST_DEBUG_FUNCPTR (gst_y4m_dec_handle_frame);
  parse_class->sink_query = GST_DEBUG_FUNCPTR (gst_y4m_dec_sink_query);
  parse_class->sink_event = GST_DEBUG_FUNCPTR (gst_y4m_dec_sink_event);
  parse_class->src_event = GST_DEBUG_FUNCPTR (gst_y4m_dec_src_event);

  gst_element_class_add_static_pad_template (element_class,
      &gst_y4m_dec_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_y4m_dec_sink_template);

  gst_element_class_set_static_metadata (element_class,
      "YUV4MPEG demuxer/decoder", "Codec/Demuxer",
      "Demuxes/decodes YUV4MPEG streams", "David Schleef <ds@schleef.org>\n"
      "Victor Jaquez <vjaquez@igalia.com>");
}

static void
gst_y4m_dec_init (GstY4mDec * y4mdec)
{
}

static gboolean
gst_y4m_dec_reset (GstBaseParse * parse)
{
  GstY4mDec *y4mdec = GST_Y4M_DEC (parse);

  GST_TRACE_OBJECT (y4mdec, "start");

  y4mdec->state = PARSER_STATE_NONE;

  gst_base_parse_set_min_frame_size (parse, MAX_STREAM_HEADER_LENGTH);

  return TRUE;
}

static gboolean
gst_y4m_dec_start (GstBaseParse * parse)
{
  return gst_y4m_dec_reset (parse);
}

static gboolean
gst_y4m_dec_stop (GstBaseParse * parse)
{
  GstY4mDec *y4mdec = GST_Y4M_DEC (parse);

  if (y4mdec->pool) {
    gst_buffer_pool_set_active (y4mdec->pool, FALSE);
    gst_clear_object (&y4mdec->pool);
  }

  return TRUE;
}

static GstVideoFormat
parse_colorspace (const char *param)
{
  char *end;
  guint iformat = g_ascii_strtoull (param, &end, 10);

  if (*end == '\0') {
    switch (iformat) {
      case 420:
        return GST_VIDEO_FORMAT_I420;
      case 411:
        return GST_VIDEO_FORMAT_Y41B;
      case 422:
        return GST_VIDEO_FORMAT_Y42B;
      case 444:
        return GST_VIDEO_FORMAT_Y444;
    }
  }

  /*
   * Parse non-standard (i.e., unknown to mjpegtools) streams that are
   * generated by FFmpeg:
   * https://wiki.multimedia.cx/index.php/YUV4MPEG2
   * https://github.com/FFmpeg/FFmpeg/blob/eee3b7e2/libavformat/yuv4mpegenc.c#L74-L166
   * Will assume little-endian because this is an on-disk serialization format.
   */

  // TODO: Differentiate between:
  // * C420jpeg: biaxially-displaced chroma planes
  // * C420paldv: coincident R and vertically-displaced B
  // * C420mpeg2: vertically-displaced chroma planes
  if (iformat == 420 && (g_strcmp0 (end, "jpeg") == 0 ||
          g_strcmp0 (end, "paldv") == 0 || g_strcmp0 (end, "mpeg2") == 0))
    return GST_VIDEO_FORMAT_I420;

  if (iformat == 0 && strncmp (end, "mono", 4) == 0) {
    char *type = end + 4;
    if (*type == '\0')
      return GST_VIDEO_FORMAT_GRAY8;
    if (g_strcmp0 (type, "16") == 0)
      return GST_VIDEO_FORMAT_GRAY16_LE;
  }

  if (*end == 'p') {
    guint depth = g_ascii_strtoull (end + 1, NULL, 10);
    if (depth == 10) {
      switch (iformat) {
        case 420:
          return GST_VIDEO_FORMAT_I420_10LE;
        case 422:
          return GST_VIDEO_FORMAT_I422_10LE;
        case 444:
          return GST_VIDEO_FORMAT_Y444_10LE;
      }
    } else if (depth == 12) {
      switch (iformat) {
        case 420:
          return GST_VIDEO_FORMAT_I420_12LE;
        case 422:
          return GST_VIDEO_FORMAT_I422_12LE;
        case 444:
          return GST_VIDEO_FORMAT_Y444_12LE;
      }
    } else if (depth == 16 && iformat == 444) {
      return GST_VIDEO_FORMAT_Y444_16LE;
    }
  }

  GST_WARNING ("%s is not a supported format", param);
  return GST_VIDEO_FORMAT_UNKNOWN;
}

static gboolean
parse_ratio (const char *param, gulong * n, gulong * d)
{
  char *end;
  *n = g_ascii_strtoull (param, &end, 10);
  if (end == param)
    return FALSE;
  param = end;
  if (param[0] != ':')
    return FALSE;
  param++;
  *d = g_ascii_strtoull (param, &end, 10);
  if (end == param)
    return FALSE;
  return TRUE;
}

static gboolean
gst_y4m_dec_parse_header (GstY4mDec * y4mdec, const char *header)
{
  guint len;
  char **params;
  guint interlaced_char = 0;
  gulong fps_n = 0, fps_d = 0;
  gulong par_n = 0, par_d = 0;
  gulong width = 0, height = 0;
  GstVideoFormat format = GST_VIDEO_FORMAT_I420;
  GstVideoInterlaceMode interlace_mode;

  if (memcmp (header, "YUV4MPEG2 ", 10) != 0) {
    GST_ERROR_OBJECT (y4mdec, "y4m start code not found");
    return FALSE;
  }

  header += 10;
  if (!g_str_is_ascii (header)) {
    GST_ERROR_OBJECT (y4mdec, "Invalid non-ASCII y4m header: %s", header);
    return FALSE;
  }

  GST_INFO_OBJECT (y4mdec, "Found header: %s", header);
  params = g_strsplit (header, " ", -1);
  len = g_strv_length (params);

  for (int i = 0; i < len; i++) {
    const char *param = params[i];
    char param_type = *param;
    const char *param_value = param + 1;
    switch (param_type) {
      case 'C':
        format = parse_colorspace (param_value);
        if (format == GST_VIDEO_FORMAT_UNKNOWN) {
          GST_ERROR_OBJECT (y4mdec, "Failed to parse colorspace: %s", param);
          return FALSE;
        }
        GST_INFO_OBJECT (y4mdec, "Parsed format as %s",
            gst_video_format_to_string (format));
        continue;
      case 'W':
        if ((width = g_ascii_strtoull (param_value, NULL, 10)) == 0) {
          GST_ERROR_OBJECT (y4mdec, "Failed to parse width: %s", param);
          return FALSE;
        }
        continue;
      case 'H':
        if ((height = g_ascii_strtoull (param_value, NULL, 10)) == 0) {
          GST_ERROR_OBJECT (y4mdec, "Failed to parse height: %s", param);
          return FALSE;
        }
        continue;
      case 'I':
        if ((interlaced_char = param_value[0]) == 0) {
          GST_ERROR_OBJECT (y4mdec, "Expecting interlaced flag: %s", param);
          return FALSE;
        }
        continue;
      case 'F':
        if (!parse_ratio (param_value, &fps_n, &fps_d)) {
          GST_ERROR_OBJECT (y4mdec, "Failed to parse framerate: %s", param);
          return FALSE;
        }
        continue;
      case 'A':
        if (!parse_ratio (param_value, &par_n, &par_d)) {
          GST_ERROR_OBJECT (y4mdec, "Failed to parse PAR: %s", param);
          return FALSE;
        }
        continue;
    }
    GST_WARNING_OBJECT (y4mdec, "Unknown y4m param field '%s', ignoring",
        param);
  }
  g_strfreev (params);

  if (width > MAX_SIZE || height > MAX_SIZE) {
    GST_ERROR_OBJECT (y4mdec, "Dimensions %lux%lu out of range", width, height);
    return FALSE;
  }

  switch (interlaced_char) {
    case 0:
    case '?':
    case 'p':
      interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
      break;
    case 't':
    case 'b':
      interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
      break;
    default:
      GST_ERROR_OBJECT (y4mdec, "Unknown interlaced char '%c'",
          interlaced_char);
      return FALSE;
      break;
  }

  if (fps_n == 0)
    fps_n = 1;
  if (fps_d == 0)
    fps_d = 1;
  if (par_n == 0)
    par_n = 1;
  if (par_d == 0)
    par_d = 1;

  gst_video_info_set_interlaced_format (&y4mdec->out_info, format,
      interlace_mode, width, height);

  GST_VIDEO_INFO_FPS_N (&y4mdec->out_info) = fps_n;
  GST_VIDEO_INFO_FPS_D (&y4mdec->out_info) = fps_d;
  GST_VIDEO_INFO_PAR_N (&y4mdec->out_info) = par_n;
  GST_VIDEO_INFO_PAR_D (&y4mdec->out_info) = par_d;

  if (!gst_y4m_video_unpadded_info (&y4mdec->info, &y4mdec->out_info))
    return FALSE;

  y4mdec->passthrough =
      gst_video_info_is_equal (&y4mdec->info, &y4mdec->out_info);

  return TRUE;
}

static inline gboolean
gst_y4m_dec_negotiate_pool (GstY4mDec * y4mdec, GstCaps * caps)
{
  GstBaseParse *parse = GST_BASE_PARSE (y4mdec);
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;
  GstAllocationParams params = { 0, };
  GstStructure *config;
  guint size = GST_VIDEO_INFO_SIZE (&y4mdec->out_info), min = 0, max = 0;
  GstQuery *query;
  gboolean our_pool = FALSE, ret;

  if (y4mdec->pool) {
    gst_buffer_pool_set_active (y4mdec->pool, FALSE);
    gst_object_unref (y4mdec->pool);
  }
  y4mdec->pool = NULL;
  y4mdec->has_video_meta = FALSE;

  query = gst_query_new_allocation (caps, FALSE);
  if (gst_pad_peer_query (GST_BASE_PARSE_SRC_PAD (parse), query)) {
    y4mdec->has_video_meta =
        gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

    if (gst_query_get_n_allocation_params (query) > 0) {
      gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    }

    if (gst_query_get_n_allocation_pools (query) > 0) {
      gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
      size = MAX (size, GST_VIDEO_INFO_SIZE (&y4mdec->out_info));
    }
  }

  do {
    if (!pool) {
      pool = gst_video_buffer_pool_new ();
      our_pool = TRUE;
    }

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, min, max);
    gst_buffer_pool_config_set_allocator (config, allocator, &params);

    gst_clear_object (&allocator);

    ret = gst_buffer_pool_set_config (pool, config);
    if (!ret) {
      if (our_pool) {
        GST_ERROR_OBJECT (y4mdec, "pool %" GST_PTR_FORMAT
            " doesn't accept configuration", pool);
        gst_clear_object (&pool);
        /* bail */
        break;
      } else {
        GST_WARNING_OBJECT (y4mdec, "pool %" GST_PTR_FORMAT "doesn't accept "
            "configuration. Trying an internal pool", pool);
        max = min = 0;
        gst_clear_object (&pool);
      }
    }
  } while (!ret);

  y4mdec->pool = pool;

  gst_query_unref (query);
  gst_caps_unref (caps);

  return ret;
}

static inline gboolean
gst_y4m_dec_negotiate (GstY4mDec * y4mdec)
{
  GstBaseParse *parse = GST_BASE_PARSE (y4mdec);
  GstCaps *caps;

  caps = gst_video_info_to_caps (&y4mdec->out_info);
  if (!gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps)) {
    GST_ERROR_OBJECT (y4mdec, "Failed to set caps in src pad: %" GST_PTR_FORMAT,
        caps);
    gst_caps_unref (caps);
    return FALSE;
  }

  return gst_y4m_dec_negotiate_pool (y4mdec, caps);
}

static inline gboolean
gst_y4m_dec_parse_magic (GstY4mDec * y4mdec, gpointer data, gsize size,
    char header[MAX_STREAM_HEADER_LENGTH + 10])
{
  GstByteReader br;
  guint i;

  gst_byte_reader_init (&br, data, size);

  /* what ever until '\n' */
  for (i = 0; i < MAX_STREAM_HEADER_LENGTH; i++) {
    header[i] = gst_byte_reader_get_uint8_unchecked (&br);
    if (header[i] == '\n') {
      header[i] = 0;
      break;
    }
  }

  if (i == MAX_STREAM_HEADER_LENGTH) {
    GST_ERROR_OBJECT (y4mdec, "Y4M header is too large");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_y4m_dec_process_header (GstY4mDec * y4mdec, GstBuffer * buffer,
    gint * skipsize)
{
  GstMapInfo mapinfo;
  char stream_hdr[MAX_STREAM_HEADER_LENGTH + 10];
  gboolean ret = FALSE;

  if (!gst_buffer_map (buffer, &mapinfo, GST_MAP_READ)) {
    GST_ERROR_OBJECT (y4mdec, "Cannot map input buffer");
    return FALSE;
  }

  g_assert (mapinfo.size >= MAX_STREAM_HEADER_LENGTH);

  if (!gst_y4m_dec_parse_magic (y4mdec, mapinfo.data, mapinfo.size, stream_hdr))
    goto bail;

  if (!gst_y4m_dec_parse_header (y4mdec, stream_hdr))
    goto bail;

  if (!gst_y4m_dec_negotiate (y4mdec))
    goto bail;

  ret = TRUE;

  /* let's skip the stream header + '\n' */
  *skipsize = strlen (stream_hdr) + 1;

bail:
  gst_buffer_unmap (buffer, &mapinfo);
  return ret;
}

enum
{
  FRAME_NOT_FOUND = -1,
  HEADER_RESYNC = -2,
};

inline static gint
gst_y4m_dec_frame_hdr_len (GstY4mDec * y4mdec, GstBuffer * buffer,
    gsize * buffer_size)
{
  GstMapInfo mapinfo;
  char frame_hdr[MAX_STREAM_HEADER_LENGTH + 10];
  gint ret = FRAME_NOT_FOUND;

  if (!gst_buffer_map (buffer, &mapinfo, GST_MAP_READ))
    return FALSE;

  if (!gst_y4m_dec_parse_magic (y4mdec, mapinfo.data, mapinfo.size, frame_hdr))
    goto bail;

  if (strncmp (frame_hdr, Y4M_FRAME_MAGIC, Y4M_FRAME_MAGIC_LEN) != 0) {
    if (strncmp (frame_hdr, Y4M_STREAM_MAGIC, Y4M_STREAM_MAGIC_LEN) == 0) {
      return HEADER_RESYNC;
    } else {
      GST_ERROR_OBJECT (y4mdec, "Frame header not found");
      goto bail;
    }
  }

  /* FRAME + '\n' */
  ret = strlen (frame_hdr) + 1;
  *buffer_size = mapinfo.size;

bail:
  gst_buffer_unmap (buffer, &mapinfo);

  return ret;
}

static GstFlowReturn
gst_y4m_dec_copy_buffer (GstY4mDec * y4mdec, GstBuffer * in_buffer,
    GstBuffer ** out_buffer_ptr)
{
  GstVideoFrame in_frame, out_frame;
  GstFlowReturn ret;
  gboolean copied;
  GstBuffer *out_buffer = NULL;

  /* do memcpy hopefully in next element's pool */
  g_assert (y4mdec->pool);

  if (!gst_buffer_pool_set_active (y4mdec->pool, TRUE)) {
    GST_ERROR_OBJECT (y4mdec, "Cannot activate internal pool");
    goto error;
  }

  ret = gst_buffer_pool_acquire_buffer (y4mdec->pool, &out_buffer, NULL);
  if (ret != GST_FLOW_OK) {
    return ret;
  }

  if (!gst_video_frame_map (&in_frame, &y4mdec->info, in_buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (y4mdec, "Cannot map input frame");
    goto error;
  }

  if (!gst_video_frame_map (&out_frame, &y4mdec->out_info, out_buffer,
          GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (y4mdec, "Cannot map output frame");
    gst_video_frame_unmap (&in_frame);
    goto error;
  }

  copied = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  if (!copied) {
    GST_ERROR_OBJECT (y4mdec, "Cannot copy frame");
    goto error;
  }

  *out_buffer_ptr = out_buffer;

  return GST_FLOW_OK;

error:
  {
    if (out_buffer)
      gst_buffer_unref (out_buffer);
    return GST_FLOW_ERROR;
  }
}

static inline gboolean
_buffer_memory_is_aligned (GstBuffer * buffer, gboolean * is_aligned)
{
  GstMapInfo mapinfo;

  if (!gst_buffer_map (buffer, &mapinfo, GST_MAP_READ))
    return FALSE;

  /* check for 4 bytes alignment required for raw video */
  *is_aligned = ((guintptr) mapinfo.data & 3) == 0;

  gst_buffer_unmap (buffer, &mapinfo);
  return TRUE;
}

static GstFlowReturn
gst_y4m_dec_handle_frame (GstBaseParse * parse, GstBaseParseFrame * frame,
    gint * skipsize)
{
  GstY4mDec *y4mdec = GST_Y4M_DEC (parse);
  gsize frame_size, buffer_size = 0;
  gint frame_hdr_len;
  gboolean is_aligned;

  GST_TRACE_OBJECT (y4mdec, "frame %" GST_PTR_FORMAT, frame->buffer);

  do {
    switch (y4mdec->state) {
      case PARSER_STATE_NONE:
        /* TODO: find the header and dismiss previous garbage  (orc-based memem?) */
        if (!gst_y4m_dec_process_header (y4mdec, frame->buffer, skipsize))
          goto error;

        /* reconfigure baseparse */
        {
          gst_base_parse_set_frame_rate (parse,
              GST_VIDEO_INFO_FPS_N (&y4mdec->out_info),
              GST_VIDEO_INFO_FPS_D (&y4mdec->out_info), 0, 0);

          /* update min frame size to input size */
          gst_base_parse_set_min_frame_size (parse,
              GST_VIDEO_INFO_SIZE (&y4mdec->info) + Y4M_FRAME_MAGIC_LEN + 1);
        }

        y4mdec->state = PARSER_STATE_GOT_HEADER;
        return GST_FLOW_OK;
      case PARSER_STATE_GOT_HEADER:
      case PARSER_STATE_GOT_FRAME:
        frame_hdr_len = gst_y4m_dec_frame_hdr_len (y4mdec, frame->buffer,
            &buffer_size);

        if (frame_hdr_len == FRAME_NOT_FOUND)
          goto error;

        if (frame_hdr_len == HEADER_RESYNC) {
          y4mdec->state = PARSER_STATE_NONE;
          break;
        }

        /* input frame size */
        frame_size = GST_VIDEO_INFO_SIZE (&y4mdec->info);

        /* frame is incomplete */
        if (frame_size > buffer_size - frame_hdr_len)
          return GST_FLOW_OK;

        y4mdec->state = PARSER_STATE_GOT_FRAME;

        /* remove frame-header */
        gst_buffer_resize (frame->buffer, frame_hdr_len, frame_size);

        if (!_buffer_memory_is_aligned (frame->buffer, &is_aligned))
          goto error;

        if (is_aligned && y4mdec->passthrough) {
          /* best case scenario  */
          frame->out_buffer = gst_buffer_ref (frame->buffer);
        } else if (is_aligned && y4mdec->has_video_meta) {
          /* delegate memcopy to next element */
          gst_buffer_add_video_meta_full (frame->buffer, 0,
              GST_VIDEO_INFO_FORMAT (&y4mdec->out_info),
              GST_VIDEO_INFO_WIDTH (&y4mdec->out_info),
              GST_VIDEO_INFO_HEIGHT (&y4mdec->out_info),
              GST_VIDEO_INFO_N_PLANES (&y4mdec->out_info),
              y4mdec->info.offset, y4mdec->info.stride);
          frame->out_buffer = gst_buffer_ref (frame->buffer);
        } else {
          GstFlowReturn ret;

          ret = gst_y4m_dec_copy_buffer (y4mdec, frame->buffer,
              &frame->out_buffer);
          if (ret == GST_FLOW_ERROR)
            goto error;
          else if (ret != GST_FLOW_OK)
            return ret;
        }

        GST_DEBUG_OBJECT (y4mdec, "output frame %" GST_PTR_FORMAT,
            frame->out_buffer);
        return gst_base_parse_finish_frame (parse, frame,
            frame_hdr_len + frame_size);

      default:
        GST_ERROR_OBJECT (y4mdec, "Invalid parser state");
        return GST_FLOW_ERROR;
    }
  } while (TRUE);

error:
  {
    GST_ELEMENT_ERROR (y4mdec, STREAM, DECODE,
        ("Failed to parse YUV4MPEG header"), (NULL));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_y4m_dec_sink_event (GstBaseParse * parse, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START) {
    if (!gst_y4m_dec_reset (parse))
      return FALSE;
  }

  return GST_BASE_PARSE_CLASS (parent_class)->sink_event (parse, event);
}

static gboolean
gst_y4m_dec_sink_query (GstBaseParse * parse, GstQuery * query)
{
  /* videoencoder (from y4menc) does allocation queries. Ignore them. */
  if (GST_QUERY_TYPE (query) == GST_QUERY_ALLOCATION)
    return FALSE;

  return GST_BASE_PARSE_CLASS (parent_class)->sink_query (parse, query);
}

static gboolean
gst_y4m_dec_src_event (GstBaseParse * parse, GstEvent * event)
{
  /* reject reverse playback */
  if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK) {
    gdouble rate;

    gst_event_parse_seek (event, &rate, NULL, NULL, NULL, NULL, NULL, NULL);
    if (rate < 0.0) {
      GstY4mDec *y4mdec = GST_Y4M_DEC (parse);

      GST_ERROR_OBJECT (y4mdec, "Reverse playback is not supported");
      return FALSE;
    }
  }

  return GST_BASE_PARSE_CLASS (parent_class)->src_event (parse, event);
}
