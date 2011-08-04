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
    gboolean at_eos);


GST_BOILERPLATE (GstBaseVideoDecoder, gst_base_video_decoder,
    GstElement, GST_TYPE_ELEMENT);



typedef struct _Timestamp Timestamp;
struct _Timestamp
{
  guint64 offset;
  GstClockTime timestamp;
  GstClockTime duration;
};

static void
gst_base_video_decoder_clear_timestamps (GstBaseVideoDecoder *
    base_video_decoder)
{
  GList *l;

  for (l = base_video_decoder->timestamps; l;
      l = base_video_decoder->timestamps) {
    g_slice_free (Timestamp, l->data);
    base_video_decoder->timestamps = l->next;
    g_list_free1 (l);
  }
}

static void
gst_base_video_decoder_add_timestamp (GstBaseVideoDecoder * base_video_decoder,
    GstBuffer * buffer)
{
  Timestamp *ts;

  ts = g_slice_new (Timestamp);

  GST_DEBUG ("adding timestamp %" G_GUINT64_FORMAT " %" GST_TIME_FORMAT,
      base_video_decoder->input_offset,
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
  GList *g;

  *timestamp = GST_CLOCK_TIME_NONE;
  *duration = GST_CLOCK_TIME_NONE;

  g = base_video_decoder->timestamps;
  while (g) {
    Timestamp *ts;

    ts = g->data;
    if (ts->offset <= offset) {
      *timestamp = ts->timestamp;
      *duration = ts->duration;
      g_slice_free (Timestamp, ts);
      g = g_list_next (g);
      base_video_decoder->timestamps =
          g_list_remove (base_video_decoder->timestamps, ts);
    } else {
      break;
    }
  }

  GST_DEBUG ("got timestamp %" G_GUINT64_FORMAT " %" GST_TIME_FORMAT,
      offset, GST_TIME_ARGS (*timestamp));
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

  return frame;
}

static void
gst_base_video_decoder_reset (GstBaseVideoDecoder * base_video_decoder)
{
  GST_DEBUG ("reset");

  base_video_decoder->discont = TRUE;
  base_video_decoder->have_sync = FALSE;

  base_video_decoder->timestamp_offset = GST_CLOCK_TIME_NONE;
  base_video_decoder->last_timestamp = GST_CLOCK_TIME_NONE;

  base_video_decoder->input_offset = 0;
  base_video_decoder->current_buf_offset = -1;
  base_video_decoder->prev_buf_offset = -1;

  gst_adapter_clear (base_video_decoder->input_adapter);

  if (base_video_decoder->current_frame) {
    gst_video_frame_unref (base_video_decoder->current_frame);
    base_video_decoder->current_frame = NULL;
  }

  gst_base_video_decoder_clear_timestamps (base_video_decoder);

  base_video_decoder->have_src_caps = FALSE;

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

static void
gst_base_video_decoder_reset_state (GstVideoState * state)
{
  if (state->codec_data)
    gst_buffer_unref (state->codec_data);

  memset (state, 0, sizeof (GstVideoState));
  state->par_n = state->par_d = 1;
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

  gst_base_video_decoder_reset_state (state);

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
  gboolean res = FALSE;

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (!base_video_decoder->packetized)
        gst_base_video_decoder_drain (base_video_decoder, TRUE);

      res =
          gst_pad_push_event (GST_BASE_VIDEO_DECODER_SRC_PAD
          (base_video_decoder), event);
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

      res =
          gst_pad_push_event (GST_BASE_VIDEO_DECODER_SRC_PAD
          (base_video_decoder), event);
      break;
    }

    case GST_EVENT_FLUSH_STOP:
      gst_base_video_decoder_flush (base_video_decoder);
      gst_segment_init (&base_video_decoder->segment, GST_FORMAT_TIME);

      res =
          gst_pad_push_event (GST_BASE_VIDEO_DECODER_SRC_PAD
          (base_video_decoder), event);
      break;

    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

done:
  gst_object_unref (base_video_decoder);
  return res;

newseg_wrong_format:
  GST_DEBUG_OBJECT (base_video_decoder, "received non TIME newsegment");
  gst_event_unref (event);
  goto done;
}

static gboolean
gst_base_video_decoder_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoDecoder *base_video_decoder;
  gboolean res = FALSE;

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:

      /* FIXME: do seek using bitrate incase upstream doesn't handle it */
      res =
          gst_pad_push_event (GST_BASE_VIDEO_DECODER_SINK_PAD
          (base_video_decoder), event);

      break;

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

  gst_object_unref (base_video_decoder);
  return res;
}

