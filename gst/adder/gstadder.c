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

#define CAPS \
  "audio/x-raw-int, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) [ 1, MAX ], " \
  "endianness = (int) BYTE_ORDER, " \
  "width = (int) 32, " \
  "depth = (int) 32, " \
  "signed = (boolean) { true, false } ;" \
  "audio/x-raw-int, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) [ 1, MAX ], " \
  "endianness = (int) BYTE_ORDER, " \
  "width = (int) 16, " \
  "depth = (int) 16, " \
  "signed = (boolean) { true, false } ;" \
  "audio/x-raw-int, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) [ 1, MAX ], " \
  "endianness = (int) BYTE_ORDER, " \
  "width = (int) 8, " \
  "depth = (int) 8, " \
  "signed = (boolean) { true, false } ;" \
  "audio/x-raw-float, " \
  "rate = (int) [ 1, MAX ], " \
  "channels = (int) [ 1, MAX ], " \
  "endianness = (int) BYTE_ORDER, " \
  "width = (int) { 32, 64 }"

static GstStaticPadTemplate gst_adder_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS)
    );

static GstStaticPadTemplate gst_adder_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (CAPS)
    );

GST_BOILERPLATE (GstAdder, gst_adder, GstElement, GST_TYPE_ELEMENT);

static void gst_adder_dispose (GObject * object);
static void gst_adder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_adder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_adder_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_adder_query (GstPad * pad, GstQuery * query);
static gboolean gst_adder_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_adder_sink_event (GstPad * pad, GstEvent * event);

static GstPad *gst_adder_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused);
static void gst_adder_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_adder_change_state (GstElement * element,
    GstStateChange transition);

static GstBuffer *gst_adder_do_clip (GstCollectPads * pads,
    GstCollectData * data, GstBuffer * buffer, gpointer user_data);
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
MAKE_FUNC_NC (add_float64, gdouble)
/* *INDENT-ON* */

/* we can only accept caps that we and downstream can handle.
 * if we have filtercaps set, use those to constrain the target caps.
 */
