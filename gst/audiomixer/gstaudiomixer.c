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
 *
 * The audiomixer allows to mix several streams into one by adding the data.
 * Mixed data is clamped to the min/max values of the data format.
 *
 * The audiomixer currently mixes all data received on the sinkpads as soon as
 * possible without trying to synchronize the streams.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch audiotestsrc freq=100 ! audiomixer name=mix ! audioconvert ! alsasink audiotestsrc freq=500 ! mix.
 * ]| This pipeline produces two sine waves mixed together.
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstaudiomixer.h"
#include <gst/audio/audio.h>
#include <string.h>             /* strcmp */
#include "gstaudiomixerorc.h"

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

G_DEFINE_TYPE (GstAudioMixerPad, gst_audiomixer_pad, GST_TYPE_AGGREGATOR_PAD);

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

static gboolean
gst_audiomixer_pad_flush_pad (GstAggregatorPad * aggpad,
    GstAggregator * aggregator)
{
  GstAudioMixerPad *pad = GST_AUDIO_MIXER_PAD (aggpad);

  GST_OBJECT_LOCK (aggpad);
  pad->position = pad->size = 0;
  pad->output_offset = pad->next_offset = -1;
  gst_buffer_replace (&pad->buffer, NULL);
  GST_OBJECT_UNLOCK (aggpad);

  return TRUE;
}

static void
gst_audiomixer_pad_class_init (GstAudioMixerPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAggregatorPadClass *aggpadclass = (GstAggregatorPadClass *) klass;

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

  aggpadclass->flush = GST_DEBUG_FUNCPTR (gst_audiomixer_pad_flush_pad);
}

static void
gst_audiomixer_pad_init (GstAudioMixerPad * pad)
{
  pad->volume = DEFAULT_PAD_VOLUME;
  pad->mute = DEFAULT_PAD_MUTE;

  pad->buffer = NULL;
  pad->position = 0;
  pad->size = 0;
  pad->output_offset = -1;
  pad->next_offset = -1;

}

#define DEFAULT_ALIGNMENT_THRESHOLD   (40 * GST_MSECOND)
#define DEFAULT_DISCONT_WAIT (1 * GST_SECOND)
#define DEFAULT_BLOCKSIZE (1024)

