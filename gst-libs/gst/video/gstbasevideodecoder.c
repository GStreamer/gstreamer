/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
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

#include "gstbasevideodecoder.h"

#include <string.h>

GST_DEBUG_CATEGORY (basevideodecoder_debug);
#define GST_CAT_DEFAULT basevideodecoder_debug

static void gst_base_video_decoder_finalize (GObject * object);

static gboolean gst_base_video_decoder_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static gboolean gst_base_video_decoder_sink_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_base_video_decoder_src_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_base_video_decoder_chain (GstPad * pad,
    GstBuffer * buf);
static gboolean gst_base_video_decoder_sink_query (GstPad * pad,
    GstQuery * query);
//static GstFlowReturn gst_base_video_decoder_process (GstBaseVideoDecoder *base_video_decoder);
static GstStateChangeReturn gst_base_video_decoder_change_state (GstElement *
    element, GstStateChange transition);
static const GstQueryType *gst_base_video_decoder_get_query_types (GstPad *
    pad);
static gboolean gst_base_video_decoder_src_query (GstPad * pad,
    GstQuery * query);
static gboolean gst_base_video_decoder_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value, GstFormat * dest_format,
    gint64 * dest_value);
static void gst_base_video_decoder_reset (GstBaseVideoDecoder *
    base_video_decoder);

static GstFlowReturn
gst_base_video_decoder_have_frame_2 (GstBaseVideoDecoder * base_video_decoder);

static guint64
gst_base_video_decoder_get_timestamp (GstBaseVideoDecoder * base_video_decoder,
    int picture_number);
static guint64
gst_base_video_decoder_get_field_timestamp (GstBaseVideoDecoder *
    base_video_decoder, int field_offset);
static guint64 gst_base_video_decoder_get_field_duration (GstBaseVideoDecoder *
    base_video_decoder, int n_fields);
static GstVideoFrame *gst_base_video_decoder_new_frame (GstBaseVideoDecoder *
    base_video_decoder);
static void gst_base_video_decoder_free_frame (GstVideoFrame * frame);

GST_BOILERPLATE (GstBaseVideoDecoder, gst_base_video_decoder,
    GstBaseVideoCodec, GST_TYPE_BASE_VIDEO_CODEC);

static void
gst_base_video_decoder_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (basevideodecoder_debug, "basevideodecoder", 0,
      "Base Video Decoder");

}

static void
gst_base_video_decoder_class_init (GstBaseVideoDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_base_video_decoder_finalize;

  gstelement_class->change_state = gst_base_video_decoder_change_state;

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_base_video_decoder_init (GstBaseVideoDecoder * base_video_decoder,
    GstBaseVideoDecoderClass * klass)
{
  GstPad *pad;

  GST_DEBUG ("gst_base_video_decoder_init");

  pad = GST_BASE_VIDEO_CODEC_SINK_PAD (base_video_decoder);

  gst_pad_set_chain_function (pad, gst_base_video_decoder_chain);
  gst_pad_set_event_function (pad, gst_base_video_decoder_sink_event);
  gst_pad_set_setcaps_function (pad, gst_base_video_decoder_sink_setcaps);
  gst_pad_set_query_function (pad, gst_base_video_decoder_sink_query);

  pad = GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder);

  gst_pad_set_event_function (pad, gst_base_video_decoder_src_event);
  gst_pad_set_query_type_function (pad, gst_base_video_decoder_get_query_types);
  gst_pad_set_query_function (pad, gst_base_video_decoder_src_query);
  gst_pad_use_fixed_caps (pad);

  base_video_decoder->input_adapter = gst_adapter_new ();
  base_video_decoder->output_adapter = gst_adapter_new ();

  gst_base_video_decoder_reset (base_video_decoder);

  base_video_decoder->current_frame =
      gst_base_video_decoder_new_frame (base_video_decoder);

  base_video_decoder->sink_clipping = TRUE;
}

static gboolean
gst_base_video_decoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseVideoDecoder *base_video_decoder;
  GstBaseVideoDecoderClass *base_video_decoder_class;
  GstStructure *structure;
  const GValue *codec_data;
  GstVideoState *state;
  gboolean ret = TRUE;

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));
  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  GST_DEBUG ("setcaps %" GST_PTR_FORMAT, caps);

  state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

  if (state->codec_data) {
    gst_buffer_unref (state->codec_data);
  }
  memset (state, 0, sizeof (GstVideoState));

  structure = gst_caps_get_structure (caps, 0);

  gst_video_format_parse_caps (caps, NULL, &state->width, &state->height);
  gst_video_parse_caps_framerate (caps, &state->fps_n, &state->fps_d);
  gst_video_parse_caps_pixel_aspect_ratio (caps, &state->par_n, &state->par_d);

  state->have_interlaced =
      gst_video_format_parse_caps_interlaced (caps, &state->interlaced);

  codec_data = gst_structure_get_value (structure, "codec_data");
  if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER) {
    state->codec_data = gst_value_get_buffer (codec_data);
  }

  if (base_video_decoder_class->start) {
    ret = base_video_decoder_class->start (base_video_decoder);
  }

  g_object_unref (base_video_decoder);

  return ret;
}

