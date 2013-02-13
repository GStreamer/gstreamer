/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2001 Thomas <thomas@apestaart.org>
 *               2005,2006 Wim Taymans <wim@fluendo.com>
 *
 * adder.c: Adder element, N in, one out, samples are added
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
 * SECTION:element-adder
 *
 * The adder allows to mix several streams into one by adding the data.
 * Mixed data is clamped to the min/max values of the data format.
 *
 * The adder currently mixes all data received on the sinkpads as soon as
 * possible without trying to synchronize the streams.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch audiotestsrc freq=100 ! adder name=mix ! audioconvert ! alsasink audiotestsrc freq=500 ! mix.
 * ]| This pipeline produces two sine waves mixed together.
 * </refsect2>
 *
 * Last reviewed on 2006-05-09 (0.10.7)
 */
/* Element-Checklist-Version: 5 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstadder.h"
#include <gst/audio/audio.h>
#include <string.h>             /* strcmp */
#include "gstadderorc.h"

/* highest positive/lowest negative x-bit value we can use for clamping */
#define MAX_INT_32  ((gint32) (0x7fffffff))
#define MAX_INT_16  ((gint16) (0x7fff))
#define MAX_INT_8   ((gint8)  (0x7f))
#define MAX_UINT_32 ((guint32)(0xffffffff))
#define MAX_UINT_16 ((guint16)(0xffff))
#define MAX_UINT_8  ((guint8) (0xff))

#define MIN_INT_32  ((gint32) (0x80000000))
#define MIN_INT_16  ((gint16) (0x8000))
#define MIN_INT_8   ((gint8)  (0x80))
#define MIN_UINT_32 ((guint32)(0x00000000))
#define MIN_UINT_16 ((guint16)(0x0000))
#define MIN_UINT_8  ((guint8) (0x00))

enum
{
  PROP_0,
  PROP_FILTER_CAPS
};

#define GST_CAT_DEFAULT gst_adder_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

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

static GstStaticPadTemplate gst_adder_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS)
    );

static GstStaticPadTemplate gst_adder_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (CAPS)
    );

#define gst_adder_parent_class parent_class
G_DEFINE_TYPE (GstAdder, gst_adder, GST_TYPE_ELEMENT);

static void gst_adder_dispose (GObject * object);
static void gst_adder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_adder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_adder_setcaps (GstAdder * adder, GstPad * pad,
    GstCaps * caps);
static gboolean gst_adder_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_adder_sink_query (GstCollectPads * pads,
    GstCollectData * pad, GstQuery * query, gpointer user_data);
static gboolean gst_adder_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_adder_sink_event (GstCollectPads * pads,
    GstCollectData * pad, GstEvent * event, gpointer user_data);

static GstPad *gst_adder_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused, const GstCaps * caps);
static void gst_adder_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_adder_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_adder_do_clip (GstCollectPads * pads,
    GstCollectData * data, GstBuffer * buffer, GstBuffer ** out,
    gpointer user_data);
static GstFlowReturn gst_adder_collected (GstCollectPads * pads,
    gpointer user_data);

/* non-clipping versions (for float) */
#define MAKE_FUNC_NC(name,type)                                 \
static void name (type *out, type *in, gint samples) {          \
  gint i;                                                       \
  for (i = 0; i < samples; i++)                                 \
    out[i] += in[i];                                            \
}

/* *INDENT-OFF* */
MAKE_FUNC_NC (adder_orc_add_float64, gdouble)
/* *INDENT-ON* */

/* we can only accept caps that we and downstream can handle.
 * if we have filtercaps set, use those to constrain the target caps.
 */
