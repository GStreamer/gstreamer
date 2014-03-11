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

typedef struct _GstAudioMixerCollect GstAudioMixerCollect;
struct _GstAudioMixerCollect
{
  GstCollectData collect;       /* we extend the CollectData */

  GstBuffer *buffer;            /* current buffer we're mixing,
                                   for comparison with collect.buffer
                                   to see if we need to update our
                                   cached values. */
  guint position, size;

  guint64 output_offset;        /* Offset in output segment that
                                   collect.pos refers to in the
                                   current buffer. */

  guint64 next_offset;          /* Next expected offset in the input segment */
};

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

G_DEFINE_TYPE (GstAudioMixerPad, gst_audiomixer_pad, GST_TYPE_PAD);

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
G_DEFINE_TYPE_WITH_CODE (GstAudioMixer, gst_audiomixer, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_audiomixer_child_proxy_init));

static void gst_audiomixer_dispose (GObject * object);
static void gst_audiomixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audiomixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_audiomixer_setcaps (GstAudioMixer * audiomixer,
    GstPad * pad, GstCaps * caps);
static gboolean gst_audiomixer_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_audiomixer_sink_query (GstCollectPads * pads,
    GstCollectData * pad, GstQuery * query, gpointer user_data);
static gboolean gst_audiomixer_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_audiomixer_sink_event (GstCollectPads * pads,
    GstCollectData * pad, GstEvent * event, gpointer user_data);

static GstPad *gst_audiomixer_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused, const GstCaps * caps);
static void gst_audiomixer_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_audiomixer_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_audiomixer_do_clip (GstCollectPads * pads,
    GstCollectData * data, GstBuffer * buffer, GstBuffer ** out,
    gpointer user_data);
static GstFlowReturn gst_audiomixer_collected (GstCollectPads * pads,
    gpointer user_data);

/* we can only accept caps that we and downstream can handle.
 * if we have filtercaps set, use those to constrain the target caps.
 */
static GstCaps *
gst_audiomixer_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstAudioMixer *audiomixer;
  GstCaps *result, *peercaps, *current_caps, *filter_caps;
  GstStructure *s;
  gint i, n;

  audiomixer = GST_AUDIO_MIXER (GST_PAD_PARENT (pad));

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
  peercaps = gst_pad_peer_query_caps (audiomixer->srcpad, filter_caps);

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
gst_audiomixer_sink_query (GstCollectPads * pads, GstCollectData * pad,
    GstQuery * query, gpointer user_data)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_audiomixer_sink_getcaps (pad->pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res = gst_collect_pads_query_default (pads, pad, query, FALSE);
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
gst_audiomixer_query_latency (GstAudioMixer * audiomixer, GstQuery * query)
{
  GstClockTime min, max;
  gboolean live;
  gboolean res;
  GstIterator *it;
  gboolean done;
  GValue item = { 0, };

  res = TRUE;
  done = FALSE;

  live = FALSE;
  min = 0;
  max = GST_CLOCK_TIME_NONE;

  /* Take maximum of all latency values */
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
        GstQuery *peerquery;
        GstClockTime min_cur, max_cur;
        gboolean live_cur;

        peerquery = gst_query_new_latency ();

        /* Ask peer for latency */
        res &= gst_pad_peer_query (pad, peerquery);

        /* take max from all valid return values */
        if (res) {
          gst_query_parse_latency (peerquery, &live_cur, &min_cur, &max_cur);

          if (min_cur > min)
            min = min_cur;

          if (max_cur != GST_CLOCK_TIME_NONE &&
              ((max != GST_CLOCK_TIME_NONE && max_cur > max) ||
                  (max == GST_CLOCK_TIME_NONE)))
            max = max_cur;

          live = live || live_cur;
        }

        gst_query_unref (peerquery);
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        live = FALSE;
        min = 0;
        max = GST_CLOCK_TIME_NONE;
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
    /* store the results */
    GST_DEBUG_OBJECT (audiomixer, "Calculated total latency: live %s, min %"
        GST_TIME_FORMAT ", max %" GST_TIME_FORMAT,
        (live ? "yes" : "no"), GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    gst_query_set_latency (query, live, min, max);
  }

  return res;
}

static gboolean
gst_audiomixer_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (parent);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          /* FIXME, bring to stream time, might be tricky */
          gst_query_set_position (query, format, audiomixer->segment.position);
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
      res = gst_audiomixer_query_latency (audiomixer, query);
      break;
    default:
      /* FIXME, needs a custom query handler because we have multiple
       * sinkpads */
      res = gst_pad_query_default (pad, parent, query);
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

/* FIXME: What is this supposed to solve? */
static gboolean
forward_event_func (const GValue * val, GValue * ret, EventData * data)
{
  GstPad *pad = g_value_get_object (val);
  GstEvent *event = data->event;
  GstPad *peer;

  gst_event_ref (event);
  GST_LOG_OBJECT (pad, "About to send event %s", GST_EVENT_TYPE_NAME (event));
  peer = gst_pad_get_peer (pad);
  /* collect pad might have been set flushing,
   * so bypass core checking that and send directly to peer */
  if (!peer || !gst_pad_send_event (peer, event)) {
    if (!peer)
      gst_event_unref (event);
    GST_WARNING_OBJECT (pad, "Sending event  %p (%s) failed.",
        event, GST_EVENT_TYPE_NAME (event));
    /* quick hack to unflush the pads, ideally we need a way to just unflush
     * this single collect pad */
    if (data->flush)
      gst_pad_send_event (pad, gst_event_new_flush_stop (TRUE));
  } else {
    g_value_set_boolean (ret, TRUE);
    GST_LOG_OBJECT (pad, "Sent event  %p (%s).",
        event, GST_EVENT_TYPE_NAME (event));
  }
  if (peer)
    gst_object_unref (peer);

  /* continue on other pads, even if one failed */
  return TRUE;
}

/* forwards the event to all sinkpads, takes ownership of the
 * event
 *
 * Returns: TRUE if the event could be forwarded on all
 * sinkpads.
 */
static gboolean
forward_event (GstAudioMixer * audiomixer, GstEvent * event, gboolean flush)
{
  gboolean ret;
  GstIterator *it;
  GstIteratorResult ires;
  GValue vret = { 0 };
  EventData data;

  GST_LOG_OBJECT (audiomixer, "Forwarding event %p (%s)", event,
      GST_EVENT_TYPE_NAME (event));

  data.event = event;
  data.flush = flush;

  g_value_init (&vret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&vret, FALSE);
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (audiomixer));
  while (TRUE) {
    ires =
        gst_iterator_fold (it, (GstIteratorFoldFunction) forward_event_func,
        &vret, &data);
    switch (ires) {
      case GST_ITERATOR_RESYNC:
        GST_WARNING ("resync");
        gst_iterator_resync (it);
        g_value_set_boolean (&vret, TRUE);
        break;
      case GST_ITERATOR_OK:
      case GST_ITERATOR_DONE:
        ret = g_value_get_boolean (&vret);
        goto done;
      default:
        ret = FALSE;
        goto done;
    }
  }
done:
  gst_iterator_free (it);
  GST_LOG_OBJECT (audiomixer, "Forwarded event %p (%s), ret=%d", event,
      GST_EVENT_TYPE_NAME (event), ret);
  gst_event_unref (event);

  return ret;
}