static void
gst_base_video_decoder_finalize (GObject * object)
{
  GstBaseVideoDecoder *base_video_decoder;

  base_video_decoder = GST_BASE_VIDEO_DECODER (object);

  gst_base_video_decoder_reset (base_video_decoder);

  if (base_video_decoder->input_adapter) {
    g_object_unref (base_video_decoder->input_adapter);
    base_video_decoder->input_adapter = NULL;
  }
  if (base_video_decoder->output_adapter) {
    g_object_unref (base_video_decoder->output_adapter);
    base_video_decoder->output_adapter = NULL;
  }

  GST_DEBUG_OBJECT (object, "finalize");

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_base_video_decoder_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoDecoder *base_video_decoder;
  GstBaseVideoDecoderClass *base_video_decoder_class;
  gboolean ret = FALSE;

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));
  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      if (!base_video_decoder->packetized) {
        GstFlowReturn flow_ret;

        do {
          flow_ret =
              base_video_decoder_class->parse_data (base_video_decoder, TRUE);
        } while (flow_ret == GST_FLOW_OK);
      }

      if (base_video_decoder_class->finish) {
        base_video_decoder_class->finish (base_video_decoder);
      }

      ret =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder),
          event);
    }
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      double rate;
      double applied_rate;
      GstFormat format;
      gint64 start;
      gint64 stop;
      gint64 position;
      GstSegment *segment = &GST_BASE_VIDEO_CODEC (base_video_decoder)->segment;

      gst_event_parse_new_segment_full (event, &update, &rate,
          &applied_rate, &format, &start, &stop, &position);

      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (!update) {
        gst_base_video_decoder_reset (base_video_decoder);
      }

      base_video_decoder->timestamp_offset = start;

      gst_segment_set_newsegment_full (segment,
          update, rate, applied_rate, format, start, stop, position);
      base_video_decoder->have_segment = TRUE;

      GST_DEBUG_OBJECT (base_video_decoder,
          "new segment: format %d rate %g start %" GST_TIME_FORMAT
          " stop %" GST_TIME_FORMAT
          " position %" GST_TIME_FORMAT
          " update %d",
          format, rate,
          GST_TIME_ARGS (segment->start),
          GST_TIME_ARGS (segment->stop), GST_TIME_ARGS (segment->time), update);

      ret =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder),
          event);
    }
      break;
    case GST_EVENT_FLUSH_STOP:{
      GST_OBJECT_LOCK (base_video_decoder);
      GST_BASE_VIDEO_CODEC (base_video_decoder)->earliest_time =
          GST_CLOCK_TIME_NONE;
      GST_BASE_VIDEO_CODEC (base_video_decoder)->proportion = 0.5;
      gst_segment_init (&GST_BASE_VIDEO_CODEC (base_video_decoder)->segment,
          GST_FORMAT_TIME);
      GST_OBJECT_UNLOCK (base_video_decoder);
    }
    default:
      /* FIXME this changes the order of events */
      ret =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder),
          event);
      break;
  }

done:
  gst_object_unref (base_video_decoder);
  return ret;

newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (base_video_decoder, "received non TIME newsegment");
    gst_event_unref (event);
    goto done;
  }
}

static gboolean
gst_base_video_decoder_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoDecoder *base_video_decoder;
  gboolean res = FALSE;

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format, tformat;
      gdouble rate;
      GstEvent *real_seek;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gint64 tcur = -1, tstop = -1;

      GST_DEBUG ("seek event");

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type,
          &cur, &stop_type, &stop);
      gst_event_unref (event);

      tformat = GST_FORMAT_TIME;
      res =
          gst_base_video_decoder_src_convert (pad, format, cur, &tformat,
          &tcur);
      if (!res)
        goto convert_error;
      res =
          gst_base_video_decoder_src_convert (pad, format, stop, &tformat,
          &tstop);
      if (!res)
        goto convert_error;

      real_seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);

      res =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SINK_PAD
          (base_video_decoder), real_seek);

      break;
    }
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      GstClockTime duration;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      GST_OBJECT_LOCK (base_video_decoder);
      GST_BASE_VIDEO_CODEC (base_video_decoder)->proportion = proportion;
      if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (timestamp))) {
        if (G_UNLIKELY (diff > 0)) {
          if (GST_BASE_VIDEO_CODEC (base_video_decoder)->state.fps_n > 0)
            duration =
                gst_util_uint64_scale (GST_SECOND,
                GST_BASE_VIDEO_CODEC (base_video_decoder)->state.fps_d,
                GST_BASE_VIDEO_CODEC (base_video_decoder)->state.fps_n);
          else
            duration = 0;
          GST_BASE_VIDEO_CODEC (base_video_decoder)->earliest_time =
              timestamp + 2 * diff + duration;
        } else {
          GST_BASE_VIDEO_CODEC (base_video_decoder)->earliest_time =
              timestamp + diff;
        }
      } else {
        GST_BASE_VIDEO_CODEC (base_video_decoder)->earliest_time =
            GST_CLOCK_TIME_NONE;
      }
      GST_OBJECT_UNLOCK (base_video_decoder);

      GST_DEBUG_OBJECT (base_video_decoder,
          "got QoS %" GST_TIME_FORMAT ", %" G_GINT64_FORMAT ", %g",
          GST_TIME_ARGS (timestamp), diff, proportion);

      res =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SINK_PAD
          (base_video_decoder), event);
      break;
    }
    default:
      res =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SINK_PAD
          (base_video_decoder), event);
      break;
  }
done:
  gst_object_unref (base_video_decoder);
  return res;

convert_error:
  GST_DEBUG_OBJECT (base_video_decoder, "could not convert format");
  goto done;
}


#if 0
static gboolean
gst_base_video_decoder_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstBaseVideoDecoder *enc;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  enc = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));

  /* FIXME: check if we are in a decoding state */

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
#if 0
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale_int (src_value, 1,
              enc->bytes_per_picture);
          break;