static GstCaps *
gst_adder_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstAdder *adder;
  GstCaps *result, *peercaps, *sinkcaps, *filter_caps;

  adder = GST_ADDER (GST_PAD_PARENT (pad));

  GST_OBJECT_LOCK (adder);
  /* take filter */
  if ((filter_caps = adder->filter_caps)) {
    if (filter)
      filter_caps =
          gst_caps_intersect_full (filter, filter_caps,
          GST_CAPS_INTERSECT_FIRST);
    else
      gst_caps_ref (filter_caps);
  } else {
    filter_caps = filter ? gst_caps_ref (filter) : NULL;
  }
  GST_OBJECT_UNLOCK (adder);

  if (filter_caps && gst_caps_is_empty (filter_caps)) {
    GST_WARNING_OBJECT (pad, "Empty filter caps");
    return filter_caps;
  }

  /* get the downstream possible caps */
  peercaps = gst_pad_peer_query_caps (adder->srcpad, filter_caps);

  /* get the allowed caps on this sinkpad */
  sinkcaps = gst_pad_get_current_caps (pad);
  if (sinkcaps == NULL) {
    sinkcaps = gst_pad_get_pad_template_caps (pad);
    if (!sinkcaps)
      sinkcaps = gst_caps_new_any ();
  }

  if (peercaps) {
    /* if the peer has caps, intersect */
    GST_DEBUG_OBJECT (adder, "intersecting peer and template caps");
    result =
        gst_caps_intersect_full (peercaps, sinkcaps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
    gst_caps_unref (sinkcaps);
  } else {
    /* the peer has no caps (or there is no peer), just use the allowed caps
     * of this sinkpad. */
    /* restrict with filter-caps if any */
    if (filter_caps) {
      GST_DEBUG_OBJECT (adder, "no peer caps, using filtered sinkcaps");
      result =
          gst_caps_intersect_full (filter_caps, sinkcaps,
          GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (sinkcaps);
    } else {
      GST_DEBUG_OBJECT (adder, "no peer caps, using sinkcaps");
      result = sinkcaps;
    }
  }

  if (filter_caps)
    gst_caps_unref (filter_caps);

  GST_LOG_OBJECT (adder, "getting caps on pad %p,%s to %" GST_PTR_FORMAT, pad,
      GST_PAD_NAME (pad), result);

  return result;
}

static gboolean
gst_adder_sink_query (GstCollectPads * pads, GstCollectData * pad,
    GstQuery * query, gpointer user_data)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_adder_sink_getcaps (pad->pad, filter);
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

typedef struct
{
  GstPad *pad;
  GstCaps *caps;
} IterData;

static void
setcapsfunc (const GValue * item, IterData * data)
{
  GstPad *otherpad = g_value_get_object (item);

  if (otherpad != data->pad) {
    GST_LOG_OBJECT (data->pad, "calling set_caps with %" GST_PTR_FORMAT,
        data->caps);
    gst_pad_set_caps (data->pad, data->caps);
    gst_pad_use_fixed_caps (data->pad);
  }
}

/* the first caps we receive on any of the sinkpads will define the caps for all
 * the other sinkpads because we can only mix streams with the same caps.
 */
static gboolean
gst_adder_setcaps (GstAdder * adder, GstPad * pad, GstCaps * caps)
{
  GstIterator *it;
  GstIteratorResult ires;
  IterData idata;
  gboolean done;

  /* this gets called recursively due to gst_iterator_foreach calling
   * gst_pad_set_caps() */
  if (adder->in_setcaps)
    return TRUE;

  /* don't allow reconfiguration for now; there's still a race between the
   * different upstream threads doing query_caps + accept_caps + sending
   * (possibly different) CAPS events, but there's not much we can do about
   * that, upstream needs to deal with it. */
  if (adder->current_caps != NULL) {
    /* FIXME: not quite right for optional fields such as channel-mask, which
     * may or may not be present for mono/stereo */
    if (gst_caps_is_equal (caps, adder->current_caps)) {
      return TRUE;
    } else {
      GST_DEBUG_OBJECT (pad, "got input caps %" GST_PTR_FORMAT ", but "
          "current caps are %" GST_PTR_FORMAT, caps, adder->current_caps);
      gst_pad_push_event (pad, gst_event_new_reconfigure ());
      return FALSE;
    }
  }

  GST_INFO_OBJECT (pad, "setting caps to %" GST_PTR_FORMAT, caps);

  adder->current_caps = gst_caps_ref (caps);
  /* send caps event later, after stream-start event */

  it = gst_element_iterate_pads (GST_ELEMENT_CAST (adder));

  /* FIXME, see if the other pads can accept the format. Also lock the
   * format on the other pads to this new format. */
  idata.caps = caps;
  idata.pad = pad;

  adder->in_setcaps = TRUE;
  done = FALSE;
  while (!done) {
    ires = gst_iterator_foreach (it, (GstIteratorForeachFunction) setcapsfunc,
        &idata);

    switch (ires) {
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      default:
        done = TRUE;
        break;
    }
  }
  adder->in_setcaps = FALSE;
  gst_iterator_free (it);

  GST_INFO_OBJECT (pad, "handle caps change to %" GST_PTR_FORMAT, caps);

  if (!gst_audio_info_from_caps (&adder->info, caps))
    goto invalid_format;

  switch (GST_AUDIO_INFO_FORMAT (&adder->info)) {
    case GST_AUDIO_FORMAT_S8:
      adder->func = (GstAdderFunction) adder_orc_add_int8;
      break;
    case GST_AUDIO_FORMAT_U8:
      adder->func = (GstAdderFunction) adder_orc_add_uint8;
      break;
    case GST_AUDIO_FORMAT_S16:
      adder->func = (GstAdderFunction) adder_orc_add_int16;
      break;
    case GST_AUDIO_FORMAT_U16:
      adder->func = (GstAdderFunction) adder_orc_add_uint16;
      break;
    case GST_AUDIO_FORMAT_S32:
      adder->func = (GstAdderFunction) adder_orc_add_int32;
      break;
    case GST_AUDIO_FORMAT_U32:
      adder->func = (GstAdderFunction) adder_orc_add_uint32;
      break;
    case GST_AUDIO_FORMAT_F32:
      adder->func = (GstAdderFunction) adder_orc_add_float32;
      break;
    case GST_AUDIO_FORMAT_F64:
      adder->func = (GstAdderFunction) adder_orc_add_float64;
      break;
    default:
      goto invalid_format;
  }
  return TRUE;

  /* ERRORS */
invalid_format:
  {
    GST_WARNING_OBJECT (adder, "invalid format set as caps");
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
gst_adder_query_duration (GstAdder * adder, GstQuery * query)
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

  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (adder));
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
    GST_DEBUG_OBJECT (adder, "Total duration in format %s: %"
        GST_TIME_FORMAT, gst_format_get_name (format), GST_TIME_ARGS (max));
    gst_query_set_duration (query, format, max);
  }

  return res;
}

