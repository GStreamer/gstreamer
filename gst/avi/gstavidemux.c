/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@temple-baptist.com>
 * Copyright (C) <2006> Nokia Corporation (contact <stefan.kost@nokia.com>)
 * Copyright (C) <2009-2010> STEricsson <benjamin.gaignard@stericsson.com>
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

/**
 * SECTION:element-avidemux
 *
 * Demuxes an .avi file into raw or compressed audio and/or video streams.
 *
 * This element supports both push and pull-based scheduling, depending on the
 * capabilities of the upstream elements.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=test.avi ! avidemux name=demux  demux.audio_00 ! decodebin ! audioconvert ! audioresample ! autoaudiosink   demux.video_00 ! queue ! decodebin ! ffmpegcolorspace ! videoscale ! autovideosink
 * ]| Play (parse and decode) an .avi file and try to output it to
 * an automatically detected soundcard and videosink. If the AVI file contains
 * compressed audio or video data, this will only work if you have the
 * right decoder elements/plugins installed.
 * </refsect2>
 *
 * Last reviewed on 2006-12-29 (0.10.6)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <string.h>
#include <stdio.h>

#include "gst/riff/riff-media.h"
#include "gstavidemux.h"
#include "avi-ids.h"
#include <gst/gst-i18n-plugin.h>
#include <gst/base/gstadapter.h>


#define DIV_ROUND_UP(s,v) (((s) + ((v)-1)) / (v))

#define GST_AVI_KEYFRAME 1
#define ENTRY_IS_KEYFRAME(e) ((e)->flags == GST_AVI_KEYFRAME)
#define ENTRY_SET_KEYFRAME(e) ((e)->flags = GST_AVI_KEYFRAME)
#define ENTRY_UNSET_KEYFRAME(e) ((e)->flags = 0)


GST_DEBUG_CATEGORY_STATIC (avidemux_debug);
#define GST_CAT_DEFAULT avidemux_debug

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-msvideo")
    );

static void gst_avi_demux_base_init (GstAviDemuxClass * klass);
static void gst_avi_demux_class_init (GstAviDemuxClass * klass);
static void gst_avi_demux_init (GstAviDemux * avi);
static void gst_avi_demux_finalize (GObject * object);

static void gst_avi_demux_reset (GstAviDemux * avi);

#if 0
static const GstEventMask *gst_avi_demux_get_event_mask (GstPad * pad);
#endif
static gboolean gst_avi_demux_handle_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_avi_demux_handle_sink_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_avi_demux_push_event (GstAviDemux * avi, GstEvent * event);

#if 0
static const GstFormat *gst_avi_demux_get_src_formats (GstPad * pad);
#endif
static const GstQueryType *gst_avi_demux_get_src_query_types (GstPad * pad);
static gboolean gst_avi_demux_handle_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_avi_demux_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

static gboolean gst_avi_demux_do_seek (GstAviDemux * avi, GstSegment * segment);
static gboolean gst_avi_demux_handle_seek (GstAviDemux * avi, GstPad * pad,
    GstEvent * event);
static gboolean gst_avi_demux_handle_seek_push (GstAviDemux * avi, GstPad * pad,
    GstEvent * event);
static void gst_avi_demux_loop (GstPad * pad);
static gboolean gst_avi_demux_sink_activate (GstPad * sinkpad);
static gboolean gst_avi_demux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_avi_demux_activate_push (GstPad * pad, gboolean active);
static GstFlowReturn gst_avi_demux_chain (GstPad * pad, GstBuffer * buf);

static void gst_avi_demux_set_index (GstElement * element, GstIndex * index);
static GstIndex *gst_avi_demux_get_index (GstElement * element);
static GstStateChangeReturn gst_avi_demux_change_state (GstElement * element,
    GstStateChange transition);
static void gst_avi_demux_calculate_durations_from_index (GstAviDemux * avi);
static void gst_avi_demux_get_buffer_info (GstAviDemux * avi,
    GstAviStream * stream, guint entry_n, GstClockTime * timestamp,
    GstClockTime * ts_end, guint64 * offset, guint64 * offset_end);

static void gst_avi_demux_parse_idit (GstAviDemux * avi, GstBuffer * buf);

static GstElementClass *parent_class = NULL;

/* GObject methods */

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
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *videosrctempl, *audiosrctempl, *subsrctempl;
  GstCaps *audcaps, *vidcaps, *subcaps;

  audcaps = gst_riff_create_audio_template_caps ();
  gst_caps_append (audcaps, gst_caps_new_simple ("audio/x-avi-unknown", NULL));
  audiosrctempl = gst_pad_template_new ("audio_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, audcaps);

  vidcaps = gst_riff_create_video_template_caps ();
  gst_caps_append (vidcaps, gst_riff_create_iavs_template_caps ());
  gst_caps_append (vidcaps, gst_caps_new_simple ("video/x-avi-unknown", NULL));
  videosrctempl = gst_pad_template_new ("video_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, vidcaps);

  subcaps = gst_caps_new_simple ("application/x-subtitle-avi", NULL);
  subsrctempl = gst_pad_template_new ("subtitle_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, subcaps);
  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class, subsrctempl);
  gst_element_class_add_static_pad_template (element_class, &sink_templ);
  gst_object_unref (audiosrctempl);
  gst_object_unref (videosrctempl);
  gst_object_unref (subsrctempl);
  gst_element_class_set_details_simple (element_class, "Avi demuxer",
      "Codec/Demuxer",
      "Demultiplex an avi file into audio and video",
      "Erik Walthinsen <omega@cse.ogi.edu>, "
      "Wim Taymans <wim.taymans@chello.be>, "
      "Thijs Vermeir <thijsvermeir@gmail.com>");
}

static void
gst_avi_demux_class_init (GstAviDemuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = (GObjectClass *) klass;

  GST_DEBUG_CATEGORY_INIT (avidemux_debug, "avidemux",
      0, "Demuxer for AVI streams");

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_avi_demux_finalize;
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_avi_demux_change_state);

  gstelement_class->set_index = GST_DEBUG_FUNCPTR (gst_avi_demux_set_index);
  gstelement_class->get_index = GST_DEBUG_FUNCPTR (gst_avi_demux_get_index);
}

static void
gst_avi_demux_init (GstAviDemux * avi)
{
  avi->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_activate_function (avi->sinkpad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_sink_activate));
  gst_pad_set_activatepull_function (avi->sinkpad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_sink_activate_pull));
  gst_pad_set_activatepush_function (avi->sinkpad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_activate_push));
  gst_pad_set_chain_function (avi->sinkpad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_chain));
  gst_pad_set_event_function (avi->sinkpad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_handle_sink_event));
  gst_element_add_pad (GST_ELEMENT_CAST (avi), avi->sinkpad);

  avi->adapter = gst_adapter_new ();

  gst_avi_demux_reset (avi);
}

static void
gst_avi_demux_finalize (GObject * object)
{
  GstAviDemux *avi = GST_AVI_DEMUX (object);

  GST_DEBUG ("AVI: finalize");

  g_object_unref (avi->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_avi_demux_reset_stream (GstAviDemux * avi, GstAviStream * stream)
{
  g_free (stream->strh);
  g_free (stream->strf.data);
  g_free (stream->name);
  g_free (stream->index);
  g_free (stream->indexes);
  if (stream->initdata)
    gst_buffer_unref (stream->initdata);
  if (stream->extradata)
    gst_buffer_unref (stream->extradata);
  if (stream->pad) {
    if (stream->exposed) {
      gst_pad_set_active (stream->pad, FALSE);
      gst_element_remove_pad (GST_ELEMENT_CAST (avi), stream->pad);
    } else
      gst_object_unref (stream->pad);
  }
  if (stream->taglist) {
    gst_tag_list_free (stream->taglist);
    stream->taglist = NULL;
  }
  memset (stream, 0, sizeof (GstAviStream));
}

static void
gst_avi_demux_reset (GstAviDemux * avi)
{
  gint i;

  GST_DEBUG ("AVI: reset");

  for (i = 0; i < avi->num_streams; i++)
    gst_avi_demux_reset_stream (avi, &avi->stream[i]);

  avi->header_state = GST_AVI_DEMUX_HEADER_TAG_LIST;
  avi->num_streams = 0;
  avi->num_v_streams = 0;
  avi->num_a_streams = 0;
  avi->num_t_streams = 0;
  avi->main_stream = -1;

  avi->state = GST_AVI_DEMUX_START;
  avi->offset = 0;
  avi->building_index = FALSE;

  avi->index_offset = 0;
  g_free (avi->avih);
  avi->avih = NULL;

  if (avi->element_index)
    gst_object_unref (avi->element_index);
  avi->element_index = NULL;

  if (avi->close_seg_event) {
    gst_event_unref (avi->close_seg_event);
    avi->close_seg_event = NULL;
  }
  if (avi->seg_event) {
    gst_event_unref (avi->seg_event);
    avi->seg_event = NULL;
  }
  if (avi->seek_event) {
    gst_event_unref (avi->seek_event);
    avi->seek_event = NULL;
  }

  if (avi->globaltags)
    gst_tag_list_free (avi->globaltags);
  avi->globaltags = NULL;

  avi->got_tags = TRUE;         /* we always want to push global tags */
  avi->have_eos = FALSE;
  avi->seekable = TRUE;

  gst_adapter_clear (avi->adapter);

  gst_segment_init (&avi->segment, GST_FORMAT_TIME);
}


/* GstElement methods */

#if 0
static const GstFormat *
gst_avi_demux_get_src_formats (GstPad * pad)
{
  GstAviStream *stream = gst_pad_get_element_private (pad);

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

/* assumes stream->strf.auds->av_bps != 0 */
static inline GstClockTime
avi_stream_convert_bytes_to_time_unchecked (GstAviStream * stream,
    guint64 bytes)
{
  return gst_util_uint64_scale_int (bytes, GST_SECOND,
      stream->strf.auds->av_bps);
}

static inline guint64
avi_stream_convert_time_to_bytes_unchecked (GstAviStream * stream,
    GstClockTime time)
{
  return gst_util_uint64_scale_int (time, stream->strf.auds->av_bps,
      GST_SECOND);
}

/* assumes stream->strh->rate != 0 */
static inline GstClockTime
avi_stream_convert_frames_to_time_unchecked (GstAviStream * stream,
    guint64 frames)
{
  return gst_util_uint64_scale (frames, stream->strh->scale * GST_SECOND,
      stream->strh->rate);
}

static inline guint64
avi_stream_convert_time_to_frames_unchecked (GstAviStream * stream,
    GstClockTime time)
{
  return gst_util_uint64_scale (time, stream->strh->rate,
      stream->strh->scale * GST_SECOND);
}

static gboolean
gst_avi_demux_src_convert (GstPad * pad,
    GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  GstAviStream *stream = gst_pad_get_element_private (pad);
  gboolean res = TRUE;

  GST_LOG_OBJECT (pad,
      "Received  src_format:%s, src_value:%" G_GUINT64_FORMAT
      ", dest_format:%s", gst_format_get_name (src_format), src_value,
      gst_format_get_name (*dest_format));

  if (G_UNLIKELY (src_format == *dest_format)) {
    *dest_value = src_value;
    goto done;
  }
  if (G_UNLIKELY (!stream->strh || !stream->strf.data)) {
    res = FALSE;
    goto done;
  }
  if (G_UNLIKELY (stream->strh->type == GST_RIFF_FCC_vids &&
          (src_format == GST_FORMAT_BYTES
              || *dest_format == GST_FORMAT_BYTES))) {
    res = FALSE;
    goto done;
  }

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = gst_util_uint64_scale_int (src_value,
              stream->strf.auds->av_bps, GST_SECOND);
          break;
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_util_uint64_scale_round (src_value, stream->strh->rate,
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
            *dest_value = avi_stream_convert_bytes_to_time_unchecked (stream,
                src_value);
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
              avi_stream_convert_frames_to_time_unchecked (stream, src_value);
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
  GST_LOG_OBJECT (pad,
      "Returning res:%d dest_format:%s dest_value:%" G_GUINT64_FORMAT, res,
      gst_format_get_name (*dest_format), *dest_value);
  return res;
}

static const GstQueryType *
gst_avi_demux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType src_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_SEEKING,
    GST_QUERY_CONVERT,
    0
  };

  return src_types;
}

static gboolean
gst_avi_demux_handle_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstAviDemux *avi = GST_AVI_DEMUX (gst_pad_get_parent (pad));

  GstAviStream *stream = gst_pad_get_element_private (pad);

  if (!stream->strh || !stream->strf.data)
    return gst_pad_query_default (pad, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:{
      gint64 pos = 0;

      GST_DEBUG ("pos query for stream %u: frames %u, bytes %u",
          stream->num, stream->current_entry, stream->current_total);

      /* FIXME, this looks clumsy */
      if (stream->strh->type == GST_RIFF_FCC_auds) {
        if (stream->is_vbr) {
          /* VBR */
          pos = gst_util_uint64_scale ((gint64) stream->current_entry *
              stream->strh->scale, GST_SECOND, (guint64) stream->strh->rate);
          GST_DEBUG_OBJECT (avi, "VBR convert frame %u, time %"
              GST_TIME_FORMAT, stream->current_entry, GST_TIME_ARGS (pos));
        } else if (stream->strf.auds->av_bps != 0) {
          /* CBR */
          pos = gst_util_uint64_scale (stream->current_total, GST_SECOND,
              (guint64) stream->strf.auds->av_bps);
          GST_DEBUG_OBJECT (avi,
              "CBR convert bytes %u, time %" GST_TIME_FORMAT,
              stream->current_total, GST_TIME_ARGS (pos));
        } else if (stream->idx_n != 0 && stream->total_bytes != 0) {
          /* calculate timestamps based on percentage of length */
          guint64 xlen = avi->avih->us_frame *
              avi->avih->tot_frames * GST_USECOND;

          if (stream->is_vbr) {
            pos = gst_util_uint64_scale (xlen, stream->current_entry,
                stream->idx_n);
            GST_DEBUG_OBJECT (avi, "VBR perc convert frame %u, time %"
                GST_TIME_FORMAT, stream->current_entry, GST_TIME_ARGS (pos));
          } else {
            pos = gst_util_uint64_scale (xlen, stream->current_total,
                stream->total_bytes);
            GST_DEBUG_OBJECT (avi,
                "CBR perc convert bytes %u, time %" GST_TIME_FORMAT,
                stream->current_total, GST_TIME_ARGS (pos));
          }
        } else {
          /* we don't know */
          res = FALSE;
        }
      } else {
        if (stream->strh->rate != 0) {
          pos = gst_util_uint64_scale ((guint64) stream->current_entry *
              stream->strh->scale, GST_SECOND, (guint64) stream->strh->rate);
        } else {
          pos = stream->current_entry * avi->avih->us_frame * GST_USECOND;
        }
      }
      if (res) {
        GST_DEBUG ("pos query : %" GST_TIME_FORMAT, GST_TIME_ARGS (pos));
        gst_query_set_position (query, GST_FORMAT_TIME, pos);
      } else
        GST_WARNING ("pos query failed");
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat fmt;
      GstClockTime duration;

      /* only act on audio or video streams */
      if (stream->strh->type != GST_RIFF_FCC_auds &&
          stream->strh->type != GST_RIFF_FCC_vids) {
        res = FALSE;
        break;
      }

      /* take stream duration, fall back to avih duration */
      if ((duration = stream->duration) == -1)
        duration = avi->duration;

      gst_query_parse_duration (query, &fmt, NULL);

      switch (fmt) {
        case GST_FORMAT_TIME:
          gst_query_set_duration (query, fmt, duration);
          break;
        case GST_FORMAT_DEFAULT:
        {
          gint64 dur;
          GST_DEBUG_OBJECT (query, "total frames is %" G_GUINT32_FORMAT,
              stream->idx_n);

          if (stream->idx_n >= 0)
            gst_query_set_duration (query, fmt, stream->idx_n);
          else if (gst_pad_query_convert (pad, GST_FORMAT_TIME,
                  duration, &fmt, &dur))
            gst_query_set_duration (query, fmt, dur);
          break;
        }
        default:
          res = FALSE;
          break;
      }
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat fmt;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt == GST_FORMAT_TIME) {
        gboolean seekable = TRUE;

        if (avi->streaming) {
          seekable = avi->seekable;
        }

        gst_query_set_seeking (query, GST_FORMAT_TIME, seekable,
            0, stream->duration);
        res = TRUE;
      }
      break;
    }
    case GST_QUERY_CONVERT:{
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if ((res = gst_avi_demux_src_convert (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      else
        res = gst_pad_query_default (pad, query);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (avi);
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

static guint64
gst_avi_demux_seek_streams (GstAviDemux * avi, guint64 offset, gboolean before)
{
  GstAviStream *stream;
  GstIndexEntry *entry;
  gint i;
  gint64 val, min = offset;

  for (i = 0; i < avi->num_streams; i++) {
    stream = &avi->stream[i];

    entry = gst_index_get_assoc_entry (avi->element_index, stream->index_id,
        before ? GST_INDEX_LOOKUP_BEFORE : GST_INDEX_LOOKUP_AFTER,
        GST_ASSOCIATION_FLAG_NONE, GST_FORMAT_BYTES, offset);

    if (before) {
      if (entry) {
        gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, &val);
        GST_DEBUG_OBJECT (avi, "stream %d, previous entry at %"
            G_GUINT64_FORMAT, i, val);
        if (val < min)
          min = val;
      }
      continue;
    }

    if (!entry) {
      GST_DEBUG_OBJECT (avi, "no position for stream %d, assuming at start", i);
      stream->current_entry = 0;
      stream->current_total = 0;
      continue;
    }

    gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, &val);
    GST_DEBUG_OBJECT (avi, "stream %d, next entry at %" G_GUINT64_FORMAT,
        i, val);

    gst_index_entry_assoc_map (entry, GST_FORMAT_TIME, &val);
    stream->current_total = val;
    gst_index_entry_assoc_map (entry, GST_FORMAT_DEFAULT, &val);
    stream->current_entry = val;
  }

  return min;
}

static guint
gst_avi_demux_index_entry_offset_search (GstAviIndexEntry * entry,
    guint64 * offset)
{
  if (entry->offset < *offset)
    return -1;
  else if (entry->offset > *offset)
    return 1;
  return 0;
}

static guint64
gst_avi_demux_seek_streams_index (GstAviDemux * avi, guint64 offset,
    gboolean before)
{
  GstAviStream *stream;
  GstAviIndexEntry *entry;
  gint i;
  gint64 val, min = offset;
  guint index = 0;

  for (i = 0; i < avi->num_streams; i++) {
    stream = &avi->stream[i];

    /* compensate for chunk header */
    offset += 8;
    entry =
        gst_util_array_binary_search (stream->index, stream->idx_n,
        sizeof (GstAviIndexEntry),
        (GCompareDataFunc) gst_avi_demux_index_entry_offset_search,
        before ? GST_SEARCH_MODE_BEFORE : GST_SEARCH_MODE_AFTER, &offset, NULL);
    offset -= 8;

    if (entry)
      index = entry - stream->index;

    if (before) {
      if (entry) {
        val = stream->index[index].offset;
        GST_DEBUG_OBJECT (avi,
            "stream %d, previous entry at %" G_GUINT64_FORMAT, i, val);
        if (val < min)
          min = val;
      }
      continue;
    }

    if (!entry) {
      GST_DEBUG_OBJECT (avi, "no position for stream %d, assuming at start", i);
      stream->current_entry = 0;
      stream->current_total = 0;
      continue;
    }

    val = stream->index[index].offset - 8;
    GST_DEBUG_OBJECT (avi, "stream %d, next entry at %" G_GUINT64_FORMAT, i,
        val);

    stream->current_total = stream->index[index].total;
    stream->current_entry = index;
  }

  return min;
}

#define GST_AVI_SEEK_PUSH_DISPLACE     (4 * GST_SECOND)

static gboolean
gst_avi_demux_handle_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstAviDemux *avi = GST_AVI_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (avi,
      "have event type %s: %p on sink pad", GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time, offset = 0;
      gboolean update;
      GstSegment segment;

      /* some debug output */
      gst_segment_init (&segment, GST_FORMAT_UNDEFINED);
      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);
      gst_segment_set_newsegment_full (&segment, update, rate, arate, format,
          start, stop, time);
      GST_DEBUG_OBJECT (avi,
          "received format %d newsegment %" GST_SEGMENT_FORMAT, format,
          &segment);

      /* chain will send initial newsegment after pads have been added */
      if (avi->state != GST_AVI_DEMUX_MOVI) {
        GST_DEBUG_OBJECT (avi, "still starting, eating event");
        goto exit;
      }

      /* we only expect a BYTE segment, e.g. following a seek */
      if (format != GST_FORMAT_BYTES) {
        GST_DEBUG_OBJECT (avi, "unsupported segment format, ignoring");
        goto exit;
      }

      if (avi->have_index) {
        GstAviIndexEntry *entry;
        guint i = 0, index = 0, k = 0;
        GstAviStream *stream;

        /* compensate chunk header, stored index offset points after header */
        start += 8;
        /* find which stream we're on */
        do {
          stream = &avi->stream[i];

          /* find the index for start bytes offset */
          entry = gst_util_array_binary_search (stream->index,
              stream->idx_n, sizeof (GstAviIndexEntry),
              (GCompareDataFunc) gst_avi_demux_index_entry_offset_search,
              GST_SEARCH_MODE_AFTER, &start, NULL);

          if (entry == NULL)
            continue;
          index = entry - stream->index;

          /* we are on the stream with a chunk start offset closest to start */
          if (!offset || stream->index[index].offset < offset) {
            offset = stream->index[index].offset;
            k = i;
          }
          /* exact match needs no further searching */
          if (stream->index[index].offset == start)
            break;
        } while (++i < avi->num_streams);
        start -= 8;
        offset -= 8;
        stream = &avi->stream[k];

        /* so we have no idea what is to come, or where we are */
        if (!offset) {
          GST_WARNING_OBJECT (avi, "insufficient index data, forcing EOS");
          goto eos;
        }

        /* get the ts corresponding to start offset bytes for the stream */
        gst_avi_demux_get_buffer_info (avi, stream, index,
            (GstClockTime *) & time, NULL, NULL, NULL);
      } else if (avi->element_index) {
        GstIndexEntry *entry;

        /* Let's check if we have an index entry for this position */
        entry = gst_index_get_assoc_entry (avi->element_index, avi->index_id,
            GST_INDEX_LOOKUP_AFTER, GST_ASSOCIATION_FLAG_NONE,
            GST_FORMAT_BYTES, start);

        /* we can not go where we have not yet been before ... */
        if (!entry) {
          GST_WARNING_OBJECT (avi, "insufficient index data, forcing EOS");
          goto eos;
        }

        gst_index_entry_assoc_map (entry, GST_FORMAT_TIME, &time);
        gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, &offset);
      } else {
        GST_WARNING_OBJECT (avi, "no index data, forcing EOS");
        goto eos;
      }

      stop = GST_CLOCK_TIME_NONE;

      /* set up segment and send downstream */
      gst_segment_set_newsegment_full (&avi->segment, update, rate, arate,
          GST_FORMAT_TIME, time, stop, time);
      GST_DEBUG_OBJECT (avi, "Pushing newseg update %d, rate %g, "
          "applied rate %g, format %d, start %" G_GINT64_FORMAT ", "
          "stop %" G_GINT64_FORMAT, update, rate, arate, GST_FORMAT_TIME,
          time, stop);
      gst_avi_demux_push_event (avi,
          gst_event_new_new_segment_full (update, rate, arate, GST_FORMAT_TIME,
              time, stop, time));

      GST_DEBUG_OBJECT (avi, "next chunk expected at %" G_GINT64_FORMAT, start);

      /* adjust state for streaming thread accordingly */
      if (avi->have_index)
        gst_avi_demux_seek_streams_index (avi, offset, FALSE);
      else
        gst_avi_demux_seek_streams (avi, offset, FALSE);

      /* set up streaming thread */
      g_assert (offset >= start);
      avi->offset = start;
      avi->todrop = offset - start;

    exit:
      gst_event_unref (event);
      res = TRUE;
      break;
    eos:
      /* set up for EOS */
      avi->have_eos = TRUE;
      goto exit;
    }
    case GST_EVENT_EOS:
    {
      if (avi->state != GST_AVI_DEMUX_MOVI) {
        gst_event_unref (event);
        GST_ELEMENT_ERROR (avi, STREAM, DEMUX,
            (NULL), ("got eos and didn't receive a complete header object"));
      } else if (!gst_avi_demux_push_event (avi, event)) {
        GST_ELEMENT_ERROR (avi, STREAM, DEMUX,
            (NULL), ("got eos but no streams (yet)"));
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      gint i;

      gst_adapter_clear (avi->adapter);
      avi->have_eos = FALSE;
      for (i = 0; i < avi->num_streams; i++) {
        avi->stream[i].last_flow = GST_FLOW_OK;
        avi->stream[i].discont = TRUE;
      }
      /* fall through to default case so that the event gets passed downstream */
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (avi);

  return res;
}

static gboolean
gst_avi_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstAviDemux *avi = GST_AVI_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (avi,
      "have event type %s: %p on src pad", GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      if (!avi->streaming) {
        res = gst_avi_demux_handle_seek (avi, pad, event);
      } else {
        res = gst_avi_demux_handle_seek_push (avi, pad, event);
      }
      gst_event_unref (event);
      break;
    case GST_EVENT_QOS:
    case GST_EVENT_NAVIGATION:
      res = FALSE;
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (avi);

  return res;
}

/* streaming helper (push) */

/*
 * gst_avi_demux_peek_chunk_info:
 * @avi: Avi object
 * @tag: holder for tag
 * @size: holder for tag size
 *
 * Peek next chunk info (tag and size)
 *
 * Returns: TRUE when one chunk info has been got
 */
static gboolean
gst_avi_demux_peek_chunk_info (GstAviDemux * avi, guint32 * tag, guint32 * size)
{
  const guint8 *data = NULL;

  if (gst_adapter_available (avi->adapter) < 8)
    return FALSE;

  data = gst_adapter_peek (avi->adapter, 8);
  *tag = GST_READ_UINT32_LE (data);
  *size = GST_READ_UINT32_LE (data + 4);

  return TRUE;
}

/*
 * gst_avi_demux_peek_chunk:
 * @avi: Avi object
 * @tag: holder for tag
 * @size: holder for tag size
 *
 * Peek enough data for one full chunk
 *
 * Returns: %TRUE when one chunk has been got
 */
static gboolean
gst_avi_demux_peek_chunk (GstAviDemux * avi, guint32 * tag, guint32 * size)
{
  guint32 peek_size = 0;
  gint available;

  if (!gst_avi_demux_peek_chunk_info (avi, tag, size))
    goto peek_failed;

  /* size 0 -> empty data buffer would surprise most callers,
   * large size -> do not bother trying to squeeze that into adapter,
   * so we throw poor man's exception, which can be caught if caller really
   * wants to handle 0 size chunk */
  if (!(*size) || (*size) >= (1 << 30))
    goto strange_size;

  peek_size = (*size + 1) & ~1;
  available = gst_adapter_available (avi->adapter);

  GST_DEBUG_OBJECT (avi,
      "Need to peek chunk of %d bytes to read chunk %" GST_FOURCC_FORMAT
      ", %d bytes available", *size, GST_FOURCC_ARGS (*tag), available);

  if (available < (8 + peek_size))
    goto need_more;

  return TRUE;

  /* ERRORS */
peek_failed:
  {
    GST_INFO_OBJECT (avi, "Failed to peek");
    return FALSE;
  }
strange_size:
  {
    GST_INFO_OBJECT (avi,
        "Invalid/unexpected chunk size %d for tag %" GST_FOURCC_FORMAT, *size,
        GST_FOURCC_ARGS (*tag));
    /* chain should give up */
    avi->abort_buffering = TRUE;
    return FALSE;
  }
need_more:
  {
    GST_INFO_OBJECT (avi, "need more %d < %" G_GUINT32_FORMAT,
        available, 8 + peek_size);
    return FALSE;
  }
}

/* AVI init */

/*
 * gst_avi_demux_parse_file_header:
 * @element: caller element (used for errors/debug).
 * @buf: input data to be used for parsing.
 *
 * "Open" a RIFF/AVI file. The buffer should be at least 12
 * bytes long. Takes ownership of @buf.
 *
 * Returns: TRUE if the file is a RIFF/AVI file, FALSE otherwise.
 *          Throws an error, caller should error out (fatal).
 */
static gboolean
gst_avi_demux_parse_file_header (GstElement * element, GstBuffer * buf)
{
  guint32 doctype;
  GstClockTime stamp;

  stamp = gst_util_get_timestamp ();

  /* riff_parse posts an error */
  if (!gst_riff_parse_file_header (element, buf, &doctype))
    return FALSE;

  if (doctype != GST_RIFF_RIFF_AVI)
    goto not_avi;

  stamp = gst_util_get_timestamp () - stamp;
  GST_DEBUG_OBJECT (element, "header parsing took %" GST_TIME_FORMAT,
      GST_TIME_ARGS (stamp));

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

/*
 * Read AVI file tag when streaming
 */
static GstFlowReturn
gst_avi_demux_stream_init_push (GstAviDemux * avi)
{
  if (gst_adapter_available (avi->adapter) >= 12) {
    GstBuffer *tmp;

    tmp = gst_adapter_take_buffer (avi->adapter, 12);

    GST_DEBUG ("Parsing avi header");
    if (!gst_avi_demux_parse_file_header (GST_ELEMENT_CAST (avi), tmp)) {
      return GST_FLOW_ERROR;
    }
    GST_DEBUG ("header ok");
    avi->offset += 12;

    avi->state = GST_AVI_DEMUX_HEADER;
  }
  return GST_FLOW_OK;
}

/*
 * Read AVI file tag
 */
static GstFlowReturn
gst_avi_demux_stream_init_pull (GstAviDemux * avi)
{
  GstFlowReturn res;
  GstBuffer *buf = NULL;

  res = gst_pad_pull_range (avi->sinkpad, avi->offset, 12, &buf);
  if (res != GST_FLOW_OK)
    return res;
  else if (!gst_avi_demux_parse_file_header (GST_ELEMENT_CAST (avi), buf))
    goto wrong_header;

  avi->offset += 12;

  return GST_FLOW_OK;

  /* ERRORS */
wrong_header:
  {
    GST_DEBUG_OBJECT (avi, "error parsing file header");
    return GST_FLOW_ERROR;
  }
}

/* AVI header handling */
/*
 * gst_avi_demux_parse_avih:
 * @avi: caller element (used for errors/debug).
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
gst_avi_demux_parse_avih (GstAviDemux * avi,
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
  GST_INFO_OBJECT (avi, "avih tag found:");
  GST_INFO_OBJECT (avi, " us_frame    %u", avih->us_frame);
  GST_INFO_OBJECT (avi, " max_bps     %u", avih->max_bps);
  GST_INFO_OBJECT (avi, " pad_gran    %u", avih->pad_gran);
  GST_INFO_OBJECT (avi, " flags       0x%08x", avih->flags);
  GST_INFO_OBJECT (avi, " tot_frames  %u", avih->tot_frames);
  GST_INFO_OBJECT (avi, " init_frames %u", avih->init_frames);
  GST_INFO_OBJECT (avi, " streams     %u", avih->streams);
  GST_INFO_OBJECT (avi, " bufsize     %u", avih->bufsize);
  GST_INFO_OBJECT (avi, " width       %u", avih->width);
  GST_INFO_OBJECT (avi, " height      %u", avih->height);
  GST_INFO_OBJECT (avi, " scale       %u", avih->scale);
  GST_INFO_OBJECT (avi, " rate        %u", avih->rate);
  GST_INFO_OBJECT (avi, " start       %u", avih->start);
  GST_INFO_OBJECT (avi, " length      %u", avih->length);

  *_avih = avih;
  gst_buffer_unref (buf);

  if (avih->us_frame != 0 && avih->tot_frames != 0)
    avi->duration =
        (guint64) avih->us_frame * (guint64) avih->tot_frames * 1000;
  else
    avi->duration = GST_CLOCK_TIME_NONE;

  GST_INFO_OBJECT (avi, " header duration  %" GST_TIME_FORMAT,
      GST_TIME_ARGS (avi->duration));

  return TRUE;

  /* ERRORS */
no_buffer:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL), ("No buffer"));
    return FALSE;
  }
avih_too_small:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("Too small avih (%d available, %d needed)",
            GST_BUFFER_SIZE (buf), (int) sizeof (gst_riff_avih)));
    gst_buffer_unref (buf);
    return FALSE;
  }
}