static gboolean
gst_audiomixer_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstAudioMixer *audiomixer;
  gboolean result;

  audiomixer = GST_AUDIO_MIXER (parent);

  GST_DEBUG_OBJECT (pad, "Got %s event on src pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
      /* TODO: Update from videomixer */
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      gdouble rate;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      GstFormat seek_format, dest_format;
      gboolean flush;

      /* parse the seek parameters */
      gst_event_parse_seek (event, &rate, &seek_format, &flags, &start_type,
          &start, &stop_type, &stop);

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

      dest_format = audiomixer->segment.format;
      if (seek_format != dest_format) {
        result = FALSE;
        GST_DEBUG_OBJECT (audiomixer,
            "seeking failed, unhandled seek format: %d", seek_format);
        goto done;
      }

      flush = (flags & GST_SEEK_FLAG_FLUSH) == GST_SEEK_FLAG_FLUSH;

      /* check if we are flushing */
      if (flush) {
        /* flushing seek, start flush downstream, the flush will be done
         * when all pads received a FLUSH_STOP.
         * Make sure we accept nothing anymore and return WRONG_STATE.
         * We send a flush-start before, to ensure no streaming is done
         * as we need to take the stream lock.
         */
        gst_pad_push_event (audiomixer->srcpad, gst_event_new_flush_start ());
        gst_collect_pads_set_flushing (audiomixer->collect, TRUE);

        /* We can't send FLUSH_STOP here since upstream could start pushing data
         * after we unlock audiomixer->collect.
         * We set flush_stop_pending to TRUE instead and send FLUSH_STOP after
         * forwarding the seek upstream or from gst_audiomixer_collected,
         * whichever happens first.
         */
        GST_COLLECT_PADS_STREAM_LOCK (audiomixer->collect);
        audiomixer->flush_stop_pending = TRUE;
        GST_COLLECT_PADS_STREAM_UNLOCK (audiomixer->collect);
        GST_DEBUG_OBJECT (audiomixer, "mark pending flush stop event");
      }
      GST_DEBUG_OBJECT (audiomixer, "handling seek event: %" GST_PTR_FORMAT,
          event);

      /* now wait for the collected to be finished and mark a new
       * segment. After we have the lock, no collect function is running and no
       * new collect function will be called for as long as we're flushing. */
      GST_COLLECT_PADS_STREAM_LOCK (audiomixer->collect);
      /* clip position and update our segment */
      if (audiomixer->segment.stop != -1) {
        audiomixer->segment.position = audiomixer->segment.stop;
      }
      gst_segment_do_seek (&audiomixer->segment, rate, seek_format, flags,
          start_type, start, stop_type, stop, NULL);

      if (flush) {
        /* Yes, we need to call _set_flushing again *WHEN* the streaming threads
         * have stopped so that the cookie gets properly updated. */
        gst_collect_pads_set_flushing (audiomixer->collect, TRUE);
      }
      GST_COLLECT_PADS_STREAM_UNLOCK (audiomixer->collect);
      GST_DEBUG_OBJECT (audiomixer, "forwarding seek event: %" GST_PTR_FORMAT,
          event);
      GST_DEBUG_OBJECT (audiomixer, "updated segment: %" GST_SEGMENT_FORMAT,
          &audiomixer->segment);

      /* we're forwarding seek to all upstream peers and wait for one to reply
       * with a newsegment-event before we send a newsegment-event downstream */
      g_atomic_int_set (&audiomixer->segment_pending, TRUE);
      result = forward_event (audiomixer, event, flush);
      /* FIXME: We should use the seek segment and forward that downstream next time
       * not any upstream segment event */
      if (!result) {
        /* seek failed. maybe source is a live source. */
        GST_DEBUG_OBJECT (audiomixer, "seeking failed");
      }
      if (g_atomic_int_compare_and_exchange (&audiomixer->flush_stop_pending,
              TRUE, FALSE)) {
        GST_DEBUG_OBJECT (audiomixer, "pending flush stop");
        if (!gst_pad_push_event (audiomixer->srcpad,
                gst_event_new_flush_stop (TRUE))) {
          GST_WARNING_OBJECT (audiomixer, "Sending flush stop event failed");
        }
      }
      break;
    }
    case GST_EVENT_QOS:
      /* QoS might be tricky */
      result = FALSE;
      gst_event_unref (event);
      break;
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      result = FALSE;
      gst_event_unref (event);
      break;
    default:
      /* just forward the rest for now */
      GST_DEBUG_OBJECT (audiomixer, "forward unhandled event: %s",
          GST_EVENT_TYPE_NAME (event));
      result = forward_event (audiomixer, event, FALSE);
      break;
  }