enum
{
  PROP_0,
  PROP_FILTER_CAPS,
  PROP_ALIGNMENT_THRESHOLD,
  PROP_DISCONT_WAIT,
  PROP_BLOCKSIZE
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
G_DEFINE_TYPE_WITH_CODE (GstAudioMixer, gst_audiomixer, GST_TYPE_AGGREGATOR,
    G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
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

static GstFlowReturn
gst_audiomixer_do_clip (GstAggregator * agg,
    GstAggregatorPad * bpad, GstBuffer * buffer, GstBuffer ** outbuf);
static GstFlowReturn gst_audiomixer_aggregate (GstAggregator * agg);

/* we can only accept caps that we and downstream can handle.
 * if we have filtercaps set, use those to constrain the target caps.
 */
static GstCaps *
gst_audiomixer_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstAggregator *agg;
  GstAudioMixer *audiomixer;
  GstCaps *result, *peercaps, *current_caps, *filter_caps;
  GstStructure *s;
  gint i, n;

  audiomixer = GST_AUDIO_MIXER (GST_PAD_PARENT (pad));
  agg = GST_AGGREGATOR (audiomixer);

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
  current_caps =
      audiomixer->current_caps ? gst_caps_ref (audiomixer->current_caps) : NULL;
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
      caps = gst_audiomixer_sink_getcaps (GST_PAD (aggpad), filter);
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
  GstCaps *caps;
  GstAudioInfo info;
  GstStructure *s;
  gint channels;

  caps = gst_caps_copy (orig_caps);

  s = gst_caps_get_structure (caps, 0);
  if (gst_structure_get_int (s, "channels", &channels))
    if (channels <= 2)
      gst_structure_remove_field (s, "channel-mask");

  if (!gst_audio_info_from_caps (&info, caps))
    goto invalid_format;

  GST_OBJECT_LOCK (audiomixer);
  /* don't allow reconfiguration for now; there's still a race between the
   * different upstream threads doing query_caps + accept_caps + sending
   * (possibly different) CAPS events, but there's not much we can do about
   * that, upstream needs to deal with it. */
  if (audiomixer->current_caps != NULL) {
    if (gst_audio_info_is_equal (&info, &audiomixer->info)) {
      GST_OBJECT_UNLOCK (audiomixer);
      gst_caps_unref (caps);
      return TRUE;
    } else {
      GST_DEBUG_OBJECT (pad, "got input caps %" GST_PTR_FORMAT ", but "
          "current caps are %" GST_PTR_FORMAT, caps, audiomixer->current_caps);
      GST_OBJECT_UNLOCK (audiomixer);
      gst_pad_push_event (pad, gst_event_new_reconfigure ());
      gst_caps_unref (caps);
      return FALSE;
    }
  }

  GST_INFO_OBJECT (pad, "setting caps to %" GST_PTR_FORMAT, caps);
  gst_caps_replace (&audiomixer->current_caps, caps);

  memcpy (&audiomixer->info, &info, sizeof (info));
  audiomixer->send_caps = TRUE;
  GST_OBJECT_UNLOCK (audiomixer);
  /* send caps event later, after stream-start event */

  GST_INFO_OBJECT (pad, "handle caps change to %" GST_PTR_FORMAT, caps);

  gst_caps_unref (caps);

  return TRUE;

  /* ERRORS */
invalid_format:
  {
    gst_caps_unref (caps);
    GST_WARNING_OBJECT (audiomixer, "invalid format set as caps");
    return FALSE;
  }
}

/* FIXME, the duration query should reflect how long you will produce
 * data, that is the amount of stream time until you will emit EOS.
 *
 * For synchronized mixing this is always the max of all the durations
 * of upstream since we emit EOS when all of them finished.
 *
 * We don't do synchronized mixing so this really depends on where the
 * streams where punched in and what their relative offsets are against
 * eachother which we can get from the first timestamps we see.
 *
 * When we add a new stream (or remove a stream) the duration might
 * also become invalid again and we need to post a new DURATION
 * message to notify this fact to the parent.
 * For now we take the max of all the upstream elements so the simple
 * cases work at least somewhat.
 */
static gboolean
gst_audiomixer_query_duration (GstAudioMixer * audiomixer, GstQuery * query)
{
  gint64 max;
  gboolean res;
  GstFormat format;
  GstIterator *it;
  gboolean done;
  GValue item = { 0, };

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  max = -1;
  res = TRUE;
  done = FALSE;

  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (audiomixer));
  while (!done) {
    GstIteratorResult ires;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = g_value_get_object (&item);
        gint64 duration;

        /* ask sink peer for duration */
        res &= gst_pad_peer_query_duration (pad, format, &duration);
        /* take max from all valid return values */
        if (res) {
          /* valid unknown length, stop searching */
          if (duration == -1) {
            max = duration;
            done = TRUE;
          }
          /* else see if bigger than current max */
          else if (duration > max)
            max = duration;
        }
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        max = -1;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (it);

  if (res) {
    /* and store the max */
    GST_DEBUG_OBJECT (audiomixer, "Total duration in format %s: %"
        GST_TIME_FORMAT, gst_format_get_name (format), GST_TIME_ARGS (max));
    gst_query_set_duration (query, format, max);
  }

  return res;
}

static gboolean
gst_audiomixer_src_query (GstAggregator * agg, GstQuery * query)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (agg);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          /* FIXME, bring to stream time, might be tricky */
          gst_query_set_position (query, format, agg->segment.position);
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, format, audiomixer->offset);
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_audiomixer_query_duration (audiomixer, query);
      break;
    case GST_QUERY_LATENCY:
      res =
          GST_AGGREGATOR_CLASS (gst_audiomixer_parent_class)->src_query
          (agg, query);
      break;
    default:
      /* FIXME, needs a custom query handler because we have multiple
       * sinkpads */
      res = gst_pad_query_default (GST_PAD (agg->srcpad), GST_OBJECT (agg),
          query);
      break;
  }

  return res;
}

/* event handling */

typedef struct
{
  GstEvent *event;
  gboolean flush;
} EventData;

