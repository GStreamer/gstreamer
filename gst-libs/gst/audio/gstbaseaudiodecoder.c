/* GStreamer
 * Copyright (C) 2009 Igalia S.L.
 * Author: Iago Toral Quiroga <itoral@igalia.com>
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
 * SECTION:gstbaseaudiodecoder
 * @short_description: Base class for audio decoders
 * @see_also: #GstBaseTransform
 *
 * This base class is for audio decoders turning encoded data into
 * raw audio samples.
 *
 * GstBaseAudioDecoder and subclass should cooperate as follows.
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Configuration</title>
 *   <listitem><para>
 *     Initially, GstBaseAudioDecoder calls @start when the decoder element
 *     is activated, which allows subclass to perform any global setup.
 *     Base class (context) parameters can already be set according to subclass
 *     capabilities (or possibly upon receive more information in subsequent
 *     @set_format).
 *   </para></listitem>
 *   <listitem><para>
 *     GstBaseAudioDecoder calls @set_format to inform subclass of the format
 *     of input audio data that it is about to receive.
 *     While unlikely, it might be called more than once, if changing input
 *     parameters require reconfiguration.
 *   </para></listitem>
 *   <listitem><para>
 *     GstBaseAudioDecoder calls @stop at end of all processing.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * As of configuration stage, and throughout processing, GstBaseAudioDecoder
 * provides various (context) parameters, e.g. describing the format of
 * output audio data (valid when output caps have been caps) or current parsing state.
 * Conversely, subclass can and should configure context to inform
 * base class of its expectation w.r.t. buffer handling.
 * <listitem>
 *   <itemizedlist>
 *   <title>Data processing</title>
 *     <listitem><para>
 *       Base class gathers input data, and optionally allows subclass
 *       to parse this into subsequently manageable (as defined by subclass)
 *       chunks.  Such chunks are subsequently referred to as 'frames',
 *       though they may or may not correspond to 1 (or more) audio format frame.
 *     </para></listitem>
 *     <listitem><para>
 *       Input frame is provided to subclass' @handle_frame.
 *     </para></listitem>
 *     <listitem><para>
 *       If codec processing results in decoded data, subclass should call
 *       @gst_base_audio_decoder_finish_frame to have decoded data pushed
 *       downstream.
 *     </para></listitem>
 *     <listitem><para>
 *       Just prior to actually pushing a buffer downstream,
 *       it is passed to @pre_push.  Subclass should either use this callback
 *       to arrange for additional downstream pushing or otherwise ensure such
 *       custom pushing occurs after at least a method call has finished since
 *       setting src pad caps.
 *     </para></listitem>
 *     <listitem><para>
 *       During the parsing process GstBaseAudioDecoderClass will handle both
 *       srcpad and sinkpad events. Sink events will be passed to subclass
 *       if @event callback has been provided.
 *     </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Shutdown phase</title>
 *   <listitem><para>
 *     GstBaseAudioDecoder class calls @stop to inform the subclass that data
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
 * it might be delayed until calling @gst_base_audio_decoder_finish_frame.
 *
 * In summary, above process should have subclass concentrating on
 * codec data processing while leaving other matters to base class,
 * such as most notably timestamp handling.  While it may exert more control
 * in this area (see e.g. @pre_push), it is very much not recommended.
 *
 * In particular, base class will try to arrange for perfect output timestamps
 * as much as possible while tracking upstream timestamps.
 * To this end, if deviation between the next ideal expected perfect timestamp
 * and upstream exceeds #GstBaseAudioDecoder:tolerance, then resync to upstream
 * occurs (which would happen always if the tolerance mechanism is disabled).
 *
 * In non-live pipelines, baseclass can also (configurably) arrange for
 * output buffer aggregation which may help to redue large(r) numbers of
 * small(er) buffers being pushed and processed downstream.
 *
 * On the other hand, it should be noted that baseclass only provides limited
 * seeking support (upon explicit subclass request), as full-fledged support
 * should rather be left to upstream demuxer, parser or alike.  This simple
 * approach caters for seeking and duration reporting using estimated input
 * bitrates.
 *
 * Things that subclass need to take care of:
 * <itemizedlist>
 *   <listitem><para>Provide pad templates</para></listitem>
 *   <listitem><para>
 *      Set source pad caps when appropriate
 *   </para></listitem>
 *   <listitem><para>
 *      Set user-configurable properties to sane defaults for format and
 *      implementing codec at hand, and convey some subclass capabilities and
 *      expectations in context.
 *   </para></listitem>
 *   <listitem><para>
 *      Accept data in @handle_frame and provide encoded results to
 *      @gst_base_audio_decoder_finish_frame.  If it is prepared to perform
 *      PLC, it should also accept NULL data in @handle_frame and provide for
 *      data for indicated duration.
 *   </para></listitem>
 * </itemizedlist>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstbaseaudiodecoder.h"
#include <gst/audio/audio.h>
#include <gst/base/gstadapter.h>
#include <gst/pbutils/descriptions.h>

#include <string.h>

GST_DEBUG_CATEGORY (baseaudiodecoder_debug);
#define GST_CAT_DEFAULT baseaudiodecoder_debug

#define GST_BASE_AUDIO_DECODER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_BASE_AUDIO_DECODER, \
        GstBaseAudioDecoderPrivate))

enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LATENCY,
  PROP_TOLERANCE,
  PROP_PLC
};

#define DEFAULT_LATENCY    0
#define DEFAULT_TOLERANCE  0
#define DEFAULT_PLC        FALSE

typedef struct _GstBaseAudioDecoderContext
{
  /* input */
  /* (output) audio format */
  GstAudioFormatInfo info;

  /* parsing state */
  gboolean eos;
  gboolean sync;

  /* misc */
  gint delay;

  /* output */
  gboolean do_plc;
  gboolean do_byte_time;
  gint max_errors;
  /* MT-protected (with LOCK) */
  GstClockTime min_latency;
  GstClockTime max_latency;
} GstBaseAudioDecoderContext;

struct _GstBaseAudioDecoderPrivate
{
  /* activation status */
  gboolean active;

  /* input base/first ts as basis for output ts */
  GstClockTime base_ts;
  /* input samples processed and sent downstream so far (w.r.t. base_ts) */
  guint64 samples;

  /* collected input data */
  GstAdapter *adapter;
  /* tracking input ts for changes */
  GstClockTime prev_ts;
  /* frames obtained from input */
  GQueue frames;
  /* collected output data */
  GstAdapter *adapter_out;
  /* ts and duration for output data collected above */
  GstClockTime out_ts, out_dur;
  /* mark outgoing discont */
  gboolean discont;

  /* subclass gave all it could already */
  gboolean drained;
  /* subclass currently being forcibly drained */
  gboolean force;

  /* input bps estimatation */
  /* global in bytes seen */
  guint64 bytes_in;
  /* global samples sent out */
  guint64 samples_out;
  /* bytes flushed during parsing */
  guint sync_flush;
  /* error count */
  gint error_count;
  /* codec id tag */
  GstTagList *taglist;

  /* whether circumstances allow output aggregation */
  gint agg;

  /* reverse playback queues */
  /* collect input */
  GList *gather;
  /* to-be-decoded */
  GList *decode;
  /* reversed output */
  GList *queued;

  /* context storage */
  GstBaseAudioDecoderContext ctx;

  /* properties */
  GstClockTime latency;
  GstClockTime tolerance;
  gboolean plc;

};


static void gst_base_audio_decoder_finalize (GObject * object);
static void gst_base_audio_decoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_audio_decoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_base_audio_decoder_clear_queues (GstBaseAudioDecoder * dec);
static GstFlowReturn gst_base_audio_decoder_chain_reverse (GstBaseAudioDecoder *
    dec, GstBuffer * buf);