done:

  return result;
}

static gboolean
gst_audiomixer_sink_event (GstCollectPads * pads, GstCollectData * pad,
    GstEvent * event, gpointer user_data)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (user_data);
  GstAudioMixerCollect *adata = (GstAudioMixerCollect *) pad;
  gboolean res = TRUE, discard = FALSE;

  GST_DEBUG_OBJECT (pad->pad, "Got %s event on sink pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_audiomixer_setcaps (audiomixer, pad->pad, caps);
      gst_event_unref (event);
      event = NULL;
      break;
    }
      /* FIXME: Who cares about flushes from upstream? We should
       * not forward them at all */
    case GST_EVENT_FLUSH_START:
      /* ensure that we will send a flush stop */
      GST_COLLECT_PADS_STREAM_LOCK (audiomixer->collect);
      audiomixer->flush_stop_pending = TRUE;
      res = gst_collect_pads_event_default (pads, pad, event, discard);
      event = NULL;
      GST_COLLECT_PADS_STREAM_UNLOCK (audiomixer->collect);
      break;
    case GST_EVENT_FLUSH_STOP:
      /* we received a flush-stop. We will only forward it when
       * flush_stop_pending is set, and we will unset it then.
       */
      g_atomic_int_set (&audiomixer->segment_pending, TRUE);
      GST_COLLECT_PADS_STREAM_LOCK (audiomixer->collect);
      if (audiomixer->flush_stop_pending) {
        GST_DEBUG_OBJECT (pad->pad, "forwarding flush stop");
        res = gst_collect_pads_event_default (pads, pad, event, discard);
        audiomixer->flush_stop_pending = FALSE;
        event = NULL;
        gst_buffer_replace (&audiomixer->current_buffer, NULL);
        audiomixer->discont_time = GST_CLOCK_TIME_NONE;
      } else {
        discard = TRUE;
        GST_DEBUG_OBJECT (pad->pad, "eating flush stop");
      }
      GST_COLLECT_PADS_STREAM_UNLOCK (audiomixer->collect);
      /* Clear pending tags */
      if (audiomixer->pending_events) {
        g_list_foreach (audiomixer->pending_events, (GFunc) gst_event_unref,
            NULL);
        g_list_free (audiomixer->pending_events);
        audiomixer->pending_events = NULL;
      }
      adata->position = adata->size = 0;
      adata->output_offset = adata->next_offset = -1;
      gst_buffer_replace (&adata->buffer, NULL);
      break;
    case GST_EVENT_TAG:
      /* collect tags here so we can push them out when we collect data */
      audiomixer->pending_events =
          g_list_append (audiomixer->pending_events, event);
      event = NULL;
      break;
    case GST_EVENT_SEGMENT:{
      const GstSegment *segment;
      gst_event_parse_segment (event, &segment);
      if (segment->rate != audiomixer->segment.rate) {
        GST_ERROR_OBJECT (pad->pad,
            "Got segment event with wrong rate %lf, expected %lf",
            segment->rate, audiomixer->segment.rate);
        res = FALSE;
        gst_event_unref (event);
        event = NULL;
      } else if (segment->rate < 0.0) {
        GST_ERROR_OBJECT (pad->pad, "Negative rates not supported yet");
        res = FALSE;
        gst_event_unref (event);
        event = NULL;
      }
      discard = TRUE;
      break;
    }
    default:
      break;
  }

  if (G_LIKELY (event))
    return gst_collect_pads_event_default (pads, pad, event, discard);
  else
    return res;
}