static gboolean
gst_audiomixer_src_event (GstAggregator * agg, GstEvent * event)
{
  gboolean result;

  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (agg);
  GST_DEBUG_OBJECT (agg->srcpad, "Got %s event on src pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      /* QoS might be tricky */
      gst_event_unref (event);
      return FALSE;
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      gst_event_unref (event);
      return FALSE;
      break;
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      gdouble rate;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GstFormat seek_format, dest_format;

      /* parse the seek parameters */
      gst_event_parse_seek (event, &rate, &seek_format, &flags, &start_type,
          &start, &stop_type, &stop);

      /* Check the seeking parametters before linking up */
      if ((start_type != GST_SEEK_TYPE_NONE)
          && (start_type != GST_SEEK_TYPE_SET)) {
        result = FALSE;
        GST_DEBUG_OBJECT (audiomixer,
            "seeking failed, unhandled seek type for start: %d", start_type);
        goto done;
      }
      if ((stop_type != GST_SEEK_TYPE_NONE) && (stop_type != GST_SEEK_TYPE_SET)) {
        result = FALSE;
        GST_DEBUG_OBJECT (audiomixer,
            "seeking failed, unhandled seek type for end: %d", stop_type);
        goto done;
      }

      dest_format = agg->segment.format;
      if (seek_format != dest_format) {
        result = FALSE;
        GST_DEBUG_OBJECT (audiomixer,
            "seeking failed, unhandled seek format: %d", seek_format);
        goto done;
      }

      /* Link up */
      result = GST_AGGREGATOR_CLASS (parent_class)->src_event (agg, event);

      if (result)
        audiomixer->base_time = agg->segment.start;
      goto done;
    }
      break;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_event (agg, event);

done:
  return result;
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
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;
      gst_event_parse_segment (event, &segment);
      if (segment->rate != agg->segment.rate) {
        GST_ERROR_OBJECT (aggpad,
            "Got segment event with wrong rate %lf, expected %lf",
            segment->rate, agg->segment.rate);
        res = FALSE;
        gst_event_unref (event);
        event = NULL;
      } else if (segment->rate < 0.0) {
        GST_ERROR_OBJECT (aggpad, "Negative rates not supported yet");
        res = FALSE;
        gst_event_unref (event);
        event = NULL;
      }

      if (event) {
        res =
            GST_AGGREGATOR_CLASS (parent_class)->sink_event (agg, aggpad,
            event);

        if (res)
          aggpad->segment.position = segment->start + segment->offset;

        event = NULL;
      }
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
gst_audiomixer_reset (GstAudioMixer * audiomixer)
{
  audiomixer->offset = 0;
  gst_caps_replace (&audiomixer->current_caps, NULL);
  audiomixer->discont_time = GST_CLOCK_TIME_NONE;
}

static gboolean
gst_audiomixer_start (GstAggregator * agg)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (agg);

  if (!GST_AGGREGATOR_CLASS (parent_class)->start (agg))
    return FALSE;

  gst_audiomixer_reset (audiomixer);

  return TRUE;
}

static gboolean
gst_audiomixer_stop (GstAggregator * agg)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (agg);

  if (!GST_AGGREGATOR_CLASS (parent_class)->stop (agg))
    return FALSE;

  gst_audiomixer_reset (audiomixer);

  return TRUE;
}

static GstFlowReturn
gst_audiomixer_flush (GstAggregator * agg)
{
  gst_audiomixer_reset (GST_AUDIO_MIXER (agg));

  return GST_FLOW_OK;
}

static gboolean
gst_audiomixer_send_event (GstElement * element, GstEvent * event)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (element);

  gboolean res = GST_ELEMENT_CLASS (parent_class)->send_event (element, event);

  GST_STATE_LOCK (element);
  if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK &&
      GST_STATE (element) < GST_STATE_PAUSED) {
    audiomixer->base_time = GST_AGGREGATOR (element)->segment.start;
  }
  GST_STATE_UNLOCK (element);

  return res;
}