#endif
        case GST_FORMAT_TIME:
          /* seems like a rather silly conversion, implement me if you like */
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (src_value,
              GST_SECOND * enc->fps_d, enc->fps_n);
          break;
#if 0
        case GST_FORMAT_BYTES:
          *dest_value = gst_util_uint64_scale_int (src_value,
              enc->bytes_per_picture, 1);
          break;
#endif
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
      break;
  }
}
#endif

static gboolean
gst_base_video_decoder_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstBaseVideoDecoder *enc;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  enc = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));

  /* FIXME: check if we are in a encoding state */

  GST_DEBUG ("src convert");
  switch (src_format) {
#if 0
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (granulepos_to_frame (src_value),
              enc->fps_d * GST_SECOND, enc->fps_n);
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
              enc->fps_n, enc->fps_d * GST_SECOND);
          break;
        }
        default:
          res = FALSE;
          break;
      }
      break;
#endif
    default:
      res = FALSE;
      break;
  }

  gst_object_unref (enc);

  return res;
}

static const GstQueryType *
gst_base_video_decoder_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return query_types;
}

static gboolean
gst_base_video_decoder_src_query (GstPad * pad, GstQuery * query)
{
  GstBaseVideoDecoder *enc;
  gboolean res = TRUE;

  enc = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));

  switch GST_QUERY_TYPE
    (query) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time;

      gst_query_parse_position (query, &format, NULL);
      GST_DEBUG ("query in format %d", format);

      if (format != GST_FORMAT_TIME) {
        goto error;
      }

      time = enc->last_timestamp;
      time =
          gst_segment_to_stream_time (&GST_BASE_VIDEO_CODEC (enc)->segment,
          GST_FORMAT_TIME, time);

      gst_query_set_position (query, format, time);

      res = TRUE;

      break;
    }
    case GST_QUERY_DURATION:
    {
      res = gst_pad_peer_query (enc->base_video_codec.sinkpad, query);
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      GST_DEBUG ("convert query");

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_base_video_decoder_src_convert (pad, src_fmt, src_val, &dest_fmt,
          &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
    }
  gst_object_unref (enc);
  return res;

error:
  GST_ERROR_OBJECT (enc, "query failed");
  gst_object_unref (enc);
  return res;
}

static gboolean
gst_base_video_decoder_sink_query (GstPad * pad, GstQuery * query)
{
  GstBaseVideoDecoder *base_video_decoder;
  gboolean res = FALSE;

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (base_video_decoder, "sink query fps=%d/%d",
      GST_BASE_VIDEO_CODEC (base_video_decoder)->state.fps_n,
      GST_BASE_VIDEO_CODEC (base_video_decoder)->state.fps_d);
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_base_video_rawvideo_convert (&GST_BASE_VIDEO_CODEC
          (base_video_decoder)->state, src_fmt, src_val, &dest_fmt, &dest_val);
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
  gst_object_unref (base_video_decoder);

  return res;
error:
  GST_DEBUG_OBJECT (base_video_decoder, "query failed");
  goto done;
}


#if 0
static gboolean
gst_pad_is_negotiated (GstPad * pad)
{
  GstCaps *caps;

  g_return_val_if_fail (pad != NULL, FALSE);

  caps = gst_pad_get_negotiated_caps (pad);
  if (caps) {
    gst_caps_unref (caps);
    return TRUE;
  }

  return FALSE;
}
#endif

typedef struct _Timestamp Timestamp;
struct _Timestamp
{
  guint64 offset;
  GstClockTime timestamp;
  GstClockTime duration;
};