static gboolean
gst_adder_query_latency (GstAdder * adder, GstQuery * query)
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
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (adder));
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
    GST_DEBUG_OBJECT (adder, "Calculated total latency: live %s, min %"
        GST_TIME_FORMAT ", max %" GST_TIME_FORMAT,
        (live ? "yes" : "no"), GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    gst_query_set_latency (query, live, min, max);
  }

  return res;
}

static gboolean
gst_adder_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstAdder *adder = GST_ADDER (parent);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          /* FIXME, bring to stream time, might be tricky */
          gst_query_set_position (query, format, adder->segment.position);
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, format, adder->offset);
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_adder_query_duration (adder, query);
      break;
    case GST_QUERY_LATENCY:
      res = gst_adder_query_latency (adder, query);
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
forward_event (GstAdder * adder, GstEvent * event, gboolean flush)
{
  gboolean ret;
  GstIterator *it;
  GstIteratorResult ires;
  GValue vret = { 0 };
  EventData data;

  GST_LOG_OBJECT (adder, "Forwarding event %p (%s)", event,
      GST_EVENT_TYPE_NAME (event));

  data.event = event;
  data.flush = flush;

  g_value_init (&vret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&vret, FALSE);
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (adder));
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
  GST_LOG_OBJECT (adder, "Forwarded event %p (%s), ret=%d", event,
      GST_EVENT_TYPE_NAME (event), ret);
  gst_event_unref (event);

  return ret;
}

