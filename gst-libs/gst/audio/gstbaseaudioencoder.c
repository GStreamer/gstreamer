/* GStreamer
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
 * SECTION:gstbaseaudioencoder
 * @short_description: Base class for audio encoders
 * @see_also: #GstBaseTransform
 *
 * This base class is for audio encoders turning raw audio samples into
 * encoded audio data.
 *
 * GstBaseAudioEncoder and subclass should cooperate as follows.
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Configuration</title>
 *   <listitem><para>
 *     Initially, GstBaseAudioEncoder calls @start when the encoder element
 *     is activated, which allows subclass to perform any global setup.
 *   </para></listitem>
 *   <listitem><para>
 *     GstBaseAudioEncoder calls @set_format to inform subclass of the format
 *     of input audio data that it is about to receive.  Subclass should
 *     setup for encoding and configure various base class context parameters
 *     appropriately, notably those directing desired input data handling.
 *     While unlikely, it might be called more than once, if changing input
 *     parameters require reconfiguration.
 *   </para></listitem>
 *   <listitem><para>
 *     GstBaseAudioEncoder calls @stop at end of all processing.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * As of configuration stage, and throughout processing, GstBaseAudioEncoder
 * provides a GstBaseAudioEncoderContext that provides required context,
 * e.g. describing the format of input audio data.
 * Conversely, subclass can and should configure context to inform
 * base class of its expectation w.r.t. buffer handling.
 * <listitem>
 *   <itemizedlist>
 *   <title>Data processing</title>
 *     <listitem><para>
 *       Base class gathers input sample data (as directed by the context's
 *       frame_samples and frame_max) and provides this to subclass' @handle_frame.
 *     </para></listitem>
 *     <listitem><para>
 *       If codec processing results in encoded data, subclass should call
 *       @gst_base_audio_encoder_finish_frame to have encoded data pushed
 *       downstream.  Alternatively, it might also call to indicate dropped
 *       (non-encoded) samples.
 *     </para></listitem>
 *     <listitem><para>
 *       Just prior to actually pushing a buffer downstream,
 *       it is passed to @pre_push.
 *     </para></listitem>
 *     <listitem><para>
 *       During the parsing process GstBaseAudioEncoderClass will handle both
 *       srcpad and sinkpad events. Sink events will be passed to subclass
 *       if @event callback has been provided.
 *     </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Shutdown phase</title>
 *   <listitem><para>
 *     GstBaseAudioEncoder class calls @stop to inform the subclass that data
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
 * it might be delayed until calling @gst_base_audio_encoder_finish_frame.
 *
 * In summary, above process should have subclass concentrating on
 * codec data processing while leaving other matters to base class,
 * such as most notably timestamp handling.  While it may exert more control
 * in this area (see e.g. @pre_push), it is very much not recommended.
 *
 * In particular, base class will either favor tracking upstream timestamps
 * (at the possible expense of jitter) or aim to arrange for a perfect stream of
 * output timestamps, depending on #GstBaseAudioEncoder:perfect-ts.
 * However, in the latter case, the input may not be so perfect or ideal, which
 * is handled as follows.  An input timestamp is compared with the expected
 * timestamp as dictated by input sample stream and if the deviation is less
 * than #GstBaseAudioEncoder:tolerance, the deviation is discarded.
 * Otherwise, it is considered a discontuinity and subsequent output timestamp
 * is resynced to the new position after performing configured discontinuity
 * processing.  In the non-perfect-ts case, an upstream variation exceeding
 * tolerance only leads to marking DISCONT on subsequent outgoing
 * (while timestamps are adjusted to upstream regardless of variation).
 * While DISCONT is also marked in the perfect-ts case, this one optionally
 * (see #GstBaseAudioEncoder:hard-resync)
 * performs some additional steps, such as clipping of (early) input samples
 * or draining all currently remaining input data, depending on the direction
 * of the discontuinity.
 *
 * If perfect timestamps are arranged, it is also possible to request baseclass
 * (usually set by subclass) to provide additional buffer metadata (in OFFSET
 * and OFFSET_END) fields according to granule defined semantics currently
 * needed by oggmux.  Specifically, OFFSET is set to granulepos (= sample count
 * including buffer) and OFFSET_END to corresponding timestamp (as determined
 * by same sample count and sample rate).
 *
 * Things that subclass need to take care of:
 * <itemizedlist>
 *   <listitem><para>Provide pad templates</para></listitem>
 *   <listitem><para>
 *      Set source pad caps when appropriate
 *   </para></listitem>
 *   <listitem><para>
 *      Inform base class of buffer processing needs using context's
 *      frame_samples and frame_bytes.
 *   </para></listitem>
 *   <listitem><para>
 *      Set user-configurable properties to sane defaults for format and
 *      implementing codec at hand, e.g. those controlling timestamp behaviour
 *      and discontinuity processing.
 *   </para></listitem>
 *   <listitem><para>
 *      Accept data in @handle_frame and provide encoded results to
 *      @gst_base_audio_encoder_finish_frame.
 *   </para></listitem>
 * </itemizedlist>
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstbaseaudioencoder.h"
#include <gst/base/gstadapter.h>
#include <gst/audio/audio.h>

#include <stdlib.h>
#include <string.h>


GST_DEBUG_CATEGORY_STATIC (gst_base_audio_encoder_debug);
#define GST_CAT_DEFAULT gst_base_audio_encoder_debug

#define GST_BASE_AUDIO_ENCODER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_BASE_AUDIO_ENCODER, \
        GstBaseAudioEncoderPrivate))

enum
{
  PROP_0,
  PROP_PERFECT_TS,
  PROP_GRANULE,
  PROP_HARD_RESYNC,
  PROP_TOLERANCE
};

#define DEFAULT_PERFECT_TS   FALSE
#define DEFAULT_GRANULE      FALSE
#define DEFAULT_HARD_RESYNC  FALSE
#define DEFAULT_TOLERANCE    40000000

struct _GstBaseAudioEncoderPrivate
{
  /* activation status */
  gboolean active;

  /* input base/first ts as basis for output ts;
   * kept nearly constant for perfect_ts,
   * otherwise resyncs to upstream ts */
  GstClockTime base_ts;
  /* corresponding base granulepos */
  gint64 base_gp;
  /* input samples processed and sent downstream so far (w.r.t. base_ts) */
  guint64 samples;

  /* currently collected sample data */
  GstAdapter *adapter;
  /* offset in adapter up to which already supplied to encoder */
  gint offset;
  /* mark outgoing discont */
  gboolean discont;
  /* to guess duration of drained data */
  GstClockTime last_duration;

  /* subclass provided data in processing round */
  gboolean got_data;
  /* subclass gave all it could already */
  gboolean drained;
  /* subclass currently being forcibly drained */
  gboolean force;

  /* output bps estimatation */
  /* global in samples seen */
  guint64 samples_in;
  /* global bytes sent out */
  guint64 bytes_out;

  /* context storage */
  GstBaseAudioEncoderContext ctx;
};