static void
gst_audiomixer_class_init (GstAudioMixerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

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
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_audiomixer_change_state);
}

static void
gst_audiomixer_init (GstAudioMixer * audiomixer)
{
  GstPadTemplate *template;

  template = gst_static_pad_template_get (&gst_audiomixer_src_template);
  audiomixer->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);

  gst_pad_set_query_function (audiomixer->srcpad,
      GST_DEBUG_FUNCPTR (gst_audiomixer_src_query));
  gst_pad_set_event_function (audiomixer->srcpad,
      GST_DEBUG_FUNCPTR (gst_audiomixer_src_event));
  GST_PAD_SET_PROXY_CAPS (audiomixer->srcpad);
  gst_element_add_pad (GST_ELEMENT (audiomixer), audiomixer->srcpad);

  audiomixer->current_caps = NULL;
  gst_audio_info_init (&audiomixer->info);
  audiomixer->padcount = 0;

  audiomixer->filter_caps = NULL;
  audiomixer->alignment_threshold = DEFAULT_ALIGNMENT_THRESHOLD;
  audiomixer->discont_wait = DEFAULT_DISCONT_WAIT;
  audiomixer->blocksize = DEFAULT_BLOCKSIZE;

  /* keep track of the sinkpads requested */
  audiomixer->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (audiomixer->collect,
      GST_DEBUG_FUNCPTR (gst_audiomixer_collected), audiomixer);
  gst_collect_pads_set_clip_function (audiomixer->collect,
      GST_DEBUG_FUNCPTR (gst_audiomixer_do_clip), audiomixer);
  gst_collect_pads_set_event_function (audiomixer->collect,
      GST_DEBUG_FUNCPTR (gst_audiomixer_sink_event), audiomixer);
  gst_collect_pads_set_query_function (audiomixer->collect,
      GST_DEBUG_FUNCPTR (gst_audiomixer_sink_query), audiomixer);
}

static void
gst_audiomixer_dispose (GObject * object)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (object);

  if (audiomixer->collect) {
    gst_object_unref (audiomixer->collect);
    audiomixer->collect = NULL;
  }
  gst_caps_replace (&audiomixer->filter_caps, NULL);
  gst_caps_replace (&audiomixer->current_caps, NULL);

  if (audiomixer->pending_events) {
    g_list_foreach (audiomixer->pending_events, (GFunc) gst_event_unref, NULL);
    g_list_free (audiomixer->pending_events);
    audiomixer->pending_events = NULL;
  }

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

static void
free_pad (GstCollectData * data)
{
  GstAudioMixerCollect *adata = (GstAudioMixerCollect *) data;

  gst_buffer_replace (&adata->buffer, NULL);
}

