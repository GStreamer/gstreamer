/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
 * Copyright (C) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>.
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
 * Copyright (C) 2012 Collabora Ltd.
 *	Author : Edward Hervey <edward@collabora.com>
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
 * SECTION:gstvideodecoder
 * @short_description: Base class for video decoders
 * @see_also: 
 *
 * This base class is for video decoders turning encoded data into raw video
 * frames.
 *
 * GstVideoDecoder and subclass should cooperate as follows.
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Configuration</title>
 *   <listitem><para>
 *     Initially, GstVideoDecoder calls @start when the decoder element
 *     is activated, which allows subclass to perform any global setup.
 *   </para></listitem>
 *   <listitem><para>
 *     GstVideoDecoder calls @set_format to inform subclass of caps
 *     describing input video data that it is about to receive, including
 *     possibly configuration data.
 *     While unlikely, it might be called more than once, if changing input
 *     parameters require reconfiguration.
 *   </para></listitem>
 *   <listitem><para>
 *     GstVideoDecoder calls @stop at end of all processing.
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
 *       Input frame is provided to subclass' @handle_frame. The ownership of
 *       the frame is given to @handle_frame.
 *     </para></listitem>
 *     <listitem><para>
 *       If codec processing results in decoded data, subclass should call
 *       @gst_video_decoder_finish_frame to have decoded data pushed.
 *       downstream.
 *     </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Shutdown phase</title>
 *   <listitem><para>
 *     GstVideoDecoder class calls @stop to inform the subclass that data
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
 * it might be delayed until calling @gst_video_decoder_finish_frame.
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
 *      Data will be provided to @parse which should invoke @gst_video_decoder_add_to_frame and
 *      @gst_video_decoder_have_frame as appropriate.
 *   </para></listitem>
 *   <listitem><para>
 *      Accept data in @handle_frame and provide decoded results to
 *      @gst_video_decoder_finish_frame.
 *   </para></listitem>
 * </itemizedlist>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* TODO
 *
 * * Add a flag/boolean for I-frame-only/image decoders so we can do extra
 *   features, like applying QoS on input (as opposed to after the frame is
 *   decoded).
 * * Add a flag/boolean for decoders that require keyframes, so the base
 *   class can automatically discard non-keyframes before one has arrived
 * * Detect reordered frame/timestamps and fix the pts/dts
 * * Support for GstIndex (or shall we not care ?)
 * * Calculate actual latency based on input/output timestamp/frame_number
 *   and if it exceeds the recorded one, save it and emit a GST_MESSAGE_LATENCY
 * * Emit latency message when it changes
 *
 */

#include "gstvideodecoder.h"
#include "gstvideoutils.h"

#include <gst/video/gstvideopool.h>
#include <gst/video/gstvideometa.h>
#include <string.h>

GST_DEBUG_CATEGORY (videodecoder_debug);
#define GST_CAT_DEFAULT videodecoder_debug

#define GST_VIDEO_DECODER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_VIDEO_DECODER, \
        GstVideoDecoderPrivate))

struct _GstVideoDecoderPrivate
{
  /* FIXME introduce a context ? */

  GstBufferPool *pool;
  GstAllocator *allocator;
  GstAllocationParams params;

  /* parse tracking */
  /* input data */
  GstAdapter *input_adapter;
  /* assembles current frame */
  GstAdapter *output_adapter;

  /* Whether we attempt to convert newsegment from bytes to
   * time using a bitrate estimation */
  gboolean do_estimate_rate;

  /* Whether input is considered packetized or not */
  gboolean packetized;

  /* Error handling */
  gint max_errors;
  gint error_count;

  /* ... being tracked here;
   * only available during parsing */
  GstVideoCodecFrame *current_frame;
  /* events that should apply to the current frame */
  GList *current_frame_events;

  /* relative offset of input data */
  guint64 input_offset;
  /* relative offset of frame */
  guint64 frame_offset;
  /* tracking ts and offsets */
  GList *timestamps;

  /* combine to yield (presentation) ts */
  GstClockTime timestamp_offset;

  /* last outgoing ts */
  GstClockTime last_timestamp;

  /* reverse playback */
  /* collect input */
  GList *gather;
  /* to-be-parsed */
  GList *parse;
  /* collected parsed frames */
  GList *parse_gather;
  /* frames to be handled == decoded */
  GList *decode;
  /* collected output */
  GList *queued;
  /* Used internally for avoiding processing of frames to flush */
  gboolean process;


  /* FIXME : base_picture_number is never set */
  guint64 base_picture_number;
  /* FIXME : reorder_depth is never set */
  int reorder_depth;
  int distance_from_sync;

  guint64 system_frame_number;
  guint64 decode_frame_number;

  GList *frames;                /* Protected with OBJECT_LOCK */
  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;
  gboolean output_state_changed;

  /* QoS properties */
  gdouble proportion;
  GstClockTime earliest_time;
  gboolean discont;
  /* qos messages: frames dropped/processed */
  guint dropped;
  guint processed;

  /* Outgoing byte size ? */
  gint64 bytes_out;
  gint64 time;

  gint64 min_latency;
  gint64 max_latency;
};

static GstElementClass *parent_class = NULL;
static void gst_video_decoder_class_init (GstVideoDecoderClass * klass);
static void gst_video_decoder_init (GstVideoDecoder * dec,
    GstVideoDecoderClass * klass);

static void gst_video_decoder_finalize (GObject * object);

static gboolean gst_video_decoder_setcaps (GstVideoDecoder * dec,
    GstCaps * caps);
static gboolean gst_video_decoder_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_video_decoder_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_video_decoder_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean gst_video_decoder_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstStateChangeReturn gst_video_decoder_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_video_decoder_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static void gst_video_decoder_reset (GstVideoDecoder * decoder, gboolean full);

static GstFlowReturn gst_video_decoder_have_frame_2 (GstVideoDecoder * decoder);
static gboolean gst_video_decoder_set_src_caps (GstVideoDecoder * decoder);

static guint64
gst_video_decoder_get_timestamp (GstVideoDecoder * decoder, int picture_number);
static guint64 gst_video_decoder_get_frame_duration (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstVideoCodecFrame *gst_video_decoder_new_frame (GstVideoDecoder *
    decoder);

static void gst_video_decoder_clear_queues (GstVideoDecoder * dec);

static gboolean gst_video_decoder_sink_event_default (GstVideoDecoder * decoder,
    GstEvent * event);
static gboolean gst_video_decoder_src_event_default (GstVideoDecoder * decoder,
    GstEvent * event);
static gboolean gst_video_decoder_decide_allocation_default (GstVideoDecoder *
    decoder, GstQuery * query);

/* we can't use G_DEFINE_ABSTRACT_TYPE because we need the klass in the _init
 * method to get to the padtemplates */
GType
gst_video_decoder_get_type (void)
{
  static volatile gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (GstVideoDecoderClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_video_decoder_class_init,
      NULL,
      NULL,
      sizeof (GstVideoDecoder),
      0,
      (GInstanceInitFunc) gst_video_decoder_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstVideoDecoder", &info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static void
gst_video_decoder_class_init (GstVideoDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (videodecoder_debug, "videodecoder", 0,
      "Base Video Decoder");

  parent_class = g_type_class_peek_parent (klass);
  g_type_class_add_private (klass, sizeof (GstVideoDecoderPrivate));

  gobject_class->finalize = gst_video_decoder_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_video_decoder_change_state);

  klass->sink_event = gst_video_decoder_sink_event_default;
  klass->src_event = gst_video_decoder_src_event_default;
  klass->decide_allocation = gst_video_decoder_decide_allocation_default;
}

static void
gst_video_decoder_init (GstVideoDecoder * decoder, GstVideoDecoderClass * klass)
{
  GstPadTemplate *pad_template;
  GstPad *pad;

  GST_DEBUG_OBJECT (decoder, "gst_video_decoder_init");

  decoder->priv = GST_VIDEO_DECODER_GET_PRIVATE (decoder);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  decoder->sinkpad = pad = gst_pad_new_from_template (pad_template, "sink");

  gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_video_decoder_chain));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_decoder_sink_event));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_decoder_sink_query));
  gst_element_add_pad (GST_ELEMENT (decoder), decoder->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  decoder->srcpad = pad = gst_pad_new_from_template (pad_template, "src");

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_decoder_src_event));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_decoder_src_query));
  gst_pad_use_fixed_caps (pad);
  gst_element_add_pad (GST_ELEMENT (decoder), decoder->srcpad);

  gst_segment_init (&decoder->input_segment, GST_FORMAT_TIME);
  gst_segment_init (&decoder->output_segment, GST_FORMAT_TIME);

  g_rec_mutex_init (&decoder->stream_lock);

  decoder->priv->input_adapter = gst_adapter_new ();
  decoder->priv->output_adapter = gst_adapter_new ();
  decoder->priv->packetized = TRUE;

  gst_video_decoder_reset (decoder, TRUE);
}