static GstElementClass *parent_class = NULL;

static void gst_base_audio_encoder_class_init (GstBaseAudioEncoderClass *
    klass);
static void gst_base_audio_encoder_init (GstBaseAudioEncoder * parse,
    GstBaseAudioEncoderClass * klass);

GType
gst_base_audio_encoder_get_type (void)
{
  static GType base_audio_encoder_type = 0;

  if (!base_audio_encoder_type) {
    static const GTypeInfo base_audio_encoder_info = {
      sizeof (GstBaseAudioEncoderClass),
      (GBaseInitFunc) NULL,
      (GBaseFinalizeFunc) NULL,
      (GClassInitFunc) gst_base_audio_encoder_class_init,
      NULL,
      NULL,
      sizeof (GstBaseAudioEncoder),
      0,
      (GInstanceInitFunc) gst_base_audio_encoder_init,
    };
    const GInterfaceInfo preset_interface_info = {
      NULL,                     /* interface_init */
      NULL,                     /* interface_finalize */
      NULL                      /* interface_data */
    };

    base_audio_encoder_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseAudioEncoder", &base_audio_encoder_info, G_TYPE_FLAG_ABSTRACT);

    g_type_add_interface_static (base_audio_encoder_type, GST_TYPE_PRESET,
        &preset_interface_info);
  }
  return base_audio_encoder_type;
}

static void gst_base_audio_encoder_finalize (GObject * object);
static void gst_base_audio_encoder_reset (GstBaseAudioEncoder * enc,
    gboolean full);

static void gst_base_audio_encoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_audio_encoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_base_audio_encoder_sink_activate_push (GstPad * pad,
    gboolean active);

static gboolean gst_base_audio_encoder_sink_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_base_audio_encoder_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static GstFlowReturn gst_base_audio_encoder_chain (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_base_audio_encoder_src_query (GstPad * pad,
    GstQuery * query);
static gboolean gst_base_audio_encoder_sink_query (GstPad * pad,
    GstQuery * query);
static const GstQueryType *gst_base_audio_encoder_get_query_types (GstPad *
    pad);
static GstCaps *gst_base_audio_encoder_sink_getcaps (GstPad * pad);


static void
gst_base_audio_encoder_class_init (GstBaseAudioEncoderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (gst_base_audio_encoder_debug, "baseaudioencoder", 0,
      "baseaudioencoder element");

  g_type_class_add_private (klass, sizeof (GstBaseAudioEncoderPrivate));

  gobject_class->set_property = gst_base_audio_encoder_set_property;
  gobject_class->get_property = gst_base_audio_encoder_get_property;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_audio_encoder_finalize);

  /* properties */
  g_object_class_install_property (gobject_class, PROP_PERFECT_TS,
      g_param_spec_boolean ("perfect-ts", "Perfect Timestamps",
          "Favour perfect timestamps over tracking upstream timestamps",
          DEFAULT_PERFECT_TS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_GRANULE,
      g_param_spec_boolean ("granule", "Granule Marking",
          "Apply granule semantics to buffer metadata (implies perfect-ts)",
          DEFAULT_GRANULE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HARD_RESYNC,
      g_param_spec_boolean ("hard-resync", "Hard Resync",
          "Perform clipping and sample flushing upon discontinuity",
          DEFAULT_HARD_RESYNC, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TOLERANCE,
      g_param_spec_int64 ("tolerance", "Tolerance",
          "Consider discontinuity if timestamp jitter/imperfection exceeds tolerance (ns)",
          0, G_MAXINT64, DEFAULT_TOLERANCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_base_audio_encoder_init (GstBaseAudioEncoder * enc,
    GstBaseAudioEncoderClass * bclass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG_OBJECT (enc, "gst_base_audio_encoder_init");

  enc->priv = GST_BASE_AUDIO_ENCODER_GET_PRIVATE (enc);

  /* only push mode supported */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "sink");
  g_return_if_fail (pad_template != NULL);
  enc->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_encoder_sink_event));
  gst_pad_set_setcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_encoder_sink_setcaps));
  gst_pad_set_getcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_encoder_sink_getcaps));
  gst_pad_set_query_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_encoder_sink_query));
  gst_pad_set_chain_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_encoder_chain));
  gst_pad_set_activatepush_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_encoder_sink_activate_push));
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);

  GST_DEBUG_OBJECT (enc, "sinkpad created");

  /* and we don't mind upstream traveling stuff that much ... */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "src");
  g_return_if_fail (pad_template != NULL);
  enc->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_set_query_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_encoder_src_query));
  gst_pad_set_query_type_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_encoder_get_query_types));
  gst_pad_use_fixed_caps (enc->srcpad);
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);
  GST_DEBUG_OBJECT (enc, "src created");

  enc->priv->adapter = gst_adapter_new ();
  enc->ctx = &enc->priv->ctx;

  /* property default */
  enc->perfect_ts = DEFAULT_PERFECT_TS;
  enc->hard_resync = DEFAULT_HARD_RESYNC;
  enc->tolerance = DEFAULT_TOLERANCE;

  /* init state */
  gst_base_audio_encoder_reset (enc, TRUE);
  GST_DEBUG_OBJECT (enc, "init ok");
}

