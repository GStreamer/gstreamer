/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2001 Thomas <thomas@apestaart.org>
 *               2005,2006 Wim Taymans <wim@fluendo.com>
 *                    2013 Sebastian Dröge <sebastian@centricular.com>
 *
 * audiomixer.c: AudioMixer element, N in, one out, samples are added
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-audiomixer
 * @title: audiomixer
 *
 * The audiomixer allows to mix several streams into one by adding the data.
 * Mixed data is clamped to the min/max values of the data format.
 *
 * Unlike the adder element audiomixer properly synchronises all input streams.
 *
 * The input pads are from a GstPad subclass and have additional
 * properties to mute each pad individually and set the volume:
 *
 * * "mute": Whether to mute the pad or not (#gboolean)
 * * "volume": The volume of the pad, between 0.0 and 10.0 (#gdouble)
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 audiotestsrc freq=100 ! audiomixer name=mix ! audioconvert ! alsasink audiotestsrc freq=500 ! mix.
 * ]| This pipeline produces two sine waves mixed together.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstaudiomixer.h"
#include <gst/audio/audio.h>
#include <string.h>             /* strcmp */
#include "gstaudiomixerorc.h"

#include "gstaudiointerleave.h"

#define GST_CAT_DEFAULT gst_audiomixer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEFAULT_PAD_VOLUME (1.0)
#define DEFAULT_PAD_MUTE (FALSE)

/* some defines for audio processing */
/* the volume factor is a range from 0.0 to (arbitrary) VOLUME_MAX_DOUBLE = 10.0
 * we map 1.0 to VOLUME_UNITY_INT*
 */
#define VOLUME_UNITY_INT8            8  /* internal int for unity 2^(8-5) */
#define VOLUME_UNITY_INT8_BIT_SHIFT  3  /* number of bits to shift for unity */
#define VOLUME_UNITY_INT16           2048       /* internal int for unity 2^(16-5) */
#define VOLUME_UNITY_INT16_BIT_SHIFT 11 /* number of bits to shift for unity */
#define VOLUME_UNITY_INT24           524288     /* internal int for unity 2^(24-5) */
#define VOLUME_UNITY_INT24_BIT_SHIFT 19 /* number of bits to shift for unity */
#define VOLUME_UNITY_INT32           134217728  /* internal int for unity 2^(32-5) */
#define VOLUME_UNITY_INT32_BIT_SHIFT 27

enum
{
  PROP_PAD_0,
  PROP_PAD_VOLUME,
  PROP_PAD_MUTE
};

G_DEFINE_TYPE (GstAudioMixerPad, gst_audiomixer_pad,
    GST_TYPE_AUDIO_AGGREGATOR_PAD);

static void
gst_audiomixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioMixerPad *pad = GST_AUDIO_MIXER_PAD (object);

  switch (prop_id) {
    case PROP_PAD_VOLUME:
      g_value_set_double (value, pad->volume);
      break;
    case PROP_PAD_MUTE:
      g_value_set_boolean (value, pad->mute);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audiomixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioMixerPad *pad = GST_AUDIO_MIXER_PAD (object);

  switch (prop_id) {
    case PROP_PAD_VOLUME:
      GST_OBJECT_LOCK (pad);
      pad->volume = g_value_get_double (value);
      pad->volume_i8 = pad->volume * VOLUME_UNITY_INT8;
      pad->volume_i16 = pad->volume * VOLUME_UNITY_INT16;
      pad->volume_i32 = pad->volume * VOLUME_UNITY_INT32;
      GST_OBJECT_UNLOCK (pad);
      break;
    case PROP_PAD_MUTE:
      GST_OBJECT_LOCK (pad);
      pad->mute = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (pad);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audiomixer_pad_class_init (GstAudioMixerPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_audiomixer_pad_set_property;
  gobject_class->get_property = gst_audiomixer_pad_get_property;

  g_object_class_install_property (gobject_class, PROP_PAD_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume of this pad",
          0.0, 10.0, DEFAULT_PAD_VOLUME,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PAD_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute this pad",
          DEFAULT_PAD_MUTE,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_audiomixer_pad_init (GstAudioMixerPad * pad)
{
  pad->volume = DEFAULT_PAD_VOLUME;
  pad->mute = DEFAULT_PAD_MUTE;
}

enum
{
  PROP_0,
  PROP_FILTER_CAPS
};

/* elementfactory information */

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define CAPS \
  GST_AUDIO_CAPS_MAKE ("{ S32LE, U32LE, S16LE, U16LE, S8, U8, F32LE, F64LE }") \
  ", layout = (string) { interleaved, non-interleaved }"
#else
#define CAPS \
  GST_AUDIO_CAPS_MAKE ("{ S32BE, U32BE, S16BE, U16BE, S8, U8, F32BE, F64BE }") \
  ", layout = (string) { interleaved, non-interleaved }"
#endif

static GstStaticPadTemplate gst_audiomixer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS)
    );

static GstStaticPadTemplate gst_audiomixer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (CAPS)
    );

static void gst_audiomixer_child_proxy_init (gpointer g_iface,
    gpointer iface_data);

#define gst_audiomixer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstAudioMixer, gst_audiomixer,
    GST_TYPE_AUDIO_AGGREGATOR, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_audiomixer_child_proxy_init));

