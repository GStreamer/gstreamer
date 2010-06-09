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

enum
{
  PROP_0,
  PROP_PACKETIZED,
  PROP_SINK_CLIPPING
};


static GstFlowReturn gst_base_video_decoder_drain (GstBaseVideoDecoder * dec,
    gboolean at_eos, gboolean codec_data);


GST_BOILERPLATE (GstBaseVideoDecoder, gst_base_video_decoder,
    GstElement, GST_TYPE_ELEMENT);



static guint64
gst_base_video_decoder_get_timestamp (GstBaseVideoDecoder * base_video_decoder,
    gint picture_number)
{
  if (base_video_decoder->state.fps_d == 0) {
    return -1;
  }
  if (picture_number < base_video_decoder->base_picture_number) {
    return base_video_decoder->timestamp_offset -
        (gint64) gst_util_uint64_scale (base_video_decoder->base_picture_number
        - picture_number, base_video_decoder->state.fps_d * GST_SECOND,
        base_video_decoder->state.fps_n);
  } else {
    return base_video_decoder->timestamp_offset +
        gst_util_uint64_scale (picture_number -
        base_video_decoder->base_picture_number,
        base_video_decoder->state.fps_d * GST_SECOND,
        base_video_decoder->state.fps_n);
  }
}

static guint64
gst_base_video_decoder_get_field_timestamp (GstBaseVideoDecoder *
    base_video_decoder, gint field_offset)
{
  if (base_video_decoder->state.fps_d == 0) {
    return GST_CLOCK_TIME_NONE;
  }
  if (field_offset < 0) {
    GST_WARNING ("field offset < 0");
    return GST_CLOCK_TIME_NONE;
  }
  return base_video_decoder->timestamp_offset +
      gst_util_uint64_scale (field_offset,
      base_video_decoder->state.fps_d * GST_SECOND,
      base_video_decoder->state.fps_n * 2);
}

static guint64
gst_base_video_decoder_get_field_duration (GstBaseVideoDecoder *
    base_video_decoder, gint n_fields)
{
  if (base_video_decoder->state.fps_d == 0) {
    return GST_CLOCK_TIME_NONE;
  }
  if (n_fields < 0) {
    GST_WARNING ("n_fields < 0");
    return GST_CLOCK_TIME_NONE;
  }
  return gst_util_uint64_scale (n_fields,
      base_video_decoder->state.fps_d * GST_SECOND,
      base_video_decoder->state.fps_n * 2);
}

static GstVideoFrame *
gst_base_video_decoder_new_frame (GstBaseVideoDecoder * base_video_decoder)
{
  GstBaseVideoDecoderClass *base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  GstVideoFrame *frame;

  if (base_video_decoder_class->create_frame)
    frame = base_video_decoder_class->create_frame (base_video_decoder);
  else
    frame = gst_video_frame_new ();

  frame->system_frame_number = base_video_decoder->system_frame_number;
  base_video_decoder->system_frame_number++;

  frame->decode_frame_number = frame->system_frame_number -
      base_video_decoder->reorder_depth;

  return frame;
}

static void
gst_base_video_decoder_reset (GstBaseVideoDecoder * base_video_decoder)
{
  GList *g;

  GST_DEBUG ("reset");

  base_video_decoder->discont = TRUE;
  base_video_decoder->have_sync = FALSE;

  base_video_decoder->timestamp_offset = GST_CLOCK_TIME_NONE;
  base_video_decoder->system_frame_number = 0;
  base_video_decoder->presentation_frame_number = 0;
  base_video_decoder->base_picture_number = 0;
  base_video_decoder->last_timestamp = GST_CLOCK_TIME_NONE;

  base_video_decoder->input_offset = 0;
  base_video_decoder->frame_offset = 0;

  /* This function could be called from finalize() */
  if (base_video_decoder->input_adapter) {
    gst_adapter_clear (base_video_decoder->input_adapter);
  }

  if (base_video_decoder->current_frame) {
    gst_video_frame_unref (base_video_decoder->current_frame);
    base_video_decoder->current_frame = NULL;
  }

  base_video_decoder->have_src_caps = FALSE;

  for (g = g_list_first (base_video_decoder->frames); g; g = g_list_next (g)) {
    GstVideoFrame *frame = g->data;
    gst_video_frame_unref (frame);
  }
  g_list_free (base_video_decoder->frames);
  base_video_decoder->frames = NULL;

  GST_OBJECT_LOCK (base_video_decoder);
  base_video_decoder->earliest_time = GST_CLOCK_TIME_NONE;
  base_video_decoder->proportion = 0.5;
  GST_OBJECT_UNLOCK (base_video_decoder);
}