static void
gst_base_audio_encoder_reset (GstBaseAudioEncoder * enc, gboolean full)
{
  GST_OBJECT_LOCK (enc);

  if (full) {
    enc->priv->active = FALSE;
    enc->priv->samples_in = 0;
    enc->priv->bytes_out = 0;
    g_free (enc->ctx->state.channel_pos);
    memset (enc->ctx, 0, sizeof (enc->ctx));
  }

  gst_segment_init (&enc->segment, GST_FORMAT_TIME);

  gst_adapter_clear (enc->priv->adapter);
  enc->priv->got_data = FALSE;
  enc->priv->drained = TRUE;
  enc->priv->offset = 0;
  enc->priv->base_ts = GST_CLOCK_TIME_NONE;
  enc->priv->base_gp = -1;
  enc->priv->samples = 0;
  enc->priv->discont = FALSE;

  GST_OBJECT_UNLOCK (enc);
}

static void
gst_base_audio_encoder_finalize (GObject * object)
{
  GstBaseAudioEncoder *enc = GST_BASE_AUDIO_ENCODER (object);

  g_object_unref (enc->priv->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_base_audio_encoder_finish_frame:
 * @enc: a #GstBaseAudioEncoder
 * @buffer: encoded data
 * @samples: number of samples (per channel) represented by encoded data
 *
 * Collects encoded data and/or pushes encoded data downstream.
 * Source pad caps must be set when this is called.  Depending on the nature
 * of the (framing of) the format, subclass can decide whether to push
 * encoded data directly or to collect various "frames" in a single buffer.
 * Note that the latter behaviour is recommended whenever the format is allowed,
 * as it incurs no additional latency and avoids otherwise generating a
 * a multitude of (small) output buffers.  If not explicitly pushed,
 * any available encoded data is pushed at the end of each processing cycle,
 * i.e. which encodes as much data as available input data allows.
 *
 * If @samples < 0, then best estimate is all samples provided to encoder
 * (subclass) so far.  @buf may be NULL, in which case next number of @samples
 * are considered discarded, e.g. as a result of discontinuous transmission,
 * and a discontinuity is marked (note that @buf == NULL => push == TRUE).
 *
 * Returns: a #GstFlowReturn that should be escalated to caller (of caller)
 */
GstFlowReturn
gst_base_audio_encoder_finish_frame (GstBaseAudioEncoder * enc, GstBuffer * buf,
    gint samples)
{
  GstBaseAudioEncoderClass *klass;
  GstBaseAudioEncoderPrivate *priv;
  GstBaseAudioEncoderContext *ctx;
  GstFlowReturn ret = GST_FLOW_OK;

  klass = GST_BASE_AUDIO_ENCODER_GET_CLASS (enc);
  priv = enc->priv;
  ctx = enc->ctx;

  /* subclass should know what it is producing by now */
  g_return_val_if_fail (GST_PAD_CAPS (enc->srcpad) != NULL, GST_FLOW_ERROR);
  /* subclass should not hand us no data */
  g_return_val_if_fail (buf == NULL || GST_BUFFER_SIZE (buf) > 0,
      GST_FLOW_ERROR);

  GST_LOG_OBJECT (enc, "accepting %d bytes encoded data as %d samples",
      buf ? GST_BUFFER_SIZE (buf) : -1, samples);

  /* mark subclass still alive and providing */
  priv->got_data = TRUE;

  /* remove corresponding samples from input */
  if (samples < 0)
    samples = (enc->priv->offset / ctx->state.bpf);

  if (G_LIKELY (samples)) {
    /* track upstream ts if so configured */
    if (!enc->perfect_ts) {
      guint64 ts, distance;

      ts = gst_adapter_prev_timestamp (priv->adapter, &distance);
      g_assert (distance % ctx->state.bpf == 0);
      distance /= ctx->state.bpf;
      GST_LOG_OBJECT (enc, "%" G_GUINT64_FORMAT " samples past prev_ts %"
          GST_TIME_FORMAT, distance, GST_TIME_ARGS (ts));
      GST_LOG_OBJECT (enc, "%" G_GUINT64_FORMAT " samples past base_ts %"
          GST_TIME_FORMAT, priv->samples, GST_TIME_ARGS (priv->base_ts));
      /* when draining adapter might be empty and no ts to offer */
      if (GST_CLOCK_TIME_IS_VALID (ts) && ts != priv->base_ts) {
        GstClockTimeDiff diff;
        GstClockTime old_ts, next_ts;

        /* passed into another buffer;
         * mild check for discontinuity and only mark if so */
        next_ts = ts +
            gst_util_uint64_scale (distance, GST_SECOND, ctx->state.rate);
        old_ts = priv->base_ts +
            gst_util_uint64_scale (priv->samples, GST_SECOND, ctx->state.rate);
        diff = GST_CLOCK_DIFF (next_ts, old_ts);
        GST_LOG_OBJECT (enc, "ts diff %d ms", (gint) (diff / GST_MSECOND));
        /* only mark discontinuity if beyond tolerance */
        if (G_UNLIKELY (diff < -enc->tolerance || diff > enc->tolerance)) {
          GST_DEBUG_OBJECT (enc, "marked discont");
          priv->discont = TRUE;
        }
        GST_LOG_OBJECT (enc, "new upstream ts %" GST_TIME_FORMAT
            " at distance %" G_GUINT64_FORMAT, GST_TIME_ARGS (ts), distance);
        /* re-sync to upstream ts */
        priv->base_ts = ts;
        priv->samples = distance;
      }
    }
    /* advance sample view */
    if (G_UNLIKELY (samples * ctx->state.bpf > priv->offset)) {
      if (G_LIKELY (!priv->force)) {
        /* no way we can let this pass */
        g_assert_not_reached ();
        /* really no way */
        goto overflow;
      } else {
        priv->offset = 0;
        if (samples * ctx->state.bpf >= gst_adapter_available (priv->adapter))
          gst_adapter_clear (priv->adapter);
        else
          gst_adapter_flush (priv->adapter, samples * ctx->state.bpf);
      }
    } else {
      gst_adapter_flush (priv->adapter, samples * ctx->state.bpf);
      priv->offset -= samples * ctx->state.bpf;
      /* avoid subsequent stray prev_ts */
      if (G_UNLIKELY (gst_adapter_available (priv->adapter) == 0))
        gst_adapter_clear (priv->adapter);
    }
    /* sample count advanced below after buffer handling */
  }

  /* collect output */
  if (G_LIKELY (buf)) {
    GST_LOG_OBJECT (enc, "taking %d bytes for output", GST_BUFFER_SIZE (buf));
    buf = gst_buffer_make_metadata_writable (buf);

    /* decorate */
    gst_buffer_set_caps (buf, GST_PAD_CAPS (enc->srcpad));
    if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (priv->base_ts))) {
      /* FIXME ? lookahead could lead to weird ts and duration ?
       * (particularly if not in perfect mode) */
      /* mind sample rounding and produce perfect output */
      GST_BUFFER_TIMESTAMP (buf) = priv->base_ts +
          gst_util_uint64_scale (priv->samples - ctx->lookahead, GST_SECOND,
          ctx->state.rate);
      GST_DEBUG_OBJECT (enc, "out samples %d", samples);
      if (G_LIKELY (samples > 0)) {
        priv->samples += samples;
        GST_BUFFER_DURATION (buf) = priv->base_ts +
            gst_util_uint64_scale (priv->samples - ctx->lookahead, GST_SECOND,
            ctx->state.rate) - GST_BUFFER_TIMESTAMP (buf);
        priv->last_duration = GST_BUFFER_DURATION (buf);
      } else {
        /* duration forecast in case of handling remainder;
         * the last one is probably like the previous one ... */
        GST_BUFFER_DURATION (buf) = priv->last_duration;
      }
      if (priv->base_gp >= 0) {
        /* pamper oggmux */
        /* FIXME: in longer run, muxer should take care of this ... */
        /* offset_end = granulepos for ogg muxer */
        GST_BUFFER_OFFSET_END (buf) = priv->base_gp + priv->samples -
            enc->ctx->lookahead;
        /* offset = timestamp corresponding to granulepos for ogg muxer */
        GST_BUFFER_OFFSET (buf) =
            GST_FRAMES_TO_CLOCK_TIME (GST_BUFFER_OFFSET_END (buf),
            ctx->state.rate);
      } else {
        GST_BUFFER_OFFSET (buf) = priv->bytes_out;
        GST_BUFFER_OFFSET_END (buf) = priv->bytes_out + GST_BUFFER_SIZE (buf);
      }
    }

    priv->bytes_out += GST_BUFFER_SIZE (buf);

    if (G_UNLIKELY (priv->discont)) {
      GST_LOG_OBJECT (enc, "marking discont");
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      priv->discont = FALSE;
    }

    if (klass->pre_push) {
      /* last chance for subclass to do some dirty stuff */
      ret = klass->pre_push (enc, &buf);
      if (ret != GST_FLOW_OK || !buf) {
        GST_DEBUG_OBJECT (enc, "subclass returned %s, buf %p",
            gst_flow_get_name (ret), buf);
        if (buf)
          gst_buffer_unref (buf);
        goto exit;
      }
    }

    GST_LOG_OBJECT (enc, "pushing buffer of size %d with ts %" GST_TIME_FORMAT
        ", duration %" GST_TIME_FORMAT, GST_BUFFER_SIZE (buf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    ret = gst_pad_push (enc->srcpad, buf);
    GST_LOG_OBJECT (enc, "buffer pushed: %s", gst_flow_get_name (ret));
  } else {
    /* merely advance samples, most work for that already done above */
    priv->samples += samples;
  }

exit:
  return ret;

  /* ERRORS */
overflow:
  {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE,
        ("received more encoded samples %d than provided %d",
            samples, priv->offset / ctx->state.bpf), (NULL));
    if (buf)
      gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

 /* adapter tracking idea:
  * - start of adapter corresponds with what has already been encoded
  * (i.e. really returned by encoder subclass)
  * - start + offset is what needs to be fed to subclass next */
static GstFlowReturn
gst_base_audio_encoder_push_buffers (GstBaseAudioEncoder * enc, gboolean force)
{
  GstBaseAudioEncoderClass *klass;
  GstBaseAudioEncoderPrivate *priv;
  GstBaseAudioEncoderContext *ctx;
  gint av, need;
  GstBuffer *buf;
  GstFlowReturn ret = GST_FLOW_OK;

  klass = GST_BASE_AUDIO_ENCODER_GET_CLASS (enc);

  g_return_val_if_fail (klass->handle_frame != NULL, GST_FLOW_ERROR);

  priv = enc->priv;
  ctx = enc->ctx;

  while (ret == GST_FLOW_OK) {

    buf = NULL;
    av = gst_adapter_available (priv->adapter);

    g_assert (priv->offset <= av);
    av -= priv->offset;

    need = ctx->frame_samples > 0 ? ctx->frame_samples * ctx->state.bpf : av;
    GST_LOG_OBJECT (enc, "available: %d, needed: %d, force: %d",
        av, need, force);

    if ((need > av) || !av) {
      if (G_UNLIKELY (force)) {
        priv->force = TRUE;
        need = av;
      } else {
        break;
      }
    } else {
      priv->force = FALSE;
    }

    /* if we have some extra metadata,
     * provide for integer multiple of frames to allow for better granularity
     * of processing */
    if (ctx->frame_samples > 0 && need) {
      if (ctx->frame_max > 1)
        need = need * MIN ((av / need), ctx->frame_max);
      else if (ctx->frame_max == 0)
        need = need * (av / need);
    }

    if (need) {
      buf = gst_buffer_new ();
      GST_BUFFER_DATA (buf) = (guint8 *)
          gst_adapter_peek (priv->adapter, priv->offset + need) + priv->offset;
      GST_BUFFER_SIZE (buf) = need;
    }

    GST_LOG_OBJECT (enc, "providing subclass with %d bytes at offset %d",
        need, priv->offset);

    /* mark this already as consumed,
     * which it should be when subclass gives us data in exchange for samples */
    priv->offset += need;
    priv->samples_in += need / ctx->state.bpf;

    priv->got_data = FALSE;
    ret = klass->handle_frame (enc, buf);

    if (G_LIKELY (buf))
      gst_buffer_unref (buf);

    /* no data to feed, no leftover provided, then bail out */
    if (G_UNLIKELY (!buf && !priv->got_data)) {
      priv->drained = TRUE;
      GST_LOG_OBJECT (enc, "no more data drained from subclass");
      break;
    }
  }

  return ret;
}

static GstFlowReturn
gst_base_audio_encoder_drain (GstBaseAudioEncoder * enc)
{
  if (enc->priv->drained)
    return GST_FLOW_OK;
  else
    return gst_base_audio_encoder_push_buffers (enc, TRUE);
}

static GstFlowReturn
gst_base_audio_encoder_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBaseAudioEncoderClass *bclass;
  GstBaseAudioEncoder *enc;
  GstBaseAudioEncoderPrivate *priv;
  GstBaseAudioEncoderContext *ctx;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean discont;

  enc = GST_BASE_AUDIO_ENCODER (GST_OBJECT_PARENT (pad));
  bclass = GST_BASE_AUDIO_ENCODER_GET_CLASS (enc);

  priv = enc->priv;
  ctx = enc->ctx;

  /* should know what is coming by now */
  if (!ctx->state.bpf)
    goto not_negotiated;

  GST_LOG_OBJECT (enc,
      "received buffer of size %d with ts %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT, GST_BUFFER_SIZE (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  /* input shoud be whole number of sample frames */
  if (GST_BUFFER_SIZE (buffer) % ctx->state.bpf)
    goto wrong_buffer;

#ifndef GST_DISABLE_GST_DEBUG
  {
    GstClockTime duration;
    GstClockTimeDiff diff;

    /* verify buffer duration */
    duration = gst_util_uint64_scale (GST_BUFFER_SIZE (buffer), GST_SECOND,
        ctx->state.rate * ctx->state.bpf);
    diff = GST_CLOCK_DIFF (duration, GST_BUFFER_DURATION (buffer));
    if (GST_BUFFER_DURATION (buffer) != GST_CLOCK_TIME_NONE &&
        (diff > GST_SECOND / ctx->state.rate / 2 ||
            diff < -GST_SECOND / ctx->state.rate / 2)) {
      GST_DEBUG_OBJECT (enc, "incoming buffer had incorrect duration %"
          GST_TIME_FORMAT ", expected duration %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
          GST_TIME_ARGS (duration));
    }
  }
#endif

  discont = GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  if (G_UNLIKELY (discont)) {
    GST_LOG_OBJECT (buffer, "marked discont");
    enc->priv->discont = discont;
  }

  /* clip to segment */
  /* NOTE: slightly painful linking -laudio only for this one ... */
  buffer = gst_audio_buffer_clip (buffer, &enc->segment, ctx->state.rate,
      ctx->state.bpf);
  if (G_UNLIKELY (!buffer)) {
    GST_DEBUG_OBJECT (buffer, "no data after clipping to segment");
    goto done;
  }

  GST_LOG_OBJECT (enc,
      "buffer after segment clipping has size %d with ts %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT, GST_BUFFER_SIZE (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  if (!GST_CLOCK_TIME_IS_VALID (priv->base_ts)) {
    priv->base_ts = GST_BUFFER_TIMESTAMP (buffer);
    GST_DEBUG_OBJECT (enc, "new base ts %" GST_TIME_FORMAT,
        GST_TIME_ARGS (priv->base_ts));
    if (enc->granule) {
      priv->base_gp =
          GST_CLOCK_TIME_TO_FRAMES (priv->base_ts, enc->ctx->state.rate);
      GST_DEBUG_OBJECT (enc, "new base gp %" G_GINT64_FORMAT,
          GST_TIME_ARGS (priv->base_gp));
    }
  }

  /* check for continuity;
   * checked elsewhere in non-perfect case */
  if (enc->perfect_ts) {
    GstClockTimeDiff diff = 0;
    GstClockTime next_ts = 0;

    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer) &&
        GST_CLOCK_TIME_IS_VALID (priv->base_ts)) {
      guint64 samples;

      samples = priv->samples +
          gst_adapter_available (priv->adapter) / ctx->state.bpf;
      next_ts = priv->base_ts +
          gst_util_uint64_scale (samples, GST_SECOND, ctx->state.rate);
      GST_LOG_OBJECT (enc, "buffer is %" G_GUINT64_FORMAT
          " samples past base_ts %" GST_TIME_FORMAT
          ", expected ts %" GST_TIME_FORMAT, samples,
          GST_TIME_ARGS (priv->base_ts), GST_TIME_ARGS (next_ts));
      diff = GST_CLOCK_DIFF (next_ts, GST_BUFFER_TIMESTAMP (buffer));
      GST_LOG_OBJECT (enc, "ts diff %d ms", (gint) (diff / GST_MSECOND));
      /* if within tolerance,
       * discard buffer ts and carry on producing perfect stream,
       * otherwise clip or resync to ts */
      if (G_UNLIKELY (diff < -enc->tolerance || diff > enc->tolerance)) {
        GST_DEBUG_OBJECT (enc, "marked discont");
        discont = TRUE;
      }
    }

    /* do some fancy tweaking in hard resync case */
    if (discont && enc->hard_resync) {
      if (diff < 0) {
        guint64 diff_bytes;

        GST_WARNING_OBJECT (enc, "Buffer is older than expected ts %"
            GST_TIME_FORMAT ".  Clipping buffer", GST_TIME_ARGS (next_ts));

        diff_bytes =
            GST_CLOCK_TIME_TO_FRAMES (-diff, ctx->state.rate) * ctx->state.bpf;
        if (diff_bytes >= GST_BUFFER_SIZE (buffer)) {
          gst_buffer_unref (buffer);
          goto done;
        }
        buffer = gst_buffer_make_metadata_writable (buffer);
        GST_BUFFER_DATA (buffer) += diff_bytes;
        GST_BUFFER_SIZE (buffer) -= diff_bytes;

        GST_BUFFER_TIMESTAMP (buffer) += diff;
        /* care even less about duration after this */
      } else {
        /* drain stuff prior to resync */
        gst_base_audio_encoder_drain (enc);
      }
    }
    /* now re-sync ts */
    priv->base_ts += diff;
    if (priv->base_gp >= 0)
      priv->base_gp =
          GST_CLOCK_TIME_TO_FRAMES (priv->base_ts, enc->ctx->state.rate);
    priv->discont |= discont;
  }

  gst_adapter_push (enc->priv->adapter, buffer);
  /* new stuff, so we can push subclass again */
  enc->priv->drained = FALSE;

  ret = gst_base_audio_encoder_push_buffers (enc, FALSE);

done:
  GST_LOG_OBJECT (enc, "chain leaving");
  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (enc, CORE, NEGOTIATION, (NULL),
        ("encoder not initialized"));
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_NEGOTIATED;
  }
wrong_buffer:
  {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
        ("buffer size %d not a multiple of %d", GST_BUFFER_SIZE (buffer),
            ctx->state.bpf));
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_base_audio_encoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseAudioEncoder *enc;
  GstBaseAudioEncoderClass *klass;
  GstBaseAudioEncoderContext *ctx;
  GstAudioState *state;
  gboolean res = TRUE, changed = FALSE;

  enc = GST_BASE_AUDIO_ENCODER (GST_PAD_PARENT (pad));
  klass = GST_BASE_AUDIO_ENCODER_GET_CLASS (enc);

  /* subclass must do something here ... */
  g_return_val_if_fail (klass->set_format != NULL, FALSE);

  ctx = enc->ctx;
  state = &ctx->state;

  GST_DEBUG_OBJECT (enc, "caps: %" GST_PTR_FORMAT, caps);

  if (!gst_caps_is_fixed (caps))
    goto refuse_caps;

  /* adjust ts tracking to new sample rate */
  if (GST_CLOCK_TIME_IS_VALID (enc->priv->base_ts) && state->rate) {
    enc->priv->base_ts +=
        GST_FRAMES_TO_CLOCK_TIME (enc->priv->samples, state->rate);
    enc->priv->samples = 0;
  }

  if (!gst_base_audio_parse_caps (caps, state, &changed))
    goto refuse_caps;

  if (changed) {
    GstClockTime old_min_latency;
    GstClockTime old_max_latency;

    /* drain any pending old data stuff */
    gst_base_audio_encoder_drain (enc);

    /* context defaults */
    enc->ctx->frame_samples = 0;
    enc->ctx->frame_max = 0;
    enc->ctx->lookahead = 0;

    /* element might report latency */
    GST_OBJECT_LOCK (enc);
    old_min_latency = ctx->min_latency;
    old_max_latency = ctx->max_latency;
    GST_OBJECT_UNLOCK (enc);

    if (klass->set_format)
      res = klass->set_format (enc, state);

    /* notify if new latency */
    GST_OBJECT_LOCK (enc);
    if ((ctx->min_latency > 0 && ctx->min_latency != old_min_latency) ||
        (ctx->max_latency > 0 && ctx->max_latency != old_max_latency)) {
      GST_OBJECT_UNLOCK (enc);
      /* post latency message on the bus */
      gst_element_post_message (GST_ELEMENT (enc),
          gst_message_new_latency (GST_OBJECT (enc)));
      GST_OBJECT_LOCK (enc);
    }
    GST_OBJECT_UNLOCK (enc);
  } else {
    GST_DEBUG_OBJECT (enc, "new audio format identical to configured format");
  }

  return res;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (enc, "rejected caps %" GST_PTR_FORMAT, caps);
    return res;
  }
}


