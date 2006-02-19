/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@temple-baptist.com>
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
/* Element-Checklist-Version: 5 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gst/riff/riff-media.h"
#include "gstavidemux.h"
#include "avi-ids.h"
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (avidemux_debug);
#define GST_CAT_DEFAULT avidemux_debug

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_EVENT);

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-msvideo")
    );

static void gst_avi_demux_base_init (GstAviDemuxClass * klass);
static void gst_avi_demux_class_init (GstAviDemuxClass * klass);
static void gst_avi_demux_init (GstAviDemux * avi);

static void gst_avi_demux_reset (GstAviDemux * avi);

#if 0
static const GstEventMask *gst_avi_demux_get_event_mask (GstPad * pad);
#endif
static gboolean gst_avi_demux_handle_src_event (GstPad * pad, GstEvent * event);

#if 0
static const GstFormat *gst_avi_demux_get_src_formats (GstPad * pad);
#endif
static const GstQueryType *gst_avi_demux_get_src_query_types (GstPad * pad);
static gboolean gst_avi_demux_handle_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_avi_demux_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

static gboolean gst_avi_demux_handle_seek (GstAviDemux * avi, gboolean update);
static void gst_avi_demux_loop (GstPad * pad);
static gboolean gst_avi_demux_sink_activate (GstPad * sinkpad);
static gboolean gst_avi_demux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static GstStateChangeReturn gst_avi_demux_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_avi_demux_get_type (void)
{
  static GType avi_demux_type = 0;

  if (!avi_demux_type) {
    static const GTypeInfo avi_demux_info = {
      sizeof (GstAviDemuxClass),
      (GBaseInitFunc) gst_avi_demux_base_init,
      NULL,
      (GClassInitFunc) gst_avi_demux_class_init,
      NULL,
      NULL,
      sizeof (GstAviDemux),
      0,
      (GInstanceInitFunc) gst_avi_demux_init,
    };

    avi_demux_type =
        g_type_register_static (GST_TYPE_ELEMENT,
        "GstAviDemux", &avi_demux_info, 0);
  }

  return avi_demux_type;
}

static void
gst_avi_demux_base_init (GstAviDemuxClass * klass)
{
  static GstElementDetails gst_avi_demux_details =
      GST_ELEMENT_DETAILS ("Avi demuxer",
      "Codec/Demuxer",
      "Demultiplex an avi file into audio and video",
      "Erik Walthinsen <omega@cse.ogi.edu>\n"
      "Wim Taymans <wim.taymans@chello.be>\n"
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *videosrctempl, *audiosrctempl;
  GstCaps *audcaps, *vidcaps;

  audcaps = gst_riff_create_audio_template_caps ();
  gst_caps_append (audcaps, gst_caps_new_simple ("audio/x-avi-unknown", NULL));
  audiosrctempl = gst_pad_template_new ("audio_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, audcaps);

  vidcaps = gst_riff_create_video_template_caps ();
  gst_caps_append (vidcaps, gst_riff_create_iavs_template_caps ());
  gst_caps_append (vidcaps, gst_caps_new_simple ("video/x-avi-unknown", NULL));
  videosrctempl = gst_pad_template_new ("video_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, vidcaps);

  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_set_details (element_class, &gst_avi_demux_details);
}

static void
gst_avi_demux_class_init (GstAviDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (avidemux_debug, "avidemux",
      0, "Demuxer for AVI streams");

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_avi_demux_change_state;
}

static void
gst_avi_demux_init (GstAviDemux * avi)
{
  avi->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_templ),
      "sink");
  gst_pad_set_activate_function (avi->sinkpad, gst_avi_demux_sink_activate);
  gst_pad_set_activatepull_function (avi->sinkpad,
      gst_avi_demux_sink_activate_pull);
  gst_element_add_pad (GST_ELEMENT (avi), avi->sinkpad);

  gst_avi_demux_reset (avi);

  avi->index_entries = NULL;
  memset (&avi->stream, 0, sizeof (avi->stream));
}

static void
gst_avi_demux_reset (GstAviDemux * avi)
{
  gint i;

  for (i = 0; i < avi->num_streams; i++) {
    g_free (avi->stream[i].strh);
    g_free (avi->stream[i].strf.data);
    if (avi->stream[i].name)
      g_free (avi->stream[i].name);
    if (avi->stream[i].initdata)
      gst_buffer_unref (avi->stream[i].initdata);
    if (avi->stream[i].extradata)
      gst_buffer_unref (avi->stream[i].extradata);
    if (avi->stream[i].pad)
      gst_element_remove_pad (GST_ELEMENT (avi), avi->stream[i].pad);
  }
  memset (&avi->stream, 0, sizeof (avi->stream));

  avi->num_streams = 0;
  avi->num_v_streams = 0;
  avi->num_a_streams = 0;

  avi->state = GST_AVI_DEMUX_START;
  avi->offset = 0;

  g_free (avi->index_entries);
  avi->index_entries = NULL;
  avi->index_size = 0;
  avi->index_offset = 0;
  avi->current_entry = 0;
  g_free (avi->avih);
  avi->avih = NULL;

  if (avi->seek_event)
    gst_event_unref (avi->seek_event);
  avi->seek_event = NULL;

  avi->segment_rate = 1.0;
  avi->segment_flags = 0;
  avi->segment_start = -1;
  avi->segment_stop = -1;
}

static gst_avi_index_entry *
gst_avi_demux_index_next (GstAviDemux * avi, gint stream_nr, gint start)
{
  gint i;
  gst_avi_index_entry *entry = NULL;

  for (i = start; i < avi->index_size; i++) {
    entry = &avi->index_entries[i];
    if (entry->stream_nr == stream_nr)
      break;
  }

  return entry;
}

static gst_avi_index_entry *
gst_avi_demux_index_entry_for_time (GstAviDemux * avi,
    gint stream_nr, guint64 time, guint32 flags)
{
  gst_avi_index_entry *entry = NULL, *last_entry = NULL;
  gint i;

  GST_LOG_OBJECT (avi, "stream_nr:%d , time:%" GST_TIME_FORMAT " flags:%d",
      stream_nr, GST_TIME_ARGS (time), flags);
  i = -1;
  do {
    entry = gst_avi_demux_index_next (avi, stream_nr, i + 1);
    if (!entry)
      return NULL;

    i = entry->index_nr;

    GST_LOG_OBJECT (avi,
        "looking at entry %d / ts:%" GST_TIME_FORMAT " / dur:%" GST_TIME_FORMAT
        " flags:%d", i, GST_TIME_ARGS (entry->ts), GST_TIME_ARGS (entry->dur),
        entry->flags);
    if (entry->ts <= time && (entry->flags & flags) == flags)
      last_entry = entry;
  } while (entry->ts < time);

  return last_entry;
}


#if 0
static const GstFormat *
gst_avi_demux_get_src_formats (GstPad * pad)
{
  avi_stream_context *stream = gst_pad_get_element_private (pad);

  static const GstFormat src_a_formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    0
  };
  static const GstFormat src_v_formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,
    0
  };

  return (stream->strh->type == GST_RIFF_FCC_auds ?
      src_a_formats : src_v_formats);
}
#endif

static gboolean
gst_avi_demux_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstAviDemux *avidemux = GST_AVI_DEMUX (gst_pad_get_parent (pad));

  avi_stream_context *stream = gst_pad_get_element_private (pad);

  GST_LOG_OBJECT (avidemux,
      "Received  src_format:%d, src_value:%lld, dest_format:%d", src_format,
      src_value, *dest_format);

  if (src_format == *dest_format) {
    *dest_value = src_value;
    goto done;
  }
  if (!stream->strh || !stream->strf.data) {
    res = FALSE;
    goto done;
  }
  if (stream->strh->type == GST_RIFF_FCC_vids &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES)) {
    res = FALSE;
    goto done;
  }

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value =
              gst_util_uint64_scale_int (src_value, stream->strf.auds->av_bps,
              GST_SECOND);
          break;
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale (src_value, stream->strh->rate,
              stream->strh->scale * GST_SECOND);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          if (stream->strf.auds->av_bps != 0) {
            *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
                stream->strf.auds->av_bps);
          } else
            res = FALSE;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value =
              gst_util_uint64_scale (src_value,
              stream->strh->scale * GST_SECOND, stream->strh->rate);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
  }

done:
  GST_LOG_OBJECT (avidemux, "Returning res:%d dest_format:%d dest_value:%lld",
      res, *dest_format, *dest_value);
  gst_object_unref (avidemux);
  return res;
}

static const GstQueryType *
gst_avi_demux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType src_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return src_types;
}

static gboolean
gst_avi_demux_handle_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstAviDemux *demux = GST_AVI_DEMUX (GST_PAD_PARENT (pad));

  avi_stream_context *stream = gst_pad_get_element_private (pad);

  if (!stream->strh || !stream->strf.data)
    return FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:{
      gint64 pos = 0;

      if (stream->strh->type == GST_RIFF_FCC_auds) {
        if (!stream->strh->samplesize) {
          pos = gst_util_uint64_scale_int ((gint64) stream->current_frame *
              stream->strh->scale, GST_SECOND, stream->strh->rate);
        } else if (stream->strf.auds->av_bps != 0) {
          pos = gst_util_uint64_scale_int (stream->current_byte, GST_SECOND,
              stream->strf.auds->av_bps);
        } else if (stream->total_frames != 0 && stream->total_bytes != 0) {
          /* calculate timestamps based on video size */
          guint64 xlen = demux->avih->us_frame *
              demux->avih->tot_frames * GST_USECOND;

          if (!stream->strh->samplesize)
            pos = gst_util_uint64_scale_int (xlen, stream->current_frame,
                stream->total_frames);
          else
            pos = gst_util_uint64_scale_int (xlen, stream->current_byte,
                stream->total_bytes);
        } else {
          res = FALSE;
        }
      } else {
        if (stream->strh->rate != 0) {
          pos =
              gst_util_uint64_scale_int ((guint64) stream->current_frame *
              stream->strh->scale, GST_SECOND, stream->strh->rate);
        } else {
          pos = stream->current_frame * demux->avih->us_frame * GST_USECOND;
        }
      }
      if (res)
        gst_query_set_position (query, GST_FORMAT_TIME, pos);
      break;
    }
    case GST_QUERY_DURATION:
    {
      gint64 len;

      len =
          gst_util_uint64_scale ((guint64) stream->strh->length *
          stream->strh->scale, GST_SECOND, stream->strh->rate);
      gst_query_set_duration (query, GST_FORMAT_TIME, len);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  return res;
}