static void
gst_base_video_decoder_flush (GstBaseVideoDecoder * base_video_decoder)
{
  GstBaseVideoDecoderClass *base_video_decoder_class;

  gst_base_video_decoder_reset (base_video_decoder);

  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  if (base_video_decoder_class->flush)
    base_video_decoder_class->flush (base_video_decoder);
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

  state = &base_video_decoder->state;

  if (state->codec_data) {
    gst_buffer_unref (state->codec_data);
  }
  memset (state, 0, sizeof (GstVideoState));

  structure = gst_caps_get_structure (caps, 0);

  gst_video_format_parse_caps (caps, NULL, &state->width, &state->height);
  gst_video_parse_caps_framerate (caps, &state->fps_n, &state->fps_d);
  gst_video_parse_caps_pixel_aspect_ratio (caps, &state->par_n, &state->par_d);

  gst_structure_get_boolean (structure, "interlaced", &state->interlaced);

  codec_data = gst_structure_get_value (structure, "codec_data");
  if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER)
    state->codec_data = gst_value_get_buffer (codec_data);

  if (base_video_decoder_class->set_sink_caps)
    ret = base_video_decoder_class->set_sink_caps (base_video_decoder, caps);

  g_object_unref (base_video_decoder);

  return ret;
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
      if (!base_video_decoder->packetized)
        gst_base_video_decoder_drain (base_video_decoder, TRUE, FALSE);

      ret =
          gst_pad_push_event (GST_BASE_VIDEO_DECODER_SRC_PAD
          (base_video_decoder), event);
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
      GstSegment *segment = &base_video_decoder->segment;

      gst_event_parse_new_segment_full (event, &update, &rate,
          &applied_rate, &format, &start, &stop, &position);

      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (!update) {
        gst_base_video_decoder_flush (base_video_decoder);
      }

      base_video_decoder->timestamp_offset = start;

      gst_segment_set_newsegment_full (segment,
          update, rate, applied_rate, format, start, stop, position);
      base_video_decoder->have_segment = TRUE;

      GST_WARNING ("new segment: format %d rate %g start %" GST_TIME_FORMAT
          " stop %" GST_TIME_FORMAT
          " position %" GST_TIME_FORMAT
          " update %d",
          format, rate,
          GST_TIME_ARGS (segment->start),
          GST_TIME_ARGS (segment->stop), GST_TIME_ARGS (segment->time), update);

      ret =
          gst_pad_push_event (GST_BASE_VIDEO_DECODER_SRC_PAD
          (base_video_decoder), event);
    }
      break;
    case GST_EVENT_FLUSH_STOP:{
      GST_OBJECT_LOCK (base_video_decoder);
      base_video_decoder->earliest_time = GST_CLOCK_TIME_NONE;
      base_video_decoder->proportion = 0.5;
      GST_OBJECT_UNLOCK (base_video_decoder);
    }
    default:
      /* FIXME this changes the order of events */
      ret =
          gst_pad_push_event (GST_BASE_VIDEO_DECODER_SRC_PAD
          (base_video_decoder), event);
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
      gint64 tcur, tstop;

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
          gst_pad_push_event (GST_BASE_VIDEO_DECODER_SINK_PAD
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
      base_video_decoder->proportion = proportion;
      if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (timestamp))) {
        if (G_UNLIKELY (diff > 0)) {
          if (base_video_decoder->state.fps_n > 0)
            duration =
                gst_util_uint64_scale (GST_SECOND,
                base_video_decoder->state.fps_d,
                base_video_decoder->state.fps_n);
          else
            duration = 0;
          base_video_decoder->earliest_time = timestamp + 2 * diff + duration;
        } else {
          base_video_decoder->earliest_time = timestamp + diff;
        }
      } else {
        base_video_decoder->earliest_time = GST_CLOCK_TIME_NONE;
      }
      GST_OBJECT_UNLOCK (base_video_decoder);

      GST_DEBUG_OBJECT (base_video_decoder,
          "got QoS %" GST_TIME_FORMAT ", %" G_GINT64_FORMAT ", %g",
          GST_TIME_ARGS (timestamp), diff, proportion);

      res =
          gst_pad_push_event (GST_BASE_VIDEO_DECODER_SINK_PAD
          (base_video_decoder), event);
      break;
    }
    default:
      res =
          gst_pad_push_event (GST_BASE_VIDEO_DECODER_SINK_PAD
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
  GstBaseVideoDecoder *dec;
  gboolean res = TRUE;

  dec = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));

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

      time = dec->last_timestamp;
      time = gst_segment_to_stream_time (&dec->segment, GST_FORMAT_TIME, time);

      gst_query_set_position (query, format, time);

      res = TRUE;

      break;
    }
    case GST_QUERY_DURATION:
    {
      res = gst_pad_peer_query (dec->sinkpad, query);
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
  gst_object_unref (dec);
  return res;

error:
  GST_ERROR_OBJECT (dec, "query failed");
  gst_object_unref (dec);
  return res;
}