/**
 * gst_base_audio_encoder_proxy_getcaps:
 * @enc: a #GstBaseAudioEncoder
 * @caps: initial
 *
 * Returns caps that express @caps (or sink template caps if @caps == NULL)
 * restricted to channel/rate combinations supported by downstream elements
 * (e.g. muxers).
 *
 * Returns: a #GstCaps owned by caller
 */
GstCaps *
gst_base_audio_encoder_proxy_getcaps (GstBaseAudioEncoder * enc, GstCaps * caps)
{
  const GstCaps *templ_caps;
  GstCaps *allowed = NULL;
  GstCaps *fcaps, *filter_caps;
  gint i, j;

  /* we want to be able to communicate to upstream elements like audioconvert
   * and audioresample any rate/channel restrictions downstream (e.g. muxer
   * only accepting certain sample rates) */
  templ_caps = caps ? caps : gst_pad_get_pad_template_caps (enc->sinkpad);
  allowed = gst_pad_get_allowed_caps (enc->srcpad);
  if (!allowed || gst_caps_is_empty (allowed) || gst_caps_is_any (allowed)) {
    fcaps = gst_caps_copy (templ_caps);
    goto done;
  }

  GST_LOG_OBJECT (enc, "template caps %" GST_PTR_FORMAT, templ_caps);
  GST_LOG_OBJECT (enc, "allowed caps %" GST_PTR_FORMAT, allowed);

  filter_caps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (templ_caps); i++) {
    GQuark q_name;

    q_name = gst_structure_get_name_id (gst_caps_get_structure (templ_caps, i));

    /* pick rate + channel fields from allowed caps */
    for (j = 0; j < gst_caps_get_size (allowed); j++) {
      const GstStructure *allowed_s = gst_caps_get_structure (allowed, j);
      const GValue *val;
      GstStructure *s;

      s = gst_structure_id_empty_new (q_name);
      if ((val = gst_structure_get_value (allowed_s, "rate")))
        gst_structure_set_value (s, "rate", val);
      if ((val = gst_structure_get_value (allowed_s, "channels")))
        gst_structure_set_value (s, "channels", val);

      gst_caps_merge_structure (filter_caps, s);
    }
  }

  fcaps = gst_caps_intersect (filter_caps, templ_caps);
  gst_caps_unref (filter_caps);

