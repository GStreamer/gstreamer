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
 * SECTION:gstvideoencoder
 * @short_description: Base class for video encoders
 * @see_also:
 *
 * This base class is for video encoders turning raw video into
 * encoded video data.
 *
 * GstVideoEncoder and subclass should cooperate as follows.
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Configuration</title>
 *   <listitem><para>
 *     Initially, GstVideoEncoder calls @start when the encoder element
 *     is activated, which allows subclass to perform any global setup.
 *   </para></listitem>
 *   <listitem><para>
 *     GstVideoEncoder calls @set_format to inform subclass of the format
 *     of input video data that it is about to receive.  Subclass should
 *     setup for encoding and configure base class as appropriate
 *     (e.g. latency). While unlikely, it might be called more than once,
 *     if changing input parameters require reconfiguration.  Baseclass
 *     will ensure that processing of current configuration is finished.
 *   </para></listitem>
 *   <listitem><para>
 *     GstVideoEncoder calls @stop at end of all processing.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist>
 *   <title>Data processing</title>
 *     <listitem><para>
 *       Base class collects input data and metadata into a frame and hands
 *       this to subclass' @handle_frame.
 *     </para></listitem>
 *     <listitem><para>
 *       If codec processing results in encoded data, subclass should call
 *       @gst_video_encoder_finish_frame to have encoded data pushed
 *       downstream.
 *     </para></listitem>
 *     <listitem><para>
 *       If implemented, baseclass calls subclass @pre_push just prior to
 *       pushing to allow subclasses to modify some metadata on the buffer.
 *       If it returns GST_FLOW_OK, the buffer is pushed downstream.
 *     </para></listitem>
 *     <listitem><para>
 *       GstVideoEncoderClass will handle both srcpad and sinkpad events.
 *       Sink events will be passed to subclass if @event callback has been
 *       provided.
 *     </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Shutdown phase</title>
 *   <listitem><para>
 *     GstVideoEncoder class calls @stop to inform the subclass that data
 *     parsing will be stopped.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * </orderedlist>
 *
 * Subclass is responsible for providing pad template caps for
 * source and sink pads. The pads need to be named "sink" and "src". It should
 * also be able to provide fixed src pad caps in @getcaps by the time it calls
 * @gst_video_encoder_finish_frame.
 *
 * Things that subclass need to take care of:
 * <itemizedlist>
 *   <listitem><para>Provide pad templates</para></listitem>
 *   <listitem><para>
 *      Provide source pad caps before pushing the first buffer
 *   </para></listitem>
 *   <listitem><para>
 *      Accept data in @handle_frame and provide encoded results to
 *      @gst_video_encoder_finish_frame.
 *   </para></listitem>
 * </itemizedlist>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* TODO
 *
 * * Change _set_output_format() to steal the reference of the provided caps
 * * Calculate actual latency based on input/output timestamp/frame_number
 *   and if it exceeds the recorded one, save it and emit a GST_MESSAGE_LATENCY
 */

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gstvideoencoder.h"
#include "gstvideoutils.h"

#include <gst/video/gstvideometa.h>

#include <string.h>

GST_DEBUG_CATEGORY (videoencoder_debug);
#define GST_CAT_DEFAULT videoencoder_debug

#define GST_VIDEO_ENCODER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_VIDEO_ENCODER, \
        GstVideoEncoderPrivate))

struct _GstVideoEncoderPrivate
{
  guint64 presentation_frame_number;
  int distance_from_sync;

  /* FIXME : (and introduce a context ?) */
  gboolean drained;
  gboolean at_eos;

  gint64 min_latency;
  gint64 max_latency;

  GList *current_frame_events;

  GList *headers;
  gboolean new_headers;         /* Whether new headers were just set */

  GList *force_key_unit;        /* List of pending forced keyunits */

  guint64 system_frame_number;

  GList *frames;                /* Protected with OBJECT_LOCK */
  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;
  gboolean output_state_changed;

  gint64 bytes;
  gint64 time;
};

typedef struct _ForcedKeyUnitEvent ForcedKeyUnitEvent;
struct _ForcedKeyUnitEvent
{
  GstClockTime running_time;
  gboolean pending;             /* TRUE if this was requested already */
  gboolean all_headers;
  guint count;
};

static void
forced_key_unit_event_free (ForcedKeyUnitEvent * evt)
{
  g_slice_free (ForcedKeyUnitEvent, evt);
}

static ForcedKeyUnitEvent *
forced_key_unit_event_new (GstClockTime running_time, gboolean all_headers,
    guint count)
{
  ForcedKeyUnitEvent *evt = g_slice_new0 (ForcedKeyUnitEvent);

  evt->running_time = running_time;
  evt->all_headers = all_headers;
  evt->count = count;

  return evt;
}

static GstElementClass *parent_class = NULL;
static void gst_video_encoder_class_init (GstVideoEncoderClass * klass);
static void gst_video_encoder_init (GstVideoEncoder * enc,
    GstVideoEncoderClass * klass);

static void gst_video_encoder_finalize (GObject * object);

static gboolean gst_video_encoder_setcaps (GstVideoEncoder * enc,
    GstCaps * caps);
static GstCaps *gst_video_encoder_sink_getcaps (GstVideoEncoder * encoder,
    GstCaps * filter);
static gboolean gst_video_encoder_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_video_encoder_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_video_encoder_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static GstStateChangeReturn gst_video_encoder_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_video_encoder_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_video_encoder_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstVideoCodecFrame *gst_video_encoder_new_frame (GstVideoEncoder *
    encoder, GstBuffer * buf, GstClockTime timestamp, GstClockTime duration);

static gboolean gst_video_encoder_sink_event_default (GstVideoEncoder * encoder,
    GstEvent * event);
static gboolean gst_video_encoder_src_event_default (GstVideoEncoder * encoder,
    GstEvent * event);