static gboolean
gst_video_rawvideo_convert (GstVideoCodecState * state,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;
  guint vidsize;
  guint fps_n, fps_d;

  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  if (src_format == *dest_format || src_value == 0 || src_value == -1) {
    *dest_value = src_value;
    return TRUE;
  }

  vidsize = GST_VIDEO_INFO_SIZE (&state->info);
  fps_n = GST_VIDEO_INFO_FPS_N (&state->info);
  fps_d = GST_VIDEO_INFO_FPS_D (&state->info);

  if (src_format == GST_FORMAT_BYTES &&
      *dest_format == GST_FORMAT_DEFAULT && vidsize) {
    /* convert bytes to frames */
    *dest_value = gst_util_uint64_scale_int (src_value, 1, vidsize);
    res = TRUE;
  } else if (src_format == GST_FORMAT_DEFAULT &&
      *dest_format == GST_FORMAT_BYTES && vidsize) {
    /* convert bytes to frames */
    *dest_value = src_value * vidsize;
    res = TRUE;
  } else if (src_format == GST_FORMAT_DEFAULT &&
      *dest_format == GST_FORMAT_TIME && fps_n) {
    /* convert frames to time */
    *dest_value = gst_util_uint64_scale (src_value, GST_SECOND * fps_d, fps_n);
    res = TRUE;
  } else if (src_format == GST_FORMAT_TIME &&
      *dest_format == GST_FORMAT_DEFAULT && fps_d) {
    /* convert time to frames */
    *dest_value = gst_util_uint64_scale (src_value, fps_n, GST_SECOND * fps_d);
    res = TRUE;
  } else if (src_format == GST_FORMAT_TIME &&
      *dest_format == GST_FORMAT_BYTES && fps_d && vidsize) {
    /* convert time to frames */
    *dest_value = gst_util_uint64_scale (src_value,
        fps_n * vidsize, GST_SECOND * fps_d);
    res = TRUE;
  } else if (src_format == GST_FORMAT_BYTES &&
      *dest_format == GST_FORMAT_TIME && fps_n && vidsize) {
    /* convert frames to time */
    *dest_value = gst_util_uint64_scale (src_value,
        GST_SECOND * fps_d, fps_n * vidsize);
    res = TRUE;
  }

  return res;
}

static gboolean
gst_video_encoded_video_convert (gint64 bytes, gint64 time,
    GstFormat src_format, gint64 src_value, GstFormat * dest_format,
    gint64 * dest_value)
{
  gboolean res = FALSE;

  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  if (G_UNLIKELY (src_format == *dest_format || src_value == 0 ||
          src_value == -1)) {
    if (dest_value)
      *dest_value = src_value;
    return TRUE;
  }

  if (bytes <= 0 || time <= 0) {
    GST_DEBUG ("not enough metadata yet to convert");
    goto exit;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (src_value, time, bytes);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = gst_util_uint64_scale (src_value, bytes, time);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      GST_DEBUG ("unhandled conversion from %d to %d", src_format,
          *dest_format);
      res = FALSE;
  }

exit:
  return res;
}

static GstVideoCodecState *
_new_input_state (GstCaps * caps)
{
  GstVideoCodecState *state;
  GstStructure *structure;
  const GValue *codec_data;

  state = g_slice_new0 (GstVideoCodecState);
  state->ref_count = 1;
  gst_video_info_init (&state->info);
  if (G_UNLIKELY (!gst_video_info_from_caps (&state->info, caps)))
    goto parse_fail;
  state->caps = gst_caps_ref (caps);

  structure = gst_caps_get_structure (caps, 0);

  codec_data = gst_structure_get_value (structure, "codec_data");
  if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER)
    state->codec_data = GST_BUFFER (g_value_dup_boxed (codec_data));

  return state;

parse_fail:
  {
    g_slice_free (GstVideoCodecState, state);
    return NULL;
  }
}

static GstVideoCodecState *
_new_output_state (GstVideoFormat fmt, guint width, guint height,
    GstVideoCodecState * reference)
{
  GstVideoCodecState *state;

  state = g_slice_new0 (GstVideoCodecState);
  state->ref_count = 1;
  gst_video_info_init (&state->info);
  gst_video_info_set_format (&state->info, fmt, width, height);

  if (reference) {
    GstVideoInfo *tgt, *ref;

    tgt = &state->info;
    ref = &reference->info;

    /* Copy over extra fields from reference state */
    tgt->interlace_mode = ref->interlace_mode;
    tgt->flags = ref->flags;
    tgt->chroma_site = ref->chroma_site;
    /* only copy values that are not unknown so that we don't override the
     * defaults. subclasses should really fill these in when they know. */
    if (ref->colorimetry.range)
      tgt->colorimetry.range = ref->colorimetry.range;
    if (ref->colorimetry.matrix)
      tgt->colorimetry.matrix = ref->colorimetry.matrix;
    if (ref->colorimetry.transfer)
      tgt->colorimetry.transfer = ref->colorimetry.transfer;
    if (ref->colorimetry.primaries)
      tgt->colorimetry.primaries = ref->colorimetry.primaries;
    GST_DEBUG ("reference par %d/%d fps %d/%d",
        ref->par_n, ref->par_d, ref->fps_n, ref->fps_d);
    tgt->par_n = ref->par_n;
    tgt->par_d = ref->par_d;
    tgt->fps_n = ref->fps_n;
    tgt->fps_d = ref->fps_d;
  }

  GST_DEBUG ("reference par %d/%d fps %d/%d",
      state->info.par_n, state->info.par_d,
      state->info.fps_n, state->info.fps_d);

  return state;
}

static gboolean
gst_video_decoder_setcaps (GstVideoDecoder * decoder, GstCaps * caps)
{
  GstVideoDecoderClass *decoder_class;
  GstVideoCodecState *state;
  gboolean ret = TRUE;

  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (decoder, "setcaps %" GST_PTR_FORMAT, caps);

  state = _new_input_state (caps);

  if (G_UNLIKELY (state == NULL))
    goto parse_fail;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (decoder_class->set_format)
    ret = decoder_class->set_format (decoder, state);

  if (!ret)
    goto refused_format;

  if (decoder->priv->input_state)
    gst_video_codec_state_unref (decoder->priv->input_state);
  decoder->priv->input_state = state;

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return ret;

  /* ERRORS */

parse_fail:
  {
    GST_WARNING_OBJECT (decoder, "Failed to parse caps");
    return FALSE;
  }

refused_format:
  {
    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    GST_WARNING_OBJECT (decoder, "Subclass refused caps");
    gst_video_codec_state_unref (state);
    return FALSE;
  }
}

static void
gst_video_decoder_finalize (GObject * object)
{
  GstVideoDecoder *decoder;

  decoder = GST_VIDEO_DECODER (object);

  GST_DEBUG_OBJECT (object, "finalize");

  g_rec_mutex_clear (&decoder->stream_lock);

  if (decoder->priv->input_adapter) {
    g_object_unref (decoder->priv->input_adapter);
    decoder->priv->input_adapter = NULL;
  }
  if (decoder->priv->output_adapter) {
    g_object_unref (decoder->priv->output_adapter);
    decoder->priv->output_adapter = NULL;
  }

  if (decoder->priv->input_state)
    gst_video_codec_state_unref (decoder->priv->input_state);
  if (decoder->priv->output_state)
    gst_video_codec_state_unref (decoder->priv->output_state);

  if (decoder->priv->pool) {
    gst_object_unref (decoder->priv->pool);
    decoder->priv->pool = NULL;
  }

  if (decoder->priv->allocator) {
    gst_allocator_unref (decoder->priv->allocator);
    decoder->priv->pool = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* hard == FLUSH, otherwise discont */
static GstFlowReturn
gst_video_decoder_flush (GstVideoDecoder * dec, gboolean hard)
{
  GstVideoDecoderClass *klass;
  GstVideoDecoderPrivate *priv = dec->priv;
  GstFlowReturn ret = GST_FLOW_OK;

  klass = GST_VIDEO_DECODER_GET_CLASS (dec);

  GST_LOG_OBJECT (dec, "flush hard %d", hard);

  /* Inform subclass */
  if (klass->reset)
    klass->reset (dec, hard);

  /* FIXME make some more distinction between hard and soft,
   * but subclass may not be prepared for that */
  /* FIXME perhaps also clear pending frames ?,
   * but again, subclass may still come up with one of those */
  if (!hard) {
    /* TODO ? finish/drain some stuff */
  } else {
    gst_segment_init (&dec->input_segment, GST_FORMAT_UNDEFINED);
    gst_segment_init (&dec->output_segment, GST_FORMAT_UNDEFINED);
    gst_video_decoder_clear_queues (dec);
    priv->error_count = 0;
    g_list_foreach (priv->current_frame_events, (GFunc) gst_event_unref, NULL);
    g_list_free (priv->current_frame_events);
    priv->current_frame_events = NULL;
  }
  /* and get (re)set for the sequel */
  gst_video_decoder_reset (dec, FALSE);

  return ret;
}


static gboolean
gst_video_decoder_push_event (GstVideoDecoder * decoder, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      GST_VIDEO_DECODER_STREAM_LOCK (decoder);

      gst_event_copy_segment (event, &segment);

      GST_DEBUG_OBJECT (decoder, "segment %" GST_SEGMENT_FORMAT, &segment);

      if (segment.format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (decoder, "received non TIME newsegment");
        GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
        break;
      }

      decoder->output_segment = segment;
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
      break;
    }
    default:
      break;
  }

  return gst_pad_push_event (decoder->srcpad, event);
}