#if 0
static const GstEventMask *
gst_avi_demux_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_KEY_UNIT},
    {0,}
  };

  return masks;
}
#endif

static gboolean
gst_avi_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstAviDemux *avi = GST_AVI_DEMUX (GST_PAD_PARENT (pad));
  avi_stream_context *stream;

  GST_CAT_DEBUG_OBJECT (GST_CAT_EVENT, avi,
      "have event type %s: %p on src pad", GST_EVENT_TYPE_NAME (event), event);
  if (!avi->index_entries) {
    GST_CAT_DEBUG_OBJECT (GST_CAT_EVENT, avi, "no index entries, returning");
    return FALSE;
  }

  stream = gst_pad_get_element_private (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      /* FIXME, this seeking code is not correct, look at wavparse for 
       * a better example */
      GstFormat format;
      GstSeekFlags flags;
      gdouble rate;
      gint64 start, stop;
      gint64 tstart, tstop;
      gint64 duration;
      GstFormat tformat = GST_FORMAT_TIME;
      GstSeekType start_type, stop_type;
      gboolean update_start = TRUE;
      gboolean update_stop = TRUE;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      GST_DEBUG_OBJECT (avi,
          "seek format %d, flags:%d, %08x, start:%lld, stop:%lld", format,
          flags, stream->strh->type, start, stop);

      if (format != GST_FORMAT_TIME) {
        res &=
            gst_avi_demux_src_convert (pad, format, start, &tformat, &tstart);
        res &= gst_avi_demux_src_convert (pad, format, stop, &tformat, &tstop);
        if (!res)
          goto done;
      } else {
        tstart = start;
        tstop = stop;
      }

      duration =
          gst_util_uint64_scale ((guint64) stream->strh->length *
          stream->strh->scale, GST_SECOND, stream->strh->rate);

      switch (start_type) {
        case GST_SEEK_TYPE_CUR:
          tstart = avi->segment_start + tstart;
          break;
        case GST_SEEK_TYPE_END:
          tstart = duration + tstart;
          break;
        case GST_SEEK_TYPE_NONE:
          tstart = avi->segment_start;
          update_start = FALSE;
          break;
        case GST_SEEK_TYPE_SET:
          break;
      }
      tstart = CLAMP (tstart, 0, duration);

      switch (stop_type) {
        case GST_SEEK_TYPE_CUR:
          tstop = avi->segment_stop + tstop;
          break;
        case GST_SEEK_TYPE_END:
          tstop = duration + tstop;
          break;
        case GST_SEEK_TYPE_NONE:
          tstop = avi->segment_stop;
          update_stop = FALSE;
          break;
        case GST_SEEK_TYPE_SET:
          break;
      }
      tstop = CLAMP (tstop, 0, duration);

      /* now store the values */
      avi->segment_rate = rate;
      avi->segment_flags = flags;
      avi->segment_start = tstart;
      avi->segment_stop = tstop;

      gst_avi_demux_handle_seek (avi, update_start || update_stop);
      break;

    }
    default:
      res = FALSE;
      break;
  }

done:
  gst_event_unref (event);

  return res;
}

/**
 * gst_avi_demux_parse_file_header:
 * @element: caller element (used for errors/debug).
 * @buf: input data to be used for parsing.
 *
 * "Open" a RIFF/AVI file. The buffer should be at least 12
 * bytes long. Discards buffer after use.
 *
 * Returns: TRUE if the file is a RIFF/AVI file, FALSE otherwise.
 *          Throws an error, caller should error out (fatal).
 */