static GstCaps *
gst_adder_sink_getcaps (GstPad * pad)
{
  GstAdder *adder;
  GstCaps *result, *peercaps, *sinkcaps, *filter_caps;

  adder = GST_ADDER (GST_PAD_PARENT (pad));

  GST_OBJECT_LOCK (adder);
  /* take filter */
  if ((filter_caps = adder->filter_caps))
    gst_caps_ref (filter_caps);
  GST_OBJECT_UNLOCK (adder);

  /* get the downstream possible caps */
  peercaps = gst_pad_peer_get_caps (adder->srcpad);

  /* get the allowed caps on this sinkpad, we use the fixed caps function so
   * that it does not call recursively in this function. */
  sinkcaps = gst_pad_get_fixed_caps_func (pad);
  if (peercaps) {
    /* restrict with filter-caps if any */
    if (filter_caps) {
      GST_DEBUG_OBJECT (adder, "filtering peer caps");
      result = gst_caps_intersect (peercaps, filter_caps);
      gst_caps_unref (peercaps);
      peercaps = result;
    }
    /* if the peer has caps, intersect */
    GST_DEBUG_OBJECT (adder, "intersecting peer and template caps");
    result = gst_caps_intersect (peercaps, sinkcaps);
    gst_caps_unref (peercaps);
    gst_caps_unref (sinkcaps);
  } else {
    /* the peer has no caps (or there is no peer), just use the allowed caps
     * of this sinkpad. */
    /* restrict with filter-caps if any */
    if (filter_caps) {
      GST_DEBUG_OBJECT (adder, "no peer caps, using filtered sinkcaps");
      result = gst_caps_intersect (sinkcaps, filter_caps);
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

/* the first caps we receive on any of the sinkpads will define the caps for all
 * the other sinkpads because we can only mix streams with the same caps.
 */
static gboolean
gst_adder_setcaps (GstPad * pad, GstCaps * caps)
{
  GstAdder *adder;
  GList *pads;
  GstStructure *structure;
  const char *media_type;

  adder = GST_ADDER (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (adder, "setting caps on pad %p,%s to %" GST_PTR_FORMAT, pad,
      GST_PAD_NAME (pad), caps);

  /* FIXME, see if the other pads can accept the format. Also lock the
   * format on the other pads to this new format. */
  GST_OBJECT_LOCK (adder);
  pads = GST_ELEMENT (adder)->pads;
  while (pads) {
    GstPad *otherpad = GST_PAD (pads->data);

    if (otherpad != pad) {
      gst_caps_replace (&GST_PAD_CAPS (otherpad), caps);
    }
    pads = g_list_next (pads);
  }
  GST_OBJECT_UNLOCK (adder);

  /* parse caps now */
  structure = gst_caps_get_structure (caps, 0);
  media_type = gst_structure_get_name (structure);
  if (strcmp (media_type, "audio/x-raw-int") == 0) {
    adder->format = GST_ADDER_FORMAT_INT;
    gst_structure_get_int (structure, "width", &adder->width);
    gst_structure_get_int (structure, "depth", &adder->depth);
    gst_structure_get_int (structure, "endianness", &adder->endianness);
    gst_structure_get_boolean (structure, "signed", &adder->is_signed);

    GST_INFO_OBJECT (pad, "parse_caps sets adder to format int, %d bit",
        adder->width);

    if (adder->endianness != G_BYTE_ORDER)
      goto not_supported;

    switch (adder->width) {
      case 8:
        adder->func = (adder->is_signed ?
            (GstAdderFunction) add_int8 : (GstAdderFunction) add_uint8);
        adder->sample_size = 1;
        break;
      case 16:
        adder->func = (adder->is_signed ?
            (GstAdderFunction) add_int16 : (GstAdderFunction) add_uint16);
        adder->sample_size = 2;
        break;
      case 32:
        adder->func = (adder->is_signed ?
            (GstAdderFunction) add_int32 : (GstAdderFunction) add_uint32);
        adder->sample_size = 4;
        break;
      default:
        goto not_supported;
    }
  } else if (strcmp (media_type, "audio/x-raw-float") == 0) {
    adder->format = GST_ADDER_FORMAT_FLOAT;
    gst_structure_get_int (structure, "width", &adder->width);
    gst_structure_get_int (structure, "endianness", &adder->endianness);

    GST_INFO_OBJECT (pad, "parse_caps sets adder to format float, %d bit",
        adder->width);

    if (adder->endianness != G_BYTE_ORDER)
      goto not_supported;

    switch (adder->width) {
      case 32:
        adder->func = (GstAdderFunction) add_float32;
        adder->sample_size = 4;
        break;
      case 64:
        adder->func = (GstAdderFunction) add_float64;
        adder->sample_size = 8;
        break;
      default:
        goto not_supported;
    }
  } else {
    goto not_supported;
  }

  gst_structure_get_int (structure, "channels", &adder->channels);
  gst_structure_get_int (structure, "rate", &adder->rate);
  /* precalc bps */
  adder->bps = (adder->width / 8) * adder->channels;

  return TRUE;

  /* ERRORS */
not_supported:
  {
    GST_DEBUG_OBJECT (adder, "unsupported format set as caps");
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

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  max = -1;
  res = TRUE;
  done = FALSE;

  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (adder));
  while (!done) {
    GstIteratorResult ires;

    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);

        gint64 duration;

        /* ask sink peer for duration */
        res &= gst_pad_query_peer_duration (pad, &format, &duration);
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
        gst_object_unref (pad);
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

  res = TRUE;
  done = FALSE;

  live = FALSE;
  min = 0;
  max = GST_CLOCK_TIME_NONE;

  /* Take maximum of all latency values */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (adder));
  while (!done) {
    GstIteratorResult ires;

    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);
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
        gst_object_unref (pad);
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
gst_adder_query (GstPad * pad, GstQuery * query)
{
  GstAdder *adder = GST_ADDER (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          /* FIXME, bring to stream time, might be tricky */
          gst_query_set_position (query, format, adder->timestamp);
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
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (adder);
  return res;
}

typedef struct
{
  GstEvent *event;
  gboolean flush;
} EventData;

static gboolean
forward_event_func (GstPad * pad, GValue * ret, EventData * data)
{
  GstEvent *event = data->event;

  gst_event_ref (event);
  GST_LOG_OBJECT (pad, "About to send event %s", GST_EVENT_TYPE_NAME (event));
  if (!gst_pad_push_event (pad, event)) {
    GST_WARNING_OBJECT (pad, "Sending event  %p (%s) failed.",
        event, GST_EVENT_TYPE_NAME (event));
    /* quick hack to unflush the pads, ideally we need a way to just unflush
     * this single collect pad */
    if (data->flush)
      gst_pad_send_event (pad, gst_event_new_flush_stop ());
  } else {
    g_value_set_boolean (ret, TRUE);
    GST_LOG_OBJECT (pad, "Sent event  %p (%s).",
        event, GST_EVENT_TYPE_NAME (event));
  }
  gst_object_unref (pad);

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
    ires = gst_iterator_fold (it, (GstIteratorFoldFunction) forward_event_func,
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
gst_adder_src_event (GstPad * pad, GstEvent * event)
{
  GstAdder *adder;
  gboolean result;

  adder = GST_ADDER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "Got %s event on src pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      GstSeekType curtype, endtype;
      gint64 cur, end;
      gboolean flush;

      /* parse the seek parameters */
      gst_event_parse_seek (event, &adder->segment_rate, NULL, &flags, &curtype,
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
        /* make sure we accept nothing anymore and return WRONG_STATE */
        gst_collect_pads_set_flushing (adder->collect, TRUE);

        /* flushing seek, start flush downstream, the flush will be done
         * when all pads received a FLUSH_STOP. */
        gst_pad_push_event (adder->srcpad, gst_event_new_flush_start ());

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
      GST_OBJECT_LOCK (adder->collect);
      if (curtype == GST_SEEK_TYPE_SET)
        adder->segment_start = cur;
      else
        adder->segment_start = 0;
      if (endtype == GST_SEEK_TYPE_SET)
        adder->segment_end = end;
      else
        adder->segment_end = GST_CLOCK_TIME_NONE;
      if (flush) {
        /* Yes, we need to call _set_flushing again *WHEN* the streaming threads
         * have stopped so that the cookie gets properly updated. */
        gst_collect_pads_set_flushing (adder->collect, TRUE);
      }
      GST_OBJECT_UNLOCK (adder->collect);
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
        gst_pad_push_event (adder->srcpad, gst_event_new_flush_stop ());
      }
      break;
    }
    case GST_EVENT_QOS:
      /* QoS might be tricky */
      result = FALSE;
      break;
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      result = FALSE;
      break;
    default:
      /* just forward the rest for now */
      GST_DEBUG_OBJECT (adder, "forward unhandled event: %s",
          GST_EVENT_TYPE_NAME (event));
      result = forward_event (adder, event, FALSE);
      break;
  }

done:
  gst_object_unref (adder);

  return result;
}

static gboolean
gst_adder_sink_event (GstPad * pad, GstEvent * event)
{
  GstAdder *adder;
  gboolean ret = TRUE;

  adder = GST_ADDER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "Got %s event on sink pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      /* we received a flush-stop. The collect_event function will push the
       * event past our element. We simply forward all flush-stop events, even
       * when no flush-stop was pending, this is required because collectpads
       * does not provide an API to handle-but-not-forward the flush-stop.
       * We unset the pending flush-stop flag so that we don't send anymore
       * flush-stop from the collect function later.
       */
      GST_OBJECT_LOCK (adder->collect);
      g_atomic_int_set (&adder->new_segment_pending, TRUE);
      g_atomic_int_set (&adder->flush_stop_pending, FALSE);
      /* Clear pending tags */
      if (adder->pending_events) {
        g_list_foreach (adder->pending_events, (GFunc) gst_event_unref, NULL);
        g_list_free (adder->pending_events);
        adder->pending_events = NULL;
      }
      GST_OBJECT_UNLOCK (adder->collect);
      break;
    case GST_EVENT_TAG:
      GST_OBJECT_LOCK (adder->collect);
      /* collect tags here so we can push them out when we collect data */
      adder->pending_events = g_list_append (adder->pending_events, event);
      GST_OBJECT_UNLOCK (adder->collect);
      goto beach;
    case GST_EVENT_NEWSEGMENT:
      if (g_atomic_int_compare_and_exchange (&adder->wait_for_new_segment,
              TRUE, FALSE)) {
        /* make sure we push a new segment, to inform about new basetime
         * see FIXME in gst_adder_collected() */
        g_atomic_int_set (&adder->new_segment_pending, TRUE);
      }
      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  ret = adder->collect_event (pad, event);

beach:
  gst_object_unref (adder);
  return ret;
}

static void
gst_adder_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_adder_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_adder_sink_template);
  gst_element_class_set_details_simple (gstelement_class, "Adder",
      "Generic/Audio",
      "Add N audio channels together",
      "Thomas Vander Stichele <thomas at apestaart dot org>");
}

static void
gst_adder_class_init (GstAdderClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_adder_set_property;
  gobject_class->get_property = gst_adder_get_property;
  gobject_class->dispose = gst_adder_dispose;

  /**
   * GstAdder:caps:
   *
   * Since: 0.10.24
   */
  g_object_class_install_property (gobject_class, PROP_FILTER_CAPS,
      g_param_spec_boxed ("caps", "Target caps",
          "Set target format for mixing (NULL means ANY). "
          "Setting this property takes a reference to the supplied GstCaps "
          "object.", GST_TYPE_CAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_adder_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_adder_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_adder_change_state);
}

static void
gst_adder_init (GstAdder * adder, GstAdderClass * klass)
{
  GstPadTemplate *template;

  template = gst_static_pad_template_get (&gst_adder_src_template);
  adder->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);

  gst_pad_set_getcaps_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_setcaps_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_adder_setcaps));
  gst_pad_set_query_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_adder_query));
  gst_pad_set_event_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_adder_src_event));
  gst_element_add_pad (GST_ELEMENT (adder), adder->srcpad);

  adder->format = GST_ADDER_FORMAT_UNSET;
  adder->padcount = 0;
  adder->func = NULL;

  adder->filter_caps = NULL;

  /* keep track of the sinkpads requested */
  adder->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (adder->collect,
      GST_DEBUG_FUNCPTR (gst_adder_collected), adder);
  gst_collect_pads_set_clip_function (adder->collect,
      GST_DEBUG_FUNCPTR (gst_adder_do_clip), adder);
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
    const gchar * unused)
{
  gchar *name;
  GstAdder *adder;
  GstPad *newpad;
  gint padcount;

  if (templ->direction != GST_PAD_SINK)
    goto not_sink;

  adder = GST_ADDER (element);

  /* increment pad counter */
#if GLIB_CHECK_VERSION(2,29,5)
  padcount = g_atomic_int_add (&adder->padcount, 1);
#else
  padcount = g_atomic_int_exchange_and_add (&adder->padcount, 1);
#endif

  name = g_strdup_printf ("sink%d", padcount);
  newpad = gst_pad_new_from_template (templ, name);
  GST_DEBUG_OBJECT (adder, "request new pad %s", name);
  g_free (name);

  gst_pad_set_getcaps_function (newpad,
      GST_DEBUG_FUNCPTR (gst_adder_sink_getcaps));
  gst_pad_set_setcaps_function (newpad, GST_DEBUG_FUNCPTR (gst_adder_setcaps));
  gst_collect_pads_add_pad (adder->collect, newpad, sizeof (GstCollectData));

  /* FIXME: hacked way to override/extend the event function of
   * GstCollectPads; because it sets its own event function giving the
   * element no access to events */
  adder->collect_event = (GstPadEventFunction) GST_PAD_EVENTFUNC (newpad);
  gst_pad_set_event_function (newpad, GST_DEBUG_FUNCPTR (gst_adder_sink_event));

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

  gst_collect_pads_remove_pad (adder->collect, pad);
  gst_element_remove_pad (element, pad);
}

