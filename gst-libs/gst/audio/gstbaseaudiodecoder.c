/* GStreamer
 * Copyright (C) 2009 Igalia S.L.
 * Author: Iago Toral <itoral@igalia.com>
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

#include "gstbaseaudiodecoder.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (baseaudio_debug);
#define GST_CAT_DEFAULT baseaudio_debug

static void gst_base_audio_decoder_finalize (GObject * object);

static gboolean gst_base_audio_decoder_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static gboolean gst_base_audio_decoder_sink_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_base_audio_decoder_src_event (GstPad * pad,
    GstEvent * event);
static GstFlowReturn gst_base_audio_decoder_chain (GstPad * pad,
    GstBuffer * buf);
static gboolean gst_base_audio_decoder_sink_query (GstPad * pad,
    GstQuery * query);
static GstStateChangeReturn gst_base_audio_decoder_change_state (GstElement *
    element, GstStateChange transition);
static const GstQueryType *gst_base_audio_decoder_get_query_types (GstPad *
    pad);
static gboolean gst_base_audio_decoder_src_query (GstPad * pad,
    GstQuery * query);
static gboolean gst_base_audio_decoder_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value, GstFormat * dest_format,
    gint64 * dest_value);
static void gst_base_audio_decoder_reset (GstBaseAudioDecoder *
    base_audio_decoder);

static guint64
gst_base_audio_decoder_get_timestamp (GstBaseAudioDecoder * base_audio_decoder,
    int picture_number);
static guint64
gst_base_audio_decoder_get_field_timestamp (GstBaseAudioDecoder *
    base_audio_decoder, int field_offset);
static GstAudioFrame *gst_base_audio_decoder_new_frame (GstBaseAudioDecoder *
    base_audio_decoder);
static void gst_base_audio_decoder_free_frame (GstAudioFrame * frame);

GST_BOILERPLATE (GstBaseAudioDecoder, gst_base_audio_decoder,
    GstBaseAudioCodec, GST_TYPE_BASE_AUDIO_CODEC);

static void
gst_base_audio_decoder_base_init (gpointer g_class)
{

}

static void
gst_base_audio_decoder_class_init (GstBaseAudioDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_base_audio_decoder_finalize;

  gstelement_class->change_state = gst_base_audio_decoder_change_state;

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_base_audio_decoder_init (GstBaseAudioDecoder * base_audio_decoder,
    GstBaseAudioDecoderClass * klass)
{
  GstPad *pad;

  GST_DEBUG ("gst_base_audio_decoder_init");

  pad = GST_BASE_AUDIO_CODEC_SINK_PAD (base_audio_decoder);

  gst_pad_set_chain_function (pad, gst_base_audio_decoder_chain);
  gst_pad_set_event_function (pad, gst_base_audio_decoder_sink_event);
  gst_pad_set_setcaps_function (pad, gst_base_audio_decoder_sink_setcaps);
  gst_pad_set_query_function (pad, gst_base_audio_decoder_sink_query);

  pad = GST_BASE_AUDIO_CODEC_SRC_PAD (base_audio_decoder);

  gst_pad_set_event_function (pad, gst_base_audio_decoder_src_event);
  gst_pad_set_query_type_function (pad, gst_base_audio_decoder_get_query_types);
  gst_pad_set_query_function (pad, gst_base_audio_decoder_src_query);

  base_audio_decoder->input_adapter = gst_adapter_new ();
  base_audio_decoder->output_adapter = gst_adapter_new ();

  gst_segment_init (&base_audio_decoder->state.segment, GST_FORMAT_TIME);
  gst_base_audio_decoder_reset (base_audio_decoder);

  base_audio_decoder->current_frame =
      gst_base_audio_decoder_new_frame (base_audio_decoder);

  base_audio_decoder->sink_clipping = TRUE;
}

static gboolean
gst_base_audio_decoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseAudioDecoder *base_audio_decoder;
  GstBaseAudioDecoderClass *base_audio_decoder_class;
  GstStructure *structure;
  const GValue *codec_data;

  base_audio_decoder = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  base_audio_decoder_class =
      GST_BASE_AUDIO_DECODER_GET_CLASS (base_audio_decoder);

  GST_DEBUG ("setcaps %" GST_PTR_FORMAT, caps);

  if (base_audio_decoder->codec_data) {
    gst_buffer_unref (base_audio_decoder->codec_data);
    base_audio_decoder->codec_data = NULL;
  }

  structure = gst_caps_get_structure (caps, 0);

  codec_data = gst_structure_get_value (structure, "codec_data");
  if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER) {
    base_audio_decoder->codec_data = gst_value_get_buffer (codec_data);
  }

  if (base_audio_decoder_class->start) {
    base_audio_decoder_class->start (base_audio_decoder);
  }

  g_object_unref (base_audio_decoder);

  return TRUE;
}

static void
gst_base_audio_decoder_finalize (GObject * object)
{
  GstBaseAudioDecoder *base_audio_decoder;
  GstBaseAudioDecoderClass *base_audio_decoder_class;

  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (object));
  base_audio_decoder = GST_BASE_AUDIO_DECODER (object);
  base_audio_decoder_class = GST_BASE_AUDIO_DECODER_GET_CLASS (object);

  gst_base_audio_decoder_reset (base_audio_decoder);

  GST_DEBUG_OBJECT (object, "finalize");

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_base_audio_decoder_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseAudioDecoder *base_audio_decoder;
  GstBaseAudioDecoderClass *base_audio_decoder_class;
  gboolean ret = FALSE;

  base_audio_decoder = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  base_audio_decoder_class =
      GST_BASE_AUDIO_DECODER_GET_CLASS (base_audio_decoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstAudioFrame *frame;

      frame = g_malloc0 (sizeof (GstAudioFrame));
      frame->presentation_frame_number =
          base_audio_decoder->presentation_frame_number;
      frame->presentation_duration = 0;
      base_audio_decoder->presentation_frame_number++;

      base_audio_decoder->frames =
          g_list_append (base_audio_decoder->frames, frame);
      if (base_audio_decoder_class->finish) {
        base_audio_decoder_class->finish (base_audio_decoder, frame);
      }

      ret =
          gst_pad_push_event (GST_BASE_AUDIO_CODEC_SRC_PAD (base_audio_decoder),
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

      gst_event_parse_new_segment_full (event, &update, &rate,
          &applied_rate, &format, &start, &stop, &position);

      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      GST_DEBUG ("new segment %lld %lld", start, position);

      gst_segment_set_newsegment_full (&base_audio_decoder->state.segment,
          update, rate, applied_rate, format, start, stop, position);

      ret =
          gst_pad_push_event (GST_BASE_AUDIO_CODEC_SRC_PAD (base_audio_decoder),
          event);
    }
      break;
    default:
      /* FIXME this changes the order of events */
      ret =
          gst_pad_push_event (GST_BASE_AUDIO_CODEC_SRC_PAD (base_audio_decoder),
          event);
      break;
  }