/*
 * gst_avi_demux_parse_superindex:
 * @avi: caller element (used for debugging/errors).
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
gst_avi_demux_parse_superindex (GstAviDemux * avi,
    GstBuffer * buf, guint64 ** _indexes)
{
  guint8 *data;
  guint16 bpe = 16;
  guint32 num, i;
  guint64 *indexes;
  guint size;

  *_indexes = NULL;

  size = buf ? GST_BUFFER_SIZE (buf) : 0;
  if (size < 24)
    goto too_small;

  data = GST_BUFFER_DATA (buf);

  /* check type of index. The opendml2 specs state that
   * there should be 4 dwords per array entry. Type can be
   * either frame or field (and we don't care). */
  if (GST_READ_UINT16_LE (data) != 4 ||
      (data[2] & 0xfe) != 0x0 || data[3] != 0x0) {
    GST_WARNING_OBJECT (avi,
        "Superindex for stream has unexpected "
        "size_entry %d (bytes) or flags 0x%02x/0x%02x",
        GST_READ_UINT16_LE (data), data[2], data[3]);
    bpe = GST_READ_UINT16_LE (data) * 4;
  }
  num = GST_READ_UINT32_LE (&data[4]);

  GST_DEBUG_OBJECT (avi, "got %d indexes", num);

  /* this can't work out well ... */
  if (num > G_MAXUINT32 >> 1 || bpe < 8) {
    goto invalid_params;
  }

  indexes = g_new (guint64, num + 1);
  for (i = 0; i < num; i++) {
    if (size < 24 + bpe * (i + 1))
      break;
    indexes[i] = GST_READ_UINT64_LE (&data[24 + bpe * i]);
    GST_DEBUG_OBJECT (avi, "index %d at %" G_GUINT64_FORMAT, i, indexes[i]);
  }
  indexes[i] = GST_BUFFER_OFFSET_NONE;
  *_indexes = indexes;

  gst_buffer_unref (buf);

  return TRUE;

  /* ERRORS */
too_small:
  {
    GST_ERROR_OBJECT (avi,
        "Not enough data to parse superindex (%d available, 24 needed)", size);
    if (buf)
      gst_buffer_unref (buf);
    return FALSE;
  }
invalid_params:
  {
    GST_ERROR_OBJECT (avi, "invalid index parameters (num = %d, bpe = %d)",
        num, bpe);
    if (buf)
      gst_buffer_unref (buf);
    return FALSE;
  }
}

/* add an entry to the index of a stream. @num should be an estimate of the
 * total amount of index entries for all streams and is used to dynamically
 * allocate memory for the index entries. */
static inline gboolean
gst_avi_demux_add_index (GstAviDemux * avi, GstAviStream * stream,
    guint num, GstAviIndexEntry * entry)
{
  /* ensure index memory */
  if (G_UNLIKELY (stream->idx_n >= stream->idx_max)) {
    guint idx_max = stream->idx_max;
    GstAviIndexEntry *new_idx;

    /* we need to make some more room */
    if (idx_max == 0) {
      /* initial size guess, assume each stream has an equal amount of entries,
       * overshoot with at least 8K */
      idx_max = (num / avi->num_streams) + (8192 / sizeof (GstAviIndexEntry));
    } else {
      idx_max += 8192 / sizeof (GstAviIndexEntry);
      GST_DEBUG_OBJECT (avi, "expanded index from %u to %u",
          stream->idx_max, idx_max);
    }
    new_idx = g_try_renew (GstAviIndexEntry, stream->index, idx_max);
    /* out of memory, if this fails stream->index is untouched. */
    if (G_UNLIKELY (!new_idx))
      return FALSE;
    /* use new index */
    stream->index = new_idx;
    stream->idx_max = idx_max;
  }

  /* update entry total and stream stats. The entry total can be converted to
   * the timestamp of the entry easily. */
  if (stream->strh->type == GST_RIFF_FCC_auds) {
    gint blockalign;

    if (stream->is_vbr) {
      entry->total = stream->total_blocks;
    } else {
      entry->total = stream->total_bytes;
    }
    blockalign = stream->strf.auds->blockalign;
    if (blockalign > 0)
      stream->total_blocks += DIV_ROUND_UP (entry->size, blockalign);
    else
      stream->total_blocks++;
  } else {
    if (stream->is_vbr) {
      entry->total = stream->idx_n;
    } else {
      entry->total = stream->total_bytes;
    }
  }
  stream->total_bytes += entry->size;
  if (ENTRY_IS_KEYFRAME (entry))
    stream->n_keyframes++;

  /* and add */
  GST_LOG_OBJECT (avi,
      "Adding stream %u, index entry %d, kf %d, size %u "
      ", offset %" G_GUINT64_FORMAT ", total %" G_GUINT64_FORMAT, stream->num,
      stream->idx_n, ENTRY_IS_KEYFRAME (entry), entry->size, entry->offset,
      entry->total);
  stream->index[stream->idx_n++] = *entry;

  return TRUE;
}

/* given @entry_n in @stream, calculate info such as timestamps and
 * offsets for the entry. */
static void
gst_avi_demux_get_buffer_info (GstAviDemux * avi, GstAviStream * stream,
    guint entry_n, GstClockTime * timestamp, GstClockTime * ts_end,
    guint64 * offset, guint64 * offset_end)
{
  GstAviIndexEntry *entry;

  entry = &stream->index[entry_n];

  if (stream->is_vbr) {
    /* VBR stream next timestamp */
    if (stream->strh->type == GST_RIFF_FCC_auds) {
      if (timestamp)
        *timestamp =
            avi_stream_convert_frames_to_time_unchecked (stream, entry->total);
      if (ts_end)
        *ts_end = avi_stream_convert_frames_to_time_unchecked (stream,
            entry->total + 1);
    } else {
      if (timestamp)
        *timestamp =
            avi_stream_convert_frames_to_time_unchecked (stream, entry_n);
      if (ts_end)
        *ts_end = avi_stream_convert_frames_to_time_unchecked (stream,
            entry_n + 1);
    }
  } else if (stream->strh->type == GST_RIFF_FCC_auds) {
    /* constant rate stream */
    if (timestamp)
      *timestamp =
          avi_stream_convert_bytes_to_time_unchecked (stream, entry->total);
    if (ts_end)
      *ts_end = avi_stream_convert_bytes_to_time_unchecked (stream,
          entry->total + entry->size);
  }
  if (stream->strh->type == GST_RIFF_FCC_vids) {
    /* video offsets are the frame number */
    if (offset)
      *offset = entry_n;
    if (offset_end)
      *offset_end = entry_n + 1;
  } else {
    /* no offsets for audio */
    if (offset)
      *offset = -1;
    if (offset_end)
      *offset_end = -1;
  }
}

/* collect and debug stats about the indexes for all streams.
 * This method is also responsible for filling in the stream duration
 * as measured by the amount of index entries.
 *
 * Returns TRUE if the index is not empty, else FALSE */
static gboolean
gst_avi_demux_do_index_stats (GstAviDemux * avi)
{
  guint total_idx = 0;
  guint i;
#ifndef GST_DISABLE_GST_DEBUG
  guint total_max = 0;
#endif

  /* get stream stats now */
  for (i = 0; i < avi->num_streams; i++) {
    GstAviStream *stream;

    if (G_UNLIKELY (!(stream = &avi->stream[i])))
      continue;
    if (G_UNLIKELY (!stream->strh))
      continue;
    if (G_UNLIKELY (!stream->index || stream->idx_n == 0))
      continue;

    /* we interested in the end_ts of the last entry, which is the total
     * duration of this stream */
    gst_avi_demux_get_buffer_info (avi, stream, stream->idx_n - 1,
        NULL, &stream->idx_duration, NULL, NULL);

    total_idx += stream->idx_n;
#ifndef GST_DISABLE_GST_DEBUG
    total_max += stream->idx_max;
#endif
    GST_INFO_OBJECT (avi, "Stream %d, dur %" GST_TIME_FORMAT ", %6u entries, "
        "%5u keyframes, entry size = %2u, total size = %10u, allocated %10u",
        i, GST_TIME_ARGS (stream->idx_duration), stream->idx_n,
        stream->n_keyframes, (guint) sizeof (GstAviIndexEntry),
        (guint) (stream->idx_n * sizeof (GstAviIndexEntry)),
        (guint) (stream->idx_max * sizeof (GstAviIndexEntry)));
  }
  total_idx *= sizeof (GstAviIndexEntry);
#ifndef GST_DISABLE_GST_DEBUG
  total_max *= sizeof (GstAviIndexEntry);
#endif
  GST_INFO_OBJECT (avi, "%u bytes for index vs %u ideally, %u wasted",
      total_max, total_idx, total_max - total_idx);

  if (total_idx == 0) {
    GST_WARNING_OBJECT (avi, "Index is empty !");
    return FALSE;
  }
  return TRUE;
}

/*
 * gst_avi_demux_parse_subindex:
 * @avi: Avi object
 * @buf: input data to use for parsing.
 * @stream: stream context.
 * @entries_list: a list (returned by the function) containing all the
 *           indexes parsed in this specific subindex. The first
 *           entry is also a pointer to allocated memory that needs
 *           to be free´ed. May be NULL if no supported indexes were
 *           found.
 *
 * Reads superindex (openDML-2 spec stuff) from the provided data.
 * The buffer should contain a GST_RIFF_TAG_ix?? chunk.
 *
 * Returns: TRUE on success, FALSE otherwise. Errors are fatal, we
 *          throw an error, caller should bail out asap.
 */