static void
gst_base_video_decoder_add_timestamp (GstBaseVideoDecoder * base_video_decoder,
    GstBuffer * buffer)
{
  Timestamp *ts;

  ts = g_malloc (sizeof (Timestamp));

  GST_DEBUG ("adding timestamp %" GST_TIME_FORMAT " %" GST_TIME_FORMAT,
      GST_TIME_ARGS (base_video_decoder->input_offset),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  ts->offset = base_video_decoder->input_offset;
  ts->timestamp = GST_BUFFER_TIMESTAMP (buffer);
  ts->duration = GST_BUFFER_DURATION (buffer);

  base_video_decoder->timestamps =
      g_list_append (base_video_decoder->timestamps, ts);
}

static void
gst_base_video_decoder_get_timestamp_at_offset (GstBaseVideoDecoder *
    base_video_decoder, guint64 offset, GstClockTime * timestamp,
    GstClockTime * duration)
{
  Timestamp *ts;
  GList *g;

  *timestamp = GST_CLOCK_TIME_NONE;
  *duration = GST_CLOCK_TIME_NONE;

  g = base_video_decoder->timestamps;
  while (g) {
    ts = g->data;
    if (ts->offset <= offset) {
      *timestamp = ts->timestamp;
      *duration = ts->duration;
      g_free (ts);
      g = g_list_next (g);
      base_video_decoder->timestamps =
          g_list_remove (base_video_decoder->timestamps, ts);
    } else {
      break;
    }
  }

  GST_DEBUG ("got timestamp %" GST_TIME_FORMAT " %" GST_TIME_FORMAT,
      GST_TIME_ARGS (offset), GST_TIME_ARGS (*timestamp));
}

static void
gst_base_video_decoder_reset (GstBaseVideoDecoder * base_video_decoder)
{
  GstBaseVideoDecoderClass *base_video_decoder_class;

  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  GST_DEBUG ("reset");

  base_video_decoder->started = FALSE;

  base_video_decoder->discont = TRUE;
  base_video_decoder->have_sync = FALSE;

  base_video_decoder->timestamp_offset = GST_CLOCK_TIME_NONE;
  GST_BASE_VIDEO_CODEC (base_video_decoder)->system_frame_number = 0;
  base_video_decoder->presentation_frame_number = 0;
  base_video_decoder->base_picture_number = 0;
  base_video_decoder->last_timestamp = GST_CLOCK_TIME_NONE;

  base_video_decoder->input_offset = 0;
  base_video_decoder->frame_offset = 0;

  /* This function could be called from finalize() */
  if (base_video_decoder->input_adapter) {
    gst_adapter_clear (base_video_decoder->input_adapter);
  }
  if (base_video_decoder->output_adapter) {
    gst_adapter_clear (base_video_decoder->output_adapter);
  }
  //gst_segment_init (&base_video_decoder->segment, GST_FORMAT_TIME);

  if (base_video_decoder->current_frame) {
    gst_base_video_decoder_free_frame (base_video_decoder->current_frame);
    base_video_decoder->current_frame = NULL;
  }

  base_video_decoder->have_src_caps = FALSE;

  GST_OBJECT_LOCK (base_video_decoder);
  GST_BASE_VIDEO_CODEC (base_video_decoder)->earliest_time =
      GST_CLOCK_TIME_NONE;
  GST_BASE_VIDEO_CODEC (base_video_decoder)->proportion = 0.5;
  GST_OBJECT_UNLOCK (base_video_decoder);

  if (base_video_decoder_class->reset) {
    base_video_decoder_class->reset (base_video_decoder);
  }
}

static GstFlowReturn
gst_base_video_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseVideoDecoder *base_video_decoder;
  GstBaseVideoDecoderClass *klass;
  GstFlowReturn ret;

  GST_DEBUG ("chain %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT " size %d",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_SIZE (buf));

#if 0
  /* requiring the pad to be negotiated makes it impossible to use
   * oggdemux or filesrc ! decoder */
  if (!gst_pad_is_negotiated (pad)) {
    GST_DEBUG ("not negotiated");
    return GST_FLOW_NOT_NEGOTIATED;
  }
#endif

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));
  klass = GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  GST_DEBUG_OBJECT (base_video_decoder, "chain");

  if (!base_video_decoder->have_segment) {
    GstEvent *event;
    GstFlowReturn ret;

    GST_WARNING_OBJECT (base_video_decoder,
        "Received buffer without a new-segment. Assuming timestamps start from 0.");

    gst_segment_set_newsegment_full (&GST_BASE_VIDEO_CODEC
        (base_video_decoder)->segment, FALSE, 1.0, 1.0, GST_FORMAT_TIME, 0,
        GST_CLOCK_TIME_NONE, 0);
    base_video_decoder->have_segment = TRUE;

    event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0,
        GST_CLOCK_TIME_NONE, 0);

    ret =
        gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder),
        event);
    if (!ret) {
#if 0
      /* Other base classes tend to ignore the return value */
      GST_ERROR ("new segment event ret=%d", ret);
      return GST_FLOW_ERROR;
#endif
    }
  }

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (base_video_decoder, "received DISCONT buffer");
    gst_base_video_decoder_reset (base_video_decoder);
  }

  if (!base_video_decoder->started) {
    klass->start (base_video_decoder);
    base_video_decoder->started = TRUE;
  }

  if (base_video_decoder->current_frame == NULL) {
    base_video_decoder->current_frame =
        gst_base_video_decoder_new_frame (base_video_decoder);
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    gst_base_video_decoder_add_timestamp (base_video_decoder, buf);
  }
  base_video_decoder->input_offset += GST_BUFFER_SIZE (buf);

#if 0
  if (base_video_decoder->timestamp_offset == GST_CLOCK_TIME_NONE &&
      GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE) {
    GST_DEBUG ("got new offset %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
    base_video_decoder->timestamp_offset = GST_BUFFER_TIMESTAMP (buf);
  }
#endif

  if (base_video_decoder->packetized) {
    base_video_decoder->current_frame->sink_buffer = buf;

    ret = gst_base_video_decoder_have_frame_2 (base_video_decoder);
  } else {

    gst_adapter_push (base_video_decoder->input_adapter, buf);

    if (!base_video_decoder->have_sync) {
      int n, m;

      GST_DEBUG ("no sync, scanning");

      n = gst_adapter_available (base_video_decoder->input_adapter);
      if (klass->capture_mask != 0) {
        m = gst_adapter_masked_scan_uint32 (base_video_decoder->input_adapter,
            klass->capture_mask, klass->capture_pattern, 0, n - 3);
      } else if (klass->scan_for_sync) {
        m = klass->scan_for_sync (base_video_decoder, FALSE, 0, n);
      } else {
        m = 0;
      }
      if (m == -1) {
        GST_ERROR ("scan returned no sync");
        gst_adapter_flush (base_video_decoder->input_adapter, n - 3);

        gst_object_unref (base_video_decoder);
        return GST_FLOW_OK;
      } else {
        if (m > 0) {
          if (m >= n) {
            GST_ERROR ("subclass scanned past end %d >= %d", m, n);
          }

          gst_adapter_flush (base_video_decoder->input_adapter, m);

          if (m < n) {
            GST_DEBUG ("found possible sync after %d bytes (of %d)", m, n);

            /* this is only "maybe" sync */
            base_video_decoder->have_sync = TRUE;
          }
        }

      }
    }

    do {
      ret = klass->parse_data (base_video_decoder, FALSE);
    } while (ret == GST_FLOW_OK);

    if (ret == GST_BASE_VIDEO_DECODER_FLOW_NEED_DATA) {
      gst_object_unref (base_video_decoder);
      return GST_FLOW_OK;
    }
  }

  gst_object_unref (base_video_decoder);
  return ret;
}