static gboolean
gst_avi_demux_parse_file_header (GstElement * element, GstBuffer * buf)
{
  guint32 doctype;

  if (!gst_riff_parse_file_header (element, buf, &doctype))
    return FALSE;

  if (doctype != GST_RIFF_RIFF_AVI)
    goto not_avi;

  return TRUE;

  /* ERRORS */
not_avi:
  {
    GST_ELEMENT_ERROR (element, STREAM, WRONG_TYPE, (NULL),
        ("File is not an AVI file: %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (doctype)));
    return FALSE;
  }
}

static GstFlowReturn
gst_avi_demux_stream_init (GstAviDemux * avi)
{
  GstFlowReturn res;
  GstBuffer *buf = NULL;

  if ((res = gst_pad_pull_range (avi->sinkpad,
              avi->offset, 12, &buf)) != GST_FLOW_OK)
    return res;
  else if (!gst_avi_demux_parse_file_header (GST_ELEMENT (avi), buf))
    goto wrong_header;

  avi->offset += 12;

  return GST_FLOW_OK;

  /* ERRORS */
wrong_header:
  {
    GST_DEBUG_OBJECT (avi, "error parsing file header");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

/**
 * gst_avi_demux_parse_avih:
 * @element: caller element (used for errors/debug).
 * @buf: input data to be used for parsing.
 * @avih: pointer to structure (filled in by function) containing
 *        stream information (such as flags, number of streams, etc.).
 *
 * Read 'avih' header. Discards buffer after use.
 *
 * Returns: TRUE on success, FALSE otherwise. Throws an error if
 *          the header is invalid. The caller should error out
 *          (fatal).
 */

static gboolean
gst_avi_demux_parse_avih (GstElement * element,
    GstBuffer * buf, gst_riff_avih ** _avih)
{
  gst_riff_avih *avih;

  if (buf == NULL)
    goto no_buffer;

  if (GST_BUFFER_SIZE (buf) < sizeof (gst_riff_avih))
    goto avih_too_small;

  avih = g_memdup (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  avih->us_frame = GUINT32_FROM_LE (avih->us_frame);
  avih->max_bps = GUINT32_FROM_LE (avih->max_bps);
  avih->pad_gran = GUINT32_FROM_LE (avih->pad_gran);
  avih->flags = GUINT32_FROM_LE (avih->flags);
  avih->tot_frames = GUINT32_FROM_LE (avih->tot_frames);
  avih->init_frames = GUINT32_FROM_LE (avih->init_frames);
  avih->streams = GUINT32_FROM_LE (avih->streams);
  avih->bufsize = GUINT32_FROM_LE (avih->bufsize);
  avih->width = GUINT32_FROM_LE (avih->width);
  avih->height = GUINT32_FROM_LE (avih->height);
  avih->scale = GUINT32_FROM_LE (avih->scale);
  avih->rate = GUINT32_FROM_LE (avih->rate);
  avih->start = GUINT32_FROM_LE (avih->start);
  avih->length = GUINT32_FROM_LE (avih->length);
#endif

  /* debug stuff */
  GST_INFO_OBJECT (element, "avih tag found:");
  GST_INFO_OBJECT (element, " us_frame    %u", avih->us_frame);
  GST_INFO_OBJECT (element, " max_bps     %u", avih->max_bps);
  GST_INFO_OBJECT (element, " pad_gran    %u", avih->pad_gran);
  GST_INFO_OBJECT (element, " flags       0x%08x", avih->flags);
  GST_INFO_OBJECT (element, " tot_frames  %u", avih->tot_frames);
  GST_INFO_OBJECT (element, " init_frames %u", avih->init_frames);
  GST_INFO_OBJECT (element, " streams     %u", avih->streams);
  GST_INFO_OBJECT (element, " bufsize     %u", avih->bufsize);
  GST_INFO_OBJECT (element, " width       %u", avih->width);
  GST_INFO_OBJECT (element, " height      %u", avih->height);
  GST_INFO_OBJECT (element, " scale       %u", avih->scale);
  GST_INFO_OBJECT (element, " rate        %u", avih->rate);
  GST_INFO_OBJECT (element, " start       %u", avih->start);
  GST_INFO_OBJECT (element, " length      %u", avih->length);

  *_avih = avih;
  gst_buffer_unref (buf);

  return TRUE;

  /* ERRORS */
no_buffer:
  {
    GST_ELEMENT_ERROR (element, STREAM, DEMUX, (NULL), ("No buffer"));
    return FALSE;
  }
avih_too_small:
  {
    GST_ELEMENT_ERROR (element, STREAM, DEMUX, (NULL),
        ("Too small avih (%d available, %d needed)",
            GST_BUFFER_SIZE (buf), (int) sizeof (gst_riff_avih)));
    gst_buffer_unref (buf);
    return FALSE;
  }
}

/**
 * gst_avi_demux_parse_superindex:
 * @element: caller element (used for debugging/errors).
 * @buf: input data to use for parsing.
 * @locations: locations in the file (byte-offsets) that contain
 *             the actual indexes (see get_avi_demux_parse_subindex()).
 *             The array ends with GST_BUFFER_OFFSET_NONE.
 *
 * Reads superindex (openDML-2 spec stuff) from the provided data.
 *
 * Returns: TRUE on success, FALSE otherwise. Indexes should be skipped
 *          on error, but they are not fatal.
 */

static gboolean
gst_avi_demux_parse_superindex (GstElement * element,
    GstBuffer * buf, guint64 ** _indexes)
{
  guint8 *data = GST_BUFFER_DATA (buf);
  gint bpe = 16, num, i;
  guint64 *indexes;

  *_indexes = NULL;

  if (buf == NULL)
    goto no_buffer;

  if (!buf || GST_BUFFER_SIZE (buf) < 24)
    goto too_small;

  /* check type of index. The opendml2 specs state that
   * there should be 4 dwords per array entry. Type can be
   * either frame or field (and we don't care). */
  if (GST_READ_UINT16_LE (data) != 4 ||
      (data[2] & 0xfe) != 0x0 || data[3] != 0x0) {
    GST_WARNING_OBJECT (element,
        "Superindex for stream %d has unexpected "
        "size_entry %d (bytes) or flags 0x%02x/0x%02x",
        GST_READ_UINT16_LE (data), data[2], data[3]);
    bpe = GST_READ_UINT16_LE (data) * 4;
  }
  num = GST_READ_UINT32_LE (&data[4]);

  indexes = g_new (guint64, num + 1);
  for (i = 0; i < num; i++) {
    if (GST_BUFFER_SIZE (buf) < 24 + bpe * (i + 1))
      break;
    indexes[i] = GST_READ_UINT64_LE (&data[24 + bpe * i]);
  }
  indexes[i] = GST_BUFFER_OFFSET_NONE;
  *_indexes = indexes;

  gst_buffer_unref (buf);

  return TRUE;

  /* ERRORS */
no_buffer:
  {
    GST_ERROR_OBJECT (element, "No buffer");
    return FALSE;
  }
too_small:
  {
    GST_ERROR_OBJECT (element,
        "Not enough data to parse superindex (%d available, %d needed)",
        GST_BUFFER_SIZE (buf), 24);
    gst_buffer_unref (buf);
    return FALSE;
  }
}

/**
 * gst_avi_demux_parse_subindex:
 * @element: caller element (used for errors/debug).
 * @buf: input data to use for parsing.
 * @stream: stream context.
 * @entries_list: a list (returned by the function) containing all the
 *           indexes parsed in this specific subindex. The first
 *           entry is also a pointer to allocated memory that needs
 *           to be free´ed. May be NULL if no supported indexes were
 *           found.
 *
 * Reads superindex (openDML-2 spec stuff) from the provided data.
 * The buffer will be discarded after use.
 *
 * Returns: TRUE on success, FALSE otherwise. Errors are fatal, we
 *          throw an error, caller should bail out asap.
 */

static gboolean
gst_avi_demux_parse_subindex (GstElement * element,
    GstBuffer * buf, avi_stream_context * stream, GList ** _entries_list)
{
  guint8 *data = GST_BUFFER_DATA (buf);
  gint bpe, num, x;
  guint64 baseoff;
  gst_avi_index_entry *entries, *entry;
  GList *entries_list = NULL;
  GstFormat format = GST_FORMAT_TIME;
  gint64 tmp;

  /* check size */
  if (buf == NULL)
    goto no_buffer;

  if (GST_BUFFER_SIZE (buf) < 24)
    goto too_small;

  /* We don't support index-data yet */
  if (data[3] & 0x80)
    goto not_implemented;

  /* check type of index. The opendml2 specs state that
   * there should be 4 dwords per array entry. Type can be
   * either frame or field (and we don't care). */
  bpe = (data[2] & 0x01) ? 12 : 8;
  if (GST_READ_UINT16_LE (data) != bpe / 4 ||
      (data[2] & 0xfe) != 0x0 || data[3] != 0x1) {
    GST_WARNING_OBJECT (element,
        "Superindex for stream %d has unexpected "
        "size_entry %d (bytes) or flags 0x%02x/0x%02x",
        GST_READ_UINT16_LE (data), data[2], data[3]);
    bpe = GST_READ_UINT16_LE (data) * 4;
  }
  num = GST_READ_UINT32_LE (&data[4]);
  baseoff = GST_READ_UINT64_LE (&data[12]);

  entries = g_new (gst_avi_index_entry, num);
  for (x = 0; x < num; x++) {
    entry = &entries[x];

    if (GST_BUFFER_SIZE (buf) < 24 + bpe * (x + 1))
      break;

    /* fill in */
    entry->offset = baseoff + GST_READ_UINT32_LE (&data[24 + bpe * x]);
    entry->size = GST_READ_UINT32_LE (&data[24 + bpe * x + 4]);
    entry->flags = (entry->size & 0x80000000) ? 0 : GST_RIFF_IF_KEYFRAME;
    entry->size &= ~0x80000000;
    entry->index_nr = x;
    entry->stream_nr = stream->num;

    /* timestamps */
    if (stream->strh->samplesize && stream->strh->type == GST_RIFF_FCC_auds) {
      /* constant rate stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes, &format, &tmp);
      entry->ts = tmp;
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes + entry->size, &format, &tmp);
      entry->dur = tmp;
    } else {
      /* VBR stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames, &format, &tmp);
      entry->ts = tmp;
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames + 1, &format, &tmp);
      entry->dur = tmp;
    }
    entry->dur -= entry->ts;

    /* stream position */
    entry->bytes_before = stream->total_bytes;
    stream->total_bytes += entry->size;
    entry->frames_before = stream->total_frames;
    stream->total_frames++;

    entries_list = g_list_prepend (entries_list, entry);
  }

  GST_LOG_OBJECT (element, "Read %d index entries", x);

  gst_buffer_unref (buf);

  if (x > 0) {
    *_entries_list = g_list_reverse (entries_list);
  } else {
    *_entries_list = NULL;
    g_free (entries);
  }

  return TRUE;

  /* ERRORS */
no_buffer:
  {
    GST_ERROR_OBJECT (element, "No buffer");
    return TRUE;                /* continue */
  }
too_small:
  {
    GST_ERROR_OBJECT (element,
        "Not enough data to parse subindex (%d available, %d needed)",
        GST_BUFFER_SIZE (buf), 24);
    gst_buffer_unref (buf);
    *_entries_list = NULL;
    return TRUE;                /* continue */
  }
not_implemented:
  {
    GST_ELEMENT_ERROR (element, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Subindex-is-data is not implemented"));
    gst_buffer_unref (buf);
    return FALSE;
  }
}

static void
gst_avi_demux_read_subindexes (GstAviDemux * avi,
    GList ** index, GList ** alloc_list)
{
  GList *list;
  guint32 tag;
  GstBuffer *buf;
  gint i, n;

  for (n = 0; n < avi->num_streams; n++) {
    avi_stream_context *stream = &avi->stream[n];

    for (i = 0; stream->indexes[i] != GST_BUFFER_OFFSET_NONE; i++) {
      if (gst_riff_read_chunk (GST_ELEMENT (avi), avi->sinkpad,
              &stream->indexes[i], &tag, &buf) != GST_FLOW_OK)
        continue;
      else if (tag != GST_MAKE_FOURCC ('i', 'x', '0' + stream->num / 10,
              '0' + stream->num % 10)) {
        GST_ERROR_OBJECT (GST_ELEMENT (avi),
            "Not an ix## chunk (%" GST_FOURCC_FORMAT ")",
            GST_FOURCC_ARGS (tag));
        gst_buffer_unref (buf);
        continue;
      }

      if (!gst_avi_demux_parse_subindex (GST_ELEMENT (avi), buf, stream, &list))
        continue;
      if (list) {
        *alloc_list = g_list_append (*alloc_list, list->data);
        *index = g_list_concat (*index, list);
      }
    }

    g_free (stream->indexes);
    stream->indexes = NULL;
  }
}

/**
 * gst_avi_demux_parse_stream:
 * @element: calling element (used for debugging/errors).
 * @buf: input buffer used to parse the stream.
 *
 * Parses all subchunks in a strl chunk (which defines a single
 * stream). Discards the buffer after use. This function will
 * increment the stream counter internally.
 *
 * Returns: whether the stream was identified successfully.
 *          Errors are not fatal. It does indicate the stream
 *          was skipped.
 */

static gboolean
gst_avi_demux_parse_stream (GstElement * element, GstBuffer * buf)
{
  GstAviDemux *avi = GST_AVI_DEMUX (element);
  avi_stream_context *stream = &avi->stream[avi->num_streams];
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstPadTemplate *templ;
  GstBuffer *sub = NULL;
  guint offset = 4;
  guint32 tag = 0;
  gchar *codec_name = NULL, *padname = NULL;
  const gchar *tag_name;
  GstCaps *caps = NULL;
  GstPad *pad;

  GST_DEBUG_OBJECT (element, "Parsing stream");

  /* read strh */
  if (!buf || !gst_riff_parse_chunk (element, buf, &offset, &tag, &sub) ||
      tag != GST_RIFF_TAG_strh) {
    GST_ERROR_OBJECT (element,
        "Failed to find strh chunk (tag: %" GST_FOURCC_FORMAT ")",
        buf ? GST_BUFFER_SIZE (buf) : 0, GST_FOURCC_ARGS (tag));
    goto fail;
  } else if (!gst_riff_parse_strh (element, sub, &stream->strh))
    goto fail;

  /* read strf */
  if (!gst_riff_parse_chunk (element, buf, &offset, &tag, &sub) ||
      tag != GST_RIFF_TAG_strf) {
    GST_ERROR_OBJECT (element,
        "Failed to find strh chunk (size: %d, tag: %"
        GST_FOURCC_FORMAT ")", buf ? GST_BUFFER_SIZE (buf) : 0,
        GST_FOURCC_ARGS (tag));
    goto fail;
  } else {
    gboolean res = FALSE;

    switch (stream->strh->type) {
      case GST_RIFF_FCC_vids:
        res = gst_riff_parse_strf_vids (element, sub,
            &stream->strf.vids, &stream->extradata);
        break;
      case GST_RIFF_FCC_auds:
        res = gst_riff_parse_strf_auds (element, sub,
            &stream->strf.auds, &stream->extradata);
        break;
      case GST_RIFF_FCC_iavs:
        res = gst_riff_parse_strf_iavs (element, sub,
            &stream->strf.iavs, &stream->extradata);
        break;
      default:
        GST_ERROR_OBJECT (element,
            "Don´t know how to handle stream type %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (stream->strh->type));
        break;
    }

    if (!res)
      goto fail;
  }

  /* read strd/strn */
  while (gst_riff_parse_chunk (element, buf, &offset, &tag, &sub)) {
    switch (tag) {
      case GST_RIFF_TAG_strd:
        if (stream->initdata)
          gst_buffer_unref (stream->initdata);
        stream->initdata = sub;
        break;
      case GST_RIFF_TAG_strn:
        g_free (stream->name);
        stream->name = g_new (gchar, GST_BUFFER_SIZE (sub) + 1);
        memcpy (stream->name, GST_BUFFER_DATA (sub), GST_BUFFER_SIZE (sub));
        stream->name[GST_BUFFER_SIZE (sub)] = '\0';
        gst_buffer_unref (sub);
        sub = NULL;
        break;
      default:
        if (tag == GST_MAKE_FOURCC ('i', 'n', 'd', 'x') ||
            tag == GST_MAKE_FOURCC ('i', 'x', '0' + avi->num_streams / 10,
                '0' + avi->num_streams % 10)) {
          g_free (stream->indexes);
          gst_avi_demux_parse_superindex (element, sub, &stream->indexes);
          break;
        }
        GST_WARNING_OBJECT (element,
            "Unknown stream header tag %" GST_FOURCC_FORMAT ", ignoring",
            GST_FOURCC_ARGS (tag));
        /* fall-through */
      case GST_RIFF_TAG_JUNK:
        gst_buffer_unref (sub);
        sub = NULL;
        break;
    }
  }

  /* we now have all info, let´s set up a pad and a caps and be done */
  /* create stream name + pad */
  switch (stream->strh->type) {
    case GST_RIFF_FCC_vids:{
      guint32 fourcc;

      fourcc = (stream->strf.vids->compression) ?
          stream->strf.vids->compression : stream->strh->fcc_handler;
      padname = g_strdup_printf ("video_%02d", avi->num_v_streams);
      templ = gst_element_class_get_pad_template (klass, "video_%02d");
      caps = gst_riff_create_video_caps (fourcc, stream->strh,
          stream->strf.vids, stream->extradata, stream->initdata, &codec_name);
      if (!caps) {
        caps = gst_caps_new_simple ("video/x-avi-unknown", "fourcc",
            GST_TYPE_FOURCC, fourcc, NULL);
      }
      tag_name = GST_TAG_VIDEO_CODEC;
      avi->num_v_streams++;
      break;
    }
    case GST_RIFF_FCC_auds:{
      padname = g_strdup_printf ("audio_%02d", avi->num_a_streams);
      templ = gst_element_class_get_pad_template (klass, "audio_%02d");
      caps = gst_riff_create_audio_caps (stream->strf.auds->format,
          stream->strh, stream->strf.auds, stream->extradata,
          stream->initdata, &codec_name);
      if (!caps) {
        caps = gst_caps_new_simple ("audio/x-avi-unknown", "codec_id",
            G_TYPE_INT, stream->strf.auds->format, NULL);
      }
      tag_name = GST_TAG_AUDIO_CODEC;
      avi->num_a_streams++;
      break;
    }
    case GST_RIFF_FCC_iavs:{
      guint32 fourcc = stream->strh->fcc_handler;

      padname = g_strdup_printf ("video_%02d", avi->num_v_streams);
      templ = gst_element_class_get_pad_template (klass, "video_%02d");
      caps = gst_riff_create_iavs_caps (fourcc, stream->strh,
          stream->strf.iavs, stream->extradata, stream->initdata, &codec_name);
      if (!caps) {
        caps = gst_caps_new_simple ("video/x-avi-unknown", "fourcc",
            GST_TYPE_FOURCC, fourcc, NULL);
      }
      tag_name = GST_TAG_VIDEO_CODEC;
      avi->num_v_streams++;
      break;
    }
    default:
      g_assert_not_reached ();
  }

  /* no caps means no stream */
  if (!caps) {
    GST_ERROR_OBJECT (element, "Did not find caps for stream %s", padname);
    goto fail;
  }

  /* set proper settings and add it */
  if (stream->pad)
    gst_object_unref (stream->pad);
  pad = stream->pad = gst_pad_new_from_template (templ, padname);
  stream->last_flow = GST_FLOW_OK;
  g_free (padname);

  gst_pad_use_fixed_caps (pad);
#if 0
  gst_pad_set_formats_function (pad, gst_avi_demux_get_src_formats);
#endif
  gst_pad_set_event_function (pad, gst_avi_demux_handle_src_event);
  gst_pad_set_query_type_function (pad, gst_avi_demux_get_src_query_types);
  gst_pad_set_query_function (pad, gst_avi_demux_handle_src_query);

  stream->num = avi->num_streams;
  stream->total_bytes = 0;
  stream->total_frames = 0;
  stream->current_frame = 0;
  stream->current_byte = 0;
  stream->current_entry = -1;
  gst_pad_set_element_private (pad, stream);
  avi->num_streams++;
  gst_pad_set_caps (pad, caps);
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (avi), pad);
  GST_LOG_OBJECT (element, "Added pad %s with caps %" GST_PTR_FORMAT,
      GST_PAD_NAME (pad), caps);

  if (codec_name) {
    GstTagList *list = gst_tag_list_new ();

    gst_tag_list_add (list, GST_TAG_MERGE_APPEND, tag_name, codec_name, NULL);
    gst_element_found_tags_for_pad (GST_ELEMENT (avi), pad, list);
    g_free (codec_name);
  }

  return TRUE;

  /* ERRORS */
fail:
  {
    /* unref any mem that may be in use */
    if (buf)
      gst_buffer_unref (buf);
    if (sub)
      gst_buffer_unref (sub);
    g_free (stream->strh);
    g_free (stream->strf.data);
    g_free (stream->name);
    g_free (stream->indexes);
    if (stream->initdata)
      gst_buffer_unref (stream->initdata);
    if (stream->extradata)
      gst_buffer_unref (stream->extradata);
    memset (stream, 0, sizeof (avi_stream_context));
    avi->num_streams++;

    return FALSE;
  }
}

/**
 * gst_avi_demux_parse_odml:
 * @element: calling element (used for debug/error).
 * @buf: input buffer to be used for parsing.
 *
 * Read an openDML-2.0 extension header. Fills in the frame number
 * in the avi demuxer object when reading succeeds.
 */

static void
gst_avi_demux_parse_odml (GstElement * element, GstBuffer * buf)
{
  GstAviDemux *avi = GST_AVI_DEMUX (element);
  guint32 tag = 0;
  guint offset = 4;
  GstBuffer *sub = NULL;

  while (gst_riff_parse_chunk (element, buf, &offset, &tag, &sub)) {
    switch (tag) {
      case GST_RIFF_TAG_dmlh:{
        gst_riff_dmlh dmlh, *_dmlh;

        if (sub && (GST_BUFFER_SIZE (sub) < sizeof (gst_riff_dmlh))) {
          GST_ERROR_OBJECT (element,
              "DMLH entry is too small (%d bytes, %d needed)",
              GST_BUFFER_SIZE (buf), (int) sizeof (gst_riff_dmlh));
          gst_buffer_unref (sub);
          sub = NULL;
          break;
        }
        _dmlh = (gst_riff_dmlh *) GST_BUFFER_DATA (buf);
        dmlh.totalframes = GUINT32_FROM_LE (_dmlh->totalframes);

        GST_INFO_OBJECT (element, "dmlh tag found:");
        GST_INFO_OBJECT (element, " totalframes: %u", dmlh.totalframes);

        avi->avih->tot_frames = dmlh.totalframes;
        if (sub) {
          gst_buffer_unref (sub);
          sub = NULL;
        }
        break;
      }

      default:
        GST_WARNING_OBJECT (element,
            "Unknown tag %" GST_FOURCC_FORMAT " in ODML header",
            GST_FOURCC_ARGS (tag));
        /* fall-through */
      case GST_RIFF_TAG_JUNK:
        if (sub) {
          gst_buffer_unref (sub);
          sub = NULL;
        }
        break;
    }
  }

  if (buf)
    gst_buffer_unref (buf);
}

/**
 * gst_avi_demux_parse_index:
 * @element: calling element (used for debugging/errors).
 * @buf: buffer containing the full index.
 * @entries_list: list (returned by this function) containing the index
 *                entries parsed from the buffer. The first in the list
 *                is also a pointer to the allocated data and should be
 *                free'ed at some point.
 *
 * Read index entries from the provided buffer.
 */

static void
gst_avi_demux_parse_index (GstElement * element,
    GstBuffer * buf, GList ** _entries_list)
{
  GstAviDemux *avi = GST_AVI_DEMUX (element);
  guint64 pos_before = avi->offset;
  gst_avi_index_entry *entries = NULL;
  guint8 *data;
  GList *entries_list = NULL;
  guint i, num, n;

  if (!buf) {
    *_entries_list = NULL;
    return;
  }

  data = GST_BUFFER_DATA (buf);
  num = GST_BUFFER_SIZE (buf) / sizeof (gst_riff_index_entry);
  entries = g_new (gst_avi_index_entry, num);

  for (i = 0, n = 0; i < num; i++) {
    gst_riff_index_entry entry, *_entry;
    avi_stream_context *stream;
    gint stream_nr;
    gst_avi_index_entry *target;
    GstFormat format;
    gint64 ts;

    _entry = &((gst_riff_index_entry *) data)[i];
    entry.id = GUINT32_FROM_LE (_entry->id);
    entry.offset = GUINT32_FROM_LE (_entry->offset);
    entry.flags = GUINT32_FROM_LE (_entry->flags);
    entry.size = GUINT32_FROM_LE (_entry->size);
    target = &entries[n];

    if (entry.id == GST_RIFF_rec || entry.id == 0 ||
        (entry.offset == 0 && n > 0))
      continue;

    stream_nr = CHUNKID_TO_STREAMNR (entry.id);
    if (stream_nr >= avi->num_streams || stream_nr < 0) {
      GST_WARNING_OBJECT (element,
          "Index entry %d has invalid stream nr %d", i, stream_nr);
      continue;
    }
    target->stream_nr = stream_nr;
    stream = &avi->stream[stream_nr];

    target->index_nr = i;
    target->flags = entry.flags;
    target->size = entry.size;
    target->offset = entry.offset + 8;

    /* figure out if the index is 0 based or relative to the MOVI start */
    if (n == 0) {
      if (target->offset < pos_before)
        avi->index_offset = pos_before + 8;
      else
        avi->index_offset = 0;
    }

    target->bytes_before = stream->total_bytes;
    target->frames_before = stream->total_frames;

    format = GST_FORMAT_TIME;
    if (stream->strh->type == GST_RIFF_FCC_auds) {
      /* all audio frames are keyframes */
      target->flags |= GST_RIFF_IF_KEYFRAME;
    }

    if (stream->strh->samplesize && stream->strh->type == GST_RIFF_FCC_auds) {
      /* constant rate stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes, &format, &ts);
      target->ts = ts;
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes + target->size, &format, &ts);
      target->dur = ts;
    } else {
      /* VBR stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames, &format, &ts);
      target->ts = ts;
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames + 1, &format, &ts);
      target->dur = ts;
    }
    target->dur -= target->ts;

    stream->total_bytes += target->size;
    stream->total_frames++;

    GST_DEBUG ("Adding index entry %d (%d) for stream %d of size %u "
        "at offset %" G_GUINT64_FORMAT " and time %" GST_TIME_FORMAT,
        target->index_nr, stream->total_frames - 1,
        target->stream_nr, target->size, target->offset,
        GST_TIME_ARGS (target->ts));
    entries_list = g_list_prepend (entries_list, target);

    n++;
  }

  *_entries_list = g_list_reverse (entries_list);
  gst_buffer_unref (buf);
}

/**
 * gst_avi_demux_stream_index:
 * @avi: avi demuxer object.
 * @index: list of index entries, returned by this function.
 * @alloc_list: list of allocated data, returned by this function.
 *
 * Seeks to index and reads it.
 */

static void
gst_avi_demux_stream_index (GstAviDemux * avi,
    GList ** index, GList ** alloc_list)
{
  guint64 offset = avi->offset;
  GstBuffer *buf;
  guint32 tag;
  gint i;

  *alloc_list = NULL;
  *index = NULL;

  /* get position */
  if (gst_pad_pull_range (avi->sinkpad, offset, 8, &buf) != GST_FLOW_OK)
    return;
  else if (!buf || GST_BUFFER_SIZE (buf) < 8) {
    if (buf)
      gst_buffer_unref (buf);
    return;
  }
  offset += 8 + GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 4);
  gst_buffer_unref (buf);

  /* get size */
  if (gst_riff_read_chunk (GST_ELEMENT (avi),
          avi->sinkpad, &offset, &tag, &buf) != GST_FLOW_OK)
    return;
  else if (tag != GST_RIFF_TAG_idx1) {
    GST_ERROR_OBJECT (avi,
        "No index data after movi chunk, but %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (tag));
    gst_buffer_unref (buf);
    return;
  }

  gst_avi_demux_parse_index (GST_ELEMENT (avi), buf, index);
  if (*index)
    *alloc_list = g_list_append (*alloc_list, (*index)->data);

  /* debug our indexes */
  for (i = 0; i < avi->num_streams; i++) {
    avi_stream_context *stream;

    stream = &avi->stream[i];
    GST_DEBUG_OBJECT (avi, "stream %u: %u frames, %" G_GINT64_FORMAT " bytes",
        i, stream->total_frames, stream->total_bytes);
  }

  return;
}

#if 0

/*
 * Sync to next data chunk.
 */

static gboolean
gst_avi_demux_skip (GstAviDemux * avi, gboolean prevent_eos)
{
  GstRiffRead *riff = GST_RIFF_READ (avi);

  if (prevent_eos) {
    guint64 pos, length;
    guint size;
    guint8 *data;

    pos = gst_bytestream_tell (riff->bs);
    length = gst_bytestream_length (riff->bs);

    if (pos + 8 > length)
      return FALSE;

    if (gst_bytestream_peek_bytes (riff->bs, &data, 8) != 8)
      return FALSE;

    size = GST_READ_UINT32_LE (&data[4]);
    if (size & 1)
      size++;

    /* Note, we're going to skip which might involve seeks. Therefore,
     * we need 1 byte more! */
    if (pos + 8 + size >= length)
      return FALSE;
  }

  return gst_riff_read_skip (riff);
}

static gboolean
gst_avi_demux_sync (GstAviDemux * avi, guint32 * ret_tag, gboolean prevent_eos)
{
  GstRiffRead *riff = GST_RIFF_READ (avi);
  guint32 tag;
  guint64 length = gst_bytestream_length (riff->bs);

  if (prevent_eos && gst_bytestream_tell (riff->bs) + 12 >= length)
    return FALSE;

  /* peek first (for the end of this 'list/movi' section) */
  if (!(tag = gst_riff_peek_tag (riff, &avi->level_up)))
    return FALSE;

  /* if we're at top-level, we didn't read the 'movi'
   * list tag yet. This can also be 'AVIX' in case of
   * openDML-2.0 AVI files. Lastly, it might be idx1,
   * in which case we skip it so we come at EOS. */
  while (1) {
    if (prevent_eos && gst_bytestream_tell (riff->bs) + 12 >= length)
      return FALSE;

    if (!(tag = gst_riff_peek_tag (riff, NULL)))
      return FALSE;

    switch (tag) {
      case GST_RIFF_TAG_LIST:
        if (!(tag = gst_riff_peek_list (riff)))
          return FALSE;

        switch (tag) {
          case GST_RIFF_LIST_AVIX:
            if (!gst_riff_read_list (riff, &tag))
              return FALSE;
            break;

          case GST_RIFF_LIST_movi:
            if (!gst_riff_read_list (riff, &tag))
              return FALSE;
            /* fall-through */

          case GST_RIFF_rec:
            goto done;

          default:
            GST_WARNING ("Unknown list %" GST_FOURCC_FORMAT " before AVI data",
                GST_FOURCC_ARGS (tag));
            /* fall-through */

          case GST_RIFF_TAG_JUNK:
            if (!gst_avi_demux_skip (avi, prevent_eos))
              return FALSE;
            break;
        }
        break;

      default:
        if ((tag & 0xff) >= '0' &&
            (tag & 0xff) <= '9' &&
            ((tag >> 8) & 0xff) >= '0' && ((tag >> 8) & 0xff) <= '9') {
          goto done;
        }
        /* pass-through */

      case GST_RIFF_TAG_idx1:
      case GST_RIFF_TAG_JUNK:
        if (!gst_avi_demux_skip (avi, prevent_eos)) {
          return FALSE;
        }
        break;
    }
  }
done:
  /* And then, we get the data */
  if (prevent_eos && gst_bytestream_tell (riff->bs) + 12 >= length)
    return FALSE;

  if (!(tag = gst_riff_peek_tag (riff, NULL)))
    return FALSE;

  /* Support for rec-list files */
  switch (tag) {
    case GST_RIFF_TAG_LIST:
      if (!(tag = gst_riff_peek_list (riff)))
        return FALSE;
      if (tag == GST_RIFF_rec) {
        /* Simply skip the list */
        if (!gst_riff_read_list (riff, &tag))
          return FALSE;
        if (!(tag = gst_riff_peek_tag (riff, NULL)))
          return FALSE;
      }
      break;

    case GST_RIFF_TAG_JUNK:
      gst_avi_demux_skip (avi, prevent_eos);
      return FALSE;
  }

  if (ret_tag)
    *ret_tag = tag;

  return TRUE;
}
#endif

/**
 * gst_avi_demux_peek_tag:
 * 
 * Returns the tag and size of the next chunk
 */

static GstFlowReturn
gst_avi_demux_peek_tag (GstAviDemux * avi, guint64 offset, guint32 * tag,
    guint * size)
{
  GstFlowReturn res = GST_FLOW_OK;
  GstBuffer *buf = NULL;

  if ((res = gst_pad_pull_range (avi->sinkpad, offset, 8, &buf)) != GST_FLOW_OK)
    goto beach;
  if (GST_BUFFER_SIZE (buf) != 8)
    return GST_FLOW_ERROR;
  *tag = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf));
  *size = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 4);
  gst_buffer_unref (buf);

  GST_LOG_OBJECT (avi,
      "Tag[%" GST_FOURCC_FORMAT "] (size:%d) %lld -- %lld",
      GST_FOURCC_ARGS (*tag), *size, offset, offset + (guint64) * size);
beach:
  return res;
}

/**
 * gst_avi_demux_next_data_buffer:
 * 
 * Returns the offset and size of the next buffer
 * Position is the position of the buffer (after tag and size)
*/

static GstFlowReturn
gst_avi_demux_next_data_buffer (GstAviDemux * avi, guint64 * offset,
    guint32 * tag, guint * size)
{
  guint64 off = *offset;
  guint siz;
  GstFlowReturn res = GST_FLOW_OK;

  while (1) {
    if ((res = gst_avi_demux_peek_tag (avi, off, tag, &siz)) != GST_FLOW_OK)
      break;
    if (*tag == GST_RIFF_TAG_LIST)
      off += 8;
    else if (*tag == GST_RIFF_LIST_movi)
      off += 4;
    else {
      *offset = off + 8;
      *size = siz;
      break;
    }
  }
  return res;
}

/*
 * Scan the file for all chunks to "create" a new index.
 * Return value indicates if we can continue reading the stream. It
 * does not say anything about whether we created an index.
 *
 * pull-range based
 */

static gboolean
gst_avi_demux_stream_scan (GstAviDemux * avi,
    GList ** index, GList ** alloc_list)
{
  GstFlowReturn res;
  gst_avi_index_entry *entry, *entries = NULL;
  avi_stream_context *stream;
  GstFormat format = GST_FORMAT_BYTES;
  guint64 pos = avi->offset;
  guint64 length;
  gint64 tmplength;
  guint32 tag;
  GstPad *peer;
  GList *list = NULL;
  guint index_size = 0;

  /* FIXME:
   * - implement non-seekable source support.
   */

  GST_LOG_OBJECT (avi, "Creating index %s existing index",
      (*index) ? "with" : "without");

  if (!(peer = gst_pad_get_peer (avi->sinkpad)))
    return FALSE;
  if (!(gst_pad_query_duration (peer, &format, &tmplength)))
    return FALSE;
  length = tmplength;
  gst_object_unref (peer);

  if (*index) {
    entry = g_list_last (*index)->data;
    pos = entry->offset + avi->index_offset + entry->size;
    if (entry->size & 1)
      pos++;

    if (pos < length) {
      GST_LOG ("Incomplete index, seeking to last valid entry @ %"
          G_GUINT64_FORMAT " of %" G_GUINT64_FORMAT " (%"
          G_GUINT64_FORMAT "+%u)", pos, length, entry->offset, entry->size);

    } else {
      return TRUE;
    }
  }

  while (1) {
    gint stream_nr;
    guint size;
    gint64 tmpts, tmpdur;

    if ((res =
            gst_avi_demux_next_data_buffer (avi, &pos, &tag,
                &size)) != GST_FLOW_OK)
      break;
    stream_nr = CHUNKID_TO_STREAMNR (tag);
    if (stream_nr < 0 || stream_nr >= avi->num_streams)
      continue;
    stream = &avi->stream[stream_nr];

    /* pre-allocate */
    if (index_size % 1024 == 0) {
      entries = g_new (gst_avi_index_entry, 1024);
      *alloc_list = g_list_prepend (*alloc_list, entries);
    }
    entry = &entries[index_size % 1024];

    entry->index_nr = index_size++;
    entry->stream_nr = stream_nr;
    entry->flags = GST_RIFF_IF_KEYFRAME;
    entry->offset = pos - avi->index_offset;
    entry->size = size;

    /* timestamps */
    if (stream->strh->samplesize && stream->strh->type == GST_RIFF_FCC_auds) {
      format = GST_FORMAT_TIME;
      /* constant rate stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes, &format, &tmpts);
      entry->ts = tmpts;
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes + entry->size, &format, &tmpdur);
      entry->dur = tmpdur;
    } else {
      format = GST_FORMAT_TIME;
      /* VBR stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames, &format, &tmpts);
      entry->ts = tmpts;
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames + 1, &format, &tmpdur);
      entry->dur = tmpdur;
    }
    entry->dur -= entry->ts;

    /* stream position */
    entry->bytes_before = stream->total_bytes;
    stream->total_bytes += entry->size;
    entry->frames_before = stream->total_frames;
    stream->total_frames++;

    list = g_list_prepend (list, entry);
    GST_DEBUG_OBJECT (avi, "Added index entry %d (in stream: %d), offset %"
        G_GUINT64_FORMAT ", time %" GST_TIME_FORMAT " for stream %d",
        index_size - 1, entry->frames_before, entry->offset,
        GST_TIME_ARGS (entry->ts), entry->stream_nr);

    /* update position */
    pos += ((size + 1) & ~1);
    if (pos > length) {
      GST_WARNING_OBJECT (avi,
          "Stopping index lookup since we are further than EOF");
      break;
    }
  }

#if 0
  while (gst_avi_demux_sync (avi, &tag, TRUE)) {
    gint stream_nr = CHUNKID_TO_STREAMNR (tag);
    guint8 *data;
    GstFormat format = GST_FORMAT_TIME;

    if (stream_nr < 0 || stream_nr >= avi->num_streams)
      goto next;
    stream = &avi->stream[stream_nr];

    /* get chunk size */
    if (gst_bytestream_peek_bytes (riff->bs, &data, 8) != 8)
      goto next;


    /* fill in */
    entry->index_nr = index_size++;
    entry->stream_nr = stream_nr;
    entry->flags = GST_RIFF_IF_KEYFRAME;
    entry->offset = gst_bytestream_tell (riff->bs) + 8 - avi->index_offset;
    entry->size = GST_READ_UINT32_LE (&data[4]);

    /* timestamps */
    if (stream->strh->samplesize && stream->strh->type == GST_RIFF_FCC_auds) {
      /* constant rate stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes, &format, &entry->ts);
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes + entry->size, &format, &entry->dur);
    } else {
      /* VBR stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames, &format, &entry->ts);
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames + 1, &format, &entry->dur);
    }
    entry->dur -= entry->ts;

    /* stream position */
    entry->bytes_before = stream->total_bytes;
    stream->total_bytes += entry->size;
    entry->frames_before = stream->total_frames;
    stream->total_frames++;

    list = g_list_prepend (list, entry);
    GST_DEBUG_OBJECT (avi, "Added index entry %d (in stream: %d), offset %"
        G_GUINT64_FORMAT ", time %" GST_TIME_FORMAT " for stream %d",
        index_size - 1, entry->frames_before, entry->offset,
        GST_TIME_ARGS (entry->ts), entry->stream_nr);

  next:
    if (!gst_avi_demux_skip (avi, TRUE))
      break;
  }
  /* seek back */
  if (!(event = gst_riff_read_seek (riff, pos))) {
    g_list_free (list);
    return FALSE;
  }
  gst_event_unref (event);

#endif
  GST_LOG_OBJECT (avi, "index created, %d items", index_size);

  *index = g_list_concat (*index, g_list_reverse (list));

  return TRUE;
}

/*
 * Massage index.
 * We're going to go over each entry in the index and finetune
 * some things we don't like about AVI. For example, a single
 * chunk might be too long. Also, individual streams might be
 * out-of-sync. In the first case, we cut the chunk in several
 * smaller pieces. In the second case, we re-order chunk reading
 * order. The end result should be a smoother playing AVI.
 */

static gint
sort (gst_avi_index_entry * a, gst_avi_index_entry * b)
{
  if (a->ts > b->ts)
    return 1;
  else if (a->ts < b->ts)
    return -1;
  else
    return a->stream_nr - b->stream_nr;
}

static void
gst_avi_demux_massage_index (GstAviDemux * avi,
    GList * list, GList * alloc_list)
{
  gst_avi_index_entry *entry;
  avi_stream_context *stream;
  gint i;
  GList *one;

  GST_LOG ("Starting index massage");

  /* init frames */
  for (i = 0; i < avi->num_streams; i++) {
    GstFormat fmt = GST_FORMAT_TIME;
    gint64 delay = 0;

    stream = &avi->stream[i];
    if (stream->strh->type == GST_RIFF_FCC_vids) {
      if (!gst_avi_demux_src_convert (stream->pad,
              GST_FORMAT_DEFAULT, stream->strh->init_frames, &fmt, &delay)) {
        delay = 0;
      }
    } else {
      if (!gst_avi_demux_src_convert (stream->pad,
              GST_FORMAT_DEFAULT, stream->strh->init_frames, &fmt, &delay)) {
        delay = 0;
      }
    }
    GST_LOG ("Adding init_time=%" GST_TIME_FORMAT " to stream %d",
        GST_TIME_ARGS (delay), i);

    for (one = list; one != NULL; one = one->next) {
      entry = one->data;

      if (entry->stream_nr == i)
        entry->ts += delay;
    }
  }

  GST_LOG ("I'm now going to cut large chunks into smaller pieces");

  /* cut chunks in small (seekable) pieces */
  for (i = 0; i < avi->num_streams; i++) {
    if (avi->stream[i].total_frames != 1)
      continue;

    for (one = list; one != NULL; one = one->next) {
      entry = one->data;

      if (entry->stream_nr != i)
        continue;

#define MAX_DURATION (GST_SECOND / 2)

      /* check for max duration of a single buffer. I suppose that
       * the allocation of index entries could be improved. */
      stream = &avi->stream[entry->stream_nr];
      if (entry->dur > MAX_DURATION && stream->strh->type == GST_RIFF_FCC_auds) {
        guint32 ideal_size = stream->strf.auds->av_bps / 10;
        gst_avi_index_entry *entries;
        gint old_size, num_added;
        GList *one2;

        /* copy index */
        old_size = entry->size;
        num_added = (entry->size - 1) / ideal_size;
        avi->index_size += num_added;
        entries = g_malloc (sizeof (gst_avi_index_entry) * num_added);
        alloc_list = g_list_prepend (alloc_list, entries);
        for (one2 = one->next; one2 != NULL; one2 = one2->next) {
          gst_avi_index_entry *entry2 = one2->data;

          entry2->index_nr += num_added;
          if (entry2->stream_nr == entry->stream_nr)
            entry2->frames_before += num_added;
        }

        /* new sized index chunks */
        for (i = 0; i < num_added + 1; i++) {
          gst_avi_index_entry *entry2;

          if (i == 0) {
            entry2 = entry;
          } else {
            entry2 = &entries[i - 1];
            list = g_list_insert_before (list, one->next, entry2);
            entry = one->data;
            one = one->next;
            memcpy (entry2, entry, sizeof (gst_avi_index_entry));
          }

          if (old_size >= ideal_size) {
            entry2->size = ideal_size;
            old_size -= ideal_size;
          } else {
            entry2->size = old_size;
          }

          entry2->dur = GST_SECOND * entry2->size / stream->strf.auds->av_bps;
          if (i != 0) {
            entry2->index_nr++;
            entry2->ts += entry->dur;
            entry2->offset += entry->size;
            entry2->bytes_before += entry->size;
            entry2->frames_before++;
          }
        }
      }
    }
  }

  GST_LOG ("I'm now going to reorder the index entries for time");

  /* re-order for time */
  list = g_list_sort (list, (GCompareFunc) sort);

  GST_LOG ("Filling in index array");

  avi->index_size = g_list_length (list);
  avi->index_entries = g_new (gst_avi_index_entry, avi->index_size);
  for (i = 0, one = list; one != NULL; one = one->next, i++) {
    entry = one->data;
    memcpy (&avi->index_entries[i], entry, sizeof (gst_avi_index_entry));
    avi->index_entries[i].index_nr = i;
  }

  GST_LOG ("Freeing original index list");

  g_list_foreach (alloc_list, (GFunc) g_free, NULL);
  g_list_free (alloc_list);
  g_list_free (list);

  for (i = 0; i < avi->num_streams; i++) {
    GST_LOG ("Stream %d, %d frames, %" G_GUINT64_FORMAT " bytes", i,
        avi->stream[i].total_frames, avi->stream[i].total_bytes);
  }

  GST_LOG ("Index massaging done");
}

static gboolean
gst_avi_demux_send_event (GstAviDemux * avi, GstEvent * event)
{
  gint i;

  for (i = 0; i < avi->num_streams; i++) {
    avi_stream_context *stream = &avi->stream[i];

    gst_event_ref (event);
    gst_pad_push_event (stream->pad, event);
  }
  gst_event_unref (event);
  return TRUE;
}

/*
 * Read full AVI headers.
 */

static GstFlowReturn
gst_avi_demux_stream_header (GstAviDemux * avi)
{
  GstFlowReturn res;
  GstBuffer *buf, *sub = NULL;
  guint32 tag;
  GList *index = NULL, *alloc = NULL;
  guint offset = 4;

  /* the header consists of a 'hdrl' LIST tag */
  if ((res = gst_riff_read_chunk (GST_ELEMENT (avi), avi->sinkpad,
              &avi->offset, &tag, &buf)) != GST_FLOW_OK)
    return res;
  else if (tag != GST_RIFF_TAG_LIST)
    goto no_list;
  else if (GST_BUFFER_SIZE (buf) < 4)
    goto no_header;

  /* Find the 'hdrl' LIST tag */
  while (GST_READ_UINT32_LE (GST_BUFFER_DATA (buf)) != GST_RIFF_LIST_hdrl) {
    GST_LOG_OBJECT (avi, "buffer contains %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (GST_READ_UINT32_LE (GST_BUFFER_DATA (buf))));

    /* Eat up */
    gst_buffer_unref (buf);
    if ((res = gst_riff_read_chunk (GST_ELEMENT (avi), avi->sinkpad,
                &avi->offset, &tag, &buf)) != GST_FLOW_OK)
      return res;
    else if (tag != GST_RIFF_TAG_LIST)
      goto no_list;
    else if (GST_BUFFER_SIZE (buf) < 4)
      goto no_header;
  }

  /* the hdrl starts with a 'avih' header */
  if (!gst_riff_parse_chunk (GST_ELEMENT (avi), buf, &offset, &tag, &sub) ||
      tag != GST_RIFF_TAG_avih)
    goto no_avih;
  else if (!gst_avi_demux_parse_avih (GST_ELEMENT (avi), sub, &avi->avih))
    goto invalid_avih;

  /* now, read the elements from the header until the end */
  while (gst_riff_parse_chunk (GST_ELEMENT (avi), buf, &offset, &tag, &sub)) {
    switch (tag) {
      case GST_RIFF_TAG_LIST:
        if (!sub || GST_BUFFER_SIZE (sub) < 4) {
          if (sub) {
            gst_buffer_unref (sub);
            sub = NULL;
          }
          break;
        }

        switch (GST_READ_UINT32_LE (GST_BUFFER_DATA (sub))) {
          case GST_RIFF_LIST_strl:
            if (!(gst_avi_demux_parse_stream (GST_ELEMENT (avi), sub)))
              return GST_FLOW_ERROR;
            gst_buffer_unref (sub);
            break;
          case GST_RIFF_LIST_odml:
            gst_avi_demux_parse_odml (GST_ELEMENT (avi), sub);
            break;
          default:
            GST_WARNING_OBJECT (avi,
                "Unknown list %" GST_FOURCC_FORMAT " in AVI header",
                GST_FOURCC_ARGS (GST_READ_UINT32_LE (GST_BUFFER_DATA (sub))));
            /* fall-through */
          case GST_RIFF_TAG_JUNK:
            if (sub) {
              gst_buffer_unref (sub);
              sub = NULL;
            }
            break;
        }
        break;
      default:
        GST_WARNING_OBJECT (avi,
            "Unknown off %d tag %" GST_FOURCC_FORMAT " in AVI header",
            offset, GST_FOURCC_ARGS (tag));
        /* fall-through */
      case GST_RIFF_TAG_JUNK:
        if (sub) {
          gst_buffer_unref (sub);
          sub = NULL;
        }
        break;
    }
  }
  gst_buffer_unref (buf);

  /* check parsed streams */
  if (avi->num_streams == 0)
    goto no_streams;
  else if (avi->num_streams != avi->avih->streams) {
    GST_WARNING_OBJECT (avi,
        "Stream header mentioned %d streams, but %d available",
        avi->avih->streams, avi->num_streams);
  }

  GST_DEBUG_OBJECT (avi, "skipping junk between header and data ...");

  /* Now, find the data (i.e. skip all junk between header and data) */
  do {
    guint size;
    guint32 tag, ltag;

    if ((res = gst_pad_pull_range (avi->sinkpad, avi->offset,
                12, &buf)) != GST_FLOW_OK)
      return res;
    else if (!buf || GST_BUFFER_SIZE (buf) < 12)
      return GST_FLOW_ERROR;

    tag = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf));
    size = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 4);
    ltag = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 8);
    gst_buffer_unref (buf);

    if (tag == GST_RIFF_TAG_LIST) {
      switch (ltag) {
        case GST_RIFF_LIST_movi:
          goto done;
        case GST_RIFF_LIST_INFO:
          if ((res = gst_riff_read_chunk (GST_ELEMENT (avi), avi->sinkpad,
                      &avi->offset, &tag, &buf)) != GST_FLOW_OK)
            return res;
          else {
            GstTagList *t;

            sub = gst_buffer_create_sub (buf, 4, GST_BUFFER_SIZE (buf) - 4);
            gst_riff_parse_info (GST_ELEMENT (avi), sub, &t);
            if (t) {
              gst_element_found_tags (GST_ELEMENT (avi), t);
            }
            if (sub) {
              gst_buffer_unref (sub);
              sub = NULL;
            }
            gst_buffer_unref (buf);
          }
          /* gst_riff_read_chunk() has already advanced avi->offset */
          break;
        default:
          avi->offset += 8 + ((size + 1) & ~1);
          break;
      }
    } else {
      avi->offset += 8 + ((size + 1) & ~1);
    }
  } while (1);
done:

  /* create or read stream index (for seeking) */
  if (avi->stream[0].indexes != NULL) {
    gst_avi_demux_read_subindexes (avi, &index, &alloc);
  }
  if (!index) {
    if (avi->avih->flags & GST_RIFF_AVIH_HASINDEX) {
      gst_avi_demux_stream_index (avi, &index, &alloc);
    }
    /* some indexes are incomplete, continue streaming from there */
    if (!index)
      gst_avi_demux_stream_scan (avi, &index, &alloc);
  }

  if (!index)
    goto no_index;

  gst_avi_demux_massage_index (avi, index, alloc);

  /* send initial discont */
  avi->segment_start = 0;
  avi->segment_stop =
      gst_util_uint64_scale_int ((gint64) avi->stream[0].strh->scale *
      avi->stream[0].strh->length, GST_SECOND, avi->stream[0].strh->rate);

  GST_DEBUG_OBJECT (avi, "segment stop %" G_GINT64_FORMAT, avi->segment_stop);

  avi->seek_event = gst_event_new_new_segment
      (FALSE, avi->segment_rate, GST_FORMAT_TIME,
      avi->segment_start, avi->segment_stop, avi->segment_start);

  /* at this point we know all the streams and we can signal the no more
   * pads signal */
  GST_DEBUG_OBJECT (avi, "signaling no more pads");
  gst_element_no_more_pads (GST_ELEMENT (avi));

  return GST_FLOW_OK;

  /* ERRORS */
no_list:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("Invalid AVI header (no LIST at start): %"
            GST_FOURCC_FORMAT, GST_FOURCC_ARGS (tag)));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
no_header:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("Invalid AVI header (no hdrl at start): %"
            GST_FOURCC_FORMAT, GST_FOURCC_ARGS (tag)));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
no_avih:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("Invalid AVI header (no avih at start): %"
            GST_FOURCC_FORMAT, GST_FOURCC_ARGS (tag)));
    if (sub) {
      gst_buffer_unref (sub);
      sub = NULL;
    }
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
invalid_avih:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("Invalid AVI header (cannot parse avih at start)"));
    if (sub) {
      gst_buffer_unref (sub);
      sub = NULL;
    }
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
no_streams:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL), ("No streams found"));
    return GST_FLOW_ERROR;
  }
