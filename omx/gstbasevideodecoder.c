/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
 * Copyright (C) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>.
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
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
 * SECTION:gstbasevideodecoder
 * @short_description: Base class for video decoders
 * @see_also: #GstBaseTransform
 *
 * This base class is for video decoders turning encoded data into raw video
 * frames.
 *
 * GstBaseVideoDecoder and subclass should cooperate as follows.
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Configuration</title>
 *   <listitem><para>
 *     Initially, GstBaseVideoDecoder calls @start when the decoder element
 *     is activated, which allows subclass to perform any global setup.
 *   </para></listitem>
 *   <listitem><para>
 *     GstBaseVideoDecoder calls @set_format to inform subclass of caps
 *     describing input video data that it is about to receive, including
 *     possibly configuration data.
 *     While unlikely, it might be called more than once, if changing input
 *     parameters require reconfiguration.
 *   </para></listitem>
 *   <listitem><para>
 *     GstBaseVideoDecoder calls @stop at end of all processing.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist>
 *   <title>Data processing</title>
 *     <listitem><para>
 *       Base class gathers input data, and optionally allows subclass
 *       to parse this into subsequently manageable chunks, typically
 *       corresponding to and referred to as 'frames'.
 *     </para></listitem>
 *     <listitem><para>
 *       Input frame is provided to subclass' @handle_frame.
 *     </para></listitem>
 *     <listitem><para>
 *       If codec processing results in decoded data, subclass should call
 *       @gst_base_video_decoder_finish_frame to have decoded data pushed
 *       downstream.
 *     </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Shutdown phase</title>
 *   <listitem><para>
 *     GstBaseVideoDecoder class calls @stop to inform the subclass that data
 *     parsing will be stopped.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * </orderedlist>
 *
 * Subclass is responsible for providing pad template caps for
 * source and sink pads. The pads need to be named "sink" and "src". It also
 * needs to set the fixed caps on srcpad, when the format is ensured.  This
 * is typically when base class calls subclass' @set_format function, though
 * it might be delayed until calling @gst_base_video_decoder_finish_frame.
 *
 * Subclass is also responsible for providing (presentation) timestamps
 * (likely based on corresponding input ones).  If that is not applicable
 * or possible, baseclass provides limited framerate based interpolation.
 *
 * Similarly, the baseclass provides some limited (legacy) seeking support
 * (upon explicit subclass request), as full-fledged support
 * should rather be left to upstream demuxer, parser or alike.  This simple
 * approach caters for seeking and duration reporting using estimated input
 * bitrates.
 *
 * Baseclass provides some support for reverse playback, in particular
 * in case incoming data is not packetized or upstream does not provide
 * fragments on keyframe boundaries.  However, subclass should then be prepared
 * for the parsing and frame processing stage to occur separately (rather
 * than otherwise the latter immediately following the former),
 * and should ensure the parsing stage properly marks keyframes or rely on
 * upstream to do so properly for incoming data.
 *
 * Things that subclass need to take care of:
 * <itemizedlist>
 *   <listitem><para>Provide pad templates</para></listitem>
 *   <listitem><para>
 *      Set source pad caps when appropriate
 *   </para></listitem>
 *   <listitem><para>
 *      Configure some baseclass behaviour parameters.
 *   </para></listitem>
 *   <listitem><para>
 *      Optionally parse input data, if it is not considered packetized.
 *      Parse sync is obtained either by providing baseclass with a
 *      mask and pattern or a custom @scan_for_sync.  When sync is established,
 *      @parse_data should invoke @gst_base_video_decoder_add_to_frame and
 *      @gst_base_video_decoder_have_frame as appropriate.
 *   </para></listitem>
 *   <listitem><para>
 *      Accept data in @handle_frame and provide decoded results to
 *      @gst_base_video_decoder_finish_frame.
 *   </para></listitem>
 * </itemizedlist>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstbasevideodecoder.h"
#include "gstbasevideoutils.h"

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
static GstStateChangeReturn gst_base_video_decoder_change_state (GstElement *
    element, GstStateChange transition);
static const GstQueryType *gst_base_video_decoder_get_query_types (GstPad *
    pad);
static gboolean gst_base_video_decoder_src_query (GstPad * pad,
    GstQuery * query);
static void gst_base_video_decoder_reset (GstBaseVideoDecoder *
    base_video_decoder, gboolean full);

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

static void gst_base_video_decoder_clear_queues (GstBaseVideoDecoder * dec);

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

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_video_decoder_change_state);
}

static void
gst_base_video_decoder_init (GstBaseVideoDecoder * base_video_decoder,
    GstBaseVideoDecoderClass * klass)
{
  GstPad *pad;

  GST_DEBUG_OBJECT (base_video_decoder, "gst_base_video_decoder_init");

  pad = GST_BASE_VIDEO_CODEC_SINK_PAD (base_video_decoder);

  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_video_decoder_chain));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_video_decoder_sink_event));
  gst_pad_set_setcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_video_decoder_sink_setcaps));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_video_decoder_sink_query));

  pad = GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder);

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_video_decoder_src_event));
  gst_pad_set_query_type_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_video_decoder_get_query_types));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_base_video_decoder_src_query));
  gst_pad_use_fixed_caps (pad);

  base_video_decoder->input_adapter = gst_adapter_new ();
  base_video_decoder->output_adapter = gst_adapter_new ();

  gst_base_video_decoder_reset (base_video_decoder, TRUE);

  base_video_decoder->sink_clipping = TRUE;
}

static gboolean
gst_base_video_decoder_push_src_event (GstBaseVideoDecoder * decoder,
    GstEvent * event)
{
  /* Forward non-serialized events and EOS/FLUSH_STOP immediately.
   * For EOS this is required because no buffer or serialized event
   * will come after EOS and nothing could trigger another
   * _finish_frame() call.   *
   * If the subclass handles sending of EOS manually it can return
   * _DROPPED from ::finish() and all other subclasses should have
   * decoded/flushed all remaining data before this
   *
   * For FLUSH_STOP this is required because it is expected
   * to be forwarded immediately and no buffers are queued anyway.
   */
  if (!GST_EVENT_IS_SERIALIZED (event)
      || GST_EVENT_TYPE (event) == GST_EVENT_EOS
      || GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP)
    return gst_pad_push_event (decoder->base_video_codec.srcpad, event);

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (decoder);
  decoder->current_frame_events =
      g_list_prepend (decoder->current_frame_events, event);
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (decoder);

  return TRUE;
}

