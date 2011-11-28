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

/**
 * SECTION:element-mad
 * @see_also: lame
 *
 * MP3 audio decoder.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch filesrc location=music.mp3 ! mad ! audioconvert ! audioresample ! autoaudiosink
 * ]| Decode the mp3 file and play
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include "gstmad.h"
#include <gst/audio/audio.h>

enum
{
  ARG_0,
  ARG_HALF,
  ARG_IGNORE_CRC
};

GST_DEBUG_CATEGORY_STATIC (mad_debug);
#define GST_CAT_DEFAULT mad_debug

static GstStaticPadTemplate mad_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean) true, "
        "width = (int) 32, "
        "depth = (int) 32, "
        "rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]")
    );

/* FIXME: make three caps, for mpegversion 1, 2 and 2.5 */
static GstStaticPadTemplate mad_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]")
    );

static void gst_mad_dispose (GObject * object);
static void gst_mad_clear_queues (GstMad * mad);

static void gst_mad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_mad_src_event (GstPad * pad, GstEvent * event);

static const GstQueryType *gst_mad_get_query_types (GstPad * pad);

static gboolean gst_mad_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_mad_convert_sink (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_mad_convert_src (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

static gboolean gst_mad_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_mad_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_mad_chain_reverse (GstMad * mad, GstBuffer * buf);

static GstStateChangeReturn gst_mad_change_state (GstElement * element,
    GstStateChange transition);

static void gst_mad_set_index (GstElement * element, GstIndex * index);
static GstIndex *gst_mad_get_index (GstElement * element);

GST_BOILERPLATE (GstMad, gst_mad, GstElement, GST_TYPE_ELEMENT);

/*
#define GST_TYPE_MAD_LAYER (gst_mad_layer_get_type())
static GType
gst_mad_layer_get_type (void)
{
  static GType mad_layer_type = 0;
  static GEnumValue mad_layer[] = {
    {0, "Unknown", "unknown"},
    {MAD_LAYER_I, "Layer I", "1"},
    {MAD_LAYER_II, "Layer II", "2"},
    {MAD_LAYER_III, "Layer III", "3"},
    {0, NULL, NULL},
  };

  if (!mad_layer_type) {
    mad_layer_type = g_enum_register_static ("GstMadLayer", mad_layer);
  }
  return mad_layer_type;
}
*/

#define GST_TYPE_MAD_MODE (gst_mad_mode_get_type())
static GType
gst_mad_mode_get_type (void)
{
  static GType mad_mode_type = 0;
  static GEnumValue mad_mode[] = {
    {-1, "Unknown", "unknown"},
    {MAD_MODE_SINGLE_CHANNEL, "Mono", "mono"},
    {MAD_MODE_DUAL_CHANNEL, "Dual Channel", "dual"},
    {MAD_MODE_JOINT_STEREO, "Joint Stereo", "joint"},
    {MAD_MODE_STEREO, "Stereo", "stereo"},
    {0, NULL, NULL},
  };

  if (!mad_mode_type) {
    mad_mode_type = g_enum_register_static ("GstMadMode", mad_mode);
  }
  return mad_mode_type;
}

#define GST_TYPE_MAD_EMPHASIS (gst_mad_emphasis_get_type())
static GType
gst_mad_emphasis_get_type (void)
{
  static GType mad_emphasis_type = 0;
  static GEnumValue mad_emphasis[] = {
    {-1, "Unknown", "unknown"},
    {MAD_EMPHASIS_NONE, "None", "none"},
    {MAD_EMPHASIS_50_15_US, "50/15 Microseconds", "50-15"},
    {MAD_EMPHASIS_CCITT_J_17, "CCITT J.17", "j-17"},
    {MAD_EMPHASIS_RESERVED, "Reserved", "reserved"},
    {0, NULL, NULL},
  };

  if (!mad_emphasis_type) {
    mad_emphasis_type = g_enum_register_static ("GstMadEmphasis", mad_emphasis);
  }
  return mad_emphasis_type;
}

static void
gst_mad_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &mad_sink_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &mad_src_template_factory);
  gst_element_class_set_details_simple (element_class, "mad mp3 decoder",
      "Codec/Decoder/Audio",
      "Uses mad code to decode mp3 streams", "Wim Taymans <wim@fluendo.com>");
}