done:
  gst_caps_replace (&allowed, NULL);

  GST_LOG_OBJECT (enc, "proxy caps %" GST_PTR_FORMAT, fcaps);

  return fcaps;
}

static GstCaps *
gst_base_audio_encoder_sink_getcaps (GstPad * pad)
{
  GstBaseAudioEncoder *enc;
  GstBaseAudioEncoderClass *klass;
  GstCaps *caps;

  enc = GST_BASE_AUDIO_ENCODER (gst_pad_get_parent (pad));
  klass = GST_BASE_AUDIO_ENCODER_GET_CLASS (enc);
  g_assert (pad == enc->sinkpad);

  if (klass->getcaps)
    caps = klass->getcaps (enc);
  else
    caps = gst_base_audio_encoder_proxy_getcaps (enc, NULL);
  gst_object_unref (enc);

  GST_LOG_OBJECT (enc, "returning caps %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_base_audio_encoder_sink_eventfunc (GstBaseAudioEncoder * enc,
    GstEvent * event)
{
  GstBaseAudioEncoderClass *klass;
  gboolean handled = FALSE;

  klass = GST_BASE_AUDIO_ENCODER_GET_CLASS (enc);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      if (format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (enc, "received TIME NEW_SEGMENT %" GST_TIME_FORMAT
            " -- %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT
            ", rate %g, applied_rate %g",
            GST_TIME_ARGS (start), GST_TIME_ARGS (stop), GST_TIME_ARGS (time),
            rate, arate);
      } else {
        GST_DEBUG_OBJECT (enc, "received NEW_SEGMENT %" G_GINT64_FORMAT
            " -- %" G_GINT64_FORMAT ", time %" G_GINT64_FORMAT
            ", rate %g, applied_rate %g", start, stop, time, rate, arate);
        GST_DEBUG_OBJECT (enc, "unsupported format; ignoring");
        break;
      }

      /* finish current segment */
      gst_base_audio_encoder_drain (enc);
      /* reset partially for new segment */
      gst_base_audio_encoder_reset (enc, FALSE);
      /* and follow along with segment */
      gst_segment_set_newsegment_full (&enc->segment, update, rate, arate,
          format, start, stop, time);
      break;
    }

    case GST_EVENT_FLUSH_START:
      break;

    case GST_EVENT_FLUSH_STOP:
      /* discard any pending stuff */
      /* TODO route through drain ?? */
      if (!enc->priv->drained && klass->flush)
        klass->flush (enc);
      /* and get (re)set for the sequel */
      gst_base_audio_encoder_reset (enc, FALSE);
      break;

    case GST_EVENT_EOS:
      gst_base_audio_encoder_drain (enc);
      break;

    default:
      break;
  }

  return handled;
}

