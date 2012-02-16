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
 * SECTION:gstaudioencoder
 * @short_description: Base class for audio encoders
 * @see_also: #GstBaseTransform
 * @since: 0.10.36
 *
 * This base class is for audio encoders turning raw audio samples into
 * encoded audio data.
 *
 * GstAudioEncoder and subclass should cooperate as follows.
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Configuration</title>
 *   <listitem><para>
 *     Initially, GstAudioEncoder calls @start when the encoder element
 *     is activated, which allows subclass to perform any global setup.
 *   </para></listitem>
 *   <listitem><para>
 *     GstAudioEncoder calls @set_format to inform subclass of the format
 *     of input audio data that it is about to receive.  Subclass should
 *     setup for encoding and configure various base class parameters
 *     appropriately, notably those directing desired input data handling.
 *     While unlikely, it might be called more than once, if changing input
 *     parameters require reconfiguration.
 *   </para></listitem>
 *   <listitem><para>
 *     GstAudioEncoder calls @stop at end of all processing.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * As of configuration stage, and throughout processing, GstAudioEncoder
 * maintains various parameters that provide required context,
 * e.g. describing the format of input audio data.
 * Conversely, subclass can and should configure these context parameters
 * to inform base class of its expectation w.r.t. buffer handling.
 * <listitem>
 *   <itemizedlist>
 *   <title>Data processing</title>
 *     <listitem><para>
 *       Base class gathers input sample data (as directed by the context's
 *       frame_samples and frame_max) and provides this to subclass' @handle_frame.
 *     </para></listitem>
 *     <listitem><para>
 *       If codec processing results in encoded data, subclass should call
 *       @gst_audio_encoder_finish_frame to have encoded data pushed
 *       downstream.  Alternatively, it might also call to indicate dropped
 *       (non-encoded) samples.
 *     </para></listitem>
 *     <listitem><para>
 *       Just prior to actually pushing a buffer downstream,
 *       it is passed to @pre_push.
 *     </para></listitem>
 *     <listitem><para>
 *       During the parsing process GstAudioEncoderClass will handle both
 *       srcpad and sinkpad events. Sink events will be passed to subclass
 *       if @event callback has been provided.
 *     </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Shutdown phase</title>
 *   <listitem><para>
 *     GstAudioEncoder class calls @stop to inform the subclass that data
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
 * it might be delayed until calling @gst_audio_encoder_finish_frame.
 *
 * In summary, above process should have subclass concentrating on
 * codec data processing while leaving other matters to base class,
 * such as most notably timestamp handling.  While it may exert more control
 * in this area (see e.g. @pre_push), it is very much not recommended.
 *
 * In particular, base class will either favor tracking upstream timestamps
 * (at the possible expense of jitter) or aim to arrange for a perfect stream of
 * output timestamps, depending on #GstAudioEncoder:perfect-timestamp.
 * However, in the latter case, the input may not be so perfect or ideal, which
 * is handled as follows.  An input timestamp is compared with the expected
 * timestamp as dictated by input sample stream and if the deviation is less
 * than #GstAudioEncoder:tolerance, the deviation is discarded.
 * Otherwise, it is considered a discontuinity and subsequent output timestamp
 * is resynced to the new position after performing configured discontinuity
 * processing.  In the non-perfect-timestamp case, an upstream variation
 * exceeding tolerance only leads to marking DISCONT on subsequent outgoing
 * (while timestamps are adjusted to upstream regardless of variation).
 * While DISCONT is also marked in the perfect-timestamp case, this one
 * optionally (see #GstAudioEncoder:hard-resync)
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
 *      @gst_audio_encoder_finish_frame.
 *   </para></listitem>
 * </itemizedlist>
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gstaudioencoder.h"
#include <gst/base/gstadapter.h>
#include <gst/audio/audio.h>
#include <gst/pbutils/descriptions.h>

#include <stdlib.h>
#include <string.h>


GST_DEBUG_CATEGORY_STATIC (gst_audio_encoder_debug);
#define GST_CAT_DEFAULT gst_audio_encoder_debug

#define GST_AUDIO_ENCODER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_AUDIO_ENCODER, \
        GstAudioEncoderPrivate))

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
#define DEFAULT_HARD_MIN     FALSE
#define DEFAULT_DRAINABLE    TRUE

typedef struct _GstAudioEncoderContext
{
  /* input */
  GstAudioInfo info;

  /* output */
  gint frame_samples_min, frame_samples_max;
  gint frame_max;
  gint lookahead;
  /* MT-protected (with LOCK) */
  GstClockTime min_latency;
  GstClockTime max_latency;
} GstAudioEncoderContext;

struct _GstAudioEncoderPrivate
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
  GstAudioEncoderContext ctx;

  /* properties */
  gint64 tolerance;
  gboolean perfect_ts;
  gboolean hard_resync;
  gboolean granule;
  gboolean hard_min;
  gboolean drainable;

  /* pending tags */
  GstTagList *tags;
  /* pending serialized sink events, will be sent from finish_frame() */
  GList *pending_events;
};


static GstElementClass *parent_class = NULL;

static void gst_audio_encoder_class_init (GstAudioEncoderClass * klass);
static void gst_audio_encoder_init (GstAudioEncoder * parse,
    GstAudioEncoderClass * klass);

GType
gst_audio_encoder_get_type (void)
{
  static GType audio_encoder_type = 0;

  if (!audio_encoder_type) {
    static const GTypeInfo audio_encoder_info = {
      sizeof (GstAudioEncoderClass),
      (GBaseInitFunc) NULL,
      (GBaseFinalizeFunc) NULL,
      (GClassInitFunc) gst_audio_encoder_class_init,
      NULL,
      NULL,
      sizeof (GstAudioEncoder),
      0,
      (GInstanceInitFunc) gst_audio_encoder_init,
    };
    const GInterfaceInfo preset_interface_info = {
      NULL,                     /* interface_init */
      NULL,                     /* interface_finalize */
      NULL                      /* interface_data */
    };

    audio_encoder_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAudioEncoder", &audio_encoder_info, G_TYPE_FLAG_ABSTRACT);

    g_type_add_interface_static (audio_encoder_type, GST_TYPE_PRESET,
        &preset_interface_info);
  }
  return audio_encoder_type;
}

static void gst_audio_encoder_finalize (GObject * object);
static void gst_audio_encoder_reset (GstAudioEncoder * enc, gboolean full);

static void gst_audio_encoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_encoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_audio_encoder_sink_activate_push (GstPad * pad,
    gboolean active);