static gboolean
gst_base_video_decoder_sink_query (GstPad * pad, GstQuery * query)
{
  GstBaseVideoDecoder *base_video_decoder;
  gboolean res = FALSE;

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (base_video_decoder, "sink query fps=%d/%d",
      base_video_decoder->state.fps_n, base_video_decoder->state.fps_d);
  switch (GST_QUERY_TYPE (query)) {

    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (base_video_decoder);

  return res;
}

static void
gst_base_video_decoder_set_src_caps (GstBaseVideoDecoder * base_video_decoder)
{
  GstCaps *caps;
  GstVideoState *state = &base_video_decoder->state;

  if (base_video_decoder->have_src_caps)
    return;

  caps = gst_pad_get_allowed_caps (base_video_decoder->srcpad);
  if (!caps)
    goto null_caps;
  if (gst_caps_is_empty (caps))
    goto empty_caps;

  gst_caps_set_simple (caps,
      "width", G_TYPE_INT, state->width,
      "height", G_TYPE_INT, state->height,
      "framerate", GST_TYPE_FRACTION, state->fps_n, state->fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, state->par_n, state->par_d,
      "interlaced", G_TYPE_BOOLEAN, state->interlaced, NULL);
  gst_pad_fixate_caps (base_video_decoder->srcpad, caps);


  GST_DEBUG ("setting caps %" GST_PTR_FORMAT, caps);

  gst_pad_set_caps (GST_BASE_VIDEO_DECODER_SRC_PAD (base_video_decoder), caps);

  base_video_decoder->have_src_caps = TRUE;

  gst_caps_unref (caps);
  return;

null_caps:
  GST_WARNING ("Got null caps from get_allowed_caps");
  return;

empty_caps:
  GST_WARNING ("Got empty caps from get_allowed_caps");
  gst_caps_unref (caps);
  return;
}

static GstFlowReturn
gst_base_video_decoder_drain (GstBaseVideoDecoder * dec, gboolean at_eos,
    gboolean codec_data)
{
  GstBaseVideoDecoderClass *klass;
  GstBaseVideoDecoderScanResult res;
  GstFlowReturn ret;
  guint size;

  klass = GST_BASE_VIDEO_DECODER_GET_CLASS (dec);

  if (gst_adapter_available (dec->input_adapter) == 0)
    return GST_FLOW_OK;

lost_sync:
  if (!dec->have_sync) {
    gint n, m;

    GST_DEBUG ("no sync, scanning");

    n = gst_adapter_available (dec->input_adapter);
    m = klass->scan_for_sync (dec, dec->input_adapter);
    if (m == -1) {
      gst_object_unref (dec);
      return GST_FLOW_OK;
    }

    if (m < 0) {
      g_warning ("subclass returned negative scan %d", m);
    }

    if (m >= n) {
      GST_ERROR ("subclass scanned past end %d >= %d", m, n);
    }

    gst_adapter_flush (dec->input_adapter, m);

    if (m < n) {
      GST_DEBUG ("found possible sync after %d bytes (of %d)", m, n);
      /* this is only "maybe" sync */
      dec->have_sync = TRUE;
    }

    if (!dec->have_sync) {
      return GST_FLOW_OK;
    }
  }

  res = klass->scan_for_packet_end (dec, dec->input_adapter, &size, at_eos);
  while (res == GST_BASE_VIDEO_DECODER_SCAN_RESULT_OK) {
    GstClockTime timestamp, duration;
    guint64 offset;
    gboolean preroll, gap;

    GstBuffer *buf;

    GST_DEBUG ("Packet size: %u", size);
    if (size > gst_adapter_available (dec->input_adapter))
      return GST_FLOW_OK;

    timestamp = GST_BUFFER_TIMESTAMP (dec->input_adapter->buflist->data);
    duration = GST_BUFFER_DURATION (dec->input_adapter->buflist->data);
    offset = GST_BUFFER_OFFSET (dec->input_adapter->buflist->data);

    preroll = GST_BUFFER_FLAG_IS_SET (dec->input_adapter->buflist->data,
        GST_BUFFER_FLAG_PREROLL);
    gap = GST_BUFFER_FLAG_IS_SET (dec->input_adapter->buflist->data,
        GST_BUFFER_FLAG_GAP);

    buf = gst_adapter_take_buffer (dec->input_adapter, size);
    GST_BUFFER_TIMESTAMP (buf) = timestamp;
    GST_BUFFER_DURATION (buf) = duration;
    GST_BUFFER_OFFSET (buf) = offset;

    if (preroll)
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_PREROLL);
    else
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_PREROLL);

    if (gap)
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_GAP);
    else
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_GAP);

    if (codec_data)
      ret = klass->parse_codec_data (dec, buf);
    else
      ret = klass->parse_data (dec, buf, at_eos);
    if (ret != GST_FLOW_OK)
      break;

    res = klass->scan_for_packet_end (dec, dec->input_adapter, &size, at_eos);
  }

  if (res == GST_BASE_VIDEO_DECODER_SCAN_RESULT_LOST_SYNC) {
    dec->have_sync = FALSE;
    goto lost_sync;
  } else if (res == GST_BASE_VIDEO_DECODER_SCAN_RESULT_NEED_DATA)
    return GST_FLOW_OK;

  return ret;
}