static gboolean
gst_base_video_decoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseVideoDecoder *base_video_decoder;
  GstBaseVideoDecoderClass *base_video_decoder_class;
  GstStructure *structure;
  const GValue *codec_data;
  GstVideoState state;
  gboolean ret = TRUE;

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));
  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  GST_DEBUG_OBJECT (base_video_decoder, "setcaps %" GST_PTR_FORMAT, caps);

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);

  memset (&state, 0, sizeof (state));

  state.caps = gst_caps_ref (caps);

  structure = gst_caps_get_structure (caps, 0);

  gst_video_format_parse_caps (caps, NULL, &state.width, &state.height);
  /* this one fails if no framerate in caps */
  if (!gst_video_parse_caps_framerate (caps, &state.fps_n, &state.fps_d)) {
    state.fps_n = 0;
    state.fps_d = 1;
  }
  /* but the p-a-r sets 1/1 instead, which is not quite informative ... */
  if (!gst_structure_has_field (structure, "pixel-aspect-ratio") ||
      !gst_video_parse_caps_pixel_aspect_ratio (caps,
          &state.par_n, &state.par_d)) {
    state.par_n = 0;
    state.par_d = 1;
  }

  state.have_interlaced =
      gst_video_format_parse_caps_interlaced (caps, &state.interlaced);

  codec_data = gst_structure_get_value (structure, "codec_data");
  if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER) {
    state.codec_data = GST_BUFFER (gst_value_dup_mini_object (codec_data));
  }

  if (base_video_decoder_class->set_format) {
    ret = base_video_decoder_class->set_format (base_video_decoder, &state);
  }

  if (ret) {
    gst_buffer_replace (&GST_BASE_VIDEO_CODEC (base_video_decoder)->
        state.codec_data, NULL);
    gst_caps_replace (&GST_BASE_VIDEO_CODEC (base_video_decoder)->state.caps,
        NULL);
    GST_BASE_VIDEO_CODEC (base_video_decoder)->state = state;
  } else {
    gst_buffer_replace (&state.codec_data, NULL);
    gst_caps_replace (&state.caps, NULL);
  }

  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
  g_object_unref (base_video_decoder);

  return ret;
}

static void
gst_base_video_decoder_finalize (GObject * object)
{
  GstBaseVideoDecoder *base_video_decoder;

  base_video_decoder = GST_BASE_VIDEO_DECODER (object);

  GST_DEBUG_OBJECT (object, "finalize");

  if (base_video_decoder->input_adapter) {
    g_object_unref (base_video_decoder->input_adapter);
    base_video_decoder->input_adapter = NULL;
  }
  if (base_video_decoder->output_adapter) {
    g_object_unref (base_video_decoder->output_adapter);
    base_video_decoder->output_adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* hard == FLUSH, otherwise discont */
static GstFlowReturn
gst_base_video_decoder_flush (GstBaseVideoDecoder * dec, gboolean hard)
{
  GstBaseVideoDecoderClass *klass;
  GstFlowReturn ret = GST_FLOW_OK;

  klass = GST_BASE_VIDEO_DECODER_GET_CLASS (dec);

  GST_LOG_OBJECT (dec, "flush hard %d", hard);

  /* Inform subclass */
  /* FIXME ? only if hard, or tell it if hard ? */
  if (klass->reset)
    klass->reset (dec);

  /* FIXME make some more distinction between hard and soft,
   * but subclass may not be prepared for that */
  /* FIXME perhaps also clear pending frames ?,
   * but again, subclass may still come up with one of those */
  if (!hard) {
    /* TODO ? finish/drain some stuff */
  } else {
    gst_segment_init (&GST_BASE_VIDEO_CODEC (dec)->segment,
        GST_FORMAT_UNDEFINED);
    gst_base_video_decoder_clear_queues (dec);
    dec->error_count = 0;
    g_list_foreach (dec->current_frame_events, (GFunc) gst_event_unref, NULL);
    g_list_free (dec->current_frame_events);
    dec->current_frame_events = NULL;
  }
  /* and get (re)set for the sequel */
  gst_base_video_decoder_reset (dec, FALSE);

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

  GST_DEBUG_OBJECT (base_video_decoder,
      "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstFlowReturn flow_ret;

      GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);
      if (!base_video_decoder->packetized) {
        do {
          flow_ret =
              base_video_decoder_class->parse_data (base_video_decoder, TRUE);
        } while (flow_ret == GST_FLOW_OK);
      }

      if (base_video_decoder_class->finish) {
        flow_ret = base_video_decoder_class->finish (base_video_decoder);
      } else {
        flow_ret = GST_FLOW_OK;
      }

      if (flow_ret == GST_FLOW_OK)
        ret = gst_base_video_decoder_push_src_event (base_video_decoder, event);
      GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      double rate, arate;
      GstFormat format;
      gint64 start;
      gint64 stop;
      gint64 pos;
      GstSegment *segment = &GST_BASE_VIDEO_CODEC (base_video_decoder)->segment;

      GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);
      gst_event_parse_new_segment_full (event, &update, &rate,
          &arate, &format, &start, &stop, &pos);

      if (format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (base_video_decoder,
            "received TIME NEW_SEGMENT %" GST_TIME_FORMAT
            " -- %" GST_TIME_FORMAT ", pos %" GST_TIME_FORMAT
            ", rate %g, applied_rate %g",
            GST_TIME_ARGS (start), GST_TIME_ARGS (stop), GST_TIME_ARGS (pos),
            rate, arate);
      } else {
        GstFormat dformat = GST_FORMAT_TIME;

        GST_DEBUG_OBJECT (base_video_decoder,
            "received NEW_SEGMENT %" G_GINT64_FORMAT
            " -- %" G_GINT64_FORMAT ", time %" G_GINT64_FORMAT
            ", rate %g, applied_rate %g", start, stop, pos, rate, arate);
        /* handle newsegment as a result from our legacy simple seeking */
        /* note that initial 0 should convert to 0 in any case */
        if (base_video_decoder->do_byte_time &&
            gst_pad_query_convert (GST_BASE_VIDEO_CODEC_SINK_PAD
                (base_video_decoder), GST_FORMAT_BYTES, start, &dformat,
                &start)) {
          /* best attempt convert */
          /* as these are only estimates, stop is kept open-ended to avoid
           * premature cutting */
          GST_DEBUG_OBJECT (base_video_decoder,
              "converted to TIME start %" GST_TIME_FORMAT,
              GST_TIME_ARGS (start));
          pos = start;
          stop = GST_CLOCK_TIME_NONE;
          /* replace event */
          gst_event_unref (event);
          event = gst_event_new_new_segment_full (update, rate, arate,
              GST_FORMAT_TIME, start, stop, pos);
        } else {
          GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
          goto newseg_wrong_format;
        }
      }

      if (!update) {
        gst_base_video_decoder_flush (base_video_decoder, FALSE);
      }

      base_video_decoder->timestamp_offset = start;

      gst_segment_set_newsegment_full (segment,
          update, rate, arate, format, start, stop, pos);

      ret = gst_base_video_decoder_push_src_event (base_video_decoder, event);
      GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);
      /* well, this is kind of worse than a DISCONT */
      gst_base_video_decoder_flush (base_video_decoder, TRUE);
      GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
    }
    default:
      /* FIXME this changes the order of events */
      ret = gst_base_video_decoder_push_src_event (base_video_decoder, event);
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

/* perform upstream byte <-> time conversion (duration, seeking)
 * if subclass allows and if enough data for moderately decent conversion */
static inline gboolean
gst_base_video_decoder_do_byte (GstBaseVideoDecoder * dec)
{
  GstBaseVideoCodec *codec = GST_BASE_VIDEO_CODEC (dec);

  return dec->do_byte_time && (codec->bytes > 0) && (codec->time > GST_SECOND);
}

