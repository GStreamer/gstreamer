/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@temple-baptist.com>
 * Copyright (C) <2006> Nokia Corporation (contact <stefan.kost@nokia.com>)
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
 * <refsect2>
 * <para>
 * Demuxes an .avi file into raw or compressed audio and/or video streams.
 * </para>
 * <para>
 * This element supports both push and pull-based scheduling, depending on the
 * capabilities of the upstream elements.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=test.avi ! avidemux name=demux  demux.audio_00 ! decodebin ! audioconvert ! audioresample ! autoaudiosink   demux.video_00 ! queue ! decodebin ! ffmpegcolorspace ! videoscale ! autovideosink
 * </programlisting>
 * Play (parse and decode) an .avi file and try to output it to
 * an automatically detected soundcard and videosink. If the AVI file contains
 * compressed audio or video data, this will only work if you have the
 * right decoder elements/plugins installed.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-12-29 (0.10.6)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gst/riff/riff-media.h"
#include "gstavidemux.h"
#include "avi-ids.h"
#include <gst/gst-i18n-plugin.h>
#include <gst/base/gstadapter.h>

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
static void gst_avi_demux_finalize (GObject * object);

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
static gboolean gst_avi_demux_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

static gboolean gst_avi_demux_do_seek (GstAviDemux * avi, GstSegment * segment);
static gboolean gst_avi_demux_handle_seek (GstAviDemux * avi, GstPad * pad,
    GstEvent * event);
static void gst_avi_demux_loop (GstPad * pad);
static gboolean gst_avi_demux_sink_activate (GstPad * sinkpad);
static gboolean gst_avi_demux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_avi_demux_activate_push (GstPad * pad, gboolean active);
static GstFlowReturn gst_avi_demux_chain (GstPad * pad, GstBuffer * buf);

static GstStateChangeReturn gst_avi_demux_change_state (GstElement * element,
    GstStateChange transition);

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
  static const GstElementDetails gst_avi_demux_details =
      GST_ELEMENT_DETAILS ("Avi demuxer",
      "Codec/Demuxer",
      "Demultiplex an avi file into audio and video",
      "Erik Walthinsen <omega@cse.ogi.edu>\n"
      "Wim Taymans <wim.taymans@chello.be>\n"
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
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
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_set_details (element_class, &gst_avi_demux_details);
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
}

static void
gst_avi_demux_init (GstAviDemux * avi)
{
  avi->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_activate_function (avi->sinkpad, gst_avi_demux_sink_activate);
  gst_pad_set_activatepull_function (avi->sinkpad,
      gst_avi_demux_sink_activate_pull);
  gst_pad_set_activatepush_function (avi->sinkpad, gst_avi_demux_activate_push);
  gst_pad_set_chain_function (avi->sinkpad, gst_avi_demux_chain);
  gst_element_add_pad (GST_ELEMENT (avi), avi->sinkpad);

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
gst_avi_demux_reset (GstAviDemux * avi)
{
  gint i;

  GST_DEBUG ("AVI: reset");

  for (i = 0; i < avi->num_streams; i++) {
    g_free (avi->stream[i].strh);
    g_free (avi->stream[i].strf.data);
    if (avi->stream[i].name)
      g_free (avi->stream[i].name);
    if (avi->stream[i].initdata)
      gst_buffer_unref (avi->stream[i].initdata);
    if (avi->stream[i].extradata)
      gst_buffer_unref (avi->stream[i].extradata);
    if (avi->stream[i].pad) {
      gst_pad_set_active (avi->stream[i].pad, FALSE);
      gst_element_remove_pad (GST_ELEMENT (avi), avi->stream[i].pad);
    }
    if (avi->stream[i].taglist) {
      gst_tag_list_free (avi->stream[i].taglist);
      avi->stream[i].taglist = NULL;
    }
  }
  memset (&avi->stream, 0, sizeof (avi->stream));

  avi->num_streams = 0;
  avi->num_v_streams = 0;
  avi->num_a_streams = 0;
  avi->num_t_streams = 0;

  avi->state = GST_AVI_DEMUX_START;
  avi->offset = 0;

  g_free (avi->index_entries);
  avi->index_entries = NULL;
  avi->index_size = 0;
  avi->index_offset = 0;
  avi->current_entry = 0;
  g_free (avi->avih);
  avi->avih = NULL;

  if (avi->seek_event) {
    gst_event_unref (avi->seek_event);
    avi->seek_event = NULL;
  }

  if (avi->globaltags)
    gst_tag_list_free (avi->globaltags);
  avi->globaltags = NULL;

  avi->got_tags = FALSE;
  avi->have_eos = FALSE;

  gst_adapter_clear (avi->adapter);

  gst_segment_init (&avi->segment, GST_FORMAT_TIME);
}

/* Index helper */
static gst_avi_index_entry *
gst_avi_demux_index_last (GstAviDemux * avi, gint stream_nr)
{
  gint i;
  gst_avi_index_entry *result = NULL;

  for (i = avi->index_size - 1; i >= 0; i--) {
    if (avi->index_entries[i].stream_nr == stream_nr) {
      result = &avi->index_entries[i];
      break;
    }
  }
  return result;
}

#if 0
static gst_avi_index_entry *
gst_avi_demux_index_next (GstAviDemux * avi, gint stream_nr, gint last,
    guchar flags)
{
  gint i;
  gst_avi_index_entry *result = NULL, *entry;

  for (i = last + 1; i < avi->index_size; i++) {
    entry = &avi->index_entries[i];

    if (entry->stream_nr != stream_nr)
      continue;

    if ((entry->flags & flags) == flags) {
      result = entry;
      break;
    }
  }
  return result;
}
#endif

static gst_avi_index_entry *
gst_avi_demux_index_prev (GstAviDemux * avi, gint stream_nr, gint last,
    guchar flags)
{
  gint i;
  gst_avi_index_entry *result = NULL, *entry;

  for (i = last - 1; i >= 0; i--) {
    entry = &avi->index_entries[i];

    if (entry->stream_nr != stream_nr)
      continue;

    if ((entry->flags & flags) == flags) {
      result = entry;
      break;
    }
  }
  return result;
}

/*
 * gst_avi_index_entry:
 * @avi: Avi object
 * @stream_nr: stream number
 * @time: seek time position
 *
 * Finds the index entry which time is less or equal than the requested time.
 *
 * Returns: the found index entry or %NULL
 */
static gst_avi_index_entry *
gst_avi_demux_index_entry_for_time (GstAviDemux * avi,
    gint stream_nr, guint64 time)
{
  gst_avi_index_entry *entry = NULL, *last_entry = NULL;
  gint i;

  GST_LOG_OBJECT (avi, "stream_nr:%d , time:%" GST_TIME_FORMAT,
      stream_nr, GST_TIME_ARGS (time));

  for (i = 0; i < avi->index_size; i++) {
    entry = &avi->index_entries[i];

    if (entry->stream_nr != stream_nr)
      continue;

    if (entry->ts > time)
      break;

    last_entry = entry;

    GST_LOG_OBJECT (avi,
        "best at entry %d / ts:%" GST_TIME_FORMAT " / dur:%" GST_TIME_FORMAT
        " flags:%02x", i, GST_TIME_ARGS (entry->ts), GST_TIME_ARGS (entry->dur),
        entry->flags);
  }

  return last_entry;
}

/* GstElement methods */

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
  GstAviDemux *avidemux = GST_AVI_DEMUX (GST_PAD_PARENT (pad));

  avi_stream_context *stream = gst_pad_get_element_private (pad);

  GST_LOG_OBJECT (avidemux,
      "Received  src_format:%s, src_value:%" G_GUINT64_FORMAT
      ", dest_format:%s", gst_format_get_name (src_format), src_value,
      gst_format_get_name (*dest_format));

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
          *dest_value = gst_util_uint64_scale (src_value,
              (guint64) stream->strf.auds->av_bps, GST_SECOND);
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
            *dest_value = gst_util_uint64_scale (src_value, GST_SECOND,
                (guint64) stream->strf.auds->av_bps);
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
          *dest_value = gst_util_uint64_scale (src_value,
              stream->strh->scale * GST_SECOND, (guint64) stream->strh->rate);
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
  GST_LOG_OBJECT (avidemux,
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
    0
  };

  return src_types;
}