static gboolean gst_video_encoder_propose_allocation_default (GstVideoEncoder *
    encoder, GstQuery * query);

/* we can't use G_DEFINE_ABSTRACT_TYPE because we need the klass in the _init
 * method to get to the padtemplates */
GType
gst_video_encoder_get_type (void)
{
  static volatile gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (GstVideoEncoderClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_video_encoder_class_init,
      NULL,
      NULL,
      sizeof (GstVideoEncoder),
      0,
      (GInstanceInitFunc) gst_video_encoder_init,
    };
    const GInterfaceInfo preset_interface_info = {
      NULL,                     /* interface_init */
      NULL,                     /* interface_finalize */
      NULL                      /* interface_data */
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstVideoEncoder", &info, G_TYPE_FLAG_ABSTRACT);
    g_type_add_interface_static (_type, GST_TYPE_PRESET,
        &preset_interface_info);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static void
gst_video_encoder_class_init (GstVideoEncoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (videoencoder_debug, "videoencoder", 0,
      "Base Video Encoder");

  parent_class = g_type_class_peek_parent (klass);

  g_type_class_add_private (klass, sizeof (GstVideoEncoderPrivate));

  gobject_class->finalize = gst_video_encoder_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_video_encoder_change_state);

  klass->sink_event = gst_video_encoder_sink_event_default;
  klass->src_event = gst_video_encoder_src_event_default;
  klass->propose_allocation = gst_video_encoder_propose_allocation_default;
}

static void
gst_video_encoder_reset (GstVideoEncoder * encoder)
{
  GstVideoEncoderPrivate *priv = encoder->priv;
  GList *g;

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  priv->presentation_frame_number = 0;
  priv->distance_from_sync = 0;

  g_list_foreach (priv->force_key_unit, (GFunc) forced_key_unit_event_free,
      NULL);
  g_list_free (priv->force_key_unit);
  priv->force_key_unit = NULL;

  priv->drained = TRUE;
  priv->min_latency = 0;
  priv->max_latency = 0;

  g_list_foreach (priv->headers, (GFunc) gst_event_unref, NULL);
  g_list_free (priv->headers);
  priv->headers = NULL;
  priv->new_headers = FALSE;

  g_list_foreach (priv->current_frame_events, (GFunc) gst_event_unref, NULL);
  g_list_free (priv->current_frame_events);
  priv->current_frame_events = NULL;

  for (g = priv->frames; g; g = g->next) {
    gst_video_codec_frame_unref ((GstVideoCodecFrame *) g->data);
  }
  g_list_free (priv->frames);
  priv->frames = NULL;

  priv->bytes = 0;
  priv->time = 0;

  if (priv->input_state)
    gst_video_codec_state_unref (priv->input_state);
  priv->input_state = NULL;
  if (priv->output_state)
    gst_video_codec_state_unref (priv->output_state);
  priv->output_state = NULL;

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
}

static void
gst_video_encoder_init (GstVideoEncoder * encoder, GstVideoEncoderClass * klass)
{
  GstVideoEncoderPrivate *priv;
  GstPadTemplate *pad_template;
  GstPad *pad;

  GST_DEBUG_OBJECT (encoder, "gst_video_encoder_init");

  priv = encoder->priv = GST_VIDEO_ENCODER_GET_PRIVATE (encoder);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  encoder->sinkpad = pad = gst_pad_new_from_template (pad_template, "sink");

  gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_video_encoder_chain));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_encoder_sink_event));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_encoder_sink_query));
  gst_element_add_pad (GST_ELEMENT (encoder), encoder->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  encoder->srcpad = pad = gst_pad_new_from_template (pad_template, "src");

  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_encoder_src_query));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_encoder_src_event));
  gst_element_add_pad (GST_ELEMENT (encoder), encoder->srcpad);

  gst_segment_init (&encoder->input_segment, GST_FORMAT_TIME);
  gst_segment_init (&encoder->output_segment, GST_FORMAT_TIME);

  g_rec_mutex_init (&encoder->stream_lock);

  priv->at_eos = FALSE;
  priv->headers = NULL;
  priv->new_headers = FALSE;

  gst_video_encoder_reset (encoder);
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

/**
 * gst_video_encoder_set_headers:
 * @encoder: a #GstVideoEncoder
 * @headers: (transfer full) (element-type GstBuffer): a list of #GstBuffer containing the codec header
 *
 * Set the codec headers to be sent downstream whenever requested.
 *
 * Since: 0.10.36
 */
void
gst_video_encoder_set_headers (GstVideoEncoder * video_encoder, GList * headers)
{
  GST_VIDEO_ENCODER_STREAM_LOCK (video_encoder);

  GST_DEBUG_OBJECT (video_encoder, "new headers %p", headers);
  if (video_encoder->priv->headers) {
    g_list_foreach (video_encoder->priv->headers, (GFunc) gst_buffer_unref,
        NULL);
    g_list_free (video_encoder->priv->headers);
  }
  video_encoder->priv->headers = headers;
  video_encoder->priv->new_headers = TRUE;

  GST_VIDEO_ENCODER_STREAM_UNLOCK (video_encoder);
}

static gboolean
gst_video_encoder_drain (GstVideoEncoder * enc)
{
  GstVideoEncoderPrivate *priv;
  GstVideoEncoderClass *enc_class;
  gboolean ret = TRUE;

  enc_class = GST_VIDEO_ENCODER_GET_CLASS (enc);
  priv = enc->priv;

  GST_DEBUG_OBJECT (enc, "draining");

  if (priv->drained) {
    GST_DEBUG_OBJECT (enc, "already drained");
    return TRUE;
  }

  if (enc_class->reset) {
    GST_DEBUG_OBJECT (enc, "requesting subclass to finish");
    ret = enc_class->reset (enc, TRUE);
  }
  /* everything should be away now */
  if (priv->frames) {
    /* not fatal/impossible though if subclass/enc eats stuff */
    g_list_foreach (priv->frames, (GFunc) gst_video_codec_frame_unref, NULL);
    g_list_free (priv->frames);
    priv->frames = NULL;
  }

  return ret;
}