static gboolean
gst_base_video_decoder_do_seek (GstBaseVideoDecoder * dec, GstEvent * event)
{
  GstBaseVideoCodec *codec = GST_BASE_VIDEO_CODEC (dec);
  GstSeekFlags flags;
  GstSeekType start_type, end_type;
  GstFormat format;
  gdouble rate;
  gint64 start, start_time, end_time;
  GstSegment seek_segment;
  guint32 seqnum;

  gst_event_parse_seek (event, &rate, &format, &flags, &start_type,
      &start_time, &end_type, &end_time);

  /* we'll handle plain open-ended flushing seeks with the simple approach */
  if (rate != 1.0) {
    GST_DEBUG_OBJECT (dec, "unsupported seek: rate");
    return FALSE;
  }

  if (start_type != GST_SEEK_TYPE_SET) {
    GST_DEBUG_OBJECT (dec, "unsupported seek: start time");
    return FALSE;
  }

  if (end_type != GST_SEEK_TYPE_NONE ||
      (end_type == GST_SEEK_TYPE_SET && end_time != GST_CLOCK_TIME_NONE)) {
    GST_DEBUG_OBJECT (dec, "unsupported seek: end time");
    return FALSE;
  }

  if (!(flags & GST_SEEK_FLAG_FLUSH)) {
    GST_DEBUG_OBJECT (dec, "unsupported seek: not flushing");
    return FALSE;
  }

  memcpy (&seek_segment, &codec->segment, sizeof (seek_segment));
  gst_segment_set_seek (&seek_segment, rate, format, flags, start_type,
      start_time, end_type, end_time, NULL);
  start_time = seek_segment.last_stop;

  format = GST_FORMAT_BYTES;
  if (!gst_pad_query_convert (codec->sinkpad, GST_FORMAT_TIME, start_time,
          &format, &start)) {
    GST_DEBUG_OBJECT (dec, "conversion failed");
    return FALSE;
  }

  seqnum = gst_event_get_seqnum (event);
  event = gst_event_new_seek (1.0, GST_FORMAT_BYTES, flags,
      GST_SEEK_TYPE_SET, start, GST_SEEK_TYPE_NONE, -1);
  gst_event_set_seqnum (event, seqnum);

  GST_DEBUG_OBJECT (dec, "seeking to %" GST_TIME_FORMAT " at byte offset %"
      G_GINT64_FORMAT, GST_TIME_ARGS (start_time), start);

  return gst_pad_push_event (codec->sinkpad, event);
}