static const GstQueryType *
gst_base_video_decoder_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
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
      /* FIXME: approximate using bitrate if upstream doesn't answear */
      res = gst_pad_query (dec->sinkpad, query);
      break;

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

gboolean
gst_base_video_decoder_set_src_caps (GstBaseVideoDecoder * base_video_decoder)
{
  GstCaps *caps;
  GstVideoState *state = &base_video_decoder->state;

  if (base_video_decoder->have_src_caps)
    return TRUE;

  caps = gst_pad_get_allowed_caps (base_video_decoder->srcpad);
  if (!caps)
    goto null_allowed_caps;
  if (gst_caps_is_empty (caps))
    goto empty_allowed_caps;

  gst_caps_set_simple (caps,
      "width", G_TYPE_INT, state->width,
      "height", G_TYPE_INT, state->height,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, state->par_n, state->par_d,
      "interlaced", G_TYPE_BOOLEAN, state->interlaced, NULL);

  if (state->fps_d != 0)
    gst_caps_set_simple (caps,
        "framerate", GST_TYPE_FRACTION, state->fps_n, state->fps_d, NULL);


  gst_pad_fixate_caps (base_video_decoder->srcpad, caps);
  GST_DEBUG ("setting caps %" GST_PTR_FORMAT, caps);

  base_video_decoder->have_src_caps =
      gst_pad_set_caps (GST_BASE_VIDEO_DECODER_SRC_PAD (base_video_decoder),
      caps);
  gst_caps_unref (caps);

  return base_video_decoder->have_src_caps;

null_allowed_caps:
  GST_ERROR_OBJECT (base_video_decoder,
      "Got null from gst_pad_get_allowed_caps");
  return FALSE;

empty_allowed_caps:
  GST_ERROR_OBJECT (base_video_decoder,
      "Got EMPTY caps from gst_pad_get_allowed_caps");

  gst_caps_unref (caps);
  return FALSE;
}