static gboolean
gst_avi_demux_parse_subindex (GstAviDemux * avi, GstAviStream * stream,
    GstBuffer * buf)
{
  guint8 *data;
  guint16 bpe;
  guint32 num, i;
  guint64 baseoff;
  guint size;

  if (!buf)
    return TRUE;

  size = GST_BUFFER_SIZE (buf);

  /* check size */
  if (size < 24)
    goto too_small;

  data = GST_BUFFER_DATA (buf);

  /* We don't support index-data yet */
  if (data[3] & 0x80)
    goto not_implemented;

  /* check type of index. The opendml2 specs state that
   * there should be 4 dwords per array entry. Type can be
   * either frame or field (and we don't care). */
  bpe = (data[2] & 0x01) ? 12 : 8;
  if (GST_READ_UINT16_LE (data) != bpe / 4 ||
      (data[2] & 0xfe) != 0x0 || data[3] != 0x1) {
    GST_WARNING_OBJECT (avi,
        "Superindex for stream %d has unexpected "
        "size_entry %d (bytes) or flags 0x%02x/0x%02x",
        stream->num, GST_READ_UINT16_LE (data), data[2], data[3]);
    bpe = GST_READ_UINT16_LE (data) * 4;
  }
  num = GST_READ_UINT32_LE (&data[4]);
  baseoff = GST_READ_UINT64_LE (&data[12]);

  /* If there's nothing, just return ! */
  if (num == 0)
    goto empty_index;

  GST_INFO_OBJECT (avi, "Parsing subindex, nr_entries = %6d", num);

  for (i = 0; i < num; i++) {
    GstAviIndexEntry entry;

    if (size < 24 + bpe * (i + 1))
      break;

    /* fill in offset and size. offset contains the keyframe flag in the
     * upper bit*/
    entry.offset = baseoff + GST_READ_UINT32_LE (&data[24 + bpe * i]);
    entry.size = GST_READ_UINT32_LE (&data[24 + bpe * i + 4]);
    /* handle flags */
    if (stream->strh->type == GST_RIFF_FCC_auds) {
      /* all audio frames are keyframes */
      ENTRY_SET_KEYFRAME (&entry);
    } else {
      /* else read flags */
      entry.flags = (entry.size & 0x80000000) ? 0 : GST_AVI_KEYFRAME;
    }
    entry.size &= ~0x80000000;

    /* and add */
    if (G_UNLIKELY (!gst_avi_demux_add_index (avi, stream, num, &entry)))
      goto out_of_mem;
  }
  gst_buffer_unref (buf);

  return TRUE;

  /* ERRORS */
too_small:
  {
    GST_ERROR_OBJECT (avi,
        "Not enough data to parse subindex (%d available, 24 needed)", size);
    gst_buffer_unref (buf);
    return TRUE;                /* continue */
  }
not_implemented:
  {
    GST_ELEMENT_ERROR (avi, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Subindex-is-data is not implemented"));
    gst_buffer_unref (buf);
    return FALSE;
  }
empty_index:
  {
    GST_DEBUG_OBJECT (avi, "the index is empty");
    gst_buffer_unref (buf);
    return TRUE;
  }
out_of_mem:
  {
    GST_ELEMENT_ERROR (avi, RESOURCE, NO_SPACE_LEFT, (NULL),
        ("Cannot allocate memory for %u*%u=%u bytes",
            (guint) sizeof (GstAviIndexEntry), num,
            (guint) sizeof (GstAviIndexEntry) * num));
    gst_buffer_unref (buf);
    return FALSE;
  }
}

/*
 * Create and push a flushing seek event upstream
 */
static gboolean
perform_seek_to_offset (GstAviDemux * demux, guint64 offset)
{
  GstEvent *event;
  gboolean res = 0;

  GST_DEBUG_OBJECT (demux, "Seeking to %" G_GUINT64_FORMAT, offset);

  event =
      gst_event_new_seek (1.0, GST_FORMAT_BYTES,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET, offset,
      GST_SEEK_TYPE_NONE, -1);

  res = gst_pad_push_event (demux->sinkpad, event);

  if (res)
    demux->offset = offset;
  return res;
}

/*
 * Read AVI index when streaming
 */
static gboolean
gst_avi_demux_read_subindexes_push (GstAviDemux * avi)
{
  guint32 tag = 0, size;
  GstBuffer *buf = NULL;
  guint odml_stream;

  GST_DEBUG_OBJECT (avi, "read subindexes for %d streams", avi->num_streams);

  if (avi->odml_subidxs[avi->odml_subidx] != avi->offset)
    return FALSE;

  if (!gst_avi_demux_peek_chunk (avi, &tag, &size))
    return TRUE;

  /* this is the ODML chunk we expect */
  odml_stream = avi->odml_stream;

  if ((tag != GST_MAKE_FOURCC ('i', 'x', '0' + odml_stream / 10,
              '0' + odml_stream % 10)) &&
      (tag != GST_MAKE_FOURCC ('0' + odml_stream / 10,
              '0' + odml_stream % 10, 'i', 'x'))) {
    GST_WARNING_OBJECT (avi, "Not an ix## chunk (%" GST_FOURCC_FORMAT ")",
        GST_FOURCC_ARGS (tag));
    return FALSE;
  }

  avi->offset += 8 + GST_ROUND_UP_2 (size);
  /* flush chunk header so we get just the 'size' payload data */
  gst_adapter_flush (avi->adapter, 8);
  buf = gst_adapter_take_buffer (avi->adapter, size);

  if (!gst_avi_demux_parse_subindex (avi, &avi->stream[odml_stream], buf))
    return FALSE;

  /* we parsed the index, go to next subindex */
  avi->odml_subidx++;

  if (avi->odml_subidxs[avi->odml_subidx] == GST_BUFFER_OFFSET_NONE) {
    /* we reached the end of the indexes for this stream, move to the next
     * stream to handle the first index */
    avi->odml_stream++;
    avi->odml_subidx = 0;

    if (avi->odml_stream < avi->num_streams) {
      /* there are more indexes */
      avi->odml_subidxs = avi->stream[avi->odml_stream].indexes;
    } else {
      /* we're done, get stream stats now */
      avi->have_index = gst_avi_demux_do_index_stats (avi);

      return TRUE;
    }
  }

  /* seek to next index */
  return perform_seek_to_offset (avi, avi->odml_subidxs[avi->odml_subidx]);
}

/*
 * Read AVI index
 */
static void
gst_avi_demux_read_subindexes_pull (GstAviDemux * avi)
{
  guint32 tag;
  GstBuffer *buf;
  gint i, n;

  GST_DEBUG_OBJECT (avi, "read subindexes for %d streams", avi->num_streams);

  for (n = 0; n < avi->num_streams; n++) {
    GstAviStream *stream = &avi->stream[n];

    if (stream->indexes == NULL)
      continue;

    for (i = 0; stream->indexes[i] != GST_BUFFER_OFFSET_NONE; i++) {
      if (gst_riff_read_chunk (GST_ELEMENT_CAST (avi), avi->sinkpad,
              &stream->indexes[i], &tag, &buf) != GST_FLOW_OK)
        continue;
      else if ((tag != GST_MAKE_FOURCC ('i', 'x', '0' + stream->num / 10,
                  '0' + stream->num % 10)) &&
          (tag != GST_MAKE_FOURCC ('0' + stream->num / 10,
                  '0' + stream->num % 10, 'i', 'x'))) {
        /* Some ODML files (created by god knows what muxer) have a ##ix format
         * instead of the 'official' ix##. They are still valid though. */
        GST_WARNING_OBJECT (avi, "Not an ix## chunk (%" GST_FOURCC_FORMAT ")",
            GST_FOURCC_ARGS (tag));
        gst_buffer_unref (buf);
        continue;
      }

      if (!gst_avi_demux_parse_subindex (avi, stream, buf))
        continue;
    }

    g_free (stream->indexes);
    stream->indexes = NULL;
  }
  /* get stream stats now */
  avi->have_index = gst_avi_demux_do_index_stats (avi);
}

/*
 * gst_avi_demux_riff_parse_vprp:
 * @element: caller element (used for debugging/error).
 * @buf: input data to be used for parsing, stripped from header.
 * @vprp: a pointer (returned by this function) to a filled-in vprp
 *        structure. Caller should free it.
 *
 * Parses a video stream´s vprp. This function takes ownership of @buf.
 *
 * Returns: TRUE if parsing succeeded, otherwise FALSE. The stream
 *          should be skipped on error, but it is not fatal.
 */
static gboolean
gst_avi_demux_riff_parse_vprp (GstElement * element,
    GstBuffer * buf, gst_riff_vprp ** _vprp)
{
  gst_riff_vprp *vprp;
  gint k;

  g_return_val_if_fail (buf != NULL, FALSE);
  g_return_val_if_fail (_vprp != NULL, FALSE);

  if (GST_BUFFER_SIZE (buf) < G_STRUCT_OFFSET (gst_riff_vprp, field_info))
    goto too_small;

  vprp = g_memdup (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  vprp->format_token = GUINT32_FROM_LE (vprp->format_token);
  vprp->standard = GUINT32_FROM_LE (vprp->standard);
  vprp->vert_rate = GUINT32_FROM_LE (vprp->vert_rate);
  vprp->hor_t_total = GUINT32_FROM_LE (vprp->hor_t_total);
  vprp->vert_lines = GUINT32_FROM_LE (vprp->vert_lines);
  vprp->aspect = GUINT32_FROM_LE (vprp->aspect);
  vprp->width = GUINT32_FROM_LE (vprp->width);
  vprp->height = GUINT32_FROM_LE (vprp->height);
  vprp->fields = GUINT32_FROM_LE (vprp->fields);
#endif

  /* size checking */
  /* calculate fields based on size */
  k = (GST_BUFFER_SIZE (buf) - G_STRUCT_OFFSET (gst_riff_vprp, field_info)) /
      vprp->fields;
  if (vprp->fields > k) {
    GST_WARNING_OBJECT (element,
        "vprp header indicated %d fields, only %d available", vprp->fields, k);
    vprp->fields = k;
  }
  if (vprp->fields > GST_RIFF_VPRP_VIDEO_FIELDS) {
    GST_WARNING_OBJECT (element,
        "vprp header indicated %d fields, at most %d supported", vprp->fields,
        GST_RIFF_VPRP_VIDEO_FIELDS);
    vprp->fields = GST_RIFF_VPRP_VIDEO_FIELDS;
  }
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
  for (k = 0; k < vprp->fields; k++) {
    gst_riff_vprp_video_field_desc *fd;

    fd = &vprp->field_info[k];
    fd->compressed_bm_height = GUINT32_FROM_LE (fd->compressed_bm_height);
    fd->compressed_bm_width = GUINT32_FROM_LE (fd->compressed_bm_width);
    fd->valid_bm_height = GUINT32_FROM_LE (fd->valid_bm_height);
    fd->valid_bm_width = GUINT16_FROM_LE (fd->valid_bm_width);
    fd->valid_bm_x_offset = GUINT16_FROM_LE (fd->valid_bm_x_offset);
    fd->valid_bm_y_offset = GUINT32_FROM_LE (fd->valid_bm_y_offset);
    fd->video_x_t_offset = GUINT32_FROM_LE (fd->video_x_t_offset);
    fd->video_y_start = GUINT32_FROM_LE (fd->video_y_start);
  }
#endif

  /* debug */
  GST_INFO_OBJECT (element, "vprp tag found in context vids:");
  GST_INFO_OBJECT (element, " format_token  %d", vprp->format_token);
  GST_INFO_OBJECT (element, " standard      %d", vprp->standard);
  GST_INFO_OBJECT (element, " vert_rate     %d", vprp->vert_rate);
  GST_INFO_OBJECT (element, " hor_t_total   %d", vprp->hor_t_total);
  GST_INFO_OBJECT (element, " vert_lines    %d", vprp->vert_lines);
  GST_INFO_OBJECT (element, " aspect        %d:%d", vprp->aspect >> 16,
      vprp->aspect & 0xffff);
  GST_INFO_OBJECT (element, " width         %d", vprp->width);
  GST_INFO_OBJECT (element, " height        %d", vprp->height);
  GST_INFO_OBJECT (element, " fields        %d", vprp->fields);
  for (k = 0; k < vprp->fields; k++) {
    gst_riff_vprp_video_field_desc *fd;

    fd = &(vprp->field_info[k]);
    GST_INFO_OBJECT (element, " field %u description:", k);
    GST_INFO_OBJECT (element, "  compressed_bm_height  %d",
        fd->compressed_bm_height);
    GST_INFO_OBJECT (element, "  compressed_bm_width  %d",
        fd->compressed_bm_width);
    GST_INFO_OBJECT (element, "  valid_bm_height       %d",
        fd->valid_bm_height);
    GST_INFO_OBJECT (element, "  valid_bm_width        %d", fd->valid_bm_width);
    GST_INFO_OBJECT (element, "  valid_bm_x_offset     %d",
        fd->valid_bm_x_offset);
    GST_INFO_OBJECT (element, "  valid_bm_y_offset     %d",
        fd->valid_bm_y_offset);
    GST_INFO_OBJECT (element, "  video_x_t_offset      %d",
        fd->video_x_t_offset);
    GST_INFO_OBJECT (element, "  video_y_start         %d", fd->video_y_start);
  }

  gst_buffer_unref (buf);

  *_vprp = vprp;

  return TRUE;

  /* ERRORS */
too_small:
  {
    GST_ERROR_OBJECT (element,
        "Too small vprp (%d available, at least %d needed)",
        GST_BUFFER_SIZE (buf),
        (int) G_STRUCT_OFFSET (gst_riff_vprp, field_info));
    gst_buffer_unref (buf);
    return FALSE;
  }
}

static void
gst_avi_demux_expose_streams (GstAviDemux * avi, gboolean force)
{
  guint i;

  GST_DEBUG_OBJECT (avi, "force : %d", force);

  for (i = 0; i < avi->num_streams; i++) {
    GstAviStream *stream = &avi->stream[i];

    if (force || stream->idx_n != 0) {
      GST_LOG_OBJECT (avi, "Added pad %s with caps %" GST_PTR_FORMAT,
          GST_PAD_NAME (stream->pad), GST_PAD_CAPS (stream->pad));
      gst_element_add_pad ((GstElement *) avi, stream->pad);

      if (avi->element_index)
        gst_index_get_writer_id (avi->element_index,
            GST_OBJECT_CAST (stream->pad), &stream->index_id);

      stream->exposed = TRUE;
      if (avi->main_stream == -1)
        avi->main_stream = i;
    } else {
      GST_WARNING_OBJECT (avi, "Stream #%d doesn't have any entry, removing it",
          i);
      gst_avi_demux_reset_stream (avi, stream);
    }
  }
}

/* buf contains LIST chunk data, and will be padded to even size,
 * since some buggy files do not account for the padding of chunks
 * within a LIST in the size of the LIST */
static inline void
gst_avi_demux_roundup_list (GstAviDemux * avi, GstBuffer ** buf)
{
  gint size = GST_BUFFER_SIZE (*buf);

  if (G_UNLIKELY (size & 1)) {
    GstBuffer *obuf;

    GST_DEBUG_OBJECT (avi, "rounding up dubious list size %d", size);
    obuf = gst_buffer_new_and_alloc (size + 1);
    memcpy (GST_BUFFER_DATA (obuf), GST_BUFFER_DATA (*buf), size);
    /* assume 0 padding, at least makes outcome deterministic */
    (GST_BUFFER_DATA (obuf))[size] = 0;
    gst_buffer_replace (buf, obuf);
  }
}

/*
 * gst_avi_demux_parse_stream:
 * @avi: calling element (used for debugging/errors).
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
gst_avi_demux_parse_stream (GstAviDemux * avi, GstBuffer * buf)
{
  GstAviStream *stream;
  GstElementClass *klass;
  GstPadTemplate *templ;
  GstBuffer *sub = NULL;
  guint offset = 4;
  guint32 tag = 0;
  gchar *codec_name = NULL, *padname = NULL;
  const gchar *tag_name;
  GstCaps *caps = NULL;
  GstPad *pad;
  GstElement *element;
  gboolean got_strh = FALSE, got_strf = FALSE, got_vprp = FALSE;
  gst_riff_vprp *vprp = NULL;

  element = GST_ELEMENT_CAST (avi);

  GST_DEBUG_OBJECT (avi, "Parsing stream");

  gst_avi_demux_roundup_list (avi, &buf);

  if (avi->num_streams >= GST_AVI_DEMUX_MAX_STREAMS) {
    GST_WARNING_OBJECT (avi,
        "maximum no of streams (%d) exceeded, ignoring stream",
        GST_AVI_DEMUX_MAX_STREAMS);
    gst_buffer_unref (buf);
    /* not a fatal error, let's say */
    return TRUE;
  }

  stream = &avi->stream[avi->num_streams];

  /* initial settings */
  stream->idx_duration = GST_CLOCK_TIME_NONE;
  stream->hdr_duration = GST_CLOCK_TIME_NONE;
  stream->duration = GST_CLOCK_TIME_NONE;

  while (gst_riff_parse_chunk (element, buf, &offset, &tag, &sub)) {
    /* sub can be NULL if the chunk is empty */
    if (sub == NULL) {
      GST_DEBUG_OBJECT (avi, "ignoring empty chunk %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (tag));
      continue;
    }
    switch (tag) {
      case GST_RIFF_TAG_strh:
      {
        gst_riff_strh *strh;

        if (got_strh) {
          GST_WARNING_OBJECT (avi, "Ignoring additional strh chunk");
          break;
        }
        if (!gst_riff_parse_strh (element, sub, &stream->strh)) {
          /* ownership given away */
          sub = NULL;
          GST_WARNING_OBJECT (avi, "Failed to parse strh chunk");
          goto fail;
        }
        sub = NULL;
        strh = stream->strh;
        /* sanity check; stream header frame rate matches global header
         * frame duration */
        if (stream->strh->type == GST_RIFF_FCC_vids) {
          GstClockTime s_dur;
          GstClockTime h_dur = avi->avih->us_frame * GST_USECOND;

          s_dur = gst_util_uint64_scale (GST_SECOND, strh->scale, strh->rate);
          GST_DEBUG_OBJECT (avi, "verifying stream framerate %d/%d, "
              "frame duration = %d ms", strh->rate, strh->scale,
              (gint) (s_dur / GST_MSECOND));
          if (h_dur > (10 * GST_MSECOND) && (s_dur > 10 * h_dur)) {
            strh->rate = GST_SECOND / GST_USECOND;
            strh->scale = h_dur / GST_USECOND;
            GST_DEBUG_OBJECT (avi, "correcting stream framerate to %d/%d",
                strh->rate, strh->scale);
          }
        }
        /* determine duration as indicated by header */
        stream->hdr_duration = gst_util_uint64_scale ((guint64) strh->length *
            strh->scale, GST_SECOND, (guint64) strh->rate);
        GST_INFO ("Stream duration according to header: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (stream->hdr_duration));
        if (stream->hdr_duration == 0)
          stream->hdr_duration = GST_CLOCK_TIME_NONE;

        got_strh = TRUE;
        break;
      }
      case GST_RIFF_TAG_strf:
      {
        gboolean res = FALSE;

        if (got_strf) {
          GST_WARNING_OBJECT (avi, "Ignoring additional strf chunk");
          break;
        }
        if (!got_strh) {
          GST_ERROR_OBJECT (avi, "Found strf chunk before strh chunk");
          goto fail;
        }
        switch (stream->strh->type) {
          case GST_RIFF_FCC_vids:
            stream->is_vbr = TRUE;
            res = gst_riff_parse_strf_vids (element, sub,
                &stream->strf.vids, &stream->extradata);
            sub = NULL;
            GST_DEBUG_OBJECT (element, "marking video as VBR, res %d", res);
            break;
          case GST_RIFF_FCC_auds:
            res =
                gst_riff_parse_strf_auds (element, sub, &stream->strf.auds,
                &stream->extradata);
            sub = NULL;
            if (!res)
              break;
            stream->is_vbr = (stream->strh->samplesize == 0)
                && stream->strh->scale > 1
                && stream->strf.auds->blockalign != 1;
            GST_DEBUG_OBJECT (element, "marking audio as VBR:%d, res %d",
                stream->is_vbr, res);
            /* we need these or we have no way to come up with timestamps */
            if ((!stream->is_vbr && !stream->strf.auds->av_bps) ||
                (stream->is_vbr && (!stream->strh->scale ||
                        !stream->strh->rate))) {
              GST_WARNING_OBJECT (element,
                  "invalid audio header, ignoring stream");
              goto fail;
            }
            /* some more sanity checks */
            if (stream->is_vbr) {
              if (stream->strf.auds->blockalign <= 4) {
                /* that would mean (too) many frames per chunk,
                 * so not likely set as expected */
                GST_DEBUG_OBJECT (element,
                    "suspicious blockalign %d for VBR audio; "
                    "overriding to 1 frame per chunk",
                    stream->strf.auds->blockalign);
                /* this should top any likely value */
                stream->strf.auds->blockalign = (1 << 12);
              }
            }
            break;
          case GST_RIFF_FCC_iavs:
            stream->is_vbr = TRUE;
            res = gst_riff_parse_strf_iavs (element, sub,
                &stream->strf.iavs, &stream->extradata);
            sub = NULL;
            GST_DEBUG_OBJECT (element, "marking iavs as VBR, res %d", res);
            break;
          case GST_RIFF_FCC_txts:
            /* nothing to parse here */
            stream->is_vbr = (stream->strh->samplesize == 0)
                && (stream->strh->scale > 1);
            res = TRUE;
            break;
          default:
            GST_ERROR_OBJECT (avi,
                "Don´t know how to handle stream type %" GST_FOURCC_FORMAT,
                GST_FOURCC_ARGS (stream->strh->type));
            break;
        }
        if (sub) {
          gst_buffer_unref (sub);
          sub = NULL;
        }
        if (!res)
          goto fail;
        got_strf = TRUE;
        break;
      }
      case GST_RIFF_TAG_vprp:
      {
        if (got_vprp) {
          GST_WARNING_OBJECT (avi, "Ignoring additional vprp chunk");
          break;
        }
        if (!got_strh) {
          GST_ERROR_OBJECT (avi, "Found vprp chunk before strh chunk");
          goto fail;
        }
        if (!got_strf) {
          GST_ERROR_OBJECT (avi, "Found vprp chunk before strf chunk");
          goto fail;
        }

        if (!gst_avi_demux_riff_parse_vprp (element, sub, &vprp)) {
          GST_WARNING_OBJECT (avi, "Failed to parse vprp chunk");
          /* not considered fatal */
          g_free (vprp);
          vprp = NULL;
        } else
          got_vprp = TRUE;
        sub = NULL;
        break;
      }
      case GST_RIFF_TAG_strd:
        if (stream->initdata)
          gst_buffer_unref (stream->initdata);
        stream->initdata = sub;
        sub = NULL;
        break;
      case GST_RIFF_TAG_strn:
        g_free (stream->name);
        if (sub != NULL) {
          stream->name =
              g_strndup ((gchar *) GST_BUFFER_DATA (sub),
              (gsize) GST_BUFFER_SIZE (sub));
          gst_buffer_unref (sub);
          sub = NULL;
        } else {
          stream->name = g_strdup ("");
        }
        GST_DEBUG_OBJECT (avi, "stream name: %s", stream->name);
        break;
      case GST_RIFF_IDIT:
        gst_avi_demux_parse_idit (avi, sub);
        break;
      default:
        if (tag == GST_MAKE_FOURCC ('i', 'n', 'd', 'x') ||
            tag == GST_MAKE_FOURCC ('i', 'x', '0' + avi->num_streams / 10,
                '0' + avi->num_streams % 10)) {
          g_free (stream->indexes);
          gst_avi_demux_parse_superindex (avi, sub, &stream->indexes);
          stream->superindex = TRUE;
          sub = NULL;
          break;
        }
        GST_WARNING_OBJECT (avi,
            "Unknown stream header tag %" GST_FOURCC_FORMAT ", ignoring",
            GST_FOURCC_ARGS (tag));
        /* fall-through */
      case GST_RIFF_TAG_JUNQ:
      case GST_RIFF_TAG_JUNK:
        break;
    }
    if (sub != NULL) {
      gst_buffer_unref (sub);
      sub = NULL;
    }
  }

  if (!got_strh) {
    GST_WARNING_OBJECT (avi, "Failed to find strh chunk");
    goto fail;
  }

  if (!got_strf) {
    GST_WARNING_OBJECT (avi, "Failed to find strf chunk");
    goto fail;
  }

  /* get class to figure out the template */
  klass = GST_ELEMENT_GET_CLASS (avi);

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
      } else if (got_vprp && vprp) {
        guint32 aspect_n, aspect_d;
        gint n, d;

        aspect_n = vprp->aspect >> 16;
        aspect_d = vprp->aspect & 0xffff;
        /* calculate the pixel aspect ratio using w/h and aspect ratio */
        n = aspect_n * stream->strf.vids->height;
        d = aspect_d * stream->strf.vids->width;
        if (n && d)
          gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              n, d, NULL);
        /* very local, not needed elsewhere */
        g_free (vprp);
        vprp = NULL;
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
    case GST_RIFF_FCC_txts:{
      padname = g_strdup_printf ("subtitle_%02d", avi->num_t_streams);
      templ = gst_element_class_get_pad_template (klass, "subtitle_%02d");
      caps = gst_caps_new_simple ("application/x-subtitle-avi", NULL);
      tag_name = NULL;
      avi->num_t_streams++;
      break;
    }
    default:
      g_return_val_if_reached (FALSE);
  }

  /* no caps means no stream */
  if (!caps) {
    GST_ERROR_OBJECT (element, "Did not find caps for stream %s", padname);
    goto fail;
  }

  GST_DEBUG_OBJECT (element, "codec-name=%s",
      (codec_name ? codec_name : "NULL"));
  GST_DEBUG_OBJECT (element, "caps=%" GST_PTR_FORMAT, caps);

  /* set proper settings and add it */
  if (stream->pad)
    gst_object_unref (stream->pad);
  pad = stream->pad = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  gst_pad_use_fixed_caps (pad);
#if 0
  gst_pad_set_formats_function (pad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_get_src_formats));
  gst_pad_set_event_mask_function (pad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_get_event_mask));