done:
  gst_object_unref (base_audio_decoder);
  return ret;

newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (base_audio_decoder, "received non TIME newsegment");
    gst_event_unref (event);
    goto done;
  }
}

static gboolean
gst_base_audio_decoder_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseAudioDecoder *base_audio_decoder;
  gboolean res = FALSE;

  base_audio_decoder = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));

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
      res =
          gst_base_audio_decoder_src_convert (pad, format, cur, &tformat,
          &tcur);
      if (!res)
        goto convert_error;
      res =
          gst_base_audio_decoder_src_convert (pad, format, stop, &tformat,
          &tstop);
      if (!res)
        goto convert_error;

      real_seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);

      res =
          gst_pad_push_event (GST_BASE_AUDIO_CODEC_SINK_PAD
          (base_audio_decoder), real_seek);

      break;
    }
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      GST_OBJECT_LOCK (base_audio_decoder);
      base_audio_decoder->proportion = proportion;
      base_audio_decoder->earliest_time = timestamp + diff;
      GST_OBJECT_UNLOCK (base_audio_decoder);

      GST_DEBUG_OBJECT (base_audio_decoder,
          "got QoS %" GST_TIME_FORMAT ", %" G_GINT64_FORMAT ", %g",
          GST_TIME_ARGS (timestamp), diff, proportion);

      res =
          gst_pad_push_event (GST_BASE_AUDIO_CODEC_SINK_PAD
          (base_audio_decoder), event);
      break;
    }
    default:
      res =
          gst_pad_push_event (GST_BASE_AUDIO_CODEC_SINK_PAD
          (base_audio_decoder), event);
      break;
  }