static GstVideoCodecState *
_new_output_state (GstCaps * caps, GstVideoCodecState * reference)
{
  GstVideoCodecState *state;

  state = g_slice_new0 (GstVideoCodecState);
  state->ref_count = 1;
  gst_video_info_init (&state->info);
  gst_video_info_set_format (&state->info, GST_VIDEO_FORMAT_ENCODED, 0, 0);

  state->caps = caps;

  if (reference) {
    GstVideoInfo *tgt, *ref;

    tgt = &state->info;
    ref = &reference->info;

    /* Copy over extra fields from reference state */
    tgt->interlace_mode = ref->interlace_mode;
    tgt->flags = ref->flags;
    tgt->width = ref->width;
    tgt->height = ref->height;
    tgt->chroma_site = ref->chroma_site;
    tgt->colorimetry = ref->colorimetry;
    tgt->par_n = ref->par_n;
    tgt->par_d = ref->par_d;
    tgt->fps_n = ref->fps_n;
    tgt->fps_d = ref->fps_d;
  }

  return state;
}

static GstVideoCodecState *
_new_input_state (GstCaps * caps)
{
  GstVideoCodecState *state;

  state = g_slice_new0 (GstVideoCodecState);
  state->ref_count = 1;
  gst_video_info_init (&state->info);
  if (G_UNLIKELY (!gst_video_info_from_caps (&state->info, caps)))
    goto parse_fail;
  state->caps = gst_caps_ref (caps);

  return state;

parse_fail:
  {
    g_slice_free (GstVideoCodecState, state);
    return NULL;
  }
}

static gboolean
gst_video_encoder_setcaps (GstVideoEncoder * encoder, GstCaps * caps)
{
  GstVideoEncoderClass *encoder_class;
  GstVideoCodecState *state;
  gboolean ret;
  gboolean samecaps = FALSE;

  encoder_class = GST_VIDEO_ENCODER_GET_CLASS (encoder);

  /* subclass should do something here ... */
  g_return_val_if_fail (encoder_class->set_format != NULL, FALSE);

  GST_DEBUG_OBJECT (encoder, "setcaps %" GST_PTR_FORMAT, caps);

  state = _new_input_state (caps);
  if (G_UNLIKELY (!state))
    goto parse_fail;

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  if (encoder->priv->input_state)
    samecaps =
        gst_video_info_is_equal (&state->info,
        &encoder->priv->input_state->info);

  if (!samecaps) {
    /* arrange draining pending frames */
    gst_video_encoder_drain (encoder);

    /* and subclass should be ready to configure format at any time around */
    ret = encoder_class->set_format (encoder, state);
    if (ret) {
      if (encoder->priv->input_state)
        gst_video_codec_state_unref (encoder->priv->input_state);
      encoder->priv->input_state = state;
    } else
      gst_video_codec_state_unref (state);
  } else {
    /* no need to stir things up */
    GST_DEBUG_OBJECT (encoder,
        "new video format identical to configured format");
    gst_video_codec_state_unref (state);
    ret = TRUE;
  }

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  if (!ret)
    GST_WARNING_OBJECT (encoder, "rejected caps %" GST_PTR_FORMAT, caps);

  return ret;

parse_fail:
  {
    GST_WARNING_OBJECT (encoder, "Failed to parse caps");
    return FALSE;
  }
}

/**
 * gst_video_encoder_proxy_getcaps:
 * @enc: a #GstVideoEncoder
 * @caps: initial caps
 *
 * Returns caps that express @caps (or sink template caps if @caps == NULL)
 * restricted to resolution/format/... combinations supported by downstream
 * elements (e.g. muxers).
 *
 * Returns: a #GstCaps owned by caller
 *
 * Since: 0.10.36
 */
GstCaps *
gst_video_encoder_proxy_getcaps (GstVideoEncoder * encoder, GstCaps * caps,
    GstCaps * filter)
{
  GstCaps *templ_caps;
  GstCaps *allowed;
  GstCaps *fcaps, *filter_caps;
  gint i, j;

  /* Allow downstream to specify width/height/framerate/PAR constraints
   * and forward them upstream for video converters to handle
   */
  templ_caps =
      caps ? gst_caps_ref (caps) :
      gst_pad_get_pad_template_caps (encoder->sinkpad);
  allowed = gst_pad_get_allowed_caps (encoder->srcpad);

  if (!allowed || gst_caps_is_empty (allowed) || gst_caps_is_any (allowed)) {
    fcaps = templ_caps;
    goto done;
  }

  GST_LOG_OBJECT (encoder, "template caps %" GST_PTR_FORMAT, templ_caps);
  GST_LOG_OBJECT (encoder, "allowed caps %" GST_PTR_FORMAT, allowed);

  filter_caps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (templ_caps); i++) {
    GQuark q_name =
        gst_structure_get_name_id (gst_caps_get_structure (templ_caps, i));

    for (j = 0; j < gst_caps_get_size (allowed); j++) {
      const GstStructure *allowed_s = gst_caps_get_structure (allowed, j);
      const GValue *val;
      GstStructure *s;

      s = gst_structure_new_id_empty (q_name);
      if ((val = gst_structure_get_value (allowed_s, "width")))
        gst_structure_set_value (s, "width", val);
      if ((val = gst_structure_get_value (allowed_s, "height")))
        gst_structure_set_value (s, "height", val);
      if ((val = gst_structure_get_value (allowed_s, "framerate")))
        gst_structure_set_value (s, "framerate", val);
      if ((val = gst_structure_get_value (allowed_s, "pixel-aspect-ratio")))
        gst_structure_set_value (s, "pixel-aspect-ratio", val);

      filter_caps = gst_caps_merge_structure (filter_caps, s);
    }
  }

  fcaps = gst_caps_intersect (filter_caps, templ_caps);
  gst_caps_unref (filter_caps);
  gst_caps_unref (templ_caps);

  if (filter) {
    GST_LOG_OBJECT (encoder, "intersecting with %" GST_PTR_FORMAT, filter);
    filter_caps = gst_caps_intersect (fcaps, filter);
    gst_caps_unref (fcaps);
    fcaps = filter_caps;
  }