static GstFlowReturn
gst_base_video_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseVideoDecoder *base_video_decoder;
  GstBaseVideoDecoderClass *base_video_decoder_class;
  GstFlowReturn ret;

  GST_DEBUG ("chain %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

#if 0
  /* requiring the pad to be negotiated makes it impossible to use
   * oggdemux or filesrc ! decoder */
  if (!gst_pad_is_negotiated (pad)) {
    GST_DEBUG ("not negotiated");
    return GST_FLOW_NOT_NEGOTIATED;
  }
#endif

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));
  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  GST_DEBUG_OBJECT (base_video_decoder, "chain");

  if (!base_video_decoder->have_segment) {
    GstEvent *event;
    GstFlowReturn ret;

    GST_WARNING
        ("Received buffer without a new-segment. Assuming timestamps start from 0.");

    gst_segment_set_newsegment_full (&base_video_decoder->segment,
        FALSE, 1.0, 1.0, GST_FORMAT_TIME, 0, GST_CLOCK_TIME_NONE, 0);
    base_video_decoder->have_segment = TRUE;

    event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0,
        GST_CLOCK_TIME_NONE, 0);

    ret =
        gst_pad_push_event (GST_BASE_VIDEO_DECODER_SRC_PAD (base_video_decoder),
        event);
    if (!ret) {
      GST_ERROR ("new segment event ret=%d", ret);
      return GST_FLOW_ERROR;
    }
  }

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (base_video_decoder, "received DISCONT buffer");
    gst_base_video_decoder_flush (base_video_decoder);
  }

  if (base_video_decoder->current_frame == NULL) {
    base_video_decoder->current_frame =
        gst_base_video_decoder_new_frame (base_video_decoder);
  }
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

    ret = gst_base_video_decoder_have_frame (base_video_decoder, NULL);
  } else {

    gst_adapter_push (base_video_decoder->input_adapter, buf);

    ret = gst_base_video_decoder_drain (base_video_decoder, FALSE, FALSE);
  }

  gst_object_unref (base_video_decoder);
  return ret;
}