done:
  gst_object_unref (base_audio_decoder);
  return res;

convert_error:
  GST_DEBUG_OBJECT (base_audio_decoder, "could not convert format");
  goto done;
}


#if 0
static gboolean
gst_base_audio_decoder_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstBaseAudioDecoder *enc;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  enc = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));

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
gst_base_audio_decoder_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstBaseAudioDecoder *enc;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  enc = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));

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
gst_base_audio_decoder_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_CONVERT,
    0
  };

  return query_types;
}

static gboolean
gst_base_audio_decoder_src_query (GstPad * pad, GstQuery * query)
{
  GstBaseAudioDecoder *enc;
  gboolean res;

  enc = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));

  switch GST_QUERY_TYPE
    (query) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_base_audio_decoder_src_convert (pad, src_fmt, src_val, &dest_fmt,
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
  GST_DEBUG_OBJECT (enc, "query failed");
  gst_object_unref (enc);
  return res;
}

static gboolean
gst_base_audio_decoder_sink_query (GstPad * pad, GstQuery * query)
{
  GstBaseAudioDecoder *base_audio_decoder;
  gboolean res = FALSE;

  base_audio_decoder = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (base_audio_decoder, "sink query fps=%d/%d",
      base_audio_decoder->state.fps_n, base_audio_decoder->state.fps_d);
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = gst_base_audio_rawaudio_convert (&base_audio_decoder->state,
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
  gst_object_unref (base_audio_decoder);

  return res;
error:
  GST_DEBUG_OBJECT (base_audio_decoder, "query failed");
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

static void
gst_base_audio_decoder_reset (GstBaseAudioDecoder * base_audio_decoder)
{
  GstBaseAudioDecoderClass *base_audio_decoder_class;
  GList *g;

  base_audio_decoder_class =
      GST_BASE_AUDIO_DECODER_GET_CLASS (base_audio_decoder);

  GST_DEBUG ("reset");

  base_audio_decoder->started = FALSE;

  base_audio_decoder->discont = TRUE;
  base_audio_decoder->have_sync = FALSE;

  base_audio_decoder->timestamp_offset = GST_CLOCK_TIME_NONE;
  base_audio_decoder->system_frame_number = 0;
  base_audio_decoder->presentation_frame_number = 0;
  base_audio_decoder->last_sink_timestamp = GST_CLOCK_TIME_NONE;
  base_audio_decoder->last_sink_offset_end = GST_CLOCK_TIME_NONE;
  base_audio_decoder->base_picture_number = 0;
  base_audio_decoder->last_timestamp = GST_CLOCK_TIME_NONE;

  base_audio_decoder->offset = 0;

  if (base_audio_decoder->caps) {
    gst_caps_unref (base_audio_decoder->caps);
    base_audio_decoder->caps = NULL;
  }

  if (base_audio_decoder->current_frame) {
    gst_base_audio_decoder_free_frame (base_audio_decoder->current_frame);
    base_audio_decoder->current_frame = NULL;
  }

  base_audio_decoder->have_src_caps = FALSE;

  for (g = g_list_first (base_audio_decoder->frames); g; g = g_list_next (g)) {
    GstAudioFrame *frame = g->data;
    gst_base_audio_decoder_free_frame (frame);
  }
  g_list_free (base_audio_decoder->frames);
  base_audio_decoder->frames = NULL;

  if (base_audio_decoder_class->reset) {
    base_audio_decoder_class->reset (base_audio_decoder);
  }
}

static GstBuffer *
gst_adapter_get_buffer (GstAdapter * adapter)
{
  return gst_buffer_ref (GST_BUFFER (adapter->buflist->data));

}

static GstFlowReturn
gst_base_audio_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseAudioDecoder *base_audio_decoder;
  GstBaseAudioDecoderClass *klass;
  GstBuffer *buffer;
  GstFlowReturn ret;

  GST_DEBUG ("chain %lld", GST_BUFFER_TIMESTAMP (buf));

#if 0
  /* requiring the pad to be negotiated makes it impossible to use
   * oggdemux or filesrc ! decoder */
  if (!gst_pad_is_negotiated (pad)) {
    GST_DEBUG ("not negotiated");
    return GST_FLOW_NOT_NEGOTIATED;
  }
#endif

  base_audio_decoder = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  klass = GST_BASE_AUDIO_DECODER_GET_CLASS (base_audio_decoder);

  GST_DEBUG_OBJECT (base_audio_decoder, "chain");

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (base_audio_decoder, "received DISCONT buffer");
    if (base_audio_decoder->started) {
      gst_base_audio_decoder_reset (base_audio_decoder);
    }
  }

  if (!base_audio_decoder->started) {
    klass->start (base_audio_decoder);
    base_audio_decoder->started = TRUE;
  }

  if (GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE) {
    GST_DEBUG ("timestamp %lld offset %lld", GST_BUFFER_TIMESTAMP (buf),
        base_audio_decoder->offset);
    base_audio_decoder->last_sink_timestamp = GST_BUFFER_TIMESTAMP (buf);
  }
  if (GST_BUFFER_OFFSET_END (buf) != -1) {
    GST_DEBUG ("gp %lld", GST_BUFFER_OFFSET_END (buf));
    base_audio_decoder->last_sink_offset_end = GST_BUFFER_OFFSET_END (buf);
  }
  base_audio_decoder->offset += GST_BUFFER_SIZE (buf);

#if 0
  if (base_audio_decoder->timestamp_offset == GST_CLOCK_TIME_NONE &&
      GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE) {
    GST_DEBUG ("got new offset %lld", GST_BUFFER_TIMESTAMP (buf));
    base_audio_decoder->timestamp_offset = GST_BUFFER_TIMESTAMP (buf);
  }
#endif

  if (base_audio_decoder->current_frame == NULL) {
    base_audio_decoder->current_frame =
        gst_base_audio_decoder_new_frame (base_audio_decoder);
  }

  gst_adapter_push (base_audio_decoder->input_adapter, buf);

  if (!base_audio_decoder->have_sync) {
    int n, m;

    GST_DEBUG ("no sync, scanning");

    n = gst_adapter_available (base_audio_decoder->input_adapter);
    m = klass->scan_for_sync (base_audio_decoder, FALSE, 0, n);

    if (m >= n) {
      g_warning ("subclass scanned past end %d >= %d", m, n);
    }

    gst_adapter_flush (base_audio_decoder->input_adapter, m);

    if (m < n) {
      GST_DEBUG ("found possible sync after %d bytes (of %d)", m, n);

      /* this is only "maybe" sync */
      base_audio_decoder->have_sync = TRUE;
    }

    if (!base_audio_decoder->have_sync) {
      gst_object_unref (base_audio_decoder);
      return GST_FLOW_OK;
    }
  }

  /* FIXME: use gst_adapter_prev_timestamp() here instead? */
  buffer = gst_adapter_get_buffer (base_audio_decoder->input_adapter);

  base_audio_decoder->buffer_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  gst_buffer_unref (buffer);

  do {
    ret = klass->parse_data (base_audio_decoder, FALSE);
  } while (ret == GST_FLOW_OK);

  if (ret == GST_BASE_AUDIO_DECODER_FLOW_NEED_DATA) {
    gst_object_unref (base_audio_decoder);
    return GST_FLOW_OK;
  }

  gst_object_unref (base_audio_decoder);
  return ret;
}