static gboolean
gst_base_video_decoder_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseVideoDecoder *base_video_decoder;
  gboolean res = FALSE;

  base_video_decoder = GST_BASE_VIDEO_DECODER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (base_video_decoder,
      "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format, tformat;
      gdouble rate;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gint64 tcur, tstop;
      guint32 seqnum;

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
          &stop_type, &stop);
      seqnum = gst_event_get_seqnum (event);

      /* upstream gets a chance first */
      if ((res =
              gst_pad_push_event (GST_BASE_VIDEO_CODEC_SINK_PAD
                  (base_video_decoder), event)))
        break;

      /* if upstream fails for a time seek, maybe we can help if allowed */
      if (format == GST_FORMAT_TIME) {
        if (gst_base_video_decoder_do_byte (base_video_decoder))
          res = gst_base_video_decoder_do_seek (base_video_decoder, event);
        break;
      }

      /* ... though a non-time seek can be aided as well */
      /* First bring the requested format to time */
      tformat = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_convert (pad, format, cur, &tformat, &tcur)))
        goto convert_error;
      if (!(res = gst_pad_query_convert (pad, format, stop, &tformat, &tstop)))
        goto convert_error;

      /* then seek with time on the peer */
      event = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);
      gst_event_set_seqnum (event, seqnum);

      res =
          gst_pad_push_event (GST_BASE_VIDEO_CODEC_SINK_PAD
          (base_video_decoder), event);
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

  GST_LOG_OBJECT (dec, "handling query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time, value;

      /* upstream gets a chance first */
      if ((res =
              gst_pad_peer_query (GST_BASE_VIDEO_CODEC_SINK_PAD (dec),
                  query))) {
        GST_LOG_OBJECT (dec, "returning peer response");
        break;
      }

      /* we start from the last seen time */
      time = dec->last_timestamp;
      /* correct for the segment values */
      time = gst_segment_to_stream_time (&GST_BASE_VIDEO_CODEC (dec)->segment,
          GST_FORMAT_TIME, time);

      GST_LOG_OBJECT (dec,
          "query %p: our time: %" GST_TIME_FORMAT, query, GST_TIME_ARGS (time));

      /* and convert to the final format */
      gst_query_parse_position (query, &format, NULL);
      if (!(res = gst_pad_query_convert (pad, GST_FORMAT_TIME, time,
                  &format, &value)))
        break;

      gst_query_set_position (query, format, value);

      GST_LOG_OBJECT (dec,
          "query %p: we return %" G_GINT64_FORMAT " (format %u)", query, value,
          format);
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      /* upstream in any case */
      if ((res = gst_pad_query_default (pad, query)))
        break;

      gst_query_parse_duration (query, &format, NULL);
      /* try answering TIME by converting from BYTE if subclass allows  */
      if (format == GST_FORMAT_TIME && gst_base_video_decoder_do_byte (dec)) {
        gint64 value;

        format = GST_FORMAT_BYTES;
        if (gst_pad_query_peer_duration (GST_BASE_VIDEO_CODEC_SINK_PAD (dec),
                &format, &value)) {
          GST_LOG_OBJECT (dec, "upstream size %" G_GINT64_FORMAT, value);
          format = GST_FORMAT_TIME;
          if (gst_pad_query_convert (GST_BASE_VIDEO_CODEC_SINK_PAD (dec),
                  GST_FORMAT_BYTES, value, &format, &value)) {
            gst_query_set_duration (query, GST_FORMAT_TIME, value);
            res = TRUE;
          }
        }
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      GST_DEBUG_OBJECT (dec, "convert query");

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = gst_base_video_rawvideo_convert (&GST_BASE_VIDEO_CODEC (dec)->state,
          src_fmt, src_val, &dest_fmt, &dest_val);
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

  GST_LOG_OBJECT (base_video_decoder, "handling query: %" GST_PTR_FORMAT,
      query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstBaseVideoCodec *codec = GST_BASE_VIDEO_CODEC (base_video_decoder);
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = gst_base_video_encoded_video_convert (&codec->state, codec->bytes,
          codec->time, src_fmt, src_val, &dest_fmt, &dest_val);
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

  GST_LOG_OBJECT (base_video_decoder,
      "adding timestamp %" GST_TIME_FORMAT " %" GST_TIME_FORMAT,
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

  GST_LOG_OBJECT (base_video_decoder,
      "got timestamp %" GST_TIME_FORMAT " %" GST_TIME_FORMAT,
      GST_TIME_ARGS (offset), GST_TIME_ARGS (*timestamp));
}

static void
gst_base_video_decoder_clear_queues (GstBaseVideoDecoder * dec)
{
  g_list_foreach (dec->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->queued);
  dec->queued = NULL;
  g_list_foreach (dec->gather, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->gather);
  dec->gather = NULL;
  g_list_foreach (dec->decode, (GFunc) gst_base_video_codec_free_frame, NULL);
  g_list_free (dec->decode);
  dec->decode = NULL;
  g_list_foreach (dec->parse, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->parse);
  dec->parse = NULL;
  g_list_foreach (dec->parse_gather, (GFunc) gst_base_video_codec_free_frame,
      NULL);
  g_list_free (dec->parse_gather);
  dec->parse_gather = NULL;
}

static void
gst_base_video_decoder_reset (GstBaseVideoDecoder * base_video_decoder,
    gboolean full)
{
  GST_DEBUG_OBJECT (base_video_decoder, "reset full %d", full);

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);

  if (full) {
    gst_segment_init (&GST_BASE_VIDEO_CODEC (base_video_decoder)->segment,
        GST_FORMAT_UNDEFINED);
    gst_base_video_decoder_clear_queues (base_video_decoder);
    base_video_decoder->error_count = 0;
  }

  GST_BASE_VIDEO_CODEC (base_video_decoder)->discont = TRUE;
  base_video_decoder->have_sync = FALSE;

  base_video_decoder->timestamp_offset = GST_CLOCK_TIME_NONE;
  base_video_decoder->field_index = 0;
  base_video_decoder->last_timestamp = GST_CLOCK_TIME_NONE;

  base_video_decoder->input_offset = 0;
  base_video_decoder->frame_offset = 0;
  gst_adapter_clear (base_video_decoder->input_adapter);
  gst_adapter_clear (base_video_decoder->output_adapter);
  g_list_foreach (base_video_decoder->timestamps, (GFunc) g_free, NULL);
  g_list_free (base_video_decoder->timestamps);
  base_video_decoder->timestamps = NULL;

  if (base_video_decoder->current_frame) {
    gst_base_video_codec_free_frame (base_video_decoder->current_frame);
    base_video_decoder->current_frame = NULL;
  }

  base_video_decoder->dropped = 0;
  base_video_decoder->processed = 0;

  GST_BASE_VIDEO_CODEC (base_video_decoder)->system_frame_number = 0;
  base_video_decoder->base_picture_number = 0;

  GST_OBJECT_LOCK (base_video_decoder);
  GST_BASE_VIDEO_CODEC (base_video_decoder)->earliest_time =
      GST_CLOCK_TIME_NONE;
  GST_BASE_VIDEO_CODEC (base_video_decoder)->proportion = 0.5;
  GST_OBJECT_UNLOCK (base_video_decoder);
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
}

static GstFlowReturn
gst_base_video_decoder_chain_forward (GstBaseVideoDecoder * base_video_decoder,
    GstBuffer * buf)
{
  GstBaseVideoDecoderClass *klass;
  GstFlowReturn ret;

  klass = GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  g_return_val_if_fail (base_video_decoder->packetized || klass->parse_data,
      GST_FLOW_ERROR);

  if (base_video_decoder->current_frame == NULL) {
    base_video_decoder->current_frame =
        gst_base_video_decoder_new_frame (base_video_decoder);
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    gst_base_video_decoder_add_timestamp (base_video_decoder, buf);
  }
  base_video_decoder->input_offset += GST_BUFFER_SIZE (buf);

  if (base_video_decoder->packetized) {
    base_video_decoder->current_frame->sink_buffer = buf;

    if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT))
      base_video_decoder->current_frame->is_sync_point = TRUE;

    ret = gst_base_video_decoder_have_frame_2 (base_video_decoder);
  } else {

    gst_adapter_push (base_video_decoder->input_adapter, buf);

    if (!base_video_decoder->have_sync) {
      int n, m;

      GST_DEBUG_OBJECT (base_video_decoder, "no sync, scanning");

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
        GST_ERROR_OBJECT (base_video_decoder, "scan returned no sync");
        gst_adapter_flush (base_video_decoder->input_adapter, n - 3);

        return GST_FLOW_OK;
      } else {
        if (m > 0) {
          if (m >= n) {
            GST_ERROR_OBJECT (base_video_decoder,
                "subclass scanned past end %d >= %d", m, n);
          }

          gst_adapter_flush (base_video_decoder->input_adapter, m);

          if (m < n) {
            GST_DEBUG_OBJECT (base_video_decoder,
                "found possible sync after %d bytes (of %d)", m, n);

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
      return GST_FLOW_OK;
    }
  }

  return ret;
}

static GstFlowReturn
gst_base_video_decoder_flush_decode (GstBaseVideoDecoder * dec)
{
  GstFlowReturn res = GST_FLOW_OK;
  GList *walk;

  walk = dec->decode;

  GST_DEBUG_OBJECT (dec, "flushing buffers to decode");

  /* clear buffer and decoder state */
  gst_base_video_decoder_flush (dec, FALSE);

  /* signal have_frame it should not capture frames */
  dec->process = TRUE;

  while (walk) {
    GList *next;
    GstVideoFrame *frame = (GstVideoFrame *) (walk->data);
    GstBuffer *buf = frame->sink_buffer;

    GST_DEBUG_OBJECT (dec, "decoding frame %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    next = g_list_next (walk);
    if (dec->current_frame)
      gst_base_video_codec_free_frame (dec->current_frame);
    dec->current_frame = frame;
    /* decode buffer, resulting data prepended to queue */
    res = gst_base_video_decoder_have_frame_2 (dec);

    walk = next;
  }

  dec->process = FALSE;

  return res;
}

static GstFlowReturn
gst_base_video_decoder_flush_parse (GstBaseVideoDecoder * dec)
{
  GstFlowReturn res = GST_FLOW_OK;
  GList *walk;

  walk = dec->parse;

  GST_DEBUG_OBJECT (dec, "flushing buffers to parsing");

  /* clear buffer and decoder state */
  gst_base_video_decoder_flush (dec, FALSE);

  while (walk) {
    GList *next;
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    GST_DEBUG_OBJECT (dec, "parsing buffer %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    next = g_list_next (walk);
    /* parse buffer, resulting frames prepended to parse_gather queue */
    gst_buffer_ref (buf);
    res = gst_base_video_decoder_chain_forward (dec, buf);

    /* if we generated output, we can discard the buffer, else we
     * keep it in the queue */
    if (dec->parse_gather) {
      GST_DEBUG_OBJECT (dec, "parsed buffer to %p", dec->parse_gather->data);
      dec->parse = g_list_delete_link (dec->parse, walk);
      gst_buffer_unref (buf);
    } else {
      GST_DEBUG_OBJECT (dec, "buffer did not decode, keeping");
    }
    walk = next;
  }

  /* now we can process frames */
  GST_DEBUG_OBJECT (dec, "checking frames");
  while (dec->parse_gather) {
    GstVideoFrame *frame;

    frame = (GstVideoFrame *) (dec->parse_gather->data);
    /* remove from the gather list */
    dec->parse_gather =
        g_list_delete_link (dec->parse_gather, dec->parse_gather);
    /* copy to decode queue */
    dec->decode = g_list_prepend (dec->decode, frame);

    /* if we copied a keyframe, flush and decode the decode queue */
    if (frame->is_sync_point) {
      GST_DEBUG_OBJECT (dec, "copied keyframe");
      res = gst_base_video_decoder_flush_decode (dec);
    }
  }

  /* now send queued data downstream */
  while (dec->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (dec->queued->data);

    if (G_LIKELY (res == GST_FLOW_OK)) {
      GST_DEBUG_OBJECT (dec, "pushing buffer %p of size %u, "
          "time %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT, buf,
          GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
      /* should be already, but let's be sure */
      buf = gst_buffer_make_metadata_writable (buf);
      /* avoid stray DISCONT from forward processing,
       * which have no meaning in reverse pushing */
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);
      res = gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (dec), buf);
    } else {
      gst_buffer_unref (buf);
    }

    dec->queued = g_list_delete_link (dec->queued, dec->queued);
  }

  return res;
}

static GstFlowReturn
gst_base_video_decoder_chain_reverse (GstBaseVideoDecoder * dec,
    GstBuffer * buf)
{
  GstFlowReturn result = GST_FLOW_OK;

  /* if we have a discont, move buffers to the decode list */
  if (!buf || GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    GST_DEBUG_OBJECT (dec, "received discont");
    while (dec->gather) {
      GstBuffer *gbuf;

      gbuf = GST_BUFFER_CAST (dec->gather->data);
      /* remove from the gather list */
      dec->gather = g_list_delete_link (dec->gather, dec->gather);
      /* copy to parse queue */
      dec->parse = g_list_prepend (dec->parse, gbuf);
    }
    /* parse and decode stuff in the parse queue */
    gst_base_video_decoder_flush_parse (dec);
  }

  if (G_LIKELY (buf)) {
    GST_DEBUG_OBJECT (dec, "gathering buffer %p of size %u, "
        "time %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT, buf,
        GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    /* add buffer to gather queue */
    dec->gather = g_list_prepend (dec->gather, buf);
  }

  return result;
}

static GstFlowReturn
gst_base_video_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseVideoDecoder *base_video_decoder;
  GstFlowReturn ret = GST_FLOW_OK;

  base_video_decoder = GST_BASE_VIDEO_DECODER (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (base_video_decoder,
      "chain %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT " size %d",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_SIZE (buf));

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);

  /* NOTE:
   * requiring the pad to be negotiated makes it impossible to use
   * oggdemux or filesrc ! decoder */

  if (GST_BASE_VIDEO_CODEC (base_video_decoder)->segment.format ==
      GST_FORMAT_UNDEFINED) {
    GstEvent *event;
    GstFlowReturn ret;

    GST_WARNING_OBJECT (base_video_decoder,
        "Received buffer without a new-segment. "
        "Assuming timestamps start from 0.");

    gst_segment_set_newsegment_full (&GST_BASE_VIDEO_CODEC
        (base_video_decoder)->segment, FALSE, 1.0, 1.0, GST_FORMAT_TIME, 0,
        GST_CLOCK_TIME_NONE, 0);

    event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0,
        GST_CLOCK_TIME_NONE, 0);

    ret = gst_base_video_decoder_push_src_event (base_video_decoder, event);
    if (!ret) {
      GST_ERROR_OBJECT (base_video_decoder, "new segment event ret=%d", ret);
      ret = GST_FLOW_ERROR;
      goto done;
    }
  }

  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    gint64 ts, index;

    GST_DEBUG_OBJECT (base_video_decoder, "received DISCONT buffer");

    /* track present position */
    ts = base_video_decoder->timestamp_offset;
    index = base_video_decoder->field_index;

    gst_base_video_decoder_flush (base_video_decoder, FALSE);

    /* buffer may claim DISCONT loudly, if it can't tell us where we are now,
     * we'll stick to where we were ...
     * Particularly useful/needed for upstream BYTE based */
    if (GST_BASE_VIDEO_CODEC (base_video_decoder)->segment.rate > 0.0 &&
        !GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
      GST_DEBUG_OBJECT (base_video_decoder,
          "... but restoring previous ts tracking");
      base_video_decoder->timestamp_offset = ts;
      base_video_decoder->field_index = index & ~1;
    }
  }

  if (GST_BASE_VIDEO_CODEC (base_video_decoder)->segment.rate > 0.0)
    ret = gst_base_video_decoder_chain_forward (base_video_decoder, buf);
  else
    ret = gst_base_video_decoder_chain_reverse (base_video_decoder, buf);

done:
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
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
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (base_video_decoder_class->start) {
        base_video_decoder_class->start (base_video_decoder);
      }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (base_video_decoder_class->stop) {
        base_video_decoder_class->stop (base_video_decoder);
      }

      GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);
      gst_base_video_decoder_reset (base_video_decoder, TRUE);
      g_list_foreach (base_video_decoder->current_frame_events,
          (GFunc) gst_event_unref, NULL);
      g_list_free (base_video_decoder->current_frame_events);
      base_video_decoder->current_frame_events = NULL;
      GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
      break;
    default:
      break;
  }

  return ret;
}