static gboolean gst_audio_encoder_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_audio_encoder_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_audio_encoder_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_audio_encoder_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_audio_encoder_sink_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_audio_encoder_get_query_types (GstPad * pad);
static GstCaps *gst_audio_encoder_sink_getcaps (GstPad * pad);


static void
gst_audio_encoder_class_init (GstAudioEncoderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (gst_audio_encoder_debug, "audioencoder", 0,
      "audio encoder base class");

  g_type_class_add_private (klass, sizeof (GstAudioEncoderPrivate));

  gobject_class->set_property = gst_audio_encoder_set_property;
  gobject_class->get_property = gst_audio_encoder_get_property;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_audio_encoder_finalize);

  /* properties */
  g_object_class_install_property (gobject_class, PROP_PERFECT_TS,
      g_param_spec_boolean ("perfect-timestamp", "Perfect Timestamps",
          "Favour perfect timestamps over tracking upstream timestamps",
          DEFAULT_PERFECT_TS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_GRANULE,
      g_param_spec_boolean ("mark-granule", "Granule Marking",
          "Apply granule semantics to buffer metadata (implies perfect-timestamp)",
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
gst_audio_encoder_init (GstAudioEncoder * enc, GstAudioEncoderClass * bclass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG_OBJECT (enc, "gst_audio_encoder_init");

  enc->priv = GST_AUDIO_ENCODER_GET_PRIVATE (enc);

  /* only push mode supported */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "sink");
  g_return_if_fail (pad_template != NULL);
  enc->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_encoder_sink_event));
  gst_pad_set_setcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_encoder_sink_setcaps));
  gst_pad_set_getcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_encoder_sink_getcaps));
  gst_pad_set_query_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_encoder_sink_query));
  gst_pad_set_chain_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_encoder_chain));
  gst_pad_set_activatepush_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_audio_encoder_sink_activate_push));
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);

  GST_DEBUG_OBJECT (enc, "sinkpad created");

  /* and we don't mind upstream traveling stuff that much ... */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "src");
  g_return_if_fail (pad_template != NULL);
  enc->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_set_query_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_encoder_src_query));
  gst_pad_set_query_type_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_audio_encoder_get_query_types));
  gst_pad_use_fixed_caps (enc->srcpad);
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);
  GST_DEBUG_OBJECT (enc, "src created");

  enc->priv->adapter = gst_adapter_new ();

  g_static_rec_mutex_init (&enc->stream_lock);

  /* property default */
  enc->priv->granule = DEFAULT_GRANULE;
  enc->priv->perfect_ts = DEFAULT_PERFECT_TS;
  enc->priv->hard_resync = DEFAULT_HARD_RESYNC;
  enc->priv->tolerance = DEFAULT_TOLERANCE;
  enc->priv->hard_min = DEFAULT_HARD_MIN;
  enc->priv->drainable = DEFAULT_DRAINABLE;

  /* init state */
  gst_audio_encoder_reset (enc, TRUE);
  GST_DEBUG_OBJECT (enc, "init ok");
}