static gboolean
gst_video_decoder_sink_event_default (GstVideoDecoder * decoder,
    GstEvent * event)
{
  GstVideoDecoderClass *decoder_class;
  GstVideoDecoderPrivate *priv;
  gboolean ret = FALSE;

  priv = decoder->priv;
  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_video_decoder_setcaps (decoder, caps);
      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_EOS:
    {
      GstFlowReturn flow_ret = GST_FLOW_OK;

      GST_VIDEO_DECODER_STREAM_LOCK (decoder);
      if (!priv->packetized)
        while (flow_ret == GST_FLOW_OK &&
            gst_adapter_available (priv->input_adapter))
          flow_ret =
              decoder_class->parse (decoder, priv->current_frame,
              priv->input_adapter, TRUE);

      if (decoder_class->finish) {
        flow_ret = decoder_class->finish (decoder);
      } else {
        flow_ret = GST_FLOW_OK;
      }

      ret = (flow_ret == GST_FLOW_OK);
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      GST_VIDEO_DECODER_STREAM_LOCK (decoder);

      gst_event_copy_segment (event, &segment);

      if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (decoder,
            "received TIME SEGMENT %" GST_SEGMENT_FORMAT, &segment);
      } else {
        gint64 start;

        GST_DEBUG_OBJECT (decoder,
            "received SEGMENT %" GST_SEGMENT_FORMAT, &segment);

        /* handle newsegment as a result from our legacy simple seeking */
        /* note that initial 0 should convert to 0 in any case */
        if (priv->do_estimate_rate &&
            gst_pad_query_convert (decoder->sinkpad, GST_FORMAT_BYTES,
                segment.start, GST_FORMAT_TIME, &start)) {
          /* best attempt convert */
          /* as these are only estimates, stop is kept open-ended to avoid
           * premature cutting */
          GST_DEBUG_OBJECT (decoder,
              "converted to TIME start %" GST_TIME_FORMAT,
              GST_TIME_ARGS (start));
          segment.start = start;
          segment.stop = GST_CLOCK_TIME_NONE;
          segment.time = start;
          /* replace event */
          gst_event_unref (event);
          event = gst_event_new_segment (&segment);
        } else {
          GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
          goto newseg_wrong_format;
        }
      }

      gst_video_decoder_flush (decoder, FALSE);

      priv->timestamp_offset = segment.start;

      decoder->input_segment = segment;

      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_VIDEO_DECODER_STREAM_LOCK (decoder);
      /* well, this is kind of worse than a DISCONT */
      gst_video_decoder_flush (decoder, TRUE);
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    }
    default:
      break;
  }

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
  if (event) {
    if (!GST_EVENT_IS_SERIALIZED (event)
        || GST_EVENT_TYPE (event) == GST_EVENT_EOS
        || GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
      ret = gst_video_decoder_push_event (decoder, event);
    } else {
      GST_VIDEO_DECODER_STREAM_LOCK (decoder);
      decoder->priv->current_frame_events =
          g_list_prepend (decoder->priv->current_frame_events, event);
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
      ret = TRUE;
    }
  }

  return ret;

newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (decoder, "received non TIME newsegment");
    gst_event_unref (event);
    /* SWALLOW EVENT */
    return TRUE;
  }
}

static gboolean
gst_video_decoder_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstVideoDecoder *decoder;
  GstVideoDecoderClass *decoder_class;
  gboolean ret = FALSE;

  decoder = GST_VIDEO_DECODER (parent);
  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (decoder, "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  if (decoder_class->sink_event)
    ret = decoder_class->sink_event (decoder, event);

  return ret;
}

/* perform upstream byte <-> time conversion (duration, seeking)
 * if subclass allows and if enough data for moderately decent conversion */
static inline gboolean
gst_video_decoder_do_byte (GstVideoDecoder * dec)
{
  return dec->priv->do_estimate_rate && (dec->priv->bytes_out > 0)
      && (dec->priv->time > GST_SECOND);
}

static gboolean
gst_video_decoder_do_seek (GstVideoDecoder * dec, GstEvent * event)
{
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, end_type;
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

  memcpy (&seek_segment, &dec->output_segment, sizeof (seek_segment));
  gst_segment_do_seek (&seek_segment, rate, format, flags, start_type,
      start_time, end_type, end_time, NULL);
  start_time = seek_segment.position;

  if (!gst_pad_query_convert (dec->sinkpad, GST_FORMAT_TIME, start_time,
          GST_FORMAT_BYTES, &start)) {
    GST_DEBUG_OBJECT (dec, "conversion failed");
    return FALSE;
  }

  seqnum = gst_event_get_seqnum (event);
  event = gst_event_new_seek (1.0, GST_FORMAT_BYTES, flags,
      GST_SEEK_TYPE_SET, start, GST_SEEK_TYPE_NONE, -1);
  gst_event_set_seqnum (event, seqnum);

  GST_DEBUG_OBJECT (dec, "seeking to %" GST_TIME_FORMAT " at byte offset %"
      G_GINT64_FORMAT, GST_TIME_ARGS (start_time), start);

  return gst_pad_push_event (dec->sinkpad, event);
}

static gboolean
gst_video_decoder_src_event_default (GstVideoDecoder * decoder,
    GstEvent * event)
{
  GstVideoDecoderPrivate *priv;
  gboolean res = FALSE;

  priv = decoder->priv;

  GST_DEBUG_OBJECT (decoder,
      "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format;
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
      if ((res = gst_pad_push_event (decoder->sinkpad, event)))
        break;

      /* if upstream fails for a time seek, maybe we can help if allowed */
      if (format == GST_FORMAT_TIME) {
        if (gst_video_decoder_do_byte (decoder))
          res = gst_video_decoder_do_seek (decoder, event);
        break;
      }

      /* ... though a non-time seek can be aided as well */
      /* First bring the requested format to time */
      if (!(res =
              gst_pad_query_convert (decoder->srcpad, format, cur,
                  GST_FORMAT_TIME, &tcur)))
        goto convert_error;
      if (!(res =
              gst_pad_query_convert (decoder->srcpad, format, stop,
                  GST_FORMAT_TIME, &tstop)))
        goto convert_error;

      /* then seek with time on the peer */
      event = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);
      gst_event_set_seqnum (event, seqnum);

      res = gst_pad_push_event (decoder->sinkpad, event);
      break;
    }
    case GST_EVENT_QOS:
    {
      GstQOSType type;
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      GstClockTime duration;

      gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);

      GST_OBJECT_LOCK (decoder);
      priv->proportion = proportion;
      if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (timestamp))) {
        if (G_UNLIKELY (diff > 0)) {
          if (priv->output_state->info.fps_n > 0)
            duration =
                gst_util_uint64_scale (GST_SECOND,
                priv->output_state->info.fps_d, priv->output_state->info.fps_n);
          else
            duration = 0;
          priv->earliest_time = timestamp + 2 * diff + duration;
        } else {
          priv->earliest_time = timestamp + diff;
        }
      } else {
        priv->earliest_time = GST_CLOCK_TIME_NONE;
      }
      GST_OBJECT_UNLOCK (decoder);

      GST_DEBUG_OBJECT (decoder,
          "got QoS %" GST_TIME_FORMAT ", %" G_GINT64_FORMAT ", %g",
          GST_TIME_ARGS (timestamp), diff, proportion);

      res = gst_pad_push_event (decoder->sinkpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (decoder->sinkpad, event);
      break;
  }
done:
  return res;

convert_error:
  GST_DEBUG_OBJECT (decoder, "could not convert format");
  goto done;
}

static gboolean
gst_video_decoder_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstVideoDecoder *decoder;
  GstVideoDecoderClass *decoder_class;
  gboolean ret = FALSE;

  decoder = GST_VIDEO_DECODER (parent);
  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (decoder, "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  if (decoder_class->src_event)
    ret = decoder_class->src_event (decoder, event);

  return ret;
}