done:
  gst_caps_replace (&allowed, NULL);

  GST_LOG_OBJECT (encoder, "proxy caps %" GST_PTR_FORMAT, fcaps);

  return fcaps;
}

static GstCaps *
gst_video_encoder_sink_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GstVideoEncoderClass *klass;
  GstCaps *caps;

  klass = GST_VIDEO_ENCODER_GET_CLASS (encoder);

  if (klass->getcaps)
    caps = klass->getcaps (encoder, filter);
  else
    caps = gst_video_encoder_proxy_getcaps (encoder, NULL, filter);

  GST_LOG_OBJECT (encoder, "Returning caps %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_video_encoder_propose_allocation_default (GstVideoEncoder * encoder,
    GstQuery * query)
{
  return TRUE;
}

static gboolean
gst_video_encoder_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstVideoEncoder *encoder;
  gboolean res = FALSE;

  encoder = GST_VIDEO_ENCODER (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_video_encoder_sink_getcaps (encoder, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    case GST_QUERY_ALLOCATION:
    {
      GstVideoEncoderClass *klass = GST_VIDEO_ENCODER_GET_CLASS (encoder);

      if (klass->propose_allocation)
        res = klass->propose_allocation (encoder, query);
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}

static void
gst_video_encoder_finalize (GObject * object)
{
  GstVideoEncoder *encoder;

  GST_DEBUG_OBJECT (object, "finalize");

  encoder = GST_VIDEO_ENCODER (object);
  if (encoder->priv->headers) {
    g_list_foreach (encoder->priv->headers, (GFunc) gst_buffer_unref, NULL);
    g_list_free (encoder->priv->headers);
  }
  g_rec_mutex_clear (&encoder->stream_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_video_encoder_push_event (GstVideoEncoder * encoder, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

      gst_event_copy_segment (event, &segment);

      GST_DEBUG_OBJECT (encoder, "segment %" GST_SEGMENT_FORMAT, &segment);

      if (segment.format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (encoder, "received non TIME segment");
        GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
        break;
      }

      encoder->output_segment = segment;
      GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
      break;
    }
    default:
      break;
  }

  return gst_pad_push_event (encoder->srcpad, event);
}

static gboolean
gst_video_encoder_sink_event_default (GstVideoEncoder * encoder,
    GstEvent * event)
{
  GstVideoEncoderClass *encoder_class;
  gboolean ret = FALSE;

  encoder_class = GST_VIDEO_ENCODER_GET_CLASS (encoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_video_encoder_setcaps (encoder, caps);
      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_EOS:
    {
      GstFlowReturn flow_ret;

      GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
      encoder->priv->at_eos = TRUE;

      if (encoder_class->finish) {
        flow_ret = encoder_class->finish (encoder);
      } else {
        flow_ret = GST_FLOW_OK;
      }

      ret = (flow_ret == GST_FLOW_OK);
      GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

      gst_event_copy_segment (event, &segment);

      GST_DEBUG_OBJECT (encoder, "segment %" GST_SEGMENT_FORMAT, &segment);

      if (segment.format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (encoder, "received non TIME newsegment");
        GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
        break;
      }

      encoder->priv->at_eos = FALSE;

      encoder->input_segment = segment;
      ret = TRUE;
      GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      if (gst_video_event_is_force_key_unit (event)) {
        GstClockTime running_time;
        gboolean all_headers;
        guint count;

        if (gst_video_event_parse_downstream_force_key_unit (event,
                NULL, NULL, &running_time, &all_headers, &count)) {
          ForcedKeyUnitEvent *fevt;

          GST_OBJECT_LOCK (encoder);
          fevt = forced_key_unit_event_new (running_time, all_headers, count);
          encoder->priv->force_key_unit =
              g_list_append (encoder->priv->force_key_unit, fevt);
          GST_OBJECT_UNLOCK (encoder);

          GST_DEBUG_OBJECT (encoder,
              "force-key-unit event: running-time %" GST_TIME_FORMAT
              ", all_headers %d, count %u",
              GST_TIME_ARGS (running_time), all_headers, count);
        }
        gst_event_unref (event);
        event = NULL;
        ret = TRUE;
      }
      break;
    }
    default:
      break;
  }

  /* Forward non-serialized events and EOS/FLUSH_STOP immediately.
   * For EOS this is required because no buffer or serialized event
   * will come after EOS and nothing could trigger another
   * _finish_frame() call.   *
   * If the subclass handles sending of EOS manually it can simply
   * not chain up to the parent class' event handler
   *
   * For FLUSH_STOP this is required because it is expected
   * to be forwarded immediately and no buffers are queued anyway.
   */
  if (event) {
    if (!GST_EVENT_IS_SERIALIZED (event)
        || GST_EVENT_TYPE (event) == GST_EVENT_EOS
        || GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
      ret = gst_video_encoder_push_event (encoder, event);
    } else {
      GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
      encoder->priv->current_frame_events =
          g_list_prepend (encoder->priv->current_frame_events, event);
      GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
      ret = TRUE;
    }
  }

  return ret;
}

static gboolean
gst_video_encoder_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstVideoEncoder *enc;
  GstVideoEncoderClass *klass;
  gboolean ret = TRUE;

  enc = GST_VIDEO_ENCODER (parent);
  klass = GST_VIDEO_ENCODER_GET_CLASS (enc);

  GST_DEBUG_OBJECT (enc, "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  if (klass->sink_event)
    ret = klass->sink_event (enc, event);

  return ret;
}

static gboolean
gst_video_encoder_src_event_default (GstVideoEncoder * encoder,
    GstEvent * event)
{
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      if (gst_video_event_is_force_key_unit (event)) {
        GstClockTime running_time;
        gboolean all_headers;
        guint count;

        if (gst_video_event_parse_upstream_force_key_unit (event,
                &running_time, &all_headers, &count)) {
          ForcedKeyUnitEvent *fevt;

          GST_OBJECT_LOCK (encoder);
          fevt = forced_key_unit_event_new (running_time, all_headers, count);
          encoder->priv->force_key_unit =
              g_list_append (encoder->priv->force_key_unit, fevt);
          GST_OBJECT_UNLOCK (encoder);

          GST_DEBUG_OBJECT (encoder,
              "force-key-unit event: running-time %" GST_TIME_FORMAT
              ", all_headers %d, count %u",
              GST_TIME_ARGS (running_time), all_headers, count);
        }
        gst_event_unref (event);
        event = NULL;
        ret = TRUE;
      }
      break;
    }
    default:
      break;
  }

  if (event)
    ret =
        gst_pad_event_default (encoder->srcpad, GST_OBJECT_CAST (encoder),
        event);

  return ret;
}