static GstBuffer *
gst_adder_do_clip (GstCollectPads * pads, GstCollectData * data,
    GstBuffer * buffer, gpointer user_data)
{
  GstAdder *adder = GST_ADDER (user_data);

  /* in 0.10 the application might need to seek on newly added source-branches
   * to make it send a newsegment, that is hard to sync and so the segment might
   * not be initialized. Check this here to not trigger the assertion
   */
  if (data->segment.format != GST_FORMAT_UNDEFINED) {
    buffer = gst_audio_buffer_clip (buffer, &data->segment, adder->rate,
        adder->bps);
  }

  return buffer;
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
  gpointer outdata = NULL;
  guint outsize;
  gint64 next_offset;
  gint64 next_timestamp;

  adder = GST_ADDER (user_data);

  /* this is fatal */
  if (G_UNLIKELY (adder->func == NULL))
    goto not_negotiated;

  if (g_atomic_int_compare_and_exchange (&adder->flush_stop_pending,
          TRUE, FALSE)) {
    GST_DEBUG_OBJECT (adder, "pending flush stop");
    gst_pad_push_event (adder->srcpad, gst_event_new_flush_stop ());
  }

  /* get available bytes for reading, this can be 0 which could mean empty
   * buffers or EOS, which we will catch when we loop over the pads. */
  outsize = gst_collect_pads_available (pads);
  /* can only happen when no pads to collect or all EOS */
  if (outsize == 0)
    goto eos;

  GST_LOG_OBJECT (adder,
      "starting to cycle through channels, %d bytes available (bps = %d)",
      outsize, adder->bps);

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
      outdata = GST_BUFFER_DATA (outbuf);
      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (adder->srcpad));
    } else {
      if (!is_gap) {
        /* we had a previous output buffer, mix this non-GAP buffer */
        guint8 *indata;
        guint insize;

        indata = GST_BUFFER_DATA (inbuf);
        insize = GST_BUFFER_SIZE (inbuf);

        /* all buffers should have outsize, there are no short buffers because we
         * asked for the max size above */
        g_assert (insize == outsize);

        GST_LOG_OBJECT (adder, "channel %p: mixing %d bytes from data %p",
            collect_data, insize, indata);

        /* further buffers, need to add them */
        adder->func ((gpointer) outdata, (gpointer) indata,
            insize / adder->sample_size);
      } else {
        /* skip gap buffer */
        GST_LOG_OBJECT (adder, "channel %p: skipping GAP buffer", collect_data);
      }
      gst_buffer_unref (inbuf);
    }
  }

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
    event = gst_event_new_new_segment_full (FALSE, adder->segment_rate,
        1.0, GST_FORMAT_TIME, adder->segment_start, adder->segment_end,
        adder->segment_start);
    if (adder->segment_rate > 0.0) {
      adder->timestamp = adder->segment_start;
    } else {
      adder->timestamp = adder->segment_end;
    }
    adder->offset = gst_util_uint64_scale (adder->timestamp,
        adder->rate, GST_SECOND);
    GST_INFO_OBJECT (adder, "seg_start %" G_GUINT64_FORMAT ", seg_end %"
        G_GUINT64_FORMAT, adder->segment_start, adder->segment_end);
    GST_INFO_OBJECT (adder, "timestamp %" G_GINT64_FORMAT ",new offset %"
        G_GINT64_FORMAT, adder->timestamp, adder->offset);

    if (event) {
      if (!gst_pad_push_event (adder->srcpad, event)) {
        GST_WARNING_OBJECT (adder->srcpad, "Sending event failed");
      }
    } else {
      GST_WARNING_OBJECT (adder->srcpad, "Creating new segment event for "
          "start:%" G_GINT64_FORMAT "  end:%" G_GINT64_FORMAT " failed",
          adder->segment_start, adder->segment_end);
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
  if (adder->segment_rate > 0.0) {
    next_offset = adder->offset + outsize / adder->bps;
  } else {
    next_offset = adder->offset - outsize / adder->bps;
  }
  next_timestamp = gst_util_uint64_scale (next_offset, GST_SECOND, adder->rate);


  /* set timestamps on the output buffer */
  if (adder->segment_rate > 0.0) {
    GST_BUFFER_TIMESTAMP (outbuf) = adder->timestamp;
    GST_BUFFER_OFFSET (outbuf) = adder->offset;
    GST_BUFFER_OFFSET_END (outbuf) = next_offset;
    GST_BUFFER_DURATION (outbuf) = next_timestamp - adder->timestamp;
  } else {
    GST_BUFFER_TIMESTAMP (outbuf) = next_timestamp;
    GST_BUFFER_OFFSET (outbuf) = next_offset;
    GST_BUFFER_OFFSET_END (outbuf) = adder->offset;
    GST_BUFFER_DURATION (outbuf) = adder->timestamp - next_timestamp;
  }

  adder->offset = next_offset;
  adder->timestamp = next_timestamp;

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
    return GST_FLOW_UNEXPECTED;
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
      adder->timestamp = 0;
      adder->offset = 0;
      adder->flush_stop_pending = FALSE;
      adder->new_segment_pending = TRUE;
      adder->wait_for_new_segment = FALSE;
      adder->segment_start = 0;
      adder->segment_end = GST_CLOCK_TIME_NONE;
      adder->segment_rate = 1.0;
      gst_segment_init (&adder->segment, GST_FORMAT_UNDEFINED);
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

  gst_adder_orc_init ();

  if (!gst_element_register (plugin, "adder", GST_RANK_NONE, GST_TYPE_ADDER)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "adder",
    "Adds multiple streams",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