static GstStateChangeReturn
gst_base_audio_decoder_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseAudioDecoder *base_audio_decoder;
  GstBaseAudioDecoderClass *base_audio_decoder_class;
  GstStateChangeReturn ret;

  base_audio_decoder = GST_BASE_AUDIO_DECODER (element);
  base_audio_decoder_class = GST_BASE_AUDIO_DECODER_GET_CLASS (element);

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (base_audio_decoder_class->stop) {
        base_audio_decoder_class->stop (base_audio_decoder);
      }
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_base_audio_decoder_free_frame (GstAudioFrame * frame)
{
  g_return_if_fail (frame != NULL);

  if (frame->sink_buffer) {
    gst_buffer_unref (frame->sink_buffer);
  }
#if 0
  if (frame->src_buffer) {
    gst_buffer_unref (frame->src_buffer);
  }
#endif

  g_free (frame);
}

static GstAudioFrame *
gst_base_audio_decoder_new_frame (GstBaseAudioDecoder * base_audio_decoder)
{
  GstAudioFrame *frame;

  frame = g_malloc0 (sizeof (GstAudioFrame));

  frame->system_frame_number = base_audio_decoder->system_frame_number;
  base_audio_decoder->system_frame_number++;

  frame->decode_frame_number = frame->system_frame_number -
      base_audio_decoder->reorder_depth;

  frame->decode_timestamp = -1;
  frame->presentation_timestamp = -1;
  frame->presentation_duration = -1;
  frame->n_fields = 2;

  return frame;
}