no_index:
  {
    g_list_free (index);
    g_list_foreach (alloc, (GFunc) g_free, NULL);
    g_list_free (alloc);

    GST_ELEMENT_ERROR (avi, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Could not get/create index"));
    return GST_FLOW_ERROR;
  }
}

/*
 * Handle seek.
 */

static gboolean
gst_avi_demux_handle_seek (GstAviDemux * avi, gboolean update)
{
  GstClockTime start_time;
  gboolean flush, keyframe;
  guint stream;
  gst_avi_index_entry *entry;

  /* FIXME: if we seek in an openDML file, we will have multiple
   * primary levels. Seeking in between those will cause havoc. */

  flush = avi->segment_flags & GST_SEEK_FLAG_FLUSH;
  keyframe = avi->segment_flags & GST_SEEK_FLAG_KEY_UNIT;

  if (flush) {
    for (stream = avi->num_streams; stream--;)
      gst_pad_push_event (avi->stream[stream].pad,
          gst_event_new_flush_start ());
    gst_pad_push_event (avi->sinkpad, gst_event_new_flush_start ());
  } else
    gst_pad_pause_task (avi->sinkpad);

  GST_PAD_STREAM_LOCK (avi->sinkpad);

  /* fill current_entry according to flags and update */
  if (update) {
    entry = gst_avi_demux_index_entry_for_time (avi, 0, avi->segment_start,
        (guint32) GST_RIFF_IF_KEYFRAME);
    if (entry) {
      GST_DEBUG_OBJECT (avi,
          "Got keyframe entry %d [stream:%d / ts:%" GST_TIME_FORMAT
          " / duration:%" GST_TIME_FORMAT "]", entry->index_nr,
          entry->stream_nr, GST_TIME_ARGS (entry->ts),
          GST_TIME_ARGS (entry->dur));
      avi->current_entry = entry->index_nr;
    } else {
      GST_WARNING_OBJECT (avi,
          "Couldn't find AviIndexEntry for time:%" GST_TIME_FORMAT,
          GST_TIME_ARGS (avi->segment_start));
    }
  }

  GST_DEBUG ("seek: %" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT
      " keyframe seeking:%d update:%d", GST_TIME_ARGS (avi->segment_start),
      GST_TIME_ARGS (avi->segment_stop), keyframe, update);

  if (keyframe)
    start_time = avi->index_entries[avi->current_entry].ts;
  else
    start_time = avi->segment_start;

  avi->seek_event = gst_event_new_new_segment
      (!update, avi->segment_rate, GST_FORMAT_TIME,
      start_time, avi->segment_stop, start_time);

  if (flush) {
    for (stream = avi->num_streams; stream--;)
      gst_pad_push_event (avi->stream[stream].pad, gst_event_new_flush_stop ());
    gst_pad_push_event (avi->sinkpad, gst_event_new_flush_stop ());
  }

  if (avi->segment_flags & GST_SEEK_FLAG_SEGMENT) {
    gst_element_post_message (GST_ELEMENT (avi),
        gst_message_new_segment_start (GST_OBJECT (avi), GST_FORMAT_TIME,
            start_time));
  }

  gst_pad_start_task (avi->sinkpad, (GstTaskFunction) gst_avi_demux_loop,
      avi->sinkpad);

  GST_PAD_STREAM_UNLOCK (avi->sinkpad);

  return TRUE;

}