static GstStateChangeReturn gst_base_audio_decoder_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_base_audio_decoder_sink_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_base_audio_decoder_src_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_base_audio_decoder_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static gboolean gst_base_audio_decoder_src_setcaps (GstPad * pad,
    GstCaps * caps);
static GstFlowReturn gst_base_audio_decoder_chain (GstPad * pad,
    GstBuffer * buf);
static gboolean gst_base_audio_decoder_src_query (GstPad * pad,
    GstQuery * query);
static gboolean gst_base_audio_decoder_sink_query (GstPad * pad,
    GstQuery * query);
static const GstQueryType *gst_base_audio_decoder_get_query_types (GstPad *
    pad);
static void gst_base_audio_decoder_reset (GstBaseAudioDecoder * dec,
    gboolean full);


GST_BOILERPLATE (GstBaseAudioDecoder, gst_base_audio_decoder, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_base_audio_decoder_base_init (gpointer g_class)
{
}

static void
gst_base_audio_decoder_class_init (GstBaseAudioDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  g_type_class_add_private (klass, sizeof (GstBaseAudioDecoderPrivate));

  GST_DEBUG_CATEGORY_INIT (baseaudiodecoder_debug, "baseaudiodecoder", 0,
      "baseaudiodecoder element");

  gobject_class->set_property = gst_base_audio_decoder_set_property;
  gobject_class->get_property = gst_base_audio_decoder_get_property;
  gobject_class->finalize = gst_base_audio_decoder_finalize;

  element_class->change_state = gst_base_audio_decoder_change_state;

  /* Properties */
  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_int64 ("min-latency", "Minimum Latency",
          "Aggregate output data to a minimum of latency time (ns)",
          0, G_MAXINT64, DEFAULT_LATENCY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TOLERANCE,
      g_param_spec_int64 ("tolerance", "Tolerance",
          "Perfect ts while timestamp jitter/imperfection within tolerance (ns)",
          0, G_MAXINT64, DEFAULT_TOLERANCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PLC,
      g_param_spec_boolean ("plc", "Packet Loss Concealment",
          "Perform packet loss concealment (if supported)",
          DEFAULT_PLC, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_base_audio_decoder_init (GstBaseAudioDecoder * dec,
    GstBaseAudioDecoderClass * klass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG_OBJECT (dec, "gst_base_audio_decoder_init");

  dec->priv = GST_BASE_AUDIO_DECODER_GET_PRIVATE (dec);

  /* Setup sink pad */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  dec->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_decoder_sink_event));
  gst_pad_set_setcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_decoder_sink_setcaps));
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_decoder_chain));
  gst_pad_set_query_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_decoder_sink_query));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
  GST_DEBUG_OBJECT (dec, "sinkpad created");

  /* Setup source pad */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  dec->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_set_setcaps_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_decoder_src_setcaps));
  gst_pad_set_event_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_decoder_src_event));
  gst_pad_set_query_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_decoder_src_query));
  gst_pad_set_query_type_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_audio_decoder_get_query_types));
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);
  GST_DEBUG_OBJECT (dec, "srcpad created");

  dec->priv->adapter = gst_adapter_new ();
  dec->priv->adapter_out = gst_adapter_new ();
  g_queue_init (&dec->priv->frames);

  /* property default */
  dec->priv->latency = DEFAULT_LATENCY;
  dec->priv->tolerance = DEFAULT_TOLERANCE;
  dec->priv->plc = DEFAULT_PLC;

  /* init state */
  gst_base_audio_decoder_reset (dec, TRUE);
  GST_DEBUG_OBJECT (dec, "init ok");
}

static void
gst_base_audio_decoder_reset (GstBaseAudioDecoder * dec, gboolean full)
{
  GST_DEBUG_OBJECT (dec, "gst_base_audio_decoder_reset");

  GST_OBJECT_LOCK (dec);

  if (full) {
    dec->priv->active = FALSE;
    dec->priv->bytes_in = 0;
    dec->priv->samples_out = 0;
    dec->priv->agg = -1;
    dec->priv->error_count = 0;
    gst_base_audio_decoder_clear_queues (dec);

    gst_base_audio_format_info_clear (&dec->priv->ctx.info);
    memset (&dec->priv->ctx, 0, sizeof (dec->priv->ctx));

    if (dec->priv->taglist) {
      gst_tag_list_free (dec->priv->taglist);
      dec->priv->taglist = NULL;
    }

    gst_segment_init (&dec->segment, GST_FORMAT_TIME);
  }

  g_queue_foreach (&dec->priv->frames, (GFunc) gst_buffer_unref, NULL);
  g_queue_clear (&dec->priv->frames);
  gst_adapter_clear (dec->priv->adapter);
  gst_adapter_clear (dec->priv->adapter_out);
  dec->priv->out_ts = GST_CLOCK_TIME_NONE;
  dec->priv->out_dur = 0;
  dec->priv->prev_ts = GST_CLOCK_TIME_NONE;
  dec->priv->drained = TRUE;
  dec->priv->base_ts = GST_CLOCK_TIME_NONE;
  dec->priv->samples = 0;
  dec->priv->discont = TRUE;
  dec->priv->sync_flush = FALSE;

  GST_OBJECT_UNLOCK (dec);
}

static void
gst_base_audio_decoder_finalize (GObject * object)
{
  GstBaseAudioDecoder *dec;

  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (object));
  dec = GST_BASE_AUDIO_DECODER (object);

  if (dec->priv->adapter) {
    g_object_unref (dec->priv->adapter);
  }
  if (dec->priv->adapter_out) {
    g_object_unref (dec->priv->adapter_out);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* automagically perform sanity checking of src caps;
 * also extracts output data format */
static gboolean
gst_base_audio_decoder_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseAudioDecoder *dec;
  GstAudioFormatInfo *state;
  gboolean res = TRUE;

  dec = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  state = &dec->priv->ctx.info;

  GST_DEBUG_OBJECT (dec, "setting src caps %" GST_PTR_FORMAT, caps);

  /* parse caps here to check subclass;
   * also makes us aware of output format */
  if (!gst_caps_is_fixed (caps))
    goto refuse_caps;

  /* adjust ts tracking to new sample rate */
  if (GST_CLOCK_TIME_IS_VALID (dec->priv->base_ts) && state->rate) {
    dec->priv->base_ts +=
        GST_FRAMES_TO_CLOCK_TIME (dec->priv->samples, state->rate);
    dec->priv->samples = 0;
  }

  if (!gst_base_audio_parse_caps (caps, state))
    goto refuse_caps;

  gst_object_unref (dec);
  return res;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (dec, "rejected caps %" GST_PTR_FORMAT, caps);
    gst_object_unref (dec);
    return res;
  }
}

static gboolean
gst_base_audio_decoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseAudioDecoder *dec;
  GstBaseAudioDecoderClass *klass;
  gboolean res = TRUE;

  dec = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  klass = GST_BASE_AUDIO_DECODER_GET_CLASS (dec);

  GST_DEBUG_OBJECT (dec, "caps: %" GST_PTR_FORMAT, caps);

  /* NOTE pbutils only needed here */
  /* TODO maybe (only) upstream demuxer/parser etc should handle this ? */
  if (dec->priv->taglist)
    gst_tag_list_free (dec->priv->taglist);
  dec->priv->taglist = gst_tag_list_new ();
  gst_pb_utils_add_codec_description_to_tag_list (dec->priv->taglist,
      GST_TAG_AUDIO_CODEC, caps);

  if (klass->set_format)
    res = klass->set_format (dec, caps);

  g_object_unref (dec);
  return res;
}