static gboolean
gst_avi_demux_handle_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstAviDemux *avi = GST_AVI_DEMUX (gst_pad_get_parent (pad));

  avi_stream_context *stream = gst_pad_get_element_private (pad);

  if (!stream->strh || !stream->strf.data)
    return gst_pad_query_default (pad, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:{
      gint64 pos = 0;

      GST_DEBUG ("pos query for stream %d: frames %d, bytes %" G_GUINT64_FORMAT,
          stream->num, stream->current_frame, stream->current_byte);

      if (stream->strh->type == GST_RIFF_FCC_auds) {
        if (stream->is_vbr) {
          /* VBR */
          pos = gst_util_uint64_scale ((gint64) stream->current_frame *
              stream->strh->scale, GST_SECOND, (guint64) stream->strh->rate);
          GST_DEBUG_OBJECT (avi, "VBR convert frame %u, time %"
              GST_TIME_FORMAT, stream->current_frame, GST_TIME_ARGS (pos));
        } else if (stream->strf.auds->av_bps != 0) {
          /* CBR */
          pos = gst_util_uint64_scale (stream->current_byte, GST_SECOND,
              (guint64) stream->strf.auds->av_bps);
          GST_DEBUG_OBJECT (avi,
              "CBR convert bytes %" G_GUINT64_FORMAT ", time %" GST_TIME_FORMAT,
              stream->current_byte, GST_TIME_ARGS (pos));
        } else if (stream->total_frames != 0 && stream->total_bytes != 0) {
          /* calculate timestamps based on percentage of length */
          guint64 xlen = avi->avih->us_frame *
              avi->avih->tot_frames * GST_USECOND;

          if (stream->is_vbr) {
            pos = gst_util_uint64_scale (xlen, stream->current_frame,
                stream->total_frames);
            GST_DEBUG_OBJECT (avi, "VBR perc convert frame %u, time %"
                GST_TIME_FORMAT, stream->current_frame, GST_TIME_ARGS (pos));
          } else {
            pos = gst_util_uint64_scale (xlen, stream->current_byte,
                stream->total_bytes);
            GST_DEBUG_OBJECT (avi, "CBR perc convert bytes %" G_GUINT64_FORMAT
                ", time %" GST_TIME_FORMAT, stream->current_byte,
                GST_TIME_ARGS (pos));
          }
        } else {
          /* we don't know */
          res = FALSE;
        }
      } else {
        if (stream->strh->rate != 0) {
          pos = gst_util_uint64_scale ((guint64) stream->current_frame *
              stream->strh->scale, GST_SECOND, (guint64) stream->strh->rate);
        } else {
          pos = stream->current_frame * avi->avih->us_frame * GST_USECOND;
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
      if (stream->strh->type != GST_RIFF_FCC_auds &&
          stream->strh->type != GST_RIFF_FCC_vids) {
        res = FALSE;
        break;
      }
      gst_query_set_duration (query, GST_FORMAT_TIME, stream->duration);
      break;
    }
    case GST_QUERY_SEEKING:{
      GstFormat fmt;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt == GST_FORMAT_TIME) {
        gboolean seekable = TRUE;

        if (avi->streaming) {
          seekable = FALSE;
        } else {
          if (avi->index_entries == NULL) {
            seekable = FALSE;
            /* FIXME: when building index_entried, count keyframes
               if (!(avi->key_frame_ct > 1))
               seekable = FALSE;
             */
          }
        }

        gst_query_set_seeking (query, GST_FORMAT_TIME, seekable,
            0, stream->duration);
        res = TRUE;
      }
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

static gboolean
gst_avi_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstAviDemux *avi = GST_AVI_DEMUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (avi,
      "have event type %s: %p on src pad", GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* handle seeking */
      res = gst_avi_demux_handle_seek (avi, pad, event);
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

  if (gst_adapter_available (avi->adapter) < 8) {
    return FALSE;
  }

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

  if (!gst_avi_demux_peek_chunk_info (avi, tag, size)) {
    return FALSE;
  }
  /* FIXME: shouldn't this check go to gst_avi_demux_peek_chunk_info() already */
  if (!(*size) || (*size) == -1) {
    GST_INFO ("Invalid chunk size %d for tag %" GST_FOURCC_FORMAT,
        *size, GST_FOURCC_ARGS (*tag));
    return FALSE;
  }
  peek_size = (*size + 1) & ~1;
  available = gst_adapter_available (avi->adapter);

  GST_DEBUG ("Need to peek chunk of %d bytes to read chunk %" GST_FOURCC_FORMAT
      ", %d bytes available", *size, GST_FOURCC_ARGS (*tag), available);

  if (available >= (8 + peek_size)) {
    return TRUE;
  } else {
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

  /* riff_parse posts an error */
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
    if (!gst_avi_demux_parse_file_header (GST_ELEMENT (avi), tmp)) {
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

  indexes = g_new (guint64, num + 1);
  for (i = 0; i < num; i++) {
    if (size < 24 + bpe * (i + 1))
      break;
    indexes[i] = GST_READ_UINT64_LE (&data[24 + bpe * i]);
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
 * The buffer will be discarded after use.
 *
 * Returns: TRUE on success, FALSE otherwise. Errors are fatal, we
 *          throw an error, caller should bail out asap.
 */
static gboolean
gst_avi_demux_parse_subindex (GstAviDemux * avi,
    GstBuffer * buf, avi_stream_context * stream, GList ** _entries_list)
{
  guint8 *data = GST_BUFFER_DATA (buf);
  guint16 bpe;
  guint32 num, i;
  guint64 baseoff;
  gst_avi_index_entry *entries, *entry;
  GList *entries_list = NULL;
  GstFormat format = GST_FORMAT_TIME;
  guint size;

  *_entries_list = NULL;

  size = buf ? GST_BUFFER_SIZE (buf) : 0;

  /* check size */
  if (size < 24)
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
    return TRUE;

  if (!(entries = g_try_new (gst_avi_index_entry, num)))
    goto out_of_mem;

  for (i = 0; i < num; i++) {
    gint64 next_ts;

    entry = &entries[i];

    if (size < 24 + bpe * (i + 1))
      break;

    /* fill in */
    entry->offset = baseoff + GST_READ_UINT32_LE (&data[24 + bpe * i]);
    entry->size = GST_READ_UINT32_LE (&data[24 + bpe * i + 4]);
    entry->flags =
        (entry->size & 0x80000000) ? 0 : GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME;
    entry->size &= ~0x80000000;
    entry->index_nr = i;
    entry->stream_nr = stream->num;

    /* stream duration unknown, now we can calculate it */
    if (stream->idx_duration == -1)
      stream->idx_duration = 0;

    /* timestamps */
    entry->ts = stream->idx_duration;
    if (stream->is_vbr) {
      /* VBR get next timestamp */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames + 1, &format, &next_ts);
    } else {
      /* CBR get next timestamp */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes + entry->size, &format, &next_ts);
    }
    /* duration is next - current */
    entry->dur = next_ts - entry->ts;

    /* stream position */
    entry->bytes_before = stream->total_bytes;
    entry->frames_before = stream->total_frames;

    stream->total_bytes += entry->size;
    stream->total_frames++;
    stream->idx_duration = next_ts;

    entries_list = g_list_prepend (entries_list, entry);
  }

  GST_LOG_OBJECT (avi, "Read %d index entries", i);

  gst_buffer_unref (buf);

  if (i > 0) {
    *_entries_list = g_list_reverse (entries_list);
  } else {
    g_free (entries);
  }

  return TRUE;

  /* ERRORS */
too_small:
  {
    GST_ERROR_OBJECT (avi,
        "Not enough data to parse subindex (%d available, 24 needed)", size);
    if (buf)
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
out_of_mem:
  {
    GST_ELEMENT_ERROR (avi, RESOURCE, NO_SPACE_LEFT, (NULL),
        ("Cannot allocate memory for %u*%u=%u bytes",
            (guint) sizeof (gst_avi_index_entry), num,
            (guint) sizeof (gst_avi_index_entry) * num));
    gst_buffer_unref (buf);
    return FALSE;
  }
}

#if 0
/*
 * Read AVI index when streaming
 */
static void
gst_avi_demux_read_subindexes_push (GstAviDemux * avi,
    GList ** index, GList ** alloc_list)
{
  GList *list = NULL;
  guint32 tag = 0, size;
  GstBuffer *buf = NULL;
  gint i, n;

  GST_DEBUG_OBJECT (avi, "gst_avi_demux_read_subindexes_push for %d streams",
      avi->num_streams);

  for (n = 0; n < avi->num_streams; n++) {
    avi_stream_context *stream = &avi->stream[n];

    for (i = 0; stream->indexes[i] != GST_BUFFER_OFFSET_NONE; i++) {
      if (!gst_avi_demux_peek_chunk (avi, &tag, &size))
        continue;
      else if (tag != GST_MAKE_FOURCC ('i', 'x', '0' + stream->num / 10,
              '0' + stream->num % 10)) {
        GST_WARNING_OBJECT (avi, "Not an ix## chunk (%" GST_FOURCC_FORMAT ")",
            GST_FOURCC_ARGS (tag));
        continue;
      }

      avi->offset += 8 + ((size + 1) & ~1);

      buf = gst_buffer_new ();
      GST_BUFFER_DATA (buf) = gst_adapter_take (avi->adapter, size);
      GST_BUFFER_SIZE (buf) = size;

      if (!gst_avi_demux_parse_subindex (avi, buf, stream, &list))
        continue;
      if (list) {
        GST_DEBUG_OBJECT (avi, "  adding %d entries", g_list_length (list));
        *alloc_list = g_list_append (*alloc_list, list->data);
        *index = g_list_concat (*index, list);
      }
    }

    g_free (stream->indexes);
    stream->indexes = NULL;
  }
  GST_DEBUG_OBJECT (avi, "index %s", ((*index) ? "!= 0" : "== 0"));
}
#endif

/*
 * Read AVI index
 */
static void
gst_avi_demux_read_subindexes_pull (GstAviDemux * avi,
    GList ** index, GList ** alloc_list)
{
  GList *list = NULL;
  guint32 tag;
  GstBuffer *buf;
  gint i, n;

  GST_DEBUG_OBJECT (avi, "gst_avi_demux_read_subindexes_pull for %d streams",
      avi->num_streams);

  for (n = 0; n < avi->num_streams; n++) {
    avi_stream_context *stream = &avi->stream[n];

    for (i = 0; stream->indexes[i] != GST_BUFFER_OFFSET_NONE; i++) {
      if (gst_riff_read_chunk (GST_ELEMENT (avi), avi->sinkpad,
              &stream->indexes[i], &tag, &buf) != GST_FLOW_OK)
        continue;
      else if (tag != GST_MAKE_FOURCC ('i', 'x', '0' + stream->num / 10,
              '0' + stream->num % 10)) {
        GST_WARNING_OBJECT (avi, "Not an ix## chunk (%" GST_FOURCC_FORMAT ")",
            GST_FOURCC_ARGS (tag));
        gst_buffer_unref (buf);
        continue;
      }

      if (!gst_avi_demux_parse_subindex (avi, buf, stream, &list))
        continue;
      if (list) {
        GST_DEBUG_OBJECT (avi, "  adding %5d entries, total %2d %5d",
            g_list_length (list), g_list_length (*alloc_list),
            g_list_length (*index));
        *alloc_list = g_list_append (*alloc_list, list->data);
        *index = g_list_concat (*index, list);
      }
    }

    g_free (stream->indexes);
    stream->indexes = NULL;
  }
  GST_DEBUG_OBJECT (avi, "index %s", ((*index) ? "!= 0" : "== 0"));
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
  avi_stream_context *stream = &avi->stream[avi->num_streams];
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
  gboolean got_strh = FALSE, got_strf = FALSE;

  element = GST_ELEMENT_CAST (avi);

  GST_DEBUG_OBJECT (avi, "Parsing stream");

  while (gst_riff_parse_chunk (element, buf, &offset, &tag, &sub)) {
    /* sub can be NULL if the chunk is empty */
    switch (tag) {
      case GST_RIFF_TAG_strh:
        if (got_strh) {
          GST_WARNING_OBJECT (avi, "Ignoring additional strh chunk");
          break;
        }
        if (!gst_riff_parse_strh (element, sub, &stream->strh)) {
          GST_WARNING_OBJECT (avi, "Failed to parse strh chunk");
          goto fail;
        }
        got_strh = TRUE;
        break;
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
            GST_DEBUG_OBJECT (element, "marking video as VBR, res %d", res);
            break;
          case GST_RIFF_FCC_auds:
            stream->is_vbr = (stream->strh->samplesize == 0)
                && stream->strh->scale > 1;
            res =
                gst_riff_parse_strf_auds (element, sub, &stream->strf.auds,
                &stream->extradata);
            GST_DEBUG_OBJECT (element, "marking audio as VBR:%d, res %d",
                stream->is_vbr, res);
            break;
          case GST_RIFF_FCC_iavs:
            stream->is_vbr = TRUE;
            res = gst_riff_parse_strf_iavs (element, sub,
                &stream->strf.iavs, &stream->extradata);
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
        if (!res)
          goto fail;
        got_strf = TRUE;
        break;
      }
      case GST_RIFF_TAG_strd:
        if (stream->initdata)
          gst_buffer_unref (stream->initdata);
        stream->initdata = sub;
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
      default:
        if (tag == GST_MAKE_FOURCC ('i', 'n', 'd', 'x') ||
            tag == GST_MAKE_FOURCC ('i', 'x', '0' + avi->num_streams / 10,
                '0' + avi->num_streams % 10)) {
          g_free (stream->indexes);
          gst_avi_demux_parse_superindex (avi, sub, &stream->indexes);
          stream->superindex = TRUE;
          break;
        }
        GST_WARNING_OBJECT (avi,
            "Unknown stream header tag %" GST_FOURCC_FORMAT ", ignoring",
            GST_FOURCC_ARGS (tag));
        /* fall-through */
      case GST_RIFF_TAG_JUNK:
        if (sub != NULL) {
          gst_buffer_unref (sub);
          sub = NULL;
        }
        break;
    }
  }

  if (!got_strh) {
    GST_ERROR_OBJECT (avi, "Failed to find strh chunk");
    goto fail;
  }

  if (!got_strf) {
    GST_ERROR_OBJECT (avi, "Failed to find strf chunk");
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
      g_assert_not_reached ();
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
  stream->last_flow = GST_FLOW_OK;
  stream->discont = TRUE;
  stream->idx_duration = GST_CLOCK_TIME_NONE;
  stream->hdr_duration = GST_CLOCK_TIME_NONE;
  stream->duration = GST_CLOCK_TIME_NONE;
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
  stream->total_bytes = 0;
  stream->total_frames = 0;
  stream->current_frame = 0;
  stream->current_byte = 0;
  gst_pad_set_element_private (pad, stream);
  avi->num_streams++;
  gst_pad_set_caps (pad, caps);
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (avi), pad);
  GST_LOG_OBJECT (element, "Added pad %s with caps %" GST_PTR_FORMAT,
      GST_PAD_NAME (pad), caps);
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

  return TRUE;

  /* ERRORS */
fail:
  {
    /* unref any mem that may be in use */
    if (buf)
      gst_buffer_unref (buf);
    if (sub)
      gst_buffer_unref (sub);
    g_free (codec_name);
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
        dmlh.totalframes = GUINT32_FROM_LE (_dmlh->totalframes);

        GST_INFO_OBJECT (avi, "dmlh tag found:");
        GST_INFO_OBJECT (avi, " totalframes: %u", dmlh.totalframes);

        avi->avih->tot_frames = dmlh.totalframes;
        goto next;
      }

      default:
        GST_WARNING_OBJECT (avi,
            "Unknown tag %" GST_FOURCC_FORMAT " in ODML header",
            GST_FOURCC_ARGS (tag));
        /* fall-through */
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

/*
 * Sort helper for index entries that sorts by index time.
 * If times are equal we sort by stream number.
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

/*
 * gst_avi_demux_parse_index:
 * @avi: calling element (used for debugging/errors).
 * @buf: buffer containing the full index.
 * @entries_list: list (returned by this function) containing the index
 *                entries parsed from the buffer. The first in the list
 *                is also a pointer to the allocated data and should be
 *                free'ed at some point.
 *
 * Read index entries from the provided buffer. Takes ownership of @buf.
 */
static void
gst_avi_demux_parse_index (GstAviDemux * avi,
    GstBuffer * buf, GList ** _entries_list)
{
  guint64 pos_before = avi->offset;
  gst_avi_index_entry *entries = NULL;
  guint8 *data;
  GList *entries_list = NULL;
  guint i, num, n;

#ifndef GST_DISABLE_DEBUG
  gulong _nr_keyframes = 0;
#endif

  if (!buf || !GST_BUFFER_SIZE (buf)) {
    *_entries_list = NULL;
    GST_DEBUG ("empty index");
    if (buf)
      gst_buffer_unref (buf);
    return;
  }

  data = GST_BUFFER_DATA (buf);
  num = GST_BUFFER_SIZE (buf) / sizeof (gst_riff_index_entry);
  if (!(entries = g_try_new (gst_avi_index_entry, num)))
    goto out_of_mem;

  GST_INFO ("Parsing index, nr_entries = %6d", num);

  for (i = 0, n = 0; i < num; i++) {
    gint64 next_ts;
    gst_riff_index_entry entry, *_entry;
    avi_stream_context *stream;
    guint stream_nr;
    gst_avi_index_entry *target;
    GstFormat format;

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
    if (stream_nr >= avi->num_streams) {
      GST_WARNING_OBJECT (avi,
          "Index entry %d has invalid stream nr %d", i, stream_nr);
      continue;
    }
    target->stream_nr = stream_nr;
    stream = &avi->stream[stream_nr];

    target->index_nr = i;
    target->flags =
        (entry.
        flags & GST_RIFF_IF_KEYFRAME) ? GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME : 0;
    target->size = entry.size;
    target->offset = entry.offset + 8;

    /* figure out if the index is 0 based or relative to the MOVI start */
    if (n == 0) {
      if (target->offset < pos_before)
        avi->index_offset = pos_before + 8;
      else
        avi->index_offset = 0;
      GST_DEBUG ("index_offset = %" G_GUINT64_FORMAT, avi->index_offset);
    }

    format = GST_FORMAT_TIME;
    if (stream->strh->type == GST_RIFF_FCC_auds) {
      /* all audio frames are keyframes */
      target->flags |= GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME;
    }
#ifndef GST_DISABLE_DEBUG
    if (target->flags & GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME)
      _nr_keyframes++;
#endif

    /* stream duration unknown, now we can calculate it */
    if (stream->idx_duration == -1)
      stream->idx_duration = 0;

    /* timestamps */
    target->ts = stream->idx_duration;
    if (stream->is_vbr) {
      /* VBR stream next timestamp */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames + 1, &format, &next_ts);
    } else {
      /* constant rate stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes + target->size, &format, &next_ts);
    }
    /* duration is next - current */
    target->dur = next_ts - target->ts;

    /* stream position */
    target->bytes_before = stream->total_bytes;
    target->frames_before = stream->total_frames;

    stream->total_bytes += target->size;
    stream->total_frames++;
    stream->idx_duration = next_ts;

    GST_DEBUG_OBJECT (avi,
        "Adding index entry %d (%6u), flags %02x, stream %d, size %u "
        ", offset %" G_GUINT64_FORMAT ", time %" GST_TIME_FORMAT ", dur %"
        GST_TIME_FORMAT,
        target->index_nr, stream->total_frames - 1, target->flags,
        target->stream_nr, target->size, target->offset,
        GST_TIME_ARGS (target->ts), GST_TIME_ARGS (target->dur));
    entries_list = g_list_prepend (entries_list, target);

    n++;
  }

  GST_INFO ("Parsed index, %6d entries, %5ld keyframes, entry size = %2d, "
      "total size = %10d", num, _nr_keyframes,
      (gint) sizeof (gst_avi_index_entry),
      (gint) (num * sizeof (gst_avi_index_entry)));

  gst_buffer_unref (buf);

  if (n > 0) {
    *_entries_list = g_list_reverse (entries_list);
  } else {
    g_free (entries);
  }
  return;

  /* ERRORS */
out_of_mem:
  {
    GST_ELEMENT_ERROR (avi, RESOURCE, NO_SPACE_LEFT, (NULL),
        ("Cannot allocate memory for %u*%u=%u bytes",
            (guint) sizeof (gst_avi_index_entry), num,
            (guint) sizeof (gst_avi_index_entry) * num));
    gst_buffer_unref (buf);
  }
}

/*
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
  GstFlowReturn res;
  guint64 offset = avi->offset;
  GstBuffer *buf;
  guint32 tag;
  guint32 size;
  gint i;

  GST_DEBUG ("demux stream index at offset %" G_GUINT64_FORMAT, offset);

  *alloc_list = NULL;
  *index = NULL;

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
        (8 + ((size + 1) & ~1)));
    offset += 8 + ((size + 1) & ~1);
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

  GST_INFO ("will parse index chunk size %u for tag %"
      GST_FOURCC_FORMAT, GST_BUFFER_SIZE (buf), GST_FOURCC_ARGS (tag));

  gst_avi_demux_parse_index (avi, buf, index);
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
        if ((tag & 0xff) >= '0' && (tag & 0xff) <= '9' &&
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

  res = gst_pad_pull_range (avi->sinkpad, offset, 8, &buf);
  if (res != GST_FLOW_OK)
    goto pull_failed;

  bufsize = GST_BUFFER_SIZE (buf);
  if (bufsize != 8)
    goto wrong_size;

  *tag = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf));
  *size = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 4);

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
    if (*tag == GST_RIFF_TAG_LIST)
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
 * @index: list of index entries, returned by this function.
 * @alloc_list: list of allocated data, returned by this function.
 *
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
  GstFormat format;
  guint64 pos = avi->offset;
  guint64 length;
  gint64 tmplength;
  guint32 tag = 0;
  GList *list = NULL;
  guint index_size = 0;

  /* FIXME:
   * - implement non-seekable source support.
   */
  GST_DEBUG_OBJECT (avi,
      "Creating index %s existing index, starting at offset %" G_GUINT64_FORMAT,
      ((*index) ? "with" : "without"), pos);

  format = GST_FORMAT_BYTES;
  if (!gst_pad_query_peer_duration (avi->sinkpad, &format, &tmplength))
    return FALSE;

  length = tmplength;

  if (*index) {
    entry = g_list_last (*index)->data;
    pos = entry->offset + avi->index_offset + entry->size;
    if (entry->size & 1)
      pos++;

    if (pos >= length) {
      GST_LOG_OBJECT (avi, "Complete index, we're done");
      return TRUE;
    }

    GST_LOG_OBJECT (avi, "Incomplete index, seeking to last valid entry @ %"
        G_GUINT64_FORMAT " of %" G_GUINT64_FORMAT " (%"
        G_GUINT64_FORMAT "+%u)", pos, length, entry->offset, entry->size);
  }

  while (TRUE) {
    guint stream_nr;
    guint size = 0;
    gint64 tmpts, tmpnextts;

    res = gst_avi_demux_next_data_buffer (avi, &pos, &tag, &size);
    if (res != GST_FLOW_OK)
      break;

    /* check valid stream */
    stream_nr = CHUNKID_TO_STREAMNR (tag);
    if (stream_nr >= avi->num_streams) {
      GST_WARNING_OBJECT (avi,
          "Index entry has invalid stream nr %d", stream_nr);
      goto next;
    }

    stream = &avi->stream[stream_nr];

    /* pre-allocate */
    if (index_size % 1024 == 0) {
      entries = g_new (gst_avi_index_entry, 1024);
      *alloc_list = g_list_prepend (*alloc_list, entries);
    }
    entry = &entries[index_size % 1024];

    entry->index_nr = index_size++;
    entry->stream_nr = stream_nr;
    entry->flags = GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME;
    entry->offset = pos - avi->index_offset;
    entry->size = size;

    /* timestamps, get timestamps of two consecutive frames to calculate
     * timestamp and duration. */
    format = GST_FORMAT_TIME;
    if (stream->is_vbr) {
      /* VBR stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames, &format, &tmpts);
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames + 1, &format, &tmpnextts);
    } else {
      /* constant rate stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes, &format, &tmpts);
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes + entry->size, &format, &tmpnextts);
    }
    entry->ts = tmpts;
    entry->dur = tmpnextts - tmpts;

    /* stream position */
    entry->bytes_before = stream->total_bytes;
    stream->total_bytes += entry->size;
    entry->frames_before = stream->total_frames;
    stream->total_frames++;
    stream->idx_duration = tmpnextts;

    list = g_list_prepend (list, entry);
    GST_DEBUG_OBJECT (avi, "Added index entry %d (in stream: %d), offset %"
        G_GUINT64_FORMAT ", time %" GST_TIME_FORMAT " for stream %d",
        index_size - 1, entry->frames_before, entry->offset,
        GST_TIME_ARGS (entry->ts), entry->stream_nr);

  next:
    /* update position */
    pos += GST_ROUND_UP_2 (size);
    if (pos > length) {
      GST_WARNING_OBJECT (avi,
          "Stopping index lookup since we are further than EOF");
      break;
    }
  }

  /* FIXME: why is this disabled */
#if 0
  while (gst_avi_demux_sync (avi, &tag, TRUE)) {
    guint stream_nr = CHUNKID_TO_STREAMNR (tag);
    guint8 *data;
    GstFormat format = GST_FORMAT_TIME;

    if (stream_nr >= avi->num_streams)
      goto next;
    stream = &avi->stream[stream_nr];

    /* get chunk size */
    if (gst_bytestream_peek_bytes (riff->bs, &data, 8) != 8)
      goto next;

    /* fill in */
    entry->index_nr = index_size++;
    entry->stream_nr = stream_nr;
    entry->flags = GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME;
    entry->offset = gst_bytestream_tell (riff->bs) + 8 - avi->index_offset;
    entry->size = GST_READ_UINT32_LE (&data[4]);

    /* timestamps */
    if (stream->is_vbr) {
      /* VBR stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames, &format, &entry->ts);
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_DEFAULT,
          stream->total_frames + 1, &format, &entry->dur);
    } else {
      /* constant rate stream */
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes, &format, &entry->ts);
      gst_avi_demux_src_convert (stream->pad, GST_FORMAT_BYTES,
          stream->total_bytes + entry->size, &format, &entry->dur);
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

  GST_DEBUG_OBJECT (avi, "index created, %d items", index_size);

  *index = g_list_concat (*index, g_list_reverse (list));

  return TRUE;
}

/*
 * gst_avi_demux_massage_index:
 * @avi: calling element (used for debugging/errors).
 *
 * We're going to go over each entry in the index and finetune
 * some things we don't like about AVI. For example, a single
 * chunk might be too long. Also, individual streams might be
 * out-of-sync. In the first case, we cut the chunk in several
 * smaller pieces. In the second case, we re-order chunk reading
 * order. The end result should be a smoother playing AVI.
 */
static gboolean
gst_avi_demux_massage_index (GstAviDemux * avi,
    GList * list, GList * alloc_list)
{
  gst_avi_index_entry *entry;
  avi_stream_context *stream;
  guint i;
  GList *node;
  gint64 delay = G_GINT64_CONSTANT (0);

  GST_LOG_OBJECT (avi, "Starting index massage, nr_entries = %d",
      list ? g_list_length (list) : 0);

  if (list) {
#ifndef GST_DISABLE_DEBUG
    guint num_added_total = 0;
    guint num_per_stream[GST_AVI_DEMUX_MAX_STREAMS] = { 0, };
#endif
    GST_LOG_OBJECT (avi,
        "I'm now going to cut large chunks into smaller pieces");

    /* cut chunks in small (seekable) pieces
     * FIXME: this should be a property where a value of
     * GST_CLOCK_TIME_NONE would disable the chunking
     */
#define MAX_DURATION (GST_SECOND / 2)
    for (i = 0; i < avi->num_streams; i++) {
      /* only chop streams that have exactly *one* chunk */
      if (avi->stream[i].total_frames != 1)
        continue;

      for (node = list; node != NULL; node = node->next) {
        entry = node->data;

        if (entry->stream_nr != i)
          continue;

        /* check for max duration of a single buffer. I suppose that
         * the allocation of index entries could be improved. */
        stream = &avi->stream[entry->stream_nr];
        if (entry->dur > MAX_DURATION
            && stream->strh->type == GST_RIFF_FCC_auds) {
          guint32 ideal_size;
          gst_avi_index_entry *entries;
          guint old_size, num_added;
          GList *node2;

          /* cut in 1/10th of a second */
          ideal_size = stream->strf.auds->av_bps / 10;

          /* ensure chunk size is multiple of blockalign */
          if (stream->strf.auds->blockalign > 1)
            ideal_size -= ideal_size % stream->strf.auds->blockalign;

          /* copy index */
          old_size = entry->size;
          num_added = (entry->size - 1) / ideal_size;
          avi->index_size += num_added;
          entries = g_malloc (sizeof (gst_avi_index_entry) * num_added);
          alloc_list = g_list_prepend (alloc_list, entries);
          for (node2 = node->next; node2 != NULL; node2 = node2->next) {
            gst_avi_index_entry *entry2 = node2->data;

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
              list = g_list_insert_before (list, node->next, entry2);
              entry = node->data;
              node = node->next;
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
#ifndef GST_DISABLE_DEBUG
          num_added_total += num_added;
#endif
        }
      }
    }
#ifndef GST_DISABLE_DEBUG
    if (num_added_total)
      GST_LOG ("added %u new index entries", num_added_total);
#endif

    GST_LOG_OBJECT (avi, "I'm now going to reorder the index entries for time");

    /* re-order for time */
    list = g_list_sort (list, (GCompareFunc) sort);

    /* make a continous array out of the list */
    avi->index_size = g_list_length (list);
    avi->index_entries = g_try_new (gst_avi_index_entry, avi->index_size);
    if (!avi->index_entries)
      goto out_of_mem;

    entry = (gst_avi_index_entry *) (list->data);
    delay = entry->ts;

    GST_LOG_OBJECT (avi,
        "Building index array, nr_entries = %d (time offset = %"
        GST_TIME_FORMAT, avi->index_size, GST_TIME_ARGS (delay));

    for (i = 0, node = list; node != NULL; node = node->next, i++) {
      entry = node->data;
      entry->index_nr = i;
      entry->ts -= delay;
      memcpy (&avi->index_entries[i], entry, sizeof (gst_avi_index_entry));
#ifndef GST_DISABLE_DEBUG
      num_per_stream[entry->stream_nr]++;
#endif

      GST_DEBUG ("Sorted index entry %3d for stream %d of size %6u"
          " at offset %7" G_GUINT64_FORMAT ", time %" GST_TIME_FORMAT
          " dur %" GST_TIME_FORMAT,
          avi->index_entries[i].index_nr, entry->stream_nr, entry->size,
          entry->offset, GST_TIME_ARGS (entry->ts), GST_TIME_ARGS (entry->dur));
    }
    if (delay) {
      for (i = 0; i < avi->num_streams; i++) {
        stream = &avi->stream[i];
        stream->idx_duration -= delay;
      }
    }
#ifndef GST_DISABLE_DEBUG
    {
      gchar str[GST_AVI_DEMUX_MAX_STREAMS * (1 + 6 + 2)];
      gchar *pad_name;

      for (i = 0; i < avi->num_streams; i++) {
        pad_name = GST_OBJECT_NAME (avi->stream[i].pad);
        sprintf (&str[i * (1 + 6 + 2)], " %6u %c", num_per_stream[i],
            pad_name[0]);
      }
      GST_LOG_OBJECT (avi, "indizies per stream:%20s", str);
    }
#endif

    GST_LOG_OBJECT (avi, "Freeing original index list");
    /* all the node->data in list point to alloc_list chunks */

    g_list_free (list);
  }
  if (alloc_list) {
    g_list_foreach (alloc_list, (GFunc) g_free, NULL);
    g_list_free (alloc_list);
  }
#ifndef GST_DISABLE_GST_DEBUG
  for (i = 0; i < avi->num_streams; i++) {
    GST_LOG_OBJECT (avi, "Stream %d, %d frames, %8" G_GUINT64_FORMAT " bytes",
        i, avi->stream[i].total_frames, avi->stream[i].total_bytes);
  }
#endif

  GST_LOG_OBJECT (avi, "Index massaging done");
  return TRUE;

  /* ERRORS */
out_of_mem:
  GST_WARNING_OBJECT (avi, "Out of memory for %" G_GSIZE_FORMAT " bytes",
      sizeof (gst_avi_index_entry) * avi->index_size);
  return FALSE;
}

static void
gst_avi_demux_calculate_durations_from_index (GstAviDemux * avi)
{
  gst_avi_index_entry *entry;
  gint stream;
  GstClockTime total;

  total = GST_CLOCK_TIME_NONE;

  /* all streams start at a timestamp 0 */
  for (stream = 0; stream < avi->num_streams; stream++) {
    GstClockTime duration, hduration;
    avi_stream_context *streamc = &avi->stream[stream];
    gst_riff_strh *strh = streamc->strh;

    /* get header duration */
    hduration = gst_util_uint64_scale ((guint64) strh->length *
        strh->scale, GST_SECOND, (guint64) strh->rate);

    GST_INFO ("Stream %d duration according to header: %" GST_TIME_FORMAT,
        stream, GST_TIME_ARGS (hduration));

    if (hduration == 0)
      hduration = GST_CLOCK_TIME_NONE;

    /* set duration for the stream */
    streamc->hdr_duration = hduration;

    /* get last index entry to get duration */
    if ((entry = gst_avi_demux_index_last (avi, stream)))
      duration = entry->ts + entry->dur;
    else
      duration = GST_CLOCK_TIME_NONE;

    streamc->idx_duration = duration;

    /* now pick a good duration */
    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      /* index gave valid duration, use that */
      GST_INFO ("Stream %d duration according to index: %" GST_TIME_FORMAT,
          stream, GST_TIME_ARGS (duration));
    } else {
      /* fall back to header info to calculate a duration */
      duration = hduration;
    }
    /* set duration for the stream */
    streamc->duration = duration;

    /* find total duration */
    if (total == GST_CLOCK_TIME_NONE || duration > total)
      total = duration;
  }

  if (GST_CLOCK_TIME_IS_VALID (total) && (total > 0)) {
    /* now update the duration for those streams where we had none */
    for (stream = 0; stream < avi->num_streams; stream++) {
      avi_stream_context *streamc = &avi->stream[stream];

      if (!GST_CLOCK_TIME_IS_VALID (streamc->duration)
          || streamc->duration == 0) {
        streamc->duration = total;

        GST_INFO ("Stream %d duration according to total: %" GST_TIME_FORMAT,
            stream, GST_TIME_ARGS (total));
      }
    }
  }

  /* and set the total duration in the segment. */
  GST_INFO ("Setting total duration to: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (total));

  gst_segment_set_duration (&avi->segment, GST_FORMAT_TIME, total);
}

static gboolean
gst_avi_demux_push_event (GstAviDemux * avi, GstEvent * event)
{
  gboolean result = FALSE;
  gint i;

  GST_DEBUG_OBJECT (avi, "sending %s event to %d streams",
      GST_EVENT_TYPE_NAME (event), avi->num_streams);

  if (avi->num_streams) {
    for (i = 0; i < avi->num_streams; i++) {
      avi_stream_context *stream = &avi->stream[i];

      if (stream->pad) {
        if (gst_pad_push_event (stream->pad, gst_event_ref (event)))
          result = TRUE;
      }
    }
  }
  gst_event_unref (event);
  return result;
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

  GST_DEBUG ("Reading and parsing avi headers: %d", avi->header_state);

  switch (avi->header_state) {
    case GST_AVI_DEMUX_HEADER_TAG_LIST:
      if (gst_avi_demux_peek_chunk (avi, &tag, &size)) {
        avi->offset += 8 + ((size + 1) & ~1);
        if (tag != GST_RIFF_TAG_LIST)
          goto header_no_list;

        gst_adapter_flush (avi->adapter, 8);
        /* Find the 'hdrl' LIST tag */
        GST_DEBUG ("Reading %d bytes", size);
        buf = gst_adapter_take_buffer (avi->adapter, size);

        if (GST_READ_UINT32_LE (GST_BUFFER_DATA (buf)) != GST_RIFF_LIST_hdrl)
          goto header_no_hdrl;

        GST_DEBUG ("'hdrl' LIST tag found. Parsing next chunk");

        /* the hdrl starts with a 'avih' header */
        if (!gst_riff_parse_chunk (GST_ELEMENT (avi), buf, &offset, &tag, &sub))
          goto header_no_avih;

        if (tag != GST_RIFF_TAG_avih)
          goto header_no_avih;

        if (!gst_avi_demux_parse_avih (GST_ELEMENT (avi), sub, &avi->avih))
          goto header_wrong_avih;

        GST_DEBUG_OBJECT (avi, "AVI header ok, reading elemnts from header");

        /* now, read the elements from the header until the end */
        while (gst_riff_parse_chunk (GST_ELEMENT (avi), buf, &offset, &tag,
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
                    GST_DEBUG_OBJECT (avi, "avi_demux_parse_stream failed");
                    return GST_FLOW_ERROR;
                  }
                  goto next;
                case GST_RIFF_LIST_odml:
                  gst_avi_demux_parse_odml (avi, sub);
                  break;
                default:
                  GST_WARNING_OBJECT (avi,
                      "Unknown list %" GST_FOURCC_FORMAT " in AVI header",
                      GST_FOURCC_ARGS (GST_READ_UINT32_LE (GST_BUFFER_DATA
                              (sub))));
                  /* fall-through */
                case GST_RIFF_TAG_JUNK:
                  goto next;
              }
              break;
            default:
              GST_WARNING_OBJECT (avi,
                  "Unknown off %d tag %" GST_FOURCC_FORMAT " in AVI header",
                  offset, GST_FOURCC_ARGS (tag));
              /* fall-through */
            case GST_RIFF_TAG_JUNK:
            next:
              /* move to next chunk */
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
              avi->offset += 12;
              goto skipping_done;
            case GST_RIFF_LIST_INFO:
              GST_DEBUG ("Found INFO chunk");
              if (gst_avi_demux_peek_chunk (avi, &tag, &size)) {
                avi->offset += 12;
                gst_adapter_flush (avi->adapter, 12);
                buf = gst_adapter_take_buffer (avi->adapter, size - 4);
                gst_riff_parse_info (GST_ELEMENT (avi), buf, &avi->globaltags);
                gst_buffer_unref (buf);

                avi->offset += ((size + 1) & ~1) - 4;
              } else {
                /* Need more data */
                return GST_FLOW_OK;
              }
              break;
            default:
              if (gst_avi_demux_peek_chunk (avi, &tag, &size)) {
                avi->offset += 8 + ((size + 1) & ~1);
                gst_adapter_flush (avi->adapter, 8 + ((size + 1) & ~1));
                // ??? goto iterate; ???
              } else {
                /* Need more data */
                return GST_FLOW_OK;
              }
              break;
          }
        } else {
          if (gst_avi_demux_peek_chunk (avi, &tag, &size)) {
            avi->offset += 8 + ((size + 1) & ~1);
            gst_adapter_flush (avi->adapter, 8 + ((size + 1) & ~1));
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

#if 0
  /*GList *index = NULL, *alloc = NULL; */

  /* ######################## this need to be integrated with the state */
  /* create or read stream index (for seeking) */
  if (avi->stream[0].indexes != NULL) {
    gst_avi_demux_read_subindexes_push (avi, &index, &alloc);
  }
  if (!index) {
    if (avi->avih->flags & GST_RIFF_AVIH_HASINDEX) {
      gst_avi_demux_stream_index (avi, &index, &alloc);
    }
    /* some indexes are incomplete, continue streaming from there */
    if (!index)
      gst_avi_demux_stream_scan (avi, &index, &alloc);
  }

  /* this is a fatal error */
  if (!index)
    goto no_index;

  if (!gst_avi_demux_massage_index (avi, index, alloc))
    goto no_index;

  gst_avi_demux_calculate_durations_from_index (avi);
  /* ######################## */
#endif

  /* create initial NEWSEGMENT event */
  if ((stop = avi->segment.stop) == GST_CLOCK_TIME_NONE)
    stop = avi->segment.duration;

  GST_DEBUG_OBJECT (avi, "segment stop %" G_GINT64_FORMAT, stop);

  if (avi->seek_event)
    gst_event_unref (avi->seek_event);
  avi->seek_event = gst_event_new_new_segment
      (FALSE, avi->segment.rate, GST_FORMAT_TIME,
      avi->segment.start, stop, avi->segment.start);

  /* at this point we know all the streams and we can signal the no more
   * pads signal */
  GST_DEBUG_OBJECT (avi, "signaling no more pads");
  gst_element_no_more_pads (GST_ELEMENT (avi));

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

/*
 * Read full AVI headers.
 */
static GstFlowReturn
gst_avi_demux_stream_header_pull (GstAviDemux * avi)
{
  GstFlowReturn res;
  GstBuffer *buf, *sub = NULL;
  guint32 tag;
  GList *index = NULL, *alloc = NULL;
  guint offset = 4;
  gint64 stop;
  GstElement *element = GST_ELEMENT_CAST (avi);

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

  /* the hdrl starts with a 'avih' header */
  if (!gst_riff_parse_chunk (element, buf, &offset, &tag, &sub))
    goto no_avih;
  else if (tag != GST_RIFF_TAG_avih)
    goto no_avih;
  else if (!gst_avi_demux_parse_avih (element, sub, &avi->avih))
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
            if (!(gst_avi_demux_parse_stream (avi, sub)))
              goto parse_stream_failed;

            goto next;
          case GST_RIFF_LIST_odml:
            gst_avi_demux_parse_odml (avi, sub);
            break;
          default:
            GST_WARNING_OBJECT (avi,
                "Unknown list %" GST_FOURCC_FORMAT " in AVI header",
                GST_FOURCC_ARGS (fourcc));
            /* fall-through */
          case GST_RIFF_TAG_JUNK:
            goto next;
        }
        break;
      }
      default:
        GST_WARNING_OBJECT (avi,
            "Unknown tag %" GST_FOURCC_FORMAT " in AVI header at off %d",
            GST_FOURCC_ARGS (tag), offset);
        /* fall-through */
      case GST_RIFF_TAG_JUNK:
      next:
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

  GST_DEBUG_OBJECT (avi, "skipping junk between header and data ...");

  /* Now, find the data (i.e. skip all junk between header and data) */
  do {
    guint size;
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

    tag = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf));
    size = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 4);
    ltag = GST_READ_UINT32_LE (GST_BUFFER_DATA (buf) + 8);
    gst_buffer_unref (buf);

    if (tag == GST_RIFF_TAG_LIST) {
      switch (ltag) {
        case GST_RIFF_LIST_movi:
          goto skipping_done;
        case GST_RIFF_LIST_INFO:
          res =
              gst_riff_read_chunk (element, avi->sinkpad, &avi->offset, &tag,
              &buf);
          if (res != GST_FLOW_OK) {
            GST_DEBUG_OBJECT (avi, "couldn't read INFO chunk");
            goto pull_range_failed;
          }

          sub = gst_buffer_create_sub (buf, 4, GST_BUFFER_SIZE (buf) - 4);
          gst_riff_parse_info (element, sub, &avi->globaltags);
          if (sub) {
            gst_buffer_unref (sub);
            sub = NULL;
          }
          gst_buffer_unref (buf);
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
skipping_done:

  GST_DEBUG_OBJECT (avi, "skipping done ... (streams=%u, stream[0].indexes=%p)",
      avi->num_streams, avi->stream[0].indexes);

  /* create or read stream index (for seeking) */
  if (avi->stream[0].indexes != NULL) {
    /* we read a super index already (gst_avi_demux_parse_superindex() ) */
    gst_avi_demux_read_subindexes_pull (avi, &index, &alloc);
  }
  if (!index) {
    if (avi->avih->flags & GST_RIFF_AVIH_HASINDEX) {
      gst_avi_demux_stream_index (avi, &index, &alloc);
    }
    /* some indexes are incomplete, continue streaming from there */
    if (!index)
      gst_avi_demux_stream_scan (avi, &index, &alloc);
  }

  /* this is a fatal error */
  if (!index)
    goto no_index;

  if (!gst_avi_demux_massage_index (avi, index, alloc))
    goto no_index;

  gst_avi_demux_calculate_durations_from_index (avi);

  /* create initial NEWSEGMENT event */
  if ((stop = avi->segment.stop) == GST_CLOCK_TIME_NONE)
    stop = avi->segment.duration;

  GST_DEBUG_OBJECT (avi, "segment stop %" G_GINT64_FORMAT, stop);

  if (avi->seek_event)
    gst_event_unref (avi->seek_event);
  avi->seek_event = gst_event_new_new_segment
      (FALSE, avi->segment.rate, GST_FORMAT_TIME,
      avi->segment.start, stop, avi->segment.start);

  /* at this point we know all the streams and we can signal the no more
   * pads signal */
  GST_DEBUG_OBJECT (avi, "signaling no more pads");
  gst_element_no_more_pads (GST_ELEMENT_CAST (avi));

  return GST_FLOW_OK;

  /* ERRORS */
parse_stream_failed:
  {
    GST_DEBUG_OBJECT (avi, "avi_demux_parse_stream failed");
    return GST_FLOW_ERROR;
  }
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
    if (sub)
      gst_buffer_unref (sub);
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
    g_list_free (index);
    g_list_foreach (alloc, (GFunc) g_free, NULL);
    g_list_free (alloc);

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

/*
 * Do the actual seeking.
 */
static gboolean
gst_avi_demux_do_seek (GstAviDemux * avi, GstSegment * segment)
{
  GstClockTime seek_time;
  gboolean keyframe;
  gst_avi_index_entry *entry, *kentry;
  gint old_entry;

  seek_time = segment->last_stop;
  keyframe = !!(segment->flags & GST_SEEK_FLAG_KEY_UNIT);

  /* FIXME: if we seek in an openDML file, we will have multiple
   * primary levels. Seeking in between those will cause havoc. */

  /* save old position so we can see if we must mark a discont. */
  old_entry = avi->current_entry;

  /* get the entry for the requested position, which is always in last_stop.
   * we search the index entry for stream 0, since all entries are sorted by
   * time and stream we automagically are positioned for the other streams as
   * well. FIXME, this code assumes the main stream with keyframes is stream 0,
   * which is mostly correct... */
  if (!(entry = gst_avi_demux_index_entry_for_time (avi, 0, seek_time)))
    goto no_entry;

  GST_DEBUG_OBJECT (avi,
      "Got requested entry %d [stream:%d / ts:%" GST_TIME_FORMAT
      " / duration:%" GST_TIME_FORMAT "]", entry->index_nr,
      entry->stream_nr, GST_TIME_ARGS (entry->ts), GST_TIME_ARGS (entry->dur));

  /* check if we are already on a keyframe */
  if (!(entry->flags & GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME)) {
    /* now go to the previous keyframe, this is where we should start
     * decoding from. */
    if (!(kentry = gst_avi_demux_index_prev (avi, 0, entry->index_nr,
                GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME))) {
      goto no_entry;
    }
  } else {
    /* we were on a keyframe */
    kentry = entry;
  }

  GST_DEBUG_OBJECT (avi,
      "Got keyframe entry %d [stream:%d / ts:%" GST_TIME_FORMAT
      " / duration:%" GST_TIME_FORMAT "]", kentry->index_nr,
      entry->stream_nr, GST_TIME_ARGS (kentry->ts),
      GST_TIME_ARGS (kentry->dur));

  /* we must start decoding at the keyframe */
  avi->current_entry = kentry->index_nr;

  if (segment->rate < 0.0) {
    /* play between the keyframe and the destination entry */
    avi->reverse_start_index = kentry->index_nr;
    avi->reverse_stop_index = entry->index_nr;

    GST_DEBUG_OBJECT (avi, "reverse seek: start idx (%d) and stop idx (%d)",
        avi->reverse_start_index, avi->reverse_stop_index);
  }

  if (keyframe) {
    /* when seeking to a keyframe, we update the result seek time
     * to the time of the keyframe. */
    seek_time = avi->index_entries[avi->current_entry].ts;
  }

next:
  /* if we changed position, mark a DISCONT on all streams */
  if (avi->current_entry != old_entry) {
    gint i;

    for (i = 0; i < avi->num_streams; i++) {
      avi->stream[i].discont = TRUE;
    }
  }

  GST_DEBUG_OBJECT (avi, "seek: %" GST_TIME_FORMAT
      " keyframe seeking:%d", GST_TIME_ARGS (seek_time), keyframe);

  /* the seek time is also the last_stop and stream time */
  segment->last_stop = seek_time;
  segment->time = seek_time;

  return TRUE;

no_entry:
  {
    /* we could not find an entry for the given time */
    GST_WARNING_OBJECT (avi,
        "Couldn't find AviIndexEntry for time:%" GST_TIME_FORMAT,
        GST_TIME_ARGS (seek_time));
    if (avi->current_entry >= avi->index_size && avi->index_size > 0)
      avi->current_entry = avi->index_size - 1;

    goto next;
  }
}

/*
 * Handle seek event.
 */
static gboolean
gst_avi_demux_handle_seek (GstAviDemux * avi, GstPad * pad, GstEvent * event)
{
  gboolean res;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type = GST_SEEK_TYPE_NONE, stop_type;
  gint64 cur, stop;
  gboolean flush;
  gboolean update;
  GstSegment seeksegment = { 0, };

  if (event) {
    GST_DEBUG_OBJECT (avi, "doing seek with event");

    gst_event_parse_seek (event, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    /* we have to have a format as the segment format. Try to convert
     * if not. */
    if (format != GST_FORMAT_TIME) {
      GstFormat fmt;

      fmt = GST_FORMAT_TIME;
      res = TRUE;
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
    GstEvent *event = gst_event_new_flush_start ();

    /* for a flushing seek, we send a flush_start on all pads. This will
     * eventually stop streaming with a WRONG_STATE. We can thus eventually
     * take the STREAM_LOCK. */
    GST_DEBUG_OBJECT (avi, "sending flush start");
    gst_avi_demux_push_event (avi, gst_event_ref (event));
    gst_pad_push_event (avi->sinkpad, event);
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
  res = gst_avi_demux_do_seek (avi, &seeksegment);

  if (flush) {
    gint i;

    GST_DEBUG_OBJECT (avi, "sending flush stop");
    gst_avi_demux_push_event (avi, gst_event_new_flush_stop ());
    gst_pad_push_event (avi->sinkpad, gst_event_new_flush_stop ());
    /* reset the last flow and mark discont, FLUSH is always DISCONT */
    for (i = 0; i < avi->num_streams; i++) {
      avi->stream[i].last_flow = GST_FLOW_OK;
      avi->stream[i].discont = TRUE;
    }
  } else if (avi->segment_running) {
    GstEvent *seg;

    /* we are running the current segment and doing a non-flushing seek,
     * close the segment first based on the last_stop. */
    GST_DEBUG_OBJECT (avi, "closing running segment %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, avi->segment.start, avi->segment.last_stop);
    seg = gst_event_new_new_segment (TRUE,
        avi->segment.rate, avi->segment.format,
        avi->segment.start, avi->segment.last_stop, avi->segment.time);
    gst_avi_demux_push_event (avi, seg);
  }

  /* now update the real segment info */
  memcpy (&avi->segment, &seeksegment, sizeof (GstSegment));

  /* post the SEGMENT_START message when we do segmented playback */
  if (avi->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    gst_element_post_message (GST_ELEMENT (avi),
        gst_message_new_segment_start (GST_OBJECT (avi),
            avi->segment.format, avi->segment.last_stop));
  }

  /* prepare for streaming again */
  if ((stop = avi->segment.stop) == GST_CLOCK_TIME_NONE)
    stop = avi->segment.duration;

  /* queue the segment event for the streaming thread. */
  if (avi->seek_event)
    gst_event_unref (avi->seek_event);
  if (avi->segment.rate > 0.0) {
    avi->seek_event = gst_event_new_new_segment (FALSE,
        avi->segment.rate, avi->segment.format,
        avi->segment.last_stop, stop, avi->segment.time);
  } else {
    avi->seek_event = gst_event_new_new_segment (FALSE,
        avi->segment.rate, avi->segment.format,
        avi->segment.start, avi->segment.last_stop, avi->segment.start);
  }

  if (!avi->streaming) {
    avi->segment_running = TRUE;
    gst_pad_start_task (avi->sinkpad, (GstTaskFunction) gst_avi_demux_loop,
        avi->sinkpad);
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
 * Helper for gst_avi_demux_invert()
 */
static inline void
swap_line (guint8 * d1, guint8 * d2, guint8 * tmp, gint bytes)
{
  memcpy (tmp, d1, bytes);
  memcpy (d1, d2, bytes);
  memcpy (d2, tmp, bytes);
}

/*
 * Invert DIB buffers... Takes existing buffer and
 * returns either the buffer or a new one (with old
 * one dereferenced).
 * FIXME: can't we preallocate tmp? and remember stride, bpp?
 */
static GstBuffer *
gst_avi_demux_invert (avi_stream_context * stream, GstBuffer * buf)
{
  GstStructure *s;
  gint y, h = stream->strf.vids->height;
  gint bpp, stride;
  guint8 *tmp = NULL;

  s = gst_caps_get_structure (GST_PAD_CAPS (stream->pad), 0);
  if (!gst_structure_get_int (s, "bpp", &bpp)) {
    GST_WARNING ("Failed to retrieve depth from caps");
    return buf;
  }

  stride = stream->strf.vids->width * (bpp / 8);

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

/*
 * Returns the aggregated GstFlowReturn.
 */
static GstFlowReturn
gst_avi_demux_combine_flows (GstAviDemux * avi, avi_stream_context * stream,
    GstFlowReturn ret)
{
  guint i;

  /* store the value */
  stream->last_flow = ret;

  /* any other error that is not-linked can be returned right away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (i = 0; i < avi->num_streams; i++) {
    avi_stream_context *ostream = &avi->stream[i];

    ret = ostream->last_flow;
    /* some other return value (must be SUCCESS but we can return
     * other values as well) */
    if (ret != GST_FLOW_NOT_LINKED)
      goto done;
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
done:
  GST_LOG_OBJECT (avi, "combined return %s", gst_flow_get_name (ret));
  return ret;
}

/*
 * Read data from one index entry
 */
static GstFlowReturn
gst_avi_demux_process_next_entry (GstAviDemux * avi)
{
  GstFlowReturn res = GST_FLOW_OK;
  gboolean processed = FALSE;
  avi_stream_context *stream;
  gst_avi_index_entry *entry;
  GstBuffer *buf;
  gint i;

  do {
    /* see if we are at the end */
    if ((avi->segment.rate > 0 && avi->current_entry >= avi->index_size))
      goto eos;

    /* get next entry, this will work as we checked for the index size above */
    entry = &avi->index_entries[avi->current_entry++];

    /* check for reverse playback */
    if (avi->segment.rate < 0 && avi->current_entry > avi->reverse_stop_index) {
      GST_LOG_OBJECT (avi, "stop_index %d reached", avi->reverse_stop_index);
      avi->reverse_stop_index = avi->reverse_start_index;
      if (avi->reverse_start_index == 0) {
        GST_DEBUG_OBJECT (avi, "start_index was 0, sending eos");
        goto eos;
      }
      entry =
          gst_avi_demux_index_prev (avi, 0, avi->reverse_stop_index,
          GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME);
      if (!entry) {
        GST_DEBUG_OBJECT (avi, "no valid index entry found index %d",
            avi->reverse_stop_index);
        goto eos;
      }
      avi->current_entry = avi->reverse_start_index = entry->index_nr;
      GST_DEBUG_OBJECT (avi,
          "reverse playback jump: start idx (%d) and stop idx (%d)",
          avi->reverse_start_index, avi->reverse_stop_index);
      gst_segment_set_last_stop (&avi->segment, GST_FORMAT_TIME, entry->ts);
      for (i = 0; i < avi->num_streams; i++) {
        avi->stream[i].last_flow = GST_FLOW_OK;
        avi->stream[i].discont = TRUE;
      }
      avi->current_entry++;
    }

    /* see if we have a valid stream, ignore if not
     * FIXME: can't we check this when building the index?
     *   we check it in _parse_index(), _stream_scan()
     */
    if (entry->stream_nr >= avi->num_streams) {
      GST_WARNING_OBJECT (avi,
          "Entry %d has non-existing stream nr %d",
          avi->current_entry - 1, entry->stream_nr);
      continue;
    }

    /* get stream now */
    stream = &avi->stream[entry->stream_nr];

    if (avi->segment.rate > 0.0) {
      /* only check this for fowards playback for now */
      if ((entry->flags & GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME)
          && GST_CLOCK_TIME_IS_VALID (entry->ts)
          && GST_CLOCK_TIME_IS_VALID (avi->segment.stop)
          && (entry->ts > avi->segment.stop)) {
        goto eos_stop;
      }
    }

    /* skip empty entries */
    if (entry->size == 0 || !stream->pad) {
      GST_DEBUG_OBJECT (avi, "Skipping entry %d (%d, %p)",
          avi->current_entry - 1, entry->size, stream->pad);
      goto next;
    }

    GST_LOG ("reading buffer (size=%d) from stream %d at current pos %"
        G_GUINT64_FORMAT " (%llx)", entry->size, entry->stream_nr,
        avi->index_offset + entry->offset, avi->index_offset + entry->offset);

    /* pull in the data */
    res = gst_pad_pull_range (avi->sinkpad, entry->offset +
        avi->index_offset, entry->size, &buf);
    if (res != GST_FLOW_OK)
      goto pull_failed;

    /* check for short buffers, this is EOS as well */
    if (GST_BUFFER_SIZE (buf) < entry->size)
      goto short_buffer;

    /* invert the picture if needed */
    if (stream->strh->fcc_handler == GST_MAKE_FOURCC ('D', 'I', 'B', ' '))
      buf = gst_avi_demux_invert (stream, buf);

    /* mark non-keyframes */
    if (!(entry->flags & GST_AVI_INDEX_ENTRY_FLAG_KEYFRAME))
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

    GST_BUFFER_TIMESTAMP (buf) = entry->ts;
    GST_BUFFER_DURATION (buf) = entry->dur;
    GST_BUFFER_OFFSET (buf) = GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;
    gst_buffer_set_caps (buf, GST_PAD_CAPS (stream->pad));

    GST_DEBUG_OBJECT (avi, "Pushing buffer of size %d and time %"
        GST_TIME_FORMAT " on pad %s",
        GST_BUFFER_SIZE (buf), GST_TIME_ARGS (entry->ts),
        GST_PAD_NAME (stream->pad));

    /* update current position in the segment */
    gst_segment_set_last_stop (&avi->segment, GST_FORMAT_TIME, entry->ts);

    /* mark discont when pending */
    if (stream->discont) {
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;
    }

    res = gst_pad_push (stream->pad, buf);

    /* combine flows */
    res = gst_avi_demux_combine_flows (avi, stream, res);

    /* mark as processed, we increment the frame and byte counters then
     * leave the while loop and return the GstFlowReturn */
    processed = TRUE;
    GST_DEBUG_OBJECT (avi, "Processed buffer %d: %s", entry->index_nr,
        gst_flow_get_name (res));

  next:
    stream->current_frame = entry->frames_before + 1;
    stream->current_byte = entry->bytes_before + entry->size;
  } while (!processed);

beach:
  GST_DEBUG_OBJECT (avi, "returning %s", gst_flow_get_name (res));

  return res;

  /* ERRORS */
eos:
  {
    GST_LOG_OBJECT (avi, "Handled last index entry, setting EOS (%d > %d)",
        avi->current_entry, avi->index_size);
    /* we mark the first stream as EOS */
    res = GST_FLOW_UNEXPECTED;
    goto beach;
  }
eos_stop:
  {
    GST_LOG_OBJECT (avi, "Found keyframe after segment,"
        " setting EOS (%" GST_TIME_FORMAT " > %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (entry->ts), GST_TIME_ARGS (avi->segment.stop));
    res = GST_FLOW_UNEXPECTED;
    goto beach;
  }
pull_failed:
  {
    GST_DEBUG_OBJECT (avi,
        "pull range failed: pos=%" G_GUINT64_FORMAT " size=%d",
        entry->offset + avi->index_offset, entry->size);
    goto beach;
  }
short_buffer:
  {
    GST_WARNING_OBJECT (avi, "Short read at offset %" G_GUINT64_FORMAT
        ", only got %d/%d bytes (truncated file?)", entry->offset +
        avi->index_offset, GST_BUFFER_SIZE (buf), entry->size);
    gst_buffer_unref (buf);
    res = GST_FLOW_UNEXPECTED;
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

  /* if we have a avi->index_entries[], we don't want to read
   * the stream linearly, but seek to the next ts/index_entry. */
  if (avi->index_entries != NULL)
    return gst_avi_demux_process_next_entry (avi);

  if (avi->have_eos) {
    /* Clean adapter, we're done */
    gst_adapter_clear (avi->adapter);
    return res;
  }

  /*
     if (!gst_avi_demux_sync (avi, &tag, FALSE))
     return FALSE;
   */

  /* Iterate until need more data, so adapter won't grow too much */
  while (1) {
    if (!gst_avi_demux_peek_chunk_info (avi, &tag, &size)) {
      return GST_FLOW_OK;
    }

    GST_DEBUG ("Trying chunk (%" GST_FOURCC_FORMAT "), size %d",
        GST_FOURCC_ARGS (tag), size);

    if ((tag & 0xff) >= '0' && (tag & 0xff) <= '9' &&
        ((tag >> 8) & 0xff) >= '0' && ((tag >> 8) & 0xff) <= '9') {
      GST_LOG ("Chunk ok");
    } else if ((tag & 0xffff) == (('x' << 8) | 'i')) {
      GST_DEBUG ("Found sub-index tag");
      if (gst_avi_demux_peek_chunk (avi, &tag, &size)) {
        if ((size > 0) && (size != -1)) {
          GST_DEBUG ("  skipping %d bytes for now", size);
          gst_adapter_flush (avi->adapter, 8 + size);
        }
      }
      return GST_FLOW_OK;
    } else if (tag == GST_RIFF_TAG_idx1) {
      GST_DEBUG ("Found index tag, stream done");
      gst_avi_demux_push_event (avi, gst_event_new_eos ());
      avi->have_eos = TRUE;
      return GST_FLOW_OK;
    } else {
      GST_DEBUG ("No more stream chunks, send EOS");
      gst_avi_demux_push_event (avi, gst_event_new_eos ());
      avi->have_eos = TRUE;
      return GST_FLOW_OK;
    }

    if (!gst_avi_demux_peek_chunk (avi, &tag, &size)) {
      if ((size == 0) || (size == -1))
        gst_adapter_flush (avi->adapter, 8);
      return GST_FLOW_OK;
    }
    GST_DEBUG ("chunk ID %" GST_FOURCC_FORMAT ", size %u",
        GST_FOURCC_ARGS (tag), size);

    stream_nr = CHUNKID_TO_STREAMNR (tag);

    if (stream_nr < 0 || stream_nr >= avi->num_streams) {
      /* recoverable */
      GST_WARNING ("Invalid stream ID %d (%" GST_FOURCC_FORMAT ")",
          stream_nr, GST_FOURCC_ARGS (tag));
      avi->offset += 8 + ((size + 1) & ~1);
      gst_adapter_flush (avi->adapter, 8 + ((size + 1) & ~1));
    } else {
      avi_stream_context *stream;
      GstClockTime next_ts = 0;
      GstBuffer *buf;

      gst_adapter_flush (avi->adapter, 8);

      /* get buffer */
      buf = gst_adapter_take_buffer (avi->adapter, ((size + 1) & ~1));
      /* patch the size */
      GST_BUFFER_SIZE (buf) = size;
      avi->offset += 8 + ((size + 1) & ~1);

      /* get time of this buffer */
      stream = &avi->stream[stream_nr];
      gst_pad_query_position (stream->pad, &format, (gint64 *) & next_ts);
      if (format != GST_FORMAT_TIME)
        goto wrong_format;

      /* set delay (if any)
         if (stream->strh->init_frames == stream->current_frame &&
         stream->delay == 0)
         stream->delay = next_ts;
       */

      stream->current_frame++;
      stream->current_byte += size;

      if (!stream->pad) {
        GST_WARNING ("No pad.");
        gst_buffer_unref (buf);
      } else {
        GstClockTime dur_ts = 0;

        /* invert the picture if needed */
        if (stream->strh->fcc_handler == GST_MAKE_FOURCC ('D', 'I', 'B', ' ')) {
          buf = gst_avi_demux_invert (stream, buf);
        }

        gst_pad_query_position (stream->pad, &format, (gint64 *) & dur_ts);
        if (format != GST_FORMAT_TIME)
          goto wrong_format;

        GST_BUFFER_TIMESTAMP (buf) = next_ts;
        GST_BUFFER_DURATION (buf) = dur_ts - next_ts;
        gst_buffer_set_caps (buf, GST_PAD_CAPS (stream->pad));
        GST_DEBUG_OBJECT (avi,
            "Pushing buffer with time=%" GST_TIME_FORMAT
            " and size %d over pad %s", GST_TIME_ARGS (next_ts), size,
            GST_PAD_NAME (stream->pad));

        /* update current position in the segment */
        gst_segment_set_last_stop (&avi->segment, GST_FORMAT_TIME, next_ts);

        /* mark discont when pending */
        if (stream->discont) {
          GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
          stream->discont = FALSE;
        }
        res = gst_pad_push (stream->pad, buf);

        /* combine flows */
        res = gst_avi_demux_combine_flows (avi, stream, res);
        if (res != GST_FLOW_OK) {
          GST_DEBUG ("Push failed; %s", gst_flow_get_name (res));
          return res;
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

  if (!avi->got_tags)
    return;

  GST_DEBUG_OBJECT (avi, "Pushing pending tag lists");

  for (i = 0; i < avi->num_streams; i++)
    if (avi->stream[i].pad && avi->stream[i].taglist) {
      gst_element_found_tags_for_pad (GST_ELEMENT (avi), avi->stream[i].pad,
          avi->stream[i].taglist);
      avi->stream[i].taglist = NULL;
    }
  if (avi->globaltags) {
    gst_element_found_tags (GST_ELEMENT (avi), avi->globaltags);
    avi->globaltags = NULL;
  }
  avi->got_tags = FALSE;
  GST_DEBUG_OBJECT (avi, "Pushed tag lists");
}

static void
gst_avi_demux_loop (GstPad * pad)
{
  GstFlowReturn res;
  GstAviDemux *avi = GST_AVI_DEMUX (GST_PAD_PARENT (pad));

  switch (avi->state) {
    case GST_AVI_DEMUX_START:
      if ((res = gst_avi_demux_stream_init_pull (avi)) != GST_FLOW_OK) {
        GST_WARNING ("stream_init flow: %s", gst_flow_get_name (res));
        goto pause;
      }
      avi->state = GST_AVI_DEMUX_HEADER;
      /* fall-through */
    case GST_AVI_DEMUX_HEADER:
      if ((res = gst_avi_demux_stream_header_pull (avi)) != GST_FLOW_OK) {
        GST_WARNING ("stream_header flow: %s", gst_flow_get_name (res));
        goto pause;
      }
      avi->state = GST_AVI_DEMUX_MOVI;
      break;
    case GST_AVI_DEMUX_MOVI:
      if (G_UNLIKELY (avi->seek_event)) {
        gst_avi_demux_push_event (avi, avi->seek_event);
        avi->seek_event = NULL;
      }
      if (G_UNLIKELY (avi->got_tags)) {
        push_tag_lists (avi);
      }
      /* process each index entry in turn */
      res = gst_avi_demux_stream_data (avi);

      /* pause when error */
      if (res != GST_FLOW_OK) {
        GST_INFO ("stream_movi flow: %s", gst_flow_get_name (res));
        goto pause;
      }
      break;
    default:
      GST_ERROR_OBJECT (avi, "unknown state %d", avi->state);
      res = GST_FLOW_ERROR;
      goto pause;
  }

  GST_LOG_OBJECT (avi, "state: %d res:%s", avi->state, gst_flow_get_name (res));

  return;

  /* ERRORS */
pause:
  GST_LOG_OBJECT (avi, "pausing task, reason %s", gst_flow_get_name (res));
  avi->segment_running = FALSE;
  gst_pad_pause_task (avi->sinkpad);

  if (GST_FLOW_IS_FATAL (res) || (res == GST_FLOW_NOT_LINKED)) {
    gboolean push_eos = TRUE;

    if (res == GST_FLOW_UNEXPECTED) {
      /* handle end-of-stream/segment */
      if (avi->segment.flags & GST_SEEK_FLAG_SEGMENT) {
        gint64 stop;

        if ((stop = avi->segment.stop) == -1)
          stop = avi->segment.duration;

        GST_INFO_OBJECT (avi, "sending segment_done");

        gst_element_post_message
            (GST_ELEMENT (avi),
            gst_message_new_segment_done (GST_OBJECT (avi), GST_FORMAT_TIME,
                stop));
        push_eos = FALSE;
      }
    } else {
      /* for fatal errors we post an error message */
      GST_ELEMENT_ERROR (avi, STREAM, FAILED,
          (_("Internal data stream error.")),
          ("streaming stopped, reason %s", gst_flow_get_name (res)));
    }
    if (push_eos) {
      GST_INFO_OBJECT (avi, "sending eos");
      if (!(gst_avi_demux_push_event (avi, gst_event_new_eos ()))) {
        /* if we don't error out here it will hang */
        GST_ELEMENT_ERROR (avi, STREAM, FAILED,
            (_("Internal data stream error.")),
            ("downstream did not handle EOS"));
      }
    }
  }
}


static GstFlowReturn
gst_avi_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn res;
  GstAviDemux *avi = GST_AVI_DEMUX (GST_PAD_PARENT (pad));

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
      if (G_UNLIKELY (avi->seek_event)) {
        gst_avi_demux_push_event (avi, avi->seek_event);
        avi->seek_event = NULL;
      }
      if (G_UNLIKELY (avi->got_tags)) {
        push_tag_lists (avi);
      }
      res = gst_avi_demux_stream_data (avi);
      break;
    default:
      GST_ELEMENT_ERROR (avi, STREAM, FAILED, (NULL),
          ("Illegal internal state"));
      res = GST_FLOW_ERROR;
      break;
  }

  GST_DEBUG_OBJECT (avi, "state: %d res:%s", avi->state,
      gst_flow_get_name (res));

  return res;
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

  if (active) {
    GST_DEBUG ("avi: activating push/chain function");
  } else {
    GST_DEBUG ("avi: deactivating push/chain function");
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
      gst_avi_demux_reset (avi);
      break;
    default:
      break;
  }

done:
  return ret;
}