static void
gst_audio_encoder_reset (GstAudioEncoder * enc, gboolean full)
{
  GST_AUDIO_ENCODER_STREAM_LOCK (enc);

  GST_LOG_OBJECT (enc, "reset full %d", full);

  if (full) {
    enc->priv->active = FALSE;
    enc->priv->samples_in = 0;
    enc->priv->bytes_out = 0;
    gst_audio_info_clear (&enc->priv->ctx.info);
    memset (&enc->priv->ctx, 0, sizeof (enc->priv->ctx));

    if (enc->priv->tags)
      gst_tag_list_free (enc->priv->tags);
    enc->priv->tags = NULL;

    g_list_foreach (enc->priv->pending_events, (GFunc) gst_event_unref, NULL);
    g_list_free (enc->priv->pending_events);
    enc->priv->pending_events = NULL;
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

  GST_AUDIO_ENCODER_STREAM_UNLOCK (enc);
}

static void
gst_audio_encoder_finalize (GObject * object)
{
  GstAudioEncoder *enc = GST_AUDIO_ENCODER (object);

  g_object_unref (enc->priv->adapter);

  g_static_rec_mutex_free (&enc->stream_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_audio_encoder_finish_frame:
 * @enc: a #GstAudioEncoder
 * @buffer: encoded data
 * @samples: number of samples (per channel) represented by encoded data
 *
 * Collects encoded data and pushes encoded data downstream.
 * Source pad caps must be set when this is called.
 *
 * If @samples < 0, then best estimate is all samples provided to encoder
 * (subclass) so far.  @buf may be NULL, in which case next number of @samples
 * are considered discarded, e.g. as a result of discontinuous transmission,
 * and a discontinuity is marked.
 *
 * Note that samples received in gst_audio_encoder_handle_frame()
 * may be invalidated by a call to this function.
 *
 * Returns: a #GstFlowReturn that should be escalated to caller (of caller)
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_audio_encoder_finish_frame (GstAudioEncoder * enc, GstBuffer * buf,
    gint samples)
{
  GstAudioEncoderClass *klass;
  GstAudioEncoderPrivate *priv;
  GstAudioEncoderContext *ctx;
  GstFlowReturn ret = GST_FLOW_OK;

  klass = GST_AUDIO_ENCODER_GET_CLASS (enc);
  priv = enc->priv;
  ctx = &enc->priv->ctx;

  /* subclass should know what it is producing by now */
  g_return_val_if_fail (GST_PAD_CAPS (enc->srcpad) != NULL, GST_FLOW_ERROR);
  /* subclass should not hand us no data */
  g_return_val_if_fail (buf == NULL || GST_BUFFER_SIZE (buf) > 0,
      GST_FLOW_ERROR);

  GST_AUDIO_ENCODER_STREAM_LOCK (enc);

  GST_LOG_OBJECT (enc, "accepting %d bytes encoded data as %d samples",
      buf ? GST_BUFFER_SIZE (buf) : -1, samples);

  /* mark subclass still alive and providing */
  if (G_LIKELY (buf))
    priv->got_data = TRUE;

  if (priv->pending_events) {
    GList *pending_events, *l;

    pending_events = priv->pending_events;
    priv->pending_events = NULL;

    GST_DEBUG_OBJECT (enc, "Pushing pending events");
    for (l = pending_events; l; l = l->next)
      gst_pad_push_event (enc->srcpad, l->data);
    g_list_free (pending_events);
  }

  /* send after pending events, which likely includes newsegment event */
  if (G_UNLIKELY (enc->priv->tags)) {
    GstTagList *tags;

    /* add codec info to pending tags */
    tags = enc->priv->tags;
    /* no more pending */
    enc->priv->tags = NULL;
    gst_pb_utils_add_codec_description_to_tag_list (tags, GST_TAG_CODEC,
        GST_PAD_CAPS (enc->srcpad));
    gst_pb_utils_add_codec_description_to_tag_list (tags, GST_TAG_AUDIO_CODEC,
        GST_PAD_CAPS (enc->srcpad));
    GST_DEBUG_OBJECT (enc, "sending tags %" GST_PTR_FORMAT, tags);
    gst_element_found_tags_for_pad (GST_ELEMENT (enc), enc->srcpad, tags);
  }

  /* remove corresponding samples from input */
  if (samples < 0)
    samples = (enc->priv->offset / ctx->info.bpf);

  if (G_LIKELY (samples)) {
    /* track upstream ts if so configured */
    if (!enc->priv->perfect_ts) {
      guint64 ts, distance;

      ts = gst_adapter_prev_timestamp (priv->adapter, &distance);
      g_assert (distance % ctx->info.bpf == 0);
      distance /= ctx->info.bpf;
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
            gst_util_uint64_scale (distance, GST_SECOND, ctx->info.rate);
        old_ts = priv->base_ts +
            gst_util_uint64_scale (priv->samples, GST_SECOND, ctx->info.rate);
        diff = GST_CLOCK_DIFF (next_ts, old_ts);
        GST_LOG_OBJECT (enc, "ts diff %d ms", (gint) (diff / GST_MSECOND));
        /* only mark discontinuity if beyond tolerance */
        if (G_UNLIKELY (diff < -enc->priv->tolerance ||
                diff > enc->priv->tolerance)) {
          GST_DEBUG_OBJECT (enc, "marked discont");
          priv->discont = TRUE;
        }
        if (diff > GST_SECOND / ctx->info.rate / 2 ||
            diff < -GST_SECOND / ctx->info.rate / 2) {
          GST_LOG_OBJECT (enc, "new upstream ts %" GST_TIME_FORMAT
              " at distance %" G_GUINT64_FORMAT, GST_TIME_ARGS (ts), distance);
          /* re-sync to upstream ts */
          priv->base_ts = ts;
          priv->samples = distance;
        } else {
          GST_LOG_OBJECT (enc, "new upstream ts only introduces jitter");
        }
      }
    }
    /* advance sample view */
    if (G_UNLIKELY (samples * ctx->info.bpf > priv->offset)) {
      if (G_LIKELY (!priv->force)) {
        /* no way we can let this pass */
        g_assert_not_reached ();
        /* really no way */
        goto overflow;
      } else {
        priv->offset = 0;
        if (samples * ctx->info.bpf >= gst_adapter_available (priv->adapter))
          gst_adapter_clear (priv->adapter);
        else
          gst_adapter_flush (priv->adapter, samples * ctx->info.bpf);
      }
    } else {
      gst_adapter_flush (priv->adapter, samples * ctx->info.bpf);
      priv->offset -= samples * ctx->info.bpf;
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
          ctx->info.rate);
      GST_DEBUG_OBJECT (enc, "out samples %d", samples);
      if (G_LIKELY (samples > 0)) {
        priv->samples += samples;
        GST_BUFFER_DURATION (buf) = priv->base_ts +
            gst_util_uint64_scale (priv->samples - ctx->lookahead, GST_SECOND,
            ctx->info.rate) - GST_BUFFER_TIMESTAMP (buf);
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
            enc->priv->ctx.lookahead;
        /* offset = timestamp corresponding to granulepos for ogg muxer */
        GST_BUFFER_OFFSET (buf) =
            GST_FRAMES_TO_CLOCK_TIME (GST_BUFFER_OFFSET_END (buf),
            ctx->info.rate);
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
  GST_AUDIO_ENCODER_STREAM_UNLOCK (enc);

  return ret;

  /* ERRORS */
overflow:
  {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE,
        ("received more encoded samples %d than provided %d",
            samples, priv->offset / ctx->info.bpf), (NULL));
    if (buf)
      gst_buffer_unref (buf);
    ret = GST_FLOW_ERROR;
    goto exit;
  }
}

 /* adapter tracking idea:
  * - start of adapter corresponds with what has already been encoded
  * (i.e. really returned by encoder subclass)
  * - start + offset is what needs to be fed to subclass next */
static GstFlowReturn
gst_audio_encoder_push_buffers (GstAudioEncoder * enc, gboolean force)
{
  GstAudioEncoderClass *klass;
  GstAudioEncoderPrivate *priv;
  GstAudioEncoderContext *ctx;
  gint av, need;
  GstBuffer *buf;
  GstFlowReturn ret = GST_FLOW_OK;

  klass = GST_AUDIO_ENCODER_GET_CLASS (enc);

  g_return_val_if_fail (klass->handle_frame != NULL, GST_FLOW_ERROR);

  priv = enc->priv;
  ctx = &enc->priv->ctx;

  while (ret == GST_FLOW_OK) {

    buf = NULL;
    av = gst_adapter_available (priv->adapter);

    g_assert (priv->offset <= av);
    av -= priv->offset;

    need =
        ctx->frame_samples_min >
        0 ? ctx->frame_samples_min * ctx->info.bpf : av;
    GST_LOG_OBJECT (enc, "available: %d, needed: %d, force: %d", av, need,
        force);

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

    if (ctx->frame_samples_max > 0)
      need = MIN (av, ctx->frame_samples_max * ctx->info.bpf);

    if (ctx->frame_samples_min == ctx->frame_samples_max) {
      /* if we have some extra metadata,
       * provide for integer multiple of frames to allow for better granularity
       * of processing */
      if (ctx->frame_samples_min > 0 && need) {
        if (ctx->frame_max > 1)
          need = need * MIN ((av / need), ctx->frame_max);
        else if (ctx->frame_max == 0)
          need = need * (av / need);
      }
    }

    priv->got_data = FALSE;
    if (G_LIKELY (need)) {
      buf = gst_buffer_new ();
      GST_BUFFER_DATA (buf) = (guint8 *)
          gst_adapter_peek (priv->adapter, priv->offset + need) + priv->offset;
      GST_BUFFER_SIZE (buf) = need;
    } else if (!priv->drainable) {
      GST_DEBUG_OBJECT (enc, "non-drainable and no more data");
      goto finish;
    }

    GST_LOG_OBJECT (enc, "providing subclass with %d bytes at offset %d",
        need, priv->offset);

    /* mark this already as consumed,
     * which it should be when subclass gives us data in exchange for samples */
    priv->offset += need;
    priv->samples_in += need / ctx->info.bpf;

    /* subclass might not want to be bothered with leftover data,
     * so take care of that here if so, otherwise pass along */
    if (G_UNLIKELY (priv->force && priv->hard_min && buf)) {
      GST_DEBUG_OBJECT (enc, "bypassing subclass with leftover");
      ret = gst_audio_encoder_finish_frame (enc, NULL, -1);
    } else {
      ret = klass->handle_frame (enc, buf);
    }

    if (G_LIKELY (buf))
      gst_buffer_unref (buf);

  finish:
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
gst_audio_encoder_drain (GstAudioEncoder * enc)
{
  GST_DEBUG_OBJECT (enc, "draining");
  if (enc->priv->drained)
    return GST_FLOW_OK;
  else {
    GST_DEBUG_OBJECT (enc, "... really");
    return gst_audio_encoder_push_buffers (enc, TRUE);
  }
}

static void
gst_audio_encoder_set_base_gp (GstAudioEncoder * enc)
{
  GstClockTime ts;

  if (!enc->priv->granule)
    return;

  /* use running time for granule */
  /* incoming data is clipped, so a valid input should yield a valid output */
  ts = gst_segment_to_running_time (&enc->segment, GST_FORMAT_TIME,
      enc->priv->base_ts);
  if (GST_CLOCK_TIME_IS_VALID (ts)) {
    enc->priv->base_gp =
        GST_CLOCK_TIME_TO_FRAMES (enc->priv->base_ts, enc->priv->ctx.info.rate);
    GST_DEBUG_OBJECT (enc, "new base gp %" G_GINT64_FORMAT, enc->priv->base_gp);
  } else {
    /* should reasonably have a valid base,
     * otherwise start at 0 if we did not already start there earlier */
    if (enc->priv->base_gp < 0) {
      enc->priv->base_gp = 0;
      GST_DEBUG_OBJECT (enc, "new base gp %" G_GINT64_FORMAT,
          enc->priv->base_gp);
    }
  }
}

static GstFlowReturn
gst_audio_encoder_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAudioEncoder *enc;
  GstAudioEncoderPrivate *priv;
  GstAudioEncoderContext *ctx;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean discont;

  enc = GST_AUDIO_ENCODER (GST_OBJECT_PARENT (pad));

  priv = enc->priv;
  ctx = &enc->priv->ctx;

  GST_AUDIO_ENCODER_STREAM_LOCK (enc);

  /* should know what is coming by now */
  if (!ctx->info.bpf)
    goto not_negotiated;

  GST_LOG_OBJECT (enc,
      "received buffer of size %d with ts %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT, GST_BUFFER_SIZE (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  /* input shoud be whole number of sample frames */
  if (GST_BUFFER_SIZE (buffer) % ctx->info.bpf)
    goto wrong_buffer;

#ifndef GST_DISABLE_GST_DEBUG
  {
    GstClockTime duration;
    GstClockTimeDiff diff;

    /* verify buffer duration */
    duration = gst_util_uint64_scale (GST_BUFFER_SIZE (buffer), GST_SECOND,
        ctx->info.rate * ctx->info.bpf);
    diff = GST_CLOCK_DIFF (duration, GST_BUFFER_DURATION (buffer));
    if (GST_BUFFER_DURATION (buffer) != GST_CLOCK_TIME_NONE &&
        (diff > GST_SECOND / ctx->info.rate / 2 ||
            diff < -GST_SECOND / ctx->info.rate / 2)) {
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
  buffer = gst_audio_buffer_clip (buffer, &enc->segment, ctx->info.rate,
      ctx->info.bpf);
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
    gst_audio_encoder_set_base_gp (enc);
  }

  /* check for continuity;
   * checked elsewhere in non-perfect case */
  if (enc->priv->perfect_ts) {
    GstClockTimeDiff diff = 0;
    GstClockTime next_ts = 0;

    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer) &&
        GST_CLOCK_TIME_IS_VALID (priv->base_ts)) {
      guint64 samples;

      samples = priv->samples +
          gst_adapter_available (priv->adapter) / ctx->info.bpf;
      next_ts = priv->base_ts +
          gst_util_uint64_scale (samples, GST_SECOND, ctx->info.rate);
      GST_LOG_OBJECT (enc, "buffer is %" G_GUINT64_FORMAT
          " samples past base_ts %" GST_TIME_FORMAT
          ", expected ts %" GST_TIME_FORMAT, samples,
          GST_TIME_ARGS (priv->base_ts), GST_TIME_ARGS (next_ts));
      diff = GST_CLOCK_DIFF (next_ts, GST_BUFFER_TIMESTAMP (buffer));
      GST_LOG_OBJECT (enc, "ts diff %d ms", (gint) (diff / GST_MSECOND));
      /* if within tolerance,
       * discard buffer ts and carry on producing perfect stream,
       * otherwise clip or resync to ts */
      if (G_UNLIKELY (diff < -enc->priv->tolerance ||
              diff > enc->priv->tolerance)) {
        GST_DEBUG_OBJECT (enc, "marked discont");
        discont = TRUE;
      }
    }

    /* do some fancy tweaking in hard resync case */
    if (discont && enc->priv->hard_resync) {
      if (diff < 0) {
        guint64 diff_bytes;

        GST_WARNING_OBJECT (enc, "Buffer is older than expected ts %"
            GST_TIME_FORMAT ".  Clipping buffer", GST_TIME_ARGS (next_ts));

        diff_bytes =
            GST_CLOCK_TIME_TO_FRAMES (-diff, ctx->info.rate) * ctx->info.bpf;
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
        gst_audio_encoder_drain (enc);
      }
    }
    if (discont) {
      /* now re-sync ts */
      priv->base_ts += diff;
      gst_audio_encoder_set_base_gp (enc);
      priv->discont |= discont;
    }
  }

  gst_adapter_push (enc->priv->adapter, buffer);
  /* new stuff, so we can push subclass again */
  enc->priv->drained = FALSE;

  ret = gst_audio_encoder_push_buffers (enc, FALSE);

done:
  GST_LOG_OBJECT (enc, "chain leaving");

  GST_AUDIO_ENCODER_STREAM_UNLOCK (enc);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (enc, CORE, NEGOTIATION, (NULL),
        ("encoder not initialized"));
    gst_buffer_unref (buffer);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
wrong_buffer:
  {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
        ("buffer size %d not a multiple of %d", GST_BUFFER_SIZE (buffer),
            ctx->info.bpf));
    gst_buffer_unref (buffer);
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static gboolean
audio_info_is_equal (GstAudioInfo * from, GstAudioInfo * to)
{
  if (from == to)
    return TRUE;
  if (from->finfo == NULL || to->finfo == NULL)
    return FALSE;
  if (GST_AUDIO_INFO_FORMAT (from) != GST_AUDIO_INFO_FORMAT (to))
    return FALSE;
  if (GST_AUDIO_INFO_RATE (from) != GST_AUDIO_INFO_RATE (to))
    return FALSE;
  if (GST_AUDIO_INFO_CHANNELS (from) != GST_AUDIO_INFO_CHANNELS (to))
    return FALSE;
  if (GST_AUDIO_INFO_CHANNELS (from) > 64)
    return TRUE;
  return memcmp (from->position, to->position,
      GST_AUDIO_INFO_CHANNELS (from) * sizeof (to->position[0]));
}

static gboolean
gst_audio_encoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstAudioEncoder *enc;
  GstAudioEncoderClass *klass;
  GstAudioEncoderContext *ctx;
  GstAudioInfo *state, *old_state;
  gboolean res = TRUE, changed = FALSE;
  guint old_rate;

  enc = GST_AUDIO_ENCODER (GST_PAD_PARENT (pad));
  klass = GST_AUDIO_ENCODER_GET_CLASS (enc);

  /* subclass must do something here ... */
  g_return_val_if_fail (klass->set_format != NULL, FALSE);

  ctx = &enc->priv->ctx;
  state = &ctx->info;

  GST_AUDIO_ENCODER_STREAM_LOCK (enc);

  GST_DEBUG_OBJECT (enc, "caps: %" GST_PTR_FORMAT, caps);

  if (!gst_caps_is_fixed (caps))
    goto refuse_caps;

  /* adjust ts tracking to new sample rate */
  old_rate = GST_AUDIO_INFO_RATE (state);
  if (GST_CLOCK_TIME_IS_VALID (enc->priv->base_ts) && old_rate) {
    enc->priv->base_ts +=
        GST_FRAMES_TO_CLOCK_TIME (enc->priv->samples, old_rate);
    enc->priv->samples = 0;
  }

  old_state = gst_audio_info_copy (state);
  if (!gst_audio_info_from_caps (state, caps))
    goto refuse_caps;

  changed = !audio_info_is_equal (state, old_state);
  gst_audio_info_free (old_state);

  if (changed) {
    GstClockTime old_min_latency;
    GstClockTime old_max_latency;

    /* drain any pending old data stuff */
    gst_audio_encoder_drain (enc);

    /* context defaults */
    enc->priv->ctx.frame_samples_min = 0;
    enc->priv->ctx.frame_samples_max = 0;
    enc->priv->ctx.frame_max = 0;
    enc->priv->ctx.lookahead = 0;

    /* element might report latency */
    GST_OBJECT_LOCK (enc);
    old_min_latency = ctx->min_latency;
    old_max_latency = ctx->max_latency;
    GST_OBJECT_UNLOCK (enc);

    if (klass->set_format)
      res = klass->set_format (enc, state);

    /* invalidate state to ensure no casual carrying on */
    if (!res) {
      GST_DEBUG_OBJECT (enc, "subclass did not accept format");
      gst_audio_info_clear (state);
      goto exit;
    }

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

exit:

  GST_AUDIO_ENCODER_STREAM_UNLOCK (enc);

  return res;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (enc, "rejected caps %" GST_PTR_FORMAT, caps);
    goto exit;
  }
}


/**
 * gst_audio_encoder_proxy_getcaps:
 * @enc: a #GstAudioEncoder
 * @caps: initial caps
 *
 * Returns caps that express @caps (or sink template caps if @caps == NULL)
 * restricted to channel/rate combinations supported by downstream elements
 * (e.g. muxers).
 *
 * Returns: a #GstCaps owned by caller
 *
 * Since: 0.10.36
 */
GstCaps *
gst_audio_encoder_proxy_getcaps (GstAudioEncoder * enc, GstCaps * caps)
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
      /* following might also make sense for some encoded formats,
       * e.g. wavpack */
      if ((val = gst_structure_get_value (allowed_s, "width")))
        gst_structure_set_value (s, "width", val);
      if ((val = gst_structure_get_value (allowed_s, "depth")))
        gst_structure_set_value (s, "depth", val);
      if ((val = gst_structure_get_value (allowed_s, "endianness")))
        gst_structure_set_value (s, "endianness", val);
      if ((val = gst_structure_get_value (allowed_s, "signed")))
        gst_structure_set_value (s, "signed", val);
      if ((val = gst_structure_get_value (allowed_s, "channel-positions")))
        gst_structure_set_value (s, "channel-positions", val);

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
gst_audio_encoder_sink_getcaps (GstPad * pad)
{
  GstAudioEncoder *enc;
  GstAudioEncoderClass *klass;
  GstCaps *caps;

  enc = GST_AUDIO_ENCODER (gst_pad_get_parent (pad));
  klass = GST_AUDIO_ENCODER_GET_CLASS (enc);
  g_assert (pad == enc->sinkpad);

  if (klass->getcaps)
    caps = klass->getcaps (enc);
  else
    caps = gst_audio_encoder_proxy_getcaps (enc, NULL);
  gst_object_unref (enc);

  GST_LOG_OBJECT (enc, "returning caps %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_audio_encoder_sink_eventfunc (GstAudioEncoder * enc, GstEvent * event)
{
  GstAudioEncoderClass *klass;
  gboolean handled = FALSE;

  klass = GST_AUDIO_ENCODER_GET_CLASS (enc);

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

      GST_AUDIO_ENCODER_STREAM_LOCK (enc);
      /* finish current segment */
      gst_audio_encoder_drain (enc);
      /* reset partially for new segment */
      gst_audio_encoder_reset (enc, FALSE);
      /* and follow along with segment */
      gst_segment_set_newsegment_full (&enc->segment, update, rate, arate,
          format, start, stop, time);
      GST_AUDIO_ENCODER_STREAM_UNLOCK (enc);
      break;
    }

    case GST_EVENT_FLUSH_START:
      break;

    case GST_EVENT_FLUSH_STOP:
      GST_AUDIO_ENCODER_STREAM_LOCK (enc);
      /* discard any pending stuff */
      /* TODO route through drain ?? */
      if (!enc->priv->drained && klass->flush)
        klass->flush (enc);
      /* and get (re)set for the sequel */
      gst_audio_encoder_reset (enc, FALSE);

      g_list_foreach (enc->priv->pending_events, (GFunc) gst_event_unref, NULL);
      g_list_free (enc->priv->pending_events);
      enc->priv->pending_events = NULL;
      GST_AUDIO_ENCODER_STREAM_UNLOCK (enc);

      break;

    case GST_EVENT_EOS:
      GST_AUDIO_ENCODER_STREAM_LOCK (enc);
      gst_audio_encoder_drain (enc);
      GST_AUDIO_ENCODER_STREAM_UNLOCK (enc);
      break;

    case GST_EVENT_TAG:
    {
      GstTagList *tags;

      gst_event_parse_tag (event, &tags);
      tags = gst_tag_list_copy (tags);
      gst_event_unref (event);

      /* FIXME: make generic based on GST_TAG_FLAG_ENCODED */
      gst_tag_list_remove_tag (tags, GST_TAG_CODEC);
      gst_tag_list_remove_tag (tags, GST_TAG_AUDIO_CODEC);
      gst_tag_list_remove_tag (tags, GST_TAG_VIDEO_CODEC);
      gst_tag_list_remove_tag (tags, GST_TAG_SUBTITLE_CODEC);
      gst_tag_list_remove_tag (tags, GST_TAG_CONTAINER_FORMAT);
      gst_tag_list_remove_tag (tags, GST_TAG_BITRATE);
      gst_tag_list_remove_tag (tags, GST_TAG_NOMINAL_BITRATE);
      gst_tag_list_remove_tag (tags, GST_TAG_MAXIMUM_BITRATE);
      gst_tag_list_remove_tag (tags, GST_TAG_MINIMUM_BITRATE);
      gst_tag_list_remove_tag (tags, GST_TAG_ENCODER);
      gst_tag_list_remove_tag (tags, GST_TAG_ENCODER_VERSION);
      event = gst_event_new_tag (tags);

      GST_AUDIO_ENCODER_STREAM_LOCK (enc);
      enc->priv->pending_events =
          g_list_append (enc->priv->pending_events, event);
      GST_AUDIO_ENCODER_STREAM_UNLOCK (enc);
      handled = TRUE;
      break;
    }

    default:
      break;
  }

  return handled;
}

static gboolean
gst_audio_encoder_sink_event (GstPad * pad, GstEvent * event)
{
  GstAudioEncoder *enc;
  GstAudioEncoderClass *klass;
  gboolean handled = FALSE;
  gboolean ret = TRUE;

  enc = GST_AUDIO_ENCODER (gst_pad_get_parent (pad));
  klass = GST_AUDIO_ENCODER_GET_CLASS (enc);

  GST_DEBUG_OBJECT (enc, "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  if (klass->event)
    handled = klass->event (enc, event);

  if (!handled)
    handled = gst_audio_encoder_sink_eventfunc (enc, event);

  if (!handled) {
    /* Forward non-serialized events and EOS/FLUSH_STOP immediately.
     * For EOS this is required because no buffer or serialized event
     * will come after EOS and nothing could trigger another
     * _finish_frame() call.
     *
     * For FLUSH_STOP this is required because it is expected
     * to be forwarded immediately and no buffers are queued anyway.
     */
    if (!GST_EVENT_IS_SERIALIZED (event)
        || GST_EVENT_TYPE (event) == GST_EVENT_EOS
        || GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
      ret = gst_pad_event_default (pad, event);
    } else {
      GST_AUDIO_ENCODER_STREAM_LOCK (enc);
      enc->priv->pending_events =
          g_list_append (enc->priv->pending_events, event);
      GST_AUDIO_ENCODER_STREAM_UNLOCK (enc);
      ret = TRUE;
    }
  }

  GST_DEBUG_OBJECT (enc, "event handled");

  gst_object_unref (enc);
  return ret;
}

static gboolean
gst_audio_encoder_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstAudioEncoder *enc;

  enc = GST_AUDIO_ENCODER (gst_pad_get_parent (pad));

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
      if (!(res = gst_audio_info_convert (&enc->priv->ctx.info,
                  src_fmt, src_val, dest_fmt, &dest_val)))
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
gst_audio_encoder_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_audio_encoder_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_LATENCY,
    0
  };

  return gst_audio_encoder_src_query_types;
}

/*
 * gst_audio_encoded_audio_convert:
 * @fmt: audio format of the encoded audio
 * @bytes: number of encoded bytes
 * @samples: number of encoded samples
 * @src_format: source format
 * @src_value: source value
 * @dest_format: destination format
 * @dest_value: destination format
 *
 * Helper function to convert @src_value in @src_format to @dest_value in
 * @dest_format for encoded audio data.  Conversion is possible between
 * BYTE and TIME format by using estimated bitrate based on
 * @samples and @bytes (and @fmt).
 *
 * Since: 0.10.36
 */
/* FIXME: make gst_audio_encoded_audio_convert() public? */
static gboolean
gst_audio_encoded_audio_convert (GstAudioInfo * fmt,
    gint64 bytes, gint64 samples, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
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

  if (samples == 0 || bytes == 0 || fmt->rate == 0) {
    GST_DEBUG ("not enough metadata yet to convert");
    goto exit;
  }

  bytes *= fmt->rate;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (src_value,
              GST_SECOND * samples, bytes);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = gst_util_uint64_scale (src_value, bytes,
              samples * GST_SECOND);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

exit:
  return res;
}

/* FIXME ? are any of these queries (other than latency) an encoder's business
 * also, the conversion stuff might seem to make sense, but seems to not mind
 * segment stuff etc at all
 * Supposedly that's backward compatibility ... */
static gboolean
gst_audio_encoder_src_query (GstPad * pad, GstQuery * query)
{
  GstAudioEncoder *enc;
  GstPad *peerpad;
  gboolean res = FALSE;

  enc = GST_AUDIO_ENCODER (GST_PAD_PARENT (pad));
  if (G_UNLIKELY (enc == NULL))
    return FALSE;

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
      if (!(res = gst_audio_encoded_audio_convert (&enc->priv->ctx.info,
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
          min_latency += enc->priv->ctx.min_latency;
        if (max_latency != -1)
          max_latency += enc->priv->ctx.max_latency;
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
gst_audio_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioEncoder *enc;

  enc = GST_AUDIO_ENCODER (object);

  switch (prop_id) {
    case PROP_PERFECT_TS:
      if (enc->priv->granule && !g_value_get_boolean (value))
        GST_WARNING_OBJECT (enc, "perfect-timestamp can not be set FALSE "
            "while granule handling is enabled");
      else
        enc->priv->perfect_ts = g_value_get_boolean (value);
      break;
    case PROP_HARD_RESYNC:
      enc->priv->hard_resync = g_value_get_boolean (value);
      break;
    case PROP_TOLERANCE:
      enc->priv->tolerance = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_encoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioEncoder *enc;

  enc = GST_AUDIO_ENCODER (object);

  switch (prop_id) {
    case PROP_PERFECT_TS:
      g_value_set_boolean (value, enc->priv->perfect_ts);
      break;
    case PROP_GRANULE:
      g_value_set_boolean (value, enc->priv->granule);
      break;
    case PROP_HARD_RESYNC:
      g_value_set_boolean (value, enc->priv->hard_resync);
      break;
    case PROP_TOLERANCE:
      g_value_set_int64 (value, enc->priv->tolerance);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_audio_encoder_activate (GstAudioEncoder * enc, gboolean active)
{
  GstAudioEncoderClass *klass;
  gboolean result = FALSE;

  klass = GST_AUDIO_ENCODER_GET_CLASS (enc);

  g_return_val_if_fail (!enc->priv->granule || enc->priv->perfect_ts, FALSE);

  GST_DEBUG_OBJECT (enc, "activate %d", active);

  if (active) {

    if (enc->priv->tags)
      gst_tag_list_free (enc->priv->tags);
    enc->priv->tags = gst_tag_list_new ();

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
    gst_audio_encoder_reset (enc, TRUE);
  }
  GST_DEBUG_OBJECT (enc, "activate return: %d", result);
  return result;
}


static gboolean
gst_audio_encoder_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstAudioEncoder *enc;

  enc = GST_AUDIO_ENCODER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (enc, "sink activate push %d", active);

  result = gst_audio_encoder_activate (enc, active);

  if (result)
    enc->priv->active = active;

  GST_DEBUG_OBJECT (enc, "sink activate push return: %d", result);

  gst_object_unref (enc);
  return result;
}

/**
 * gst_audio_encoder_get_audio_info:
 * @enc: a #GstAudioEncoder
 *
 * Returns: a #GstAudioInfo describing the input audio format
 *
 * Since: 0.10.36
 */
GstAudioInfo *
gst_audio_encoder_get_audio_info (GstAudioEncoder * enc)
{
  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), NULL);

  return &enc->priv->ctx.info;
}

/**
 * gst_audio_encoder_set_frame_samples_min:
 * @enc: a #GstAudioEncoder
 * @num: number of samples per frame
 *
 * Sets number of samples (per channel) subclass needs to be handed,
 * at least or will be handed all available if 0.
 *
 * If an exact number of samples is required, gst_audio_encoder_set_frame_samples_max()
 * must be called with the same number.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_frame_samples_min (GstAudioEncoder * enc, gint num)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  enc->priv->ctx.frame_samples_min = num;
}

/**
 * gst_audio_encoder_get_frame_samples_min:
 * @enc: a #GstAudioEncoder
 *
 * Returns: currently minimum requested samples per frame
 *
 * Since: 0.10.36
 */
gint
gst_audio_encoder_get_frame_samples_min (GstAudioEncoder * enc)
{
  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), 0);

  return enc->priv->ctx.frame_samples_min;
}

/**
 * gst_audio_encoder_set_frame_samples_max:
 * @enc: a #GstAudioEncoder
 * @num: number of samples per frame
 *
 * Sets number of samples (per channel) subclass needs to be handed,
 * at most or will be handed all available if 0.
 *
 * If an exact number of samples is required, gst_audio_encoder_set_frame_samples_min()
 * must be called with the same number.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_frame_samples_max (GstAudioEncoder * enc, gint num)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  enc->priv->ctx.frame_samples_max = num;
}

/**
 * gst_audio_encoder_get_frame_samples_min:
 * @enc: a #GstAudioEncoder
 *
 * Returns: currently maximum requested samples per frame
 *
 * Since: 0.10.36
 */
gint
gst_audio_encoder_get_frame_samples_max (GstAudioEncoder * enc)
{
  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), 0);

  return enc->priv->ctx.frame_samples_max;
}

/**
 * gst_audio_encoder_set_frame_max:
 * @enc: a #GstAudioEncoder
 * @num: number of frames
 *
 * Sets max number of frames accepted at once (assumed minimally 1).
 * Requires @frame_samples_min and @frame_samples_max to be the equal.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_frame_max (GstAudioEncoder * enc, gint num)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  enc->priv->ctx.frame_max = num;
}

/**
 * gst_audio_encoder_get_frame_max:
 * @enc: a #GstAudioEncoder
 *
 * Returns: currently configured maximum handled frames
 *
 * Since: 0.10.36
 */
gint
gst_audio_encoder_get_frame_max (GstAudioEncoder * enc)
{
  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), 0);

  return enc->priv->ctx.frame_max;
}