static void
gst_audiomixer_class_init (GstAudioMixerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;

  gobject_class->set_property = gst_audiomixer_set_property;
  gobject_class->get_property = gst_audiomixer_get_property;
  gobject_class->dispose = gst_audiomixer_dispose;

  g_object_class_install_property (gobject_class, PROP_FILTER_CAPS,
      g_param_spec_boxed ("caps", "Target caps",
          "Set target format for mixing (NULL means ANY). "
          "Setting this property takes a reference to the supplied GstCaps "
          "object", GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALIGNMENT_THRESHOLD,
      g_param_spec_uint64 ("alignment-threshold", "Alignment Threshold",
          "Timestamp alignment threshold in nanoseconds", 0,
          G_MAXUINT64 - 1, DEFAULT_ALIGNMENT_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DISCONT_WAIT,
      g_param_spec_uint64 ("discont-wait", "Discont Wait",
          "Window of time in nanoseconds to wait before "
          "creating a discontinuity", 0,
          G_MAXUINT64 - 1, DEFAULT_DISCONT_WAIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BLOCKSIZE,
      g_param_spec_uint ("blocksize", "Block Size",
          "Output block size in number of samples", 0,
          G_MAXUINT, DEFAULT_BLOCKSIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_audiomixer_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_audiomixer_sink_template));
  gst_element_class_set_static_metadata (gstelement_class, "AudioMixer",
      "Generic/Audio",
      "Mixes multiple audio streams",
      "Sebastian Dröge <sebastian@centricular.com>");

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_audiomixer_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_audiomixer_release_pad);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_audiomixer_send_event);


  agg_class->sinkpads_type = GST_TYPE_AUDIO_MIXER_PAD;
  agg_class->start = gst_audiomixer_start;
  agg_class->stop = gst_audiomixer_stop;

  agg_class->sink_query = GST_DEBUG_FUNCPTR (gst_audiomixer_sink_query);
  agg_class->sink_event = GST_DEBUG_FUNCPTR (gst_audiomixer_sink_event);

  agg_class->aggregate = GST_DEBUG_FUNCPTR (gst_audiomixer_aggregate);
  agg_class->clip = GST_DEBUG_FUNCPTR (gst_audiomixer_do_clip);

  agg_class->src_event = GST_DEBUG_FUNCPTR (gst_audiomixer_src_event);
  agg_class->src_query = GST_DEBUG_FUNCPTR (gst_audiomixer_src_query);

  agg_class->flush = GST_DEBUG_FUNCPTR (gst_audiomixer_flush);
}

static void
gst_audiomixer_init (GstAudioMixer * audiomixer)
{
  audiomixer->current_caps = NULL;
  gst_audio_info_init (&audiomixer->info);

  audiomixer->filter_caps = NULL;
  audiomixer->alignment_threshold = DEFAULT_ALIGNMENT_THRESHOLD;
  audiomixer->discont_wait = DEFAULT_DISCONT_WAIT;
  audiomixer->blocksize = DEFAULT_BLOCKSIZE;
}

static void
gst_audiomixer_dispose (GObject * object)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (object);

  gst_caps_replace (&audiomixer->filter_caps, NULL);
  gst_caps_replace (&audiomixer->current_caps, NULL);

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
    case PROP_ALIGNMENT_THRESHOLD:
      audiomixer->alignment_threshold = g_value_get_uint64 (value);
      break;
    case PROP_DISCONT_WAIT:
      audiomixer->discont_wait = g_value_get_uint64 (value);
      break;
    case PROP_BLOCKSIZE:
      audiomixer->blocksize = g_value_get_uint (value);
      break;
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
    case PROP_ALIGNMENT_THRESHOLD:
      g_value_set_uint64 (value, audiomixer->alignment_threshold);
      break;
    case PROP_DISCONT_WAIT:
      g_value_set_uint64 (value, audiomixer->discont_wait);
      break;
    case PROP_BLOCKSIZE:
      g_value_set_uint (value, audiomixer->blocksize);
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

static GstFlowReturn
gst_audiomixer_do_clip (GstAggregator * agg,
    GstAggregatorPad * bpad, GstBuffer * buffer, GstBuffer ** out)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (agg);
  gint rate, bpf;

  rate = GST_AUDIO_INFO_RATE (&audiomixer->info);
  bpf = GST_AUDIO_INFO_BPF (&audiomixer->info);

  buffer = gst_audio_buffer_clip (buffer, &bpad->segment, rate, bpf);

  *out = buffer;
  return GST_FLOW_OK;
}