#endif
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_handle_src_event));
  gst_pad_set_query_type_function (pad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_get_src_query_types));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_handle_src_query));
#if 0
  gst_pad_set_convert_function (pad,
      GST_DEBUG_FUNCPTR (gst_avi_demux_src_convert));
#endif

  stream->num = avi->num_streams;

  stream->start_entry = 0;
  stream->step_entry = 0;
  stream->stop_entry = 0;

  stream->current_entry = -1;
  stream->current_total = 0;

  stream->last_flow = GST_FLOW_OK;
  stream->discont = TRUE;

  stream->total_bytes = 0;
  stream->total_blocks = 0;
  stream->n_keyframes = 0;

  stream->idx_n = 0;
  stream->idx_max = 0;

  gst_pad_set_element_private (pad, stream);
  avi->num_streams++;

  gst_pad_set_caps (pad, caps);
  gst_pad_set_active (pad, TRUE);
  gst_caps_unref (caps);

  /* make tags */
  if (codec_name) {
    if (!stream->taglist)
      stream->taglist = gst_tag_list_new ();

    avi->got_tags = TRUE;

    gst_tag_list_add (stream->taglist, GST_TAG_MERGE_APPEND, tag_name,
        codec_name, NULL);
    g_free (codec_name);
  }

  gst_buffer_unref (buf);

  return TRUE;

  /* ERRORS */
fail:
  {
    /* unref any mem that may be in use */
    if (buf)
      gst_buffer_unref (buf);
    if (sub)
      gst_buffer_unref (sub);
    g_free (vprp);
    g_free (codec_name);
    gst_avi_demux_reset_stream (avi, stream);
    avi->num_streams++;
    return FALSE;
  }
}

/*
 * gst_avi_demux_parse_odml:
 * @avi: calling element (used for debug/error).
 * @buf: input buffer to be used for parsing.
 *
 * Read an openDML-2.0 extension header. Fills in the frame number
 * in the avi demuxer object when reading succeeds.
 */
static void
gst_avi_demux_parse_odml (GstAviDemux * avi, GstBuffer * buf)
{
  guint32 tag = 0;
  guint offset = 4;
  GstBuffer *sub = NULL;

  while (gst_riff_parse_chunk (GST_ELEMENT_CAST (avi), buf, &offset, &tag,
          &sub)) {
    switch (tag) {
      case GST_RIFF_TAG_dmlh:{
        gst_riff_dmlh dmlh, *_dmlh;
        guint size;

        /* sub == NULL is possible and means an empty buffer */
        size = sub ? GST_BUFFER_SIZE (sub) : 0;

        /* check size */
        if (size < sizeof (gst_riff_dmlh)) {
          GST_ERROR_OBJECT (avi,
              "DMLH entry is too small (%d bytes, %d needed)",
              size, (int) sizeof (gst_riff_dmlh));
          goto next;
        }
        _dmlh = (gst_riff_dmlh *) GST_BUFFER_DATA (sub);
        dmlh.totalframes = GST_READ_UINT32_LE (&_dmlh->totalframes);

        GST_INFO_OBJECT (avi, "dmlh tag found: totalframes: %u",
            dmlh.totalframes);

        avi->avih->tot_frames = dmlh.totalframes;
        goto next;
      }

      default:
        GST_WARNING_OBJECT (avi,
            "Unknown tag %" GST_FOURCC_FORMAT " in ODML header",
            GST_FOURCC_ARGS (tag));
        /* fall-through */
      case GST_RIFF_TAG_JUNQ:
      case GST_RIFF_TAG_JUNK:
      next:
        /* skip and move to next chunk */
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

/* Index helper */
static guint
gst_avi_demux_index_last (GstAviDemux * avi, GstAviStream * stream)
{
  return stream->idx_n;
}

/* find a previous entry in the index with the given flags */
static guint
gst_avi_demux_index_prev (GstAviDemux * avi, GstAviStream * stream,
    guint last, gboolean keyframe)
{
  GstAviIndexEntry *entry;
  guint i;

  for (i = last; i > 0; i--) {
    entry = &stream->index[i - 1];
    if (!keyframe || ENTRY_IS_KEYFRAME (entry)) {
      return i - 1;
    }
  }
  return 0;
}

static guint
gst_avi_demux_index_next (GstAviDemux * avi, GstAviStream * stream,
    guint last, gboolean keyframe)
{
  GstAviIndexEntry *entry;
  gint i;

  for (i = last + 1; i < stream->idx_n; i++) {
    entry = &stream->index[i];
    if (!keyframe || ENTRY_IS_KEYFRAME (entry)) {
      return i;
    }
  }
  return stream->idx_n - 1;
}

static guint
gst_avi_demux_index_entry_search (GstAviIndexEntry * entry, guint64 * total)
{
  if (entry->total < *total)
    return -1;
  else if (entry->total > *total)
    return 1;
  return 0;
}

/*
 * gst_avi_demux_index_for_time:
 * @avi: Avi object
 * @stream: the stream
 * @time: a time position
 *
 * Finds the index entry which time is less or equal than the requested time.
 * Try to avoid binary search when we can convert the time to an index
 * position directly (for example for video frames with a fixed duration).
 *
 * Returns: the found position in the index.
 */
static guint
gst_avi_demux_index_for_time (GstAviDemux * avi,
    GstAviStream * stream, guint64 time)
{
  guint index = -1;
  guint64 total;

  GST_LOG_OBJECT (avi, "search time:%" GST_TIME_FORMAT, GST_TIME_ARGS (time));

  /* easy (and common) cases */
  if (time == 0 || stream->idx_n == 0)
    return 0;
  if (time >= stream->idx_duration)
    return stream->idx_n - 1;

  /* figure out where we need to go. For that we convert the time to an
   * index entry or we convert it to a total and then do a binary search. */
  if (stream->is_vbr) {
    /* VBR stream next timestamp */
    if (stream->strh->type == GST_RIFF_FCC_auds) {
      total = avi_stream_convert_time_to_frames_unchecked (stream, time);
    } else {
      index = avi_stream_convert_time_to_frames_unchecked (stream, time);
    }
  } else {
    /* constant rate stream */
    total = avi_stream_convert_time_to_bytes_unchecked (stream, time);
  }

  if (index == -1) {
    GstAviIndexEntry *entry;

    /* no index, find index with binary search on total */
    GST_LOG_OBJECT (avi, "binary search for entry with total %"
        G_GUINT64_FORMAT, total);

    entry = gst_util_array_binary_search (stream->index,
        stream->idx_n, sizeof (GstAviIndexEntry),
        (GCompareDataFunc) gst_avi_demux_index_entry_search,
        GST_SEARCH_MODE_BEFORE, &total, NULL);

    if (entry == NULL) {
      GST_LOG_OBJECT (avi, "not found, assume index 0");
      index = 0;
    } else {
      index = entry - stream->index;
      GST_LOG_OBJECT (avi, "found at %u", index);
    }
  } else {
    GST_LOG_OBJECT (avi, "converted time to index %u", index);
  }

  return index;
}

static inline GstAviStream *
gst_avi_demux_stream_for_id (GstAviDemux * avi, guint32 id)
{
  guint stream_nr;
  GstAviStream *stream;

  /* get the stream for this entry */
  stream_nr = CHUNKID_TO_STREAMNR (id);
  if (G_UNLIKELY (stream_nr >= avi->num_streams)) {
    GST_WARNING_OBJECT (avi, "invalid stream nr %d", stream_nr);
    return NULL;
  }
  stream = &avi->stream[stream_nr];
  if (G_UNLIKELY (!stream->strh)) {
    GST_WARNING_OBJECT (avi, "Unhandled stream %d, skipping", stream_nr);
    return NULL;
  }
  return stream;
}

/*
 * gst_avi_demux_parse_index:
 * @avi: calling element (used for debugging/errors).
 * @buf: buffer containing the full index.
 *
 * Read index entries from the provided buffer.
 * The buffer should contain a GST_RIFF_TAG_idx1 chunk.
 */
static gboolean
gst_avi_demux_parse_index (GstAviDemux * avi, GstBuffer * buf)
{
  guint8 *data;
  guint size;
  guint i, num, n;
  gst_riff_index_entry *index;
  GstClockTime stamp;
  GstAviStream *stream;
  GstAviIndexEntry entry;
  guint32 id;

  if (!buf)
    return FALSE;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  stamp = gst_util_get_timestamp ();

  /* see how many items in the index */
  num = size / sizeof (gst_riff_index_entry);
  if (num == 0)
    goto empty_list;

  GST_INFO_OBJECT (avi, "Parsing index, nr_entries = %6d", num);

  index = (gst_riff_index_entry *) data;

  /* figure out if the index is 0 based or relative to the MOVI start */
  entry.offset = GST_READ_UINT32_LE (&index[0].offset);
  if (entry.offset < avi->offset) {
    avi->index_offset = avi->offset + 8;
    GST_DEBUG ("index_offset = %" G_GUINT64_FORMAT, avi->index_offset);
  } else {
    avi->index_offset = 0;
    GST_DEBUG ("index is 0 based");
  }

  for (i = 0, n = 0; i < num; i++) {
    id = GST_READ_UINT32_LE (&index[i].id);
    entry.offset = GST_READ_UINT32_LE (&index[i].offset);

    /* some sanity checks */
    if (G_UNLIKELY (id == GST_RIFF_rec || id == 0 ||
            (entry.offset == 0 && n > 0)))
      continue;

    /* get the stream for this entry */
    stream = gst_avi_demux_stream_for_id (avi, id);
    if (G_UNLIKELY (!stream))
      continue;

    /* handle offset and size */
    entry.offset += avi->index_offset + 8;
    entry.size = GST_READ_UINT32_LE (&index[i].size);

    /* handle flags */
    if (stream->strh->type == GST_RIFF_FCC_auds) {
      /* all audio frames are keyframes */
      ENTRY_SET_KEYFRAME (&entry);
    } else {
      guint32 flags;
      /* else read flags */
      flags = GST_READ_UINT32_LE (&index[i].flags);
      if (flags & GST_RIFF_IF_KEYFRAME) {
        ENTRY_SET_KEYFRAME (&entry);
      } else {
        ENTRY_UNSET_KEYFRAME (&entry);
      }
    }

    /* and add */
    if (G_UNLIKELY (!gst_avi_demux_add_index (avi, stream, num, &entry)))
      goto out_of_mem;

    n++;
  }
  gst_buffer_unref (buf);

  /* get stream stats now */
  avi->have_index = gst_avi_demux_do_index_stats (avi);

  stamp = gst_util_get_timestamp () - stamp;
  GST_DEBUG_OBJECT (avi, "index parsing took %" GST_TIME_FORMAT,
      GST_TIME_ARGS (stamp));

  return TRUE;

  /* ERRORS */
empty_list:
  {
    GST_DEBUG_OBJECT (avi, "empty index");
    gst_buffer_unref (buf);
    return FALSE;
  }
out_of_mem:
  {
    GST_ELEMENT_ERROR (avi, RESOURCE, NO_SPACE_LEFT, (NULL),
        ("Cannot allocate memory for %u*%u=%u bytes",
            (guint) sizeof (GstAviIndexEntry), num,
            (guint) sizeof (GstAviIndexEntry) * num));
    gst_buffer_unref (buf);
    return FALSE;
  }
}

/*
 * gst_avi_demux_stream_index:
 * @avi: avi demuxer object.
 *
 * Seeks to index and reads it.
 */
static void
gst_avi_demux_stream_index (GstAviDemux * avi)
{
  GstFlowReturn res;
  guint64 offset = avi->offset;
  GstBuffer *buf;
  guint32 tag;
  guint32 size;

  GST_DEBUG ("demux stream index at offset %" G_GUINT64_FORMAT, offset);

  /* get chunk information */
  res = gst_pad_pull_range (avi->sinkpad, offset, 8, &buf);
  if (res != GST_FLOW_OK)
    goto pull_failed;
  else if (GST_BUFFER_SIZE (buf) < 8)
    goto too_small;

  /* check tag first before blindy trying to read 'size' bytes */
  tag = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf));
  size = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 4);
  if (tag == GST_RIFF_TAG_LIST) {
    /* this is the movi tag */
    GST_DEBUG_OBJECT (avi, "skip LIST chunk, size %" G_GUINT32_FORMAT,
        (8 + GST_ROUND_UP_2 (size)));
    offset += 8 + GST_ROUND_UP_2 (size);
    gst_buffer_unref (buf);
    res = gst_pad_pull_range (avi->sinkpad, offset, 8, &buf);
    if (res != GST_FLOW_OK)
      goto pull_failed;
    else if (GST_BUFFER_SIZE (buf) < 8)
      goto too_small;
    tag = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf));
    size = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 4);
  }

  if (tag != GST_RIFF_TAG_idx1)
    goto no_index;
  if (!size)
    goto zero_index;

  gst_buffer_unref (buf);

  GST_DEBUG ("index found at offset %" G_GUINT64_FORMAT, offset);

  /* read chunk, advance offset */
  if (gst_riff_read_chunk (GST_ELEMENT_CAST (avi),
          avi->sinkpad, &offset, &tag, &buf) != GST_FLOW_OK)
    return;

  GST_DEBUG ("will parse index chunk size %u for tag %"
      GST_FOURCC_FORMAT, GST_BUFFER_SIZE (buf), GST_FOURCC_ARGS (tag));

  gst_avi_demux_parse_index (avi, buf);

#ifndef GST_DISABLE_GST_DEBUG
  /* debug our indexes */
  {
    gint i;
    GstAviStream *stream;

    for (i = 0; i < avi->num_streams; i++) {
      stream = &avi->stream[i];
      GST_DEBUG_OBJECT (avi, "stream %u: %u frames, %" G_GINT64_FORMAT " bytes",
          i, stream->idx_n, stream->total_bytes);
    }
  }
#endif
  return;

  /* ERRORS */
pull_failed:
  {
    GST_DEBUG_OBJECT (avi,
        "pull range failed: pos=%" G_GUINT64_FORMAT " size=8", offset);
    return;
  }
too_small:
  {
    GST_DEBUG_OBJECT (avi, "Buffer is too small");
    gst_buffer_unref (buf);
    return;
  }
no_index:
  {
    GST_WARNING_OBJECT (avi,
        "No index data (idx1) after movi chunk, but %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (tag));
    gst_buffer_unref (buf);
    return;
  }
zero_index:
  {
    GST_WARNING_OBJECT (avi, "Empty index data (idx1) after movi chunk");
    gst_buffer_unref (buf);
    return;
  }
}

/*
 * gst_avi_demux_stream_index_push:
 * @avi: avi demuxer object.
 *
 * Read index.
 */
static void
gst_avi_demux_stream_index_push (GstAviDemux * avi)
{
  guint64 offset = avi->idx1_offset;
  GstBuffer *buf;
  guint32 tag;
  guint32 size;

  GST_DEBUG ("demux stream index at offset %" G_GUINT64_FORMAT, offset);

  /* get chunk information */
  if (!gst_avi_demux_peek_chunk (avi, &tag, &size))
    return;

  /* check tag first before blindly trying to read 'size' bytes */
  if (tag == GST_RIFF_TAG_LIST) {
    /* this is the movi tag */
    GST_DEBUG_OBJECT (avi, "skip LIST chunk, size %" G_GUINT32_FORMAT,
        (8 + GST_ROUND_UP_2 (size)));
    avi->idx1_offset = offset + 8 + GST_ROUND_UP_2 (size);
    /* issue seek to allow chain function to handle it and return! */
    perform_seek_to_offset (avi, avi->idx1_offset);
    return;
  }

  if (tag != GST_RIFF_TAG_idx1)
    goto no_index;

  GST_DEBUG ("index found at offset %" G_GUINT64_FORMAT, offset);

  /* flush chunk header */
  gst_adapter_flush (avi->adapter, 8);
  /* read chunk payload */
  buf = gst_adapter_take_buffer (avi->adapter, size);
  if (!buf)
    goto pull_failed;
  /* advance offset */
  offset += 8 + GST_ROUND_UP_2 (size);

  GST_DEBUG ("will parse index chunk size %u for tag %"
      GST_FOURCC_FORMAT, GST_BUFFER_SIZE (buf), GST_FOURCC_ARGS (tag));

  avi->offset = avi->first_movi_offset;
  gst_avi_demux_parse_index (avi, buf);

#ifndef GST_DISABLE_GST_DEBUG
  /* debug our indexes */
  {
    gint i;
    GstAviStream *stream;

    for (i = 0; i < avi->num_streams; i++) {
      stream = &avi->stream[i];
      GST_DEBUG_OBJECT (avi, "stream %u: %u frames, %" G_GINT64_FORMAT " bytes",
          i, stream->idx_n, stream->total_bytes);
    }
  }
#endif
  return;

  /* ERRORS */
pull_failed:
  {
    GST_DEBUG_OBJECT (avi,
        "taking data from adapter failed: pos=%" G_GUINT64_FORMAT " size=%u",
        offset, size);
    return;
  }
no_index:
  {
    GST_WARNING_OBJECT (avi,
        "No index data (idx1) after movi chunk, but %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (tag));
    return;
  }
}

/*
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
  guint bufsize;
  guint8 *bufdata;

  res = gst_pad_pull_range (avi->sinkpad, offset, 8, &buf);
  if (res != GST_FLOW_OK)
    goto pull_failed;

  bufsize = GST_BUFFER_SIZE (buf);
  if (bufsize != 8)
    goto wrong_size;

  bufdata = GST_BUFFER_DATA (buf);

  *tag = GST_READ_UINT32_LE (bufdata);
  *size = GST_READ_UINT32_LE (bufdata + 4);

  GST_LOG_OBJECT (avi, "Tag[%" GST_FOURCC_FORMAT "] (size:%d) %"
      G_GINT64_FORMAT " -- %" G_GINT64_FORMAT, GST_FOURCC_ARGS (*tag),
      *size, offset + 8, offset + 8 + (gint64) * size);

done:
  gst_buffer_unref (buf);

  return res;

  /* ERRORS */
pull_failed:
  {
    GST_DEBUG_OBJECT (avi, "pull_ranged returned %s", gst_flow_get_name (res));
    return res;
  }
wrong_size:
  {
    GST_DEBUG_OBJECT (avi, "got %d bytes which is <> 8 bytes", bufsize);
    res = GST_FLOW_ERROR;
    goto done;
  }
}