static gboolean
gst_base_audio_encoder_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseAudioEncoder *enc;
  GstBaseAudioEncoderClass *klass;
  gboolean handled = FALSE;
  gboolean ret = TRUE;

  enc = GST_BASE_AUDIO_ENCODER (gst_pad_get_parent (pad));
  klass = GST_BASE_AUDIO_ENCODER_GET_CLASS (enc);

  GST_DEBUG_OBJECT (enc, "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  if (klass->event)
    handled = klass->event (enc, event);

  if (!handled)
    handled = gst_base_audio_encoder_sink_eventfunc (enc, event);

  if (!handled)
    ret = gst_pad_event_default (pad, event);

  GST_DEBUG_OBJECT (enc, "event handled");

  gst_object_unref (enc);
  return ret;
}

static gboolean
gst_base_audio_encoder_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstBaseAudioEncoder *enc;

  enc = GST_BASE_AUDIO_ENCODER (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_FORMATS:
    {
      gst_query_set_formats (query, 3,
          GST_FORMAT_TIME, GST_FORMAT_BYTES, GST_FORMAT_DEFAULT);
      res = TRUE;
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res = gst_base_audio_raw_audio_convert (&enc->ctx->state,
                  src_fmt, src_val, &dest_fmt, &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

error:
  gst_object_unref (enc);
  return res;
}

static const GstQueryType *
gst_base_audio_encoder_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_base_audio_encoder_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_LATENCY,
    0
  };

  return gst_base_audio_encoder_src_query_types;
}