static gboolean
gst_base_video_decoder_stop (GstBaseVideoDecoder * base_video_decoder)
{
  GstBaseVideoDecoderClass *base_video_decoder_class;

  GST_DEBUG ("stop");

  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  if (base_video_decoder_class->stop)
    return base_video_decoder_class->stop (base_video_decoder);

  return TRUE;
}

static gboolean
gst_base_video_decoder_start (GstBaseVideoDecoder * base_video_decoder)
{
  GstBaseVideoDecoderClass *base_video_decoder_class;

  GST_DEBUG ("start");

  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  gst_base_video_decoder_reset (base_video_decoder);

  if (base_video_decoder_class->start)
    return base_video_decoder_class->start (base_video_decoder);

  return TRUE;
}

static GstStateChangeReturn
gst_base_video_decoder_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseVideoDecoder *base_video_decoder;
  GstStateChangeReturn ret;

  base_video_decoder = GST_BASE_VIDEO_DECODER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_base_video_decoder_start (base_video_decoder);
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_base_video_decoder_stop (base_video_decoder);
      break;

    default:
      break;
  }

  return ret;
}

GstFlowReturn
gst_base_video_decoder_finish_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame)
{
  GstBaseVideoDecoderClass *base_video_decoder_class;
  GstBuffer *src_buffer;

  GST_DEBUG ("finish frame");

  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  GST_DEBUG ("finish frame sync=%d pts=%" GST_TIME_FORMAT,
      GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VIDEO_FRAME_FLAG_SYNC_POINT),
      GST_TIME_ARGS (frame->presentation_timestamp));

  if (GST_CLOCK_TIME_IS_VALID (frame->presentation_timestamp)) {
    if (frame->presentation_timestamp != base_video_decoder->timestamp_offset) {
      GST_DEBUG ("sync timestamp %" GST_TIME_FORMAT " diff %" GST_TIME_FORMAT,
          GST_TIME_ARGS (frame->presentation_timestamp),
          GST_TIME_ARGS (frame->presentation_timestamp -
              base_video_decoder->segment.start));
      base_video_decoder->timestamp_offset = frame->presentation_timestamp;
      base_video_decoder->field_index = 0;
    } else {
      /* This case is for one initial timestamp and no others, e.g.,
       * filesrc ! decoder ! xvimagesink */
      GST_WARNING ("sync timestamp didn't change, ignoring");
      frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
    }
  } else {
    if (GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VIDEO_FRAME_FLAG_SYNC_POINT)) {
      GST_WARNING ("sync point doesn't have timestamp");
      if (!GST_CLOCK_TIME_IS_VALID (base_video_decoder->timestamp_offset)) {
        GST_WARNING
            ("No base timestamp.  Assuming frames start at segment start");
        base_video_decoder->timestamp_offset =
            base_video_decoder->segment.start;
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

  if (GST_CLOCK_TIME_IS_VALID (base_video_decoder->last_timestamp)) {
    if (frame->presentation_timestamp < base_video_decoder->last_timestamp) {
      GST_WARNING ("decreasing timestamp (%" GST_TIME_FORMAT " < %"
          GST_TIME_FORMAT ")", GST_TIME_ARGS (frame->presentation_timestamp),
          GST_TIME_ARGS (base_video_decoder->last_timestamp));
    }
  }
  base_video_decoder->last_timestamp = frame->presentation_timestamp;

  src_buffer = frame->src_buffer;

  GST_BUFFER_FLAG_UNSET (src_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  if (base_video_decoder->state.interlaced) {
#ifndef GST_VIDEO_BUFFER_TFF
#define GST_VIDEO_BUFFER_TFF (GST_MINI_OBJECT_FLAG_LAST << 5)
#endif
#ifndef GST_VIDEO_BUFFER_RFF
#define GST_VIDEO_BUFFER_RFF (GST_MINI_OBJECT_FLAG_LAST << 6)
#endif
#ifndef GST_VIDEO_BUFFER_ONEFIELD
#define GST_VIDEO_BUFFER_ONEFIELD (GST_MINI_OBJECT_FLAG_LAST << 7)
#endif

    if (GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VIDEO_FRAME_FLAG_TFF)) {
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
    GST_BUFFER_FLAG_UNSET (src_buffer, GST_BUFFER_FLAG_DISCONT);
    base_video_decoder->discont = FALSE;
  }

  GST_BUFFER_TIMESTAMP (src_buffer) = frame->presentation_timestamp;
  GST_BUFFER_DURATION (src_buffer) = frame->presentation_duration;
  GST_BUFFER_OFFSET (src_buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (src_buffer) = GST_BUFFER_OFFSET_NONE;

  GST_DEBUG ("pushing frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->presentation_timestamp));

  base_video_decoder->frames =
      g_list_remove (base_video_decoder->frames, frame);

  gst_base_video_decoder_set_src_caps (base_video_decoder);

  if (base_video_decoder->sink_clipping) {
    gint64 start = GST_BUFFER_TIMESTAMP (src_buffer);
    gint64 stop = GST_BUFFER_TIMESTAMP (src_buffer) +
        GST_BUFFER_DURATION (src_buffer);

    if (gst_segment_clip (&base_video_decoder->segment, GST_FORMAT_TIME,
            start, stop, &start, &stop)) {
      GST_BUFFER_TIMESTAMP (src_buffer) = start;
      GST_BUFFER_DURATION (src_buffer) = stop - start;
      GST_DEBUG ("accepting buffer inside segment: %" GST_TIME_FORMAT
          " %" GST_TIME_FORMAT
          " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
          " time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer)),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer) +
              GST_BUFFER_DURATION (src_buffer)),
          GST_TIME_ARGS (base_video_decoder->segment.start),
          GST_TIME_ARGS (base_video_decoder->segment.stop),
          GST_TIME_ARGS (base_video_decoder->segment.time));
    } else {
      GST_DEBUG ("dropping buffer outside segment: %" GST_TIME_FORMAT
          " %" GST_TIME_FORMAT
          " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
          " time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer)),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer) +
              GST_BUFFER_DURATION (src_buffer)),
          GST_TIME_ARGS (base_video_decoder->segment.start),
          GST_TIME_ARGS (base_video_decoder->segment.stop),
          GST_TIME_ARGS (base_video_decoder->segment.time));
      gst_video_frame_unref (frame);
      return GST_FLOW_OK;
    }
  }

  gst_buffer_ref (src_buffer);
  gst_video_frame_unref (frame);

  if (base_video_decoder_class->shape_output)
    return base_video_decoder_class->shape_output (base_video_decoder,
        src_buffer);

  return gst_pad_push (GST_BASE_VIDEO_DECODER_SRC_PAD (base_video_decoder),
      src_buffer);
}

void
gst_base_video_decoder_skip_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame)
{
  GstBaseVideoDecoderClass *base_video_decoder_class;

  GST_DEBUG ("skip frame");

  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  GST_DEBUG ("skip frame sync=%d pts=%" GST_TIME_FORMAT,
      GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VIDEO_FRAME_FLAG_SYNC_POINT),
      GST_TIME_ARGS (frame->presentation_timestamp));

  if (GST_CLOCK_TIME_IS_VALID (frame->presentation_timestamp)) {
    if (frame->presentation_timestamp != base_video_decoder->timestamp_offset) {
      GST_DEBUG ("sync timestamp %" GST_TIME_FORMAT " diff %" GST_TIME_FORMAT,
          GST_TIME_ARGS (frame->presentation_timestamp),
          GST_TIME_ARGS (frame->presentation_timestamp -
              base_video_decoder->segment.start));
      base_video_decoder->timestamp_offset = frame->presentation_timestamp;
      base_video_decoder->field_index = 0;
    } else {
      /* This case is for one initial timestamp and no others, e.g.,
       * filesrc ! decoder ! xvimagesink */
      GST_WARNING ("sync timestamp didn't change, ignoring");
      frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
    }
  } else {
    if (GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VIDEO_FRAME_FLAG_SYNC_POINT)) {
      GST_WARNING ("sync point doesn't have timestamp");
      if (GST_CLOCK_TIME_IS_VALID (base_video_decoder->timestamp_offset)) {
        GST_WARNING
            ("No base timestamp.  Assuming frames start at segment start");
        base_video_decoder->timestamp_offset =
            base_video_decoder->segment.start;
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

  base_video_decoder->frames =
      g_list_remove (base_video_decoder->frames, frame);

  gst_video_frame_unref (frame);
}

GstFlowReturn
gst_base_video_decoder_have_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame ** new_frame)
{
  GstVideoFrame *frame = base_video_decoder->current_frame;
  GstBaseVideoDecoderClass *klass;
  GstClockTime running_time;
  GstClockTimeDiff deadline;
  GstFlowReturn ret;

  klass = GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  if (GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VIDEO_FRAME_FLAG_SYNC_POINT))
    base_video_decoder->distance_from_sync = 0;

  frame->distance_from_sync = base_video_decoder->distance_from_sync;
  base_video_decoder->distance_from_sync++;

  if (frame->sink_buffer) {
    frame->presentation_timestamp = GST_BUFFER_TIMESTAMP (frame->sink_buffer);
    frame->presentation_duration = GST_BUFFER_DURATION (frame->sink_buffer);
  }

  GST_DEBUG ("pts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->presentation_timestamp));
  GST_DEBUG ("dts %" GST_TIME_FORMAT, GST_TIME_ARGS (frame->decode_timestamp));
  GST_DEBUG ("dist %d", frame->distance_from_sync);

  base_video_decoder->frames = g_list_append (base_video_decoder->frames,
      frame);

  running_time = gst_segment_to_running_time (&base_video_decoder->segment,
      GST_FORMAT_TIME, frame->presentation_timestamp);

  if (GST_CLOCK_TIME_IS_VALID (base_video_decoder->earliest_time))
    deadline = GST_CLOCK_DIFF (base_video_decoder->earliest_time, running_time);
  else
    deadline = G_MAXINT64;

  /* do something with frame */
  ret = klass->handle_frame (base_video_decoder, frame, deadline);
  if (!GST_FLOW_IS_SUCCESS (ret)) {
    GST_DEBUG ("flow error!");
  }

  /* create new frame */
  base_video_decoder->current_frame =
      gst_base_video_decoder_new_frame (base_video_decoder);

  if (new_frame)
    *new_frame = base_video_decoder->current_frame;

  return ret;
}

GstVideoState *
gst_base_video_decoder_get_state (GstBaseVideoDecoder * base_video_decoder)
{
  return &base_video_decoder->state;

}

void
gst_base_video_decoder_set_state (GstBaseVideoDecoder * base_video_decoder,
    GstVideoState * state)
{
  memcpy (&base_video_decoder->state, state, sizeof (*state));

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

GstVideoFrame *
gst_base_video_decoder_get_current_frame (GstBaseVideoDecoder *
    base_video_decoder)
{
  return base_video_decoder->current_frame;
}


GstVideoFrame *
gst_base_video_decoder_get_oldest_frame (GstBaseVideoDecoder *
    base_video_decoder)
{
  GList *g;

  g = g_list_first (base_video_decoder->frames);

  if (g == NULL)
    return NULL;
  return (GstVideoFrame *) (g->data);
}

GstVideoFrame *
gst_base_video_decoder_get_frame (GstBaseVideoDecoder * base_video_decoder,
    int frame_number)
{
  GList *g;

  for (g = g_list_first (base_video_decoder->frames); g; g = g_list_next (g)) {
    GstVideoFrame *frame = g->data;

    if (frame->system_frame_number == frame_number) {
      return frame;
    }
  }

  return NULL;
}

void
gst_base_video_decoder_update_src_caps (GstBaseVideoDecoder *
    base_video_decoder)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_DECODER (base_video_decoder));

  base_video_decoder->have_src_caps = FALSE;
  gst_base_video_decoder_set_src_caps (base_video_decoder);
}