/*
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
  guint _size = 0;
  GstFlowReturn res;

  do {
    res = gst_avi_demux_peek_tag (avi, off, tag, &_size);
    if (res != GST_FLOW_OK)
      break;
    if (*tag == GST_RIFF_TAG_LIST || *tag == GST_RIFF_TAG_RIFF)
      off += 8 + 4;             /* skip tag + size + subtag */
    else {
      *offset = off + 8;
      *size = _size;
      break;
    }
  } while (TRUE);

  return res;
}

/*
 * gst_avi_demux_stream_scan:
 * @avi: calling element (used for debugging/errors).
 *
 * Scan the file for all chunks to "create" a new index.
 * pull-range based
 */
static gboolean
gst_avi_demux_stream_scan (GstAviDemux * avi)
{
  GstFlowReturn res;
  GstAviStream *stream;
  GstFormat format;
  guint64 pos = 0;
  guint64 length;
  gint64 tmplength;
  guint32 tag = 0;
  guint num;

  /* FIXME:
   * - implement non-seekable source support.
   */
  GST_DEBUG_OBJECT (avi, "Creating index");

  /* get the size of the file */
  format = GST_FORMAT_BYTES;
  if (!gst_pad_query_peer_duration (avi->sinkpad, &format, &tmplength))
    return FALSE;
  length = tmplength;

  /* guess the total amount of entries we expect */
  num = 16000;

  while (TRUE) {
    GstAviIndexEntry entry;
    guint size = 0;

    /* start reading data buffers to find the id and offset */
    res = gst_avi_demux_next_data_buffer (avi, &pos, &tag, &size);
    if (G_UNLIKELY (res != GST_FLOW_OK))
      break;

    /* get stream */
    stream = gst_avi_demux_stream_for_id (avi, tag);
    if (G_UNLIKELY (!stream))
      goto next;

    /* we can't figure out the keyframes, assume they all are */
    entry.flags = GST_AVI_KEYFRAME;
    entry.offset = pos;
    entry.size = size;

    /* and add to the index of this stream */
    if (G_UNLIKELY (!gst_avi_demux_add_index (avi, stream, num, &entry)))
      goto out_of_mem;

  next:
    /* update position */
    pos += GST_ROUND_UP_2 (size);
    if (G_UNLIKELY (pos > length)) {
      GST_WARNING_OBJECT (avi,
          "Stopping index lookup since we are further than EOF");
      break;
    }
  }

  /* collect stats */
  avi->have_index = gst_avi_demux_do_index_stats (avi);

  return TRUE;

  /* ERRORS */
out_of_mem:
  {
    GST_ELEMENT_ERROR (avi, RESOURCE, NO_SPACE_LEFT, (NULL),
        ("Cannot allocate memory for %u*%u=%u bytes",
            (guint) sizeof (GstAviIndexEntry), num,
            (guint) sizeof (GstAviIndexEntry) * num));
    return FALSE;
  }
}

static void
gst_avi_demux_calculate_durations_from_index (GstAviDemux * avi)
{
  guint i;
  GstClockTime total;
  GstAviStream *stream;

  total = GST_CLOCK_TIME_NONE;

  /* all streams start at a timestamp 0 */
  for (i = 0; i < avi->num_streams; i++) {
    GstClockTime duration, hduration;
    gst_riff_strh *strh;

    stream = &avi->stream[i];
    if (G_UNLIKELY (!stream || !stream->idx_n || !(strh = stream->strh)))
      continue;

    /* get header duration for the stream */
    hduration = stream->hdr_duration;
    /* index duration calculated during parsing */
    duration = stream->idx_duration;

    /* now pick a good duration */
    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      /* index gave valid duration, use that */
      GST_INFO ("Stream %p duration according to index: %" GST_TIME_FORMAT,
          stream, GST_TIME_ARGS (duration));
    } else {
      /* fall back to header info to calculate a duration */
      duration = hduration;
    }
    GST_INFO ("Setting duration of stream #%d to %" GST_TIME_FORMAT,
        i, GST_TIME_ARGS (duration));
    /* set duration for the stream */
    stream->duration = duration;

    /* find total duration */
    if (total == GST_CLOCK_TIME_NONE ||
        (GST_CLOCK_TIME_IS_VALID (duration) && duration > total))
      total = duration;
  }

  if (GST_CLOCK_TIME_IS_VALID (total) && (total > 0)) {
    /* now update the duration for those streams where we had none */
    for (i = 0; i < avi->num_streams; i++) {
      stream = &avi->stream[i];

      if (!GST_CLOCK_TIME_IS_VALID (stream->duration)
          || stream->duration == 0) {
        stream->duration = total;

        GST_INFO ("Stream %p duration according to total: %" GST_TIME_FORMAT,
            stream, GST_TIME_ARGS (total));
      }
    }
  }

  /* and set the total duration in the segment. */
  GST_INFO ("Setting total duration to: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (total));

  gst_segment_set_duration (&avi->segment, GST_FORMAT_TIME, total);
}

/* returns FALSE if there are no pads to deliver event to,
 * otherwise TRUE (whatever the outcome of event sending),
 * takes ownership of the event. */
static gboolean
gst_avi_demux_push_event (GstAviDemux * avi, GstEvent * event)
{
  gboolean result = FALSE;
  gint i;

  GST_DEBUG_OBJECT (avi, "sending %s event to %d streams",
      GST_EVENT_TYPE_NAME (event), avi->num_streams);

  for (i = 0; i < avi->num_streams; i++) {
    GstAviStream *stream = &avi->stream[i];

    if (stream->pad) {
      result = TRUE;
      gst_pad_push_event (stream->pad, gst_event_ref (event));
    }
  }
  gst_event_unref (event);
  return result;
}

static void
gst_avi_demux_check_seekability (GstAviDemux * avi)
{
  GstQuery *query;
  gboolean seekable = FALSE;
  gint64 start = -1, stop = -1;

  query = gst_query_new_seeking (GST_FORMAT_BYTES);
  if (!gst_pad_peer_query (avi->sinkpad, query)) {
    GST_DEBUG_OBJECT (avi, "seeking query failed");
    goto done;
  }

  gst_query_parse_seeking (query, NULL, &seekable, &start, &stop);

  /* try harder to query upstream size if we didn't get it the first time */
  if (seekable && stop == -1) {
    GstFormat fmt = GST_FORMAT_BYTES;

    GST_DEBUG_OBJECT (avi, "doing duration query to fix up unset stop");
    gst_pad_query_peer_duration (avi->sinkpad, &fmt, &stop);
  }

  /* if upstream doesn't know the size, it's likely that it's not seekable in
   * practice even if it technically may be seekable */
  if (seekable && (start != 0 || stop <= start)) {
    GST_DEBUG_OBJECT (avi, "seekable but unknown start/stop -> disable");
    seekable = FALSE;
  }

done:
  GST_INFO_OBJECT (avi, "seekable: %d (%" G_GUINT64_FORMAT " - %"
      G_GUINT64_FORMAT ")", seekable, start, stop);
  avi->seekable = seekable;

  gst_query_unref (query);
}

/*
 * Read AVI headers when streaming
 */
static GstFlowReturn
gst_avi_demux_stream_header_push (GstAviDemux * avi)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 tag = 0;
  guint32 ltag = 0;
  guint32 size = 0;
  const guint8 *data;
  GstBuffer *buf = NULL, *sub = NULL;
  guint offset = 4;
  gint64 stop;
  gint i;
  GstTagList *tags = NULL;

  GST_DEBUG ("Reading and parsing avi headers: %d", avi->header_state);

  switch (avi->header_state) {
    case GST_AVI_DEMUX_HEADER_TAG_LIST:
      if (gst_avi_demux_peek_chunk (avi, &tag, &size)) {
        avi->offset += 8 + GST_ROUND_UP_2 (size);
        if (tag != GST_RIFF_TAG_LIST)
          goto header_no_list;

        gst_adapter_flush (avi->adapter, 8);
        /* Find the 'hdrl' LIST tag */
        GST_DEBUG ("Reading %d bytes", size);
        buf = gst_adapter_take_buffer (avi->adapter, size);

        if (GST_READ_UINT32_LE (GST_BUFFER_DATA (buf)) != GST_RIFF_LIST_hdrl)
          goto header_no_hdrl;

        /* mind padding */
        if (size & 1)
          gst_adapter_flush (avi->adapter, 1);

        GST_DEBUG ("'hdrl' LIST tag found. Parsing next chunk");

        gst_avi_demux_roundup_list (avi, &buf);

        /* the hdrl starts with a 'avih' header */
        if (!gst_riff_parse_chunk (GST_ELEMENT_CAST (avi), buf, &offset, &tag,
                &sub))
          goto header_no_avih;

        if (tag != GST_RIFF_TAG_avih)
          goto header_no_avih;

        if (!gst_avi_demux_parse_avih (avi, sub, &avi->avih))
          goto header_wrong_avih;

        GST_DEBUG_OBJECT (avi, "AVI header ok, reading elemnts from header");

        /* now, read the elements from the header until the end */
        while (gst_riff_parse_chunk (GST_ELEMENT_CAST (avi), buf, &offset, &tag,
                &sub)) {
          /* sub can be NULL on empty tags */
          if (!sub)
            continue;

          switch (tag) {
            case GST_RIFF_TAG_LIST:
              if (GST_BUFFER_SIZE (sub) < 4)
                goto next;

              switch (GST_READ_UINT32_LE (GST_BUFFER_DATA (sub))) {
                case GST_RIFF_LIST_strl:
                  if (!(gst_avi_demux_parse_stream (avi, sub))) {
                    sub = NULL;
                    GST_ELEMENT_WARNING (avi, STREAM, DEMUX, (NULL),
                        ("failed to parse stream, ignoring"));
                    goto next;
                  }
                  sub = NULL;
                  goto next;
                case GST_RIFF_LIST_odml:
                  gst_avi_demux_parse_odml (avi, sub);
                  sub = NULL;
                  break;
                default:
                  GST_WARNING_OBJECT (avi,
                      "Unknown list %" GST_FOURCC_FORMAT " in AVI header",
                      GST_FOURCC_ARGS (GST_READ_UINT32_LE (GST_BUFFER_DATA
                              (sub))));
                  /* fall-through */
                case GST_RIFF_TAG_JUNQ:
                case GST_RIFF_TAG_JUNK:
                  goto next;
              }
              break;
            case GST_RIFF_IDIT:
              gst_avi_demux_parse_idit (avi, sub);
              goto next;
            default:
              GST_WARNING_OBJECT (avi,
                  "Unknown off %d tag %" GST_FOURCC_FORMAT " in AVI header",
                  offset, GST_FOURCC_ARGS (tag));
              /* fall-through */
            case GST_RIFF_TAG_JUNQ:
            case GST_RIFF_TAG_JUNK:
            next:
              /* move to next chunk */
              if (sub)
                gst_buffer_unref (sub);
              sub = NULL;
              break;
          }
        }
        gst_buffer_unref (buf);
        GST_DEBUG ("elements parsed");

        /* check parsed streams */
        if (avi->num_streams == 0) {
          goto no_streams;
        } else if (avi->num_streams != avi->avih->streams) {
          GST_WARNING_OBJECT (avi,
              "Stream header mentioned %d streams, but %d available",
              avi->avih->streams, avi->num_streams);
        }
        GST_DEBUG ("Get junk and info next");
        avi->header_state = GST_AVI_DEMUX_HEADER_INFO;
      } else {
        /* Need more data */
        return ret;
      }
      /* fall-though */
    case GST_AVI_DEMUX_HEADER_INFO:
      GST_DEBUG_OBJECT (avi, "skipping junk between header and data ...");
      while (TRUE) {
        if (gst_adapter_available (avi->adapter) < 12)
          return GST_FLOW_OK;

        data = gst_adapter_peek (avi->adapter, 12);
        tag = GST_READ_UINT32_LE (data);
        size = GST_READ_UINT32_LE (data + 4);
        ltag = GST_READ_UINT32_LE (data + 8);

        if (tag == GST_RIFF_TAG_LIST) {
          switch (ltag) {
            case GST_RIFF_LIST_movi:
              gst_adapter_flush (avi->adapter, 12);
              if (!avi->first_movi_offset)
                avi->first_movi_offset = avi->offset;
              avi->offset += 12;
              avi->idx1_offset = avi->offset + size - 4;
              goto skipping_done;
            case GST_RIFF_LIST_INFO:
              GST_DEBUG ("Found INFO chunk");
              if (gst_avi_demux_peek_chunk (avi, &tag, &size)) {
                GST_DEBUG ("got size %d", size);
                avi->offset += 12;
                gst_adapter_flush (avi->adapter, 12);
                if (size > 4) {
                  buf = gst_adapter_take_buffer (avi->adapter, size - 4);
                  /* mind padding */
                  if (size & 1)
                    gst_adapter_flush (avi->adapter, 1);
                  gst_riff_parse_info (GST_ELEMENT_CAST (avi), buf, &tags);
                  if (tags) {
                    if (avi->globaltags) {
                      gst_tag_list_insert (avi->globaltags, tags,
                          GST_TAG_MERGE_REPLACE);
                    } else {
                      avi->globaltags = tags;
                    }
                  }
                  tags = NULL;
                  gst_buffer_unref (buf);

                  avi->offset += GST_ROUND_UP_2 (size) - 4;
                } else {
                  GST_DEBUG ("skipping INFO LIST prefix");
                }
              } else {
                /* Need more data */
                return GST_FLOW_OK;
              }
              break;
            default:
              if (gst_avi_demux_peek_chunk (avi, &tag, &size)) {
                avi->offset += 8 + GST_ROUND_UP_2 (size);
                gst_adapter_flush (avi->adapter, 8 + GST_ROUND_UP_2 (size));
                // ??? goto iterate; ???
              } else {
                /* Need more data */
                return GST_FLOW_OK;
              }
              break;
          }
        } else {
          if (gst_avi_demux_peek_chunk (avi, &tag, &size)) {
            avi->offset += 8 + GST_ROUND_UP_2 (size);
            gst_adapter_flush (avi->adapter, 8 + GST_ROUND_UP_2 (size));
            //goto iterate;
          } else {
            /* Need more data */
            return GST_FLOW_OK;
          }
        }
      }
      break;
    default:
      GST_WARNING ("unhandled header state: %d", avi->header_state);
      break;
  }
skipping_done:

  GST_DEBUG_OBJECT (avi, "skipping done ... (streams=%u, stream[0].indexes=%p)",
      avi->num_streams, avi->stream[0].indexes);

  GST_DEBUG ("Found movi chunk. Starting to stream data");
  avi->state = GST_AVI_DEMUX_MOVI;

  /* no indexes in push mode, but it still sets some variables */
  gst_avi_demux_calculate_durations_from_index (avi);

  gst_avi_demux_expose_streams (avi, TRUE);

  /* prepare all streams for index 0 */
  for (i = 0; i < avi->num_streams; i++)
    avi->stream[i].current_entry = 0;

  /* create initial NEWSEGMENT event */
  if ((stop = avi->segment.stop) == GST_CLOCK_TIME_NONE)
    stop = avi->segment.duration;

  GST_DEBUG_OBJECT (avi, "segment stop %" G_GINT64_FORMAT, stop);

  if (avi->seg_event)
    gst_event_unref (avi->seg_event);
  avi->seg_event = gst_event_new_new_segment_full
      (FALSE, avi->segment.rate, avi->segment.applied_rate, GST_FORMAT_TIME,
      avi->segment.start, stop, avi->segment.time);

  gst_avi_demux_check_seekability (avi);

  /* at this point we know all the streams and we can signal the no more
   * pads signal */
  GST_DEBUG_OBJECT (avi, "signaling no more pads");
  gst_element_no_more_pads (GST_ELEMENT_CAST (avi));

  return GST_FLOW_OK;

  /* ERRORS */
no_streams:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL), ("No streams found"));
    return GST_FLOW_ERROR;
  }
header_no_list:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("Invalid AVI header (no LIST at start): %"
            GST_FOURCC_FORMAT, GST_FOURCC_ARGS (tag)));
    return GST_FLOW_ERROR;
  }
header_no_hdrl:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("Invalid AVI header (no hdrl at start): %"
            GST_FOURCC_FORMAT, GST_FOURCC_ARGS (tag)));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
header_no_avih:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("Invalid AVI header (no avih at start): %"
            GST_FOURCC_FORMAT, GST_FOURCC_ARGS (tag)));
    if (sub)
      gst_buffer_unref (sub);

    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