static void
gst_base_audio_decoder_setup (GstBaseAudioDecoder * dec)
{
  GstQuery *query;
  gboolean res;

  /* check if in live pipeline, then latency messing is no-no */
  query = gst_query_new_latency ();
  res = gst_pad_peer_query (dec->sinkpad, query);
  if (res) {
    gst_query_parse_latency (query, &res, NULL, NULL);
    res = !res;
  }
  gst_query_unref (query);

  /* normalize to bool */
  dec->priv->agg = !!res;
}

/* mini aggregator combining output buffers into fewer larger ones,
 * if so allowed/configured */
static GstFlowReturn
gst_base_audio_decoder_output (GstBaseAudioDecoder * dec, GstBuffer * buf)
{
  GstBaseAudioDecoderClass *klass;
  GstBaseAudioDecoderPrivate *priv;
  GstBaseAudioDecoderContext *ctx;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *inbuf = NULL;

  klass = GST_BASE_AUDIO_DECODER_GET_CLASS (dec);
  priv = dec->priv;
  ctx = &dec->priv->ctx;

  if (G_UNLIKELY (priv->agg < 0))
    gst_base_audio_decoder_setup (dec);

  if (G_LIKELY (buf)) {
    g_return_val_if_fail (ctx->info.bpf != 0, GST_FLOW_ERROR);

    GST_LOG_OBJECT (dec, "output buffer of size %d with ts %" GST_TIME_FORMAT
        ", duration %" GST_TIME_FORMAT, GST_BUFFER_SIZE (buf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    /* clip buffer */
    buf = gst_audio_buffer_clip (buf, &dec->segment, ctx->info.rate,
        ctx->info.bpf);
    if (G_UNLIKELY (!buf)) {
      GST_DEBUG_OBJECT (dec, "no data after clipping to segment");
    } else {
      GST_LOG_OBJECT (dec,
          "buffer after segment clipping has size %d with ts %" GST_TIME_FORMAT
          ", duration %" GST_TIME_FORMAT, GST_BUFFER_SIZE (buf),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
    }
  } else {
    GST_DEBUG_OBJECT (dec, "no output buffer");
  }

again:
  inbuf = NULL;
  if (priv->agg && dec->priv->latency > 0) {
    gint av;
    gboolean assemble = FALSE;
    const GstClockTimeDiff tol = 10 * GST_MSECOND;
    GstClockTimeDiff diff = -100 * GST_MSECOND;

    av = gst_adapter_available (priv->adapter_out);
    if (G_UNLIKELY (!buf)) {
      /* forcibly send current */
      assemble = TRUE;
      GST_LOG_OBJECT (dec, "forcing fragment flush");
    } else if (av && (!GST_BUFFER_TIMESTAMP_IS_VALID (buf) ||
            !GST_CLOCK_TIME_IS_VALID (priv->out_ts) ||
            ((diff = GST_CLOCK_DIFF (GST_BUFFER_TIMESTAMP (buf),
                        priv->out_ts + priv->out_dur)) > tol) || diff < -tol)) {
      assemble = TRUE;
      GST_LOG_OBJECT (dec, "buffer %d ms apart from current fragment",
          (gint) (diff / GST_MSECOND));
    } else {
      /* add or start collecting */
      if (!av) {
        GST_LOG_OBJECT (dec, "starting new fragment");
        priv->out_ts = GST_BUFFER_TIMESTAMP (buf);
      } else {
        GST_LOG_OBJECT (dec, "adding to fragment");
      }
      gst_adapter_push (priv->adapter_out, buf);
      priv->out_dur += GST_BUFFER_DURATION (buf);
      av += GST_BUFFER_SIZE (buf);
      buf = NULL;
    }
    if (priv->out_dur > dec->priv->latency)
      assemble = TRUE;
    if (av && assemble) {
      GST_LOG_OBJECT (dec, "assembling fragment");
      inbuf = buf;
      buf = gst_adapter_take_buffer (priv->adapter_out, av);
      GST_BUFFER_TIMESTAMP (buf) = priv->out_ts;
      GST_BUFFER_DURATION (buf) = priv->out_dur;
      priv->out_ts = GST_CLOCK_TIME_NONE;
      priv->out_dur = 0;
    }
  }

  if (G_LIKELY (buf)) {

    /* decorate */
    gst_buffer_set_caps (buf, GST_PAD_CAPS (dec->srcpad));

    if (G_UNLIKELY (priv->discont)) {
      GST_LOG_OBJECT (dec, "marking discont");
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      priv->discont = FALSE;
    }

    if (G_LIKELY (GST_BUFFER_TIMESTAMP_IS_VALID (buf))) {
      /* duration should always be valid for raw audio */
      g_assert (GST_BUFFER_DURATION_IS_VALID (buf));
      dec->segment.last_stop =
          GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf);
    }

    if (klass->pre_push) {
      /* last chance for subclass to do some dirty stuff */
      ret = klass->pre_push (dec, &buf);
      if (ret != GST_FLOW_OK || !buf) {
        GST_DEBUG_OBJECT (dec, "subclass returned %s, buf %p",
            gst_flow_get_name (ret), buf);
        if (buf)
          gst_buffer_unref (buf);
        goto exit;
      }
    }

    GST_LOG_OBJECT (dec, "pushing buffer of size %d with ts %" GST_TIME_FORMAT
        ", duration %" GST_TIME_FORMAT, GST_BUFFER_SIZE (buf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    if (dec->segment.rate > 0.0) {
      ret = gst_pad_push (dec->srcpad, buf);
      GST_LOG_OBJECT (dec, "buffer pushed: %s", gst_flow_get_name (ret));
    } else {
      ret = GST_FLOW_OK;
      priv->queued = g_list_prepend (priv->queued, buf);
      GST_LOG_OBJECT (dec, "buffer queued");
    }

  exit:
    if (inbuf) {
      buf = inbuf;
      goto again;
    }
  }

  return ret;
}

GstFlowReturn
gst_base_audio_decoder_finish_frame (GstBaseAudioDecoder * dec, GstBuffer * buf,
    gint frames)
{
  GstBaseAudioDecoderPrivate *priv;
  GstBaseAudioDecoderContext *ctx;
  gint samples = 0;
  GstClockTime ts, next_ts;

  /* subclass should know what it is producing by now */
  g_return_val_if_fail (buf == NULL || GST_PAD_CAPS (dec->srcpad) != NULL,
      GST_FLOW_ERROR);
  /* subclass should not hand us no data */
  g_return_val_if_fail (buf == NULL || GST_BUFFER_SIZE (buf) > 0,
      GST_FLOW_ERROR);
  /* no dummy calls please */
  g_return_val_if_fail (frames != 0, GST_FLOW_ERROR);

  priv = dec->priv;
  ctx = &dec->priv->ctx;

  GST_LOG_OBJECT (dec, "accepting %d bytes == %d samples for %d frames",
      buf ? GST_BUFFER_SIZE (buf) : -1,
      buf ? GST_BUFFER_SIZE (buf) / ctx->info.bpf : -1, frames);

  /* output shoud be whole number of sample frames */
  if (G_LIKELY (buf && ctx->info.bpf)) {
    if (GST_BUFFER_SIZE (buf) % ctx->info.bpf)
      goto wrong_buffer;
    /* per channel least */
    samples = GST_BUFFER_SIZE (buf) / ctx->info.bpf;
  }

  /* frame and ts book-keeping */
  if (G_UNLIKELY (frames < 0)) {
    if (G_UNLIKELY (-frames - 1 > priv->frames.length))
      goto overflow;
    frames = priv->frames.length + frames + 1;
  } else if (G_UNLIKELY (frames > priv->frames.length)) {
    if (G_LIKELY (!priv->force)) {
      /* no way we can let this pass */
      g_assert_not_reached ();
      /* really no way */
      goto overflow;
    }
  }

  if (G_LIKELY (priv->frames.length))
    ts = GST_BUFFER_TIMESTAMP (priv->frames.head->data);
  else
    ts = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (dec, "leading frame ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (ts));

  while (priv->frames.length && frames) {
    gst_buffer_unref (g_queue_pop_head (&priv->frames));
    dec->priv->ctx.delay = dec->priv->frames.length;
    frames--;
  }

  /* lock on */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (priv->base_ts))) {
    priv->base_ts = ts;
    GST_DEBUG_OBJECT (dec, "base_ts now %" GST_TIME_FORMAT, GST_TIME_ARGS (ts));
  }

  if (G_UNLIKELY (!buf))
    goto exit;

  /* slightly convoluted approach caters for perfect ts if subclass desires */
  if (GST_CLOCK_TIME_IS_VALID (ts)) {
    if (dec->priv->tolerance > 0) {
      GstClockTimeDiff diff;

      g_assert (GST_CLOCK_TIME_IS_VALID (priv->base_ts));
      next_ts = priv->base_ts +
          gst_util_uint64_scale (samples, GST_SECOND, ctx->info.rate);
      GST_LOG_OBJECT (dec, "buffer is %d samples past base_ts %" GST_TIME_FORMAT
          ", expected ts %" GST_TIME_FORMAT, samples,
          GST_TIME_ARGS (priv->base_ts), GST_TIME_ARGS (next_ts));
      diff = GST_CLOCK_DIFF (next_ts, ts);
      GST_LOG_OBJECT (dec, "ts diff %d ms", (gint) (diff / GST_MSECOND));
      /* if within tolerance,
       * discard buffer ts and carry on producing perfect stream,
       * otherwise resync to ts */
      if (G_UNLIKELY (diff < -dec->priv->tolerance ||
              diff > dec->priv->tolerance)) {
        GST_DEBUG_OBJECT (dec, "base_ts resync");
        priv->base_ts = ts;
        priv->samples = 0;
      }
    } else {
      GST_DEBUG_OBJECT (dec, "base_ts resync");
      priv->base_ts = ts;
      priv->samples = 0;
    }
  }

  /* delayed one-shot stuff until confirmed data */
  if (priv->taglist) {
    GST_DEBUG_OBJECT (dec, "codec tag %" GST_PTR_FORMAT, priv->taglist);
    if (gst_tag_list_is_empty (priv->taglist)) {
      gst_tag_list_free (priv->taglist);
    } else {
      gst_element_found_tags (GST_ELEMENT (dec), priv->taglist);
    }
    priv->taglist = NULL;
  }

  buf = gst_buffer_make_metadata_writable (buf);
  if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (priv->base_ts))) {
    GST_BUFFER_TIMESTAMP (buf) =
        priv->base_ts +
        GST_FRAMES_TO_CLOCK_TIME (priv->samples, ctx->info.rate);
    GST_BUFFER_DURATION (buf) = priv->base_ts +
        GST_FRAMES_TO_CLOCK_TIME (priv->samples + samples, ctx->info.rate) -
        GST_BUFFER_TIMESTAMP (buf);
  } else {
    GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (buf) =
        GST_FRAMES_TO_CLOCK_TIME (samples, ctx->info.rate);
  }
  priv->samples += samples;
  priv->samples_out += samples;

  /* we got data, so note things are looking up */
  if (G_UNLIKELY (dec->priv->error_count))
    dec->priv->error_count--;

exit:
  return gst_base_audio_decoder_output (dec, buf);

  /* ERRORS */
wrong_buffer:
  {
    GST_ELEMENT_ERROR (dec, STREAM, ENCODE, (NULL),
        ("buffer size %d not a multiple of %d", GST_BUFFER_SIZE (buf),
            ctx->info.bpf));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
overflow:
  {
    GST_ELEMENT_ERROR (dec, STREAM, ENCODE,
        ("received more decoded frames %d than provided %d", frames,
            priv->frames.length), (NULL));
    if (buf)
      gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_base_audio_decoder_handle_frame (GstBaseAudioDecoder * dec,
    GstBaseAudioDecoderClass * klass, GstBuffer * buffer)
{
  if (G_LIKELY (buffer)) {
    /* keep around for admin */
    GST_LOG_OBJECT (dec, "tracking frame size %d, ts %" GST_TIME_FORMAT,
        GST_BUFFER_SIZE (buffer),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
    g_queue_push_tail (&dec->priv->frames, buffer);
    dec->priv->ctx.delay = dec->priv->frames.length;
    dec->priv->bytes_in += GST_BUFFER_SIZE (buffer);
  } else {
    GST_LOG_OBJECT (dec, "providing subclass with NULL frame");
  }

  return klass->handle_frame (dec, buffer);
}

/* maybe subclass configurable instead, but this allows for a whole lot of
 * raw samples, so at least quite some encoded ... */
#define GST_BASE_AUDIO_DECODER_MAX_SYNC     10 * 8 * 2 * 1024

static GstFlowReturn
gst_base_audio_decoder_push_buffers (GstBaseAudioDecoder * dec, gboolean force)
{
  GstBaseAudioDecoderClass *klass;
  GstBaseAudioDecoderPrivate *priv;
  GstBaseAudioDecoderContext *ctx;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buffer;
  gint av, flush;

  klass = GST_BASE_AUDIO_DECODER_GET_CLASS (dec);
  priv = dec->priv;
  ctx = &dec->priv->ctx;

  g_return_val_if_fail (klass->handle_frame != NULL, GST_FLOW_ERROR);

  av = gst_adapter_available (priv->adapter);
  GST_DEBUG_OBJECT (dec, "available: %d", av);

  while (ret == GST_FLOW_OK) {

    flush = 0;
    ctx->eos = force;

    if (G_LIKELY (av)) {
      gint len;
      GstClockTime ts;

      /* parse if needed */
      if (klass->parse) {
        gint offset = 0;

        /* limited (legacy) parsing; avoid whole of baseparse */
        GST_DEBUG_OBJECT (dec, "parsing available: %d", av);
        /* piggyback sync state on discont */
        ctx->sync = !priv->discont;
        ret = klass->parse (dec, priv->adapter, &offset, &len);

        g_assert (offset <= av);
        if (offset) {
          /* jumped a bit */
          GST_DEBUG_OBJECT (dec, "setting DISCONT");
          gst_adapter_flush (priv->adapter, offset);
          flush = offset;
          /* avoid parsing indefinitely */
          priv->sync_flush += offset;
          if (priv->sync_flush > GST_BASE_AUDIO_DECODER_MAX_SYNC)
            goto parse_failed;
        }

        if (ret == GST_FLOW_UNEXPECTED) {
          GST_LOG_OBJECT (dec, "no frame yet");
          ret = GST_FLOW_OK;
          break;
        } else if (ret == GST_FLOW_OK) {
          GST_LOG_OBJECT (dec, "frame at offset %d of length %d", offset, len);
          g_assert (offset + len <= av);
          priv->sync_flush = 0;
        } else {
          break;
        }
      } else {
        len = av;
      }
      /* track upstream ts, but do not get stuck if nothing new upstream */
      ts = gst_adapter_prev_timestamp (priv->adapter, NULL);
      if (ts == priv->prev_ts) {
        GST_LOG_OBJECT (dec, "ts == prev_ts; discarding");
        ts = GST_CLOCK_TIME_NONE;
      } else {
        priv->prev_ts = ts;
      }
      buffer = gst_adapter_take_buffer (priv->adapter, len);
      buffer = gst_buffer_make_metadata_writable (buffer);
      GST_BUFFER_TIMESTAMP (buffer) = ts;
      flush += len;
    } else {
      if (!force)
        break;
      buffer = NULL;
    }

    ret = gst_base_audio_decoder_handle_frame (dec, klass, buffer);

    /* do not keep pushing it ... */
    if (G_UNLIKELY (!av)) {
      priv->drained = TRUE;
      break;
    }

    av -= flush;
    g_assert (av >= 0);
  }

  GST_LOG_OBJECT (dec, "done pushing to subclass");
  return ret;

  /* ERRORS */
parse_failed:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("failed to parse stream"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_base_audio_decoder_drain (GstBaseAudioDecoder * dec)
{
  GstFlowReturn ret;

  if (dec->priv->drained)
    return GST_FLOW_OK;
  else {
    /* dispatch reverse pending buffers */
    /* chain eventually calls upon drain as well, but by that time
     * gather list should be clear, so ok ... */
    if (dec->segment.rate < 0.0 && dec->priv->gather)
      gst_base_audio_decoder_chain_reverse (dec, NULL);
    /* have subclass give all it can */
    ret = gst_base_audio_decoder_push_buffers (dec, TRUE);
    /* ensure all output sent */
    ret = gst_base_audio_decoder_output (dec, NULL);
    /* everything should be away now */
    if (dec->priv->frames.length) {
      /* not fatal/impossible though if subclass/codec eats stuff */
      GST_WARNING_OBJECT (dec, "still %d frames left after draining",
          dec->priv->frames.length);
      g_queue_foreach (&dec->priv->frames, (GFunc) gst_buffer_unref, NULL);
      g_queue_clear (&dec->priv->frames);
    }
    /* discard (unparsed) leftover */
    gst_adapter_clear (dec->priv->adapter);

    return ret;
  }
}

/* hard == FLUSH, otherwise discont */
static GstFlowReturn
gst_base_audio_decoder_flush (GstBaseAudioDecoder * dec, gboolean hard)
{
  GstBaseAudioDecoderClass *klass;
  GstFlowReturn ret = GST_FLOW_OK;

  klass = GST_BASE_AUDIO_DECODER_GET_CLASS (dec);

  GST_LOG_OBJECT (dec, "flush hard %d", hard);

  if (!hard) {
    ret = gst_base_audio_decoder_drain (dec);
  } else {
    gst_base_audio_decoder_clear_queues (dec);
    gst_segment_init (&dec->segment, GST_FORMAT_TIME);
    dec->priv->error_count = 0;
  }
  /* only bother subclass with flushing if known it is already alive
   * and kicking out stuff */
  if (klass->flush && dec->priv->samples_out > 0)
    klass->flush (dec, hard);
  /* and get (re)set for the sequel */
  gst_base_audio_decoder_reset (dec, FALSE);

  return ret;
}

static GstFlowReturn
gst_base_audio_decoder_chain_forward (GstBaseAudioDecoder * dec,
    GstBuffer * buffer)
{
  GstFlowReturn ret;

  /* grab buffer */
  gst_adapter_push (dec->priv->adapter, buffer);
  buffer = NULL;
  /* new stuff, so we can push subclass again */
  dec->priv->drained = FALSE;

  /* hand to subclass */
  ret = gst_base_audio_decoder_push_buffers (dec, FALSE);

  GST_LOG_OBJECT (dec, "chain-done");
  return ret;
}

static void
gst_base_audio_decoder_clear_queues (GstBaseAudioDecoder * dec)
{
  GstBaseAudioDecoderPrivate *priv = dec->priv;

  g_list_foreach (priv->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (priv->queued);
  priv->queued = NULL;
  g_list_foreach (priv->gather, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (priv->gather);
  priv->gather = NULL;
  g_list_foreach (priv->decode, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (priv->decode);
  priv->decode = NULL;
}

/*
 * Input:
 *  Buffer decoding order:  7  8  9  4  5  6  3  1  2  EOS
 *  Discont flag:           D        D        D  D
 *
 * - Each Discont marks a discont in the decoding order.
 *
 * for vorbis, each buffer is a keyframe when we have the previous
 * buffer. This means that to decode buffer 7, we need buffer 6, which
 * arrives out of order.
 *
 * we first gather buffers in the gather queue until we get a DISCONT. We
 * prepend each incomming buffer so that they are in reversed order.
 *
 *    gather queue:    9  8  7
 *    decode queue:
 *    output queue:
 *
 * When a DISCONT is received (buffer 4), we move the gather queue to the
 * decode queue. This is simply done be taking the head of the gather queue
 * and prepending it to the decode queue. This yields:
 *
 *    gather queue:
 *    decode queue:    7  8  9
 *    output queue:
 *
 * Then we decode each buffer in the decode queue in order and put the output
 * buffer in the output queue. The first buffer (7) will not produce any output
 * because it needs the previous buffer (6) which did not arrive yet. This
 * yields:
 *
 *    gather queue:
 *    decode queue:    7  8  9
 *    output queue:    9  8
 *
 * Then we remove the consumed buffers from the decode queue. Buffer 7 is not
 * completely consumed, we need to keep it around for when we receive buffer
 * 6. This yields:
 *
 *    gather queue:
 *    decode queue:    7
 *    output queue:    9  8
 *
 * Then we accumulate more buffers:
 *
 *    gather queue:    6  5  4
 *    decode queue:    7
 *    output queue:
 *
 * prepending to the decode queue on DISCONT yields:
 *
 *    gather queue:
 *    decode queue:    4  5  6  7
 *    output queue:
 *
 * after decoding and keeping buffer 4:
 *
 *    gather queue:
 *    decode queue:    4
 *    output queue:    7  6  5
 *
 * Etc..
 */
static GstFlowReturn
gst_base_audio_decoder_flush_decode (GstBaseAudioDecoder * dec)
{
  GstBaseAudioDecoderPrivate *priv = dec->priv;
  GstFlowReturn res = GST_FLOW_OK;
  GList *walk;

  walk = priv->decode;

  GST_DEBUG_OBJECT (dec, "flushing buffers to decoder");

  /* clear buffer and decoder state */
  gst_base_audio_decoder_flush (dec, FALSE);

  while (walk) {
    GList *next;
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    GST_DEBUG_OBJECT (dec, "decoding buffer %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    next = g_list_next (walk);
    /* decode buffer, resulting data prepended to output queue */
    gst_buffer_ref (buf);
    res = gst_base_audio_decoder_chain_forward (dec, buf);

    /* if we generated output, we can discard the buffer, else we
     * keep it in the queue */
    if (priv->queued) {
      GST_DEBUG_OBJECT (dec, "decoded buffer to %p", priv->queued->data);
      priv->decode = g_list_delete_link (priv->decode, walk);
      gst_buffer_unref (buf);
    } else {
      GST_DEBUG_OBJECT (dec, "buffer did not decode, keeping");
    }
    walk = next;
  }

  /* drain any aggregation (or otherwise) leftover */
  gst_base_audio_decoder_drain (dec);

  /* now send queued data downstream */
  while (priv->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (priv->queued->data);

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
      res = gst_pad_push (dec->srcpad, buf);
    } else {
      gst_buffer_unref (buf);
    }

    priv->queued = g_list_delete_link (priv->queued, priv->queued);
  }

  return res;
}

static GstFlowReturn
gst_base_audio_decoder_chain_reverse (GstBaseAudioDecoder * dec,
    GstBuffer * buf)
{
  GstBaseAudioDecoderPrivate *priv = dec->priv;
  GstFlowReturn result = GST_FLOW_OK;

  /* if we have a discont, move buffers to the decode list */
  if (!buf || GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    GST_DEBUG_OBJECT (dec, "received discont");
    while (priv->gather) {
      GstBuffer *gbuf;

      gbuf = GST_BUFFER_CAST (priv->gather->data);
      /* remove from the gather list */
      priv->gather = g_list_delete_link (priv->gather, priv->gather);
      /* copy to decode queue */
      priv->decode = g_list_prepend (priv->decode, gbuf);
    }
    /* decode stuff in the decode queue */
    gst_base_audio_decoder_flush_decode (dec);
  }

  if (G_LIKELY (buf)) {
    GST_DEBUG_OBJECT (dec, "gathering buffer %p of size %u, "
        "time %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT, buf,
        GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    /* add buffer to gather queue */
    priv->gather = g_list_prepend (priv->gather, buf);
  }

  return result;
}

static GstFlowReturn
gst_base_audio_decoder_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBaseAudioDecoder *dec;
  GstFlowReturn ret;

  dec = GST_BASE_AUDIO_DECODER (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (dec,
      "received buffer of size %d with ts %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT, GST_BUFFER_SIZE (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    gint64 samples, ts;

    /* track present position */
    ts = dec->priv->base_ts;
    samples = dec->priv->samples;

    GST_DEBUG_OBJECT (dec, "handling discont");
    gst_base_audio_decoder_flush (dec, FALSE);
    dec->priv->discont = TRUE;

    /* buffer may claim DISCONT loudly, if it can't tell us where we are now,
     * we'll stick to where we were ...
     * Particularly useful/needed for upstream BYTE based */
    if (dec->segment.rate > 0.0 && !GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
      GST_DEBUG_OBJECT (dec, "... but restoring previous ts tracking");
      dec->priv->base_ts = ts;
      dec->priv->samples = samples;
    }
  }

  if (dec->segment.rate > 0.0)
    ret = gst_base_audio_decoder_chain_forward (dec, buffer);
  else
    ret = gst_base_audio_decoder_chain_reverse (dec, buffer);

  return ret;
}

/* perform upstream byte <-> time conversion (duration, seeking)
 * if subclass allows and if enough data for moderately decent conversion */
static inline gboolean
gst_base_audio_decoder_do_byte (GstBaseAudioDecoder * dec)
{
  return dec->priv->ctx.do_byte_time && dec->priv->ctx.info.bpf &&
      dec->priv->ctx.info.rate <= dec->priv->samples_out;
}

static gboolean
gst_base_audio_decoder_sink_eventfunc (GstBaseAudioDecoder * dec,
    GstEvent * event)
{
  gboolean handled = FALSE;

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
        GST_DEBUG_OBJECT (dec, "received TIME NEW_SEGMENT %" GST_TIME_FORMAT
            " -- %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT
            ", rate %g, applied_rate %g",
            GST_TIME_ARGS (start), GST_TIME_ARGS (stop), GST_TIME_ARGS (time),
            rate, arate);
      } else {
        GstFormat dformat = GST_FORMAT_TIME;

        GST_DEBUG_OBJECT (dec, "received NEW_SEGMENT %" G_GINT64_FORMAT
            " -- %" G_GINT64_FORMAT ", time %" G_GINT64_FORMAT
            ", rate %g, applied_rate %g", start, stop, time, rate, arate);
        /* handle newsegment resulting from legacy simple seeking */
        /* note that we need to convert this whether or not enough data
         * to handle initial newsegment */
        if (dec->priv->ctx.do_byte_time &&
            gst_pad_query_convert (dec->sinkpad, GST_FORMAT_BYTES, start,
                &dformat, &start)) {
          /* best attempt convert */
          /* as these are only estimates, stop is kept open-ended to avoid
           * premature cutting */
          GST_DEBUG_OBJECT (dec, "converted to TIME start %" GST_TIME_FORMAT,
              GST_TIME_ARGS (start));
          format = GST_FORMAT_TIME;
          time = start;
          stop = GST_CLOCK_TIME_NONE;
          /* replace event */
          gst_event_unref (event);
          event = gst_event_new_new_segment_full (update, rate, arate,
              GST_FORMAT_TIME, start, stop, time);
        } else {
          GST_DEBUG_OBJECT (dec, "unsupported format; ignoring");
          break;
        }
      }

      /* finish current segment */
      gst_base_audio_decoder_drain (dec);

      if (update) {
        /* time progressed without data, see if we can fill the gap with
         * some concealment data */
        GST_DEBUG_OBJECT (dec,
            "segment update: plc %d, do_plc %d, last_stop %" GST_TIME_FORMAT,
            dec->priv->plc, dec->priv->ctx.do_plc,
            GST_TIME_ARGS (dec->segment.last_stop));
        if (dec->priv->plc && dec->priv->ctx.do_plc &&
            dec->segment.rate > 0.0 && dec->segment.last_stop < start) {
          GstBaseAudioDecoderClass *klass;
          GstBuffer *buf;

          klass = GST_BASE_AUDIO_DECODER_GET_CLASS (dec);
          /* hand subclass empty frame with duration that needs covering */
          buf = gst_buffer_new ();
          GST_BUFFER_DURATION (buf) = start - dec->segment.last_stop;
          /* best effort, not much error handling */
          gst_base_audio_decoder_handle_frame (dec, klass, buf);
        }
      } else {
        /* prepare for next one */
        gst_base_audio_decoder_flush (dec, FALSE);
        /* and that's where we time from,
         * in case upstream does not come up with anything better
         * (e.g. upstream BYTE) */
        if (format != GST_FORMAT_TIME) {
          dec->priv->base_ts = start;
          dec->priv->samples = 0;
        }
      }

      /* and follow along with segment */
      gst_segment_set_newsegment_full (&dec->segment, update, rate, arate,
          format, start, stop, time);

      gst_pad_push_event (dec->srcpad, event);
      handled = TRUE;
      break;
    }

    case GST_EVENT_FLUSH_START:
      break;

    case GST_EVENT_FLUSH_STOP:
      /* prepare for fresh start */
      gst_base_audio_decoder_flush (dec, TRUE);
      break;

    case GST_EVENT_EOS:
      gst_base_audio_decoder_drain (dec);
      break;

    default:
      break;
  }

  return handled;
}

static gboolean
gst_base_audio_decoder_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseAudioDecoder *dec;
  GstBaseAudioDecoderClass *klass;
  gboolean handled = FALSE;
  gboolean ret = TRUE;

  dec = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  klass = GST_BASE_AUDIO_DECODER_GET_CLASS (dec);

  GST_DEBUG_OBJECT (dec, "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  if (klass->event)
    handled = klass->event (dec, event);

  if (!handled)
    handled = gst_base_audio_decoder_sink_eventfunc (dec, event);

  if (!handled)
    ret = gst_pad_event_default (pad, event);

  GST_DEBUG_OBJECT (dec, "event handled");

  gst_object_unref (dec);
  return ret;
}

static gboolean
gst_base_audio_decoder_do_seek (GstBaseAudioDecoder * dec, GstEvent * event)
{
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

  memcpy (&seek_segment, &dec->segment, sizeof (seek_segment));
  gst_segment_set_seek (&seek_segment, rate, format, flags, start_type,
      start_time, end_type, end_time, NULL);
  start_time = seek_segment.last_stop;

  format = GST_FORMAT_BYTES;
  if (!gst_pad_query_convert (dec->sinkpad, GST_FORMAT_TIME, start_time,
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

  return gst_pad_push_event (dec->sinkpad, event);
}

static gboolean
gst_base_audio_decoder_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseAudioDecoder *dec;
  GstBaseAudioDecoderClass *klass;
  gboolean res = FALSE;

  dec = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  klass = GST_BASE_AUDIO_DECODER_GET_CLASS (dec);

  GST_DEBUG_OBJECT (dec, "received event %d, %s", GST_EVENT_TYPE (event),
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
      if ((res = gst_pad_push_event (dec->sinkpad, event)))
        break;

      /* if upstream fails for a time seek, maybe we can help if allowed */
      if (format == GST_FORMAT_TIME) {
        if (gst_base_audio_decoder_do_byte (dec))
          res = gst_base_audio_decoder_do_seek (dec, event);
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

      res = gst_pad_push_event (dec->sinkpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (dec->sinkpad, event);
      break;
  }
done:
  gst_object_unref (dec);

  return res;

  /* ERRORS */
convert_error:
  {
    GST_DEBUG_OBJECT (dec, "cannot convert start/stop for seek");
    goto done;
  }
}

static gboolean
gst_base_audio_decoder_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstBaseAudioDecoder *dec;

  dec = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
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
      if (!(res = gst_base_audio_encoded_audio_convert (&dec->priv->ctx.info,
                  dec->priv->bytes_in, dec->priv->samples_out,
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
  gst_object_unref (dec);
  return res;
}

static const GstQueryType *
gst_base_audio_decoder_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_base_audio_decoder_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_LATENCY,
    0
  };

  return gst_base_audio_decoder_src_query_types;
}

/* FIXME ? are any of these queries (other than latency) a decoder's business ??
 * also, the conversion stuff might seem to make sense, but seems to not mind
 * segment stuff etc at all
 * Supposedly that's backward compatibility ... */
static gboolean
gst_base_audio_decoder_src_query (GstPad * pad, GstQuery * query)
{
  GstBaseAudioDecoder *dec;
  GstPad *peerpad;
  gboolean res = FALSE;

  dec = GST_BASE_AUDIO_DECODER (GST_PAD_PARENT (pad));
  peerpad = gst_pad_get_peer (GST_PAD (dec->sinkpad));

  GST_LOG_OBJECT (dec, "handling query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      /* upstream in any case */
      if ((res = gst_pad_query_default (pad, query)))
        break;

      gst_query_parse_duration (query, &format, NULL);
      /* try answering TIME by converting from BYTE if subclass allows  */
      if (format == GST_FORMAT_TIME && gst_base_audio_decoder_do_byte (dec)) {
        gint64 value;

        format = GST_FORMAT_BYTES;
        if (gst_pad_query_peer_duration (dec->sinkpad, &format, &value)) {
          GST_LOG_OBJECT (dec, "upstream size %" G_GINT64_FORMAT, value);
          format = GST_FORMAT_TIME;
          if (gst_pad_query_convert (dec->sinkpad, GST_FORMAT_BYTES, value,
                  &format, &value)) {
            gst_query_set_duration (query, GST_FORMAT_TIME, value);
            res = TRUE;
          }
        }
      }
      break;
    }
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time, value;

      if ((res = gst_pad_peer_query (dec->sinkpad, query))) {
        GST_LOG_OBJECT (dec, "returning peer response");
        break;
      }

      /* we start from the last seen time */
      time = dec->segment.last_stop;
      /* correct for the segment values */
      time = gst_segment_to_stream_time (&dec->segment, GST_FORMAT_TIME, time);

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
      if (!(res = gst_base_audio_raw_audio_convert (&dec->priv->ctx.info,
                  src_fmt, src_val, &dest_fmt, &dest_val)))
        break;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      if ((res = gst_pad_peer_query (dec->sinkpad, query))) {
        gboolean live;
        GstClockTime min_latency, max_latency;

        gst_query_parse_latency (query, &live, &min_latency, &max_latency);
        GST_DEBUG_OBJECT (dec, "Peer latency: live %d, min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT, live,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        GST_OBJECT_LOCK (dec);
        /* add our latency */
        if (min_latency != -1)
          min_latency += dec->priv->ctx.min_latency;
        if (max_latency != -1)
          max_latency += dec->priv->ctx.max_latency;
        GST_OBJECT_UNLOCK (dec);

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

static gboolean
gst_base_audio_decoder_stop (GstBaseAudioDecoder * dec)
{
  GstBaseAudioDecoderClass *klass;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "gst_base_audio_decoder_stop");

  klass = GST_BASE_AUDIO_DECODER_GET_CLASS (dec);

  if (klass->stop) {
    ret = klass->stop (dec);
  }

  /* clean up */
  gst_base_audio_decoder_reset (dec, TRUE);

  if (ret)
    dec->priv->active = FALSE;

  return TRUE;
}

static gboolean
gst_base_audio_decoder_start (GstBaseAudioDecoder * dec)
{
  GstBaseAudioDecoderClass *klass;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "gst_base_audio_decoder_start");

  klass = GST_BASE_AUDIO_DECODER_GET_CLASS (dec);

  /* arrange clean state */
  gst_base_audio_decoder_reset (dec, TRUE);

  if (klass->start) {
    ret = klass->start (dec);
  }

  if (ret)
    dec->priv->active = TRUE;

  return TRUE;
}

static void
gst_base_audio_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseAudioDecoder *dec;

  dec = GST_BASE_AUDIO_DECODER (object);

  switch (prop_id) {
    case PROP_LATENCY:
      g_value_set_int64 (value, dec->priv->latency);
      break;
    case PROP_TOLERANCE:
      g_value_set_int64 (value, dec->priv->tolerance);
      break;
    case PROP_PLC:
      g_value_set_boolean (value, dec->priv->plc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_audio_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseAudioDecoder *dec;

  dec = GST_BASE_AUDIO_DECODER (object);

  switch (prop_id) {
    case PROP_LATENCY:
      dec->priv->latency = g_value_get_int64 (value);
      break;
    case PROP_TOLERANCE:
      dec->priv->tolerance = g_value_get_int64 (value);
      break;
    case PROP_PLC:
      dec->priv->plc = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_base_audio_decoder_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseAudioDecoder *codec;
  GstBaseAudioDecoderClass *codec_class;
  GstStateChangeReturn ret;

  codec = GST_BASE_AUDIO_DECODER (element);
  codec_class = GST_BASE_AUDIO_DECODER_GET_CLASS (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_base_audio_decoder_start (codec)) {
        goto start_failed;
      }
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
      if (!gst_base_audio_decoder_stop (codec)) {
        goto stop_failed;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;

start_failed:
  {
    GST_ELEMENT_ERROR (codec, LIBRARY, INIT, (NULL), ("Failed to start codec"));
    return GST_STATE_CHANGE_FAILURE;
  }
stop_failed:
  {
    GST_ELEMENT_ERROR (codec, LIBRARY, INIT, (NULL), ("Failed to stop codec"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

GstFlowReturn
_gst_base_audio_decoder_error (GstBaseAudioDecoder * dec, gint weight,
    GQuark domain, gint code, gchar * txt, gchar * dbg, const gchar * file,
    const gchar * function, gint line)
{
  if (txt)
    GST_WARNING_OBJECT (dec, "error: %s", txt);
  if (dbg)
    GST_WARNING_OBJECT (dec, "error: %s", dbg);
  dec->priv->error_count += weight;
  dec->priv->discont = TRUE;
  if (dec->priv->ctx.max_errors < dec->priv->error_count) {
    gst_element_message_full (GST_ELEMENT (dec), GST_MESSAGE_ERROR,
        domain, code, txt, dbg, file, function, line);
    return GST_FLOW_ERROR;
  } else {
    return GST_FLOW_OK;
  }
}

/**
 * gst_base_audio_decoder_get_info:
 * @dec: a #GstBaseAudioDecoder
 *
 * Returns: a #AudioFormatInfo describing input audio format
 */
GstAudioFormatInfo *
gst_base_audio_decoder_get_info (GstBaseAudioDecoder * dec)
{
  g_return_val_if_fail (GST_IS_BASE_AUDIO_DECODER (dec), NULL);

  return &dec->priv->ctx.info;
}

/**
 * gst_base_audio_decoder_set_plc_aware:
 * @dec: a #GstBaseAudioDecoder
 * @plc: new plc state
 *
 * Indicates whether or not subclass handles packet loss concealment (plc).
 */
void
gst_base_audio_decoder_set_plc_aware (GstBaseAudioDecoder * dec, gboolean plc)
{
  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (dec));

  dec->priv->ctx.do_plc = plc;
}

/**
 * gst_base_audio_decoder_get_plc_aware:
 * @dec: a #GstBaseAudioDecoder
 *
 * Returns: currently configured plc handling
 */
gint
gst_base_audio_decoder_get_plc_aware (GstBaseAudioDecoder * dec)
{
  g_return_val_if_fail (GST_IS_BASE_AUDIO_DECODER (dec), 0);

  return dec->priv->ctx.do_plc;
}

/**
 * gst_base_audio_decoder_set_byte_time:
 * @dec: a #GstBaseAudioDecoder
 * @enabled: whether to enable byte to time conversion
 *
 * Allows baseclass to perform byte to time estimated conversion.
 */
void
gst_base_audio_decoder_set_byte_time (GstBaseAudioDecoder * dec,
    gboolean enabled)
{
  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (dec));

  dec->priv->ctx.do_byte_time = enabled;
}

/**
 * gst_base_audio_decoder_get_byte_time:
 * @dec: a #GstBaseAudioDecoder
 *
 * Returns: currently configured byte to time conversion setting
 */
gint
gst_base_audio_decoder_get_byte_time (GstBaseAudioDecoder * dec)
{
  g_return_val_if_fail (GST_IS_BASE_AUDIO_DECODER (dec), 0);

  return dec->priv->ctx.do_byte_time;
}

/**
 * gst_base_audio_decoder_get_delay:
 * @dec: a #GstBaseAudioDecoder
 *
 * Returns: currently configured decoder delay
 */
gint
gst_base_audio_decoder_get_delay (GstBaseAudioDecoder * dec)
{
  g_return_val_if_fail (GST_IS_BASE_AUDIO_DECODER (dec), 0);

  return dec->priv->ctx.delay;
}

/**
 * gst_base_audio_decoder_set_max_errors:
 * @dec: a #GstBaseAudioDecoder
 * @num: max tolerated errors
 *
 * Sets numbers of tolerated decoder errors, where a tolerated one is then only
 * warned about, but more than tolerated will lead to fatal error.
 */
void
gst_base_audio_decoder_set_max_errors (GstBaseAudioDecoder * enc, gint num)
{
  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (enc));

  enc->priv->ctx.max_errors = num;
}

/**
 * gst_base_audio_decoder_get_max_errors:
 * @dec: a #GstBaseAudioDecoder
 *
 * Returns: currently configured decoder tolerated error count.
 */
gint
gst_base_audio_decoder_get_max_errors (GstBaseAudioDecoder * dec)
{
  g_return_val_if_fail (GST_IS_BASE_AUDIO_DECODER (dec), 0);

  return dec->priv->ctx.max_errors;
}

/**
 * gst_base_audio_decoder_set_latency:
 * @dec: a #GstBaseAudioDecoder
 * @min: minimum latency
 * @max: maximum latency
 *
 * Sets decoder latency.
 */
void
gst_base_audio_decoder_set_latency (GstBaseAudioDecoder * dec,
    GstClockTime min, GstClockTime max)
{
  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (dec));

  GST_OBJECT_LOCK (dec);
  dec->priv->ctx.min_latency = min;
  dec->priv->ctx.max_latency = max;
  GST_OBJECT_UNLOCK (dec);
}

/**
 * gst_base_audio_decoder_get_latency:
 * @dec: a #GstBaseAudioDecoder
 * @min: a pointer to storage to hold minimum latency
 * @max: a pointer to storage to hold maximum latency
 *
 * Returns currently configured latency.
 */
void
gst_base_audio_decoder_get_latency (GstBaseAudioDecoder * dec,
    GstClockTime * min, GstClockTime * max)
{
  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (dec));

  GST_OBJECT_LOCK (dec);
  if (min)
    *min = dec->priv->ctx.min_latency;
  if (max)
    *max = dec->priv->ctx.max_latency;
  GST_OBJECT_UNLOCK (dec);
}

/**
 * gst_base_audio_decoder_get_parse_state:
 * @dec: a #GstBaseAudioDecoder
 * @min: a pointer to storage to hold current sync state
 * @max: a pointer to storage to hold current eos state
 *
 * Return current parsing (sync and eos) state.
 */
void
gst_base_audio_decoder_get_parse_state (GstBaseAudioDecoder * dec,
    gboolean * sync, gboolean * eos)
{
  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (dec));

  if (sync)
    *sync = dec->priv->ctx.sync;
  if (eos)
    *eos = dec->priv->ctx.eos;
}

/**
 * gst_base_audio_decoder_set_plc:
 * @dec: a #GstBaseAudioDecoder
 * @enabled: new state
 *
 * Enable or disable decoder packet loss concealment, provided subclass
 * and codec are capable and allow handling plc.
 *
 * MT safe.
 *
 */
void
gst_base_audio_decoder_set_plc (GstBaseAudioDecoder * dec, gboolean enabled)
{
  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (dec));

  GST_LOG_OBJECT (dec, "enabled: %d", enabled);

  GST_OBJECT_LOCK (dec);
  dec->priv->plc = enabled;
  GST_OBJECT_UNLOCK (dec);
}

/**
 * gst_base_audio_decoder_get_plc:
 * @dec: a #GstBaseAudioDecoder
 *
 * Queries decoder packet loss concealment handling.
 *
 * Returns: TRUE if packet loss concealment is enabled.
 *
 * MT safe.
 */
gboolean
gst_base_audio_decoder_get_plc (GstBaseAudioDecoder * dec)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_BASE_AUDIO_DECODER (dec), FALSE);

  GST_OBJECT_LOCK (dec);
  result = dec->priv->plc;
  GST_OBJECT_UNLOCK (dec);

  return result;
}

/**
 * gst_base_audio_decoder_set_min_latency:
 * @dec: a #GstBaseAudioDecoder
 * @num: new minimum latency
 *
 * Sets decoder minimum aggregation latency.
 *
 * MT safe.
 *
 */
void
gst_base_audio_decoder_set_min_latency (GstBaseAudioDecoder * dec, gint64 num)
{
  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (dec));

  GST_OBJECT_LOCK (dec);
  dec->priv->latency = num;
  GST_OBJECT_UNLOCK (dec);
}

/**
 * gst_base_audio_decoder_get_min_latency:
 * @enc: a #GstBaseAudioDecoder
 *
 * Queries decoder's latency aggregation.
 *
 * Returns: aggregation latency.
 *
 * MT safe.
 */
gint64
gst_base_audio_decoder_get_min_latency (GstBaseAudioDecoder * dec)
{
  gint64 result;

  g_return_val_if_fail (GST_IS_BASE_AUDIO_DECODER (dec), FALSE);

  GST_OBJECT_LOCK (dec);
  result = dec->priv->latency;
  GST_OBJECT_UNLOCK (dec);

  return result;
}

/**
 * gst_base_audio_decoder_set_tolerance:
 * @dec: a #GstBaseAudioDecoder
 * @tolerance: new tolerance
 *
 * Configures decoder audio jitter tolerance threshold.
 *
 * MT safe.
 *
 */
void
gst_base_audio_decoder_set_tolerance (GstBaseAudioDecoder * dec,
    gint64 tolerance)
{
  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (dec));

  GST_OBJECT_LOCK (dec);
  dec->priv->tolerance = tolerance;
  GST_OBJECT_UNLOCK (dec);
}

/**
 * gst_base_audio_decoder_get_tolerance:
 * @dec: a #GstBaseAudioDecoder
 *
 * Queries current audio jitter tolerance threshold.
 *
 * Returns: decoder audio jitter tolerance threshold.
 *
 * MT safe.
 */
gint64
gst_base_audio_decoder_get_tolerance (GstBaseAudioDecoder * dec)
{
  gint64 result;

  g_return_val_if_fail (GST_IS_BASE_AUDIO_DECODER (dec), 0);

  GST_OBJECT_LOCK (dec);
  result = dec->priv->tolerance;
  GST_OBJECT_UNLOCK (dec);

  return result;
}