static gboolean
gst_adder_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstAdder *adder;
  gboolean result;

  adder = GST_ADDER (parent);

  GST_DEBUG_OBJECT (pad, "Got %s event on src pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      gdouble rate;
      GstSeekType curtype, endtype;
      gint64 cur, end;
      gboolean flush;

      /* parse the seek parameters */
      gst_event_parse_seek (event, &rate, NULL, &flags, &curtype,
          &cur, &endtype, &end);

      if ((curtype != GST_SEEK_TYPE_NONE) && (curtype != GST_SEEK_TYPE_SET)) {
        result = FALSE;
        GST_DEBUG_OBJECT (adder,
            "seeking failed, unhandled seek type for start: %d", curtype);
        goto done;
      }
      if ((endtype != GST_SEEK_TYPE_NONE) && (endtype != GST_SEEK_TYPE_SET)) {
        result = FALSE;
        GST_DEBUG_OBJECT (adder,
            "seeking failed, unhandled seek type for end: %d", endtype);
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
        gst_pad_push_event (adder->srcpad, gst_event_new_flush_start ());
        gst_collect_pads_set_flushing (adder->collect, TRUE);

        /* We can't send FLUSH_STOP here since upstream could start pushing data
         * after we unlock adder->collect.
         * We set flush_stop_pending to TRUE instead and send FLUSH_STOP after
         * forwarding the seek upstream or from gst_adder_collected,
         * whichever happens first.
         */
        g_atomic_int_set (&adder->flush_stop_pending, TRUE);
      }
      GST_DEBUG_OBJECT (adder, "handling seek event: %" GST_PTR_FORMAT, event);

      /* now wait for the collected to be finished and mark a new
       * segment. After we have the lock, no collect function is running and no
       * new collect function will be called for as long as we're flushing. */
      GST_COLLECT_PADS_STREAM_LOCK (adder->collect);
      adder->segment.rate = rate;
      if (curtype == GST_SEEK_TYPE_SET)
        adder->segment.start = cur;
      else
        adder->segment.start = 0;
      if (endtype == GST_SEEK_TYPE_SET)
        adder->segment.stop = end;
      else
        adder->segment.stop = GST_CLOCK_TIME_NONE;
      if (flush) {
        /* Yes, we need to call _set_flushing again *WHEN* the streaming threads
         * have stopped so that the cookie gets properly updated. */
        gst_collect_pads_set_flushing (adder->collect, TRUE);
      }
      GST_COLLECT_PADS_STREAM_UNLOCK (adder->collect);
      GST_DEBUG_OBJECT (adder, "forwarding seek event: %" GST_PTR_FORMAT,
          event);

      /* we're forwarding seek to all upstream peers and wait for one to reply
       * with a newsegment-event before we send a newsegment-event downstream */
      g_atomic_int_set (&adder->wait_for_new_segment, TRUE);
      result = forward_event (adder, event, flush);
      if (!result) {
        /* seek failed. maybe source is a live source. */
        GST_DEBUG_OBJECT (adder, "seeking failed");
      }
      if (g_atomic_int_compare_and_exchange (&adder->flush_stop_pending,
              TRUE, FALSE)) {
        GST_DEBUG_OBJECT (adder, "pending flush stop");
        gst_pad_push_event (adder->srcpad, gst_event_new_flush_stop (TRUE));
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
      GST_DEBUG_OBJECT (adder, "forward unhandled event: %s",
          GST_EVENT_TYPE_NAME (event));
      result = forward_event (adder, event, FALSE);
      break;
  }

done:

  return result;
}

static gboolean
gst_adder_sink_event (GstCollectPads * pads, GstCollectData * pad,
    GstEvent * event, gpointer user_data)
{
  GstAdder *adder = GST_ADDER (user_data);
  gboolean res = TRUE, discard = FALSE;

  GST_DEBUG_OBJECT (pad->pad, "Got %s event on sink pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_adder_setcaps (adder, pad->pad, caps);
      gst_event_unref (event);
      event = NULL;
    }
    case GST_EVENT_FLUSH_STOP:
      /* we received a flush-stop. We will only forward it when
       * flush_stop_pending is set, and we will unset it then.
       */
      if (g_atomic_int_compare_and_exchange (&adder->flush_stop_pending,
              TRUE, FALSE)) {
        g_atomic_int_set (&adder->new_segment_pending, TRUE);
        GST_DEBUG_OBJECT (pad->pad, "forwarding flush stop");
      } else {
        discard = TRUE;
        GST_DEBUG_OBJECT (pad->pad, "eating flush stop");
      }
      /* Clear pending tags */
      if (adder->pending_events) {
        g_list_foreach (adder->pending_events, (GFunc) gst_event_unref, NULL);
        g_list_free (adder->pending_events);
        adder->pending_events = NULL;
      }
      break;
    case GST_EVENT_TAG:
      /* collect tags here so we can push them out when we collect data */
      adder->pending_events = g_list_append (adder->pending_events, event);
      event = NULL;
      break;
    case GST_EVENT_SEGMENT:
      if (g_atomic_int_compare_and_exchange (&adder->wait_for_new_segment,
              TRUE, FALSE)) {
        /* make sure we push a new segment, to inform about new basetime
         * see FIXME in gst_adder_collected() */
        g_atomic_int_set (&adder->new_segment_pending, TRUE);
      }
      discard = TRUE;
      break;
    default:
      break;
  }

  if (G_LIKELY (event))
    return gst_collect_pads_event_default (pads, pad, event, discard);
  else
    return res;
}