static gboolean
gst_video_decoder_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstVideoDecoder *dec;
  gboolean res = TRUE;

  dec = GST_VIDEO_DECODER (parent);

  GST_LOG_OBJECT (dec, "handling query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time, value;

      /* upstream gets a chance first */
      if ((res = gst_pad_peer_query (dec->sinkpad, query))) {
        GST_LOG_OBJECT (dec, "returning peer response");
        break;
      }

      /* we start from the last seen time */
      time = dec->priv->last_timestamp;
      /* correct for the segment values */
      time = gst_segment_to_stream_time (&dec->output_segment,
          GST_FORMAT_TIME, time);

      GST_LOG_OBJECT (dec,
          "query %p: our time: %" GST_TIME_FORMAT, query, GST_TIME_ARGS (time));

      /* and convert to the final format */
      gst_query_parse_position (query, &format, NULL);
      if (!(res = gst_pad_query_convert (pad, GST_FORMAT_TIME, time,
                  format, &value)))
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
      if ((res = gst_pad_query_default (pad, parent, query)))
        break;

      gst_query_parse_duration (query, &format, NULL);
      /* try answering TIME by converting from BYTE if subclass allows  */
      if (format == GST_FORMAT_TIME && gst_video_decoder_do_byte (dec)) {
        gint64 value;

        if (gst_pad_peer_query_duration (dec->sinkpad, GST_FORMAT_BYTES,
                &value)) {
          GST_LOG_OBJECT (dec, "upstream size %" G_GINT64_FORMAT, value);
          if (gst_pad_query_convert (dec->sinkpad,
                  GST_FORMAT_BYTES, value, GST_FORMAT_TIME, &value)) {
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
      res = gst_video_rawvideo_convert (dec->priv->output_state,
          src_fmt, src_val, &dest_fmt, &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min_latency, max_latency;

      res = gst_pad_peer_query (dec->sinkpad, query);
      if (res) {
        gst_query_parse_latency (query, &live, &min_latency, &max_latency);
        GST_DEBUG_OBJECT (dec, "Peer latency: live %d, min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT, live,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        GST_OBJECT_LOCK (dec);
        min_latency += dec->priv->min_latency;
        if (dec->priv->max_latency == GST_CLOCK_TIME_NONE) {
          max_latency = GST_CLOCK_TIME_NONE;
        } else if (max_latency != GST_CLOCK_TIME_NONE) {
          max_latency += dec->priv->max_latency;
        }
        GST_OBJECT_UNLOCK (dec);

        gst_query_set_latency (query, live, min_latency, max_latency);
      }
    }
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
  }
  return res;

error:
  GST_ERROR_OBJECT (dec, "query failed");
  return res;
}

static gboolean
gst_video_decoder_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstVideoDecoder *decoder;
  GstVideoDecoderPrivate *priv;
  gboolean res = FALSE;

  decoder = GST_VIDEO_DECODER (parent);
  priv = decoder->priv;

  GST_LOG_OBJECT (decoder, "handling query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_video_encoded_video_convert (priv->bytes_out, priv->time, src_fmt,
          src_val, &dest_fmt, &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
done:

  return res;
error:
  GST_DEBUG_OBJECT (decoder, "query failed");
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
gst_video_decoder_add_timestamp (GstVideoDecoder * decoder, GstBuffer * buffer)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  Timestamp *ts;

  ts = g_malloc (sizeof (Timestamp));

  GST_LOG_OBJECT (decoder,
      "adding timestamp %" GST_TIME_FORMAT " (offset:%" G_GUINT64_FORMAT ")",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)), priv->input_offset);

  ts->offset = priv->input_offset;
  ts->timestamp = GST_BUFFER_TIMESTAMP (buffer);
  ts->duration = GST_BUFFER_DURATION (buffer);

  priv->timestamps = g_list_append (priv->timestamps, ts);
}

static void
gst_video_decoder_get_timestamp_at_offset (GstVideoDecoder *
    decoder, guint64 offset, GstClockTime * timestamp, GstClockTime * duration)
{
  Timestamp *ts;
  GList *g;

  *timestamp = GST_CLOCK_TIME_NONE;
  *duration = GST_CLOCK_TIME_NONE;

  g = decoder->priv->timestamps;
  while (g) {
    ts = g->data;
    if (ts->offset <= offset) {
      *timestamp = ts->timestamp;
      *duration = ts->duration;
      g_free (ts);
      g = g->next;
      decoder->priv->timestamps = g_list_remove (decoder->priv->timestamps, ts);
    } else {
      break;
    }
  }

  GST_LOG_OBJECT (decoder,
      "got timestamp %" GST_TIME_FORMAT " (offset:%" G_GUINT64_FORMAT ")",
      GST_TIME_ARGS (*timestamp), offset);
}

static void
gst_video_decoder_clear_queues (GstVideoDecoder * dec)
{
  GstVideoDecoderPrivate *priv = dec->priv;

  g_list_foreach (priv->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (priv->queued);
  priv->queued = NULL;
  g_list_foreach (priv->gather, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (priv->gather);
  priv->gather = NULL;
  g_list_foreach (priv->decode, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (priv->decode);
  priv->decode = NULL;
  g_list_foreach (priv->parse, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (priv->parse);
  priv->parse = NULL;
  g_list_foreach (priv->parse_gather, (GFunc) gst_video_codec_frame_unref,
      NULL);
  g_list_free (priv->parse_gather);
  priv->parse_gather = NULL;
  g_list_foreach (priv->frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (priv->frames);
  priv->frames = NULL;
}

static void
gst_video_decoder_reset (GstVideoDecoder * decoder, gboolean full)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GList *g;

  GST_DEBUG_OBJECT (decoder, "reset full %d", full);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (full) {
    gst_segment_init (&decoder->input_segment, GST_FORMAT_UNDEFINED);
    gst_segment_init (&decoder->output_segment, GST_FORMAT_UNDEFINED);
    gst_video_decoder_clear_queues (decoder);
    priv->error_count = 0;
    priv->max_errors = GST_VIDEO_DECODER_MAX_ERRORS;
    if (priv->input_state)
      gst_video_codec_state_unref (priv->input_state);
    priv->input_state = NULL;
    if (priv->output_state)
      gst_video_codec_state_unref (priv->output_state);
    priv->output_state = NULL;
    priv->min_latency = 0;
    priv->max_latency = 0;
  }

  priv->discont = TRUE;

  priv->timestamp_offset = GST_CLOCK_TIME_NONE;
  priv->last_timestamp = GST_CLOCK_TIME_NONE;

  priv->input_offset = 0;
  priv->frame_offset = 0;
  gst_adapter_clear (priv->input_adapter);
  gst_adapter_clear (priv->output_adapter);
  g_list_foreach (priv->timestamps, (GFunc) g_free, NULL);
  g_list_free (priv->timestamps);
  priv->timestamps = NULL;

  if (priv->current_frame) {
    gst_video_codec_frame_unref (priv->current_frame);
    priv->current_frame = NULL;
  }

  priv->dropped = 0;
  priv->processed = 0;

  priv->decode_frame_number = 0;
  priv->base_picture_number = 0;
  for (g = priv->frames; g; g = g->next) {
    gst_video_codec_frame_unref ((GstVideoCodecFrame *) g->data);
  }
  g_list_free (priv->frames);
  priv->frames = NULL;

  priv->bytes_out = 0;
  priv->time = 0;

  priv->earliest_time = GST_CLOCK_TIME_NONE;
  priv->proportion = 0.5;

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
}

static GstFlowReturn
gst_video_decoder_chain_forward (GstVideoDecoder * decoder, GstBuffer * buf)
{
  GstVideoDecoderPrivate *priv;
  GstVideoDecoderClass *klass;
  GstFlowReturn ret = GST_FLOW_OK;

  klass = GST_VIDEO_DECODER_GET_CLASS (decoder);
  priv = decoder->priv;

  g_return_val_if_fail (priv->packetized || klass->parse, GST_FLOW_ERROR);

  if (priv->current_frame == NULL) {
    priv->current_frame = gst_video_decoder_new_frame (decoder);
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    gst_video_decoder_add_timestamp (decoder, buf);
  }
  priv->input_offset += gst_buffer_get_size (buf);

  if (priv->packetized) {
    priv->current_frame->input_buffer = buf;

    if (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT)) {
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (priv->current_frame);
    }

    ret = gst_video_decoder_have_frame_2 (decoder);
  } else {

    gst_adapter_push (priv->input_adapter, buf);

    if (G_UNLIKELY (!gst_adapter_available (priv->input_adapter)))
      goto beach;

    do {
      ret =
          klass->parse (decoder, priv->current_frame, priv->input_adapter,
          FALSE);
    } while (ret == GST_FLOW_OK && gst_adapter_available (priv->input_adapter));

  }

beach:
  if (ret == GST_VIDEO_DECODER_FLOW_NEED_DATA)
    return GST_FLOW_OK;

  return ret;
}

static GstFlowReturn
gst_video_decoder_flush_decode (GstVideoDecoder * dec)
{
  GstVideoDecoderPrivate *priv = dec->priv;
  GstFlowReturn res = GST_FLOW_OK;
  GList *walk;

  walk = priv->decode;

  GST_DEBUG_OBJECT (dec, "flushing buffers to decode");

  /* clear buffer and decoder state */
  gst_video_decoder_flush (dec, FALSE);

  /* signal have_frame it should not capture frames */
  priv->process = TRUE;

  while (walk) {
    GList *next;
    GstVideoCodecFrame *frame = (GstVideoCodecFrame *) (walk->data);
    GstBuffer *buf = frame->input_buffer;

    GST_DEBUG_OBJECT (dec, "decoding frame %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    next = walk->next;
    if (priv->current_frame)
      gst_video_codec_frame_unref (priv->current_frame);
    priv->current_frame = frame;
    gst_video_codec_frame_ref (priv->current_frame);

    /* decode buffer, resulting data prepended to queue */
    res = gst_video_decoder_have_frame_2 (dec);

    walk = next;
  }

  priv->process = FALSE;

  return res;
}

static GstFlowReturn
gst_video_decoder_flush_parse (GstVideoDecoder * dec)
{
  GstVideoDecoderPrivate *priv = dec->priv;
  GstFlowReturn res = GST_FLOW_OK;
  GList *walk;

  walk = priv->parse;

  GST_DEBUG_OBJECT (dec, "flushing buffers to parsing");

  /* clear buffer and decoder state */
  gst_video_decoder_flush (dec, FALSE);

  while (walk) {
    GList *next;
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    GST_DEBUG_OBJECT (dec, "parsing buffer %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    next = walk->next;
    /* parse buffer, resulting frames prepended to parse_gather queue */
    gst_buffer_ref (buf);
    res = gst_video_decoder_chain_forward (dec, buf);

    /* if we generated output, we can discard the buffer, else we
     * keep it in the queue */
    if (priv->parse_gather) {
      GST_DEBUG_OBJECT (dec, "parsed buffer to %p", priv->parse_gather->data);
      priv->parse = g_list_delete_link (priv->parse, walk);
      gst_buffer_unref (buf);
    } else {
      GST_DEBUG_OBJECT (dec, "buffer did not decode, keeping");
    }
    walk = next;
  }

  /* now we can process frames */
  GST_DEBUG_OBJECT (dec, "checking frames");
  while (priv->parse_gather) {
    GstVideoCodecFrame *frame;

    frame = (GstVideoCodecFrame *) (priv->parse_gather->data);
    /* remove from the gather list */
    priv->parse_gather =
        g_list_delete_link (priv->parse_gather, priv->parse_gather);
    /* copy to decode queue */
    priv->decode = g_list_prepend (priv->decode, frame);

    /* if we copied a keyframe, flush and decode the decode queue */
    if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame)) {
      GST_DEBUG_OBJECT (dec, "copied keyframe");
      res = gst_video_decoder_flush_decode (dec);
    }
  }

  /* now send queued data downstream */
  while (priv->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (priv->queued->data);

    if (G_LIKELY (res == GST_FLOW_OK)) {
      GST_DEBUG_OBJECT (dec, "pushing buffer %p of size %" G_GSIZE_FORMAT ", "
          "time %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT, buf,
          gst_buffer_get_size (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
      /* should be already, but let's be sure */
      buf = gst_buffer_make_writable (buf);
      /* avoid stray DISCONT from forward processing,
       * which have no meaning in reverse pushing */
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);
      res = gst_pad_push (dec->srcpad, buf);
    } else {
      gst_buffer_unref (buf);
    }

    priv->queued = g_list_delete_link (priv->queued, priv->queued);
  }

  return res;
}

static GstFlowReturn
gst_video_decoder_chain_reverse (GstVideoDecoder * dec, GstBuffer * buf)
{
  GstVideoDecoderPrivate *priv = dec->priv;
  GstFlowReturn result = GST_FLOW_OK;

  /* if we have a discont, move buffers to the decode list */
  if (!buf || GST_BUFFER_IS_DISCONT (buf)) {
    GST_DEBUG_OBJECT (dec, "received discont");
    while (priv->gather) {
      GstBuffer *gbuf;

      gbuf = GST_BUFFER_CAST (priv->gather->data);
      /* remove from the gather list */
      priv->gather = g_list_delete_link (priv->gather, priv->gather);
      /* copy to parse queue */
      priv->parse = g_list_prepend (priv->parse, gbuf);
    }
    /* parse and decode stuff in the parse queue */
    gst_video_decoder_flush_parse (dec);
  }

  if (G_LIKELY (buf)) {
    GST_DEBUG_OBJECT (dec, "gathering buffer %p of size %" G_GSIZE_FORMAT ", "
        "time %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT, buf,
        gst_buffer_get_size (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    /* add buffer to gather queue */
    priv->gather = g_list_prepend (priv->gather, buf);
  }

  return result;
}

static GstFlowReturn
gst_video_decoder_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstVideoDecoder *decoder;
  GstVideoDecoderPrivate *priv;
  GstFlowReturn ret = GST_FLOW_OK;

  decoder = GST_VIDEO_DECODER (parent);
  priv = decoder->priv;

  GST_LOG_OBJECT (decoder,
      "chain %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT " size %"
      G_GSIZE_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), gst_buffer_get_size (buf));

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  /* NOTE:
   * requiring the pad to be negotiated makes it impossible to use
   * oggdemux or filesrc ! decoder */

  if (decoder->input_segment.format == GST_FORMAT_UNDEFINED) {
    GstEvent *event;
    GstSegment *segment = &decoder->input_segment;

    GST_WARNING_OBJECT (decoder,
        "Received buffer without a new-segment. "
        "Assuming timestamps start from 0.");

    gst_segment_init (segment, GST_FORMAT_TIME);

    event = gst_event_new_segment (segment);

    decoder->priv->current_frame_events =
        g_list_prepend (decoder->priv->current_frame_events, event);
  }

  if (G_UNLIKELY (GST_BUFFER_IS_DISCONT (buf))) {
    gint64 ts;

    GST_DEBUG_OBJECT (decoder, "received DISCONT buffer");

    /* track present position */
    ts = priv->timestamp_offset;

    /* buffer may claim DISCONT loudly, if it can't tell us where we are now,
     * we'll stick to where we were ...
     * Particularly useful/needed for upstream BYTE based */
    if (decoder->input_segment.rate > 0.0
        && !GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
      GST_DEBUG_OBJECT (decoder, "... but restoring previous ts tracking");
      priv->timestamp_offset = ts;
    }
  }

  if (decoder->input_segment.rate > 0.0)
    ret = gst_video_decoder_chain_forward (decoder, buf);
  else
    ret = gst_video_decoder_chain_reverse (decoder, buf);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  return ret;
}

static GstStateChangeReturn
gst_video_decoder_change_state (GstElement * element, GstStateChange transition)
{
  GstVideoDecoder *decoder;
  GstVideoDecoderClass *decoder_class;
  GstStateChangeReturn ret;

  decoder = GST_VIDEO_DECODER (element);
  decoder_class = GST_VIDEO_DECODER_GET_CLASS (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* open device/library if needed */
      if (decoder_class->open && !decoder_class->open (decoder))
        goto open_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Initialize device/library if needed */
      if (decoder_class->start && !decoder_class->start (decoder))
        goto start_failed;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (decoder_class->stop && !decoder_class->stop (decoder))
        goto stop_failed;

      GST_VIDEO_DECODER_STREAM_LOCK (decoder);
      gst_video_decoder_reset (decoder, TRUE);
      g_list_foreach (decoder->priv->current_frame_events,
          (GFunc) gst_event_unref, NULL);
      g_list_free (decoder->priv->current_frame_events);
      decoder->priv->current_frame_events = NULL;
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* close device/library if needed */
      if (decoder_class->close && !decoder_class->close (decoder))
        goto close_failed;
      break;
    default:
      break;
  }

  return ret;

  /* Errors */
open_failed:
  {
    GST_ELEMENT_ERROR (decoder, LIBRARY, INIT, (NULL),
        ("Failed to open decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }

start_failed:
  {
    GST_ELEMENT_ERROR (decoder, LIBRARY, INIT, (NULL),
        ("Failed to start decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }

stop_failed:
  {
    GST_ELEMENT_ERROR (decoder, LIBRARY, INIT, (NULL),
        ("Failed to stop decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }

close_failed:
  {
    GST_ELEMENT_ERROR (decoder, LIBRARY, INIT, (NULL),
        ("Failed to close decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

static GstVideoCodecFrame *
gst_video_decoder_new_frame (GstVideoDecoder * decoder)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstVideoCodecFrame *frame;

  frame = g_slice_new0 (GstVideoCodecFrame);

  frame->ref_count = 1;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  frame->system_frame_number = priv->system_frame_number;
  priv->system_frame_number++;
  frame->decode_frame_number = priv->decode_frame_number;
  priv->decode_frame_number++;

  frame->dts = GST_CLOCK_TIME_NONE;
  frame->pts = GST_CLOCK_TIME_NONE;
  frame->duration = GST_CLOCK_TIME_NONE;
  frame->events = priv->current_frame_events;
  priv->current_frame_events = NULL;
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  GST_LOG_OBJECT (decoder, "Created new frame %p (sfn:%d)",
      frame, frame->system_frame_number);

  return frame;
}

static void
gst_video_decoder_prepare_finish_frame (GstVideoDecoder *
    decoder, GstVideoCodecFrame * frame)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GList *l, *events = NULL;

#ifndef GST_DISABLE_GST_DEBUG
  GST_LOG_OBJECT (decoder, "n %d in %" G_GSIZE_FORMAT " out %" G_GSIZE_FORMAT,
      g_list_length (priv->frames),
      gst_adapter_available (priv->input_adapter),
      gst_adapter_available (priv->output_adapter));
#endif

  GST_LOG_OBJECT (decoder,
      "finish frame %p sync=%d pts=%" GST_TIME_FORMAT, frame,
      GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame), GST_TIME_ARGS (frame->pts));

  /* Push all pending events that arrived before this frame */
  for (l = priv->frames; l; l = l->next) {
    GstVideoCodecFrame *tmp = l->data;

    if (tmp->events) {
      events = g_list_concat (events, tmp->events);
      tmp->events = NULL;
    }

    if (tmp == frame)
      break;
  }

  for (l = g_list_last (events); l; l = l->prev) {
    GST_LOG_OBJECT (decoder, "pushing %s event", GST_EVENT_TYPE_NAME (l->data));
    gst_video_decoder_push_event (decoder, l->data);
  }
  g_list_free (events);

  /* Check if the data should not be displayed. For example altref/invisible
   * frame in vp8. In this case we should not update the timestamps. */
  if (GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY (frame))
    return;

  /* If the frame is meant to be outputted but we don't have an output buffer
   * we have a problem :) */
  if (G_UNLIKELY (frame->output_buffer == NULL))
    goto no_output_buffer;

  if (GST_CLOCK_TIME_IS_VALID (frame->pts)) {
    if (frame->pts != priv->timestamp_offset) {
      GST_DEBUG_OBJECT (decoder,
          "sync timestamp %" GST_TIME_FORMAT " diff %" GST_TIME_FORMAT,
          GST_TIME_ARGS (frame->pts),
          GST_TIME_ARGS (frame->pts - decoder->output_segment.start));
      priv->timestamp_offset = frame->pts;
    } else {
      /* This case is for one initial timestamp and no others, e.g.,
       * filesrc ! decoder ! xvimagesink */
      GST_WARNING_OBJECT (decoder, "sync timestamp didn't change, ignoring");
      frame->pts = GST_CLOCK_TIME_NONE;
    }
  } else {
    if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame)) {
      GST_WARNING_OBJECT (decoder, "sync point doesn't have timestamp");
      if (!GST_CLOCK_TIME_IS_VALID (priv->timestamp_offset)) {
        GST_WARNING_OBJECT (decoder,
            "No base timestamp.  Assuming frames start at segment start");
        priv->timestamp_offset = decoder->output_segment.start;
      }
    }
  }
  if (frame->pts == GST_CLOCK_TIME_NONE) {
    frame->pts =
        gst_video_decoder_get_timestamp (decoder, frame->decode_frame_number);
    frame->duration = GST_CLOCK_TIME_NONE;
  }
  if (frame->duration == GST_CLOCK_TIME_NONE) {
    frame->duration = gst_video_decoder_get_frame_duration (decoder, frame);
  }

  if (GST_CLOCK_TIME_IS_VALID (priv->last_timestamp)) {
    if (frame->pts < priv->last_timestamp) {
      GST_WARNING_OBJECT (decoder,
          "decreasing timestamp (%" GST_TIME_FORMAT " < %"
          GST_TIME_FORMAT ")",
          GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (priv->last_timestamp));
    }
  }
  priv->last_timestamp = frame->pts;

  return;

  /* ERRORS */
no_output_buffer:
  {
    GST_ERROR_OBJECT (decoder, "No buffer to output !");
  }
}

static void
gst_video_decoder_do_finish_frame (GstVideoDecoder * dec,
    GstVideoCodecFrame * frame)
{
  GList *link;

  /* unref once from the list */
  link = g_list_find (dec->priv->frames, frame);
  if (link) {
    gst_video_codec_frame_unref (frame);
    dec->priv->frames = g_list_delete_link (dec->priv->frames, link);
  }

  /* unref because this function takes ownership */
  gst_video_codec_frame_unref (frame);
}

/**
 * gst_video_decoder_drop_frame:
 * @dec: a #GstVideoDecoder
 * @frame: (transfer full): the #GstVideoCodecFrame to drop
 *
 * Similar to gst_video_decoder_finish_frame(), but drops @frame in any
 * case and posts a QoS message with the frame's details on the bus.
 * In any case, the frame is considered finished and released.
 *
 * Returns: a #GstFlowReturn, usually GST_FLOW_OK.
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_video_decoder_drop_frame (GstVideoDecoder * dec, GstVideoCodecFrame * frame)
{
  GstClockTime stream_time, jitter, earliest_time, qostime, timestamp;
  GstSegment *segment;
  GstMessage *qos_msg;
  gdouble proportion;

  GST_LOG_OBJECT (dec, "drop frame %p", frame);

  GST_VIDEO_DECODER_STREAM_LOCK (dec);

  gst_video_decoder_prepare_finish_frame (dec, frame);

  GST_DEBUG_OBJECT (dec, "dropping frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->pts));

  dec->priv->dropped++;

  /* post QoS message */
  timestamp = frame->pts;
  proportion = dec->priv->proportion;
  segment = &dec->output_segment;
  stream_time =
      gst_segment_to_stream_time (segment, GST_FORMAT_TIME, timestamp);
  qostime = gst_segment_to_running_time (segment, GST_FORMAT_TIME, timestamp);
  earliest_time = dec->priv->earliest_time;
  jitter = GST_CLOCK_DIFF (qostime, earliest_time);
  qos_msg =
      gst_message_new_qos (GST_OBJECT_CAST (dec), FALSE, qostime, stream_time,
      timestamp, GST_CLOCK_TIME_NONE);
  gst_message_set_qos_values (qos_msg, jitter, proportion, 1000000);
  gst_message_set_qos_stats (qos_msg, GST_FORMAT_BUFFERS,
      dec->priv->processed, dec->priv->dropped);
  gst_element_post_message (GST_ELEMENT_CAST (dec), qos_msg);

  /* now free the frame */
  gst_video_decoder_do_finish_frame (dec, frame);

  GST_VIDEO_DECODER_STREAM_UNLOCK (dec);

  return GST_FLOW_OK;
}

/**
 * gst_video_decoder_finish_frame:
 * @decoder: a #GstVideoDecoder
 * @frame: (transfer full): a decoded #GstVideoCodecFrame
 *
 * @frame should have a valid decoded data buffer, whose metadata fields
 * are then appropriately set according to frame data and pushed downstream.
 * If no output data is provided, @frame is considered skipped.
 * In any case, the frame is considered finished and released.
 *
 * Returns: a #GstFlowReturn resulting from sending data downstream
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_video_decoder_finish_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstBuffer *output_buffer;
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 start, stop;
  GstSegment *segment;

  GST_LOG_OBJECT (decoder, "finish frame %p", frame);

  if (G_UNLIKELY (priv->output_state_changed || (priv->output_state
              && gst_pad_check_reconfigure (decoder->srcpad))))
    gst_video_decoder_set_src_caps (decoder);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  gst_video_decoder_prepare_finish_frame (decoder, frame);
  priv->processed++;
  /* no buffer data means this frame is skipped */
  if (!frame->output_buffer || GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY (frame)) {
    GST_DEBUG_OBJECT (decoder, "skipping frame %" GST_TIME_FORMAT,
        GST_TIME_ARGS (frame->pts));
    goto done;
  }

  output_buffer = gst_buffer_make_writable (frame->output_buffer);
  frame->output_buffer = NULL;

  GST_BUFFER_FLAG_UNSET (output_buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  if (priv->discont) {
    GST_BUFFER_FLAG_SET (output_buffer, GST_BUFFER_FLAG_DISCONT);
    priv->discont = FALSE;
  }

  /* Check for clipping */
  start = frame->pts;
  stop = frame->pts + frame->duration;

  segment = &decoder->output_segment;
  if (gst_segment_clip (segment, GST_FORMAT_TIME, start, stop, &start, &stop)) {
    GST_BUFFER_TIMESTAMP (output_buffer) = start;
    GST_BUFFER_DURATION (output_buffer) = stop - start;
    GST_LOG_OBJECT (decoder,
        "accepting buffer inside segment: %" GST_TIME_FORMAT " %"
        GST_TIME_FORMAT " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
        " time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (output_buffer)),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (output_buffer) +
            GST_BUFFER_DURATION (output_buffer)),
        GST_TIME_ARGS (segment->start), GST_TIME_ARGS (segment->stop),
        GST_TIME_ARGS (segment->time));
  } else {
    GST_LOG_OBJECT (decoder,
        "dropping buffer outside segment: %" GST_TIME_FORMAT
        " %" GST_TIME_FORMAT
        " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
        " time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (frame->pts),
        GST_TIME_ARGS (frame->pts + frame->duration),
        GST_TIME_ARGS (segment->start),
        GST_TIME_ARGS (segment->stop), GST_TIME_ARGS (segment->time));
    gst_buffer_unref (output_buffer);
    ret = GST_FLOW_OK;
    goto done;
  }

  GST_BUFFER_OFFSET (output_buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (output_buffer) = GST_BUFFER_OFFSET_NONE;

  /* update rate estimate */
  priv->bytes_out += gst_buffer_get_size (output_buffer);
  if (GST_CLOCK_TIME_IS_VALID (frame->duration)) {
    priv->time += frame->duration;
  } else {
    /* FIXME : Use difference between current and previous outgoing
     * timestamp, and relate to difference between current and previous
     * bytes */
    /* better none than nothing valid */
    priv->time = GST_CLOCK_TIME_NONE;
  }

  GST_LOG_OBJECT (decoder, "pushing frame ts %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (output_buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (output_buffer)));

  /* we got data, so note things are looking up again */
  /* FIXME : Shouldn't we avoid going under zero ? */
  if (G_UNLIKELY (priv->error_count))
    priv->error_count--;
  if (decoder->output_segment.rate < 0.0) {
    GST_LOG_OBJECT (decoder, "queued buffer");
    priv->queued = g_list_prepend (priv->queued, output_buffer);
  } else {
    ret = gst_pad_push (decoder->srcpad, output_buffer);
  }

done:

  gst_video_decoder_do_finish_frame (decoder, frame);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return ret;
}

/**
 * gst_video_decoder_add_to_frame:
 * @decoder: a #GstVideoDecoder
 * @n_bytes: the number of bytes to add
 *
 * Removes next @n_bytes of input data and adds it to currently parsed frame.
 *
 * Since: 0.10.36
 */
void
gst_video_decoder_add_to_frame (GstVideoDecoder * decoder, int n_bytes)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstBuffer *buf;

  GST_LOG_OBJECT (decoder, "add %d bytes to frame", n_bytes);

  if (n_bytes == 0)
    return;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  if (gst_adapter_available (priv->output_adapter) == 0) {
    priv->frame_offset =
        priv->input_offset - gst_adapter_available (priv->input_adapter);
  }
  buf = gst_adapter_take_buffer (priv->input_adapter, n_bytes);

  gst_adapter_push (priv->output_adapter, buf);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
}

static guint64
gst_video_decoder_get_timestamp (GstVideoDecoder * decoder, int picture_number)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstVideoCodecState *state = priv->output_state;

  if (state->info.fps_d == 0 || state->info.fps_n == 0) {
    return -1;
  }
  if (picture_number < priv->base_picture_number) {
    return priv->timestamp_offset -
        (gint64) gst_util_uint64_scale (priv->base_picture_number
        - picture_number, state->info.fps_d * GST_SECOND, state->info.fps_n);
  } else {
    return priv->timestamp_offset +
        gst_util_uint64_scale (picture_number -
        priv->base_picture_number, state->info.fps_d * GST_SECOND,
        state->info.fps_n);
  }
}

static guint64
gst_video_decoder_get_frame_duration (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstVideoCodecState *state = decoder->priv->output_state;

  if (state->info.fps_d == 0 || state->info.fps_n == 0) {
    return GST_CLOCK_TIME_NONE;
  }

  /* FIXME: For interlaced frames this needs to take into account
   * the number of valid fields in the frame
   */

  return gst_util_uint64_scale (GST_SECOND, state->info.fps_d,
      state->info.fps_n);
}

/**
 * gst_video_decoder_have_frame:
 * @decoder: a #GstVideoDecoder
 *
 * Gathers all data collected for currently parsed frame, gathers corresponding
 * metadata and passes it along for further processing, i.e. @handle_frame.
 *
 * Returns: a #GstFlowReturn
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_video_decoder_have_frame (GstVideoDecoder * decoder)
{
  GstBuffer *buffer;
  int n_available;
  GstClockTime timestamp;
  GstClockTime duration;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (decoder, "have_frame");

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  n_available = gst_adapter_available (decoder->priv->output_adapter);
  if (n_available) {
    buffer =
        gst_adapter_take_buffer (decoder->priv->output_adapter, n_available);
  } else {
    buffer = gst_buffer_new_and_alloc (0);
  }

  decoder->priv->current_frame->input_buffer = buffer;

  gst_video_decoder_get_timestamp_at_offset (decoder,
      decoder->priv->frame_offset, &timestamp, &duration);

  GST_BUFFER_TIMESTAMP (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = duration;

  GST_LOG_OBJECT (decoder, "collected frame size %d, "
      "ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT,
      n_available, GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration));

  ret = gst_video_decoder_have_frame_2 (decoder);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return ret;
}

static GstFlowReturn
gst_video_decoder_have_frame_2 (GstVideoDecoder * decoder)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstVideoCodecFrame *frame = priv->current_frame;
  GstVideoDecoderClass *decoder_class;
  GstFlowReturn ret = GST_FLOW_OK;

  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);

  /* FIXME : This should only have to be checked once (either the subclass has an 
   * implementation, or it doesn't) */
  g_return_val_if_fail (decoder_class->handle_frame != NULL, GST_FLOW_ERROR);

  /* capture frames and queue for later processing */
  if (decoder->output_segment.rate < 0.0 && !priv->process) {
    priv->parse_gather = g_list_prepend (priv->parse_gather, frame);
    goto exit;
  }

  frame->distance_from_sync = priv->distance_from_sync;
  priv->distance_from_sync++;
  frame->pts = GST_BUFFER_TIMESTAMP (frame->input_buffer);
  frame->duration = GST_BUFFER_DURATION (frame->input_buffer);

  /* For keyframes, DTS = PTS */
  if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame))
    frame->dts = frame->pts;

  GST_LOG_OBJECT (decoder, "pts %" GST_TIME_FORMAT, GST_TIME_ARGS (frame->pts));
  GST_LOG_OBJECT (decoder, "dts %" GST_TIME_FORMAT, GST_TIME_ARGS (frame->dts));
  GST_LOG_OBJECT (decoder, "dist %d", frame->distance_from_sync);
  priv->frames = g_list_append (priv->frames, frame);
  frame->deadline =
      gst_segment_to_running_time (&decoder->input_segment, GST_FORMAT_TIME,
      frame->pts);

  /* do something with frame */
  gst_video_codec_frame_ref (frame);
  ret = decoder_class->handle_frame (decoder, frame);
  if (ret != GST_FLOW_OK)
    GST_DEBUG_OBJECT (decoder, "flow error %s", gst_flow_get_name (ret));

exit:
  /* current frame has either been added to parse_gather or sent to
     handle frame so there is no need to unref it */

  /* create new frame */
  priv->current_frame = gst_video_decoder_new_frame (decoder);
  return ret;
}


/**
 * gst_video_decoder_get_output_state:
 * @decoder: a #GstVideoDecoder
 *
 * Get the #GstVideoCodecState currently describing the output stream.
 *
 * Returns: (transfer full): #GstVideoCodecState describing format of video data.
 *
 * Since: 0.10.36
 */
GstVideoCodecState *
gst_video_decoder_get_output_state (GstVideoDecoder * decoder)
{
  GstVideoCodecState *state = NULL;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  if (decoder->priv->output_state)
    state = gst_video_codec_state_ref (decoder->priv->output_state);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return state;
}

/**
 * gst_video_decoder_set_output_state:
 * @decoder: a #GstVideoDecoder
 * @fmt: a #GstVideoFormat
 * @width: The width in pixels
 * @height: The height in pixels
 * @reference: (allow-none) (transfer none): An optional reference #GstVideoCodecState
 *
 * Creates a new #GstVideoCodecState with the specified @fmt, @width and @height
 * as the output state for the decoder.
 * Any previously set output state on @decoder will be replaced by the newly
 * created one.
 *
 * If the subclass wishes to copy over existing fields (like pixel aspec ratio,
 * or framerate) from an existing #GstVideoCodecState, it can be provided as a
 * @reference.
 *
 * If the subclass wishes to override some fields from the output state (like
 * pixel-aspect-ratio or framerate) it can do so on the returned #GstVideoCodecState.
 *
 * The new output state will only take effect (set on pads and buffers) starting
 * from the next call to #gst_video_decoder_finish_frame().
 *
 * Returns: (transfer full): the newly configured output state.
 *
 * Since: 0.10.36
 */
GstVideoCodecState *
gst_video_decoder_set_output_state (GstVideoDecoder * decoder,
    GstVideoFormat fmt, guint width, guint height,
    GstVideoCodecState * reference)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstVideoCodecState *state;

  GST_DEBUG_OBJECT (decoder, "fmt:%d, width:%d, height:%d, reference:%p",
      fmt, width, height, reference);

  /* Create the new output state */
  state = _new_output_state (fmt, width, height, reference);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  /* Replace existing output state by new one */
  if (priv->output_state)
    gst_video_codec_state_unref (priv->output_state);
  priv->output_state = gst_video_codec_state_ref (state);

  priv->output_state_changed = TRUE;
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return state;
}


/**
 * gst_video_decoder_get_oldest_frame:
 * @decoder: a #GstVideoDecoder
 *
 * Get the oldest pending unfinished #GstVideoCodecFrame
 *
 * Returns: (transfer full): oldest pending unfinished #GstVideoCodecFrame.
 *
 * Since: 0.10.36
 */
GstVideoCodecFrame *
gst_video_decoder_get_oldest_frame (GstVideoDecoder * decoder)
{
  GstVideoCodecFrame *frame = NULL;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  if (decoder->priv->frames)
    frame = gst_video_codec_frame_ref (decoder->priv->frames->data);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return (GstVideoCodecFrame *) frame;
}

/**
 * gst_video_decoder_get_frame:
 * @decoder: a #GstVideoDecoder
 * @frame_number: system_frame_number of a frame
 *
 * Get a pending unfinished #GstVideoCodecFrame
 * 
 * Returns: (transfer none): pending unfinished #GstVideoCodecFrame identified by @frame_number.
 *
 * Since: 0.10.36
 */
GstVideoCodecFrame *
gst_video_decoder_get_frame (GstVideoDecoder * decoder, int frame_number)
{
  GList *g;
  GstVideoCodecFrame *frame = NULL;

  GST_DEBUG_OBJECT (decoder, "frame_number : %d", frame_number);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  for (g = decoder->priv->frames; g; g = g->next) {
    GstVideoCodecFrame *tmp = g->data;

    if (tmp->system_frame_number == frame_number) {
      frame = tmp;
      break;
    }
  }
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return frame;
}

static gboolean
gst_video_decoder_decide_allocation_default (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstCaps *outcaps;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  GstStructure *config;
  gboolean update_pool, update_allocator;
  GstVideoInfo vinfo;

  gst_query_parse_allocation (query, &outcaps, NULL);
  gst_video_info_init (&vinfo);
  gst_video_info_from_caps (&vinfo, outcaps);

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    /* try the allocator */
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    update_allocator = TRUE;
  } else {
    allocator = NULL;
    gst_allocation_params_init (&params);
    update_allocator = FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    size = MAX (size, vinfo.size);
    update_pool = TRUE;
  } else {
    pool = NULL;
    size = vinfo.size;
    min = max = 0;

    update_pool = FALSE;
  }

  if (pool == NULL) {
    /* no pool, we can make our own */
    GST_DEBUG_OBJECT (decoder, "no pool, making new pool");
    pool = gst_video_buffer_pool_new ();
  }

  /* now configure */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);
  gst_buffer_pool_set_config (pool, config);

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);
  if (allocator)
    gst_allocator_unref (allocator);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  if (pool)
    gst_object_unref (pool);

  return TRUE;
}

/**
 * gst_video_decoder_set_src_caps:
 * @decoder: a #GstVideoDecoder
 *
 * Sets src pad caps according to currently configured #GstVideoCodecState.
 *
 * Returns: #TRUE if the caps were accepted downstream, else #FALSE.
 *
 * Since: 0.10.36
 */
static gboolean
gst_video_decoder_set_src_caps (GstVideoDecoder * decoder)
{
  GstVideoCodecState *state = decoder->priv->output_state;
  GstVideoDecoderClass *klass;
  GstQuery *query = NULL;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator;
  GstAllocationParams params;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_VIDEO_INFO_WIDTH (&state->info) != 0, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_HEIGHT (&state->info) != 0, FALSE);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  klass = GST_VIDEO_DECODER_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (decoder, "output_state par %d/%d fps %d/%d",
      state->info.par_n, state->info.par_d,
      state->info.fps_n, state->info.fps_d);

  if (G_UNLIKELY (state->caps == NULL))
    state->caps = gst_video_info_to_caps (&state->info);

  GST_DEBUG_OBJECT (decoder, "setting caps %" GST_PTR_FORMAT, state->caps);

  ret = gst_pad_set_caps (decoder->srcpad, state->caps);
  if (!ret)
    goto done;
  decoder->priv->output_state_changed = FALSE;

  /* Negotiate pool */
  query = gst_query_new_allocation (state->caps, TRUE);

  if (!gst_pad_peer_query (decoder->srcpad, query)) {
    GST_DEBUG_OBJECT (decoder, "didn't get downstream ALLOCATION hints");
  }

  g_assert (klass->decide_allocation != NULL);
  ret = klass->decide_allocation (decoder, query);

  GST_DEBUG_OBJECT (decoder, "ALLOCATION (%d) params: %" GST_PTR_FORMAT, ret,
      query);

  if (!ret)
    goto no_decide_allocation;

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
  } else {
    allocator = NULL;
    gst_allocation_params_init (&params);
  }

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);
  if (!pool) {
    if (allocator)
      gst_allocator_unref (allocator);
    ret = FALSE;
    goto no_decide_allocation;
  }

  if (decoder->priv->allocator)
    gst_allocator_unref (decoder->priv->allocator);
  decoder->priv->allocator = allocator;
  decoder->priv->params = params;

  if (decoder->priv->pool) {
    gst_buffer_pool_set_active (decoder->priv->pool, FALSE);
    gst_object_unref (decoder->priv->pool);
  }
  decoder->priv->pool = pool;

  /* and activate */
  gst_buffer_pool_set_active (pool, TRUE);

done:
  if (query)
    gst_query_unref (query);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return ret;

  /* Errors */
no_decide_allocation:
  {
    GST_WARNING_OBJECT (decoder, "Subclass failed to decide allocation");
    goto done;
  }
}

/**
 * gst_video_decoder_alloc_output_buffer:
 * @decoder: a #GstVideoDecoder
 *
 * Helper function that allocates a buffer to hold a video frame for @decoder's
 * current #GstVideoCodecState.
 *
 * Returns: (transfer full): allocated buffer
 *
 * Since: 0.10.36
 */
GstBuffer *
gst_video_decoder_alloc_output_buffer (GstVideoDecoder * decoder)
{
  GstBuffer *buffer;

  GST_DEBUG ("alloc src buffer");

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  if (G_UNLIKELY (decoder->priv->output_state_changed
          || (decoder->priv->output_state
              && gst_pad_check_reconfigure (decoder->srcpad))))
    gst_video_decoder_set_src_caps (decoder);

  gst_buffer_pool_acquire_buffer (decoder->priv->pool, &buffer, NULL);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return buffer;
}

/**
 * gst_video_decoder_alloc_output_frame:
 * @decoder: a #GstVideoDecoder
 * @frame: a #GstVideoCodecFrame
 *
 * Helper function that allocates a buffer to hold a video frame for @decoder's
 * current #GstVideoCodecState.  Subclass should already have configured video
 * state and set src pad caps.
 *
 * Returns: %GST_FLOW_OK if an output buffer could be allocated
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_video_decoder_alloc_output_frame (GstVideoDecoder *
    decoder, GstVideoCodecFrame * frame)
{
  GstFlowReturn flow_ret;
  GstVideoCodecState *state = decoder->priv->output_state;
  int num_bytes = GST_VIDEO_INFO_SIZE (&state->info);

  g_return_val_if_fail (num_bytes != 0, GST_FLOW_ERROR);

  if (G_UNLIKELY (decoder->priv->output_state_changed
          || (decoder->priv->output_state
              && gst_pad_check_reconfigure (decoder->srcpad))))
    gst_video_decoder_set_src_caps (decoder);

  GST_LOG_OBJECT (decoder, "alloc buffer size %d", num_bytes);
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  flow_ret = gst_buffer_pool_acquire_buffer (decoder->priv->pool,
      &frame->output_buffer, NULL);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return flow_ret;
}

/**
 * gst_video_decoder_get_max_decode_time:
 * @decoder: a #GstVideoDecoder
 * @frame: a #GstVideoCodecFrame
 *
 * Determines maximum possible decoding time for @frame that will
 * allow it to decode and arrive in time (as determined by QoS events).
 * In particular, a negative result means decoding in time is no longer possible
 * and should therefore occur as soon/skippy as possible.
 *
 * Returns: max decoding time.
 *
 * Since: 0.10.36
 */
GstClockTimeDiff
gst_video_decoder_get_max_decode_time (GstVideoDecoder *
    decoder, GstVideoCodecFrame * frame)
{
  GstClockTimeDiff deadline;
  GstClockTime earliest_time;

  GST_OBJECT_LOCK (decoder);
  earliest_time = decoder->priv->earliest_time;
  if (GST_CLOCK_TIME_IS_VALID (earliest_time))
    deadline = GST_CLOCK_DIFF (earliest_time, frame->deadline);
  else
    deadline = G_MAXINT64;

  GST_LOG_OBJECT (decoder, "earliest %" GST_TIME_FORMAT
      ", frame deadline %" GST_TIME_FORMAT ", deadline %" GST_TIME_FORMAT,
      GST_TIME_ARGS (earliest_time), GST_TIME_ARGS (frame->deadline),
      GST_TIME_ARGS (deadline));

  GST_OBJECT_UNLOCK (decoder);

  return deadline;
}

GstFlowReturn
_gst_video_decoder_error (GstVideoDecoder * dec, gint weight,
    GQuark domain, gint code, gchar * txt, gchar * dbg, const gchar * file,
    const gchar * function, gint line)
{
  if (txt)
    GST_WARNING_OBJECT (dec, "error: %s", txt);
  if (dbg)
    GST_WARNING_OBJECT (dec, "error: %s", dbg);
  dec->priv->error_count += weight;
  dec->priv->discont = TRUE;
  if (dec->priv->max_errors < dec->priv->error_count) {
    gst_element_message_full (GST_ELEMENT (dec), GST_MESSAGE_ERROR,
        domain, code, txt, dbg, file, function, line);
    return GST_FLOW_ERROR;
  } else {
    return GST_FLOW_OK;
  }
}

/**
 * gst_video_decoder_set_max_errors:
 * @dec: a #GstVideoDecoder
 * @num: max tolerated errors
 *
 * Sets numbers of tolerated decoder errors, where a tolerated one is then only
 * warned about, but more than tolerated will lead to fatal error.  Default
 * is set to GST_VIDEO_DECODER_MAX_ERRORS.
 *
 * Since: 0.10.36
 */
void
gst_video_decoder_set_max_errors (GstVideoDecoder * dec, gint num)
{
  g_return_if_fail (GST_IS_VIDEO_DECODER (dec));

  dec->priv->max_errors = num;
}

/**
 * gst_video_decoder_get_max_errors:
 * @dec: a #GstVideoDecoder
 *
 * Returns: currently configured decoder tolerated error count.
 *
 * Since: 0.10.36
 */
gint
gst_video_decoder_get_max_errors (GstVideoDecoder * dec)
{
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (dec), 0);

  return dec->priv->max_errors;
}