/*
 * Invert DIB buffers... Takes existing buffer and
 * returns either the buffer or a new one (with old
 * one dereferenced).
 */

static inline void
swap_line (guint8 * d1, guint8 * d2, guint8 * tmp, gint bytes)
{
  memcpy (tmp, d1, bytes);
  memcpy (d1, d2, bytes);
  memcpy (d2, tmp, bytes);
}

static GstBuffer *
gst_avi_demux_invert (avi_stream_context * stream, GstBuffer * buf)
{
  gint y, h = stream->strf.vids->height, w = stream->strf.vids->width;
  guint8 *tmp = NULL;

  buf = gst_buffer_make_writable (buf);
  if (GST_BUFFER_SIZE (buf) < (w * h)) {
    GST_WARNING ("Buffer is smaller than reported Width x Height");
    return buf;
  }

  tmp = g_malloc (w);

  for (y = 0; y < h / 2; y++) {
    swap_line (GST_BUFFER_DATA (buf) + w * y,
        GST_BUFFER_DATA (buf) + w * (h - 1 - y), tmp, w);
  }

  g_free (tmp);

  return buf;
}

static gboolean
gst_avi_demux_all_source_pads_unlinked (GstAviDemux * avi)
{
  gint i;

  for (i = 0; i < avi->num_streams; ++i) {
    if (gst_pad_is_linked (avi->stream[i].pad))
      return FALSE;
    /* ignore unlinked state if we haven't tried to push on this pad yet */
    if (avi->stream[i].last_flow == GST_FLOW_OK)
      return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_avi_demux_process_next_entry (GstAviDemux * avi)
{
  GstFlowReturn res;
  gboolean processed = FALSE;

  do {

    if (avi->current_entry >= avi->index_size) {
      GST_LOG_OBJECT (avi, "Handled last index entry, setting EOS (%d > %d)",
          avi->current_entry, avi->index_size);
      goto eos;
    } else {
      GstBuffer *buf;
      gst_avi_index_entry *entry = &avi->index_entries[avi->current_entry++];
      avi_stream_context *stream;

      if (entry->stream_nr >= avi->num_streams) {
        GST_DEBUG_OBJECT (avi,
            "Entry has non-existing stream nr %d", entry->stream_nr);
        continue;
      }

      if ((entry->flags & GST_RIFF_IF_KEYFRAME)
          && GST_CLOCK_TIME_IS_VALID (entry->ts)
          && GST_CLOCK_TIME_IS_VALID (avi->segment_stop)
          && (entry->ts > avi->segment_stop)) {
        GST_LOG_OBJECT (avi, "Found keyframe after segment,"
            " setting EOS (%" GST_TIME_FORMAT " > %" GST_TIME_FORMAT ")",
            GST_TIME_ARGS (entry->ts), GST_TIME_ARGS (avi->segment_stop));
        goto eos;
      }

      stream = &avi->stream[entry->stream_nr];

      if (entry->size == 0 || !stream->pad) {
        GST_DEBUG_OBJECT (avi, "Skipping entry %d (%d, %p)",
            avi->current_entry - 1, entry->size, stream->pad);
        goto next;
      }

      if ((res = gst_pad_pull_range (avi->sinkpad, entry->offset +
                  avi->index_offset, entry->size, &buf)) != GST_FLOW_OK)
        return res;
      else {
        if (stream->strh->fcc_handler == GST_MAKE_FOURCC ('D', 'I', 'B', ' ')) {
          buf = gst_avi_demux_invert (stream, buf);
        }
        if (!(entry->flags & GST_RIFF_IF_KEYFRAME))
          GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
        GST_BUFFER_TIMESTAMP (buf) = entry->ts;
        GST_BUFFER_DURATION (buf) = entry->dur;
        gst_buffer_set_caps (buf, GST_PAD_CAPS (stream->pad));
        GST_DEBUG_OBJECT (avi, "Processing buffer of size %d and time %"
            GST_TIME_FORMAT " on pad %s",
            GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
            GST_PAD_NAME (stream->pad));
        res = gst_pad_push (stream->pad, buf);
        stream->last_flow = res;
        if (res != GST_FLOW_OK && res != GST_FLOW_NOT_LINKED) {
          GST_DEBUG_OBJECT (avi, "Flow on pad %s: %s",
              GST_PAD_NAME (stream->pad), gst_flow_get_name (res));
          return res;
        }
        processed = TRUE;
      }
    next:
      stream->current_frame = entry->frames_before + 1;
      stream->current_byte = entry->bytes_before + entry->size;
    }
  } while (!processed);

  return GST_FLOW_OK;

eos:
  /* handle end-of-stream/segment */
  if (avi->segment_flags & GST_SEEK_FLAG_SEGMENT)
    gst_element_post_message
        (GST_ELEMENT (avi),
        gst_message_new_segment_done (GST_OBJECT (avi), GST_FORMAT_TIME,
            avi->segment_stop));
  else
    gst_avi_demux_send_event (avi, gst_event_new_eos ());
  return GST_FLOW_WRONG_STATE;
}

/*
 * Read data.
 */

static GstFlowReturn
gst_avi_demux_stream_data (GstAviDemux * avi)
{
  /* if we have a avi->index_entries[], we don't want to read
   * the stream linearly, but seek to the next ts/index_entry. */
  //if (avi->index_entries != NULL) {
  return gst_avi_demux_process_next_entry (avi);
  //}
#if 0
  if (!gst_avi_demux_sync (avi, &tag, FALSE))
    return FALSE;
  stream_nr = CHUNKID_TO_STREAMNR (tag);

  if (stream_nr < 0 || stream_nr >= avi->num_streams) {
    /* recoverable */
    GST_WARNING ("Invalid stream ID %d (%" GST_FOURCC_FORMAT ")",
        stream_nr, GST_FOURCC_ARGS (tag));
    if (!gst_riff_read_skip (riff))
      return FALSE;
  } else {
    avi_stream_context *stream;
    GstClockTime next_ts;
    GstFormat format;
    GstBuffer *buf;

    /* get buffer */
    if (!gst_riff_read_data (riff, &tag, &buf))
      return FALSE;

    /* get time of this buffer */
    stream = &avi->stream[stream_nr];
    format = GST_FORMAT_TIME;
    gst_pad_query (stream->pad, GST_QUERY_POSITION, &format, &next_ts);

    /* set delay (if any) */
    if (stream->strh->init_frames == stream->current_frame &&
        stream->delay == 0)
      stream->delay = next_ts;

    stream->current_frame++;
    stream->current_byte += GST_BUFFER_SIZE (buf);

    /* should we skip this data? */
    if (stream->skip) {
      stream->skip--;
      gst_buffer_unref (buf);
    } else {
      if (!stream->pad || !GST_PAD_IS_USABLE (stream->pad)) {
        gst_buffer_unref (buf);
      } else {
        GstClockTime dur_ts;

        if (stream->strh->fcc_handler == GST_MAKE_FOURCC ('D', 'I', 'B', ' ')) {
          buf = gst_avi_demux_invert (stream, buf);
        }

        GST_BUFFER_TIMESTAMP (buf) = next_ts;
        gst_pad_query (stream->pad, GST_QUERY_POSITION, &format, &dur_ts);
        GST_BUFFER_DURATION (buf) = dur_ts - next_ts;
        GST_DEBUG_OBJECT (avi,
            "Pushing buffer with time=%" GST_TIME_FORMAT " over pad %s",
            GST_TIME_ARGS (next_ts), GST_PAD_NAME (stream->pad));
        gst_pad_push (stream->pad, GST_DATA (buf));
      }
    }
  }

  return TRUE;
#endif
}

static void
gst_avi_demux_loop (GstPad * pad)
{
  GstFlowReturn res;
  GstAviDemux *avi = GST_AVI_DEMUX (GST_PAD_PARENT (pad));

  switch (avi->state) {
    case GST_AVI_DEMUX_START:
      if ((res = gst_avi_demux_stream_init (avi)) != GST_FLOW_OK)
        goto pause;
      avi->state = GST_AVI_DEMUX_HEADER;
      /* fall-through */
    case GST_AVI_DEMUX_HEADER:
      if ((res = gst_avi_demux_stream_header (avi)) != GST_FLOW_OK)
        goto pause;
      avi->state = GST_AVI_DEMUX_MOVI;
      break;
    case GST_AVI_DEMUX_MOVI:
      if (avi->seek_event) {
        gst_avi_demux_send_event (avi, avi->seek_event);
        avi->seek_event = NULL;
      }
      if ((res = gst_avi_demux_stream_data (avi)) != GST_FLOW_OK) {
        GST_DEBUG_OBJECT (avi, "stream_data flow: %s", gst_flow_get_name (res));
        goto pause;
      }
      break;
    default:
      g_assert_not_reached ();
  }

  if (gst_avi_demux_all_source_pads_unlinked (avi)) {
    GST_DEBUG_OBJECT (avi, "all source pads unlinked, pausing");
    goto pause;
  }

  return;

pause:
  GST_LOG_OBJECT (avi, "pausing task");
  gst_pad_pause_task (avi->sinkpad);
  if (GST_FLOW_IS_FATAL (res)) {
    guint stream = avi->num_streams;

    /* for fatal errors we post an error message */
    GST_ELEMENT_ERROR (avi, STREAM, FAILED,
        (_("Internal data stream error.")),
        ("streaming stopped, reason %s", gst_flow_get_name (res)));
    while (stream--) {
      if (avi->stream[stream].pad)
        gst_pad_push_event (avi->stream[stream].pad, gst_event_new_eos ());
    }
  }
}

static gboolean
gst_avi_demux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);

  return FALSE;
}

static gboolean
gst_avi_demux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  if (active) {
    /* if we have a scheduler we can start the task */
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_avi_demux_loop, sinkpad);
  } else {
    gst_pad_stop_task (sinkpad);
  }

  return TRUE;
}

static GstStateChangeReturn
gst_avi_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstAviDemux *avi = GST_AVI_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }


  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_avi_demux_reset (avi);
      break;
    default:
      break;
  }

done:
  return ret;
}