static void
gst_adder_class_init (GstAdderClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_adder_set_property;
  gobject_class->get_property = gst_adder_get_property;
  gobject_class->dispose = gst_adder_dispose;

  g_object_class_install_property (gobject_class, PROP_FILTER_CAPS,
      g_param_spec_boxed ("caps", "Target caps",
          "Set target format for mixing (NULL means ANY). "
          "Setting this property takes a reference to the supplied GstCaps "
          "object.", GST_TYPE_CAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_adder_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_adder_sink_template));
  gst_element_class_set_static_metadata (gstelement_class, "Adder",
      "Generic/Audio",
      "Add N audio channels together",
      "Thomas Vander Stichele <thomas at apestaart dot org>");

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_adder_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_adder_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_adder_change_state);
}

static void
gst_adder_init (GstAdder * adder)
{
  GstPadTemplate *template;

  template = gst_static_pad_template_get (&gst_adder_src_template);
  adder->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);

  gst_pad_set_query_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_adder_src_query));
  gst_pad_set_event_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_adder_src_event));
  GST_PAD_SET_PROXY_CAPS (adder->srcpad);
  gst_element_add_pad (GST_ELEMENT (adder), adder->srcpad);

  adder->current_caps = NULL;
  gst_audio_info_init (&adder->info);
  adder->padcount = 0;
  adder->func = NULL;

  adder->filter_caps = NULL;

  /* keep track of the sinkpads requested */
  adder->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (adder->collect,
      GST_DEBUG_FUNCPTR (gst_adder_collected), adder);
  gst_collect_pads_set_clip_function (adder->collect,
      GST_DEBUG_FUNCPTR (gst_adder_do_clip), adder);
  gst_collect_pads_set_event_function (adder->collect,
      GST_DEBUG_FUNCPTR (gst_adder_sink_event), adder);
  gst_collect_pads_set_query_function (adder->collect,
      GST_DEBUG_FUNCPTR (gst_adder_sink_query), adder);
}