static void
gst_mad_class_init (GstMadClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_mad_set_property;
  gobject_class->get_property = gst_mad_get_property;
  gobject_class->dispose = gst_mad_dispose;

  gstelement_class->change_state = gst_mad_change_state;
  gstelement_class->set_index = gst_mad_set_index;
  gstelement_class->get_index = gst_mad_get_index;

  /* init properties */
  /* currently, string representations are used, we might want to change that */
  /* FIXME: descriptions need to be more technical,
   * default values and ranges need to be selected right */
  g_object_class_install_property (gobject_class, ARG_HALF,
      g_param_spec_boolean ("half", "Half", "Generate PCM at 1/2 sample rate",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_IGNORE_CRC,
      g_param_spec_boolean ("ignore-crc", "Ignore CRC", "Ignore CRC errors",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* register tags */
#define GST_TAG_LAYER    "layer"
#define GST_TAG_MODE     "mode"
#define GST_TAG_EMPHASIS "emphasis"

  /* FIXME 0.11: strings!? why? */
  gst_tag_register (GST_TAG_LAYER, GST_TAG_FLAG_ENCODED, G_TYPE_UINT,
      "layer", "MPEG audio layer", NULL);
  gst_tag_register (GST_TAG_MODE, GST_TAG_FLAG_ENCODED, G_TYPE_STRING,
      "mode", "MPEG audio channel mode", NULL);
  gst_tag_register (GST_TAG_EMPHASIS, GST_TAG_FLAG_ENCODED, G_TYPE_STRING,
      "emphasis", "MPEG audio emphasis", NULL);

  /* ref these here from a thread-safe context (ie. not the streaming thread) */
  g_type_class_ref (GST_TYPE_MAD_MODE);
  g_type_class_ref (GST_TYPE_MAD_EMPHASIS);
}

static void
gst_mad_init (GstMad * mad, GstMadClass * klass)
{
  GstPadTemplate *template;

  /* create the sink and src pads */
  template = gst_static_pad_template_get (&mad_sink_template_factory);
  mad->sinkpad = gst_pad_new_from_template (template, "sink");
  gst_object_unref (template);
  gst_element_add_pad (GST_ELEMENT (mad), mad->sinkpad);
  gst_pad_set_chain_function (mad->sinkpad, GST_DEBUG_FUNCPTR (gst_mad_chain));
  gst_pad_set_event_function (mad->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mad_sink_event));

  template = gst_static_pad_template_get (&mad_src_template_factory);
  mad->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);
  gst_element_add_pad (GST_ELEMENT (mad), mad->srcpad);
  gst_pad_set_event_function (mad->srcpad,
      GST_DEBUG_FUNCPTR (gst_mad_src_event));
  gst_pad_set_query_function (mad->srcpad,
      GST_DEBUG_FUNCPTR (gst_mad_src_query));
  gst_pad_set_query_type_function (mad->srcpad,
      GST_DEBUG_FUNCPTR (gst_mad_get_query_types));
  gst_pad_use_fixed_caps (mad->srcpad);

  mad->tempbuffer = g_malloc (MAD_BUFFER_MDLEN * 3);
  mad->tempsize = 0;
  mad->base_byte_offset = 0;
  mad->bytes_consumed = 0;
  mad->total_samples = 0;
  mad->new_header = TRUE;
  mad->framecount = 0;
  mad->vbr_average = 0;
  mad->vbr_rate = 0;
  mad->restart = TRUE;
  mad->segment_start = 0;
  gst_segment_init (&mad->segment, GST_FORMAT_TIME);
  mad->header.mode = -1;
  mad->header.emphasis = -1;
  mad->tags = NULL;

  mad->half = FALSE;
  mad->ignore_crc = TRUE;
  mad->check_for_xing = TRUE;
  mad->xing_found = FALSE;
}

static void
gst_mad_dispose (GObject * object)
{
  GstMad *mad = GST_MAD (object);

  gst_mad_set_index (GST_ELEMENT (object), NULL);

  g_free (mad->tempbuffer);
  mad->tempbuffer = NULL;

  g_list_foreach (mad->pending_events, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (mad->pending_events);
  mad->pending_events = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_mad_set_index (GstElement * element, GstIndex * index)
{
  GstMad *mad = GST_MAD (element);

  mad->index = index;

  if (index)
    gst_index_get_writer_id (index, GST_OBJECT (element), &mad->index_id);
}

static GstIndex *
gst_mad_get_index (GstElement * element)
{
  GstMad *mad = GST_MAD (element);

  return mad->index;
}

static gboolean
gst_mad_convert_sink (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstMad *mad;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  /* -1 always maps to -1, and 0 to 0, we don't need any more info for that */
  if (src_value == -1 || src_value == 0) {
    *dest_value = src_value;
    return TRUE;
  }

  mad = GST_MAD (GST_PAD_PARENT (pad));

  if (mad->vbr_average == 0)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          /* multiply by 8 because vbr is in bits/second */
          *dest_value = gst_util_uint64_scale (src_value, 8 * GST_SECOND,
              mad->vbr_average);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          /* multiply by 8 because vbr is in bits/second */
          *dest_value = gst_util_uint64_scale (src_value, mad->vbr_average,
              8 * GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static gboolean
gst_mad_convert_src (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  GstMad *mad;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  /* -1 always maps to -1, and 0 to 0, we don't need any more info for that */
  if (src_value == -1 || src_value == 0) {
    *dest_value = src_value;
    return TRUE;
  }

  mad = GST_MAD (GST_PAD_PARENT (pad));

  bytes_per_sample = mad->channels * 4;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (bytes_per_sample == 0)
            return FALSE;
          *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = bytes_per_sample * mad->rate;

          if (byterate == 0)
            return FALSE;
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND, byterate);
          break;
        }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          if (mad->rate == 0)
            return FALSE;
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
              mad->rate);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale_int (src_value,
              scale * mad->rate, GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static const GstQueryType *
gst_mad_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_mad_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return gst_mad_src_query_types;
}

static gboolean
gst_mad_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstPad *peer;
  GstMad *mad;

  mad = GST_MAD (GST_PAD_PARENT (pad));

  peer = gst_pad_get_peer (mad->sinkpad);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 3, GST_FORMAT_DEFAULT, GST_FORMAT_TIME,
          GST_FORMAT_BYTES);
      break;
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 cur;

      /* save requested format */
      gst_query_parse_position (query, &format, NULL);

      /* try any demuxer before us first */
      if (format == GST_FORMAT_TIME && peer && gst_pad_query (peer, query)) {
        gst_query_parse_position (query, NULL, &cur);
        GST_LOG_OBJECT (mad, "peer returned position %" GST_TIME_FORMAT,
            GST_TIME_ARGS (cur));
        break;
      }

      /* and convert to the requested format */
      if (format != GST_FORMAT_DEFAULT) {
        if (!gst_mad_convert_src (pad, GST_FORMAT_DEFAULT, mad->total_samples,
                &format, &cur))
          goto error;
      } else {
        cur = mad->total_samples;
      }

      gst_query_set_position (query, format, cur);

      if (format == GST_FORMAT_TIME) {
        GST_LOG ("position=%" GST_TIME_FORMAT, GST_TIME_ARGS (cur));
      } else {
        GST_LOG ("position=%" G_GINT64_FORMAT ", format=%u", cur, format);
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat bytes_format = GST_FORMAT_BYTES;
      GstFormat time_format = GST_FORMAT_TIME;
      GstFormat req_format;
      gint64 total, total_bytes;

      /* save requested format */
      gst_query_parse_duration (query, &req_format, NULL);

      if (peer == NULL)
        goto error;

      /* try any demuxer before us first */
      if (req_format == GST_FORMAT_TIME && gst_pad_query (peer, query)) {
        gst_query_parse_duration (query, NULL, &total);
        GST_LOG_OBJECT (mad, "peer returned duration %" GST_TIME_FORMAT,
            GST_TIME_ARGS (total));
        break;
      }

      /* query peer for total length in bytes */
      if (!gst_pad_query_peer_duration (mad->sinkpad, &bytes_format,
              &total_bytes) || total_bytes <= 0) {
        GST_LOG_OBJECT (mad, "duration query on peer pad failed");
        goto error;
      }

      GST_LOG_OBJECT (mad, "peer pad returned total=%" G_GINT64_FORMAT
          " bytes", total_bytes);

      if (!gst_mad_convert_sink (pad, GST_FORMAT_BYTES, total_bytes,
              &time_format, &total)) {
        GST_DEBUG_OBJECT (mad, "conversion BYTE => TIME failed");
        goto error;
      }
      if (!gst_mad_convert_src (pad, GST_FORMAT_TIME, total,
              &req_format, &total)) {
        GST_DEBUG_OBJECT (mad, "conversion TIME => %s failed",
            gst_format_get_name (req_format));
        goto error;
      }

      gst_query_set_duration (query, req_format, total);

      if (req_format == GST_FORMAT_TIME) {
        GST_LOG_OBJECT (mad, "duration=%" GST_TIME_FORMAT,
            GST_TIME_ARGS (total));
      } else {
        GST_LOG_OBJECT (mad, "duration=%" G_GINT64_FORMAT " (%s)",
            total, gst_format_get_name (req_format));
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_mad_convert_src (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  if (peer)
    gst_object_unref (peer);

  return res;

error:

  GST_DEBUG ("error handling query");

  if (peer)
    gst_object_unref (peer);

  return FALSE;
}

static gboolean
index_seek (GstMad * mad, GstPad * pad, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  GstIndexEntry *entry = NULL;

  /* since we know the exact byteoffset of the frame,
     make sure to try bytes first */

  const GstFormat try_all_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    0
  };
  const GstFormat *try_formats = try_all_formats;
  const GstFormat *peer_formats;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  if (rate < 0.0)
    return FALSE;

  if (format == GST_FORMAT_TIME) {
    gst_segment_set_seek (&mad->segment, rate, format, flags, cur_type,
        cur, stop_type, stop, NULL);
  } else {
    gst_segment_init (&mad->segment, GST_FORMAT_UNDEFINED);
  }

  entry = gst_index_get_assoc_entry (mad->index, mad->index_id,
      GST_INDEX_LOOKUP_BEFORE, 0, format, cur);

  GST_DEBUG ("index seek");

  if (!entry)
    return FALSE;

#if 0
  peer_formats = gst_pad_get_formats (GST_PAD_PEER (mad->sinkpad));
#else
  peer_formats = try_all_formats;       /* FIXME */
#endif

  while (gst_formats_contains (peer_formats, *try_formats)) {
    gint64 value;
    GstEvent *seek_event;

    if (gst_index_entry_assoc_map (entry, *try_formats, &value)) {
      /* lookup succeeded, create the seek */

      GST_DEBUG ("index %s %" G_GINT64_FORMAT
          " -> %s %" G_GINT64_FORMAT,
          gst_format_get_details (format)->nick,
          cur, gst_format_get_details (*try_formats)->nick, value);

      seek_event = gst_event_new_seek (rate, *try_formats, flags,
          cur_type, value, stop_type, stop);

      if (gst_pad_send_event (GST_PAD_PEER (mad->sinkpad), seek_event)) {
        /* seek worked, we're done, loop will exit */
        mad->restart = TRUE;
        g_assert (format == GST_FORMAT_TIME);
        mad->segment_start = cur;
        return TRUE;
      }
    }
    try_formats++;
  }

  return FALSE;
}

static gboolean
normal_seek (GstMad * mad, GstPad * pad, GstEvent * event)
{
  gdouble rate;
  GstFormat format, conv;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gint64 time_cur, time_stop;
  gint64 bytes_cur, bytes_stop;
  gboolean flush;

  /* const GstFormat *peer_formats; */
  gboolean res;

  GST_DEBUG ("normal seek");

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  if (rate < 0.0)
    return FALSE;

  if (format != GST_FORMAT_TIME) {
    conv = GST_FORMAT_TIME;
    if (!gst_mad_convert_src (pad, format, cur, &conv, &time_cur))
      goto convert_error;
    if (!gst_mad_convert_src (pad, format, stop, &conv, &time_stop))
      goto convert_error;
  } else {
    time_cur = cur;
    time_stop = stop;
  }

  gst_segment_set_seek (&mad->segment, rate, GST_FORMAT_TIME, flags, cur_type,
      time_cur, stop_type, time_stop, NULL);

  GST_DEBUG ("seek to time %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT,
      GST_TIME_ARGS (time_cur), GST_TIME_ARGS (time_stop));

  /* shave off the flush flag, we'll need it later */
  flush = ((flags & GST_SEEK_FLAG_FLUSH) != 0);

  conv = GST_FORMAT_BYTES;
  if (!gst_mad_convert_sink (pad, GST_FORMAT_TIME, time_cur, &conv, &bytes_cur))
    goto convert_error;
  if (!gst_mad_convert_sink (pad, GST_FORMAT_TIME, time_stop, &conv,
          &bytes_stop))
    goto convert_error;

  {
    GstEvent *seek_event;

    /* conversion succeeded, create the seek */
    seek_event =
        gst_event_new_seek (rate, GST_FORMAT_BYTES, flags, cur_type,
        bytes_cur, stop_type, bytes_stop);

    /* do the seek */
    res = gst_pad_push_event (mad->sinkpad, seek_event);

    if (res) {
      /* we need to break out of the processing loop on flush */
      mad->restart = flush;
      mad->segment_start = time_cur;
      mad->last_ts = time_cur;
    }
  }
#if 0
  peer_formats = gst_pad_get_formats (GST_PAD_PEER (mad->sinkpad));
  /* while we did not exhaust our seek formats without result */
  while (peer_formats && *peer_formats && !res) {
    gint64 desired_offset;

    format = *peer_formats;

    /* try to convert requested format to one we can seek with on the sinkpad */
    if (gst_pad_convert (mad->sinkpad, GST_FORMAT_TIME, src_offset,
            &format, &desired_offset)) {
      GstEvent *seek_event;

      /* conversion succeeded, create the seek */
      seek_event =
          gst_event_new_seek (format | GST_EVENT_SEEK_METHOD (event) | flush,
          desired_offset);
      /* do the seek */
      if (gst_pad_send_event (GST_PAD_PEER (mad->sinkpad), seek_event)) {
        /* seek worked, we're done, loop will exit */
        res = TRUE;
      }
    }
    /* at this point, either the seek worked or res == FALSE */
    if (res)
      /* we need to break out of the processing loop on flush */
      mad->restart = flush;

    peer_formats++;
  }
#endif

  return res;

  /* ERRORS */
convert_error:
  {
    /* probably unsupported seek format */
    GST_DEBUG ("failed to convert format %u into GST_FORMAT_TIME", format);
    return FALSE;
  }
}

static gboolean
gst_mad_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstMad *mad;

  mad = GST_MAD (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* the all-formats seek logic, ref the event, we need it later */
      gst_event_ref (event);
      if (!(res = gst_pad_push_event (mad->sinkpad, event))) {
        if (mad->index)
          res = index_seek (mad, pad, event);
        else
          res = normal_seek (mad, pad, event);
      }
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_push_event (mad->sinkpad, event);
      break;
  }

  return res;
}

static inline gint32
scale (mad_fixed_t sample)
{
#if MAD_F_FRACBITS < 28
  /* round */
  sample += (1L << (28 - MAD_F_FRACBITS - 1));
#endif

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

#if MAD_F_FRACBITS < 28
  /* quantize */
  sample >>= (28 - MAD_F_FRACBITS);
#endif

  /* convert from 29 bits to 32 bits */
  return (gint32) (sample << 3);
}

/* do we need this function? */
static void
gst_mad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMad *mad;

  mad = GST_MAD (object);

  switch (prop_id) {
    case ARG_HALF:
      mad->half = g_value_get_boolean (value);
      break;
    case ARG_IGNORE_CRC:
      mad->ignore_crc = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMad *mad;

  mad = GST_MAD (object);

  switch (prop_id) {
    case ARG_HALF:
      g_value_set_boolean (value, mad->half);
      break;
    case ARG_IGNORE_CRC:
      g_value_set_boolean (value, mad->ignore_crc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mad_update_info (GstMad * mad)
{
  struct mad_header *header = &mad->frame.header;
  gboolean changed = FALSE;
  GstTagList *list = NULL;

#define CHECK_HEADER(h1,str)                                    \
G_STMT_START{                                                   \
  if (mad->header.h1 != header->h1 || mad->new_header) {        \
    mad->header.h1 = header->h1;                                \
     changed = TRUE;                                            \
  };                                                            \
} G_STMT_END

  /* update average bitrate */
  if (mad->new_header) {
    mad->framecount = 1;
    mad->vbr_rate = header->bitrate;
  } else {
    mad->framecount++;
    mad->vbr_rate += header->bitrate;
  }
  mad->vbr_average = (gint) (mad->vbr_rate / mad->framecount);

  CHECK_HEADER (layer, "layer");
  CHECK_HEADER (mode, "mode");
  CHECK_HEADER (emphasis, "emphasis");
  mad->new_header = FALSE;

  if (changed) {
    GEnumValue *mode;
    GEnumValue *emphasis;

    mode =
        g_enum_get_value (g_type_class_peek (GST_TYPE_MAD_MODE),
        mad->header.mode);
    emphasis =
        g_enum_get_value (g_type_class_peek (GST_TYPE_MAD_EMPHASIS),
        mad->header.emphasis);
    list = gst_tag_list_new ();
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_LAYER, mad->header.layer,
        GST_TAG_MODE, mode->value_nick,
        GST_TAG_EMPHASIS, emphasis->value_nick, NULL);
    if (!mad->framed) {
      gchar *str;

      str = g_strdup_printf ("MPEG-1 layer %d", mad->header.layer);
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_AUDIO_CODEC, str, NULL);
      g_free (str);
    }
  }

  changed = FALSE;
  CHECK_HEADER (bitrate, "bitrate");
  if (!mad->xing_found && changed) {
    if (!list)
      list = gst_tag_list_new ();
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, mad->header.bitrate, NULL);
  }
  mad->header.bitrate = header->bitrate;
#undef CHECK_HEADER

  if (list) {
    gst_element_post_message (GST_ELEMENT (mad),
        gst_message_new_tag (GST_OBJECT (mad), gst_tag_list_copy (list)));

    if (mad->need_newsegment)
      mad->pending_events =
          g_list_append (mad->pending_events, gst_event_new_tag (list));
    else
      gst_pad_push_event (mad->srcpad, gst_event_new_tag (list));
  }
}

static gboolean
gst_mad_sink_event (GstPad * pad, GstEvent * event)
{
  GstMad *mad = GST_MAD (GST_PAD_PARENT (pad));
  gboolean result;

  GST_DEBUG ("handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat format;
      gboolean update;
      gdouble rate, applied_rate;
      gint64 start, stop, pos;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &pos);

      if (format == GST_FORMAT_TIME) {
        /* FIXME: is this really correct? */
        mad->tempsize = 0;
        result = gst_pad_push_event (mad->srcpad, event);
        /* we don't need to restart when we get here */
        mad->restart = FALSE;
        mad->framed = TRUE;
        gst_segment_set_newsegment_full (&mad->segment, update, rate,
            applied_rate, GST_FORMAT_TIME, start, stop, pos);
      } else {
        GST_DEBUG ("dropping newsegment event in format %s",
            gst_format_get_name (format));
        /* on restart the chain function will generate a new
         * newsegment event, so we can just drop this one */
        mad->restart = TRUE;
        gst_event_unref (event);
        mad->tempsize = 0;
        mad->framed = FALSE;
        result = TRUE;
      }
      break;
    }
    case GST_EVENT_EOS:
      if (mad->segment.rate < 0.0)
        gst_mad_chain_reverse (mad, NULL);
      mad->caps_set = FALSE;    /* could be a new stream */
      result = gst_pad_push_event (mad->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      /* Clear any stored data, as it won't make sense once
       * the new data arrives */
      mad->tempsize = 0;
      mad_frame_mute (&mad->frame);
      mad_synth_mute (&mad->synth);
      gst_mad_clear_queues (mad);
      /* fall-through */
    case GST_EVENT_FLUSH_START:
      result = gst_pad_event_default (pad, event);
      break;
    default:
      if (mad->restart) {
        /* Cache all other events if we still have to send a NEWSEGMENT */
        mad->pending_events = g_list_append (mad->pending_events, event);
        result = TRUE;
      } else {
        result = gst_pad_event_default (pad, event);
      }
      break;
  }
  return result;
}

static gboolean
gst_mad_check_restart (GstMad * mad)
{
  gboolean yes = mad->restart;

  if (mad->restart) {
    mad->restart = FALSE;
    mad->tempsize = 0;
  }
  return yes;
}


/* The following code has been taken from
 * rhythmbox/metadata/monkey-media/stream-info-impl/id3-vfs/mp3bitrate.c
 * which took it from xine-lib/src/demuxers/demux_mpgaudio.c
 * This code has been kindly relicensed to LGPL by Thibaut Mattern and
 * Bastien Nocera
 */
#define BE_32(x) GST_READ_UINT32_BE(x)

#define FOURCC_TAG( ch0, ch1, ch2, ch3 )                \
        ( (long)(unsigned char)(ch3) |                  \
          ( (long)(unsigned char)(ch2) << 8 ) |         \
          ( (long)(unsigned char)(ch1) << 16 ) |        \
          ( (long)(unsigned char)(ch0) << 24 ) )

/* Xing header stuff */
#define XING_TAG FOURCC_TAG('X', 'i', 'n', 'g')
#define XING_FRAMES_FLAG     0x0001
#define XING_BYTES_FLAG      0x0002
#define XING_TOC_FLAG        0x0004
#define XING_VBR_SCALE_FLAG  0x0008
#define XING_TOC_LENGTH      100

/* check for valid "Xing" VBR header */
static int
is_xhead (unsigned char *buf)
{
  return (BE_32 (buf) == XING_TAG);
}


#undef LOG
/*#define LOG*/
#ifdef LOG
#ifndef WIN32
#define lprintf(x...) g_print(x)
#else
#define lprintf GST_DEBUG
#endif
#else
#ifndef WIN32
#define lprintf(x...)
#else
#define lprintf GST_DEBUG
#endif
#endif

static int
mpg123_parse_xing_header (struct mad_header *header,
    const guint8 * buf, int bufsize, int *bitrate, int *time)
{
  int i;
  guint8 *ptr = (guint8 *) buf;
  double frame_duration;
  int xflags, xframes, xbytes;
  int abr;
  guint8 xtoc[XING_TOC_LENGTH];
  int lsf_bit = !(header->flags & MAD_FLAG_LSF_EXT);

  xframes = xbytes = 0;

  /* offset of the Xing header */
  if (lsf_bit) {
    if (header->mode != MAD_MODE_SINGLE_CHANNEL)
      ptr += (32 + 4);
    else
      ptr += (17 + 4);
  } else {
    if (header->mode != MAD_MODE_SINGLE_CHANNEL)
      ptr += (17 + 4);
    else
      ptr += (9 + 4);
  }

  if (ptr >= (buf + bufsize - 4))
    return 0;

  if (is_xhead (ptr)) {
    lprintf ("Xing header found\n");

    ptr += 4;
    if (ptr >= (buf + bufsize - 4))
      return 0;

    xflags = BE_32 (ptr);
    ptr += 4;

    if (xflags & XING_FRAMES_FLAG) {
      if (ptr >= (buf + bufsize - 4))
        return 0;
      xframes = BE_32 (ptr);
      lprintf ("xframes: %d\n", xframes);
      ptr += 4;
    }
    if (xflags & XING_BYTES_FLAG) {
      if (ptr >= (buf + bufsize - 4))
        return 0;
      xbytes = BE_32 (ptr);
      lprintf ("xbytes: %d\n", xbytes);
      ptr += 4;
    }
    if (xflags & XING_TOC_FLAG) {
      guchar old = 0;

      lprintf ("toc found\n");
      if (ptr >= (buf + bufsize - XING_TOC_LENGTH))
        return 0;
      if (*ptr != 0) {
        lprintf ("skipping broken Xing TOC\n");
        goto skip_toc;
      }
      for (i = 0; i < XING_TOC_LENGTH; i++) {
        xtoc[i] = *(ptr + i);
        if (old > xtoc[i]) {
          lprintf ("skipping broken Xing TOC\n");
          goto skip_toc;
        }
        lprintf ("%d ", xtoc[i]);
      }
      lprintf ("\n");
    skip_toc:
      ptr += XING_TOC_LENGTH;
    }

    if (xflags & XING_VBR_SCALE_FLAG) {
      if (ptr >= (buf + bufsize - 4))
        return 0;
      lprintf ("xvbr_scale: %d\n", BE_32 (ptr));
    }

    /* 1 kbit = 1000 bits ! (and not 1024 bits) */
    if (xflags & (XING_FRAMES_FLAG | XING_BYTES_FLAG)) {
      if (header->layer == MAD_LAYER_I) {
        frame_duration = 384.0 / (double) header->samplerate;
      } else {
        int slots_per_frame;

        slots_per_frame = ((header->layer == MAD_LAYER_III)
            && !lsf_bit) ? 72 : 144;
        frame_duration = slots_per_frame * 8.0 / (double) header->samplerate;
      }
      abr = ((double) xbytes * 8.0) / ((double) xframes * frame_duration);
      lprintf ("abr: %d bps\n", abr);
      if (bitrate != NULL) {
        *bitrate = abr;
      }
      if (time != NULL) {
        *time = (double) xframes *frame_duration;

        lprintf ("stream_length: %d s, %d min %d s\n", *time,
            *time / 60, *time % 60);
      }
    } else {
      /* it's a stupid Xing header */
      lprintf ("not a Xing VBR file\n");
    }
    return 1;
  } else {
    lprintf ("Xing header not found\n");
    return 0;
  }
}

/* End of Xine code */

/* internal function to check if the header has changed and thus the
 * caps need to be reset.  Only call during normal mode, not resyncing */
static void
gst_mad_check_caps_reset (GstMad * mad)
{
  guint nchannels;
  guint rate, old_rate = mad->rate;

  nchannels = MAD_NCHANNELS (&mad->frame.header);

#if MAD_VERSION_MINOR <= 12
  rate = mad->header.sfreq;
#else
  rate = mad->frame.header.samplerate;
#endif

  /* rate and channels are not supposed to change in a continuous stream,
   * so check this first before doing anything */

  /* only set caps if they weren't already set for this continuous stream */
  if (mad->channels != nchannels || mad->rate != rate) {
    if (mad->caps_set) {
      GST_DEBUG
          ("Header changed from %d Hz/%d ch to %d Hz/%d ch, failed sync after seek ?",
          mad->rate, mad->channels, rate, nchannels);
      /* we're conservative on stream changes. However, our *initial* caps
       * might have been wrong as well - mad ain't perfect in syncing. So,
       * we count caps changes and change if we pass a limit treshold (3). */
      if (nchannels != mad->pending_channels || rate != mad->pending_rate) {
        mad->times_pending = 0;
        mad->pending_channels = nchannels;
        mad->pending_rate = rate;
      }
      if (++mad->times_pending < 3)
        return;
    }
  }
  gst_mad_update_info (mad);

  if (mad->channels != nchannels || mad->rate != rate) {
    GstCaps *caps;

    if (mad->stream.options & MAD_OPTION_HALFSAMPLERATE)
      rate >>= 1;

    /* FIXME see if peer can accept the caps */

    /* we set the caps even when the pad is not connected so they
     * can be gotten for streaminfo */
    caps = gst_caps_new_simple ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "width", G_TYPE_INT, 32,
        "depth", G_TYPE_INT, 32,
        "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, nchannels, NULL);

    gst_pad_set_caps (mad->srcpad, caps);
    gst_caps_unref (caps);

    mad->caps_set = TRUE;       /* set back to FALSE on discont */
    mad->channels = nchannels;
    mad->rate = rate;

    /* update sample count so we don't come up with crazy timestamps */
    if (mad->total_samples && old_rate) {
      mad->total_samples = mad->total_samples * rate / old_rate;
    }
  }
}

static void
gst_mad_clear_queues (GstMad * mad)
{
  g_list_foreach (mad->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (mad->queued);
  mad->queued = NULL;
  g_list_foreach (mad->gather, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (mad->gather);
  mad->gather = NULL;
  g_list_foreach (mad->decode, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (mad->decode);
  mad->decode = NULL;
}

static GstFlowReturn
gst_mad_flush_decode (GstMad * mad)
{
  GstFlowReturn res = GST_FLOW_OK;
  GList *walk;

  walk = mad->decode;

  GST_DEBUG_OBJECT (mad, "flushing buffers to decoder");

  /* clear buffer and decoder state */
  mad->tempsize = 0;
  mad_frame_mute (&mad->frame);
  mad_synth_mute (&mad->synth);

  mad->process = TRUE;
  while (walk) {
    GList *next;
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    GST_DEBUG_OBJECT (mad, "decoding buffer %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    next = g_list_next (walk);
    /* decode buffer, resulting data prepended to output queue */
    gst_buffer_ref (buf);
    res = gst_mad_chain (mad->sinkpad, buf);

    /* if we generated output, we can discard the buffer, else we
     * keep it in the queue */
    if (mad->queued) {
      GST_DEBUG_OBJECT (mad, "decoded buffer to %p", mad->queued->data);
      mad->decode = g_list_delete_link (mad->decode, walk);
      gst_buffer_unref (buf);
    } else {
      GST_DEBUG_OBJECT (mad, "buffer did not decode, keeping");
    }
    walk = next;
  }
  mad->process = FALSE;

  /* now send queued data downstream */
  while (mad->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (mad->queued->data);

    GST_DEBUG_OBJECT (mad, "pushing buffer %p of size %u, "
        "time %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT, buf,
        GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
    res = gst_pad_push (mad->srcpad, buf);

    mad->queued = g_list_delete_link (mad->queued, mad->queued);
  }

  return res;
}

static GstFlowReturn
gst_mad_chain_reverse (GstMad * mad, GstBuffer * buf)
{
  GstFlowReturn result = GST_FLOW_OK;

  /* if we have a discont, move buffers to the decode list */
  if (!buf || GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    GST_DEBUG_OBJECT (mad, "received discont");
    while (mad->gather) {
      GstBuffer *gbuf;

      gbuf = GST_BUFFER_CAST (mad->gather->data);
      /* remove from the gather list */
      mad->gather = g_list_delete_link (mad->gather, mad->gather);
      /* copy to decode queue */
      mad->decode = g_list_prepend (mad->decode, gbuf);
    }
    /* decode stuff in the decode queue */
    gst_mad_flush_decode (mad);
  }

  if (G_LIKELY (buf)) {
    GST_DEBUG_OBJECT (mad, "gathering buffer %p of size %u, "
        "time %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT, buf,
        GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    /* add buffer to gather queue */
    mad->gather = g_list_prepend (mad->gather, buf);
  }

  return result;
}

static GstFlowReturn
gst_mad_chain (GstPad * pad, GstBuffer * buffer)
{
  GstMad *mad;
  guint8 *data;
  glong size, tempsize;
  gboolean new_pts = FALSE;
  gboolean discont;
  GstClockTime timestamp;
  GstFlowReturn result = GST_FLOW_OK;

  mad = GST_MAD (GST_PAD_PARENT (pad));

  /* restarts happen on discontinuities, ie. seek, flush, PAUSED to PLAYING */
  if (gst_mad_check_restart (mad)) {
    mad->need_newsegment = TRUE;
    GST_DEBUG ("mad restarted");
  }

  if (mad->segment.rate < 0.0) {
    if (!mad->process)
      return gst_mad_chain_reverse (mad, buffer);
    /* no output discont */
    discont = FALSE;
  } else {
    /* take discont flag */
    discont = GST_BUFFER_IS_DISCONT (buffer);
  }

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  GST_DEBUG ("mad in timestamp %" GST_TIME_FORMAT " duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp), GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  /* handle timestamps */
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    /* if there is nothing left to process in our temporary buffer,
     * we can set this timestamp on the next outgoing buffer */
    if (mad->tempsize == 0) {
      /* we have to save the result here because we can't yet convert
       * the timestamp to a sample offset, as the samplerate might not
       * be known yet */
      mad->last_ts = timestamp;
      mad->base_byte_offset = GST_BUFFER_OFFSET (buffer);
      mad->bytes_consumed = 0;
    }
    /* else we need to finish the current partial frame with the old timestamp
     * and queue this timestamp for the next frame */
    else {
      new_pts = TRUE;
    }
  }
  GST_DEBUG ("last_ts %" GST_TIME_FORMAT, GST_TIME_ARGS (mad->last_ts));

  /* handle data */
  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  tempsize = mad->tempsize;

  /* process the incoming buffer in chunks of maximum MAD_BUFFER_MDLEN bytes;
   * this is the upper limit on processable chunk sizes set by mad */
  while (size > 0) {
    gint tocopy;
    guchar *mad_input_buffer;   /* convenience pointer to tempbuffer */

    if (mad->tempsize == 0 && discont) {
      mad->discont = TRUE;
      discont = FALSE;
    }
    tocopy =
        MIN (MAD_BUFFER_MDLEN, MIN (size,
            MAD_BUFFER_MDLEN * 3 - mad->tempsize));
    if (tocopy == 0) {
      GST_ELEMENT_ERROR (mad, STREAM, DECODE, (NULL),
          ("mad claims to need more data than %u bytes, we don't have that much",
              MAD_BUFFER_MDLEN * 3));
      result = GST_FLOW_ERROR;
      goto end;
    }

    /* append the chunk to process to our internal temporary buffer */
    GST_LOG ("tempbuffer size %ld, copying %d bytes from incoming buffer",
        mad->tempsize, tocopy);
    memcpy (mad->tempbuffer + mad->tempsize, data, tocopy);
    mad->tempsize += tocopy;

    /* update our incoming buffer's parameters to reflect this */
    size -= tocopy;
    data += tocopy;

    mad_input_buffer = mad->tempbuffer;

    /* while we have data we can consume it */
    while (mad->tempsize > 0) {
      gint consumed = 0;
      guint nsamples;
      guint64 time_offset = GST_CLOCK_TIME_NONE;
      guint64 time_duration = GST_CLOCK_TIME_NONE;
      unsigned char const *before_sync, *after_sync;
      gboolean goto_exit = FALSE;

      mad->in_error = FALSE;

      mad_stream_buffer (&mad->stream, mad_input_buffer, mad->tempsize);

      /* added separate header decoding to catch errors earlier, also fixes
       * some weird decoding errors... */
      GST_LOG ("decoding the header now");
      if (mad_header_decode (&mad->frame.header, &mad->stream) == -1) {
        if (mad->stream.error == MAD_ERROR_BUFLEN) {
          GST_LOG ("not enough data in tempbuffer (%ld), breaking to get more",
              mad->tempsize);
          break;
        } else {
          GST_WARNING ("mad_header_decode had an error: %s",
              mad_stream_errorstr (&mad->stream));
        }
      }

      GST_LOG ("decoding one frame now");

      if (mad_frame_decode (&mad->frame, &mad->stream) == -1) {
        GST_LOG ("got error %d", mad->stream.error);

        /* not enough data, need to wait for next buffer? */
        if (mad->stream.error == MAD_ERROR_BUFLEN) {
          if (mad->stream.next_frame == mad_input_buffer) {
            GST_LOG
                ("not enough data in tempbuffer (%ld), breaking to get more",
                mad->tempsize);
            break;
          } else {
            GST_LOG ("sync error, flushing unneeded data");
            goto next_no_samples;
          }
        } else if (mad->stream.error == MAD_ERROR_BADDATAPTR) {
          /* Flush data */
          goto next_no_samples;
        }
        /* we are in an error state */
        mad->in_error = TRUE;
        GST_WARNING ("mad_frame_decode had an error: %s",
            mad_stream_errorstr (&mad->stream));
        if (!MAD_RECOVERABLE (mad->stream.error)) {
          GST_ELEMENT_ERROR (mad, STREAM, DECODE, (NULL), (NULL));
          result = GST_FLOW_ERROR;
          goto end;
        } else if (mad->stream.error == MAD_ERROR_LOSTSYNC) {
          /* lost sync, force a resync */
          GST_INFO ("recoverable lost sync error");
        }

        mad_frame_mute (&mad->frame);
        mad_synth_mute (&mad->synth);
        before_sync = mad->stream.ptr.byte;
        if (mad_stream_sync (&mad->stream) != 0)
          GST_WARNING ("mad_stream_sync failed");
        after_sync = mad->stream.ptr.byte;
        /* a succesful resync should make us drop bytes as consumed, so
           calculate from the byte pointers before and after resync */
        consumed = after_sync - before_sync;
        GST_DEBUG ("resynchronization consumes %d bytes", consumed);
        GST_DEBUG ("synced to data: 0x%0x 0x%0x", *mad->stream.ptr.byte,
            *(mad->stream.ptr.byte + 1));

        mad_stream_sync (&mad->stream);
        /* recoverable errors pass */
        goto next_no_samples;
      }

      if (mad->check_for_xing) {
        int bitrate = 0, time = 0;
        GstTagList *list;
        int frame_len = mad->stream.next_frame - mad->stream.this_frame;

        mad->check_for_xing = FALSE;

        /* Assume Xing headers can only be the first frame in a mp3 file */
        if (mpg123_parse_xing_header (&mad->frame.header,
                mad->stream.this_frame, frame_len, &bitrate, &time)) {
          mad->xing_found = TRUE;
          list = gst_tag_list_new ();
          gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
              GST_TAG_DURATION, (gint64) time * 1000 * 1000 * 1000,
              GST_TAG_BITRATE, bitrate, NULL);
          gst_element_post_message (GST_ELEMENT (mad),
              gst_message_new_tag (GST_OBJECT (mad), gst_tag_list_copy (list)));

          if (mad->need_newsegment)
            mad->pending_events =
                g_list_append (mad->pending_events, gst_event_new_tag (list));
          else
            gst_pad_push_event (mad->srcpad, gst_event_new_tag (list));

          goto next_no_samples;
        }
      }

      /* if we're not resyncing/in error, check if caps need to be set again */
      if (!mad->in_error)
        gst_mad_check_caps_reset (mad);
      nsamples = MAD_NSBSAMPLES (&mad->frame.header) *
          (mad->stream.options & MAD_OPTION_HALFSAMPLERATE ? 16 : 32);

      if (mad->rate == 0) {
        g_warning ("mad->rate is 0; timestamps cannot be calculated");
      } else {
        /* if we have a pending timestamp, we can use it now to calculate the sample offset */
        if (GST_CLOCK_TIME_IS_VALID (mad->last_ts)) {
          GstFormat format = GST_FORMAT_DEFAULT;
          gint64 total;

          /* Convert incoming timestamp to a number of encoded samples */
          gst_pad_query_convert (mad->srcpad, GST_FORMAT_TIME, mad->last_ts,
              &format, &total);

          GST_DEBUG_OBJECT (mad, "calculated samples offset from ts is %"
              G_GUINT64_FORMAT " accumulated samples offset is %"
              G_GUINT64_FORMAT, total, mad->total_samples);

          /* We are using the incoming timestamps to generate the outgoing ones
           * if available. However some muxing formats are not precise enough
           * to allow us to generate a perfect stream. When converting the
           * timestamp to a number of encoded samples so far we are introducing
           * a lot of potential error compared to our accumulated number of
           * samples encoded. If the difference between those 2 numbers is
           * bigger than half a frame we then use the incoming timestamp
           * as a reference, otherwise we continue using our accumulated samples
           * counter */
          if (ABS (((gint64) (mad->total_samples)) - total) > nsamples / 2) {
            GST_DEBUG_OBJECT (mad, "difference is bigger than half a frame, "
                "using calculated samples offset %" G_GUINT64_FORMAT, total);
            /* Override our accumulated samples counter */
            mad->total_samples = total;
            /* We use that timestamp directly */
            time_offset = mad->last_ts;
          }

          mad->last_ts = GST_CLOCK_TIME_NONE;
        }

        if (!GST_CLOCK_TIME_IS_VALID (time_offset)) {
          time_offset = gst_util_uint64_scale_int (mad->total_samples,
              GST_SECOND, mad->rate);
        }
        /* Duration is next timestamp - this one to generate a continuous
         * stream */
        time_duration =
            gst_util_uint64_scale_int (mad->total_samples + nsamples,
            GST_SECOND, mad->rate) - time_offset;
      }

      if (mad->index) {
        guint64 x_bytes = mad->base_byte_offset + mad->bytes_consumed;

        gst_index_add_association (mad->index, mad->index_id,
            GST_ASSOCIATION_FLAG_DELTA_UNIT,
            GST_FORMAT_BYTES, x_bytes, GST_FORMAT_TIME, time_offset, NULL);
      }

      if (mad->segment_start <= (time_offset ==
              GST_CLOCK_TIME_NONE ? 0 : time_offset)) {

        /* for sample accurate seeking, calculate how many samples
           to skip and send the remaining pcm samples */

        GstBuffer *outbuffer = NULL;
        gint32 *outdata;
        mad_fixed_t const *left_ch, *right_ch;

        if (mad->need_newsegment) {
          gint64 start = time_offset;

          GST_DEBUG ("Sending NEWSEGMENT event, start=%" GST_TIME_FORMAT,
              GST_TIME_ARGS (start));

          gst_segment_set_newsegment (&mad->segment, FALSE, 1.0,
              GST_FORMAT_TIME, start, GST_CLOCK_TIME_NONE, start);

          gst_pad_push_event (mad->srcpad,
              gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
                  start, GST_CLOCK_TIME_NONE, start));
          mad->need_newsegment = FALSE;
        }

        if (mad->pending_events) {
          GList *l;

          for (l = mad->pending_events; l != NULL; l = l->next) {
            gst_pad_push_event (mad->srcpad, GST_EVENT (l->data));
          }
          g_list_free (mad->pending_events);
          mad->pending_events = NULL;
        }

        /* will attach the caps to the buffer */
        result =
            gst_pad_alloc_buffer_and_set_caps (mad->srcpad, 0,
            nsamples * mad->channels * 4, GST_PAD_CAPS (mad->srcpad),
            &outbuffer);
        if (result != GST_FLOW_OK) {
          /* Head for the exit, dropping samples as we go */
          GST_LOG ("Skipping frame synthesis due to pad_alloc return value");
          goto_exit = TRUE;
          goto skip_frame;
        }

        if (GST_BUFFER_SIZE (outbuffer) != nsamples * mad->channels * 4) {
          gst_buffer_unref (outbuffer);

          outbuffer = gst_buffer_new_and_alloc (nsamples * mad->channels * 4);
          gst_buffer_set_caps (outbuffer, GST_PAD_CAPS (mad->srcpad));
        }

        mad_synth_frame (&mad->synth, &mad->frame);
        left_ch = mad->synth.pcm.samples[0];
        right_ch = mad->synth.pcm.samples[1];

        outdata = (gint32 *) GST_BUFFER_DATA (outbuffer);

        GST_DEBUG ("mad out timestamp %" GST_TIME_FORMAT " dur: %"
            GST_TIME_FORMAT, GST_TIME_ARGS (time_offset),
            GST_TIME_ARGS (time_duration));

        GST_BUFFER_TIMESTAMP (outbuffer) = time_offset;
        GST_BUFFER_DURATION (outbuffer) = time_duration;
        GST_BUFFER_OFFSET (outbuffer) = mad->total_samples;
        GST_BUFFER_OFFSET_END (outbuffer) = mad->total_samples + nsamples;

        /* output sample(s) in 16-bit signed native-endian PCM */
        if (mad->channels == 1) {
          gint count = nsamples;

          while (count--) {
            *outdata++ = scale (*left_ch++) & 0xffffffff;
          }
        } else {
          gint count = nsamples;

          while (count--) {
            *outdata++ = scale (*left_ch++) & 0xffffffff;
            *outdata++ = scale (*right_ch++) & 0xffffffff;
          }
        }

        if ((outbuffer = gst_audio_buffer_clip (outbuffer, &mad->segment,
                    mad->rate, 4 * mad->channels))) {
          GST_LOG_OBJECT (mad,
              "pushing buffer, off=%" G_GUINT64_FORMAT ", ts=%" GST_TIME_FORMAT,
              GST_BUFFER_OFFSET (outbuffer),
              GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuffer)));

          /* apply discont */
          if (mad->discont) {
            GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_DISCONT);
            mad->discont = FALSE;
          }

          mad->segment.last_stop = GST_BUFFER_TIMESTAMP (outbuffer);
          if (mad->segment.rate > 0.0) {
            result = gst_pad_push (mad->srcpad, outbuffer);
          } else {
            GST_LOG_OBJECT (mad, "queued buffer");
            mad->queued = g_list_prepend (mad->queued, outbuffer);
            result = GST_FLOW_OK;
          }
          if (result != GST_FLOW_OK) {
            /* Head for the exit, dropping samples as we go */
            goto_exit = TRUE;
          }
        } else {
          GST_LOG_OBJECT (mad, "Dropping buffer");
        }
      }

    skip_frame:
      mad->total_samples += nsamples;

      /* we have a queued timestamp on the incoming buffer that we should
       * use for the next frame */
      if (new_pts && (mad->stream.next_frame - mad_input_buffer >= tempsize)) {
        new_pts = FALSE;
        mad->last_ts = timestamp;
        mad->base_byte_offset = GST_BUFFER_OFFSET (buffer);
        mad->bytes_consumed = 0;
      }
      tempsize = 0;
      if (discont) {
        mad->discont = TRUE;
        discont = FALSE;
      }

      if (gst_mad_check_restart (mad)) {
        goto end;
      }

    next_no_samples:
      /* figure out how many bytes mad consumed */
      /* if consumed is already set, it's from the resync higher up, so
         we need to use that value instead.  Otherwise, recalculate from
         mad's consumption */
      if (consumed == 0)
        consumed = mad->stream.next_frame - mad_input_buffer;

      GST_LOG ("mad consumed %d bytes", consumed);
      /* move out pointer to where mad want the next data */
      mad_input_buffer += consumed;
      mad->tempsize -= consumed;
      mad->bytes_consumed += consumed;
      if (goto_exit == TRUE)
        goto end;
    }
    /* we only get here from breaks, tempsize never actually drops below 0 */
    memmove (mad->tempbuffer, mad_input_buffer, mad->tempsize);
  }
  result = GST_FLOW_OK;

end:
  gst_buffer_unref (buffer);

  return result;
}

static GstStateChangeReturn
gst_mad_change_state (GstElement * element, GstStateChange transition)
{
  GstMad *mad;
  GstStateChangeReturn ret;

  mad = GST_MAD (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      guint options = 0;

      mad_stream_init (&mad->stream);
      mad_frame_init (&mad->frame);
      mad_synth_init (&mad->synth);
      mad->tempsize = 0;
      mad->discont = TRUE;
      mad->total_samples = 0;
      mad->rate = 0;
      mad->channels = 0;
      mad->caps_set = FALSE;
      mad->times_pending = mad->pending_rate = mad->pending_channels = 0;
      mad->vbr_average = 0;
      gst_segment_init (&mad->segment, GST_FORMAT_TIME);
      mad->new_header = TRUE;
      mad->framed = FALSE;
      mad->framecount = 0;
      mad->vbr_rate = 0;
      mad->frame.header.samplerate = 0;
      mad->last_ts = GST_CLOCK_TIME_NONE;
      if (mad->ignore_crc)
        options |= MAD_OPTION_IGNORECRC;
      if (mad->half)
        options |= MAD_OPTION_HALFSAMPLERATE;
      mad_stream_options (&mad->stream, options);
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      mad_synth_finish (&mad->synth);
      mad_frame_finish (&mad->frame);
      mad_stream_finish (&mad->stream);
      mad->restart = TRUE;
      mad->check_for_xing = TRUE;
      if (mad->tags) {
        gst_tag_list_free (mad->tags);
        mad->tags = NULL;
      }
      gst_mad_clear_queues (mad);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mad_debug, "mad", 0, "mad mp3 decoding");

  return gst_element_register (plugin, "mad", GST_RANK_SECONDARY,
      gst_mad_get_type ());
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mad",
    "mp3 decoding based on the mad library",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