header_wrong_avih:
  {
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static void
gst_avi_demux_add_date_tag (GstAviDemux * avi, gint y, gint m, gint d,
    gint h, gint min, gint s)
{
  GDate *date;
  GstDateTime *dt;

  date = g_date_new_dmy (d, m, y);
  if (!g_date_valid (date)) {
    /* bogus date */
    GST_WARNING_OBJECT (avi, "Refusing to add invalid date %d-%d-%d", y, m, d);
    g_date_free (date);
    return;
  }

  dt = gst_date_time_new_local_time (y, m, d, h, min, s);

  if (avi->globaltags == NULL)
    avi->globaltags = gst_tag_list_new ();

  gst_tag_list_add (avi->globaltags, GST_TAG_MERGE_REPLACE, GST_TAG_DATE, date,
      NULL);
  g_date_free (date);
  if (dt) {
    gst_tag_list_add (avi->globaltags, GST_TAG_MERGE_REPLACE, GST_TAG_DATE_TIME,
        dt, NULL);
    gst_date_time_unref (dt);
  }
}

static void
gst_avi_demux_parse_idit_nums_only (GstAviDemux * avi, gchar * data)
{
  gint y, m, d;
  gint hr = 0, min = 0, sec = 0;
  gint ret;

  GST_DEBUG ("data : '%s'", data);

  ret = sscanf (data, "%d:%d:%d %d:%d:%d", &y, &m, &d, &hr, &min, &sec);
  if (ret < 3) {
    /* Attempt YYYY/MM/DD/ HH:MM variant (found in CASIO cameras) */
    ret = sscanf (data, "%04d/%02d/%02d/ %d:%d", &y, &m, &d, &hr, &min);
    if (ret < 3) {
      GST_WARNING_OBJECT (avi, "Failed to parse IDIT tag");
      return;
    }
  }
  gst_avi_demux_add_date_tag (avi, y, m, d, hr, min, sec);
}

static gint
get_month_num (gchar * data, guint size)
{
  if (g_ascii_strncasecmp (data, "jan", 3) == 0) {
    return 1;
  } else if (g_ascii_strncasecmp (data, "feb", 3) == 0) {
    return 2;
  } else if (g_ascii_strncasecmp (data, "mar", 3) == 0) {
    return 3;
  } else if (g_ascii_strncasecmp (data, "apr", 3) == 0) {
    return 4;
  } else if (g_ascii_strncasecmp (data, "may", 3) == 0) {
    return 5;
  } else if (g_ascii_strncasecmp (data, "jun", 3) == 0) {
    return 6;
  } else if (g_ascii_strncasecmp (data, "jul", 3) == 0) {
    return 7;
  } else if (g_ascii_strncasecmp (data, "aug", 3) == 0) {
    return 8;
  } else if (g_ascii_strncasecmp (data, "sep", 3) == 0) {
    return 9;
  } else if (g_ascii_strncasecmp (data, "oct", 3) == 0) {
    return 10;
  } else if (g_ascii_strncasecmp (data, "nov", 3) == 0) {
    return 11;
  } else if (g_ascii_strncasecmp (data, "dec", 3) == 0) {
    return 12;
  }

  return 0;
}

static void
gst_avi_demux_parse_idit_text (GstAviDemux * avi, gchar * data)
{
  gint year, month, day;
  gint hour, min, sec;
  gint ret;
  gchar weekday[4];
  gchar monthstr[4];

  ret = sscanf (data, "%3s %3s %d %d:%d:%d %d", weekday, monthstr, &day, &hour,
      &min, &sec, &year);
  if (ret != 7) {
    GST_WARNING_OBJECT (avi, "Failed to parse IDIT tag");
    return;
  }
  month = get_month_num (monthstr, strlen (monthstr));
  gst_avi_demux_add_date_tag (avi, year, month, day, hour, min, sec);
}

static void
gst_avi_demux_parse_idit (GstAviDemux * avi, GstBuffer * buf)
{
  gchar *data = (gchar *) GST_BUFFER_DATA (buf);
  guint size = GST_BUFFER_SIZE (buf);
  gchar *safedata = NULL;

  /*
   * According to:
   * http://www.eden-foundation.org/products/code/film_date_stamp/index.html
   *
   * This tag could be in one of the below formats
   * 2005:08:17 11:42:43
   * THU OCT 26 16:46:04 2006
   * Mon Mar  3 09:44:56 2008
   *
   * FIXME: Our date tag doesn't include hours
   */

  /* skip eventual initial whitespace */
  while (size > 0 && g_ascii_isspace (data[0])) {
    data++;
    size--;
  }

  if (size == 0) {
    goto non_parsable;
  }

  /* make a safe copy to add a \0 to the end of the string */
  safedata = g_strndup (data, size);

  /* test if the first char is a alpha or a number */
  if (g_ascii_isdigit (data[0])) {
    gst_avi_demux_parse_idit_nums_only (avi, safedata);
    g_free (safedata);
    return;
  } else if (g_ascii_isalpha (data[0])) {
    gst_avi_demux_parse_idit_text (avi, safedata);
    g_free (safedata);
    return;
  }

  g_free (safedata);

non_parsable:
  GST_WARNING_OBJECT (avi, "IDIT tag has no parsable info");
}

/*
 * Read full AVI headers.
 */
static GstFlowReturn
gst_avi_demux_stream_header_pull (GstAviDemux * avi)
{
  GstFlowReturn res;
  GstBuffer *buf, *sub = NULL;
  guint32 tag;
  guint offset = 4;
  gint64 stop;
  GstElement *element = GST_ELEMENT_CAST (avi);
  GstClockTime stamp;
  GstTagList *tags = NULL;

  stamp = gst_util_get_timestamp ();

  /* the header consists of a 'hdrl' LIST tag */
  res = gst_riff_read_chunk (element, avi->sinkpad, &avi->offset, &tag, &buf);
  if (res != GST_FLOW_OK)
    goto pull_range_failed;
  else if (tag != GST_RIFF_TAG_LIST)
    goto no_list;
  else if (GST_BUFFER_SIZE (buf) < 4)
    goto no_header;

  GST_DEBUG_OBJECT (avi, "parsing headers");

  /* Find the 'hdrl' LIST tag */
  while (GST_READ_UINT32_LE (GST_BUFFER_DATA (buf)) != GST_RIFF_LIST_hdrl) {
    GST_LOG_OBJECT (avi, "buffer contains %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (GST_READ_UINT32_LE (GST_BUFFER_DATA (buf))));

    /* Eat up */
    gst_buffer_unref (buf);

    /* read new chunk */
    res = gst_riff_read_chunk (element, avi->sinkpad, &avi->offset, &tag, &buf);
    if (res != GST_FLOW_OK)
      goto pull_range_failed;
    else if (tag != GST_RIFF_TAG_LIST)
      goto no_list;
    else if (GST_BUFFER_SIZE (buf) < 4)
      goto no_header;
  }

  GST_DEBUG_OBJECT (avi, "hdrl LIST tag found");

  gst_avi_demux_roundup_list (avi, &buf);

  /* the hdrl starts with a 'avih' header */
  if (!gst_riff_parse_chunk (element, buf, &offset, &tag, &sub))
    goto no_avih;
  else if (tag != GST_RIFF_TAG_avih)
    goto no_avih;
  else if (!gst_avi_demux_parse_avih (avi, sub, &avi->avih))
    goto invalid_avih;

  GST_DEBUG_OBJECT (avi, "AVI header ok, reading elements from header");

  /* now, read the elements from the header until the end */
  while (gst_riff_parse_chunk (element, buf, &offset, &tag, &sub)) {
    /* sub can be NULL on empty tags */
    if (!sub)
      continue;

    switch (tag) {
      case GST_RIFF_TAG_LIST:
      {
        guint8 *data;
        guint32 fourcc;

        if (GST_BUFFER_SIZE (sub) < 4)
          goto next;

        data = GST_BUFFER_DATA (sub);
        fourcc = GST_READ_UINT32_LE (data);

        switch (fourcc) {
          case GST_RIFF_LIST_strl:
            if (!(gst_avi_demux_parse_stream (avi, sub))) {
              GST_ELEMENT_WARNING (avi, STREAM, DEMUX, (NULL),
                  ("failed to parse stream, ignoring"));
              sub = NULL;
            }
            sub = NULL;
            goto next;
          case GST_RIFF_LIST_odml:
            gst_avi_demux_parse_odml (avi, sub);
            sub = NULL;
            break;
          case GST_RIFF_LIST_INFO:
            GST_BUFFER_DATA (sub) = data + 4;
            GST_BUFFER_SIZE (sub) -= 4;
            gst_riff_parse_info (element, sub, &tags);
            if (tags) {
              if (avi->globaltags) {
                gst_tag_list_insert (avi->globaltags, tags,
                    GST_TAG_MERGE_REPLACE);
              } else {
                avi->globaltags = tags;
              }
            }
            tags = NULL;
            break;
          default:
            GST_WARNING_OBJECT (avi,
                "Unknown list %" GST_FOURCC_FORMAT " in AVI header",
                GST_FOURCC_ARGS (fourcc));
            GST_MEMDUMP_OBJECT (avi, "Unknown list", GST_BUFFER_DATA (sub),
                GST_BUFFER_SIZE (sub));
            /* fall-through */
          case GST_RIFF_TAG_JUNQ:
          case GST_RIFF_TAG_JUNK:
            goto next;
        }
        break;
      }
      case GST_RIFF_IDIT:
        gst_avi_demux_parse_idit (avi, sub);
        goto next;
      default:
        GST_WARNING_OBJECT (avi,
            "Unknown tag %" GST_FOURCC_FORMAT " in AVI header at off %d",
            GST_FOURCC_ARGS (tag), offset);
        GST_MEMDUMP_OBJECT (avi, "Unknown tag", GST_BUFFER_DATA (sub),
            GST_BUFFER_SIZE (sub));
        /* fall-through */
      case GST_RIFF_TAG_JUNQ:
      case GST_RIFF_TAG_JUNK:
      next:
        if (sub)
          gst_buffer_unref (sub);
        sub = NULL;
        break;
    }
  }
  gst_buffer_unref (buf);
  GST_DEBUG ("elements parsed");

  /* check parsed streams */
  if (avi->num_streams == 0)
    goto no_streams;
  else if (avi->num_streams != avi->avih->streams) {
    GST_WARNING_OBJECT (avi,
        "Stream header mentioned %d streams, but %d available",
        avi->avih->streams, avi->num_streams);
  }

  GST_DEBUG_OBJECT (avi, "skipping junk between header and data, offset=%"
      G_GUINT64_FORMAT, avi->offset);

  /* Now, find the data (i.e. skip all junk between header and data) */
  do {
    guint size;
    guint8 *data;
    guint32 tag, ltag;

    res = gst_pad_pull_range (avi->sinkpad, avi->offset, 12, &buf);
    if (res != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (avi, "pull_range failure while looking for tags");
      goto pull_range_failed;
    } else if (GST_BUFFER_SIZE (buf) < 12) {
      GST_DEBUG_OBJECT (avi, "got %d bytes which is less than 12 bytes",
          GST_BUFFER_SIZE (buf));
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }

    data = GST_BUFFER_DATA (buf);

    tag = GST_READ_UINT32_LE (data);
    size = GST_READ_UINT32_LE (data + 4);
    ltag = GST_READ_UINT32_LE (data + 8);

    GST_DEBUG ("tag %" GST_FOURCC_FORMAT ", size %u",
        GST_FOURCC_ARGS (tag), size);
    GST_MEMDUMP ("Tag content", data, GST_BUFFER_SIZE (buf));
    gst_buffer_unref (buf);

    switch (tag) {
      case GST_RIFF_TAG_LIST:{
        switch (ltag) {
          case GST_RIFF_LIST_movi:
            GST_DEBUG_OBJECT (avi,
                "Reached the 'movi' tag, we're done with skipping");
            goto skipping_done;
          case GST_RIFF_LIST_INFO:
            res =
                gst_riff_read_chunk (element, avi->sinkpad, &avi->offset, &tag,
                &buf);
            if (res != GST_FLOW_OK) {
              GST_DEBUG_OBJECT (avi, "couldn't read INFO chunk");
              goto pull_range_failed;
            }
            GST_DEBUG ("got size %u", GST_BUFFER_SIZE (buf));
            if (size < 4) {
              GST_DEBUG ("skipping INFO LIST prefix");
              avi->offset += (4 - GST_ROUND_UP_2 (size));
              gst_buffer_unref (buf);
              continue;
            }

            sub = gst_buffer_create_sub (buf, 4, GST_BUFFER_SIZE (buf) - 4);
            gst_riff_parse_info (element, sub, &tags);
            if (tags) {
              if (avi->globaltags) {
                gst_tag_list_insert (avi->globaltags, tags,
                    GST_TAG_MERGE_REPLACE);
              } else {
                avi->globaltags = tags;
              }
            }
            tags = NULL;
            if (sub) {
              gst_buffer_unref (sub);
              sub = NULL;
            }
            gst_buffer_unref (buf);
            /* gst_riff_read_chunk() has already advanced avi->offset */
            break;
          default:
            GST_WARNING_OBJECT (avi,
                "Skipping unknown list tag %" GST_FOURCC_FORMAT,
                GST_FOURCC_ARGS (ltag));
            avi->offset += 8 + GST_ROUND_UP_2 (size);
            break;
        }
      }
        break;
      default:
        GST_WARNING_OBJECT (avi, "Skipping unknown tag %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (tag));
        /* Fall-through */
      case GST_MAKE_FOURCC ('J', 'U', 'N', 'Q'):
      case GST_MAKE_FOURCC ('J', 'U', 'N', 'K'):
        /* Only get buffer for debugging if the memdump is needed  */
        if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= 9) {
          res = gst_pad_pull_range (avi->sinkpad, avi->offset, size, &buf);
          if (res != GST_FLOW_OK) {
            GST_DEBUG_OBJECT (avi, "couldn't read INFO chunk");
            goto pull_range_failed;
          }
          GST_MEMDUMP ("Junk", GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
          gst_buffer_unref (buf);
        }
        avi->offset += 8 + GST_ROUND_UP_2 (size);
        break;
    }
  } while (1);
skipping_done:

  GST_DEBUG_OBJECT (avi, "skipping done ... (streams=%u, stream[0].indexes=%p)",
      avi->num_streams, avi->stream[0].indexes);

  /* create or read stream index (for seeking) */
  if (avi->stream[0].indexes != NULL) {
    /* we read a super index already (gst_avi_demux_parse_superindex() ) */
    gst_avi_demux_read_subindexes_pull (avi);
  }
  if (!avi->have_index) {
    if (avi->avih->flags & GST_RIFF_AVIH_HASINDEX)
      gst_avi_demux_stream_index (avi);

    /* still no index, scan */
    if (!avi->have_index) {
      gst_avi_demux_stream_scan (avi);

      /* still no index.. this is a fatal error for now.
       * FIXME, we should switch to plain push mode without seeking
       * instead of failing. */
      if (!avi->have_index)
        goto no_index;
    }
  }
  /* use the indexes now to construct nice durations */
  gst_avi_demux_calculate_durations_from_index (avi);

  gst_avi_demux_expose_streams (avi, FALSE);

  /* create initial NEWSEGMENT event */
  if ((stop = avi->segment.stop) == GST_CLOCK_TIME_NONE)
    stop = avi->segment.duration;

  GST_DEBUG_OBJECT (avi, "segment stop %" G_GINT64_FORMAT, stop);

  /* do initial seek to the default segment values */
  gst_avi_demux_do_seek (avi, &avi->segment);

  /* prepare initial segment */
  if (avi->seg_event)
    gst_event_unref (avi->seg_event);
  avi->seg_event = gst_event_new_new_segment_full
      (FALSE, avi->segment.rate, avi->segment.applied_rate, GST_FORMAT_TIME,
      avi->segment.start, stop, avi->segment.time);

  stamp = gst_util_get_timestamp () - stamp;
  GST_DEBUG_OBJECT (avi, "pulling header took %" GST_TIME_FORMAT,
      GST_TIME_ARGS (stamp));

  /* at this point we know all the streams and we can signal the no more
   * pads signal */
  GST_DEBUG_OBJECT (avi, "signaling no more pads");
  gst_element_no_more_pads (GST_ELEMENT_CAST (avi));

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
    if (sub)
      gst_buffer_unref (sub);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
invalid_avih:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("Invalid AVI header (cannot parse avih at start)"));
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
    GST_WARNING ("file without or too big index");
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("Could not get/create index"));
    return GST_FLOW_ERROR;
  }
pull_range_failed:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL),
        ("pull_range flow reading header: %s", gst_flow_get_name (res)));
    return GST_FLOW_ERROR;
  }
}

/* move a stream to @index */
static void
gst_avi_demux_move_stream (GstAviDemux * avi, GstAviStream * stream,
    GstSegment * segment, guint index)
{
  GST_DEBUG_OBJECT (avi, "Move stream %d to %u", stream->num, index);

  if (segment->rate < 0.0) {
    guint next_key;
    /* Because we don't know the frame order we need to push from the prev keyframe
     * to the next keyframe. If there is a smart decoder downstream he will notice
     * that there are too many encoded frames send and return UNEXPECTED when there
     * are enough decoded frames to fill the segment. */
    next_key = gst_avi_demux_index_next (avi, stream, index, TRUE);

    /* FIXME, we go back to 0, we should look at segment.start. We will however
     * stop earlier when the see the timestamp < segment.start */
    stream->start_entry = 0;
    stream->step_entry = index;
    stream->current_entry = index;
    stream->stop_entry = next_key;

    GST_DEBUG_OBJECT (avi, "reverse seek: start %u, step %u, stop %u",
        stream->start_entry, stream->step_entry, stream->stop_entry);
  } else {
    stream->start_entry = index;
    stream->step_entry = index;
    stream->stop_entry = gst_avi_demux_index_last (avi, stream);
  }
  if (stream->current_entry != index) {
    GST_DEBUG_OBJECT (avi, "Move DISCONT from %u to %u",
        stream->current_entry, index);
    stream->current_entry = index;
    stream->discont = TRUE;
  }

  /* update the buffer info */
  gst_avi_demux_get_buffer_info (avi, stream, index,
      &stream->current_timestamp, &stream->current_ts_end,
      &stream->current_offset, &stream->current_offset_end);

  GST_DEBUG_OBJECT (avi, "Moved to %u, ts %" GST_TIME_FORMAT
      ", ts_end %" GST_TIME_FORMAT ", off %" G_GUINT64_FORMAT
      ", off_end %" G_GUINT64_FORMAT, index,
      GST_TIME_ARGS (stream->current_timestamp),
      GST_TIME_ARGS (stream->current_ts_end), stream->current_offset,
      stream->current_offset_end);

  GST_DEBUG_OBJECT (avi, "Seeking to offset %" G_GUINT64_FORMAT,
      stream->index[index].offset);
}

/*
 * Do the actual seeking.
 */