static GstPad *
gst_audiomixer_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused, const GstCaps * caps)
{
  gchar *name;
  GstAudioMixer *audiomixer;
  GstPad *newpad;
  gint padcount;
  GstCollectData *cdata;
  GstAudioMixerCollect *adata;

  if (templ->direction != GST_PAD_SINK)
    goto not_sink;

  audiomixer = GST_AUDIO_MIXER (element);

  /* increment pad counter */
  padcount = g_atomic_int_add (&audiomixer->padcount, 1);

  name = g_strdup_printf ("sink_%u", padcount);
  newpad = g_object_new (GST_TYPE_AUDIO_MIXER_PAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  GST_DEBUG_OBJECT (audiomixer, "request new pad %s", name);
  g_free (name);

  cdata =
      gst_collect_pads_add_pad (audiomixer->collect, newpad,
      sizeof (GstAudioMixerCollect), free_pad, TRUE);
  adata = (GstAudioMixerCollect *) cdata;
  adata->buffer = NULL;
  adata->position = 0;
  adata->size = 0;
  adata->output_offset = -1;
  adata->next_offset = -1;

  /* takes ownership of the pad */
  if (!gst_element_add_pad (GST_ELEMENT (audiomixer), newpad))
    goto could_not_add;

  gst_child_proxy_child_added (GST_CHILD_PROXY (audiomixer), G_OBJECT (newpad),
      GST_OBJECT_NAME (newpad));

  return newpad;

  /* errors */
not_sink:
  {
    g_warning ("gstaudiomixer: request new pad that is not a SINK pad\n");
    return NULL;
  }
could_not_add:
  {
    GST_DEBUG_OBJECT (audiomixer, "could not add pad");
    gst_collect_pads_remove_pad (audiomixer->collect, newpad);
    gst_object_unref (newpad);
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
  if (audiomixer->collect)
    gst_collect_pads_remove_pad (audiomixer->collect, pad);
  gst_element_remove_pad (element, pad);
}

static GstFlowReturn
gst_audiomixer_do_clip (GstCollectPads * pads, GstCollectData * data,
    GstBuffer * buffer, GstBuffer ** out, gpointer user_data)
{
  GstAudioMixer *audiomixer = GST_AUDIO_MIXER (user_data);
  gint rate, bpf;

  rate = GST_AUDIO_INFO_RATE (&audiomixer->info);
  bpf = GST_AUDIO_INFO_BPF (&audiomixer->info);

  buffer = gst_audio_buffer_clip (buffer, &data->segment, rate, bpf);

  *out = buffer;
  return GST_FLOW_OK;
}

static gboolean
gst_audio_mixer_fill_buffer (GstAudioMixer * audiomixer, GstCollectPads * pads,
    GstCollectData * collect_data, GstAudioMixerCollect * adata,
    GstBuffer * inbuf)
{
  GstClockTime start_time, end_time;
  gboolean discont = FALSE;
  guint64 start_offset, end_offset;
  GstClockTime timestamp, stream_time;
  gint rate, bpf;

  g_assert (adata->buffer == NULL);

  rate = GST_AUDIO_INFO_RATE (&audiomixer->info);
  bpf = GST_AUDIO_INFO_BPF (&audiomixer->info);

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  stream_time =
      gst_segment_to_stream_time (&collect_data->segment, GST_FORMAT_TIME,
      timestamp);

  /* sync object properties on stream time */
  /* TODO: Ideally we would want to do that on every sample */
  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (collect_data->pad), stream_time);

  adata->position = 0;
  adata->size = gst_buffer_get_size (inbuf);

  start_time = GST_BUFFER_TIMESTAMP (inbuf);
  end_time =
      start_time + gst_util_uint64_scale_ceil (adata->size / bpf,
      GST_SECOND, rate);

  start_offset = gst_util_uint64_scale (start_time, rate, GST_SECOND);
  end_offset = start_offset + adata->size / bpf;

  if (GST_BUFFER_IS_DISCONT (inbuf)
      || GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_RESYNC)
      || adata->next_offset == -1) {
    discont = TRUE;
  } else {
    guint64 diff, max_sample_diff;

    /* Check discont, based on audiobasesink */
    if (start_offset <= adata->next_offset)
      diff = adata->next_offset - start_offset;
    else
      diff = start_offset - adata->next_offset;

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
    if (adata->next_offset != -1)
      GST_INFO_OBJECT (collect_data->pad, "Have discont. Expected %"
          G_GUINT64_FORMAT ", got %" G_GUINT64_FORMAT,
          adata->next_offset, start_offset);
    adata->output_offset = -1;
  } else {
    audiomixer->discont_time = GST_CLOCK_TIME_NONE;
  }

  adata->next_offset = end_offset;

  if (adata->output_offset == -1) {
    GstClockTime start_running_time;
    GstClockTime end_running_time;
    guint64 start_running_time_offset;
    guint64 end_running_time_offset;

    start_running_time =
        gst_segment_to_running_time (&collect_data->segment,
        GST_FORMAT_TIME, start_time);
    end_running_time =
        gst_segment_to_running_time (&collect_data->segment,
        GST_FORMAT_TIME, end_time);
    start_running_time_offset =
        gst_util_uint64_scale (start_running_time, rate, GST_SECOND);
    end_running_time_offset =
        gst_util_uint64_scale (end_running_time, rate, GST_SECOND);

    if (end_running_time_offset < audiomixer->offset) {
      /* Before output segment, drop */
      gst_buffer_unref (inbuf);
      adata->buffer = NULL;
      gst_buffer_unref (gst_collect_pads_pop (pads, collect_data));
      adata->position = 0;
      adata->size = 0;
      adata->output_offset = -1;
      GST_DEBUG_OBJECT (collect_data->pad,
          "Buffer before segment or current position: %" G_GUINT64_FORMAT " < %"
          G_GUINT64_FORMAT, end_running_time_offset, audiomixer->offset);
      return FALSE;
    }

    if (start_running_time_offset < audiomixer->offset) {
      guint diff = (audiomixer->offset - start_running_time_offset) * bpf;
      adata->position += diff;
      adata->size -= diff;
      /* FIXME: This could only happen due to rounding errors */
      if (adata->size == 0) {
        /* Empty buffer, drop */
        gst_buffer_unref (inbuf);
        adata->buffer = NULL;
        gst_buffer_unref (gst_collect_pads_pop (pads, collect_data));
        adata->position = 0;
        adata->size = 0;
        adata->output_offset = -1;
        GST_DEBUG_OBJECT (collect_data->pad,
            "Buffer before segment or current position: %" G_GUINT64_FORMAT
            " < %" G_GUINT64_FORMAT, end_running_time_offset,
            audiomixer->offset);
        return FALSE;
      }
    }

    adata->output_offset = MAX (start_running_time_offset, audiomixer->offset);
    GST_DEBUG_OBJECT (collect_data->pad,
        "Buffer resynced: Pad offset %" G_GUINT64_FORMAT
        ", current mixer offset %" G_GUINT64_FORMAT, adata->output_offset,
        audiomixer->offset);
  }

  GST_LOG_OBJECT (collect_data->pad,
      "Queued new buffer at offset %" G_GUINT64_FORMAT, adata->output_offset);
  adata->buffer = inbuf;

  return TRUE;
}