static void gst_audiomixer_dispose (GObject * object);
static void gst_audiomixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audiomixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_audiomixer_setcaps (GstAudioMixer * audiomixer,
    GstPad * pad, GstCaps * caps);
static GstPad *gst_audiomixer_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * req_name, const GstCaps * caps);
static void gst_audiomixer_release_pad (GstElement * element, GstPad * pad);

static gboolean
gst_audiomixer_aggregate_one_buffer (GstAudioAggregator * aagg,
    GstAudioAggregatorPad * aaggpad, GstBuffer * inbuf, guint in_offset,
    GstBuffer * outbuf, guint out_offset, guint num_samples);


/* we can only accept caps that we and downstream can handle.
 * if we have filtercaps set, use those to constrain the target caps.
 */
static GstCaps *
gst_audiomixer_sink_getcaps (GstAggregator * agg, GstPad * pad,
    GstCaps * filter)
{
  GstAudioAggregator *aagg;
  GstAudioMixer *audiomixer;
  GstCaps *result, *peercaps, *current_caps, *filter_caps;
  GstStructure *s;
  gint i, n;

  audiomixer = GST_AUDIO_MIXER (agg);
  aagg = GST_AUDIO_AGGREGATOR (agg);

  GST_OBJECT_LOCK (audiomixer);
  /* take filter */
  if ((filter_caps = audiomixer->filter_caps)) {
    if (filter)
      filter_caps =
          gst_caps_intersect_full (filter, filter_caps,
          GST_CAPS_INTERSECT_FIRST);
    else
      gst_caps_ref (filter_caps);
  } else {
    filter_caps = filter ? gst_caps_ref (filter) : NULL;
  }
  GST_OBJECT_UNLOCK (audiomixer);

  if (filter_caps && gst_caps_is_empty (filter_caps)) {
    GST_WARNING_OBJECT (pad, "Empty filter caps");
    return filter_caps;
  }

  /* get the downstream possible caps */
  peercaps = gst_pad_peer_query_caps (agg->srcpad, filter_caps);

  /* get the allowed caps on this sinkpad */
  GST_OBJECT_LOCK (audiomixer);
  current_caps = aagg->current_caps ? gst_caps_ref (aagg->current_caps) : NULL;
  if (current_caps == NULL) {
    current_caps = gst_pad_get_pad_template_caps (pad);
    if (!current_caps)
      current_caps = gst_caps_new_any ();
  }
  GST_OBJECT_UNLOCK (audiomixer);

  if (peercaps) {
    /* if the peer has caps, intersect */
    GST_DEBUG_OBJECT (audiomixer, "intersecting peer and our caps");
    result =
        gst_caps_intersect_full (peercaps, current_caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
    gst_caps_unref (current_caps);
  } else {
    /* the peer has no caps (or there is no peer), just use the allowed caps
     * of this sinkpad. */
    /* restrict with filter-caps if any */
    if (filter_caps) {
      GST_DEBUG_OBJECT (audiomixer, "no peer caps, using filtered caps");
      result =
          gst_caps_intersect_full (filter_caps, current_caps,
          GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (current_caps);
    } else {
      GST_DEBUG_OBJECT (audiomixer, "no peer caps, using our caps");
      result = current_caps;
    }
  }

  result = gst_caps_make_writable (result);

  n = gst_caps_get_size (result);
  for (i = 0; i < n; i++) {
    GstStructure *sref;

    s = gst_caps_get_structure (result, i);
    sref = gst_structure_copy (s);
    gst_structure_set (sref, "channels", GST_TYPE_INT_RANGE, 0, 2, NULL);
    if (gst_structure_is_subset (s, sref)) {
      /* This field is irrelevant when in mono or stereo */
      gst_structure_remove_field (s, "channel-mask");
    }
    gst_structure_free (sref);
  }

  if (filter_caps)
    gst_caps_unref (filter_caps);

  GST_LOG_OBJECT (audiomixer, "getting caps on pad %p,%s to %" GST_PTR_FORMAT,
      pad, GST_PAD_NAME (pad), result);

  return result;
}

static gboolean
gst_audiomixer_sink_query (GstAggregator * agg, GstAggregatorPad * aggpad,
    GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_audiomixer_sink_getcaps (agg, GST_PAD (aggpad), filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res =
          GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, aggpad, query);
      break;
  }

  return res;
}

/* the first caps we receive on any of the sinkpads will define the caps for all
 * the other sinkpads because we can only mix streams with the same caps.
 */
static gboolean
gst_audiomixer_setcaps (GstAudioMixer * audiomixer, GstPad * pad,
    GstCaps * orig_caps)
{
  GstAggregator *agg = GST_AGGREGATOR (audiomixer);
  GstAudioAggregator *aagg = GST_AUDIO_AGGREGATOR (audiomixer);
  GstCaps *caps;
  GstAudioInfo info;
  GstStructure *s;
  gint channels = 0;
  gboolean ret;

  caps = gst_caps_copy (orig_caps);

  s = gst_caps_get_structure (caps, 0);
  if (gst_structure_get_int (s, "channels", &channels))
    if (channels <= 2)
      gst_structure_remove_field (s, "channel-mask");

  if (!gst_audio_info_from_caps (&info, caps))
    goto invalid_format;

  if (channels == 1) {
    GstCaps *filter;
    GstCaps *downstream_caps;

    if (audiomixer->filter_caps)
      filter = gst_caps_intersect_full (caps, audiomixer->filter_caps,
          GST_CAPS_INTERSECT_FIRST);
    else
      filter = gst_caps_ref (caps);

    downstream_caps = gst_pad_peer_query_caps (agg->srcpad, filter);
    gst_caps_unref (filter);

    if (downstream_caps) {
      gst_caps_unref (caps);
      caps = downstream_caps;

      if (gst_caps_is_empty (caps)) {
        gst_caps_unref (caps);
        return FALSE;
      }
      caps = gst_caps_fixate (caps);
    }
  }

  GST_OBJECT_LOCK (audiomixer);
  /* don't allow reconfiguration for now; there's still a race between the
   * different upstream threads doing query_caps + accept_caps + sending
   * (possibly different) CAPS events, but there's not much we can do about
   * that, upstream needs to deal with it. */
  if (aagg->current_caps != NULL) {
    if (gst_audio_info_is_equal (&info, &aagg->info)) {
      GST_OBJECT_UNLOCK (audiomixer);
      gst_caps_unref (caps);
      gst_audio_aggregator_set_sink_caps (aagg, GST_AUDIO_AGGREGATOR_PAD (pad),
          orig_caps);
      return TRUE;
    } else {
      GST_DEBUG_OBJECT (pad, "got input caps %" GST_PTR_FORMAT ", but "
          "current caps are %" GST_PTR_FORMAT, caps, aagg->current_caps);
      GST_OBJECT_UNLOCK (audiomixer);
      gst_pad_push_event (pad, gst_event_new_reconfigure ());
      gst_caps_unref (caps);
      return FALSE;
    }
  }
  GST_OBJECT_UNLOCK (audiomixer);

  ret = gst_audio_aggregator_set_src_caps (aagg, caps);

  if (ret)
    gst_audio_aggregator_set_sink_caps (aagg, GST_AUDIO_AGGREGATOR_PAD (pad),
        orig_caps);

  GST_INFO_OBJECT (pad, "handle caps change to %" GST_PTR_FORMAT, caps);

  gst_caps_unref (caps);

  return ret;

  /* ERRORS */
invalid_format:
  {
    gst_caps_unref (caps);
    GST_WARNING_OBJECT (audiomixer, "invalid format set as caps");
    return FALSE;
  }
}

static gboolean
gst_audiomixer_sink_event (GstAggregator * agg, GstAggregatorPad * aggpad,
    GstEvent * event)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (agg);
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (aggpad, "Got %s event on sink pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_audiomixer_setcaps (audiomixer, GST_PAD_CAST (aggpad), caps);
      gst_event_unref (event);
      event = NULL;
      break;
    }
    default:
      break;
  }

  if (event != NULL)
    return GST_AGGREGATOR_CLASS (parent_class)->sink_event (agg, aggpad, event);

  return res;
}