static gboolean
gst_avi_demux_do_seek (GstAviDemux * avi, GstSegment * segment)
{
  GstClockTime seek_time;
  gboolean keyframe;
  guint i, index;
  GstAviStream *stream;

  seek_time = segment->last_stop;
  keyframe = !!(segment->flags & GST_SEEK_FLAG_KEY_UNIT);

  GST_DEBUG_OBJECT (avi, "seek to: %" GST_TIME_FORMAT
      " keyframe seeking:%d", GST_TIME_ARGS (seek_time), keyframe);

  /* FIXME, this code assumes the main stream with keyframes is stream 0,
   * which is mostly correct... */
  stream = &avi->stream[avi->main_stream];

  /* get the entry index for the requested position */
  index = gst_avi_demux_index_for_time (avi, stream, seek_time);
  GST_DEBUG_OBJECT (avi, "Got entry %u", index);

  /* check if we are already on a keyframe */
  if (!ENTRY_IS_KEYFRAME (&stream->index[index])) {
    GST_DEBUG_OBJECT (avi, "not keyframe, searching back");
    /* now go to the previous keyframe, this is where we should start
     * decoding from. */
    index = gst_avi_demux_index_prev (avi, stream, index, TRUE);
    GST_DEBUG_OBJECT (avi, "previous keyframe at %u", index);
  }

  /* move the main stream to this position */
  gst_avi_demux_move_stream (avi, stream, segment, index);

  if (keyframe) {
    /* when seeking to a keyframe, we update the result seek time
     * to the time of the keyframe. */
    seek_time = stream->current_timestamp;
    GST_DEBUG_OBJECT (avi, "keyframe adjusted to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (seek_time));
  }

  /* the seek time is also the last_stop and stream time when going
   * forwards */
  segment->last_stop = seek_time;
  if (segment->rate > 0.0)
    segment->time = seek_time;

  /* now set DISCONT and align the other streams */
  for (i = 0; i < avi->num_streams; i++) {
    GstAviStream *ostream;

    ostream = &avi->stream[i];
    if ((ostream == stream) || (ostream->index == NULL))
      continue;

    /* get the entry index for the requested position */
    index = gst_avi_demux_index_for_time (avi, ostream, seek_time);

    /* move to previous keyframe */
    if (!ENTRY_IS_KEYFRAME (&ostream->index[index]))
      index = gst_avi_demux_index_prev (avi, ostream, index, TRUE);

    gst_avi_demux_move_stream (avi, ostream, segment, index);
  }
  GST_DEBUG_OBJECT (avi, "done seek to: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (seek_time));

  return TRUE;
}

/*
 * Handle seek event in pull mode.
 */
static gboolean
gst_avi_demux_handle_seek (GstAviDemux * avi, GstPad * pad, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type = GST_SEEK_TYPE_NONE, stop_type;
  gint64 cur, stop;
  gboolean flush;
  gboolean update;
  GstSegment seeksegment = { 0, };
  gint i;

  if (event) {
    GST_DEBUG_OBJECT (avi, "doing seek with event");

    gst_event_parse_seek (event, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    /* we have to have a format as the segment format. Try to convert
     * if not. */
    if (format != GST_FORMAT_TIME) {
      GstFormat fmt = GST_FORMAT_TIME;
      gboolean res = TRUE;

      if (cur_type != GST_SEEK_TYPE_NONE)
        res = gst_pad_query_convert (pad, format, cur, &fmt, &cur);
      if (res && stop_type != GST_SEEK_TYPE_NONE)
        res = gst_pad_query_convert (pad, format, stop, &fmt, &stop);
      if (!res)
        goto no_format;

      format = fmt;
    }
    GST_DEBUG_OBJECT (avi,
        "seek requested: rate %g cur %" GST_TIME_FORMAT " stop %"
        GST_TIME_FORMAT, rate, GST_TIME_ARGS (cur), GST_TIME_ARGS (stop));
    /* FIXME: can we do anything with rate!=1.0 */
  } else {
    GST_DEBUG_OBJECT (avi, "doing seek without event");
    flags = 0;
    rate = 1.0;
  }

  /* save flush flag */
  flush = flags & GST_SEEK_FLAG_FLUSH;

  if (flush) {
    GstEvent *fevent = gst_event_new_flush_start ();

    /* for a flushing seek, we send a flush_start on all pads. This will
     * eventually stop streaming with a WRONG_STATE. We can thus eventually
     * take the STREAM_LOCK. */
    GST_DEBUG_OBJECT (avi, "sending flush start");
    gst_avi_demux_push_event (avi, gst_event_ref (fevent));
    gst_pad_push_event (avi->sinkpad, fevent);
  } else {
    /* a non-flushing seek, we PAUSE the task so that we can take the
     * STREAM_LOCK */
    GST_DEBUG_OBJECT (avi, "non flushing seek, pausing task");
    gst_pad_pause_task (avi->sinkpad);
  }

  /* wait for streaming to stop */
  GST_DEBUG_OBJECT (avi, "wait for streaming to stop");
  GST_PAD_STREAM_LOCK (avi->sinkpad);

  /* copy segment, we need this because we still need the old
   * segment when we close the current segment. */
  memcpy (&seeksegment, &avi->segment, sizeof (GstSegment));

  if (event) {
    GST_DEBUG_OBJECT (avi, "configuring seek");
    gst_segment_set_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }
  /* do the seek, seeksegment.last_stop contains the new position, this
   * actually never fails. */
  gst_avi_demux_do_seek (avi, &seeksegment);

  gst_event_replace (&avi->close_seg_event, NULL);
  if (flush) {
    GstEvent *fevent = gst_event_new_flush_stop ();

    GST_DEBUG_OBJECT (avi, "sending flush stop");
    gst_avi_demux_push_event (avi, gst_event_ref (fevent));
    gst_pad_push_event (avi->sinkpad, fevent);
  } else if (avi->segment_running) {
    /* we are running the current segment and doing a non-flushing seek,
     * close the segment first based on the last_stop. */
    GST_DEBUG_OBJECT (avi, "closing running segment %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, avi->segment.start, avi->segment.last_stop);
    avi->close_seg_event = gst_event_new_new_segment_full (TRUE,
        avi->segment.rate, avi->segment.applied_rate, avi->segment.format,
        avi->segment.start, avi->segment.last_stop, avi->segment.time);
  }

  /* now update the real segment info */
  memcpy (&avi->segment, &seeksegment, sizeof (GstSegment));

  /* post the SEGMENT_START message when we do segmented playback */
  if (avi->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    gst_element_post_message (GST_ELEMENT_CAST (avi),
        gst_message_new_segment_start (GST_OBJECT_CAST (avi),
            avi->segment.format, avi->segment.last_stop));
  }

  /* prepare for streaming again */
  if ((stop = avi->segment.stop) == GST_CLOCK_TIME_NONE)
    stop = avi->segment.duration;

  /* queue the segment event for the streaming thread. */
  if (avi->seg_event)
    gst_event_unref (avi->seg_event);
  if (avi->segment.rate > 0.0) {
    /* forwards goes from last_stop to stop */
    avi->seg_event = gst_event_new_new_segment_full (FALSE,
        avi->segment.rate, avi->segment.applied_rate, avi->segment.format,
        avi->segment.last_stop, stop, avi->segment.time);
  } else {
    /* reverse goes from start to last_stop */
    avi->seg_event = gst_event_new_new_segment_full (FALSE,
        avi->segment.rate, avi->segment.applied_rate, avi->segment.format,
        avi->segment.start, avi->segment.last_stop, avi->segment.time);
  }

  if (!avi->streaming) {
    avi->segment_running = TRUE;
    gst_pad_start_task (avi->sinkpad, (GstTaskFunction) gst_avi_demux_loop,
        avi->sinkpad);
  }
  /* reset the last flow and mark discont, seek is always DISCONT */
  for (i = 0; i < avi->num_streams; i++) {
    GST_DEBUG_OBJECT (avi, "marking DISCONT");
    avi->stream[i].last_flow = GST_FLOW_OK;
    avi->stream[i].discont = TRUE;
  }
  GST_PAD_STREAM_UNLOCK (avi->sinkpad);

  return TRUE;

  /* ERRORS */
no_format:
  {
    GST_DEBUG_OBJECT (avi, "unsupported format given, seek aborted.");
    return FALSE;
  }
}

/*
 * Handle seek event in push mode.
 */
static gboolean
avi_demux_handle_seek_push (GstAviDemux * avi, GstPad * pad, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type = GST_SEEK_TYPE_NONE, stop_type;
  gint64 cur, stop;
  gboolean keyframe;
  GstAviStream *stream;
  guint index;
  guint n, str_num;
  guint64 min_offset;
  GstSegment seeksegment;
  gboolean update;

  /* check we have the index */
  if (!avi->have_index) {
    GST_DEBUG_OBJECT (avi, "no seek index built, seek aborted.");
    return FALSE;
  } else {
    GST_DEBUG_OBJECT (avi, "doing push-based seek with event");
  }

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  if (format != GST_FORMAT_TIME) {
    GstFormat fmt = GST_FORMAT_TIME;
    gboolean res = TRUE;

    if (cur_type != GST_SEEK_TYPE_NONE)
      res = gst_pad_query_convert (pad, format, cur, &fmt, &cur);
    if (res && stop_type != GST_SEEK_TYPE_NONE)
      res = gst_pad_query_convert (pad, format, stop, &fmt, &stop);
    if (!res) {
      GST_DEBUG_OBJECT (avi, "unsupported format given, seek aborted.");
      return FALSE;
    }

    format = fmt;
  }

  /* let gst_segment handle any tricky stuff */
  GST_DEBUG_OBJECT (avi, "configuring seek");
  memcpy (&seeksegment, &avi->segment, sizeof (GstSegment));
  gst_segment_set_seek (&seeksegment, rate, format, flags,
      cur_type, cur, stop_type, stop, &update);

  keyframe = !!(flags & GST_SEEK_FLAG_KEY_UNIT);
  cur = seeksegment.last_stop;

  GST_DEBUG_OBJECT (avi,
      "Seek requested: ts %" GST_TIME_FORMAT " stop %" GST_TIME_FORMAT
      ", kf %u, rate %lf", GST_TIME_ARGS (cur), GST_TIME_ARGS (stop), keyframe,
      rate);

  if (rate < 0) {
    GST_DEBUG_OBJECT (avi, "negative rate seek not supported in push mode");
    return FALSE;
  }

  /* FIXME, this code assumes the main stream with keyframes is stream 0,
   * which is mostly correct... */
  str_num = avi->main_stream;
  stream = &avi->stream[str_num];

  /* get the entry index for the requested position */
  index = gst_avi_demux_index_for_time (avi, stream, cur);
  GST_DEBUG_OBJECT (avi, "str %u: Found entry %u for %" GST_TIME_FORMAT,
      str_num, index, GST_TIME_ARGS (cur));

  /* check if we are already on a keyframe */
  if (!ENTRY_IS_KEYFRAME (&stream->index[index])) {
    GST_DEBUG_OBJECT (avi, "Entry is not a keyframe - searching back");
    /* now go to the previous keyframe, this is where we should start
     * decoding from. */
    index = gst_avi_demux_index_prev (avi, stream, index, TRUE);
    GST_DEBUG_OBJECT (avi, "Found previous keyframe at %u", index);
  }

  gst_avi_demux_get_buffer_info (avi, stream, index,
      &stream->current_timestamp, &stream->current_ts_end,
      &stream->current_offset, &stream->current_offset_end);

  /* re-use cur to be the timestamp of the seek as it _will_ be */
  cur = stream->current_timestamp;

  min_offset = stream->index[index].offset;
  avi->seek_kf_offset = min_offset - 8;

  GST_DEBUG_OBJECT (avi,
      "Seek to: ts %" GST_TIME_FORMAT " (on str %u, idx %u, offset %"
      G_GUINT64_FORMAT ")", GST_TIME_ARGS (stream->current_timestamp), str_num,
      index, min_offset);

  for (n = 0; n < avi->num_streams; n++) {
    GstAviStream *str = &avi->stream[n];
    guint idx;

    if (n == avi->main_stream)
      continue;

    /* get the entry index for the requested position */
    idx = gst_avi_demux_index_for_time (avi, str, cur);
    GST_DEBUG_OBJECT (avi, "str %u: Found entry %u for %" GST_TIME_FORMAT, n,
        idx, GST_TIME_ARGS (cur));

    /* check if we are already on a keyframe */
    if (!ENTRY_IS_KEYFRAME (&str->index[idx])) {
      GST_DEBUG_OBJECT (avi, "Entry is not a keyframe - searching back");
      /* now go to the previous keyframe, this is where we should start
       * decoding from. */
      idx = gst_avi_demux_index_prev (avi, str, idx, TRUE);
      GST_DEBUG_OBJECT (avi, "Found previous keyframe at %u", idx);
    }

    gst_avi_demux_get_buffer_info (avi, str, idx,
        &str->current_timestamp, &str->current_ts_end,
        &str->current_offset, &str->current_offset_end);

    if (str->index[idx].offset < min_offset) {
      min_offset = str->index[idx].offset;
      GST_DEBUG_OBJECT (avi,
          "Found an earlier offset at %" G_GUINT64_FORMAT ", str %u",
          min_offset, n);
      str_num = n;
      stream = str;
      index = idx;
    }
  }

  GST_DEBUG_OBJECT (avi,
      "Seek performed: str %u, offset %" G_GUINT64_FORMAT ", idx %u, ts %"
      GST_TIME_FORMAT ", ts_end %" GST_TIME_FORMAT ", off %" G_GUINT64_FORMAT
      ", off_end %" G_GUINT64_FORMAT, str_num, min_offset, index,
      GST_TIME_ARGS (stream->current_timestamp),
      GST_TIME_ARGS (stream->current_ts_end), stream->current_offset,
      stream->current_offset_end);

  /* index data refers to data, not chunk header (for pull mode convenience) */
  min_offset -= 8;
  GST_DEBUG_OBJECT (avi, "seeking to chunk at offset %" G_GUINT64_FORMAT,
      min_offset);

  if (!perform_seek_to_offset (avi, min_offset)) {
    GST_DEBUG_OBJECT (avi, "seek event failed!");
    return FALSE;
  }

  return TRUE;
}

/*
 * Handle whether we can perform the seek event or if we have to let the chain
 * function handle seeks to build the seek indexes first.
 */
static gboolean
gst_avi_demux_handle_seek_push (GstAviDemux * avi, GstPad * pad,
    GstEvent * event)
{
  /* check for having parsed index already */
  if (!avi->have_index) {
    guint64 offset = 0;
    gboolean building_index;

    GST_OBJECT_LOCK (avi);
    /* handle the seek event in the chain function */
    avi->state = GST_AVI_DEMUX_SEEK;

    /* copy the event */
    if (avi->seek_event)
      gst_event_unref (avi->seek_event);
    avi->seek_event = gst_event_ref (event);

    /* set the building_index flag so that only one thread can setup the
     * structures for index seeking. */
    building_index = avi->building_index;
    if (!building_index) {
      avi->building_index = TRUE;
      if (avi->stream[0].indexes) {
        avi->odml_stream = 0;
        avi->odml_subidxs = avi->stream[avi->odml_stream].indexes;
        offset = avi->odml_subidxs[0];
      } else {
        offset = avi->idx1_offset;
      }
    }
    GST_OBJECT_UNLOCK (avi);

    if (!building_index) {
      /* seek to the first subindex or legacy index */
      GST_INFO_OBJECT (avi,
          "Seeking to legacy index/first subindex at %" G_GUINT64_FORMAT,
          offset);
      return perform_seek_to_offset (avi, offset);
    }

    /* FIXME: we have to always return true so that we don't block the seek
     * thread.
     * Note: maybe it is OK to return true if we're still building the index */
    return TRUE;
  }

  return avi_demux_handle_seek_push (avi, pad, event);
}

/*
 * Helper for gst_avi_demux_invert()
 */
static inline void
swap_line (guint8 * d1, guint8 * d2, guint8 * tmp, gint bytes)
{
  memcpy (tmp, d1, bytes);
  memcpy (d1, d2, bytes);
  memcpy (d2, tmp, bytes);
}


#define gst_avi_demux_is_uncompressed(fourcc)		\
  (fourcc &&						\
    (fourcc == GST_RIFF_DIB ||				\
     fourcc == GST_RIFF_rgb ||				\
     fourcc == GST_RIFF_RGB || fourcc == GST_RIFF_RAW))

/*
 * Invert DIB buffers... Takes existing buffer and
 * returns either the buffer or a new one (with old
 * one dereferenced).
 * FIXME: can't we preallocate tmp? and remember stride, bpp?
 */
static GstBuffer *
gst_avi_demux_invert (GstAviStream * stream, GstBuffer * buf)
{
  GstStructure *s;
  gint y, w, h;
  gint bpp, stride;
  guint8 *tmp = NULL;

  if (stream->strh->type != GST_RIFF_FCC_vids)
    return buf;

  if (!gst_avi_demux_is_uncompressed (stream->strh->fcc_handler)) {
    return buf;                 /* Ignore non DIB buffers */
  }

  s = gst_caps_get_structure (GST_PAD_CAPS (stream->pad), 0);
  if (!gst_structure_get_int (s, "bpp", &bpp)) {
    GST_WARNING ("Failed to retrieve depth from caps");
    return buf;
  }

  if (stream->strf.vids == NULL) {
    GST_WARNING ("Failed to retrieve vids for stream");
    return buf;
  }

  h = stream->strf.vids->height;
  w = stream->strf.vids->width;
  stride = GST_ROUND_UP_4 (w * (bpp / 8));

  buf = gst_buffer_make_writable (buf);
  if (GST_BUFFER_SIZE (buf) < (stride * h)) {
    GST_WARNING ("Buffer is smaller than reported Width x Height x Depth");
    return buf;
  }

  tmp = g_malloc (stride);

  for (y = 0; y < h / 2; y++) {
    swap_line (GST_BUFFER_DATA (buf) + stride * y,
        GST_BUFFER_DATA (buf) + stride * (h - 1 - y), tmp, stride);
  }

  g_free (tmp);

  return buf;
}

static void
gst_avi_demux_add_assoc (GstAviDemux * avi, GstAviStream * stream,
    GstClockTime timestamp, guint64 offset, gboolean keyframe)
{
  /* do not add indefinitely for open-ended streaming */
  if (G_UNLIKELY (avi->element_index && avi->seekable)) {
    GST_LOG_OBJECT (avi, "adding association %" GST_TIME_FORMAT "-> %"
        G_GUINT64_FORMAT, GST_TIME_ARGS (timestamp), offset);
    gst_index_add_association (avi->element_index, avi->index_id,
        keyframe ? GST_ASSOCIATION_FLAG_KEY_UNIT :
        GST_ASSOCIATION_FLAG_DELTA_UNIT, GST_FORMAT_TIME, timestamp,
        GST_FORMAT_BYTES, offset, NULL);
    /* current_entry is DEFAULT (frame #) */
    gst_index_add_association (avi->element_index, stream->index_id,
        keyframe ? GST_ASSOCIATION_FLAG_KEY_UNIT :
        GST_ASSOCIATION_FLAG_DELTA_UNIT, GST_FORMAT_TIME, timestamp,
        GST_FORMAT_BYTES, offset, GST_FORMAT_DEFAULT, stream->current_entry,
        NULL);
  }
}

/*
 * Returns the aggregated GstFlowReturn.
 */
static GstFlowReturn
gst_avi_demux_combine_flows (GstAviDemux * avi, GstAviStream * stream,
    GstFlowReturn ret)
{
  guint i;
  gboolean unexpected = FALSE, not_linked = TRUE;

  /* store the value */
  stream->last_flow = ret;

  /* any other error that is not-linked or eos can be returned right away */
  if (G_LIKELY (ret != GST_FLOW_UNEXPECTED && ret != GST_FLOW_NOT_LINKED))
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (i = 0; i < avi->num_streams; i++) {
    GstAviStream *ostream = &avi->stream[i];

    ret = ostream->last_flow;
    /* no unexpected or unlinked, return */
    if (G_LIKELY (ret != GST_FLOW_UNEXPECTED && ret != GST_FLOW_NOT_LINKED))
      goto done;

    /* we check to see if we have at least 1 unexpected or all unlinked */
    unexpected |= (ret == GST_FLOW_UNEXPECTED);
    not_linked &= (ret == GST_FLOW_NOT_LINKED);
  }
  /* when we get here, we all have unlinked or unexpected */
  if (not_linked)
    ret = GST_FLOW_NOT_LINKED;
  else if (unexpected)
    ret = GST_FLOW_UNEXPECTED;
done:
  GST_LOG_OBJECT (avi, "combined %s to return %s",
      gst_flow_get_name (stream->last_flow), gst_flow_get_name (ret));
  return ret;
}

/* move @stream to the next position in its index */
static GstFlowReturn
gst_avi_demux_advance (GstAviDemux * avi, GstAviStream * stream,
    GstFlowReturn ret)
{
  guint old_entry, new_entry;

  old_entry = stream->current_entry;
  /* move forwards */
  new_entry = old_entry + 1;

  /* see if we reached the end */
  if (new_entry >= stream->stop_entry) {
    if (avi->segment.rate < 0.0) {
      if (stream->step_entry == stream->start_entry) {
        /* we stepped all the way to the start, eos */
        GST_DEBUG_OBJECT (avi, "reverse reached start %u", stream->start_entry);
        goto eos;
      }
      /* backwards, stop becomes step, find a new step */
      stream->stop_entry = stream->step_entry;
      stream->step_entry = gst_avi_demux_index_prev (avi, stream,
          stream->stop_entry, TRUE);

      GST_DEBUG_OBJECT (avi,
          "reverse playback jump: start %u, step %u, stop %u",
          stream->start_entry, stream->step_entry, stream->stop_entry);

      /* and start from the previous keyframe now */
      new_entry = stream->step_entry;
    } else {
      /* EOS */
      GST_DEBUG_OBJECT (avi, "forward reached stop %u", stream->stop_entry);
      goto eos;
    }
  }

  if (new_entry != old_entry) {
    stream->current_entry = new_entry;
    stream->current_total = stream->index[new_entry].total;

    if (new_entry == old_entry + 1) {
      GST_DEBUG_OBJECT (avi, "moved forwards from %u to %u",
          old_entry, new_entry);
      /* we simply moved one step forwards, reuse current info */
      stream->current_timestamp = stream->current_ts_end;
      stream->current_offset = stream->current_offset_end;
      gst_avi_demux_get_buffer_info (avi, stream, new_entry,
          NULL, &stream->current_ts_end, NULL, &stream->current_offset_end);
    } else {
      /* we moved DISCONT, full update */
      gst_avi_demux_get_buffer_info (avi, stream, new_entry,
          &stream->current_timestamp, &stream->current_ts_end,
          &stream->current_offset, &stream->current_offset_end);
      /* and MARK discont for this stream */
      stream->last_flow = GST_FLOW_OK;
      stream->discont = TRUE;
      GST_DEBUG_OBJECT (avi, "Moved from %u to %u, ts %" GST_TIME_FORMAT
          ", ts_end %" GST_TIME_FORMAT ", off %" G_GUINT64_FORMAT
          ", off_end %" G_GUINT64_FORMAT, old_entry, new_entry,
          GST_TIME_ARGS (stream->current_timestamp),
          GST_TIME_ARGS (stream->current_ts_end), stream->current_offset,
          stream->current_offset_end);
    }
  }
  return ret;

  /* ERROR */
eos:
  {
    GST_DEBUG_OBJECT (avi, "we are EOS");
    /* setting current_timestamp to -1 marks EOS */
    stream->current_timestamp = -1;
    return GST_FLOW_UNEXPECTED;
  }
}

/* find the stream with the lowest current position when going forwards or with
 * the highest position when going backwards, this is the stream
 * we should push from next */
static gint
gst_avi_demux_find_next (GstAviDemux * avi, gfloat rate)
{
  guint64 min_time, max_time;
  guint stream_num, i;

  max_time = 0;
  min_time = G_MAXUINT64;
  stream_num = -1;

  for (i = 0; i < avi->num_streams; i++) {
    guint64 position;
    GstAviStream *stream;

    stream = &avi->stream[i];

    /* ignore streams that finished */
    if (stream->last_flow == GST_FLOW_UNEXPECTED)
      continue;

    position = stream->current_timestamp;

    /* position of -1 is EOS */
    if (position != -1) {
      if (rate > 0.0 && position < min_time) {
        min_time = position;
        stream_num = i;
      } else if (rate < 0.0 && position >= max_time) {
        max_time = position;
        stream_num = i;
      }
    }
  }
  return stream_num;
}

static GstFlowReturn
gst_avi_demux_loop_data (GstAviDemux * avi)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint stream_num;
  GstAviStream *stream;
  gboolean processed = FALSE;
  GstBuffer *buf;
  guint64 offset, size;
  GstClockTime timestamp, duration;
  guint64 out_offset, out_offset_end;
  gboolean keyframe;
  GstAviIndexEntry *entry;

  do {
    stream_num = gst_avi_demux_find_next (avi, avi->segment.rate);

    /* all are EOS */
    if (G_UNLIKELY (stream_num == -1)) {
      GST_DEBUG_OBJECT (avi, "all streams are EOS");
      goto eos;
    }

    /* we have the stream now */
    stream = &avi->stream[stream_num];

    /* skip streams without pads */
    if (!stream->pad) {
      GST_DEBUG_OBJECT (avi, "skipping entry from stream %d without pad",
          stream_num);
      goto next;
    }

    /* get the timing info for the entry */
    timestamp = stream->current_timestamp;
    duration = stream->current_ts_end - timestamp;
    out_offset = stream->current_offset;
    out_offset_end = stream->current_offset_end;

    /* get the entry data info */
    entry = &stream->index[stream->current_entry];
    offset = entry->offset;
    size = entry->size;
    keyframe = ENTRY_IS_KEYFRAME (entry);

    /* skip empty entries */
    if (size == 0) {
      GST_DEBUG_OBJECT (avi, "Skipping entry %u (%" G_GUINT64_FORMAT ", %p)",
          stream->current_entry, size, stream->pad);
      goto next;
    }

    if (avi->segment.rate > 0.0) {
      /* only check this for fowards playback for now */
      if (keyframe && GST_CLOCK_TIME_IS_VALID (avi->segment.stop)
          && (timestamp > avi->segment.stop)) {
        goto eos_stop;
      }
    }

    GST_LOG ("reading buffer (size=%" G_GUINT64_FORMAT "), stream %d, pos %"
        G_GUINT64_FORMAT " (0x%" G_GINT64_MODIFIER "x), kf %d", size,
        stream_num, offset, offset, keyframe);

    /* FIXME, check large chunks and cut them up */

    /* pull in the data */
    ret = gst_pad_pull_range (avi->sinkpad, offset, size, &buf);
    if (ret != GST_FLOW_OK)
      goto pull_failed;

    /* check for short buffers, this is EOS as well */
    if (GST_BUFFER_SIZE (buf) < size)
      goto short_buffer;

    /* invert the picture if needed */
    buf = gst_avi_demux_invert (stream, buf);

    /* mark non-keyframes */
    if (keyframe)
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
    else
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

    GST_BUFFER_TIMESTAMP (buf) = timestamp;
    GST_BUFFER_DURATION (buf) = duration;
    GST_BUFFER_OFFSET (buf) = out_offset;
    GST_BUFFER_OFFSET_END (buf) = out_offset_end;

    /* mark discont when pending */
    if (stream->discont) {
      GST_DEBUG_OBJECT (avi, "setting DISCONT flag");
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;
    }

    gst_avi_demux_add_assoc (avi, stream, timestamp, offset, keyframe);

    gst_buffer_set_caps (buf, GST_PAD_CAPS (stream->pad));

    /* update current position in the segment */
    gst_segment_set_last_stop (&avi->segment, GST_FORMAT_TIME, timestamp);

    GST_DEBUG_OBJECT (avi, "Pushing buffer of size %u, ts %"
        GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT ", off %" G_GUINT64_FORMAT
        ", off_end %" G_GUINT64_FORMAT,
        GST_BUFFER_SIZE (buf), GST_TIME_ARGS (timestamp),
        GST_TIME_ARGS (duration), out_offset, out_offset_end);

    ret = gst_pad_push (stream->pad, buf);

    /* mark as processed, we increment the frame and byte counters then
     * leave the while loop and return the GstFlowReturn */
    processed = TRUE;

    if (avi->segment.rate < 0) {
      if (timestamp > avi->segment.stop && ret == GST_FLOW_UNEXPECTED) {
        /* In reverse playback we can get a GST_FLOW_UNEXPECTED when
         * we are at the end of the segment, so we just need to jump
         * back to the previous section. */
        GST_DEBUG_OBJECT (avi, "downstream has reached end of segment");
        ret = GST_FLOW_OK;
      }
    }
  next:
    /* move to next item */
    ret = gst_avi_demux_advance (avi, stream, ret);

    /* combine flows */
    ret = gst_avi_demux_combine_flows (avi, stream, ret);
  } while (!processed);

beach:
  return ret;

  /* special cases */
eos:
  {
    GST_DEBUG_OBJECT (avi, "No samples left for any streams - EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }
eos_stop:
  {
    GST_LOG_OBJECT (avi, "Found keyframe after segment,"
        " setting EOS (%" GST_TIME_FORMAT " > %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (timestamp), GST_TIME_ARGS (avi->segment.stop));
    ret = GST_FLOW_UNEXPECTED;
    /* move to next stream */
    goto next;
  }
pull_failed:
  {
    GST_DEBUG_OBJECT (avi, "pull range failed: pos=%" G_GUINT64_FORMAT
        " size=%" G_GUINT64_FORMAT, offset, size);
    goto beach;
  }
short_buffer:
  {
    GST_WARNING_OBJECT (avi, "Short read at offset %" G_GUINT64_FORMAT
        ", only got %d/%" G_GUINT64_FORMAT " bytes (truncated file?)", offset,
        GST_BUFFER_SIZE (buf), size);
    gst_buffer_unref (buf);
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }
}

/*
 * Read data. If we have an index it delegates to
 * gst_avi_demux_process_next_entry().
 */
static GstFlowReturn
gst_avi_demux_stream_data (GstAviDemux * avi)
{
  guint32 tag = 0;
  guint32 size = 0;
  gint stream_nr = 0;
  GstFlowReturn res = GST_FLOW_OK;
  GstFormat format = GST_FORMAT_TIME;

  if (G_UNLIKELY (avi->have_eos)) {
    /* Clean adapter, we're done */
    gst_adapter_clear (avi->adapter);
    return GST_FLOW_UNEXPECTED;
  }

  if (G_UNLIKELY (avi->todrop)) {
    guint drop;

    if ((drop = gst_adapter_available (avi->adapter))) {
      if (drop > avi->todrop)
        drop = avi->todrop;
      GST_DEBUG_OBJECT (avi, "Dropping %d bytes", drop);
      gst_adapter_flush (avi->adapter, drop);
      avi->todrop -= drop;
      avi->offset += drop;
    }
  }

  /* Iterate until need more data, so adapter won't grow too much */
  while (1) {
    if (G_UNLIKELY (!gst_avi_demux_peek_chunk_info (avi, &tag, &size))) {
      return GST_FLOW_OK;
    }

    GST_DEBUG ("Trying chunk (%" GST_FOURCC_FORMAT "), size %d",
        GST_FOURCC_ARGS (tag), size);

    if (G_LIKELY ((tag & 0xff) >= '0' && (tag & 0xff) <= '9' &&
            ((tag >> 8) & 0xff) >= '0' && ((tag >> 8) & 0xff) <= '9')) {
      GST_LOG ("Chunk ok");
    } else if ((tag & 0xffff) == (('x' << 8) | 'i')) {
      GST_DEBUG ("Found sub-index tag");
      if (gst_avi_demux_peek_chunk (avi, &tag, &size) || size == 0) {
        /* accept 0 size buffer here */
        avi->abort_buffering = FALSE;
        GST_DEBUG ("  skipping %d bytes for now", size);
        gst_adapter_flush (avi->adapter, 8 + GST_ROUND_UP_2 (size));
      }
      return GST_FLOW_OK;
    } else if (tag == GST_RIFF_TAG_RIFF) {
      /* RIFF tags can appear in ODML files, just jump over them */
      if (gst_adapter_available (avi->adapter) >= 12) {
        GST_DEBUG ("Found RIFF tag, skipping RIFF header");
        gst_adapter_flush (avi->adapter, 12);
        continue;
      }
      return GST_FLOW_OK;
    } else if (tag == GST_RIFF_TAG_idx1) {
      GST_DEBUG ("Found index tag");
      if (gst_avi_demux_peek_chunk (avi, &tag, &size) || size == 0) {
        /* accept 0 size buffer here */
        avi->abort_buffering = FALSE;
        GST_DEBUG ("  skipping %d bytes for now", size);
        gst_adapter_flush (avi->adapter, 8 + GST_ROUND_UP_2 (size));
      }
      return GST_FLOW_OK;
    } else if (tag == GST_RIFF_TAG_LIST) {
      /* movi chunks might be grouped in rec list */
      if (gst_adapter_available (avi->adapter) >= 12) {
        GST_DEBUG ("Found LIST tag, skipping LIST header");
        gst_adapter_flush (avi->adapter, 12);
        continue;
      }
      return GST_FLOW_OK;
    } else if (tag == GST_RIFF_TAG_JUNK || tag == GST_RIFF_TAG_JUNQ) {
      /* rec list might contain JUNK chunks */
      GST_DEBUG ("Found JUNK tag");
      if (gst_avi_demux_peek_chunk (avi, &tag, &size) || size == 0) {
        /* accept 0 size buffer here */
        avi->abort_buffering = FALSE;
        GST_DEBUG ("  skipping %d bytes for now", size);
        gst_adapter_flush (avi->adapter, 8 + GST_ROUND_UP_2 (size));
      }
      return GST_FLOW_OK;
    } else {
      GST_DEBUG ("No more stream chunks, send EOS");
      avi->have_eos = TRUE;
      return GST_FLOW_UNEXPECTED;
    }

    if (G_UNLIKELY (!gst_avi_demux_peek_chunk (avi, &tag, &size))) {
      /* supposedly one hopes to catch a nicer chunk later on ... */
      /* FIXME ?? give up here rather than possibly ending up going
       * through the whole file */
      if (avi->abort_buffering) {
        avi->abort_buffering = FALSE;
        if (size) {
          gst_adapter_flush (avi->adapter, 8);
          return GST_FLOW_OK;
        }
      } else {
        return GST_FLOW_OK;
      }
    }
    GST_DEBUG ("chunk ID %" GST_FOURCC_FORMAT ", size %u",
        GST_FOURCC_ARGS (tag), size);

    stream_nr = CHUNKID_TO_STREAMNR (tag);

    if (G_UNLIKELY (stream_nr < 0 || stream_nr >= avi->num_streams)) {
      /* recoverable */
      GST_WARNING ("Invalid stream ID %d (%" GST_FOURCC_FORMAT ")",
          stream_nr, GST_FOURCC_ARGS (tag));
      avi->offset += 8 + GST_ROUND_UP_2 (size);
      gst_adapter_flush (avi->adapter, 8 + GST_ROUND_UP_2 (size));
    } else {
      GstAviStream *stream;
      GstClockTime next_ts = 0;
      GstBuffer *buf = NULL;
      guint64 offset;
      gboolean saw_desired_kf = stream_nr != avi->main_stream
          || avi->offset >= avi->seek_kf_offset;

      if (stream_nr == avi->main_stream && avi->offset == avi->seek_kf_offset) {
        GST_DEBUG_OBJECT (avi, "Desired keyframe reached");
        avi->seek_kf_offset = 0;
      }

      if (saw_desired_kf) {
        gst_adapter_flush (avi->adapter, 8);
        /* get buffer */
        if (size) {
          buf = gst_adapter_take_buffer (avi->adapter, GST_ROUND_UP_2 (size));
          /* patch the size */
          GST_BUFFER_SIZE (buf) = size;
        } else {
          buf = NULL;
        }
      } else {
        GST_DEBUG_OBJECT (avi,
            "Desired keyframe not yet reached, flushing chunk");
        gst_adapter_flush (avi->adapter, 8 + GST_ROUND_UP_2 (size));
      }

      offset = avi->offset;
      avi->offset += 8 + GST_ROUND_UP_2 (size);

      stream = &avi->stream[stream_nr];

      /* set delay (if any)
         if (stream->strh->init_frames == stream->current_frame &&
         stream->delay == 0)
         stream->delay = next_ts;
       */

      /* parsing of corresponding header may have failed */
      if (G_UNLIKELY (!stream->pad)) {
        GST_WARNING_OBJECT (avi, "no pad for stream ID %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (tag));
        if (buf)
          gst_buffer_unref (buf);
      } else {
        /* get time of this buffer */
        gst_pad_query_position (stream->pad, &format, (gint64 *) & next_ts);
        if (G_UNLIKELY (format != GST_FORMAT_TIME))
          goto wrong_format;

        gst_avi_demux_add_assoc (avi, stream, next_ts, offset, FALSE);

        /* increment our positions */
        stream->current_entry++;
        stream->current_total += size;

        /* update current position in the segment */
        gst_segment_set_last_stop (&avi->segment, GST_FORMAT_TIME, next_ts);

        if (saw_desired_kf && buf) {
          GstClockTime dur_ts = 0;

          /* invert the picture if needed */
          buf = gst_avi_demux_invert (stream, buf);

          gst_pad_query_position (stream->pad, &format, (gint64 *) & dur_ts);
          if (G_UNLIKELY (format != GST_FORMAT_TIME))
            goto wrong_format;

          GST_BUFFER_TIMESTAMP (buf) = next_ts;
          GST_BUFFER_DURATION (buf) = dur_ts - next_ts;
          if (stream->strh->type == GST_RIFF_FCC_vids) {
            GST_BUFFER_OFFSET (buf) = stream->current_entry - 1;
            GST_BUFFER_OFFSET_END (buf) = stream->current_entry;
          } else {
            GST_BUFFER_OFFSET (buf) = GST_BUFFER_OFFSET_NONE;
            GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;
          }

          gst_buffer_set_caps (buf, GST_PAD_CAPS (stream->pad));
          GST_DEBUG_OBJECT (avi,
              "Pushing buffer with time=%" GST_TIME_FORMAT ", duration %"
              GST_TIME_FORMAT ", offset %" G_GUINT64_FORMAT
              " and size %d over pad %s", GST_TIME_ARGS (next_ts),
              GST_TIME_ARGS (GST_BUFFER_DURATION (buf)),
              GST_BUFFER_OFFSET (buf), size, GST_PAD_NAME (stream->pad));

          /* mark discont when pending */
          if (G_UNLIKELY (stream->discont)) {
            GST_DEBUG_OBJECT (avi, "Setting DISCONT");
            GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
            stream->discont = FALSE;
          }
          res = gst_pad_push (stream->pad, buf);
          buf = NULL;

          /* combine flows */
          res = gst_avi_demux_combine_flows (avi, stream, res);
          if (G_UNLIKELY (res != GST_FLOW_OK)) {
            GST_DEBUG ("Push failed; %s", gst_flow_get_name (res));
            return res;
          }
        }
      }
    }
  }

done:
  return res;

  /* ERRORS */
wrong_format:
  {
    GST_DEBUG_OBJECT (avi, "format %s != GST_FORMAT_TIME",
        gst_format_get_name (format));
    res = GST_FLOW_ERROR;
    goto done;
  }
}

/*
 * Send pending tags.
 */
static void
push_tag_lists (GstAviDemux * avi)
{
  guint i;
  GstTagList *tags;

  if (!avi->got_tags)
    return;

  GST_DEBUG_OBJECT (avi, "Pushing pending tag lists");

  for (i = 0; i < avi->num_streams; i++) {
    GstAviStream *stream = &avi->stream[i];
    GstPad *pad = stream->pad;

    tags = stream->taglist;

    if (pad && tags) {
      GST_DEBUG_OBJECT (pad, "Tags: %" GST_PTR_FORMAT, tags);

      gst_element_found_tags_for_pad (GST_ELEMENT_CAST (avi), pad, tags);
      stream->taglist = NULL;
    }
  }

  if (!(tags = avi->globaltags))
    tags = gst_tag_list_new ();

  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
      GST_TAG_CONTAINER_FORMAT, "AVI", NULL);

  GST_DEBUG_OBJECT (avi, "Global tags: %" GST_PTR_FORMAT, tags);
  gst_element_found_tags (GST_ELEMENT_CAST (avi), tags);
  avi->globaltags = NULL;
  avi->got_tags = FALSE;
}

static void
gst_avi_demux_loop (GstPad * pad)
{
  GstFlowReturn res;
  GstAviDemux *avi = GST_AVI_DEMUX (GST_PAD_PARENT (pad));

  switch (avi->state) {
    case GST_AVI_DEMUX_START:
      res = gst_avi_demux_stream_init_pull (avi);
      if (G_UNLIKELY (res != GST_FLOW_OK)) {
        GST_WARNING ("stream_init flow: %s", gst_flow_get_name (res));
        goto pause;
      }
      avi->state = GST_AVI_DEMUX_HEADER;
      /* fall-through */
    case GST_AVI_DEMUX_HEADER:
      res = gst_avi_demux_stream_header_pull (avi);
      if (G_UNLIKELY (res != GST_FLOW_OK)) {
        GST_WARNING ("stream_header flow: %s", gst_flow_get_name (res));
        goto pause;
      }
      avi->state = GST_AVI_DEMUX_MOVI;
      break;
    case GST_AVI_DEMUX_MOVI:
      if (G_UNLIKELY (avi->close_seg_event)) {
        gst_avi_demux_push_event (avi, avi->close_seg_event);
        avi->close_seg_event = NULL;
      }
      if (G_UNLIKELY (avi->seg_event)) {
        gst_avi_demux_push_event (avi, avi->seg_event);
        avi->seg_event = NULL;
      }
      if (G_UNLIKELY (avi->got_tags)) {
        push_tag_lists (avi);
      }
      /* process each index entry in turn */
      res = gst_avi_demux_loop_data (avi);

      /* pause when error */
      if (G_UNLIKELY (res != GST_FLOW_OK)) {
        GST_INFO ("stream_movi flow: %s", gst_flow_get_name (res));
        goto pause;
      }
      break;
    default:
      GST_ERROR_OBJECT (avi, "unknown state %d", avi->state);
      res = GST_FLOW_ERROR;
      goto pause;
  }

  return;

  /* ERRORS */
pause:{

    gboolean push_eos = FALSE;
    GST_LOG_OBJECT (avi, "pausing task, reason %s", gst_flow_get_name (res));
    avi->segment_running = FALSE;
    gst_pad_pause_task (avi->sinkpad);


    if (res == GST_FLOW_UNEXPECTED) {
      /* handle end-of-stream/segment */
      if (avi->segment.flags & GST_SEEK_FLAG_SEGMENT) {
        gint64 stop;

        if ((stop = avi->segment.stop) == -1)
          stop = avi->segment.duration;

        GST_INFO_OBJECT (avi, "sending segment_done");

        gst_element_post_message
            (GST_ELEMENT_CAST (avi),
            gst_message_new_segment_done (GST_OBJECT_CAST (avi),
                GST_FORMAT_TIME, stop));
      } else {
        push_eos = TRUE;
      }
    } else if (res == GST_FLOW_NOT_LINKED || res < GST_FLOW_UNEXPECTED) {
      /* for fatal errors we post an error message, wrong-state is
       * not fatal because it happens due to flushes and only means
       * that we should stop now. */
      GST_ELEMENT_ERROR (avi, STREAM, FAILED,
          (_("Internal data stream error.")),
          ("streaming stopped, reason %s", gst_flow_get_name (res)));
      push_eos = TRUE;
    }
    if (push_eos) {
      GST_INFO_OBJECT (avi, "sending eos");
      if (!gst_avi_demux_push_event (avi, gst_event_new_eos ()) &&
          (res == GST_FLOW_UNEXPECTED)) {
        GST_ELEMENT_ERROR (avi, STREAM, DEMUX,
            (NULL), ("got eos but no streams (yet)"));
      }
    }
  }
}


static GstFlowReturn
gst_avi_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn res;
  GstAviDemux *avi = GST_AVI_DEMUX (GST_PAD_PARENT (pad));
  gint i;

  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_DEBUG_OBJECT (avi, "got DISCONT");
    gst_adapter_clear (avi->adapter);
    /* mark all streams DISCONT */
    for (i = 0; i < avi->num_streams; i++)
      avi->stream[i].discont = TRUE;
  }

  GST_DEBUG ("Store %d bytes in adapter", GST_BUFFER_SIZE (buf));
  gst_adapter_push (avi->adapter, buf);

  switch (avi->state) {
    case GST_AVI_DEMUX_START:
      if ((res = gst_avi_demux_stream_init_push (avi)) != GST_FLOW_OK) {
        GST_WARNING ("stream_init flow: %s", gst_flow_get_name (res));
        break;
      }
      break;
    case GST_AVI_DEMUX_HEADER:
      if ((res = gst_avi_demux_stream_header_push (avi)) != GST_FLOW_OK) {
        GST_WARNING ("stream_header flow: %s", gst_flow_get_name (res));
        break;
      }
      break;
    case GST_AVI_DEMUX_MOVI:
      if (G_UNLIKELY (avi->close_seg_event)) {
        gst_avi_demux_push_event (avi, avi->close_seg_event);
        avi->close_seg_event = NULL;
      }
      if (G_UNLIKELY (avi->seg_event)) {
        gst_avi_demux_push_event (avi, avi->seg_event);
        avi->seg_event = NULL;
      }
      if (G_UNLIKELY (avi->got_tags)) {
        push_tag_lists (avi);
      }
      res = gst_avi_demux_stream_data (avi);
      break;
    case GST_AVI_DEMUX_SEEK:
    {
      GstEvent *event;

      res = GST_FLOW_OK;

      /* obtain and parse indexes */
      if (avi->stream[0].indexes && !gst_avi_demux_read_subindexes_push (avi))
        /* seek in subindex read function failed */
        goto index_failed;

      if (!avi->stream[0].indexes && !avi->have_index
          && avi->avih->flags & GST_RIFF_AVIH_HASINDEX)
        gst_avi_demux_stream_index_push (avi);

      if (avi->have_index) {
        /* use the indexes now to construct nice durations */
        gst_avi_demux_calculate_durations_from_index (avi);
      } else {
        /* still parsing indexes */
        break;
      }

      GST_OBJECT_LOCK (avi);
      event = avi->seek_event;
      avi->seek_event = NULL;
      GST_OBJECT_UNLOCK (avi);

      /* calculate and perform seek */
      if (!avi_demux_handle_seek_push (avi, avi->sinkpad, event))
        goto seek_failed;

      gst_event_unref (event);
      avi->state = GST_AVI_DEMUX_MOVI;
      break;
    }
    default:
      GST_ELEMENT_ERROR (avi, STREAM, FAILED, (NULL),
          ("Illegal internal state"));
      res = GST_FLOW_ERROR;
      break;
  }

  GST_DEBUG_OBJECT (avi, "state: %d res:%s", avi->state,
      gst_flow_get_name (res));

  if (G_UNLIKELY (avi->abort_buffering))
    goto abort_buffering;

  return res;

  /* ERRORS */
index_failed:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL), ("failed to read indexes"));
    return GST_FLOW_ERROR;
  }
seek_failed:
  {
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL), ("push mode seek failed"));
    return GST_FLOW_ERROR;
  }