static gboolean
gst_video_encoder_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstVideoEncoder *encoder;
  GstVideoEncoderClass *klass;
  gboolean ret = FALSE;

  encoder = GST_VIDEO_ENCODER (parent);
  klass = GST_VIDEO_ENCODER_GET_CLASS (encoder);

  GST_LOG_OBJECT (encoder, "handling event: %" GST_PTR_FORMAT, event);

  if (klass->src_event)
    ret = klass->src_event (encoder, event);

  return ret;
}

static gboolean
gst_video_encoder_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstVideoEncoderPrivate *priv;
  GstVideoEncoder *enc;
  gboolean res;

  enc = GST_VIDEO_ENCODER (parent);
  priv = enc->priv;

  GST_LOG_OBJECT (enc, "handling query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_video_encoded_video_convert (priv->bytes, priv->time, src_fmt,
          src_val, &dest_fmt, &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min_latency, max_latency;

      res = gst_pad_peer_query (enc->sinkpad, query);
      if (res) {
        gst_query_parse_latency (query, &live, &min_latency, &max_latency);
        GST_DEBUG_OBJECT (enc, "Peer latency: live %d, min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT, live,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        GST_OBJECT_LOCK (enc);
        min_latency += priv->min_latency;
        if (max_latency != GST_CLOCK_TIME_NONE) {
          max_latency += priv->max_latency;
        }
        GST_OBJECT_UNLOCK (enc);

        gst_query_set_latency (query, live, min_latency, max_latency);
      }
    }
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
  }
  return res;

error:
  GST_DEBUG_OBJECT (enc, "query failed");
  return res;
}

static GstVideoCodecFrame *
gst_video_encoder_new_frame (GstVideoEncoder * encoder, GstBuffer * buf,
    GstClockTime timestamp, GstClockTime duration)
{
  GstVideoEncoderPrivate *priv = encoder->priv;
  GstVideoCodecFrame *frame;

  frame = g_slice_new0 (GstVideoCodecFrame);

  frame->ref_count = 1;

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
  frame->system_frame_number = priv->system_frame_number;
  priv->system_frame_number++;

  frame->presentation_frame_number = priv->presentation_frame_number;
  priv->presentation_frame_number++;
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  frame->events = priv->current_frame_events;
  priv->current_frame_events = NULL;
  frame->input_buffer = buf;
  frame->pts = timestamp;
  frame->duration = duration;

  if (GST_VIDEO_INFO_IS_INTERLACED (&encoder->priv->input_state->info)) {
    if (GST_BUFFER_FLAG_IS_SET (buf, GST_VIDEO_BUFFER_FLAG_TFF)) {
      GST_VIDEO_CODEC_FRAME_FLAG_SET (frame, GST_VIDEO_CODEC_FRAME_FLAG_TFF);
    } else {
      GST_VIDEO_CODEC_FRAME_FLAG_UNSET (frame, GST_VIDEO_CODEC_FRAME_FLAG_TFF);
    }
    if (GST_BUFFER_FLAG_IS_SET (buf, GST_VIDEO_BUFFER_FLAG_RFF)) {
      GST_VIDEO_CODEC_FRAME_FLAG_SET (frame, GST_VIDEO_CODEC_FRAME_FLAG_RFF);
    } else {
      GST_VIDEO_CODEC_FRAME_FLAG_UNSET (frame, GST_VIDEO_CODEC_FRAME_FLAG_RFF);
    }
    if (GST_BUFFER_FLAG_IS_SET (buf, GST_VIDEO_BUFFER_FLAG_ONEFIELD)) {
      GST_VIDEO_CODEC_FRAME_FLAG_SET (frame,
          GST_VIDEO_CODEC_FRAME_FLAG_ONEFIELD);
    } else {
      GST_VIDEO_CODEC_FRAME_FLAG_UNSET (frame,
          GST_VIDEO_CODEC_FRAME_FLAG_ONEFIELD);
    }
  }

  return frame;
}