GstFlowReturn
gst_base_audio_decoder_finish_frame (GstBaseAudioDecoder * base_audio_decoder,
    GstAudioFrame * frame)
{
  GstBaseAudioDecoderClass *base_audio_decoder_class;
  GstBuffer *src_buffer;

  GST_DEBUG ("finish frame");

  base_audio_decoder_class =
      GST_BASE_AUDIO_DECODER_GET_CLASS (base_audio_decoder);

  GST_DEBUG ("finish frame sync=%d pts=%lld", frame->is_sync_point,
      frame->presentation_timestamp);

  if (frame->is_sync_point) {
    if (GST_CLOCK_TIME_IS_VALID (frame->presentation_timestamp)) {
      if (frame->presentation_timestamp != base_audio_decoder->timestamp_offset) {
        GST_DEBUG ("sync timestamp %lld diff %lld",
            frame->presentation_timestamp,
            frame->presentation_timestamp -
            base_audio_decoder->state.segment.start);
        base_audio_decoder->timestamp_offset = frame->presentation_timestamp;
        base_audio_decoder->field_index = 0;
      } else {
        /* This case is for one initial timestamp and no others, e.g.,
         * filesrc ! decoder ! xvimagesink */
        GST_WARNING ("sync timestamp didn't change, ignoring");
        frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
      }
    } else {
      GST_WARNING ("sync point doesn't have timestamp");
      if (GST_CLOCK_TIME_IS_VALID (base_audio_decoder->timestamp_offset)) {
        GST_ERROR ("No base timestamp.  Assuming frames start at 0");
        base_audio_decoder->timestamp_offset = 0;
        base_audio_decoder->field_index = 0;
      }
    }
  }
  frame->field_index = base_audio_decoder->field_index;
  base_audio_decoder->field_index += frame->n_fields;

  if (frame->presentation_timestamp == GST_CLOCK_TIME_NONE) {
    frame->presentation_timestamp =
        gst_base_audio_decoder_get_field_timestamp (base_audio_decoder,
        frame->field_index);
    frame->presentation_duration = GST_CLOCK_TIME_NONE;
    frame->decode_timestamp =
        gst_base_audio_decoder_get_timestamp (base_audio_decoder,
        frame->decode_frame_number);
  }
  if (frame->presentation_duration == GST_CLOCK_TIME_NONE) {
    frame->presentation_duration =
        gst_base_audio_decoder_get_field_timestamp (base_audio_decoder,
        frame->field_index + frame->n_fields) - frame->presentation_timestamp;
  }

  if (GST_CLOCK_TIME_IS_VALID (base_audio_decoder->last_timestamp)) {
    if (frame->presentation_timestamp < base_audio_decoder->last_timestamp) {
      GST_WARNING ("decreasing timestamp (%lld < %lld)",
          frame->presentation_timestamp, base_audio_decoder->last_timestamp);
    }
  }
  base_audio_decoder->last_timestamp = frame->presentation_timestamp;

  GST_BUFFER_FLAG_UNSET (frame->src_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  if (base_audio_decoder->state.interlaced) {
#ifndef GST_AUDIO_BUFFER_TFF
#define GST_AUDIO_BUFFER_TFF (GST_MINI_OBJECT_FLAG_LAST << 5)
#endif
#ifndef GST_AUDIO_BUFFER_RFF
#define GST_AUDIO_BUFFER_RFF (GST_MINI_OBJECT_FLAG_LAST << 6)
#endif
#ifndef GST_AUDIO_BUFFER_ONEFIELD
#define GST_AUDIO_BUFFER_ONEFIELD (GST_MINI_OBJECT_FLAG_LAST << 7)
#endif
    int tff = base_audio_decoder->state.top_field_first;

    if (frame->field_index & 1) {
      tff ^= 1;
    }
    if (tff) {
      GST_BUFFER_FLAG_SET (frame->src_buffer, GST_AUDIO_BUFFER_TFF);
    } else {
      GST_BUFFER_FLAG_UNSET (frame->src_buffer, GST_AUDIO_BUFFER_TFF);
    }
    GST_BUFFER_FLAG_UNSET (frame->src_buffer, GST_AUDIO_BUFFER_RFF);
    GST_BUFFER_FLAG_UNSET (frame->src_buffer, GST_AUDIO_BUFFER_ONEFIELD);
    if (frame->n_fields == 3) {
      GST_BUFFER_FLAG_SET (frame->src_buffer, GST_AUDIO_BUFFER_RFF);
    } else if (frame->n_fields == 1) {
      GST_BUFFER_FLAG_UNSET (frame->src_buffer, GST_AUDIO_BUFFER_ONEFIELD);
    }
  }

  GST_BUFFER_TIMESTAMP (frame->src_buffer) = frame->presentation_timestamp;
  GST_BUFFER_DURATION (frame->src_buffer) = frame->presentation_duration;
  GST_BUFFER_OFFSET (frame->src_buffer) = -1;
  GST_BUFFER_OFFSET_END (frame->src_buffer) = -1;

  GST_DEBUG ("pushing frame %lld", frame->presentation_timestamp);

  base_audio_decoder->frames =
      g_list_remove (base_audio_decoder->frames, frame);

  gst_base_audio_decoder_set_src_caps (base_audio_decoder);

  src_buffer = frame->src_buffer;
  frame->src_buffer = NULL;

  gst_base_audio_decoder_free_frame (frame);

  if (base_audio_decoder->sink_clipping) {
    gint64 start = GST_BUFFER_TIMESTAMP (src_buffer);
    gint64 stop = GST_BUFFER_TIMESTAMP (src_buffer) +
        GST_BUFFER_DURATION (src_buffer);

    if (gst_segment_clip (&base_audio_decoder->state.segment, GST_FORMAT_TIME,
            start, stop, &start, &stop)) {
      GST_BUFFER_TIMESTAMP (src_buffer) = start;
      GST_BUFFER_DURATION (src_buffer) = stop - start;
    } else {
      GST_DEBUG ("dropping buffer outside segment");
      gst_buffer_unref (src_buffer);
      return GST_FLOW_OK;
    }
  }

  return gst_pad_push (GST_BASE_AUDIO_CODEC_SRC_PAD (base_audio_decoder),
      src_buffer);
}

int
gst_base_audio_decoder_get_height (GstBaseAudioDecoder * base_audio_decoder)
{
  return base_audio_decoder->state.height;
}

int
gst_base_audio_decoder_get_width (GstBaseAudioDecoder * base_audio_decoder)
{
  return base_audio_decoder->state.width;
}

GstFlowReturn
gst_base_audio_decoder_end_of_stream (GstBaseAudioDecoder * base_audio_decoder,
    GstBuffer * buffer)
{

  if (base_audio_decoder->frames) {
    GST_DEBUG ("EOS with frames left over");
  }

  return gst_pad_push (GST_BASE_AUDIO_CODEC_SRC_PAD (base_audio_decoder),
      buffer);
}

void
gst_base_audio_decoder_add_to_frame (GstBaseAudioDecoder * base_audio_decoder,
    int n_bytes)
{
  GstBuffer *buf;

  GST_DEBUG ("add to frame");

#if 0
  if (gst_adapter_available (base_audio_decoder->output_adapter) == 0) {
    GstBuffer *buffer;

    buffer =
        gst_adapter_get_orig_buffer_at_offset
        (base_audio_decoder->input_adapter, 0);
    if (buffer) {
      base_audio_decoder->current_frame->presentation_timestamp =
          GST_BUFFER_TIMESTAMP (buffer);
      gst_buffer_unref (buffer);
    }
  }
#endif

  if (n_bytes == 0)
    return;

  buf = gst_adapter_take_buffer (base_audio_decoder->input_adapter, n_bytes);

  gst_adapter_push (base_audio_decoder->output_adapter, buf);
}

static guint64
gst_base_audio_decoder_get_timestamp (GstBaseAudioDecoder * base_audio_decoder,
    int picture_number)
{
  if (base_audio_decoder->state.fps_d == 0) {
    return -1;
  }
  if (picture_number < base_audio_decoder->base_picture_number) {
    return base_audio_decoder->timestamp_offset -
        (gint64) gst_util_uint64_scale (base_audio_decoder->base_picture_number
        - picture_number, base_audio_decoder->state.fps_d * GST_SECOND,
        base_audio_decoder->state.fps_n);
  } else {
    return base_audio_decoder->timestamp_offset +
        gst_util_uint64_scale (picture_number -
        base_audio_decoder->base_picture_number,
        base_audio_decoder->state.fps_d * GST_SECOND,
        base_audio_decoder->state.fps_n);
  }
}

static guint64
gst_base_audio_decoder_get_field_timestamp (GstBaseAudioDecoder *
    base_audio_decoder, int field_offset)
{
  if (base_audio_decoder->state.fps_d == 0) {
    return GST_CLOCK_TIME_NONE;
  }
  if (field_offset < 0) {
    GST_WARNING ("field offset < 0");
    return GST_CLOCK_TIME_NONE;
  }
  return base_audio_decoder->timestamp_offset +
      gst_util_uint64_scale (field_offset,
      base_audio_decoder->state.fps_d * GST_SECOND,
      base_audio_decoder->state.fps_n * 2);
}


GstFlowReturn
gst_base_audio_decoder_have_frame (GstBaseAudioDecoder * base_audio_decoder)
{
  GstAudioFrame *frame = base_audio_decoder->current_frame;
  GstBuffer *buffer;
  GstBaseAudioDecoderClass *base_audio_decoder_class;
  GstFlowReturn ret = GST_FLOW_OK;
  int n_available;

  GST_DEBUG ("have_frame");

  base_audio_decoder_class =
      GST_BASE_AUDIO_DECODER_GET_CLASS (base_audio_decoder);

  n_available = gst_adapter_available (base_audio_decoder->output_adapter);
  if (n_available) {
    buffer = gst_adapter_take_buffer (base_audio_decoder->output_adapter,
        n_available);
  } else {
    buffer = gst_buffer_new_and_alloc (0);
  }

  frame->distance_from_sync = base_audio_decoder->distance_from_sync;
  base_audio_decoder->distance_from_sync++;

#if 0
  if (frame->presentation_timestamp == GST_CLOCK_TIME_NONE) {
    frame->presentation_timestamp =
        gst_base_audio_decoder_get_timestamp (base_audio_decoder,
        frame->presentation_frame_number);
    frame->presentation_duration =
        gst_base_audio_decoder_get_timestamp (base_audio_decoder,
        frame->presentation_frame_number + 1) - frame->presentation_timestamp;
    frame->decode_timestamp =
        gst_base_audio_decoder_get_timestamp (base_audio_decoder,
        frame->decode_frame_number);
  }
#endif

#if 0
  GST_BUFFER_TIMESTAMP (buffer) = frame->presentation_timestamp;
  GST_BUFFER_DURATION (buffer) = frame->presentation_duration;
  if (frame->decode_frame_number < 0) {
    GST_BUFFER_OFFSET (buffer) = 0;
  } else {
    GST_BUFFER_OFFSET (buffer) = frame->decode_timestamp;
  }
  GST_BUFFER_OFFSET_END (buffer) = GST_CLOCK_TIME_NONE;
#endif

  GST_DEBUG ("pts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->presentation_timestamp));
  GST_DEBUG ("dts %" GST_TIME_FORMAT, GST_TIME_ARGS (frame->decode_timestamp));
  GST_DEBUG ("dist %d", frame->distance_from_sync);

  if (frame->is_sync_point) {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  }
  if (base_audio_decoder->discont) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    base_audio_decoder->discont = FALSE;
  }

  frame->sink_buffer = buffer;

  base_audio_decoder->frames = g_list_append (base_audio_decoder->frames,
      frame);

  /* do something with frame */
  ret = base_audio_decoder_class->handle_frame (base_audio_decoder, frame);
  if (!GST_FLOW_IS_SUCCESS (ret)) {
    GST_DEBUG ("flow error!");
  }

  /* create new frame */
  base_audio_decoder->current_frame =
      gst_base_audio_decoder_new_frame (base_audio_decoder);

  return ret;
}

GstAudioState *
gst_base_audio_decoder_get_state (GstBaseAudioDecoder * base_audio_decoder)
{
  return &base_audio_decoder->state;

}

void
gst_base_audio_decoder_set_state (GstBaseAudioDecoder * base_audio_decoder,
    GstAudioState * state)
{
  memcpy (&base_audio_decoder->state, state, sizeof (*state));

}

void
gst_base_audio_decoder_lost_sync (GstBaseAudioDecoder * base_audio_decoder)
{
  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (base_audio_decoder));

  GST_DEBUG ("lost_sync");

  if (gst_adapter_available (base_audio_decoder->input_adapter) >= 1) {
    gst_adapter_flush (base_audio_decoder->input_adapter, 1);
  }

  base_audio_decoder->have_sync = FALSE;
}