abort_buffering:
  {
    avi->abort_buffering = FALSE;
    GST_ELEMENT_ERROR (avi, STREAM, DEMUX, (NULL), ("unhandled buffer size"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_avi_demux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad)) {
    GST_DEBUG ("going to pull mode");
    return gst_pad_activate_pull (sinkpad, TRUE);
  } else {
    GST_DEBUG ("going to push (streaming) mode");
    return gst_pad_activate_push (sinkpad, TRUE);
  }
}

static gboolean
gst_avi_demux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstAviDemux *avi = GST_AVI_DEMUX (GST_OBJECT_PARENT (sinkpad));

  if (active) {
    avi->segment_running = TRUE;
    avi->streaming = FALSE;
    return gst_pad_start_task (sinkpad, (GstTaskFunction) gst_avi_demux_loop,
        sinkpad);
  } else {
    avi->segment_running = FALSE;
    return gst_pad_stop_task (sinkpad);
  }
}

static gboolean
gst_avi_demux_activate_push (GstPad * pad, gboolean active)
{
  GstAviDemux *avi = GST_AVI_DEMUX (GST_OBJECT_PARENT (pad));

  if (active) {
    GST_DEBUG ("avi: activating push/chain function");
    avi->streaming = TRUE;
#if 0
    /* create index for some push based seeking if not provided */
    GST_OBJECT_LOCK (avi);
    if (!avi->element_index) {
      GST_DEBUG_OBJECT (avi, "creating index");
      avi->element_index = gst_index_factory_make ("memindex");
    }
    GST_OBJECT_UNLOCK (avi);
    /* object lock might be taken again */
    gst_index_get_writer_id (avi->element_index, GST_OBJECT_CAST (avi),
        &avi->index_id);
#endif
  } else {
    GST_DEBUG ("avi: deactivating push/chain function");
  }

  return TRUE;
}

static void
gst_avi_demux_set_index (GstElement * element, GstIndex * index)
{
  GstAviDemux *avi = GST_AVI_DEMUX (element);

  GST_OBJECT_LOCK (avi);
  if (avi->element_index)
    gst_object_unref (avi->element_index);
  if (index) {
    avi->element_index = gst_object_ref (index);
  } else {
    avi->element_index = NULL;
  }
  GST_OBJECT_UNLOCK (avi);
  /* object lock might be taken again */
  if (index)
    gst_index_get_writer_id (index, GST_OBJECT_CAST (element), &avi->index_id);
  GST_DEBUG_OBJECT (avi, "Set index %" GST_PTR_FORMAT, avi->element_index);
}

static GstIndex *
gst_avi_demux_get_index (GstElement * element)
{
  GstIndex *result = NULL;
  GstAviDemux *avi = GST_AVI_DEMUX (element);

  GST_OBJECT_LOCK (avi);
  if (avi->element_index)
    result = gst_object_ref (avi->element_index);
  GST_OBJECT_UNLOCK (avi);

  GST_DEBUG_OBJECT (avi, "Returning index %" GST_PTR_FORMAT, result);

  return result;
}

static GstStateChangeReturn
gst_avi_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstAviDemux *avi = GST_AVI_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      avi->streaming = FALSE;
      gst_segment_init (&avi->segment, GST_FORMAT_TIME);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      avi->have_index = FALSE;
      gst_avi_demux_reset (avi);
      break;
    default:
      break;
  }

done:
  return ret;
}