/**
 * gst_video_decoder_set_packetized:
 * @decoder: a #GstVideoDecoder
 * @packetized: whether the input data should be considered as packetized.
 *
 * Allows baseclass to consider input data as packetized or not. If the
 * input is packetized, then the @parse method will not be called.
 *
 * Since: 0.10.36
 */
void
gst_video_decoder_set_packetized (GstVideoDecoder * decoder,
    gboolean packetized)
{
  decoder->priv->packetized = packetized;
}

/**
 * gst_video_decoder_get_packetized:
 * @decoder: a #GstVideoDecoder
 *
 * Queries whether input data is considered packetized or not by the
 * base class.
 *
 * Returns: TRUE if input data is considered packetized.
 *
 * Since: 0.10.36
 */
gboolean
gst_video_decoder_get_packetized (GstVideoDecoder * decoder)
{
  return decoder->priv->packetized;
}

/**
 * gst_video_decoder_set_estimate_rate:
 * @dec: a #GstVideoDecoder
 * @enabled: whether to enable byte to time conversion
 *
 * Allows baseclass to perform byte to time estimated conversion.
 *
 * Since: 0.10.36
 */
void
gst_video_decoder_set_estimate_rate (GstVideoDecoder * dec, gboolean enabled)
{
  g_return_if_fail (GST_IS_VIDEO_DECODER (dec));

  dec->priv->do_estimate_rate = enabled;
}