static GstStateChangeReturn
gst_base_video_decoder_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseVideoDecoder *base_video_decoder;
  GstBaseVideoDecoderClass *base_video_decoder_class;
  GstStateChangeReturn ret;

  base_video_decoder = GST_BASE_VIDEO_DECODER (element);
  base_video_decoder_class = GST_BASE_VIDEO_DECODER_GET_CLASS (element);

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (base_video_decoder_class->stop) {
        base_video_decoder_class->stop (base_video_decoder);
      }
      gst_segment_init (&GST_BASE_VIDEO_CODEC (base_video_decoder)->segment,
          GST_FORMAT_TIME);
      g_list_foreach (base_video_decoder->timestamps, (GFunc) g_free, NULL);
      g_list_free (base_video_decoder->timestamps);
      base_video_decoder->timestamps = NULL;
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_base_video_decoder_free_frame (GstVideoFrame * frame)
{
  g_return_if_fail (frame != NULL);

  if (frame->sink_buffer) {
    gst_buffer_unref (frame->sink_buffer);
  }
  if (frame->src_buffer) {
    gst_buffer_unref (frame->src_buffer);
  }

  g_free (frame);
}

static GstVideoFrame *
gst_base_video_decoder_new_frame (GstBaseVideoDecoder * base_video_decoder)
{
  GstVideoFrame *frame;

  frame = g_malloc0 (sizeof (GstVideoFrame));

  frame->system_frame_number =
      GST_BASE_VIDEO_CODEC (base_video_decoder)->system_frame_number;
  GST_BASE_VIDEO_CODEC (base_video_decoder)->system_frame_number++;

  frame->decode_frame_number = frame->system_frame_number -
      base_video_decoder->reorder_depth;

  frame->decode_timestamp = GST_CLOCK_TIME_NONE;
  frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
  frame->presentation_duration = GST_CLOCK_TIME_NONE;
  frame->n_fields = 2;

  return frame;
}