static GstFlowReturn
gst_base_video_decoder_drain (GstBaseVideoDecoder * dec, gboolean at_eos)
{
  GstBaseVideoDecoderClass *klass;
  GstBaseVideoDecoderScanResult res;
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
    GstBuffer *buf;
    GstFlowReturn ret;

    GST_DEBUG ("Packet size: %u", size);
    if (size > gst_adapter_available (dec->input_adapter))
      return GST_FLOW_OK;

    buf = gst_adapter_take_buffer (dec->input_adapter, size);

    dec->prev_buf_offset = dec->current_buf_offset;
    dec->current_buf_offset = dec->input_offset -
        gst_adapter_available (dec->input_adapter);

    ret = klass->parse_data (dec, buf, at_eos, dec->current_frame);
    if (ret != GST_FLOW_OK)
      return ret;

    res = klass->scan_for_packet_end (dec, dec->input_adapter, &size, at_eos);
  }

  switch (res) {
    case GST_BASE_VIDEO_DECODER_SCAN_RESULT_LOST_SYNC:
      dec->have_sync = FALSE;
      goto lost_sync;

    case GST_BASE_VIDEO_DECODER_SCAN_RESULT_NEED_DATA:
      return GST_FLOW_OK;

    default:
      GST_ERROR_OBJECT (dec, "Subclass returned invalid scan result");
      return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_base_video_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseVideoDecoder *base_video_decoder;
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

  base_video_decoder->input_offset += GST_BUFFER_SIZE (buf);
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    gst_base_video_decoder_add_timestamp (base_video_decoder, buf);
  }

  if (!base_video_decoder->current_frame)
    base_video_decoder->current_frame =
        gst_base_video_decoder_new_frame (base_video_decoder);

  if (base_video_decoder->packetized) {
    base_video_decoder->current_frame->sink_buffer = buf;

    ret = gst_base_video_decoder_have_frame (base_video_decoder, TRUE, NULL);
  } else {

    gst_adapter_push (base_video_decoder->input_adapter, buf);

    ret = gst_base_video_decoder_drain (base_video_decoder, FALSE);
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

  gst_base_video_decoder_reset (base_video_decoder);

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
  gst_base_video_decoder_reset_state (&base_video_decoder->state);

  gst_segment_init (&base_video_decoder->segment, GST_FORMAT_TIME);

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
      if (!gst_base_video_decoder_start (base_video_decoder))
        return GST_STATE_CHANGE_FAILURE;
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_base_video_decoder_stop (base_video_decoder))
        ret = GST_STATE_CHANGE_FAILURE;
      break;

    default:
      break;
  }

  return ret;
}

static gboolean
gst_base_video_decoder_check_timestamp (GstBaseVideoDecoder *
    base_video_decoder, GstClockTime timestamp)
{
  if (timestamp == GST_CLOCK_TIME_NONE)
    return FALSE;

  if (GST_CLOCK_TIME_IS_VALID (base_video_decoder->last_timestamp))
    return timestamp > base_video_decoder->last_timestamp;

  return TRUE;
}

static void
gst_base_video_decoder_calculate_timestamps (GstBaseVideoDecoder *
    base_video_decoder, GstVideoFrame * frame,
    GstClockTime * presentation_timestamp, GstClockTime * presentation_duration)
{
  GST_DEBUG ("calculate timestamps sync=%d upstream timestamp: %"
      GST_TIME_FORMAT " parsed timestamp: %" GST_TIME_FORMAT,
      GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VIDEO_FRAME_FLAG_SYNC_POINT),
      GST_TIME_ARGS (frame->upstream_timestamp),
      GST_TIME_ARGS (frame->parsed_timestamp));

  *presentation_timestamp = GST_CLOCK_TIME_NONE;
  *presentation_duration = GST_CLOCK_TIME_NONE;

  if (gst_base_video_decoder_check_timestamp (base_video_decoder,
          frame->upstream_timestamp)) {
    *presentation_timestamp = frame->upstream_timestamp;
    *presentation_duration = frame->upstream_duration;
  }

  else if (gst_base_video_decoder_check_timestamp (base_video_decoder,
          frame->parsed_timestamp))
    *presentation_timestamp = frame->parsed_timestamp;


  if (GST_CLOCK_TIME_IS_VALID (*presentation_timestamp)) {
    GST_DEBUG ("sync timestamp %" GST_TIME_FORMAT " diff %" GST_TIME_FORMAT,
        GST_TIME_ARGS (*presentation_timestamp),
        GST_TIME_ARGS (*presentation_timestamp -
            base_video_decoder->segment.start));
    base_video_decoder->timestamp_offset = *presentation_timestamp;
    base_video_decoder->field_index = 0;
  }

  else {
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

    *presentation_timestamp =
        gst_base_video_decoder_get_field_timestamp (base_video_decoder,
        base_video_decoder->field_index);
  }

  if (*presentation_duration == GST_CLOCK_TIME_NONE) {
    *presentation_duration =
        gst_base_video_decoder_get_field_duration (base_video_decoder,
        frame->n_fields);
  }

  base_video_decoder->field_index += frame->n_fields;
  base_video_decoder->last_timestamp = *presentation_timestamp;
}