static gboolean
gst_audio_mixer_fill_buffer (GstAudioMixer * audiomixer, GstAudioMixerPad * pad,
    GstBuffer * inbuf)
{
  GstClockTime start_time, end_time;
  gboolean discont = FALSE;
  guint64 start_offset, end_offset;
  GstClockTime timestamp, stream_time;
  gint rate, bpf;

  GstAggregator *agg = GST_AGGREGATOR (audiomixer);
  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);

  g_assert (pad->buffer == NULL);

  rate = GST_AUDIO_INFO_RATE (&audiomixer->info);
  bpf = GST_AUDIO_INFO_BPF (&audiomixer->info);

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  stream_time = gst_segment_to_stream_time (&agg->segment, GST_FORMAT_TIME,
      timestamp);

  /* sync object properties on stream time */
  /* TODO: Ideally we would want to do that on every sample */
  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (pad), stream_time);

  pad->position = 0;
  pad->size = gst_buffer_get_size (inbuf);

  start_time = GST_BUFFER_PTS (inbuf);
  end_time =
      start_time + gst_util_uint64_scale_ceil (pad->size / bpf,
      GST_SECOND, rate);

  start_offset = gst_util_uint64_scale (start_time, rate, GST_SECOND);
  end_offset = start_offset + pad->size / bpf;

  if (GST_BUFFER_IS_DISCONT (inbuf)
      || GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_RESYNC)
      || pad->next_offset == -1) {
    discont = TRUE;
  } else {
    guint64 diff, max_sample_diff;

    /* Check discont, based on audiobasesink */
    if (start_offset <= pad->next_offset)
      diff = pad->next_offset - start_offset;
    else
      diff = start_offset - pad->next_offset;

    max_sample_diff =
        gst_util_uint64_scale_int (audiomixer->alignment_threshold, rate,
        GST_SECOND);

    /* Discont! */
    if (G_UNLIKELY (diff >= max_sample_diff)) {
      if (audiomixer->discont_wait > 0) {
        if (audiomixer->discont_time == GST_CLOCK_TIME_NONE) {
          audiomixer->discont_time = start_time;
        } else if (start_time - audiomixer->discont_time >=
            audiomixer->discont_wait) {
          discont = TRUE;
          audiomixer->discont_time = GST_CLOCK_TIME_NONE;
        }
      } else {
        discont = TRUE;
      }
    } else if (G_UNLIKELY (audiomixer->discont_time != GST_CLOCK_TIME_NONE)) {
      /* we have had a discont, but are now back on track! */
      audiomixer->discont_time = GST_CLOCK_TIME_NONE;
    }
  }

  if (discont) {
    /* Have discont, need resync */
    if (pad->next_offset != -1)
      GST_INFO_OBJECT (pad, "Have discont. Expected %"
          G_GUINT64_FORMAT ", got %" G_GUINT64_FORMAT,
          pad->next_offset, start_offset);
    pad->output_offset = -1;
  } else {
    audiomixer->discont_time = GST_CLOCK_TIME_NONE;
  }

  pad->next_offset = end_offset;

  if (pad->output_offset == -1) {
    GstClockTime start_running_time;
    GstClockTime end_running_time;
    guint64 start_running_time_offset;
    guint64 end_running_time_offset;

    aggpad->segment.base = audiomixer->base_time;
    start_running_time =
        gst_segment_to_running_time (&aggpad->segment,
        GST_FORMAT_TIME, start_time);
    end_running_time =
        gst_segment_to_running_time (&aggpad->segment,
        GST_FORMAT_TIME, end_time);
    start_running_time_offset =
        gst_util_uint64_scale (start_running_time, rate, GST_SECOND);
    end_running_time_offset =
        gst_util_uint64_scale (end_running_time, rate, GST_SECOND);

    if (end_running_time_offset < audiomixer->offset) {
      GstBuffer *buf;

      /* Before output segment, drop */
      gst_buffer_unref (inbuf);
      pad->buffer = NULL;
      buf = gst_aggregator_pad_steal_buffer (aggpad);
      if (buf)
        gst_buffer_unref (buf);
      pad->position = 0;
      pad->size = 0;
      pad->output_offset = -1;
      GST_DEBUG_OBJECT (pad,
          "Buffer before segment or current position: %" G_GUINT64_FORMAT " < %"
          G_GUINT64_FORMAT, end_running_time_offset, audiomixer->offset);
      return FALSE;
    }

    if (start_running_time_offset < audiomixer->offset) {
      GstBuffer *buf;
      guint diff = (audiomixer->offset - start_running_time_offset) * bpf;
      pad->position += diff;
      pad->size -= diff;
      /* FIXME: This could only happen due to rounding errors */
      if (pad->size == 0) {
        /* Empty buffer, drop */
        gst_buffer_unref (inbuf);
        pad->buffer = NULL;
        buf = gst_aggregator_pad_steal_buffer (aggpad);
        if (buf)
          gst_buffer_unref (buf);
        pad->position = 0;
        pad->size = 0;
        pad->output_offset = -1;
        GST_DEBUG_OBJECT (pad,
            "Buffer before segment or current position: %" G_GUINT64_FORMAT
            " < %" G_GUINT64_FORMAT, end_running_time_offset,
            audiomixer->offset);
        return FALSE;
      }
    }

    pad->output_offset = MAX (start_running_time_offset, audiomixer->offset);
    GST_DEBUG_OBJECT (pad,
        "Buffer resynced: Pad offset %" G_GUINT64_FORMAT
        ", current mixer offset %" G_GUINT64_FORMAT, pad->output_offset,
        audiomixer->offset);
  }

  GST_LOG_OBJECT (pad,
      "Queued new buffer at offset %" G_GUINT64_FORMAT, pad->output_offset);
  pad->buffer = inbuf;

  return TRUE;
}

