/* Schrodinger
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
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

#include "gstbasevideocodec.h"

#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY (basevideocodec_debug);
#define GST_CAT_DEFAULT basevideocodec_debug

/* GstBaseVideoCodec signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static void gst_base_video_codec_finalize (GObject * object);

//static const GstQueryType *gst_base_video_codec_get_query_types (GstPad *pad);
//static gboolean gst_base_video_codec_src_query (GstPad *pad, GstQuery *query);
//static gboolean gst_base_video_codec_sink_query (GstPad *pad, GstQuery *query);
//static gboolean gst_base_video_codec_src_event (GstPad *pad, GstEvent *event);
//static gboolean gst_base_video_codec_sink_event (GstPad *pad, GstEvent *event);
static GstStateChangeReturn gst_base_video_codec_change_state (GstElement *
    element, GstStateChange transition);
//static GstFlowReturn gst_base_video_codec_push_all (GstBaseVideoCodec *base_video_codec, 
//    gboolean at_eos);


GST_BOILERPLATE (GstBaseVideoCodec, gst_base_video_codec, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_base_video_codec_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (basevideocodec_debug, "basevideocodec", 0,
      "Base Video Codec");

}

static void
gst_base_video_codec_class_init (GstBaseVideoCodecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_base_video_codec_finalize;

  element_class->change_state = gst_base_video_codec_change_state;
}

static void
gst_base_video_codec_init (GstBaseVideoCodec * base_video_codec,
    GstBaseVideoCodecClass * klass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG ("gst_base_video_codec_init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  base_video_codec->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  //gst_pad_set_query_function (base_video_codec->sinkpad,
  //    gst_base_video_codec_sink_query);
  gst_element_add_pad (GST_ELEMENT (base_video_codec),
      base_video_codec->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  base_video_codec->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (base_video_codec),
      base_video_codec->srcpad);

  gst_segment_init (&base_video_codec->segment, GST_FORMAT_TIME);

}

static void
gst_base_video_codec_reset (GstBaseVideoCodec * base_video_codec)
{
  GList *g;

  GST_DEBUG ("reset");

  for (g = base_video_codec->frames; g; g = g_list_next (g)) {
    gst_base_video_codec_free_frame ((GstVideoFrame *) g->data);
  }
  g_list_free (base_video_codec->frames);

  if (base_video_codec->caps) {
    gst_caps_unref (base_video_codec->caps);
    base_video_codec->caps = NULL;
  }

}

static void
gst_base_video_codec_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#ifdef unused
static const GstQueryType *
gst_base_video_codec_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return query_types;
}
#endif

#if 0
static gboolean
gst_base_video_codec_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res;
  GstBaseVideoCodec *dec;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  dec = GST_BASE_VIDEO_CODEC (gst_pad_get_parent (pad));

  if (src_format == GST_FORMAT_DEFAULT && *dest_format == GST_FORMAT_TIME) {
    if (dec->fps_d != 0) {
      *dest_value = gst_util_uint64_scale (granulepos_to_frame (src_value),
          dec->fps_d * GST_SECOND, dec->fps_n);
      res = TRUE;
    } else {
      res = FALSE;
    }
  } else {
    GST_WARNING ("unhandled conversion from %d to %d", src_format,
        *dest_format);
    res = FALSE;
  }

  gst_object_unref (dec);

  return res;
}

static gboolean
gst_base_video_codec_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstBaseVideoCodec *dec;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  dec = GST_BASE_VIDEO_CODEC (gst_pad_get_parent (pad));

  /* FIXME: check if we are in a decoding state */

  switch (src_format) {
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (src_value,
              dec->fps_d * GST_SECOND, dec->fps_n);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
        {
          *dest_value = gst_util_uint64_scale (src_value,
              dec->fps_n, dec->fps_d * GST_SECOND);
          break;
        }
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  gst_object_unref (dec);

  return res;
}
#endif