GstFlowReturn
gst_base_video_decoder_finish_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;
  GstBuffer *src_buffer;

  GST_DEBUG ("finish frame");
  GST_DEBUG ("n %d in %d out %d",
      g_list_length (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames),
      gst_adapter_available (base_video_decoder->input_adapter),
      gst_adapter_available (base_video_decoder->output_adapter));

  GST_DEBUG ("finish frame sync=%d pts=%" GST_TIME_FORMAT, frame->is_sync_point,
      GST_TIME_ARGS (frame->presentation_timestamp));

  if (GST_CLOCK_TIME_IS_VALID (frame->presentation_timestamp)) {
    if (frame->presentation_timestamp != base_video_decoder->timestamp_offset) {
      GST_DEBUG ("sync timestamp %" GST_TIME_FORMAT " diff %" GST_TIME_FORMAT,
          GST_TIME_ARGS (frame->presentation_timestamp),
          GST_TIME_ARGS (frame->presentation_timestamp -
              GST_BASE_VIDEO_CODEC (base_video_decoder)->segment.start));
      base_video_decoder->timestamp_offset = frame->presentation_timestamp;
      base_video_decoder->field_index &= 1;
    } else {
      /* This case is for one initial timestamp and no others, e.g.,
       * filesrc ! decoder ! xvimagesink */
      GST_WARNING ("sync timestamp didn't change, ignoring");
      frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
    }
  } else {
    if (frame->is_sync_point) {
      GST_WARNING ("sync point doesn't have timestamp");
      if (!GST_CLOCK_TIME_IS_VALID (base_video_decoder->timestamp_offset)) {
        GST_WARNING
            ("No base timestamp.  Assuming frames start at segment start");
        base_video_decoder->timestamp_offset =
            GST_BASE_VIDEO_CODEC (base_video_decoder)->segment.start;
        base_video_decoder->field_index &= 1;
      }
    }
  }
  frame->field_index = base_video_decoder->field_index;
  base_video_decoder->field_index += frame->n_fields;

  if (frame->presentation_timestamp == GST_CLOCK_TIME_NONE) {
    frame->presentation_timestamp =
        gst_base_video_decoder_get_field_timestamp (base_video_decoder,
        frame->field_index);
    frame->presentation_duration = GST_CLOCK_TIME_NONE;
    frame->decode_timestamp =
        gst_base_video_decoder_get_timestamp (base_video_decoder,
        frame->decode_frame_number);
  }
  if (frame->presentation_duration == GST_CLOCK_TIME_NONE) {
    frame->presentation_duration =
        gst_base_video_decoder_get_field_duration (base_video_decoder,
        frame->n_fields);
  }

  if (GST_CLOCK_TIME_IS_VALID (base_video_decoder->last_timestamp)) {
    if (frame->presentation_timestamp < base_video_decoder->last_timestamp) {
      GST_WARNING ("decreasing timestamp (%" GST_TIME_FORMAT " < %"
          GST_TIME_FORMAT ")", GST_TIME_ARGS (frame->presentation_timestamp),
          GST_TIME_ARGS (base_video_decoder->last_timestamp));
    }
  }
  base_video_decoder->last_timestamp = frame->presentation_timestamp;

  src_buffer = gst_buffer_make_metadata_writable (frame->src_buffer);
  frame->src_buffer = NULL;

  GST_BUFFER_FLAG_UNSET (src_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  if (state->interlaced) {
    int tff = state->top_field_first;

    if (frame->field_index & 1) {
      tff ^= 1;
    }
    if (tff) {
      GST_BUFFER_FLAG_SET (src_buffer, GST_VIDEO_BUFFER_TFF);
    } else {
      GST_BUFFER_FLAG_UNSET (src_buffer, GST_VIDEO_BUFFER_TFF);
    }
    GST_BUFFER_FLAG_UNSET (src_buffer, GST_VIDEO_BUFFER_RFF);
    GST_BUFFER_FLAG_UNSET (src_buffer, GST_VIDEO_BUFFER_ONEFIELD);
    if (frame->n_fields == 3) {
      GST_BUFFER_FLAG_SET (src_buffer, GST_VIDEO_BUFFER_RFF);
    } else if (frame->n_fields == 1) {
      GST_BUFFER_FLAG_SET (src_buffer, GST_VIDEO_BUFFER_ONEFIELD);
    }
  }
  if (base_video_decoder->discont) {
    GST_BUFFER_FLAG_SET (src_buffer, GST_BUFFER_FLAG_DISCONT);
    base_video_decoder->discont = FALSE;
  }

  GST_BUFFER_TIMESTAMP (src_buffer) = frame->presentation_timestamp;
  GST_BUFFER_DURATION (src_buffer) = frame->presentation_duration;
  GST_BUFFER_OFFSET (src_buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (src_buffer) = GST_BUFFER_OFFSET_NONE;

  GST_DEBUG ("pushing frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->presentation_timestamp));

  GST_BASE_VIDEO_CODEC (base_video_decoder)->frames =
      g_list_remove (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames, frame);

  gst_base_video_decoder_set_src_caps (base_video_decoder);
  gst_buffer_set_caps (src_buffer,
      GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder)));

  gst_base_video_decoder_free_frame (frame);

  if (base_video_decoder->sink_clipping) {
    gint64 start = GST_BUFFER_TIMESTAMP (src_buffer);
    gint64 stop = GST_BUFFER_TIMESTAMP (src_buffer) +
        GST_BUFFER_DURATION (src_buffer);
    GstSegment *segment = &GST_BASE_VIDEO_CODEC (base_video_decoder)->segment;

    if (gst_segment_clip (segment, GST_FORMAT_TIME, start, stop, &start, &stop)) {
      GST_BUFFER_TIMESTAMP (src_buffer) = start;
      GST_BUFFER_DURATION (src_buffer) = stop - start;
      GST_DEBUG ("accepting buffer inside segment: %" GST_TIME_FORMAT
          " %" GST_TIME_FORMAT
          " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
          " time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer)),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer) +
              GST_BUFFER_DURATION (src_buffer)),
          GST_TIME_ARGS (segment->start),
          GST_TIME_ARGS (segment->stop), GST_TIME_ARGS (segment->time));
    } else {
      GST_DEBUG ("dropping buffer outside segment: %" GST_TIME_FORMAT
          " %" GST_TIME_FORMAT
          " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
          " time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer)),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer) +
              GST_BUFFER_DURATION (src_buffer)),
          GST_TIME_ARGS (segment->start),
          GST_TIME_ARGS (segment->stop), GST_TIME_ARGS (segment->time));
      gst_buffer_unref (src_buffer);
      return GST_FLOW_OK;
    }
  }

  return gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder),
      src_buffer);
}

GstFlowReturn
gst_base_video_decoder_skip_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame)
{
  GST_DEBUG ("finish frame");
  GST_DEBUG ("finish frame sync=%d pts=%" GST_TIME_FORMAT, frame->is_sync_point,
      GST_TIME_ARGS (frame->presentation_timestamp));

  if (GST_CLOCK_TIME_IS_VALID (frame->presentation_timestamp)) {
    if (frame->presentation_timestamp != base_video_decoder->timestamp_offset) {
      GST_DEBUG ("sync timestamp %" GST_TIME_FORMAT " diff %" GST_TIME_FORMAT,
          GST_TIME_ARGS (frame->presentation_timestamp),
          GST_TIME_ARGS (frame->presentation_timestamp -
              GST_BASE_VIDEO_CODEC (base_video_decoder)->segment.start));
      base_video_decoder->timestamp_offset = frame->presentation_timestamp;
      base_video_decoder->field_index = 0;
    } else {
      /* This case is for one initial timestamp and no others, e.g.,
       * filesrc ! decoder ! xvimagesink */
      GST_WARNING ("sync timestamp didn't change, ignoring");
      frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
    }
  } else {
    if (frame->is_sync_point) {
      GST_WARNING ("sync point doesn't have timestamp");
      if (GST_CLOCK_TIME_IS_VALID (base_video_decoder->timestamp_offset)) {
        GST_WARNING
            ("No base timestamp.  Assuming frames start at segment start");
        base_video_decoder->timestamp_offset =
            GST_BASE_VIDEO_CODEC (base_video_decoder)->segment.start;
        base_video_decoder->field_index = 0;
      }
    }
  }
  frame->field_index = base_video_decoder->field_index;
  base_video_decoder->field_index += frame->n_fields;

  if (frame->presentation_timestamp == GST_CLOCK_TIME_NONE) {
    frame->presentation_timestamp =
        gst_base_video_decoder_get_field_timestamp (base_video_decoder,
        frame->field_index);
    frame->presentation_duration = GST_CLOCK_TIME_NONE;
    frame->decode_timestamp =
        gst_base_video_decoder_get_timestamp (base_video_decoder,
        frame->decode_frame_number);
  }
  if (frame->presentation_duration == GST_CLOCK_TIME_NONE) {
    frame->presentation_duration =
        gst_base_video_decoder_get_field_duration (base_video_decoder,
        frame->n_fields);
  }

  base_video_decoder->last_timestamp = frame->presentation_timestamp;

  GST_DEBUG ("skipping frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->presentation_timestamp));

  GST_BASE_VIDEO_CODEC (base_video_decoder)->frames =
      g_list_remove (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames, frame);

  gst_base_video_decoder_free_frame (frame);

  return GST_FLOW_OK;
}

int
gst_base_video_decoder_get_height (GstBaseVideoDecoder * base_video_decoder)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

  return state->height;
}