/* GObject vmethod implementations */
static void
gst_base_video_decoder_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseVideoDecoder *base_video_decoder = GST_BASE_VIDEO_DECODER (object);

  switch (property_id) {
    case PROP_PACKETIZED:
      g_value_set_boolean (value, base_video_decoder->packetized);
      break;
    case PROP_SINK_CLIPPING:
      g_value_set_boolean (value, base_video_decoder->sink_clipping);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/* GObject vmethod implementations */
static void
gst_base_video_decoder_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseVideoDecoder *base_video_decoder = GST_BASE_VIDEO_DECODER (object);

  switch (property_id) {
    case PROP_PACKETIZED:
      base_video_decoder->packetized = g_value_get_boolean (value);
      break;
    case PROP_SINK_CLIPPING:
      base_video_decoder->sink_clipping = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_base_video_decoder_finalize (GObject * object)
{
  GstBaseVideoDecoder *base_video_decoder;
  GstBaseVideoDecoderClass *base_video_decoder_class;

  g_return_if_fail (GST_IS_BASE_VIDEO_DECODER (object));
  base_video_decoder = GST_BASE_VIDEO_DECODER (object);
  base_video_decoder_class = GST_BASE_VIDEO_DECODER_GET_CLASS (object);

  gst_base_video_decoder_reset (base_video_decoder);

  if (base_video_decoder->input_adapter) {
    g_object_unref (base_video_decoder->input_adapter);
    base_video_decoder->input_adapter = NULL;
  }

  GST_DEBUG_OBJECT (object, "finalize");

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

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
  gobject_class->get_property = gst_base_video_decoder_get_property;
  gobject_class->set_property = gst_base_video_decoder_set_property;

  g_object_class_install_property (gobject_class, PROP_PACKETIZED,
      g_param_spec_boolean ("packetized", "Packetized",
          "Whether the incoming data is already packetized into suitable "
          "packets", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PACKETIZED,
      g_param_spec_boolean ("parse-codec-data", "Parse Codec Data",
          "Whether the codec_data should be parsed", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PACKETIZED,
      g_param_spec_boolean ("sink-clipping", "Sink Clipping",
          "If enabled GstBaseVideoDecoder will clip outgoing frames", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_base_video_decoder_change_state;

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_base_video_decoder_init (GstBaseVideoDecoder * base_video_decoder,
    GstBaseVideoDecoderClass * base_video_decoder_class)
{
  GstPadTemplate *pad_template;
  GstPad *pad;

  GST_DEBUG ("gst_base_video_decoder_init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS
      (base_video_decoder_class), "sink");
  g_return_if_fail (pad_template != NULL);

  base_video_decoder->sinkpad = pad =
      gst_pad_new_from_template (pad_template, "sink");
  gst_element_add_pad (GST_ELEMENT (base_video_decoder), pad);

  gst_pad_set_chain_function (pad, gst_base_video_decoder_chain);
  gst_pad_set_event_function (pad, gst_base_video_decoder_sink_event);
  gst_pad_set_setcaps_function (pad, gst_base_video_decoder_sink_setcaps);
  gst_pad_set_query_function (pad, gst_base_video_decoder_sink_query);

  if (base_video_decoder_class->create_srcpad) {
    base_video_decoder->srcpad = pad =
        base_video_decoder_class->create_srcpad (base_video_decoder,
        base_video_decoder_class);
  } else {
    pad_template =
        gst_element_class_get_pad_template (GST_ELEMENT_CLASS
        (base_video_decoder_class), "src");
    g_return_if_fail (pad_template != NULL);

    base_video_decoder->srcpad = pad =
        gst_pad_new_from_template (pad_template, "src");
  }
  gst_element_add_pad (GST_ELEMENT (base_video_decoder), pad);

  gst_pad_set_event_function (pad, gst_base_video_decoder_src_event);
  gst_pad_set_query_type_function (pad, gst_base_video_decoder_get_query_types);
  gst_pad_set_query_function (pad, gst_base_video_decoder_src_query);
  gst_pad_use_fixed_caps (pad);

  base_video_decoder->input_adapter = gst_adapter_new ();

  gst_segment_init (&base_video_decoder->segment, GST_FORMAT_TIME);

  base_video_decoder->current_frame =
      gst_base_video_decoder_new_frame (base_video_decoder);

  /* properties */
  base_video_decoder->packetized = FALSE;
  base_video_decoder->sink_clipping = TRUE;
}