#ifdef unused
static gboolean
gst_base_video_codec_src_query (GstPad * pad, GstQuery * query)
{
  GstBaseVideoCodec *base_codec;
  gboolean res = FALSE;

  base_codec = GST_BASE_VIDEO_CODEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time;
      gint64 value;

      gst_query_parse_position (query, &format, NULL);

      time = gst_util_uint64_scale (base_codec->system_frame_number,
          base_codec->state.fps_n, base_codec->state.fps_d);
      time += base_codec->state.segment.time;
      GST_DEBUG ("query position %" GST_TIME_FORMAT, GST_TIME_ARGS (time));
      res = gst_base_video_encoded_video_convert (&base_codec->state,
          GST_FORMAT_TIME, time, &format, &value);
      if (!res)
        goto error;

      gst_query_set_position (query, format, value);
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_pad_query (GST_PAD_PEER (base_codec->sinkpad), query);
      if (!res)
        goto error;
      break;
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      GST_DEBUG ("query convert");

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = gst_base_video_encoded_video_convert (&base_codec->state,
          src_fmt, src_val, &dest_fmt, &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
done:
  gst_object_unref (base_codec);

  return res;
error:
  GST_DEBUG_OBJECT (base_codec, "query failed");
  goto done;
}
#endif

#ifdef unused
static gboolean
gst_base_video_codec_sink_query (GstPad * pad, GstQuery * query)
{
  GstBaseVideoCodec *base_video_codec;
  gboolean res = FALSE;

  base_video_codec = GST_BASE_VIDEO_CODEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = gst_base_video_encoded_video_convert (&base_video_codec->state,
          src_fmt, src_val, &dest_fmt, &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
done:
  gst_object_unref (base_video_codec);

  return res;
error:
  GST_DEBUG_OBJECT (base_video_codec, "query failed");
  goto done;
}
#endif

#ifdef unused
static gboolean
gst_base_video_codec_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoCodec *base_video_codec;
  gboolean res = FALSE;

  base_video_codec = GST_BASE_VIDEO_CODEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format, tformat;
      gdouble rate;
      GstEvent *real_seek;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gint64 tcur, tstop;

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type,
          &cur, &stop_type, &stop);
      gst_event_unref (event);

      tformat = GST_FORMAT_TIME;
      res = gst_base_video_encoded_video_convert (&base_video_codec->state,
          format, cur, &tformat, &tcur);
      if (!res)
        goto convert_error;
      res = gst_base_video_encoded_video_convert (&base_video_codec->state,
          format, stop, &tformat, &tstop);
      if (!res)
        goto convert_error;

      real_seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);

      res = gst_pad_push_event (base_video_codec->sinkpad, real_seek);

      break;
    }
#if 0
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      GST_OBJECT_LOCK (base_video_codec);
      base_video_codec->proportion = proportion;
      base_video_codec->earliest_time = timestamp + diff;
      GST_OBJECT_UNLOCK (base_video_codec);

      GST_DEBUG_OBJECT (base_video_codec,
          "got QoS %" GST_TIME_FORMAT ", %" G_GINT64_FORMAT,
          GST_TIME_ARGS (timestamp), diff);

      res = gst_pad_push_event (base_video_codec->sinkpad, event);
      break;
    }
#endif
    default:
      res = gst_pad_push_event (base_video_codec->sinkpad, event);
      break;
  }
done:
  gst_object_unref (base_video_codec);
  return res;

convert_error:
  GST_DEBUG_OBJECT (base_video_codec, "could not convert format");
  goto done;
}
#endif

#ifdef unused
static gboolean
gst_base_video_codec_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoCodec *base_video_codec;
  gboolean ret = FALSE;

  base_video_codec = GST_BASE_VIDEO_CODEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      ret = gst_pad_push_event (base_video_codec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_base_video_codec_reset (base_video_codec);
      ret = gst_pad_push_event (base_video_codec->srcpad, event);
      break;
    case GST_EVENT_EOS:
      if (gst_base_video_codec_push_all (base_video_codec,
              FALSE) == GST_FLOW_ERROR) {
        gst_event_unref (event);
        return FALSE;
      }

      ret = gst_pad_push_event (base_video_codec->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &time);

      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (rate <= 0.0)
        goto newseg_wrong_rate;

      GST_DEBUG ("newsegment %" GST_TIME_FORMAT " %" GST_TIME_FORMAT,
          GST_TIME_ARGS (start), GST_TIME_ARGS (time));
      gst_segment_set_newsegment (&base_video_codec->state.segment, update,
          rate, format, start, stop, time);

      ret = gst_pad_push_event (base_video_codec->srcpad, event);
      break;
    }
    default:
      ret = gst_pad_push_event (base_video_codec->srcpad, event);
      break;
  }
done:
  gst_object_unref (base_video_codec);
  return ret;

newseg_wrong_format:
  GST_DEBUG_OBJECT (base_video_codec, "received non TIME newsegment");
  gst_event_unref (event);
  goto done;

newseg_wrong_rate:
  GST_DEBUG_OBJECT (base_video_codec, "negative rates not supported");
  gst_event_unref (event);
  goto done;
}
#endif


static GstStateChangeReturn
gst_base_video_codec_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseVideoCodec *base_video_codec = GST_BASE_VIDEO_CODEC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_base_video_codec_reset (base_video_codec);
      break;
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
      gst_base_video_codec_reset (base_video_codec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

GstVideoFrame *
gst_base_video_codec_new_frame (GstBaseVideoCodec * base_video_codec)
{
  GstVideoFrame *frame;

  frame = g_malloc0 (sizeof (GstVideoFrame));

  frame->system_frame_number = base_video_codec->system_frame_number;
  base_video_codec->system_frame_number++;

  return frame;
}

void
gst_base_video_codec_free_frame (GstVideoFrame * frame)
{
  if (frame->sink_buffer) {
    gst_buffer_unref (frame->sink_buffer);
  }

  g_free (frame);
}