/**
 * gst_audio_encoder_set_lookahead:
 * @enc: a #GstAudioEncoder
 * @num: lookahead
 *
 * Sets encoder lookahead (in units of input rate samples)
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_lookahead (GstAudioEncoder * enc, gint num)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  enc->priv->ctx.lookahead = num;
}

/**
 * gst_audio_encoder_get_lookahead:
 * @enc: a #GstAudioEncoder
 *
 * Returns: currently configured encoder lookahead
 */
gint
gst_audio_encoder_get_lookahead (GstAudioEncoder * enc)
{
  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), 0);

  return enc->priv->ctx.lookahead;
}

/**
 * gst_audio_encoder_set_latency:
 * @enc: a #GstAudioEncoder
 * @min: minimum latency
 * @max: maximum latency
 *
 * Sets encoder latency.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_latency (GstAudioEncoder * enc,
    GstClockTime min, GstClockTime max)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  GST_OBJECT_LOCK (enc);
  enc->priv->ctx.min_latency = min;
  enc->priv->ctx.max_latency = max;
  GST_OBJECT_UNLOCK (enc);
}

/**
 * gst_audio_encoder_get_latency:
 * @enc: a #GstAudioEncoder
 * @min: (out) (allow-none): a pointer to storage to hold minimum latency
 * @max: (out) (allow-none): a pointer to storage to hold maximum latency
 *
 * Sets the variables pointed to by @min and @max to the currently configured
 * latency.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_get_latency (GstAudioEncoder * enc,
    GstClockTime * min, GstClockTime * max)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  GST_OBJECT_LOCK (enc);
  if (min)
    *min = enc->priv->ctx.min_latency;
  if (max)
    *max = enc->priv->ctx.max_latency;
  GST_OBJECT_UNLOCK (enc);
}

/**
 * gst_audio_encoder_set_mark_granule:
 * @enc: a #GstAudioEncoder
 * @enabled: new state
 *
 * Enable or disable encoder granule handling.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_mark_granule (GstAudioEncoder * enc, gboolean enabled)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  GST_LOG_OBJECT (enc, "enabled: %d", enabled);

  GST_OBJECT_LOCK (enc);
  enc->priv->granule = enabled;
  GST_OBJECT_UNLOCK (enc);
}

/**
 * gst_audio_encoder_get_mark_granule:
 * @enc: a #GstAudioEncoder
 *
 * Queries if the encoder will handle granule marking.
 *
 * Returns: TRUE if granule marking is enabled.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
gboolean
gst_audio_encoder_get_mark_granule (GstAudioEncoder * enc)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), FALSE);

  GST_OBJECT_LOCK (enc);
  result = enc->priv->granule;
  GST_OBJECT_UNLOCK (enc);

  return result;
}

/**
 * gst_audio_encoder_set_perfect_timestamp:
 * @enc: a #GstAudioEncoder
 * @enabled: new state
 *
 * Enable or disable encoder perfect output timestamp preference.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_perfect_timestamp (GstAudioEncoder * enc,
    gboolean enabled)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  GST_LOG_OBJECT (enc, "enabled: %d", enabled);

  GST_OBJECT_LOCK (enc);
  enc->priv->perfect_ts = enabled;
  GST_OBJECT_UNLOCK (enc);
}

/**
 * gst_audio_encoder_get_perfect_timestamp:
 * @enc: a #GstAudioEncoder
 *
 * Queries encoder perfect timestamp behaviour.
 *
 * Returns: TRUE if perfect timestamp setting enabled.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
gboolean
gst_audio_encoder_get_perfect_timestamp (GstAudioEncoder * enc)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), FALSE);

  GST_OBJECT_LOCK (enc);
  result = enc->priv->perfect_ts;
  GST_OBJECT_UNLOCK (enc);

  return result;
}

/**
 * gst_audio_encoder_set_hard_sync:
 * @enc: a #GstAudioEncoder
 * @enabled: new state
 *
 * Sets encoder hard resync handling.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_hard_resync (GstAudioEncoder * enc, gboolean enabled)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  GST_LOG_OBJECT (enc, "enabled: %d", enabled);

  GST_OBJECT_LOCK (enc);
  enc->priv->hard_resync = enabled;
  GST_OBJECT_UNLOCK (enc);
}

/**
 * gst_audio_encoder_get_hard_sync:
 * @enc: a #GstAudioEncoder
 *
 * Queries encoder's hard resync setting.
 *
 * Returns: TRUE if hard resync is enabled.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
gboolean
gst_audio_encoder_get_hard_resync (GstAudioEncoder * enc)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), FALSE);

  GST_OBJECT_LOCK (enc);
  result = enc->priv->hard_resync;
  GST_OBJECT_UNLOCK (enc);

  return result;
}

/**
 * gst_audio_encoder_set_tolerance:
 * @enc: a #GstAudioEncoder
 * @tolerance: new tolerance
 *
 * Configures encoder audio jitter tolerance threshold.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_tolerance (GstAudioEncoder * enc, gint64 tolerance)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  GST_OBJECT_LOCK (enc);
  enc->priv->tolerance = tolerance;
  GST_OBJECT_UNLOCK (enc);
}

/**
 * gst_audio_encoder_get_tolerance:
 * @enc: a #GstAudioEncoder
 *
 * Queries current audio jitter tolerance threshold.
 *
 * Returns: encoder audio jitter tolerance threshold.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
gint64
gst_audio_encoder_get_tolerance (GstAudioEncoder * enc)
{
  gint64 result;

  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), 0);

  GST_OBJECT_LOCK (enc);
  result = enc->priv->tolerance;
  GST_OBJECT_UNLOCK (enc);

  return result;
}

/**
 * gst_audio_encoder_set_hard_min:
 * @enc: a #GstAudioEncoder
 * @enabled: new state
 *
 * Configures encoder hard minimum handling.  If enabled, subclass
 * will never be handed less samples than it configured, which otherwise
 * might occur near end-of-data handling.  Instead, the leftover samples
 * will simply be discarded.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_hard_min (GstAudioEncoder * enc, gboolean enabled)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  GST_OBJECT_LOCK (enc);
  enc->priv->hard_min = enabled;
  GST_OBJECT_UNLOCK (enc);
}

/**
 * gst_audio_encoder_get_hard_min:
 * @enc: a #GstAudioEncoder
 *
 * Queries encoder hard minimum handling.
 *
 * Returns: TRUE if hard minimum handling is enabled.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
gboolean
gst_audio_encoder_get_hard_min (GstAudioEncoder * enc)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), 0);

  GST_OBJECT_LOCK (enc);
  result = enc->priv->hard_min;
  GST_OBJECT_UNLOCK (enc);

  return result;
}

/**
 * gst_audio_encoder_set_drainable:
 * @enc: a #GstAudioEncoder
 * @enabled: new state
 *
 * Configures encoder drain handling.  If drainable, subclass might
 * be handed a NULL buffer to have it return any leftover encoded data.
 * Otherwise, it is not considered so capable and will only ever be passed
 * real data.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_set_drainable (GstAudioEncoder * enc, gboolean enabled)
{
  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));

  GST_OBJECT_LOCK (enc);
  enc->priv->drainable = enabled;
  GST_OBJECT_UNLOCK (enc);
}

/**
 * gst_audio_encoder_get_drainable:
 * @enc: a #GstAudioEncoder
 *
 * Queries encoder drain handling.
 *
 * Returns: TRUE if drainable handling is enabled.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
gboolean
gst_audio_encoder_get_drainable (GstAudioEncoder * enc)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_AUDIO_ENCODER (enc), 0);

  GST_OBJECT_LOCK (enc);
  result = enc->priv->drainable;
  GST_OBJECT_UNLOCK (enc);

  return result;
}

/**
 * gst_audio_encoder_merge_tags:
 * @enc: a #GstAudioEncoder
 * @tags: a #GstTagList to merge
 * @mode: the #GstTagMergeMode to use
 *
 * Adds tags to so-called pending tags, which will be processed
 * before pushing out data downstream.
 *
 * Note that this is provided for convenience, and the subclass is
 * not required to use this and can still do tag handling on its own,
 * although it should be aware that baseclass already takes care
 * of the usual CODEC/AUDIO_CODEC tags.
 *
 * MT safe.
 *
 * Since: 0.10.36
 */
void
gst_audio_encoder_merge_tags (GstAudioEncoder * enc,
    const GstTagList * tags, GstTagMergeMode mode)
{
  GstTagList *otags;

  g_return_if_fail (GST_IS_AUDIO_ENCODER (enc));
  g_return_if_fail (tags == NULL || GST_IS_TAG_LIST (tags));

  GST_OBJECT_LOCK (enc);
  if (tags)
    GST_DEBUG_OBJECT (enc, "merging tags %" GST_PTR_FORMAT, tags);
  otags = enc->priv->tags;
  enc->priv->tags = gst_tag_list_merge (enc->priv->tags, tags, mode);
  if (otags)
    gst_tag_list_free (otags);
  GST_OBJECT_UNLOCK (enc);
}