static GstVideoFrame *
gst_base_video_decoder_new_frame (GstBaseVideoDecoder * base_video_decoder)
{
  GstVideoFrame *frame;

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);
  frame =
      gst_base_video_codec_new_frame (GST_BASE_VIDEO_CODEC
      (base_video_decoder));

  frame->decode_frame_number = frame->system_frame_number -
      base_video_decoder->reorder_depth;

  frame->decode_timestamp = GST_CLOCK_TIME_NONE;
  frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
  frame->presentation_duration = GST_CLOCK_TIME_NONE;
  frame->n_fields = 2;

  frame->events = base_video_decoder->current_frame_events;
  base_video_decoder->current_frame_events = NULL;

  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);

  return frame;
}

static void
gst_base_video_decoder_prepare_finish_frame (GstBaseVideoDecoder *
    base_video_decoder, GstVideoFrame * frame)
{
  GList *l, *events = NULL;

#ifndef GST_DISABLE_GST_DEBUG
  GST_LOG_OBJECT (base_video_decoder, "n %d in %d out %d",
      g_list_length (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames),
      gst_adapter_available (base_video_decoder->input_adapter),
      gst_adapter_available (base_video_decoder->output_adapter));
#endif

  GST_LOG_OBJECT (base_video_decoder,
      "finish frame sync=%d pts=%" GST_TIME_FORMAT, frame->is_sync_point,
      GST_TIME_ARGS (frame->presentation_timestamp));

  /* Push all pending events that arrived before this frame */
  for (l = base_video_decoder->base_video_codec.frames; l; l = l->next) {
    GstVideoFrame *tmp = l->data;

    if (tmp->events) {
      events = tmp->events;
      tmp->events = NULL;
    }

    if (tmp == frame)
      break;
  }

  for (l = g_list_last (events); l; l = l->prev) {
    GST_LOG_OBJECT (base_video_decoder, "pushing %s event",
        GST_EVENT_TYPE_NAME (l->data));
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder),
        l->data);
  }
  g_list_free (events);

  if (GST_CLOCK_TIME_IS_VALID (frame->presentation_timestamp)) {
    if (frame->presentation_timestamp != base_video_decoder->timestamp_offset) {
      GST_DEBUG_OBJECT (base_video_decoder,
          "sync timestamp %" GST_TIME_FORMAT " diff %" GST_TIME_FORMAT,
          GST_TIME_ARGS (frame->presentation_timestamp),
          GST_TIME_ARGS (frame->presentation_timestamp -
              GST_BASE_VIDEO_CODEC (base_video_decoder)->segment.start));
      base_video_decoder->timestamp_offset = frame->presentation_timestamp;
      base_video_decoder->field_index &= 1;
    } else {
      /* This case is for one initial timestamp and no others, e.g.,
       * filesrc ! decoder ! xvimagesink */
      GST_WARNING_OBJECT (base_video_decoder,
          "sync timestamp didn't change, ignoring");
      frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
    }
  } else {
    if (frame->is_sync_point) {
      GST_WARNING_OBJECT (base_video_decoder,
          "sync point doesn't have timestamp");
      if (!GST_CLOCK_TIME_IS_VALID (base_video_decoder->timestamp_offset)) {
        GST_WARNING_OBJECT (base_video_decoder,
            "No base timestamp.  Assuming frames start at segment start");
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
      GST_WARNING_OBJECT (base_video_decoder,
          "decreasing timestamp (%" GST_TIME_FORMAT " < %"
          GST_TIME_FORMAT ")", GST_TIME_ARGS (frame->presentation_timestamp),
          GST_TIME_ARGS (base_video_decoder->last_timestamp));
    }
  }
  base_video_decoder->last_timestamp = frame->presentation_timestamp;
}