static GstFlowReturn
gst_video_encoder_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstVideoEncoder *encoder;
  GstVideoEncoderPrivate *priv;
  GstVideoEncoderClass *klass;
  GstVideoCodecFrame *frame;
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 start, stop = GST_CLOCK_TIME_NONE, cstart, cstop;

  encoder = GST_VIDEO_ENCODER (parent);
  priv = encoder->priv;
  klass = GST_VIDEO_ENCODER_GET_CLASS (encoder);

  g_return_val_if_fail (klass->handle_frame != NULL, GST_FLOW_ERROR);

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  start = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buf)))
    stop = start + GST_BUFFER_DURATION (buf);

  GST_LOG_OBJECT (encoder,
      "received buffer of size %d with ts %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT, gst_buffer_get_size (buf),
      GST_TIME_ARGS (start), GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  if (priv->at_eos) {
    ret = GST_FLOW_EOS;
    goto done;
  }

  /* Drop buffers outside of segment */
  if (!gst_segment_clip (&encoder->output_segment,
          GST_FORMAT_TIME, start, stop, &cstart, &cstop)) {
    GST_DEBUG_OBJECT (encoder, "clipping to segment dropped frame");
    gst_buffer_unref (buf);
    goto done;
  }

  frame = gst_video_encoder_new_frame (encoder, buf, cstart, cstop - cstart);

  GST_OBJECT_LOCK (encoder);
  if (priv->force_key_unit) {
    ForcedKeyUnitEvent *fevt = NULL;
    GstClockTime running_time;
    GList *l;

    running_time =
        gst_segment_to_running_time (&encoder->output_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buf));

    for (l = priv->force_key_unit; l; l = l->next) {
      ForcedKeyUnitEvent *tmp = l->data;

      /* Skip pending keyunits */
      if (tmp->pending)
        continue;

      /* Simple case, keyunit ASAP */
      if (tmp->running_time == GST_CLOCK_TIME_NONE) {
        fevt = tmp;
        break;
      }

      /* Event for before this frame */
      if (tmp->running_time <= running_time) {
        fevt = tmp;
        break;
      }
    }

    if (fevt) {
      GST_DEBUG_OBJECT (encoder,
          "Forcing a key unit at running time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (running_time));
      GST_VIDEO_CODEC_FRAME_SET_FORCE_KEYFRAME (frame);
      if (fevt->all_headers)
        GST_VIDEO_CODEC_FRAME_SET_FORCE_KEYFRAME_HEADERS (frame);
      fevt->pending = TRUE;
    }
  }
  GST_OBJECT_UNLOCK (encoder);

  priv->frames = g_list_append (priv->frames, frame);

  /* new data, more finish needed */
  priv->drained = FALSE;

  GST_LOG_OBJECT (encoder, "passing frame pfn %d to subclass",
      frame->presentation_frame_number);

  ret = klass->handle_frame (encoder, frame);

done:
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  return ret;
}