static void
gst_audio_mixer_mix_buffer (GstAudioMixer * audiomixer, GstCollectPads * pads,
    GstCollectData * collect_data, GstAudioMixerCollect * adata,
    GstMapInfo * outmap)
{
  GstAudioMixerPad *pad = GST_AUDIO_MIXER_PAD (adata->collect.pad);
  guint overlap;
  guint out_start;
  GstBuffer *inbuf;
  GstMapInfo inmap;
  gint bpf;

  bpf = GST_AUDIO_INFO_BPF (&audiomixer->info);

  /* Overlap => mix */
  if (audiomixer->offset < adata->output_offset)
    out_start = adata->output_offset - audiomixer->offset;
  else
    out_start = 0;

  overlap = adata->size / bpf - adata->position / bpf;
  if (overlap > audiomixer->blocksize - out_start)
    overlap = audiomixer->blocksize - out_start;

  inbuf = gst_collect_pads_peek (pads, collect_data);
  g_assert (inbuf != NULL && inbuf == adata->buffer);

  GST_OBJECT_LOCK (pad);
  if (pad->mute || pad->volume < G_MINDOUBLE) {
    GST_DEBUG_OBJECT (pad, "Skipping muted pad");
    gst_buffer_unref (inbuf);
    adata->position += overlap * bpf;
    adata->output_offset += overlap;
    if (adata->position >= adata->size) {
      /* Buffer done, drop it */
      gst_buffer_replace (&adata->buffer, NULL);
      gst_buffer_unref (gst_collect_pads_pop (pads, collect_data));
    }
    GST_OBJECT_UNLOCK (pad);
    return;
  }

  if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
    /* skip gap buffer */
    GST_LOG_OBJECT (pad, "skipping GAP buffer");
    gst_buffer_unref (inbuf);
    adata->output_offset += adata->size / bpf;
    /* Buffer done, drop it */
    gst_buffer_replace (&adata->buffer, NULL);
    gst_buffer_unref (gst_collect_pads_pop (pads, collect_data));
    GST_OBJECT_UNLOCK (pad);
    return;
  }

  gst_buffer_map (inbuf, &inmap, GST_MAP_READ);
  GST_LOG_OBJECT (pad, "mixing %u bytes at offset %u from offset %u",
      overlap * bpf, out_start * bpf, adata->position);
  /* further buffers, need to add them */
  if (pad->volume == 1.0) {
    switch (audiomixer->info.finfo->format) {
      case GST_AUDIO_FORMAT_U8:
        audiomixer_orc_add_u8 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + adata->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S8:
        audiomixer_orc_add_s8 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + adata->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_U16:
        audiomixer_orc_add_u16 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + adata->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S16:
        audiomixer_orc_add_s16 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + adata->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_U32:
        audiomixer_orc_add_u32 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + adata->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S32:
        audiomixer_orc_add_s32 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + adata->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_F32:
        audiomixer_orc_add_f32 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + adata->position),
            overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_F64:
        audiomixer_orc_add_f64 ((gpointer) (outmap->data + out_start * bpf),
            (gpointer) (inmap.data + adata->position),
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
                out_start * bpf), (gpointer) (inmap.data + adata->position),
            pad->volume_i8, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S8:
        audiomixer_orc_add_volume_s8 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + adata->position),
            pad->volume_i8, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_U16:
        audiomixer_orc_add_volume_u16 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + adata->position),
            pad->volume_i16, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S16:
        audiomixer_orc_add_volume_s16 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + adata->position),
            pad->volume_i16, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_U32:
        audiomixer_orc_add_volume_u32 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + adata->position),
            pad->volume_i32, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_S32:
        audiomixer_orc_add_volume_s32 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + adata->position),
            pad->volume_i32, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_F32:
        audiomixer_orc_add_volume_f32 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + adata->position),
            pad->volume, overlap * audiomixer->info.channels);
        break;
      case GST_AUDIO_FORMAT_F64:
        audiomixer_orc_add_volume_f64 ((gpointer) (outmap->data +
                out_start * bpf), (gpointer) (inmap.data + adata->position),
            pad->volume, overlap * audiomixer->info.channels);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }
  gst_buffer_unmap (inbuf, &inmap);
  gst_buffer_unref (inbuf);

  adata->position += overlap * bpf;
  adata->output_offset += overlap;

  if (adata->position == adata->size) {
    /* Buffer done, drop it */
    gst_buffer_replace (&adata->buffer, NULL);
    gst_buffer_unref (gst_collect_pads_pop (pads, collect_data));
    GST_DEBUG_OBJECT (pad, "Finished mixing buffer, waiting for next");
  }

  GST_OBJECT_UNLOCK (pad);
}