static void
gst_base_video_decoder_do_finish_frame (GstBaseVideoDecoder * dec,
    GstVideoFrame * frame)
{
  GST_BASE_VIDEO_CODEC (dec)->frames =
      g_list_remove (GST_BASE_VIDEO_CODEC (dec)->frames, frame);

  if (frame->src_buffer)
    gst_buffer_unref (frame->src_buffer);

  gst_base_video_codec_free_frame (frame);
}

/**
 * gst_base_video_decoder_drop_frame:
 * @dec: a #GstBaseVideoDecoder
 * @frame: the #GstVideoFrame to drop
 *
 * Similar to gst_base_video_decoder_finish_frame(), but drops @frame in any
 * case and posts a QoS message with the frame's details on the bus.
 * In any case, the frame is considered finished and released.
 *
 * Returns: a #GstFlowReturn, usually GST_FLOW_OK.
 *
 * Since: 0.10.23
 */
GstFlowReturn
gst_base_video_decoder_drop_frame (GstBaseVideoDecoder * dec,
    GstVideoFrame * frame)
{
  GstClockTime stream_time, jitter, earliest_time, qostime, timestamp;
  GstSegment *segment;
  GstMessage *qos_msg;
  gdouble proportion;

  GST_LOG_OBJECT (dec, "drop frame");

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (dec);

  gst_base_video_decoder_prepare_finish_frame (dec, frame);

  GST_DEBUG_OBJECT (dec, "dropping frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->presentation_timestamp));

  dec->dropped++;

  /* post QoS message */
  timestamp = frame->presentation_timestamp;
  proportion = GST_BASE_VIDEO_CODEC (dec)->proportion;
  segment = &GST_BASE_VIDEO_CODEC (dec)->segment;
  stream_time =
      gst_segment_to_stream_time (segment, GST_FORMAT_TIME, timestamp);
  qostime = gst_segment_to_running_time (segment, GST_FORMAT_TIME, timestamp);
  earliest_time = GST_BASE_VIDEO_CODEC (dec)->earliest_time;
  jitter = GST_CLOCK_DIFF (qostime, earliest_time);
  qos_msg = gst_message_new_qos (GST_OBJECT_CAST (dec), FALSE,
      qostime, stream_time, timestamp, GST_CLOCK_TIME_NONE);
  gst_message_set_qos_values (qos_msg, jitter, proportion, 1000000);
  gst_message_set_qos_stats (qos_msg, GST_FORMAT_BUFFERS,
      dec->processed, dec->dropped);
  gst_element_post_message (GST_ELEMENT_CAST (dec), qos_msg);

  /* now free the frame */
  gst_base_video_decoder_do_finish_frame (dec, frame);

  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (dec);

  return GST_FLOW_OK;
}

/**
 * gst_base_video_decoder_finish_frame:
 * @base_video_decoder: a #GstBaseVideoDecoder
 * @frame: a decoded #GstVideoFrame
 *
 * @frame should have a valid decoded data buffer, whose metadata fields
 * are then appropriately set according to frame data and pushed downstream.
 * If no output data is provided, @frame is considered skipped.
 * In any case, the frame is considered finished and released.
 *
 * Returns: a #GstFlowReturn resulting from sending data downstream
 */
GstFlowReturn
gst_base_video_decoder_finish_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;
  GstBuffer *src_buffer;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (base_video_decoder, "finish frame");
  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);

  gst_base_video_decoder_prepare_finish_frame (base_video_decoder, frame);

  base_video_decoder->processed++;

  /* no buffer data means this frame is skipped */
  if (!frame->src_buffer) {
    GST_DEBUG_OBJECT (base_video_decoder, "skipping frame %" GST_TIME_FORMAT,
        GST_TIME_ARGS (frame->presentation_timestamp));
    goto done;
  }

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
  if (GST_BASE_VIDEO_CODEC (base_video_decoder)->discont) {
    GST_BUFFER_FLAG_SET (src_buffer, GST_BUFFER_FLAG_DISCONT);
    GST_BASE_VIDEO_CODEC (base_video_decoder)->discont = FALSE;
  }

  GST_BUFFER_TIMESTAMP (src_buffer) = frame->presentation_timestamp;
  GST_BUFFER_DURATION (src_buffer) = frame->presentation_duration;
  GST_BUFFER_OFFSET (src_buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (src_buffer) = GST_BUFFER_OFFSET_NONE;

  /* update rate estimate */
  GST_BASE_VIDEO_CODEC (base_video_decoder)->bytes +=
      GST_BUFFER_SIZE (src_buffer);
  if (GST_CLOCK_TIME_IS_VALID (frame->presentation_duration)) {
    GST_BASE_VIDEO_CODEC (base_video_decoder)->time +=
        frame->presentation_duration;
  } else {
    /* better none than nothing valid */
    GST_BASE_VIDEO_CODEC (base_video_decoder)->time = GST_CLOCK_TIME_NONE;
  }

  gst_buffer_set_caps (src_buffer,
      GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder)));

  GST_LOG_OBJECT (base_video_decoder, "pushing frame ts %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (src_buffer)));

  if (base_video_decoder->sink_clipping) {
    gint64 start = GST_BUFFER_TIMESTAMP (src_buffer);
    gint64 stop = GST_BUFFER_TIMESTAMP (src_buffer) +
        GST_BUFFER_DURATION (src_buffer);
    GstSegment *segment = &GST_BASE_VIDEO_CODEC (base_video_decoder)->segment;

    if (gst_segment_clip (segment, GST_FORMAT_TIME, start, stop, &start, &stop)) {
      GST_BUFFER_TIMESTAMP (src_buffer) = start;
      GST_BUFFER_DURATION (src_buffer) = stop - start;
      GST_LOG_OBJECT (base_video_decoder,
          "accepting buffer inside segment: %" GST_TIME_FORMAT
          " %" GST_TIME_FORMAT
          " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
          " time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer)),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer) +
              GST_BUFFER_DURATION (src_buffer)),
          GST_TIME_ARGS (segment->start),
          GST_TIME_ARGS (segment->stop), GST_TIME_ARGS (segment->time));
    } else {
      GST_LOG_OBJECT (base_video_decoder,
          "dropping buffer outside segment: %" GST_TIME_FORMAT
          " %" GST_TIME_FORMAT
          " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
          " time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer)),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (src_buffer) +
              GST_BUFFER_DURATION (src_buffer)),
          GST_TIME_ARGS (segment->start),
          GST_TIME_ARGS (segment->stop), GST_TIME_ARGS (segment->time));
      gst_buffer_unref (src_buffer);
      ret = GST_FLOW_OK;
      goto done;
    }
  }

  /* we got data, so note things are looking up again */
  if (G_UNLIKELY (base_video_decoder->error_count))
    base_video_decoder->error_count--;

  if (GST_BASE_VIDEO_CODEC (base_video_decoder)->segment.rate < 0.0) {
    GST_LOG_OBJECT (base_video_decoder, "queued buffer");
    base_video_decoder->queued =
        g_list_prepend (base_video_decoder->queued, src_buffer);
  } else {
    ret = gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder),
        src_buffer);
  }