static GstStateChangeReturn
gst_video_encoder_change_state (GstElement * element, GstStateChange transition)
{
  GstVideoEncoder *encoder;
  GstVideoEncoderClass *encoder_class;
  GstStateChangeReturn ret;

  encoder = GST_VIDEO_ENCODER (element);
  encoder_class = GST_VIDEO_ENCODER_GET_CLASS (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* open device/library if needed */
      if (encoder_class->open && !encoder_class->open (encoder))
        goto open_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Initialize device/library if needed */
      if (encoder_class->start && !encoder_class->start (encoder))
        goto start_failed;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_video_encoder_reset (encoder);
      if (encoder_class->stop && !encoder_class->stop (encoder))
        goto stop_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* close device/library if needed */
      if (encoder_class->close && !encoder_class->close (encoder))
        goto close_failed;
      break;
    default:
      break;
  }

  return ret;

  /* Errors */

open_failed:
  {
    GST_ELEMENT_ERROR (encoder, LIBRARY, INIT, (NULL),
        ("Failed to open encoder"));
    return GST_STATE_CHANGE_FAILURE;
  }

start_failed:
  {
    GST_ELEMENT_ERROR (encoder, LIBRARY, INIT, (NULL),
        ("Failed to start encoder"));
    return GST_STATE_CHANGE_FAILURE;
  }

stop_failed:
  {
    GST_ELEMENT_ERROR (encoder, LIBRARY, INIT, (NULL),
        ("Failed to stop encoder"));
    return GST_STATE_CHANGE_FAILURE;
  }

close_failed:
  {
    GST_ELEMENT_ERROR (encoder, LIBRARY, INIT, (NULL),
        ("Failed to close encoder"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

static gboolean
gst_video_encoder_set_src_caps (GstVideoEncoder * encoder)
{
  gboolean ret;
  GstVideoCodecState *state = encoder->priv->output_state;
  GstVideoInfo *info = &state->info;

  g_return_val_if_fail (state->caps != NULL, FALSE);

  if (encoder->priv->output_state_changed) {
    state->caps = gst_caps_make_writable (state->caps);

    /* Fill caps */
    gst_caps_set_simple (state->caps, "width", G_TYPE_INT, info->width,
        "height", G_TYPE_INT, info->height,
        "pixel-aspect-ratio", GST_TYPE_FRACTION,
        info->par_n, info->par_d, NULL);
    if (info->flags & GST_VIDEO_FLAG_VARIABLE_FPS && info->fps_n != 0) {
      /* variable fps with a max-framerate */
      gst_caps_set_simple (state->caps, "framerate", GST_TYPE_FRACTION, 0, 1,
          "max-framerate", GST_TYPE_FRACTION, info->fps_n, info->fps_d, NULL);
    } else {
      /* no variable fps or no max-framerate */
      gst_caps_set_simple (state->caps, "framerate", GST_TYPE_FRACTION,
          info->fps_n, info->fps_d, NULL);
    }
    if (state->codec_data)
      gst_caps_set_simple (state->caps, "codec_data", GST_TYPE_BUFFER,
          state->codec_data, NULL);
    encoder->priv->output_state_changed = FALSE;
  }

  ret = gst_pad_set_caps (encoder->srcpad, state->caps);

  return ret;
}

/**
 * gst_video_encoder_finish_frame:
 * @encoder: a #GstVideoEncoder
 * @frame: (transfer full): an encoded #GstVideoCodecFrame 
 *
 * @frame must have a valid encoded data buffer, whose metadata fields
 * are then appropriately set according to frame data or no buffer at
 * all if the frame should be dropped.
 * It is subsequently pushed downstream or provided to @pre_push.
 * In any case, the frame is considered finished and released.
 *
 * Returns: a #GstFlowReturn resulting from sending data downstream
 *
 * Since: 0.10.36
 */
GstFlowReturn
gst_video_encoder_finish_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstVideoEncoderPrivate *priv = encoder->priv;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoEncoderClass *encoder_class;
  GList *l;
  gboolean send_headers = FALSE;
  gboolean discont = (frame->presentation_frame_number == 0);

  encoder_class = GST_VIDEO_ENCODER_GET_CLASS (encoder);

  GST_LOG_OBJECT (encoder,
      "finish frame fpn %d", frame->presentation_frame_number);

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  if (G_UNLIKELY (priv->output_state_changed))
    gst_video_encoder_set_src_caps (encoder);

  if (G_UNLIKELY (priv->output_state == NULL))
    goto no_output_state;

  /* Push all pending events that arrived before this frame */
  for (l = priv->frames; l; l = l->next) {
    GstVideoCodecFrame *tmp = l->data;

    if (tmp->events) {
      GList *k;

      for (k = g_list_last (tmp->events); k; k = k->prev)
        gst_video_encoder_push_event (encoder, k->data);
      g_list_free (tmp->events);
      tmp->events = NULL;
    }

    if (tmp == frame)
      break;
  }

  /* no buffer data means this frame is skipped/dropped */
  if (!frame->output_buffer) {
    GST_DEBUG_OBJECT (encoder, "skipping frame %" GST_TIME_FORMAT,
        GST_TIME_ARGS (frame->pts));
    goto done;
  }

  if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame) && priv->force_key_unit) {
    GstClockTime stream_time, running_time;
    GstEvent *ev;
    ForcedKeyUnitEvent *fevt = NULL;
    GList *l;

    running_time =
        gst_segment_to_running_time (&encoder->output_segment, GST_FORMAT_TIME,
        frame->pts);

    GST_OBJECT_LOCK (encoder);
    for (l = priv->force_key_unit; l; l = l->next) {
      ForcedKeyUnitEvent *tmp = l->data;

      /* Skip non-pending keyunits */
      if (!tmp->pending)
        continue;

      /* Simple case, keyunit ASAP */
      if (tmp->running_time == GST_CLOCK_TIME_NONE) {
        fevt = tmp;
        break;
      }

      /* Event for before this frame */
      if (tmp->running_time <= running_time) {
        fevt = tmp;
        break;
      }
    }

    if (fevt) {
      priv->force_key_unit = g_list_remove (priv->force_key_unit, fevt);
    }
    GST_OBJECT_UNLOCK (encoder);

    if (fevt) {
      stream_time =
          gst_segment_to_stream_time (&encoder->output_segment, GST_FORMAT_TIME,
          frame->pts);

      ev = gst_video_event_new_downstream_force_key_unit
          (frame->pts, stream_time, running_time,
          fevt->all_headers, fevt->count);

      gst_video_encoder_push_event (encoder, ev);

      if (fevt->all_headers)
        send_headers = TRUE;

      GST_DEBUG_OBJECT (encoder,
          "Forced key unit: running-time %" GST_TIME_FORMAT
          ", all_headers %d, count %u",
          GST_TIME_ARGS (running_time), fevt->all_headers, fevt->count);
      forced_key_unit_event_free (fevt);
    }
  }

  if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame)) {
    priv->distance_from_sync = 0;
    GST_BUFFER_FLAG_UNSET (frame->output_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    /* For keyframes, DTS = PTS */
    frame->dts = frame->pts;
  } else {
    GST_BUFFER_FLAG_SET (frame->output_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  frame->distance_from_sync = priv->distance_from_sync;
  priv->distance_from_sync++;

  GST_BUFFER_TIMESTAMP (frame->output_buffer) = frame->pts;
  GST_BUFFER_DURATION (frame->output_buffer) = frame->duration;

  /* update rate estimate */
  priv->bytes += gst_buffer_get_size (frame->output_buffer);
  if (GST_CLOCK_TIME_IS_VALID (frame->duration)) {
    priv->time += frame->duration;
  } else {
    /* better none than nothing valid */
    priv->time = GST_CLOCK_TIME_NONE;
  }

  if (G_UNLIKELY (send_headers || priv->new_headers)) {
    GList *tmp, *copy = NULL;

    GST_DEBUG_OBJECT (encoder, "Sending headers");

    /* First make all buffers metadata-writable */
    for (tmp = priv->headers; tmp; tmp = tmp->next) {
      GstBuffer *tmpbuf = GST_BUFFER (tmp->data);

      copy = g_list_append (copy, gst_buffer_make_writable (tmpbuf));
    }
    g_list_free (priv->headers);
    priv->headers = copy;

    for (tmp = priv->headers; tmp; tmp = tmp->next) {
      GstBuffer *tmpbuf = GST_BUFFER (tmp->data);

      gst_buffer_ref (tmpbuf);
      priv->bytes += gst_buffer_get_size (tmpbuf);
      if (G_UNLIKELY (discont)) {
        GST_LOG_OBJECT (encoder, "marking discont");
        GST_BUFFER_FLAG_SET (tmpbuf, GST_BUFFER_FLAG_DISCONT);
        discont = FALSE;
      }

      gst_pad_push (encoder->srcpad, tmpbuf);
    }
    priv->new_headers = FALSE;
  }

  if (G_UNLIKELY (discont)) {
    GST_LOG_OBJECT (encoder, "marking discont");
    GST_BUFFER_FLAG_SET (frame->output_buffer, GST_BUFFER_FLAG_DISCONT);
  }

  if (encoder_class->pre_push)
    ret = encoder_class->pre_push (encoder, frame);

  if (ret == GST_FLOW_OK)
    ret = gst_pad_push (encoder->srcpad, frame->output_buffer);

  frame->output_buffer = NULL;

done:
  /* handed out */
  priv->frames = g_list_remove (priv->frames, frame);

  gst_video_codec_frame_unref (frame);

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  return ret;

  /* ERRORS */
no_output_state:
  {
    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
    GST_ERROR_OBJECT (encoder, "Output state was not configured");
    return GST_FLOW_ERROR;
  }
}

/**
 * gst_video_encoder_get_output_state:
 * @encoder: a #GstVideoEncoder
 *
 * Get the current #GstVideoCodecState
 *
 * Returns: (transfer full): #GstVideoCodecState describing format of video data.
 *
 * Since: 0.10.36
 */
GstVideoCodecState *
gst_video_encoder_get_output_state (GstVideoEncoder * encoder)
{
  GstVideoCodecState *state;

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
  state = gst_video_codec_state_ref (encoder->priv->output_state);
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  return state;
}

/**
 * gst_video_encoder_set_output_state:
 * @encoder: a #GstVideoEncoder
 * @caps: (transfer full): the #GstCaps to use for the output
 * @reference: (allow-none) (transfer none): An optional reference @GstVideoCodecState
 *
 * Creates a new #GstVideoCodecState with the specified caps as the output state
 * for the encoder.
 * Any previously set output state on @decoder will be replaced by the newly
 * created one.
 *
 * The specified @caps should not contain any resolution, pixel-aspect-ratio,
 * framerate, codec-data, .... Those should be specified instead in the returned
 * #GstVideoCodecState.
 *
 * If the subclass wishes to copy over existing fields (like pixel aspect ratio,
 * or framerate) from an existing #GstVideoCodecState, it can be provided as a
 * @reference.
 *
 * If the subclass wishes to override some fields from the output state (like
 * pixel-aspect-ratio or framerate) it can do so on the returned #GstVideoCodecState.
 *
 * The new output state will only take effect (set on pads and buffers) starting
 * from the next call to #gst_video_encoder_finish_frame().
 *
 * Returns: (transfer full): the newly configured output state.
 *
 * Since: 0.10.36
 */
GstVideoCodecState *
gst_video_encoder_set_output_state (GstVideoEncoder * encoder, GstCaps * caps,
    GstVideoCodecState * reference)
{
  GstVideoEncoderPrivate *priv = encoder->priv;
  GstVideoCodecState *state;

  g_return_val_if_fail (caps != NULL, NULL);

  state = _new_output_state (caps, reference);

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
  if (priv->output_state)
    gst_video_codec_state_unref (priv->output_state);
  priv->output_state = gst_video_codec_state_ref (state);

  priv->output_state_changed = TRUE;
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  return state;
}

/**
 * gst_video_encoder_set_latency:
 * @encoder: a #GstVideoEncoder
 * @min_latency: minimum latency
 * @max_latency: maximum latency
 *
 * Informs baseclass of encoding latency.
 *
 * Since: 0.10.36
 */
void
gst_video_encoder_set_latency (GstVideoEncoder * encoder,
    GstClockTime min_latency, GstClockTime max_latency)
{
  g_return_if_fail (GST_CLOCK_TIME_IS_VALID (min_latency));
  g_return_if_fail (max_latency >= min_latency);

  GST_OBJECT_LOCK (encoder);
  encoder->priv->min_latency = min_latency;
  encoder->priv->max_latency = max_latency;
  GST_OBJECT_UNLOCK (encoder);

  gst_element_post_message (GST_ELEMENT_CAST (encoder),
      gst_message_new_latency (GST_OBJECT_CAST (encoder)));
}

/**
 * gst_video_encoder_get_latency:
 * @encoder: a #GstVideoEncoder
 * @min_latency: (out) (allow-none): the configured minimum latency
 * @max_latency: (out) (allow-none): the configured maximum latency
 *
 * Returns the configured encoding latency.
 *
 * Since: 0.10.36
 */
void
gst_video_encoder_get_latency (GstVideoEncoder * encoder,
    GstClockTime * min_latency, GstClockTime * max_latency)
{
  GST_OBJECT_LOCK (encoder);
  if (min_latency)
    *min_latency = encoder->priv->min_latency;
  if (max_latency)
    *max_latency = encoder->priv->max_latency;
  GST_OBJECT_UNLOCK (encoder);
}

/**
 * gst_video_encoder_get_oldest_frame:
 * @encoder: a #GstVideoEncoder
 *
 * Get the oldest unfinished pending #GstVideoCodecFrame
 *
 * Returns: oldest unfinished pending #GstVideoCodecFrame
 *
 * Since: 0.10.36
 */
GstVideoCodecFrame *
gst_video_encoder_get_oldest_frame (GstVideoEncoder * encoder)
{
  GList *g;

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
  g = encoder->priv->frames;
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  if (g == NULL)
    return NULL;
  return (GstVideoCodecFrame *) (g->data);
}

/**
 * gst_video_encoder_get_frame:
 * @encoder: a #GstVideoEnccoder
 * @frame_number: system_frame_number of a frame
 *
 * Get a pending unfinished #GstVideoCodecFrame
 * 
 * Returns: (transfer none): pending unfinished #GstVideoCodecFrame identified by @frame_number.
 *
 * Since: 0.10.36
 */
GstVideoCodecFrame *
gst_video_encoder_get_frame (GstVideoEncoder * encoder, int frame_number)
{
  GList *g;
  GstVideoCodecFrame *frame = NULL;

  GST_DEBUG_OBJECT (encoder, "frame_number : %d", frame_number);

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
  for (g = encoder->priv->frames; g; g = g->next) {
    GstVideoCodecFrame *tmp = g->data;

    if (tmp->system_frame_number == frame_number) {
      frame = tmp;
      break;
    }
  }
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  return frame;
}