static void
gst_audiomixer_class_init (GstAudioMixerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;
  GstAudioAggregatorClass *aagg_class = (GstAudioAggregatorClass *) klass;

  gobject_class->set_property = gst_audiomixer_set_property;
  gobject_class->get_property = gst_audiomixer_get_property;
  gobject_class->dispose = gst_audiomixer_dispose;

  g_object_class_install_property (gobject_class, PROP_FILTER_CAPS,
      g_param_spec_boxed ("caps", "Target caps",
          "Set target format for mixing (NULL means ANY). "
          "Setting this property takes a reference to the supplied GstCaps "
          "object", GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_audiomixer_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_audiomixer_sink_template);
  gst_element_class_set_static_metadata (gstelement_class, "AudioMixer",
      "Generic/Audio", "Mixes multiple audio streams",
      "Sebastian Dröge <sebastian@centricular.com>");

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_audiomixer_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_audiomixer_release_pad);

  agg_class->sinkpads_type = GST_TYPE_AUDIO_MIXER_PAD;

  agg_class->sink_query = GST_DEBUG_FUNCPTR (gst_audiomixer_sink_query);
  agg_class->sink_event = GST_DEBUG_FUNCPTR (gst_audiomixer_sink_event);

  aagg_class->aggregate_one_buffer = gst_audiomixer_aggregate_one_buffer;
}