GstFlowReturn
gst_base_video_decoder_finish_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame)
{
  GstBaseVideoDecoderClass *base_video_decoder_class;

  GstClockTime presentation_timestamp;
  GstClockTime presentation_duration;

  GstBuffer *src_buffer;

  GST_DEBUG ("finish frame");

  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);


  if (!gst_base_video_decoder_set_src_caps (base_video_decoder))
    return GST_FLOW_NOT_NEGOTIATED;

  gst_base_video_decoder_calculate_timestamps (base_video_decoder, frame,
      &presentation_timestamp, &presentation_duration);

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

  GST_BUFFER_TIMESTAMP (src_buffer) = presentation_timestamp;
  GST_BUFFER_DURATION (src_buffer) = presentation_duration;
  GST_BUFFER_OFFSET (src_buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (src_buffer) = GST_BUFFER_OFFSET_NONE;

  GST_DEBUG ("pushing frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (presentation_timestamp));

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
  GstClockTime presentation_timestamp;
  GstClockTime presentation_duration;

  GST_DEBUG ("skip frame");

  gst_base_video_decoder_calculate_timestamps (base_video_decoder, frame,
      &presentation_timestamp, &presentation_duration);

  GST_DEBUG ("skipping frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (presentation_timestamp));

  gst_video_frame_unref (frame);
}

GstFlowReturn
gst_base_video_decoder_have_frame (GstBaseVideoDecoder * base_video_decoder,
    gboolean include_current_buf, GstVideoFrame ** new_frame)
{
  GstVideoFrame *frame = base_video_decoder->current_frame;
  GstBaseVideoDecoderClass *klass;

  guint64 frame_end_offset;
  GstClockTime timestamp, duration;
  GstClockTime running_time;
  GstClockTimeDiff deadline;
  GstFlowReturn ret;

  klass = GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  if (include_current_buf)
    frame_end_offset = base_video_decoder->current_buf_offset;
  else
    frame_end_offset = base_video_decoder->prev_buf_offset;

  gst_base_video_decoder_get_timestamp_at_offset (base_video_decoder,
      frame_end_offset, &timestamp, &duration);

  frame->upstream_timestamp = timestamp;
  frame->upstream_duration = duration;

  GST_DEBUG ("upstream timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->upstream_timestamp));

  running_time = gst_segment_to_running_time (&base_video_decoder->segment,
      GST_FORMAT_TIME, frame->upstream_timestamp);

  if (GST_CLOCK_TIME_IS_VALID (base_video_decoder->earliest_time))
    deadline = GST_CLOCK_DIFF (base_video_decoder->earliest_time, running_time);
  else
    deadline = G_MAXINT64;

  /* do something with frame */
  ret = klass->handle_frame (base_video_decoder, frame, deadline);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG ("flow error!");
  }

  /* create new frame */
  base_video_decoder->current_frame =
      gst_base_video_decoder_new_frame (base_video_decoder);

  if (new_frame)
    *new_frame = base_video_decoder->current_frame;

  return ret;
}

GstVideoState
gst_base_video_decoder_get_state (GstBaseVideoDecoder * base_video_decoder)
{
  return base_video_decoder->state;
}

void
gst_base_video_decoder_set_state (GstBaseVideoDecoder * base_video_decoder,
    GstVideoState state)
{
  base_video_decoder->state = state;

  base_video_decoder->have_src_caps = FALSE;
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

  g_return_if_fail (GST_IS_BASE_VIDEO_DECODER (object));
  base_video_decoder = GST_BASE_VIDEO_DECODER (object);

  g_object_unref (base_video_decoder->input_adapter);

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
  memset (&base_video_decoder->state, 0, sizeof (GstVideoState));

  /* properties */
  base_video_decoder->packetized = FALSE;
  base_video_decoder->sink_clipping = TRUE;
}