/**
 * gst_video_decoder_get_estimate_rate:
 * @dec: a #GstVideoDecoder
 *
 * Returns: currently configured byte to time conversion setting
 *
 * Since: 0.10.36
 */
gboolean
gst_video_decoder_get_estimate_rate (GstVideoDecoder * dec)
{
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (dec), 0);

  return dec->priv->do_estimate_rate;
}

/**
 * gst_video_decoder_set_latency:
 * @decoder: a #GstVideoDecoder
 * @min_latency: minimum latency
 * @max_latency: maximum latency
 *
 * Lets #GstVideoDecoder sub-classes tell the baseclass what the decoder
 * latency is. Will also post a LATENCY message on the bus so the pipeline
 * can reconfigure its global latency.
 *
 * Since: 0.10.36
 */
void
gst_video_decoder_set_latency (GstVideoDecoder * decoder,
    GstClockTime min_latency, GstClockTime max_latency)
{
  g_return_if_fail (GST_CLOCK_TIME_IS_VALID (min_latency));
  g_return_if_fail (max_latency >= min_latency);

  GST_OBJECT_LOCK (decoder);
  decoder->priv->min_latency = min_latency;
  decoder->priv->max_latency = max_latency;
  GST_OBJECT_UNLOCK (decoder);

  gst_element_post_message (GST_ELEMENT_CAST (decoder),
      gst_message_new_latency (GST_OBJECT_CAST (decoder)));
}

/**
 * gst_video_decoder_get_latency:
 * @decoder: a #GstVideoDecoder
 * @min_latency: (out) (allow-none): address of variable in which to store the
 *     configured minimum latency, or %NULL
 * @max_latency: (out) (allow-none): address of variable in which to store the
 *     configured mximum latency, or %NULL
 *
 * Query the configured decoder latency. Results will be returned via
 * @min_latency and @max_latency.
 *
 * Since: 0.10.36
 */
void
gst_video_decoder_get_latency (GstVideoDecoder * decoder,
    GstClockTime * min_latency, GstClockTime * max_latency)
{
  GST_OBJECT_LOCK (decoder);
  if (min_latency)
    *min_latency = decoder->priv->min_latency;
  if (max_latency)
    *max_latency = decoder->priv->max_latency;
  GST_OBJECT_UNLOCK (decoder);
}