static void
gst_audiomixer_init (GstAudioMixer * audiomixer)
{
  audiomixer->filter_caps = NULL;
}

static void
gst_audiomixer_dispose (GObject * object)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (object);

  gst_caps_replace (&audiomixer->filter_caps, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_audiomixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (object);

  switch (prop_id) {
    case PROP_FILTER_CAPS:{
      GstCaps *new_caps = NULL;
      GstCaps *old_caps;
      const GstCaps *new_caps_val = gst_value_get_caps (value);

      if (new_caps_val != NULL) {
        new_caps = (GstCaps *) new_caps_val;
        gst_caps_ref (new_caps);
      }

      GST_OBJECT_LOCK (audiomixer);
      old_caps = audiomixer->filter_caps;
      audiomixer->filter_caps = new_caps;
      GST_OBJECT_UNLOCK (audiomixer);

      if (old_caps)
        gst_caps_unref (old_caps);

      GST_DEBUG_OBJECT (audiomixer, "set new caps %" GST_PTR_FORMAT, new_caps);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audiomixer_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (object);

  switch (prop_id) {
    case PROP_FILTER_CAPS:
      GST_OBJECT_LOCK (audiomixer);
      gst_value_set_caps (value, audiomixer->filter_caps);
      GST_OBJECT_UNLOCK (audiomixer);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
gst_audiomixer_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * req_name, const GstCaps * caps)
{
  GstAudioMixerPad *newpad;

  newpad = (GstAudioMixerPad *)
      GST_ELEMENT_CLASS (parent_class)->request_new_pad (element,
      templ, req_name, caps);

  if (newpad == NULL)
    goto could_not_create;

  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (newpad),
      GST_OBJECT_NAME (newpad));

  return GST_PAD_CAST (newpad);

could_not_create:
  {
    GST_DEBUG_OBJECT (element, "could not create/add  pad");
    return NULL;
  }
}

static void
gst_audiomixer_release_pad (GstElement * element, GstPad * pad)
{
  GstAudioMixer *audiomixer;

  audiomixer = GST_AUDIO_MIXER (element);

  GST_DEBUG_OBJECT (audiomixer, "release pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  gst_child_proxy_child_removed (GST_CHILD_PROXY (audiomixer), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);
}


/* Called with object lock and pad object lock held */
static gboolean
gst_audiomixer_aggregate_one_buffer (GstAudioAggregator * aagg,
    GstAudioAggregatorPad * aaggpad, GstBuffer * inbuf, guint in_offset,
    GstBuffer * outbuf, guint out_offset, guint num_frames)
{
  GstAudioMixerPad *pad = GST_AUDIO_MIXER_PAD (aaggpad);
  GstMapInfo inmap;
  GstMapInfo outmap;
  gint bpf;

  if (pad->mute || pad->volume < G_MINDOUBLE) {
    GST_DEBUG_OBJECT (pad, "Skipping muted pad");
    return FALSE;
  }

  bpf = GST_AUDIO_INFO_BPF (&aagg->info);

  gst_buffer_map (outbuf, &outmap, GST_MAP_READWRITE);
  gst_buffer_map (inbuf, &inmap, GST_MAP_READ);
  GST_LOG_OBJECT (pad, "mixing %u bytes at offset %u from offset %u",
      num_frames * bpf, out_offset * bpf, in_offset * bpf);

  /* further buffers, need to add them */
  if (pad->volume == 1.0) {
    switch (aagg->info.finfo->format) {
      case GST_AUDIO_FORMAT_U8:
        audiomixer_orc_add_u8 ((gpointer) (outmap.data + out_offset * bpf),
            (gpointer) (inmap.data + in_offset * bpf),
            num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_S8:
        audiomixer_orc_add_s8 ((gpointer) (outmap.data + out_offset * bpf),
            (gpointer) (inmap.data + in_offset * bpf),
            num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_U16:
        audiomixer_orc_add_u16 ((gpointer) (outmap.data + out_offset * bpf),
            (gpointer) (inmap.data + in_offset * bpf),
            num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_S16:
        audiomixer_orc_add_s16 ((gpointer) (outmap.data + out_offset * bpf),
            (gpointer) (inmap.data + in_offset * bpf),
            num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_U32:
        audiomixer_orc_add_u32 ((gpointer) (outmap.data + out_offset * bpf),
            (gpointer) (inmap.data + in_offset * bpf),
            num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_S32:
        audiomixer_orc_add_s32 ((gpointer) (outmap.data + out_offset * bpf),
            (gpointer) (inmap.data + in_offset * bpf),
            num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_F32:
        audiomixer_orc_add_f32 ((gpointer) (outmap.data + out_offset * bpf),
            (gpointer) (inmap.data + in_offset * bpf),
            num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_F64:
        audiomixer_orc_add_f64 ((gpointer) (outmap.data + out_offset * bpf),
            (gpointer) (inmap.data + in_offset * bpf),
            num_frames * aagg->info.channels);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  } else {
    switch (aagg->info.finfo->format) {
      case GST_AUDIO_FORMAT_U8:
        audiomixer_orc_add_volume_u8 ((gpointer) (outmap.data +
                out_offset * bpf), (gpointer) (inmap.data + in_offset * bpf),
            pad->volume_i8, num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_S8:
        audiomixer_orc_add_volume_s8 ((gpointer) (outmap.data +
                out_offset * bpf), (gpointer) (inmap.data + in_offset * bpf),
            pad->volume_i8, num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_U16:
        audiomixer_orc_add_volume_u16 ((gpointer) (outmap.data +
                out_offset * bpf), (gpointer) (inmap.data + in_offset * bpf),
            pad->volume_i16, num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_S16:
        audiomixer_orc_add_volume_s16 ((gpointer) (outmap.data +
                out_offset * bpf), (gpointer) (inmap.data + in_offset * bpf),
            pad->volume_i16, num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_U32:
        audiomixer_orc_add_volume_u32 ((gpointer) (outmap.data +
                out_offset * bpf), (gpointer) (inmap.data + in_offset * bpf),
            pad->volume_i32, num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_S32:
        audiomixer_orc_add_volume_s32 ((gpointer) (outmap.data +
                out_offset * bpf), (gpointer) (inmap.data + in_offset * bpf),
            pad->volume_i32, num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_F32:
        audiomixer_orc_add_volume_f32 ((gpointer) (outmap.data +
                out_offset * bpf), (gpointer) (inmap.data + in_offset * bpf),
            pad->volume, num_frames * aagg->info.channels);
        break;
      case GST_AUDIO_FORMAT_F64:
        audiomixer_orc_add_volume_f64 ((gpointer) (outmap.data +
                out_offset * bpf), (gpointer) (inmap.data + in_offset * bpf),
            pad->volume, num_frames * aagg->info.channels);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }
  gst_buffer_unmap (inbuf, &inmap);
  gst_buffer_unmap (outbuf, &outmap);

  return TRUE;
}


/* GstChildProxy implementation */
static GObject *
gst_audiomixer_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (child_proxy);
  GObject *obj = NULL;

  GST_OBJECT_LOCK (audiomixer);
  obj = g_list_nth_data (GST_ELEMENT_CAST (audiomixer)->sinkpads, index);
  if (obj)
    gst_object_ref (obj);
  GST_OBJECT_UNLOCK (audiomixer);

  return obj;
}

static guint
gst_audiomixer_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  guint count = 0;
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (child_proxy);

  GST_OBJECT_LOCK (audiomixer);
  count = GST_ELEMENT_CAST (audiomixer)->numsinkpads;
  GST_OBJECT_UNLOCK (audiomixer);
  GST_INFO_OBJECT (audiomixer, "Children Count: %d", count);

  return count;
}

static void
gst_audiomixer_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  GST_INFO ("intializing child proxy interface");
  iface->get_child_by_index = gst_audiomixer_child_proxy_get_child_by_index;
  iface->get_children_count = gst_audiomixer_child_proxy_get_children_count;
}

/* Empty liveadder alias with non-zero latency */

typedef GstAudioMixer GstLiveAdder;
typedef GstAudioMixerClass GstLiveAdderClass;

static GType gst_live_adder_get_type (void);
#define GST_TYPE_LIVE_ADDER gst_live_adder_get_type ()

G_DEFINE_TYPE (GstLiveAdder, gst_live_adder, GST_TYPE_AUDIO_MIXER);

enum
{
  LIVEADDER_PROP_LATENCY = 1
};

static void
gst_live_adder_init (GstLiveAdder * self)
{
}

static void
gst_live_adder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case LIVEADDER_PROP_LATENCY:
    {
      GParamSpec *parent_spec =
          g_object_class_find_property (G_OBJECT_CLASS
          (gst_live_adder_parent_class), "latency");
      GObjectClass *pspec_class = g_type_class_peek (parent_spec->owner_type);
      GValue v = { 0 };

      g_value_init (&v, G_TYPE_INT64);

      g_value_set_int64 (&v, g_value_get_uint (value) * GST_MSECOND);

      G_OBJECT_CLASS (pspec_class)->set_property (object,
          parent_spec->param_id, &v, parent_spec);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_live_adder_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  switch (prop_id) {
    case LIVEADDER_PROP_LATENCY:
    {
      GParamSpec *parent_spec =
          g_object_class_find_property (G_OBJECT_CLASS
          (gst_live_adder_parent_class), "latency");
      GObjectClass *pspec_class = g_type_class_peek (parent_spec->owner_type);
      GValue v = { 0 };

      g_value_init (&v, G_TYPE_INT64);

      G_OBJECT_CLASS (pspec_class)->get_property (object,
          parent_spec->param_id, &v, parent_spec);

      g_value_set_uint (value, g_value_get_int64 (&v) / GST_MSECOND);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_live_adder_class_init (GstLiveAdderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_live_adder_set_property;
  gobject_class->get_property = gst_live_adder_get_property;

  g_object_class_install_property (gobject_class, LIVEADDER_PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency",
          "Additional latency in live mode to allow upstream "
          "to take longer to produce buffers for the current "
          "position (in milliseconds)", 0, G_MAXUINT,
          30, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "audiomixer", 0,
      "audio mixing element");

  if (!gst_element_register (plugin, "audiomixer", GST_RANK_NONE,
          GST_TYPE_AUDIO_MIXER))
    return FALSE;

  if (!gst_element_register (plugin, "liveadder", GST_RANK_NONE,
          GST_TYPE_LIVE_ADDER))
    return FALSE;

  if (!gst_element_register (plugin, "audiointerleave", GST_RANK_NONE,
          GST_TYPE_AUDIO_INTERLEAVE))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    audiomixer,
    "Mixes multiple audio streams",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