static void
gst_adder_dispose (GObject * object)
{
  GstAdder *adder = GST_ADDER (object);

  if (adder->collect) {
    gst_object_unref (adder->collect);
    adder->collect = NULL;
  }
  gst_caps_replace (&adder->filter_caps, NULL);
  gst_caps_replace (&adder->current_caps, NULL);

  if (adder->pending_events) {
    g_list_foreach (adder->pending_events, (GFunc) gst_event_unref, NULL);
    g_list_free (adder->pending_events);
    adder->pending_events = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_adder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAdder *adder = GST_ADDER (object);

  switch (prop_id) {
    case PROP_FILTER_CAPS:{
      GstCaps *new_caps = NULL;
      GstCaps *old_caps;
      const GstCaps *new_caps_val = gst_value_get_caps (value);

      if (new_caps_val != NULL) {
        new_caps = (GstCaps *) new_caps_val;
        gst_caps_ref (new_caps);
      }

      GST_OBJECT_LOCK (adder);
      old_caps = adder->filter_caps;
      adder->filter_caps = new_caps;
      GST_OBJECT_UNLOCK (adder);

      if (old_caps)
        gst_caps_unref (old_caps);

      GST_DEBUG_OBJECT (adder, "set new caps %" GST_PTR_FORMAT, new_caps);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_adder_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAdder *adder = GST_ADDER (object);

  switch (prop_id) {
    case PROP_FILTER_CAPS:
      GST_OBJECT_LOCK (adder);
      gst_value_set_caps (value, adder->filter_caps);
      GST_OBJECT_UNLOCK (adder);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstPad *
gst_adder_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused, const GstCaps * caps)
{
  gchar *name;
  GstAdder *adder;
  GstPad *newpad;
  gint padcount;

  if (templ->direction != GST_PAD_SINK)
    goto not_sink;

  adder = GST_ADDER (element);

  /* increment pad counter */
  padcount = g_atomic_int_add (&adder->padcount, 1);

  name = g_strdup_printf ("sink_%u", padcount);
  newpad = gst_pad_new_from_template (templ, name);
  GST_DEBUG_OBJECT (adder, "request new pad %s", name);
  g_free (name);

  gst_collect_pads_add_pad (adder->collect, newpad, sizeof (GstCollectData),
      NULL, TRUE);

  /* takes ownership of the pad */
  if (!gst_element_add_pad (GST_ELEMENT (adder), newpad))
    goto could_not_add;

  return newpad;

  /* errors */
not_sink:
  {
    g_warning ("gstadder: request new pad that is not a SINK pad\n");
    return NULL;
  }
could_not_add:
  {
    GST_DEBUG_OBJECT (adder, "could not add pad");
    gst_collect_pads_remove_pad (adder->collect, newpad);
    gst_object_unref (newpad);
    return NULL;
  }
}

static void
gst_adder_release_pad (GstElement * element, GstPad * pad)
{
  GstAdder *adder;

  adder = GST_ADDER (element);

  GST_DEBUG_OBJECT (adder, "release pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  if (adder->collect)
    gst_collect_pads_remove_pad (adder->collect, pad);
  gst_element_remove_pad (element, pad);
}

static GstFlowReturn
gst_adder_do_clip (GstCollectPads * pads, GstCollectData * data,
    GstBuffer * buffer, GstBuffer ** out, gpointer user_data)
{
  GstAdder *adder = GST_ADDER (user_data);
  gint rate, bpf;

  rate = GST_AUDIO_INFO_RATE (&adder->info);
  bpf = GST_AUDIO_INFO_BPF (&adder->info);

  buffer = gst_audio_buffer_clip (buffer, &data->segment, rate, bpf);

  *out = buffer;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_adder_collected (GstCollectPads * pads, gpointer user_data)
{
  /*
   * combine streams by adding data values
   * basic algorithm :
   * - this function is called when all pads have a buffer
   * - get available bytes on all pads.
   * - repeat for each input pad :
   *   - read available bytes, copy or add to target buffer
   *   - if there's an EOS event, remove the input channel
   * - push out the output buffer
   *
   * todo:
   * - would be nice to have a mixing mode, where instead of adding we mix
   *   - for float we could downscale after collect loop
   *   - for int we need to downscale each input to avoid clipping or
   *     mix into a temp (float) buffer and scale afterwards as well
   */
  GstAdder *adder;
  GSList *collected, *next = NULL;
  GstFlowReturn ret;
  GstBuffer *outbuf = NULL, *gapbuf = NULL;
  GstMapInfo outmap = { NULL };
  guint outsize;
  gint64 next_offset;
  gint64 next_timestamp;
  gint rate, bps, bpf;

  adder = GST_ADDER (user_data);

  /* this is fatal */
  if (G_UNLIKELY (adder->func == NULL))
    goto not_negotiated;

  if (adder->send_stream_start) {
    gchar s_id[32];

    /* stream-start (FIXME: create id based on input ids) */
    g_snprintf (s_id, sizeof (s_id), "adder-%08x", g_random_int ());
    gst_pad_push_event (adder->srcpad, gst_event_new_stream_start (s_id));
    adder->send_stream_start = FALSE;
  }

  if (adder->send_caps) {
    GstEvent *caps_event;

    caps_event = gst_event_new_caps (adder->current_caps);
    GST_INFO_OBJECT (adder, "caps event %" GST_PTR_FORMAT, caps_event);
    gst_pad_push_event (adder->srcpad, caps_event);
    adder->send_caps = FALSE;
  }

  if (g_atomic_int_compare_and_exchange (&adder->flush_stop_pending,
          TRUE, FALSE)) {
    GST_DEBUG_OBJECT (adder, "pending flush stop");
    gst_pad_push_event (adder->srcpad, gst_event_new_flush_stop (TRUE));
  }

  /* get available bytes for reading, this can be 0 which could mean empty
   * buffers or EOS, which we will catch when we loop over the pads. */
  outsize = gst_collect_pads_available (pads);
  /* can only happen when no pads to collect or all EOS */
  if (outsize == 0)
    goto eos;

  rate = GST_AUDIO_INFO_RATE (&adder->info);
  bps = GST_AUDIO_INFO_BPS (&adder->info);
  bpf = GST_AUDIO_INFO_BPF (&adder->info);

  GST_LOG_OBJECT (adder,
      "starting to cycle through channels, %d bytes available (bps = %d, bpf = %d)",
      outsize, bps, bpf);

  for (collected = pads->data; collected; collected = next) {
    GstCollectData *collect_data;
    GstBuffer *inbuf;
    gboolean is_gap;

    /* take next to see if this is the last collectdata */
    next = g_slist_next (collected);

    collect_data = (GstCollectData *) collected->data;

    /* get a buffer of size bytes, if we get a buffer, it is at least outsize
     * bytes big. */
    inbuf = gst_collect_pads_take_buffer (pads, collect_data, outsize);
    /* NULL means EOS or an empty buffer so we still need to flush in
     * case of an empty buffer. */
    if (inbuf == NULL) {
      GST_LOG_OBJECT (adder, "channel %p: no bytes available", collect_data);
      continue;
    }

    is_gap = GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP);

    /* Try to make an output buffer */
    if (outbuf == NULL) {
      /* if this is a gap buffer but we have some more pads to check, skip it.
       * If we are at the last buffer, take it, regardless if it is a GAP
       * buffer or not. */
      if (is_gap && next) {
        GST_DEBUG_OBJECT (adder, "skipping, non-last GAP buffer");
        /* we keep the GAP buffer, if we don't have anymore buffers (all pads
         * EOS, we can use this one as the output buffer. */
        if (gapbuf == NULL)
          gapbuf = inbuf;
        else
          gst_buffer_unref (inbuf);
        continue;
      }

      GST_LOG_OBJECT (adder, "channel %p: preparing output buffer of %d bytes",
          collect_data, outsize);

      /* make data and metadata writable, can simply return the inbuf when we
       * are the only one referencing this buffer. If this is the last (and
       * only) GAP buffer, it will automatically copy the GAP flag. */
      outbuf = gst_buffer_make_writable (inbuf);
      gst_buffer_map (outbuf, &outmap, GST_MAP_WRITE);
    } else {
      if (!is_gap) {
        /* we had a previous output buffer, mix this non-GAP buffer */
        GstMapInfo inmap;

        gst_buffer_map (inbuf, &inmap, GST_MAP_READ);

        /* all buffers should have outsize, there are no short buffers because we
         * asked for the max size above */
        g_assert (inmap.size == outmap.size);

        GST_LOG_OBJECT (adder, "channel %p: mixing %" G_GSIZE_FORMAT " bytes"
            " from data %p", collect_data, inmap.size, inmap.data);

        /* further buffers, need to add them */
        adder->func ((gpointer) outmap.data, (gpointer) inmap.data,
            inmap.size / bps);
        gst_buffer_unmap (inbuf, &inmap);
      } else {
        /* skip gap buffer */
        GST_LOG_OBJECT (adder, "channel %p: skipping GAP buffer", collect_data);
      }
      gst_buffer_unref (inbuf);
    }
  }
  if (outbuf)
    gst_buffer_unmap (outbuf, &outmap);

  if (outbuf == NULL) {
    /* no output buffer, reuse one of the GAP buffers then if we have one */
    if (gapbuf) {
      GST_LOG_OBJECT (adder, "reusing GAP buffer %p", gapbuf);
      outbuf = gapbuf;
    } else
      /* assume EOS otherwise, this should not happen, really */
      goto eos;
  } else if (gapbuf)
    /* we had an output buffer, unref the gapbuffer we kept */
    gst_buffer_unref (gapbuf);

  if (g_atomic_int_compare_and_exchange (&adder->new_segment_pending, TRUE,
          FALSE)) {
    GstEvent *event;

    /* FIXME, use rate/applied_rate as set on all sinkpads.
     * - currently we just set rate as received from last seek-event
     *
     * When seeking we set the start and stop positions as given in the seek
     * event. We also adjust offset & timestamp accordingly.
     * This basically ignores all newsegments sent by upstream.
     */
    event = gst_event_new_segment (&adder->segment);

    if (adder->segment.rate > 0.0) {
      adder->segment.position = adder->segment.start;
    } else {
      adder->segment.position = adder->segment.stop;
    }
    adder->offset = gst_util_uint64_scale (adder->segment.position,
        rate, GST_SECOND);
    GST_INFO_OBJECT (adder, "seg_start %" G_GUINT64_FORMAT ", seg_end %"
        G_GUINT64_FORMAT, adder->segment.start, adder->segment.stop);
    GST_INFO_OBJECT (adder, "timestamp %" G_GINT64_FORMAT ",new offset %"
        G_GINT64_FORMAT, adder->segment.position, adder->offset);

    if (event) {
      if (!gst_pad_push_event (adder->srcpad, event)) {
        GST_WARNING_OBJECT (adder->srcpad, "Sending new segment event failed");
      }
    } else {
      GST_WARNING_OBJECT (adder->srcpad, "Creating new segment event for "
          "start:%" G_GINT64_FORMAT "  end:%" G_GINT64_FORMAT " failed",
          adder->segment.start, adder->segment.stop);
    }
  }

  if (G_UNLIKELY (adder->pending_events)) {
    GList *tmp = adder->pending_events;

    while (tmp) {
      GstEvent *ev = (GstEvent *) tmp->data;

      gst_pad_push_event (adder->srcpad, ev);
      tmp = g_list_next (tmp);
    }
    g_list_free (adder->pending_events);
    adder->pending_events = NULL;
  }

  /* for the next timestamp, use the sample counter, which will
   * never accumulate rounding errors */
  if (adder->segment.rate > 0.0) {
    next_offset = adder->offset + outsize / bpf;
  } else {
    next_offset = adder->offset - outsize / bpf;
  }
  next_timestamp = gst_util_uint64_scale (next_offset, GST_SECOND, rate);


  /* set timestamps on the output buffer */
  if (adder->segment.rate > 0.0) {
    GST_BUFFER_TIMESTAMP (outbuf) = adder->segment.position;
    GST_BUFFER_OFFSET (outbuf) = adder->offset;
    GST_BUFFER_OFFSET_END (outbuf) = next_offset;
    GST_BUFFER_DURATION (outbuf) = next_timestamp - adder->segment.position;
  } else {
    GST_BUFFER_TIMESTAMP (outbuf) = next_timestamp;
    GST_BUFFER_OFFSET (outbuf) = next_offset;
    GST_BUFFER_OFFSET_END (outbuf) = adder->offset;
    GST_BUFFER_DURATION (outbuf) = adder->segment.position - next_timestamp;
  }

  adder->offset = next_offset;
  adder->segment.position = next_timestamp;

  /* send it out */
  GST_LOG_OBJECT (adder, "pushing outbuf %p, timestamp %" GST_TIME_FORMAT
      " offset %" G_GINT64_FORMAT, outbuf,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_BUFFER_OFFSET (outbuf));
  ret = gst_pad_push (adder->srcpad, outbuf);

  GST_LOG_OBJECT (adder, "pushed outbuf, result = %s", gst_flow_get_name (ret));

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (adder, STREAM, FORMAT, (NULL),
        ("Unknown data received, not negotiated"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
eos:
  {
    GST_DEBUG_OBJECT (adder, "no data available, must be EOS");
    gst_pad_push_event (adder->srcpad, gst_event_new_eos ());
    return GST_FLOW_EOS;
  }
}

static GstStateChangeReturn
gst_adder_change_state (GstElement * element, GstStateChange transition)
{
  GstAdder *adder;
  GstStateChangeReturn ret;

  adder = GST_ADDER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      adder->segment.position = 0;
      adder->offset = 0;
      adder->flush_stop_pending = FALSE;
      adder->new_segment_pending = TRUE;
      adder->wait_for_new_segment = FALSE;
      adder->send_stream_start = TRUE;
      adder->send_caps = TRUE;
      gst_caps_replace (&adder->current_caps, NULL);
      gst_segment_init (&adder->segment, GST_FORMAT_TIME);
      gst_collect_pads_start (adder->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* need to unblock the collectpads before calling the
       * parent change_state so that streaming can finish */
      gst_collect_pads_stop (adder->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "adder", 0,
      "audio channel mixing element");

  if (!gst_element_register (plugin, "adder", GST_RANK_NONE, GST_TYPE_ADDER)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    adder,
    "Adds multiple streams",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