void
gst_base_audio_decoder_set_sync_point (GstBaseAudioDecoder * base_audio_decoder)
{
  GST_DEBUG ("set_sync_point");

  base_audio_decoder->current_frame->is_sync_point = TRUE;
  base_audio_decoder->distance_from_sync = 0;

  base_audio_decoder->current_frame->presentation_timestamp =
      base_audio_decoder->last_sink_timestamp;


}

GstAudioFrame *
gst_base_audio_decoder_get_frame (GstBaseAudioDecoder * base_audio_decoder,
    int frame_number)
{
  GList *g;

  for (g = g_list_first (base_audio_decoder->frames); g; g = g_list_next (g)) {
    GstAudioFrame *frame = g->data;

    if (frame->system_frame_number == frame_number) {
      return frame;
    }
  }

  return NULL;
}

void
gst_base_audio_decoder_set_src_caps (GstBaseAudioDecoder * base_audio_decoder)
{
  GstCaps *caps;
  GstAudioState *state = &base_audio_decoder->state;

  if (base_audio_decoder->have_src_caps)
    return;

  caps = gst_audio_format_new_caps (state->format,
      state->width, state->height,
      state->fps_n, state->fps_d, state->par_n, state->par_d);
  gst_caps_set_simple (caps, "interlaced",
      G_TYPE_BOOLEAN, state->interlaced, NULL);

  GST_DEBUG ("setting caps %" GST_PTR_FORMAT, caps);

  gst_pad_set_caps (GST_BASE_AUDIO_CODEC_SRC_PAD (base_audio_decoder), caps);

  base_audio_decoder->have_src_caps = TRUE;
}