int
gst_base_video_decoder_get_width (GstBaseVideoDecoder * base_video_decoder)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

  return state->width;
}

GstFlowReturn
gst_base_video_decoder_end_of_stream (GstBaseVideoDecoder * base_video_decoder,
    GstBuffer * buffer)
{

  if (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames) {
    GST_DEBUG ("EOS with frames left over");
  }

  return gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder),
      buffer);
}

void
gst_base_video_decoder_add_to_frame (GstBaseVideoDecoder * base_video_decoder,
    int n_bytes)
{
  GstBuffer *buf;

  GST_DEBUG ("add to frame");

  if (n_bytes == 0)
    return;

  if (gst_adapter_available (base_video_decoder->output_adapter) == 0) {
    base_video_decoder->frame_offset = base_video_decoder->input_offset -
        gst_adapter_available (base_video_decoder->input_adapter);
  }
  buf = gst_adapter_take_buffer (base_video_decoder->input_adapter, n_bytes);

  gst_adapter_push (base_video_decoder->output_adapter, buf);
}

static guint64
gst_base_video_decoder_get_timestamp (GstBaseVideoDecoder * base_video_decoder,
    int picture_number)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

  if (state->fps_d == 0) {
    return -1;
  }
  if (picture_number < base_video_decoder->base_picture_number) {
    return base_video_decoder->timestamp_offset -
        (gint64) gst_util_uint64_scale (base_video_decoder->base_picture_number
        - picture_number, state->fps_d * GST_SECOND, state->fps_n);
  } else {
    return base_video_decoder->timestamp_offset +
        gst_util_uint64_scale (picture_number -
        base_video_decoder->base_picture_number,
        state->fps_d * GST_SECOND, state->fps_n);
  }
}

static guint64
gst_base_video_decoder_get_field_timestamp (GstBaseVideoDecoder *
    base_video_decoder, int field_offset)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

  if (state->fps_d == 0) {
    return GST_CLOCK_TIME_NONE;
  }
  if (field_offset < 0) {
    GST_WARNING ("field offset < 0");
    return GST_CLOCK_TIME_NONE;
  }
  return base_video_decoder->timestamp_offset +
      gst_util_uint64_scale (field_offset, state->fps_d * GST_SECOND,
      state->fps_n * 2);
}

static guint64
gst_base_video_decoder_get_field_duration (GstBaseVideoDecoder *
    base_video_decoder, int n_fields)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

  if (state->fps_d == 0) {
    return GST_CLOCK_TIME_NONE;
  }
  if (n_fields < 0) {
    GST_WARNING ("n_fields < 0");
    return GST_CLOCK_TIME_NONE;
  }
  return gst_util_uint64_scale (n_fields, state->fps_d * GST_SECOND,
      state->fps_n * 2);
}


GstFlowReturn
gst_base_video_decoder_have_frame (GstBaseVideoDecoder * base_video_decoder)
{
  GstBuffer *buffer;
  int n_available;
  GstClockTime timestamp;
  GstClockTime duration;

  GST_DEBUG ("have_frame");

  n_available = gst_adapter_available (base_video_decoder->output_adapter);
  if (n_available) {
    buffer = gst_adapter_take_buffer (base_video_decoder->output_adapter,
        n_available);
  } else {
    buffer = gst_buffer_new_and_alloc (0);
  }

  base_video_decoder->current_frame->sink_buffer = buffer;

  gst_base_video_decoder_get_timestamp_at_offset (base_video_decoder,
      base_video_decoder->frame_offset, &timestamp, &duration);

  GST_BUFFER_TIMESTAMP (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = duration;

  return gst_base_video_decoder_have_frame_2 (base_video_decoder);
}

static GstFlowReturn
gst_base_video_decoder_have_frame_2 (GstBaseVideoDecoder * base_video_decoder)
{
  GstVideoFrame *frame = base_video_decoder->current_frame;
  GstBaseVideoDecoderClass *base_video_decoder_class;
  GstFlowReturn ret = GST_FLOW_OK;

  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  frame->distance_from_sync = base_video_decoder->distance_from_sync;
  base_video_decoder->distance_from_sync++;

  frame->presentation_timestamp = GST_BUFFER_TIMESTAMP (frame->sink_buffer);
  frame->presentation_duration = GST_BUFFER_DURATION (frame->sink_buffer);

  GST_DEBUG ("pts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->presentation_timestamp));
  GST_DEBUG ("dts %" GST_TIME_FORMAT, GST_TIME_ARGS (frame->decode_timestamp));
  GST_DEBUG ("dist %d", frame->distance_from_sync);

  GST_BASE_VIDEO_CODEC (base_video_decoder)->frames =
      g_list_append (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames, frame);

  frame->deadline =
      gst_segment_to_running_time (&GST_BASE_VIDEO_CODEC
      (base_video_decoder)->segment, GST_FORMAT_TIME,
      frame->presentation_timestamp);

  /* do something with frame */
  ret = base_video_decoder_class->handle_frame (base_video_decoder, frame);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG ("flow error!");
  }

  /* create new frame */
  base_video_decoder->current_frame =
      gst_base_video_decoder_new_frame (base_video_decoder);

  return ret;
}

GstVideoState *
gst_base_video_decoder_get_state (GstBaseVideoDecoder * base_video_decoder)
{
  return &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

}

void
gst_base_video_decoder_set_state (GstBaseVideoDecoder * base_video_decoder,
    GstVideoState * state)
{
  memcpy (&GST_BASE_VIDEO_CODEC (base_video_decoder)->state,
      state, sizeof (*state));

}

void
gst_base_video_decoder_lost_sync (GstBaseVideoDecoder * base_video_decoder)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_DECODER (base_video_decoder));

  GST_DEBUG ("lost_sync");

  if (gst_adapter_available (base_video_decoder->input_adapter) >= 1) {
    gst_adapter_flush (base_video_decoder->input_adapter, 1);
  }

  base_video_decoder->have_sync = FALSE;
}