static void
gst_audio_mixer_mix_buffer (GstAudioMixer * audiomixer, GstAudioMixerPad * pad,
    GstMapInfo * outmap)
{
  guint overlap;
  guint out_start;
  GstBuffer *inbuf;
  GstMapInfo inmap;
  gint bpf;

  GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (pad);

  bpf = GST_AUDIO_INFO_BPF (&audiomixer->info);

  /* Overlap => mix */
  if (audiomixer->offset < pad->output_offset)
    out_start = pad->output_offset - audiomixer->offset;
  else
    out_start = 0;

  overlap = pad->size / bpf - pad->position / bpf;
  if (overlap > audiomixer->blocksize - out_start)
    overlap = audiomixer->blocksize - out_start;

  inbuf = gst_aggregator_pad_get_buffer (aggpad);
  if (inbuf == NULL)
    return;

  GST_OBJECT_LOCK (pad);
  if (pad->mute || pad->volume < G_MINDOUBLE) {
    GST_DEBUG_OBJECT (pad, "Skipping muted pad");
    gst_buffer_unref (inbuf);
    pad->position += overlap * bpf;
    pad->output_offset += overlap;
    if (pad->position >= pad->size) {
      GstBuffer *buf;
      /* Buffer done, drop it */
      gst_buffer_replace (&pad->buffer, NULL);
      buf = gst_aggregator_pad_steal_buffer (aggpad);
      if (buf)
        gst_buffer_unref (buf);
    }
    GST_OBJECT_UNLOCK (pad);
    return;
  }

  if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
    GstBuffer *aggpadbuf = gst_aggregator_pad_steal_buffer (aggpad);

    /* skip gap buffer */
    GST_LOG_OBJECT (pad, "skipping GAP buffer");
    gst_buffer_unref (inbuf);
    pad->output_offset += pad->size / bpf;
    /* Buffer done, drop it */
    gst_buffer_replace (&pad->buffer, NULL);
    if (aggpadbuf)
      gst_buffer_unref (aggpadbuf);
    GST_OBJECT_UNLOCK (pad);
    return;
  }

  gst_buffer_map (inbuf, &inmap, GST_MAP_READ);
  GST_LOG_OBJECT (pad, "mixing %u bytes at offset %u from offset %u",
      overlap * bpf, out_start * bpf, pad->position);
  /* further buffers, need to add them */
  if (pad->volume == 1.0) {
    switch (audiomixer->info.finfo->format) {
      case GST_AUDIO_FORMAT_U8:
        audiomixer_orc_add_u8 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + pad->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S8:
        audiomixer_orc_add_s8 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + pad->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_U16:
        audiomixer_orc_add_u16 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + pad->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S16:
        audiomixer_orc_add_s16 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + pad->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_U32:
        audiomixer_orc_add_u32 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + pad->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S32:
        audiomixer_orc_add_s32 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + pad->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_F32:
        audiomixer_orc_add_f32 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + pad->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_F64:
        audiomixer_orc_add_f64 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + pad->position),
            overlap * audiomixer->info.channels);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  } else {
    switch (audiomixer->info.finfo->format) {
      case GST_AUDIO_FORMAT_U8:
        audiomixer_orc_add_volume_u8 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + pad->position),
            pad->volume_i8, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S8:
        audiomixer_orc_add_volume_s8 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + pad->position),
            pad->volume_i8, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_U16:
        audiomixer_orc_add_volume_u16 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + pad->position),
            pad->volume_i16, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S16:
        audiomixer_orc_add_volume_s16 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + pad->position),
            pad->volume_i16, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_U32:
        audiomixer_orc_add_volume_u32 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + pad->position),
            pad->volume_i32, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S32:
        audiomixer_orc_add_volume_s32 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + pad->position),
            pad->volume_i32, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_F32:
        audiomixer_orc_add_volume_f32 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + pad->position),
            pad->volume, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_F64:
        audiomixer_orc_add_volume_f64 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + pad->position),
            pad->volume, overlap * audiomixer->info.channels);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }
  gst_buffer_unmap (inbuf, &inmap);
  gst_buffer_unref (inbuf);

  pad->position += overlap * bpf;
  pad->output_offset += overlap;

  if (pad->position == pad->size) {
    GstBuffer *buf;

    /* Buffer done, drop it */
    gst_buffer_replace (&pad->buffer, NULL);
    buf = gst_aggregator_pad_steal_buffer (aggpad);
    if (buf)
      gst_buffer_unref (buf);
    GST_DEBUG_OBJECT (pad, "Finished mixing buffer, waiting for next");
  }

  GST_OBJECT_UNLOCK (pad);
}