done:

  gst_base_video_decoder_do_finish_frame (base_video_decoder, frame);

  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);

  return ret;
}

/**
 * gst_base_video_decoder_finish_frame:
 * @base_video_decoder: a #GstBaseVideoDecoder
 * @n_bytes: an encoded #GstVideoFrame
 *
 * Removes next @n_bytes of input data and adds it to currently parsed frame.
 */
void
gst_base_video_decoder_add_to_frame (GstBaseVideoDecoder * base_video_decoder,
    int n_bytes)
{
  GstBuffer *buf;

  GST_LOG_OBJECT (base_video_decoder, "add %d bytes to frame", n_bytes);

  if (n_bytes == 0)
    return;

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);
  if (gst_adapter_available (base_video_decoder->output_adapter) == 0) {
    base_video_decoder->frame_offset = base_video_decoder->input_offset -
        gst_adapter_available (base_video_decoder->input_adapter);
  }
  buf = gst_adapter_take_buffer (base_video_decoder->input_adapter, n_bytes);

  gst_adapter_push (base_video_decoder->output_adapter, buf);
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
}

static guint64
gst_base_video_decoder_get_timestamp (GstBaseVideoDecoder * base_video_decoder,
    int picture_number)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;

  if (state->fps_d == 0 || state->fps_n == 0) {
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

  if (state->fps_d == 0 || state->fps_n == 0) {
    return GST_CLOCK_TIME_NONE;
  }
  if (field_offset < 0) {
    GST_WARNING_OBJECT (base_video_decoder, "field offset < 0");
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

  if (state->fps_d == 0 || state->fps_n == 0) {
    return GST_CLOCK_TIME_NONE;
  }
  if (n_fields < 0) {
    GST_WARNING_OBJECT (base_video_decoder, "n_fields < 0");
    return GST_CLOCK_TIME_NONE;
  }
  return gst_util_uint64_scale (n_fields, state->fps_d * GST_SECOND,
      state->fps_n * 2);
}

/**
 * gst_base_video_decoder_have_frame:
 * @base_video_decoder: a #GstBaseVideoDecoder
 *
 * Gathers all data collected for currently parsed frame, gathers corresponding
 * metadata and passes it along for further processing, i.e. @handle_frame.
 *
 * Returns: a #GstFlowReturn
 */
GstFlowReturn
gst_base_video_decoder_have_frame (GstBaseVideoDecoder * base_video_decoder)
{
  GstBuffer *buffer;
  int n_available;
  GstClockTime timestamp;
  GstClockTime duration;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (base_video_decoder, "have_frame");

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);

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

  GST_LOG_OBJECT (base_video_decoder, "collected frame size %d, "
      "ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT,
      n_available, GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration));

  ret = gst_base_video_decoder_have_frame_2 (base_video_decoder);

  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);

  return ret;
}

static GstFlowReturn
gst_base_video_decoder_have_frame_2 (GstBaseVideoDecoder * base_video_decoder)
{
  GstVideoFrame *frame = base_video_decoder->current_frame;
  GstBaseVideoDecoderClass *base_video_decoder_class;
  GstFlowReturn ret = GST_FLOW_OK;

  base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_GET_CLASS (base_video_decoder);

  g_return_val_if_fail (base_video_decoder_class->handle_frame != NULL,
      GST_FLOW_ERROR);

  /* capture frames and queue for later processing */
  if (GST_BASE_VIDEO_CODEC (base_video_decoder)->segment.rate < 0.0 &&
      !base_video_decoder->process) {
    base_video_decoder->parse_gather =
        g_list_prepend (base_video_decoder->parse_gather, frame);
    goto exit;
  }

  frame->distance_from_sync = base_video_decoder->distance_from_sync;
  base_video_decoder->distance_from_sync++;

  frame->presentation_timestamp = GST_BUFFER_TIMESTAMP (frame->sink_buffer);
  frame->presentation_duration = GST_BUFFER_DURATION (frame->sink_buffer);

  GST_LOG_OBJECT (base_video_decoder, "pts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->presentation_timestamp));
  GST_LOG_OBJECT (base_video_decoder, "dts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->decode_timestamp));
  GST_LOG_OBJECT (base_video_decoder, "dist %d", frame->distance_from_sync);

  GST_BASE_VIDEO_CODEC (base_video_decoder)->frames =
      g_list_append (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames, frame);

  frame->deadline =
      gst_segment_to_running_time (&GST_BASE_VIDEO_CODEC
      (base_video_decoder)->segment, GST_FORMAT_TIME,
      frame->presentation_timestamp);

  /* do something with frame */
  ret = base_video_decoder_class->handle_frame (base_video_decoder, frame);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (base_video_decoder, "flow error %s",
        gst_flow_get_name (ret));
  }

exit:
  /* create new frame */
  base_video_decoder->current_frame =
      gst_base_video_decoder_new_frame (base_video_decoder);

  return ret;
}

/**
 * gst_base_video_decoder_get_state:
 * @base_video_decoder: a #GstBaseVideoDecoder
 *
 * Returns: #GstVideoState describing format of video data.
 */
GstVideoState *
gst_base_video_decoder_get_state (GstBaseVideoDecoder * base_video_decoder)
{
  return &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;
}

/**
 * gst_base_video_decoder_lost_sync:
 * @base_video_decoder: a #GstBaseVideoDecoder
 *
 * Advances out-of-sync input data by 1 byte and marks it accordingly.
 */
void
gst_base_video_decoder_lost_sync (GstBaseVideoDecoder * base_video_decoder)
{
  g_return_if_fail (GST_IS_BASE_VIDEO_DECODER (base_video_decoder));

  GST_DEBUG_OBJECT (base_video_decoder, "lost_sync");

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);

  if (gst_adapter_available (base_video_decoder->input_adapter) >= 1) {
    gst_adapter_flush (base_video_decoder->input_adapter, 1);
  }

  base_video_decoder->have_sync = FALSE;
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
}

/* FIXME not quite exciting; get rid of this ? */
/**
 * gst_base_video_decoder_set_sync_point:
 * @base_video_decoder: a #GstBaseVideoDecoder
 *
 * Marks current frame as a sync point, i.e. keyframe.
 */
void
gst_base_video_decoder_set_sync_point (GstBaseVideoDecoder * base_video_decoder)
{
  GST_DEBUG_OBJECT (base_video_decoder, "set_sync_point");

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);
  base_video_decoder->current_frame->is_sync_point = TRUE;
  base_video_decoder->distance_from_sync = 0;
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
}

/**
 * gst_base_video_decoder_get_oldest_frame:
 * @base_video_decoder: a #GstBaseVideoDecoder
 *
 * Returns: oldest pending unfinished #GstVideoFrame.
 */
GstVideoFrame *
gst_base_video_decoder_get_oldest_frame (GstBaseVideoDecoder *
    base_video_decoder)
{
  GList *g;

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);
  g = g_list_first (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames);
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);

  if (g == NULL)
    return NULL;
  return (GstVideoFrame *) (g->data);
}

/**
 * gst_base_video_decoder_get_frame:
 * @base_video_decoder: a #GstBaseVideoDecoder
 * @frame_number: system_frame_number of a frame
 *
 * Returns: pending unfinished #GstVideoFrame identified by @frame_number.
 */