void
gst_base_video_decoder_set_sync_point (GstBaseVideoDecoder * base_video_decoder)
{
  GST_DEBUG ("set_sync_point");

  base_video_decoder->current_frame->is_sync_point = TRUE;
  base_video_decoder->distance_from_sync = 0;
}

GstVideoFrame *
gst_base_video_decoder_get_oldest_frame (GstBaseVideoDecoder *
    base_video_decoder)
{
  GList *g;

  g = g_list_first (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames);

  if (g == NULL)
    return NULL;
  return (GstVideoFrame *) (g->data);
}

GstVideoFrame *
gst_base_video_decoder_get_frame (GstBaseVideoDecoder * base_video_decoder,
    int frame_number)
{
  GList *g;

  for (g = g_list_first (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames);
      g; g = g_list_next (g)) {
    GstVideoFrame *frame = g->data;

    if (frame->system_frame_number == frame_number) {
      return frame;
    }
  }

  return NULL;
}

void
gst_base_video_decoder_set_src_caps (GstBaseVideoDecoder * base_video_decoder)
{
  GstCaps *caps;
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

  if (base_video_decoder->have_src_caps)
    return;

  caps = gst_video_format_new_caps (state->format,
      state->width, state->height,
      state->fps_n, state->fps_d, state->par_n, state->par_d);
  gst_caps_set_simple (caps, "interlaced",
      G_TYPE_BOOLEAN, state->interlaced, NULL);

  GST_DEBUG ("setting caps %" GST_PTR_FORMAT, caps);

  gst_pad_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder), caps);

  base_video_decoder->have_src_caps = TRUE;

  gst_caps_unref (caps);
}

GstBuffer *
gst_base_video_decoder_alloc_src_buffer (GstBaseVideoDecoder *
    base_video_decoder)
{
  GstBuffer *buffer;
  GstFlowReturn flow_ret;
  int num_bytes;
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

  num_bytes = gst_video_format_get_size (state->format, state->width,
      state->height);
  flow_ret =
      gst_pad_alloc_buffer_and_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD
      (base_video_decoder), GST_BUFFER_OFFSET_NONE, num_bytes,
      GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder)),
      &buffer);

  if (flow_ret != GST_FLOW_OK) {
    buffer = gst_buffer_new_and_alloc (num_bytes);
    gst_buffer_set_caps (buffer,
        GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder)));
  }

  return buffer;
}

GstFlowReturn
gst_base_video_decoder_alloc_src_frame (GstBaseVideoDecoder *
    base_video_decoder, GstVideoFrame * frame)
{
  GstFlowReturn flow_ret;
  int num_bytes;
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

  gst_base_video_decoder_set_src_caps (base_video_decoder);

  num_bytes = gst_video_format_get_size (state->format, state->width,
      state->height);
  flow_ret =
      gst_pad_alloc_buffer_and_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD
      (base_video_decoder), GST_BUFFER_OFFSET_NONE, num_bytes,
      GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder)),
      &frame->src_buffer);

  if (flow_ret != GST_FLOW_OK) {
    GST_WARNING ("failed to get buffer");
  }

  return flow_ret;
}

GstClockTimeDiff
gst_base_video_decoder_get_max_decode_time (GstBaseVideoDecoder *
    base_video_decoder, GstVideoFrame * frame)
{
  GstClockTimeDiff deadline;
  GstClockTime earliest_time;

  earliest_time = GST_BASE_VIDEO_CODEC (base_video_decoder)->earliest_time;
  if (GST_CLOCK_TIME_IS_VALID (earliest_time))
    deadline = GST_CLOCK_DIFF (earliest_time, frame->deadline);
  else
    deadline = G_MAXINT64;

  return deadline;
}

void
gst_base_video_decoder_class_set_capture_pattern (GstBaseVideoDecoderClass *
    base_video_decoder_class, guint32 mask, guint32 pattern)
{
  g_return_if_fail (((~mask) & pattern) == 0);

  base_video_decoder_class->capture_mask = mask;
  base_video_decoder_class->capture_pattern = pattern;
}