static GstFlowReturn
gst_audiomixer_collected (GstCollectPads * pads, gpointer user_data)
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
  GSList *collected;
  GstFlowReturn ret;
  GstBuffer *outbuf = NULL;
  GstMapInfo outmap;
  gint64 next_offset;
  gint64 next_timestamp;
  gint rate, bpf;
  gboolean dropped = FALSE;
  gboolean is_eos = TRUE;
  gboolean is_done = TRUE;

  audiomixer = GST_AUDIO_MIXER (user_data);

  /* this is fatal */
  if (G_UNLIKELY (audiomixer->info.finfo->format == GST_AUDIO_FORMAT_UNKNOWN))
    goto not_negotiated;

  if (audiomixer->flush_stop_pending == TRUE) {
    GST_INFO_OBJECT (audiomixer->srcpad, "send pending flush stop event");
    if (!gst_pad_push_event (audiomixer->srcpad,
            gst_event_new_flush_stop (TRUE))) {
      GST_WARNING_OBJECT (audiomixer->srcpad,
          "Sending flush stop event failed");
    }

    audiomixer->flush_stop_pending = FALSE;
    gst_buffer_replace (&audiomixer->current_buffer, NULL);
    audiomixer->discont_time = GST_CLOCK_TIME_NONE;
  }

  if (audiomixer->send_stream_start) {
    gchar s_id[32];
    GstEvent *event;

    GST_INFO_OBJECT (audiomixer->srcpad, "send pending stream start event");
    /* FIXME: create id based on input ids, we can't use 
     * gst_pad_create_stream_id() though as that only handles 0..1 sink-pad
     */
    g_snprintf (s_id, sizeof (s_id), "audiomixer-%08x", g_random_int ());
    event = gst_event_new_stream_start (s_id);
    gst_event_set_group_id (event, gst_util_group_id_next ());

    if (!gst_pad_push_event (audiomixer->srcpad, event)) {
      GST_WARNING_OBJECT (audiomixer->srcpad,
          "Sending stream start event failed");
    }
    audiomixer->send_stream_start = FALSE;
  }

  if (audiomixer->send_caps) {
    GstEvent *caps_event;

    caps_event = gst_event_new_caps (audiomixer->current_caps);
    GST_INFO_OBJECT (audiomixer->srcpad,
        "send pending caps event %" GST_PTR_FORMAT, caps_event);
    if (!gst_pad_push_event (audiomixer->srcpad, caps_event)) {
      GST_WARNING_OBJECT (audiomixer->srcpad, "Sending caps event failed");
    }
    audiomixer->send_caps = FALSE;
  }

  rate = GST_AUDIO_INFO_RATE (&audiomixer->info);
  bpf = GST_AUDIO_INFO_BPF (&audiomixer->info);

  if (g_atomic_int_compare_and_exchange (&audiomixer->segment_pending, TRUE,
          FALSE)) {
    GstEvent *event;

    /* 
     * When seeking we set the start and stop positions as given in the seek
     * event. We also adjust offset & timestamp accordingly.
     * This basically ignores all newsegments sent by upstream.
     *
     * FIXME: We require that all inputs have the same rate currently
     * as we do no rate conversion!
     */
    event = gst_event_new_segment (&audiomixer->segment);
    if (audiomixer->segment.rate > 0.0) {
      audiomixer->segment.position = audiomixer->segment.start;
    } else {
      audiomixer->segment.position = audiomixer->segment.stop;
    }
    audiomixer->offset = gst_util_uint64_scale (audiomixer->segment.position,
        rate, GST_SECOND);

    GST_INFO_OBJECT (audiomixer->srcpad, "sending pending new segment event %"
        GST_SEGMENT_FORMAT, &audiomixer->segment);
    if (event) {
      if (!gst_pad_push_event (audiomixer->srcpad, event)) {
        GST_WARNING_OBJECT (audiomixer->srcpad,
            "Sending new segment event failed");
      }
    } else {
      GST_WARNING_OBJECT (audiomixer->srcpad, "Creating new segment event for "
          "start:%" G_GINT64_FORMAT "  end:%" G_GINT64_FORMAT " failed",
          audiomixer->segment.start, audiomixer->segment.stop);
    }
  }

  if (G_UNLIKELY (audiomixer->pending_events)) {
    GList *tmp = audiomixer->pending_events;

    while (tmp) {
      GstEvent *ev = (GstEvent *) tmp->data;

      gst_pad_push_event (audiomixer->srcpad, ev);
      tmp = g_list_next (tmp);
    }
    g_list_free (audiomixer->pending_events);
    audiomixer->pending_events = NULL;
  }

  /* for the next timestamp, use the sample counter, which will
   * never accumulate rounding errors */

  /* FIXME: Reverse mixing does not work at all yet */
  if (audiomixer->segment.rate > 0.0) {
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

  GST_LOG_OBJECT (audiomixer,
      "Starting to mix %u samples for offset %" G_GUINT64_FORMAT
      " with timestamp %" GST_TIME_FORMAT, audiomixer->blocksize,
      audiomixer->offset, GST_TIME_ARGS (audiomixer->segment.position));

  gst_buffer_map (outbuf, &outmap, GST_MAP_READWRITE);

  for (collected = pads->data; collected; collected = collected->next) {
    GstCollectData *collect_data;
    GstAudioMixerCollect *adata;
    GstBuffer *inbuf;

    collect_data = (GstCollectData *) collected->data;
    adata = (GstAudioMixerCollect *) collect_data;

    inbuf = gst_collect_pads_peek (pads, collect_data);
    if (!inbuf)
      continue;

    /* New buffer? */
    if (!adata->buffer || adata->buffer != inbuf) {
      /* Takes ownership of buffer */
      if (!gst_audio_mixer_fill_buffer (audiomixer, pads, collect_data, adata,
              inbuf)) {
        dropped = TRUE;
        continue;
      }
    } else {
      gst_buffer_unref (inbuf);
    }

    if (!adata->buffer && !dropped
        && GST_COLLECT_PADS_STATE_IS_SET (&adata->collect,
            GST_COLLECT_PADS_STATE_EOS)) {
      GST_DEBUG_OBJECT (collect_data->pad, "Pad is in EOS state");
    } else {
      is_eos = FALSE;
    }

    /* At this point adata->output_offset >= audiomixer->offset or we have no buffer anymore */
    if (adata->output_offset >= audiomixer->offset
        && adata->output_offset <
        audiomixer->offset + audiomixer->blocksize && adata->buffer) {
      GST_LOG_OBJECT (collect_data->pad, "Mixing buffer for current offset");
      gst_audio_mixer_mix_buffer (audiomixer, pads, collect_data, adata,
          &outmap);
      if (adata->output_offset >= next_offset) {
        GST_DEBUG_OBJECT (collect_data->pad,
            "Pad is after current offset: %" G_GUINT64_FORMAT " >= %"
            G_GUINT64_FORMAT, adata->output_offset, next_offset);
      } else {
        is_done = FALSE;
      }
    }
  }

  gst_buffer_unmap (outbuf, &outmap);

  if (dropped) {
    /* We dropped a buffer, retry */
    GST_DEBUG_OBJECT (audiomixer,
        "A pad dropped a buffer, wait for the next one");
    return GST_FLOW_OK;
  }

  if (!is_done && !is_eos) {
    /* Get more buffers */
    GST_DEBUG_OBJECT (audiomixer,
        "We're not done yet for the current offset," " waiting for more data");
    return GST_FLOW_OK;
  }

  if (is_eos) {
    gint64 max_offset = 0;
    gboolean empty_buffer = TRUE;

    GST_DEBUG_OBJECT (audiomixer, "We're EOS");


    for (collected = pads->data; collected; collected = collected->next) {
      GstCollectData *collect_data;
      GstAudioMixerCollect *adata;

      collect_data = (GstCollectData *) collected->data;
      adata = (GstAudioMixerCollect *) collect_data;

      max_offset = MAX (max_offset, adata->output_offset);
      if (adata->output_offset > audiomixer->offset)
        empty_buffer = FALSE;
    }

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
  if (audiomixer->segment.rate > 0.0) {
    GST_BUFFER_TIMESTAMP (outbuf) = audiomixer->segment.position;
    GST_BUFFER_OFFSET (outbuf) = audiomixer->offset;
    GST_BUFFER_OFFSET_END (outbuf) = next_offset;
    GST_BUFFER_DURATION (outbuf) =
        next_timestamp - audiomixer->segment.position;
  } else {
    GST_BUFFER_TIMESTAMP (outbuf) = next_timestamp;
    GST_BUFFER_OFFSET (outbuf) = next_offset;
    GST_BUFFER_OFFSET_END (outbuf) = audiomixer->offset;
    GST_BUFFER_DURATION (outbuf) =
        audiomixer->segment.position - next_timestamp;
  }

  audiomixer->offset = next_offset;
  audiomixer->segment.position = next_timestamp;

  /* send it out */
  GST_LOG_OBJECT (audiomixer,
      "pushing outbuf %p, timestamp %" GST_TIME_FORMAT " offset %"
      G_GINT64_FORMAT, outbuf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_BUFFER_OFFSET (outbuf));

  ret = gst_pad_push (audiomixer->srcpad, outbuf);
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
    gst_pad_push_event (audiomixer->srcpad, gst_event_new_eos ());
    return GST_FLOW_EOS;
  }
}

static GstStateChangeReturn
gst_audiomixer_change_state (GstElement * element, GstStateChange transition)
{
  GstAudioMixer *audiomixer;
  GstStateChangeReturn ret;

  audiomixer = GST_AUDIO_MIXER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      audiomixer->offset = 0;
      audiomixer->flush_stop_pending = FALSE;
      audiomixer->segment_pending = TRUE;
      audiomixer->send_stream_start = TRUE;
      audiomixer->send_caps = TRUE;
      gst_caps_replace (&audiomixer->current_caps, NULL);
      gst_segment_init (&audiomixer->segment, GST_FORMAT_TIME);
      gst_collect_pads_start (audiomixer->collect);
      audiomixer->discont_time = GST_CLOCK_TIME_NONE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* need to unblock the collectpads before calling the
       * parent change_state so that streaming can finish */
      gst_collect_pads_stop (audiomixer->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_buffer_replace (&audiomixer->current_buffer, NULL);
      break;
    default:
      break;
  }

  return ret;
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