static GstFlowReturn
gst_audiomixer_aggregate (GstAggregator * agg)
{
  /* Get all pads that have data for us and store them in a
   * new list.
   *
   * Calculate the current output offset/timestamp and
   * offset_end/timestamp_end. Allocate a silence buffer
   * for this and store it.
   *
   * For all pads:
   * 1) Once per input buffer (cached)
   *   1) Check discont (flag and timestamp with tolerance)
   *   2) If discont or new, resync. That means:
   *     1) Drop all start data of the buffer that comes before
   *        the current position/offset.
   *     2) Calculate the offset (output segment!) that the first
   *        frame of the input buffer corresponds to. Base this on
   *        the running time.
   *
   * 2) If the current pad's offset/offset_end overlaps with the output
   *    offset/offset_end, mix it at the appropiate position in the output
   *    buffer and advance the pad's position. Remember if this pad needs
   *    a new buffer to advance behind the output offset_end.
   *
   * 3) If we had no pad with a buffer, go EOS.
   *
   * 4) If we had at least one pad that did not advance behind output
   *    offset_end, let collected be called again for the current
   *    output offset/offset_end.
   */
  GstAudioMixer *audiomixer;
  GList *iter;
  GstFlowReturn ret;
  GstBuffer *outbuf = NULL;
  GstMapInfo outmap;
  gint64 next_offset;
  gint64 next_timestamp;
  gint rate, bpf;
  gboolean dropped = FALSE;
  gboolean is_eos = TRUE;
  gboolean is_done = TRUE;

  audiomixer = GST_AUDIO_MIXER (agg);

  /* this is fatal */
  if (G_UNLIKELY (audiomixer->info.finfo->format == GST_AUDIO_FORMAT_UNKNOWN))
    goto not_negotiated;

  if (audiomixer->send_caps) {
    gst_aggregator_set_src_caps (agg, audiomixer->current_caps);

    if (agg->segment.rate > 0.0)
      agg->segment.position = agg->segment.start;
    else
      agg->segment.position = agg->segment.stop;

    audiomixer->offset = gst_util_uint64_scale (agg->segment.position,
        GST_AUDIO_INFO_RATE (&audiomixer->info), GST_SECOND);

    audiomixer->send_caps = FALSE;
  }

  rate = GST_AUDIO_INFO_RATE (&audiomixer->info);
  bpf = GST_AUDIO_INFO_BPF (&audiomixer->info);

  /* for the next timestamp, use the sample counter, which will
   * never accumulate rounding errors */

  /* FIXME: Reverse mixing does not work at all yet */
  if (agg->segment.rate > 0.0) {
    next_offset = audiomixer->offset + audiomixer->blocksize;
  } else {
    next_offset = audiomixer->offset - audiomixer->blocksize;
  }

  next_timestamp = gst_util_uint64_scale (next_offset, GST_SECOND, rate);

  if (audiomixer->current_buffer) {
    outbuf = audiomixer->current_buffer;
  } else {
    outbuf = gst_buffer_new_and_alloc (audiomixer->blocksize * bpf);
    gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE);
    gst_audio_format_fill_silence (audiomixer->info.finfo, outmap.data,
        outmap.size);
    gst_buffer_unmap (outbuf, &outmap);
    audiomixer->current_buffer = outbuf;
  }

  GST_LOG_OBJECT (agg,
      "Starting to mix %u samples for offset %" G_GUINT64_FORMAT
      " with timestamp %" GST_TIME_FORMAT, audiomixer->blocksize,
      audiomixer->offset, GST_TIME_ARGS (agg->segment.position));

  gst_buffer_map (outbuf, &outmap, GST_MAP_READWRITE);

  GST_OBJECT_LOCK (agg);
  for (iter = GST_ELEMENT (agg)->sinkpads; iter; iter = iter->next) {
    GstBuffer *inbuf;
    GstAudioMixerPad *pad = GST_AUDIO_MIXER_PAD (iter->data);
    GstAggregatorPad *aggpad = GST_AGGREGATOR_PAD (iter->data);


    inbuf = gst_aggregator_pad_get_buffer (aggpad);
    if (!inbuf)
      continue;

    /* New buffer? */
    if (!pad->buffer || pad->buffer != inbuf) {
      /* Takes ownership of buffer */
      if (!gst_audio_mixer_fill_buffer (audiomixer, pad, inbuf)) {
        dropped = TRUE;
        continue;
      }
    } else {
      gst_buffer_unref (inbuf);
    }

    if (!pad->buffer && !dropped && GST_AGGREGATOR_PAD (pad)->eos) {
      GST_DEBUG_OBJECT (aggpad, "Pad is in EOS state");
    } else {
      is_eos = FALSE;
    }

    /* At this point adata->output_offset >= audiomixer->offset or we have no buffer anymore */
    if (pad->output_offset >= audiomixer->offset
        && pad->output_offset <
        audiomixer->offset + audiomixer->blocksize && pad->buffer) {
      GST_LOG_OBJECT (aggpad, "Mixing buffer for current offset");
      gst_audio_mixer_mix_buffer (audiomixer, pad, &outmap);
      if (pad->output_offset >= next_offset) {
        GST_DEBUG_OBJECT (pad,
            "Pad is after current offset: %" G_GUINT64_FORMAT " >= %"
            G_GUINT64_FORMAT, pad->output_offset, next_offset);
      } else {
        is_done = FALSE;
      }
    }
  }
  GST_OBJECT_UNLOCK (agg);

  gst_buffer_unmap (outbuf, &outmap);

  if (dropped) {
    /* We dropped a buffer, retry */
    GST_INFO_OBJECT (audiomixer,
        "A pad dropped a buffer, wait for the next one");
    return GST_FLOW_OK;
  }

  if (!is_done && !is_eos) {
    /* Get more buffers */
    GST_INFO_OBJECT (audiomixer,
        "We're not done yet for the current offset," " waiting for more data");
    return GST_FLOW_OK;
  }

  if (is_eos) {
    gint64 max_offset = 0;
    gboolean empty_buffer = TRUE;

    GST_DEBUG_OBJECT (audiomixer, "We're EOS");


    GST_OBJECT_LOCK (agg);
    for (iter = GST_ELEMENT (agg)->sinkpads; iter; iter = iter->next) {
      GstAudioMixerPad *pad = GST_AUDIO_MIXER_PAD (iter->data);

      max_offset = MAX ((gint64) max_offset, (gint64) pad->output_offset);
      if (pad->output_offset > audiomixer->offset)
        empty_buffer = FALSE;
    }
    GST_OBJECT_UNLOCK (agg);

    /* This means EOS or no pads at all */
    if (empty_buffer) {
      gst_buffer_replace (&audiomixer->current_buffer, NULL);
      goto eos;
    }

    if (max_offset <= next_offset) {
      GST_DEBUG_OBJECT (audiomixer,
          "Last buffer is incomplete: %" G_GUINT64_FORMAT " <= %"
          G_GUINT64_FORMAT, max_offset, next_offset);
      next_offset = max_offset;

      gst_buffer_resize (outbuf, 0, (next_offset - audiomixer->offset) * bpf);
      next_timestamp = gst_util_uint64_scale (next_offset, GST_SECOND, rate);
    }
  }

  /* set timestamps on the output buffer */
  if (agg->segment.rate > 0.0) {
    GST_BUFFER_TIMESTAMP (outbuf) = agg->segment.position;
    GST_BUFFER_OFFSET (outbuf) = audiomixer->offset;
    GST_BUFFER_OFFSET_END (outbuf) = next_offset;
    GST_BUFFER_DURATION (outbuf) = next_timestamp - agg->segment.position;
  } else {
    GST_BUFFER_TIMESTAMP (outbuf) = next_timestamp;
    GST_BUFFER_OFFSET (outbuf) = next_offset;
    GST_BUFFER_OFFSET_END (outbuf) = audiomixer->offset;
    GST_BUFFER_DURATION (outbuf) = agg->segment.position - next_timestamp;
  }

  audiomixer->offset = next_offset;
  agg->segment.position = next_timestamp;

  /* send it out */
  GST_LOG_OBJECT (audiomixer,
      "pushing outbuf %p, timestamp %" GST_TIME_FORMAT " offset %"
      G_GINT64_FORMAT, outbuf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_BUFFER_OFFSET (outbuf));

  ret = gst_aggregator_finish_buffer (agg, audiomixer->current_buffer);
  audiomixer->current_buffer = NULL;

  GST_LOG_OBJECT (audiomixer, "pushed outbuf, result = %s",
      gst_flow_get_name (ret));

  if (ret == GST_FLOW_OK && is_eos)
    goto eos;

  return ret;
  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (audiomixer, STREAM, FORMAT, (NULL),
        ("Unknown data received, not negotiated"));
    return GST_FLOW_NOT_NEGOTIATED;
  }

eos:
  {
    GST_DEBUG_OBJECT (audiomixer, "EOS");
    return GST_FLOW_EOS;
  }
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

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "audiomixer", 0,
      "audio mixing element");

  if (!gst_element_register (plugin, "audiomixer", GST_RANK_NONE,
          GST_TYPE_AUDIO_MIXER))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    audiomixer,
    "Mixes multiple audio streams",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