GstVideoFrame *
gst_base_video_decoder_get_frame (GstBaseVideoDecoder * base_video_decoder,
    int frame_number)
{
  GList *g;
  GstVideoFrame *frame = NULL;

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);
  for (g = g_list_first (GST_BASE_VIDEO_CODEC (base_video_decoder)->frames);
      g; g = g_list_next (g)) {
    GstVideoFrame *tmp = g->data;

    if (frame->system_frame_number == frame_number) {
      frame = tmp;
      break;
    }
  }
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);

  return frame;
}

/**
 * gst_base_video_decoder_set_src_caps:
 * @base_video_decoder: a #GstBaseVideoDecoder
 *
 * Sets src pad caps according to currently configured #GstVideoState.
 *
 */
gboolean
gst_base_video_decoder_set_src_caps (GstBaseVideoDecoder * base_video_decoder)
{
  GstCaps *caps;
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;
  gboolean ret;

  /* minimum sense */
  g_return_val_if_fail (state->format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (state->width != 0, FALSE);
  g_return_val_if_fail (state->height != 0, FALSE);

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);

  /* sanitize */
  if (state->fps_n == 0 || state->fps_d == 0) {
    state->fps_n = 0;
    state->fps_d = 1;
  }
  if (state->par_n == 0 || state->par_d == 0) {
    state->par_n = 1;
    state->par_d = 1;
  }

  caps = gst_video_format_new_caps (state->format,
      state->width, state->height,
      state->fps_n, state->fps_d, state->par_n, state->par_d);
  gst_caps_set_simple (caps, "interlaced",
      G_TYPE_BOOLEAN, state->interlaced, NULL);

  GST_DEBUG_OBJECT (base_video_decoder, "setting caps %" GST_PTR_FORMAT, caps);

  ret =
      gst_pad_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder),
      caps);
  gst_caps_unref (caps);

  /* arrange for derived info */
  state->bytes_per_picture =
      gst_video_format_get_size (state->format, state->width, state->height);

  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);

  return ret;
}

/**
 * gst_base_video_decoder_alloc_src_buffer:
 * @base_video_decoder: a #GstBaseVideoDecoder
 *
 * Helper function that uses gst_pad_alloc_buffer_and_set_caps
 * to allocate a buffer to hold a video frame for @base_video_decoder's
 * current #GstVideoState.
 *
 * Returns: allocated buffer
 */
GstBuffer *
gst_base_video_decoder_alloc_src_buffer (GstBaseVideoDecoder *
    base_video_decoder)
{
  GstBuffer *buffer;
  GstFlowReturn flow_ret;
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;
  int num_bytes = state->bytes_per_picture;

  GST_DEBUG ("alloc src buffer caps=%" GST_PTR_FORMAT,
      GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder)));

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);

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

  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);
  return buffer;
}

/**
 * gst_base_video_decoder_alloc_src_frame:
 * @base_video_decoder: a #GstBaseVideoDecoder
 * @frame: a #GstVideoFrame
 *
 * Helper function that uses gst_pad_alloc_buffer_and_set_caps
 * to allocate a buffer to hold a video frame for @base_video_decoder's
 * current #GstVideoState.  Subclass should already have configured video state
 * and set src pad caps.
 *
 * Returns: result from pad alloc call
 */
GstFlowReturn
gst_base_video_decoder_alloc_src_frame (GstBaseVideoDecoder *
    base_video_decoder, GstVideoFrame * frame)
{
  GstFlowReturn flow_ret;
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (base_video_decoder)->state;
  int num_bytes = state->bytes_per_picture;

  g_return_val_if_fail (state->bytes_per_picture != 0, GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD
          (base_video_decoder)) != NULL, GST_FLOW_ERROR);

  GST_LOG_OBJECT (base_video_decoder, "alloc buffer size %d", num_bytes);
  GST_BASE_VIDEO_CODEC_STREAM_LOCK (base_video_decoder);

  flow_ret =
      gst_pad_alloc_buffer_and_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD
      (base_video_decoder), GST_BUFFER_OFFSET_NONE, num_bytes,
      GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (base_video_decoder)),
      &frame->src_buffer);

  if (flow_ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (base_video_decoder, "failed to get buffer %s",
        gst_flow_get_name (flow_ret));
  }

  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (base_video_decoder);

  return flow_ret;
}

/**
 * gst_base_video_decoder_get_max_decode_time:
 * @base_video_decoder: a #GstBaseVideoDecoder
 * @frame: a #GstVideoFrame
 *
 * Determines maximum possible decoding time for @frame that will
 * allow it to decode and arrive in time (as determined by QoS events).
 * In particular, a negative result means decoding in time is no longer possible
 * and should therefore occur as soon/skippy as possible.
 *
 * Returns: max decoding time.
 */
GstClockTimeDiff
gst_base_video_decoder_get_max_decode_time (GstBaseVideoDecoder *
    base_video_decoder, GstVideoFrame * frame)
{
  GstClockTimeDiff deadline;
  GstClockTime earliest_time;

  GST_OBJECT_LOCK (base_video_decoder);
  earliest_time = GST_BASE_VIDEO_CODEC (base_video_decoder)->earliest_time;
  if (GST_CLOCK_TIME_IS_VALID (earliest_time))
    deadline = GST_CLOCK_DIFF (earliest_time, frame->deadline);
  else
    deadline = G_MAXINT64;

  GST_LOG_OBJECT (base_video_decoder, "earliest %" GST_TIME_FORMAT
      ", frame deadline %" GST_TIME_FORMAT ", deadline %" GST_TIME_FORMAT,
      GST_TIME_ARGS (earliest_time), GST_TIME_ARGS (frame->deadline),
      GST_TIME_ARGS (deadline));

  GST_OBJECT_UNLOCK (base_video_decoder);

  return deadline;
}

/**
 * gst_base_video_decoder_get_oldest_frame:
 * @base_video_decoder_class: a #GstBaseVideoDecoderClass
 *
 * Sets the mask and pattern that will be scanned for to obtain parse sync.
 * Note that a non-zero @mask implies that @scan_for_sync will be ignored.
 *
 */
void
gst_base_video_decoder_class_set_capture_pattern (GstBaseVideoDecoderClass *
    base_video_decoder_class, guint32 mask, guint32 pattern)
{
  g_return_if_fail (((~mask) & pattern) == 0);

  GST_DEBUG ("capture mask %08x, pattern %08x", mask, pattern);

  base_video_decoder_class->capture_mask = mask;
  base_video_decoder_class->capture_pattern = pattern;
}

GstFlowReturn
_gst_base_video_decoder_error (GstBaseVideoDecoder * dec, gint weight,
    GQuark domain, gint code, gchar * txt, gchar * dbg, const gchar * file,
    const gchar * function, gint line)
{
  if (txt)
    GST_WARNING_OBJECT (dec, "error: %s", txt);
  if (dbg)
    GST_WARNING_OBJECT (dec, "error: %s", dbg);
  dec->error_count += weight;
  GST_BASE_VIDEO_CODEC (dec)->discont = TRUE;
  if (dec->max_errors < dec->error_count) {
    gst_element_message_full (GST_ELEMENT (dec), GST_MESSAGE_ERROR,
        domain, code, txt, dbg, file, function, line);
    return GST_FLOW_ERROR;
  } else {
    return GST_FLOW_OK;
  }
}