/* FIXME ? are any of these queries (other than latency) an encoder's business
 * also, the conversion stuff might seem to make sense, but seems to not mind
 * segment stuff etc at all
 * Supposedly that's backward compatibility ... */
static gboolean
gst_base_audio_encoder_src_query (GstPad * pad, GstQuery * query)
{
  GstBaseAudioEncoder *enc;
  GstPad *peerpad;
  gboolean res = FALSE;

  enc = GST_BASE_AUDIO_ENCODER (GST_PAD_PARENT (pad));
  peerpad = gst_pad_get_peer (GST_PAD (enc->sinkpad));

  GST_LOG_OBJECT (enc, "handling query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat fmt, req_fmt;
      gint64 pos, val;

      if ((res = gst_pad_peer_query (enc->sinkpad, query))) {
        GST_LOG_OBJECT (enc, "returning peer response");
        break;
      }

      if (!peerpad) {
        GST_LOG_OBJECT (enc, "no peer");
        break;
      }

      gst_query_parse_position (query, &req_fmt, NULL);
      fmt = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_position (peerpad, &fmt, &pos)))
        break;

      if ((res = gst_pad_query_convert (peerpad, fmt, pos, &req_fmt, &val))) {
        gst_query_set_position (query, req_fmt, val);
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat fmt, req_fmt;
      gint64 dur, val;

      if ((res = gst_pad_peer_query (enc->sinkpad, query))) {
        GST_LOG_OBJECT (enc, "returning peer response");
        break;
      }

      if (!peerpad) {
        GST_LOG_OBJECT (enc, "no peer");
        break;
      }

      gst_query_parse_duration (query, &req_fmt, NULL);
      fmt = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_duration (peerpad, &fmt, &dur)))
        break;

      if ((res = gst_pad_query_convert (peerpad, fmt, dur, &req_fmt, &val))) {
        gst_query_set_duration (query, req_fmt, val);
      }
      break;
    }
    case GST_QUERY_FORMATS:
    {
      gst_query_set_formats (query, 2, GST_FORMAT_TIME, GST_FORMAT_BYTES);
      res = TRUE;
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res = gst_base_audio_encoded_audio_convert (&enc->ctx->state,
                  enc->priv->bytes_out, enc->priv->samples_in, src_fmt, src_val,
                  &dest_fmt, &dest_val)))
        break;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      if ((res = gst_pad_peer_query (enc->sinkpad, query))) {
        gboolean live;
        GstClockTime min_latency, max_latency;

        gst_query_parse_latency (query, &live, &min_latency, &max_latency);
        GST_DEBUG_OBJECT (enc, "Peer latency: live %d, min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT, live,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        GST_OBJECT_LOCK (enc);
        /* add our latency */
        if (min_latency != -1)
          min_latency += enc->ctx->min_latency;
        if (max_latency != -1)
          max_latency += enc->ctx->max_latency;
        GST_OBJECT_UNLOCK (enc);

        gst_query_set_latency (query, live, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (peerpad);
  return res;
}

static void
gst_base_audio_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseAudioEncoder *enc;

  enc = GST_BASE_AUDIO_ENCODER (object);

  switch (prop_id) {
    case PROP_PERFECT_TS:
      if (enc->granule && !g_value_get_boolean (value))
        GST_WARNING_OBJECT (enc, "perfect-ts can not be set FALSE");
      else
        enc->perfect_ts = g_value_get_boolean (value);
      break;
    case PROP_HARD_RESYNC:
      enc->hard_resync = g_value_get_boolean (value);
      break;
    case PROP_TOLERANCE:
      enc->tolerance = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_audio_encoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseAudioEncoder *enc;

  enc = GST_BASE_AUDIO_ENCODER (object);

  switch (prop_id) {
    case PROP_PERFECT_TS:
      g_value_set_boolean (value, enc->perfect_ts);
      break;
    case PROP_GRANULE:
      g_value_set_boolean (value, enc->granule);
      break;
    case PROP_HARD_RESYNC:
      g_value_set_boolean (value, enc->hard_resync);
      break;
    case PROP_TOLERANCE:
      g_value_set_int64 (value, enc->tolerance);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_base_audio_encoder_activate (GstBaseAudioEncoder * enc, gboolean active)
{
  GstBaseAudioEncoderClass *klass;
  gboolean result = FALSE;

  klass = GST_BASE_AUDIO_ENCODER_GET_CLASS (enc);

  g_return_val_if_fail (!enc->granule || enc->perfect_ts, FALSE);

  GST_DEBUG_OBJECT (enc, "activate %d", active);

  if (active) {
    if (!enc->priv->active && klass->start)
      result = klass->start (enc);
  } else {
    /* We must make sure streaming has finished before resetting things
     * and calling the ::stop vfunc */
    GST_PAD_STREAM_LOCK (enc->sinkpad);
    GST_PAD_STREAM_UNLOCK (enc->sinkpad);

    if (enc->priv->active && klass->stop)
      result = klass->stop (enc);

    /* clean up */
    gst_base_audio_encoder_reset (enc, TRUE);
  }
  GST_DEBUG_OBJECT (enc, "activate return: %d", result);
  return result;
}


static gboolean
gst_base_audio_encoder_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstBaseAudioEncoder *enc;

  enc = GST_BASE_AUDIO_ENCODER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (enc, "sink activate push %d", active);

  result = gst_base_audio_encoder_activate (enc, active);

  if (result)
    enc->priv->active = active;

  GST_DEBUG_OBJECT (enc, "sink activate push return: %d", result);

  gst_object_unref (enc);
  return result;
}
