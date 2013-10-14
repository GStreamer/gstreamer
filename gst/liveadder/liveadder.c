/*
 * GStreamer
 *
 *  Copyright 2012 Collabora Ltd
 *  Copyright 2008 Nokia Corporation
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *
 * With parts copied from the adder plugin which is
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2001 Thomas <thomas@apestaart.org>
 *               2005,2006 Wim Taymans <wim@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */
/**
 * SECTION:element-liveadder
 * @see_also: adder
 *
 * The live adder allows to mix several streams into one by adding the data.
 * Mixed data is clamped to the min/max values of the data format.
 *
 * Unlike the adder, the liveadder mixes the streams according the their
 * timestamps and waits for some milli-seconds before trying doing the mixing.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "liveadder.h"

#include <gst/audio/audio.h>

#include <string.h>

#define DEFAULT_LATENCY_MS 60

GST_DEBUG_CATEGORY_STATIC (live_adder_debug);
#define GST_CAT_DEFAULT (live_adder_debug)

static GstStaticPadTemplate gst_live_adder_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE ("{ S8, U8, "
            GST_AUDIO_NE (S16) "," GST_AUDIO_NE (U16) ","
            GST_AUDIO_NE (S32) "," GST_AUDIO_NE (U32) ","
            GST_AUDIO_NE (F32) "," GST_AUDIO_NE (F64) "}"))
    );

static GstStaticPadTemplate gst_live_adder_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE ("{ S8, U8, "
            GST_AUDIO_NE (S16) "," GST_AUDIO_NE (U16) ","
            GST_AUDIO_NE (S32) "," GST_AUDIO_NE (U32) ","
            GST_AUDIO_NE (F32) "," GST_AUDIO_NE (F64) "}"))
    );

/* Valve signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LATENCY,
};

typedef struct _GstLiveAdderPadPrivate
{
  GstSegment segment;
  gboolean eos;

  GstClockTime expected_timestamp;

} GstLiveAdderPadPrivate;

G_DEFINE_TYPE (GstLiveAdder, gst_live_adder, GST_TYPE_ELEMENT);

static void gst_live_adder_finalize (GObject * object);
static void
gst_live_adder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void
gst_live_adder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *gst_live_adder_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_live_adder_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_live_adder_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_live_adder_setcaps (GstLiveAdder * adder, GstPad * pad,
    GstCaps * caps);
static GstCaps *gst_live_adder_sink_getcaps (GstLiveAdder * adder, GstPad * pad,
    GstCaps * filter);
static gboolean gst_live_adder_src_activate_mode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active);
static gboolean gst_live_adder_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static void gst_live_adder_loop (gpointer data);
static gboolean gst_live_adder_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_live_adder_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_live_adder_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);


static void reset_pad_private (GstPad * pad);

/* clipping versions */
#define MAKE_FUNC(name,type,ttype,min,max)                      \
static void name (type *out, type *in, gint bytes) {            \
  gint i;                                                       \
  for (i = 0; i < bytes / sizeof (type); i++)                   \
    out[i] = CLAMP ((ttype)out[i] + (ttype)in[i], min, max);    \
}

/* non-clipping versions (for float) */
#define MAKE_FUNC_NC(name,type,ttype)                           \
static void name (type *out, type *in, gint bytes) {            \
  gint i;                                                       \
  for (i = 0; i < bytes / sizeof (type); i++)                   \
    out[i] = (ttype)out[i] + (ttype)in[i];                      \
}

/* *INDENT-OFF* */
MAKE_FUNC (add_int32, gint32, gint64, G_MININT32, G_MAXINT32)
MAKE_FUNC (add_int16, gint16, gint32, G_MININT16, G_MAXINT16)
MAKE_FUNC (add_int8, gint8, gint16, G_MININT8, G_MAXINT8)
MAKE_FUNC (add_uint32, guint32, guint64, 0, G_MAXUINT32)
MAKE_FUNC (add_uint16, guint16, guint32, 0, G_MAXUINT16)
MAKE_FUNC (add_uint8, guint8, guint16, 0, G_MAXUINT8)
MAKE_FUNC_NC (add_float64, gdouble, gdouble)
MAKE_FUNC_NC (add_float32, gfloat, gfloat)
/* *INDENT-ON* */


static void
gst_live_adder_class_init (GstLiveAdderClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (live_adder_debug, "liveadder", 0, "Live Adder");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_live_adder_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_live_adder_sink_template));
  gst_element_class_set_static_metadata (gstelement_class, "Live Adder element",
      "Generic/Audio",
      "Mixes live/discontinuous audio streams",
      "Olivier Crete <olivier.crete@collabora.co.uk>");

  gobject_class->finalize = gst_live_adder_finalize;
  gobject_class->set_property = gst_live_adder_set_property;
  gobject_class->get_property = gst_live_adder_get_property;

  gstelement_class->request_new_pad = gst_live_adder_request_new_pad;
  gstelement_class->release_pad = gst_live_adder_release_pad;
  gstelement_class->change_state = gst_live_adder_change_state;

  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffering latency",
          "Amount of data to buffer (in milliseconds)",
          0, G_MAXUINT, DEFAULT_LATENCY_MS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_live_adder_init (GstLiveAdder * adder)
{
  adder->srcpad =
      gst_pad_new_from_static_template (&gst_live_adder_src_template, "src");
  gst_pad_set_query_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_live_adder_src_query));
  gst_pad_set_event_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_live_adder_src_event));
  gst_pad_set_activatemode_function (adder->srcpad,
      GST_DEBUG_FUNCPTR (gst_live_adder_src_activate_mode));
  gst_element_add_pad (GST_ELEMENT (adder), adder->srcpad);

  adder->padcount = 0;
  adder->func = NULL;
  g_cond_init (&adder->not_empty_cond);

  adder->next_timestamp = GST_CLOCK_TIME_NONE;

  adder->latency_ms = DEFAULT_LATENCY_MS;

  adder->buffers = g_queue_new ();
}


static void
gst_live_adder_finalize (GObject * object)
{
  GstLiveAdder *adder = GST_LIVE_ADDER (object);

  g_cond_clear (&adder->not_empty_cond);

  g_queue_foreach (adder->buffers, (GFunc) gst_mini_object_unref, NULL);
  while (g_queue_pop_head (adder->buffers)) {
  }
  g_queue_free (adder->buffers);

  g_list_free (adder->sinkpads);

  G_OBJECT_CLASS (gst_live_adder_parent_class)->finalize (object);
}


static void
gst_live_adder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstLiveAdder *adder = GST_LIVE_ADDER (object);

  switch (prop_id) {
    case PROP_LATENCY:
    {
      guint64 new_latency, old_latency;

      new_latency = g_value_get_uint (value);

      GST_OBJECT_LOCK (adder);
      old_latency = adder->latency_ms;
      adder->latency_ms = new_latency;
      GST_OBJECT_UNLOCK (adder);

      /* post message if latency changed, this will inform the parent pipeline
       * that a latency reconfiguration is possible/needed. */
      if (new_latency != old_latency) {
        GST_DEBUG_OBJECT (adder, "latency changed to: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (new_latency));

        gst_element_post_message (GST_ELEMENT_CAST (adder),
            gst_message_new_latency (GST_OBJECT_CAST (adder)));
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_live_adder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstLiveAdder *adder = GST_LIVE_ADDER (object);

  switch (prop_id) {
    case PROP_LATENCY:
      GST_OBJECT_LOCK (adder);
      g_value_set_uint (value, adder->latency_ms);
      GST_OBJECT_UNLOCK (adder);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* we can only accept caps that we and downstream can handle. */
static GstCaps *
gst_live_adder_sink_getcaps (GstLiveAdder * adder, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *result, *peercaps, *sinkcaps;

  /* get the downstream possible caps */
  peercaps = gst_pad_peer_query_caps (adder->srcpad, filter);
  /* get the allowed caps on this sinkpad, we use the fixed caps function so
   * that it does not call recursively in this function. */
  sinkcaps = gst_pad_get_current_caps (pad);
  if (!sinkcaps)
    sinkcaps = gst_pad_get_pad_template_caps (pad);
  if (peercaps) {
    /* if the peer has caps, intersect */
    GST_DEBUG_OBJECT (adder, "intersecting peer and template caps");
    result = gst_caps_intersect (peercaps, sinkcaps);
    gst_caps_unref (sinkcaps);
    gst_caps_unref (peercaps);
  } else {
    /* the peer has no caps (or there is no peer), just use the allowed caps
     * of this sinkpad. */
    GST_DEBUG_OBJECT (adder, "no peer caps, using sinkcaps");
    result = sinkcaps;
  }

  return result;
}

struct SetCapsIterCtx
{
  GstPad *pad;
  GstCaps *caps;
  gboolean all_valid;
};

static void
check_other_caps (const GValue * item, gpointer user_data)
{
  GstPad *otherpad = GST_PAD (g_value_get_object (item));
  struct SetCapsIterCtx *ctx = user_data;

  if (otherpad == ctx->pad)
    return;

  if (!gst_pad_peer_query_accept_caps (otherpad, ctx->caps))
    ctx->all_valid = FALSE;
}

static void
set_other_caps (const GValue * item, gpointer user_data)
{
  GstPad *otherpad = GST_PAD (g_value_get_object (item));
  struct SetCapsIterCtx *ctx = user_data;

  if (otherpad == ctx->pad)
    return;

  if (!gst_pad_set_caps (otherpad, ctx->caps))
    ctx->all_valid = FALSE;
}

/* the first caps we receive on any of the sinkpads will define the caps for all
 * the other sinkpads because we can only mix streams with the same caps.
 * */
static gboolean
gst_live_adder_setcaps (GstLiveAdder * adder, GstPad * pad, GstCaps * caps)
{
  GstIterator *iter;
  struct SetCapsIterCtx ctx;

  GST_LOG_OBJECT (adder, "setting caps on pad %p,%s to %" GST_PTR_FORMAT, pad,
      GST_PAD_NAME (pad), caps);

  /* FIXME, see if the other pads can accept the format. Also lock the
   * format on the other pads to this new format. */
  iter = gst_element_iterate_sink_pads (GST_ELEMENT (adder));
  ctx.pad = pad;
  ctx.caps = caps;
  ctx.all_valid = TRUE;
  while (gst_iterator_foreach (iter, check_other_caps, &ctx) ==
      GST_ITERATOR_RESYNC) {
    ctx.all_valid = TRUE;
    gst_iterator_resync (iter);
  }
  if (!ctx.all_valid) {
    GST_WARNING_OBJECT (adder, "Caps are not acceptable by other sinkpads");
    gst_iterator_free (iter);
    return FALSE;
  }

  while (gst_iterator_foreach (iter, set_other_caps, &ctx) ==
      GST_ITERATOR_RESYNC) {
    ctx.all_valid = TRUE;
    gst_iterator_resync (iter);
  }
  gst_iterator_free (iter);

  if (!ctx.all_valid) {
    GST_WARNING_OBJECT (adder, "Could not set caps on the other sink pads");
    return FALSE;
  }

  if (!gst_pad_set_caps (adder->srcpad, caps)) {
    GST_WARNING_OBJECT (adder, "Could not set caps downstream");
    return FALSE;
  }

  GST_OBJECT_LOCK (adder);
  /* parse caps now */
  if (!gst_audio_info_from_caps (&adder->info, caps))
    goto not_supported;

  if (GST_AUDIO_INFO_IS_INTEGER (&adder->info)) {
    switch (GST_AUDIO_INFO_WIDTH (&adder->info)) {
      case 8:
        adder->func = GST_AUDIO_INFO_IS_SIGNED (&adder->info) ?
            (GstLiveAdderFunction) add_int8 : (GstLiveAdderFunction) add_uint8;
        break;
      case 16:
        adder->func = GST_AUDIO_INFO_IS_SIGNED (&adder->info) ?
            (GstLiveAdderFunction) add_int16 : (GstLiveAdderFunction)
            add_uint16;
        break;
      case 32:
        adder->func = GST_AUDIO_INFO_IS_SIGNED (&adder->info) ?
            (GstLiveAdderFunction) add_int32 : (GstLiveAdderFunction)
            add_uint32;
        break;
      default:
        goto not_supported;
    }
  } else if (GST_AUDIO_INFO_IS_FLOAT (&adder->info)) {
    switch (GST_AUDIO_INFO_WIDTH (&adder->info)) {
      case 32:
        adder->func = (GstLiveAdderFunction) add_float32;
        break;
      case 64:
        adder->func = (GstLiveAdderFunction) add_float64;
        break;
      default:
        goto not_supported;
    }
  } else {
    goto not_supported;
  }

  GST_OBJECT_UNLOCK (adder);
  return TRUE;

  /* ERRORS */
not_supported:
  {
    GST_OBJECT_UNLOCK (adder);
    GST_DEBUG_OBJECT (adder, "unsupported format set as caps");
    return FALSE;
  }
}

static void
gst_live_adder_flush_start (GstLiveAdder * adder)
{
  GST_DEBUG_OBJECT (adder, "Disabling pop on queue");

  GST_OBJECT_LOCK (adder);
  /* mark ourselves as flushing */
  adder->srcresult = GST_FLOW_FLUSHING;

  /* Empty the queue */
  g_queue_foreach (adder->buffers, (GFunc) gst_mini_object_unref, NULL);
  while (g_queue_pop_head (adder->buffers));

  /* unlock clock, we just unschedule, the entry will be released by the
   * locking streaming thread. */
  if (adder->clock_id)
    gst_clock_id_unschedule (adder->clock_id);

  g_cond_broadcast (&adder->not_empty_cond);
  GST_OBJECT_UNLOCK (adder);
}

static gboolean
gst_live_adder_src_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstLiveAdder *adder = GST_LIVE_ADDER (parent);
  gboolean result = TRUE;

  if (mode == GST_PAD_MODE_PULL)
    return FALSE;

  if (active) {
    /* Mark as non flushing */
    GST_OBJECT_LOCK (adder);
    adder->srcresult = GST_FLOW_OK;
    GST_OBJECT_UNLOCK (adder);

    /* start pushing out buffers */
    GST_DEBUG_OBJECT (adder, "Starting task on srcpad");
    gst_pad_start_task (adder->srcpad,
        (GstTaskFunction) gst_live_adder_loop, adder, NULL);
  } else {
    /* make sure all data processing stops ASAP */
    gst_live_adder_flush_start (adder);

    /* NOTE this will hardlock if the state change is called from the src pad
     * task thread because we will _join() the thread. */
    GST_DEBUG_OBJECT (adder, "Stopping task on srcpad");
    result = gst_pad_stop_task (pad);
  }

  return result;
}

static gboolean
gst_live_adder_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstLiveAdder *adder = GST_LIVE_ADDER (parent);
  GstLiveAdderPadPrivate *padprivate = NULL;
  gboolean ret = TRUE;

  padprivate = gst_pad_get_element_private (pad);

  if (!padprivate)
    return FALSE;

  GST_LOG_OBJECT (adder, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_live_adder_setcaps (adder, pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;
      GstSegment livesegment;

      gst_event_parse_segment (event, &segment);

      /* we need time for now */
      if (segment->format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      /* now configure the values, we need these to time the release of the
       * buffers on the srcpad. */
      GST_OBJECT_LOCK (adder);
      gst_segment_copy_into (segment, &padprivate->segment);
      GST_OBJECT_UNLOCK (adder);
      gst_event_unref (event);

      gst_segment_init (&livesegment, GST_FORMAT_TIME);
      gst_pad_push_event (adder->srcpad, gst_event_new_segment (&livesegment));
      break;
    }
    case GST_EVENT_FLUSH_START:
      gst_live_adder_flush_start (adder);
      ret = gst_pad_push_event (adder->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_OBJECT_LOCK (adder);
      adder->next_timestamp = GST_CLOCK_TIME_NONE;
      reset_pad_private (pad);
      GST_OBJECT_UNLOCK (adder);
      ret = gst_pad_push_event (adder->srcpad, event);
      ret = gst_live_adder_src_activate_mode (adder->srcpad, GST_OBJECT (adder),
          GST_PAD_MODE_PUSH, TRUE);
      break;
    case GST_EVENT_EOS:
    {
      GST_OBJECT_LOCK (adder);

      ret = adder->srcresult == GST_FLOW_OK;
      if (ret && !padprivate->eos) {
        GST_DEBUG_OBJECT (adder, "queuing EOS");
        padprivate->eos = TRUE;
        g_cond_broadcast (&adder->not_empty_cond);
      } else if (padprivate->eos) {
        GST_DEBUG_OBJECT (adder, "dropping EOS, we are already EOS");
      } else {
        GST_DEBUG_OBJECT (adder, "dropping EOS, reason %s",
            gst_flow_get_name (adder->srcresult));
      }

      GST_OBJECT_UNLOCK (adder);

      gst_event_unref (event);
      break;
    }
    default:
      ret = gst_pad_push_event (adder->srcpad, event);
      break;
  }

done:

  return ret;

  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (adder, "received non TIME segment");
    ret = FALSE;
    goto done;
  }
}

static gboolean
gst_live_adder_query_pos_dur (GstLiveAdder * adder, GstFormat format,
    gboolean position, gint64 * outvalue)
{
  gint64 max = G_MININT64;
  gboolean res = TRUE;
  GstIterator *it;
  gboolean done = FALSE;


  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (adder));
  while (!done) {
    GstIteratorResult ires;
    GValue item = { 0 };

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (g_value_get_object (&item));
        gint64 value;
        gboolean curres;

        /* ask sink peer for duration */
        if (position)
          curres = gst_pad_peer_query_position (pad, format, &value);
        else
          curres = gst_pad_peer_query_duration (pad, format, &value);

        /* take max from all valid return values */
        /* Only if the format is the one we requested, otherwise ignore it ?
         */

        if (curres) {
          res &= curres;

          /* valid unknown length, stop searching */
          if (value == -1) {
            max = value;
            done = TRUE;
          } else if (value > max) {
            max = value;
          }
        }
        break;
      }
      case GST_ITERATOR_RESYNC:
        max = -1;
        res = TRUE;
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  if (res)
    *outvalue = max;

  return res;
}

/* FIXME:
 *
 * When we add a new stream (or remove a stream) the duration might
 * also become invalid again and we need to post a new DURATION
 * message to notify this fact to the parent.
 * For now we take the max of all the upstream elements so the simple
 * cases work at least somewhat.
 */
static gboolean
gst_live_adder_query_duration (GstLiveAdder * adder, GstQuery * query)
{
  GstFormat format;
  gint64 max;
  gboolean res;

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  res = gst_live_adder_query_pos_dur (adder, format, FALSE, &max);

  if (res) {
    /* and store the max */
    gst_query_set_duration (query, format, max);
  }

  return res;
}

static gboolean
gst_live_adder_query_position (GstLiveAdder * adder, GstQuery * query)
{
  GstFormat format;
  gint64 max;
  gboolean res;

  /* parse format */
  gst_query_parse_position (query, &format, NULL);

  res = gst_live_adder_query_pos_dur (adder, format, TRUE, &max);

  if (res) {
    /* and store the max */
    gst_query_set_position (query, format, max);
  }

  return res;
}



static gboolean
gst_live_adder_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstLiveAdder *adder = GST_LIVE_ADDER (parent);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      /* We need to send the query upstream and add the returned latency to our
       * own */
      GstClockTime min_latency = 0, max_latency = G_MAXUINT64;
      GValue item = { 0 };
      GstIterator *iter = NULL;
      gboolean done = FALSE;

      iter = gst_element_iterate_sink_pads (GST_ELEMENT (adder));

      while (!done) {
        switch (gst_iterator_next (iter, &item)) {
          case GST_ITERATOR_OK:
          {
            GstPad *sinkpad = GST_PAD (g_value_get_object (&item));
            GstClockTime pad_min_latency, pad_max_latency;
            gboolean pad_us_live;

            if (gst_pad_peer_query (sinkpad, query)) {
              gst_query_parse_latency (query, &pad_us_live, &pad_min_latency,
                  &pad_max_latency);

              res = TRUE;

              GST_DEBUG_OBJECT (adder, "Peer latency for pad %s: min %"
                  GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
                  GST_PAD_NAME (sinkpad),
                  GST_TIME_ARGS (pad_min_latency),
                  GST_TIME_ARGS (pad_max_latency));

              min_latency = MAX (pad_min_latency, min_latency);
              max_latency = MIN (pad_max_latency, max_latency);
            }
          }
            break;
          case GST_ITERATOR_RESYNC:
            min_latency = 0;
            max_latency = G_MAXUINT64;

            gst_iterator_resync (iter);
            break;
          case GST_ITERATOR_ERROR:
            GST_ERROR_OBJECT (adder, "Error looping sink pads");
            done = TRUE;
            break;
          case GST_ITERATOR_DONE:
            done = TRUE;
            break;
        }
      }
      gst_iterator_free (iter);

      if (res) {
        GstClockTime my_latency = adder->latency_ms * GST_MSECOND;
        GST_OBJECT_LOCK (adder);
        adder->peer_latency = min_latency;
        min_latency += my_latency;
        GST_OBJECT_UNLOCK (adder);

        /* Make sure we don't risk an overflow */
        if (max_latency < G_MAXUINT64 - my_latency)
          max_latency += my_latency;
        else
          max_latency = G_MAXUINT64;
        gst_query_set_latency (query, TRUE, min_latency, max_latency);
        GST_DEBUG_OBJECT (adder, "Calculated total latency : min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));
      }
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_live_adder_query_duration (adder, query);
      break;
    case GST_QUERY_POSITION:
      res = gst_live_adder_query_position (adder, query);
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static gboolean
gst_live_adder_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstLiveAdder *adder = GST_LIVE_ADDER (parent);
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter;
      GstCaps *result;

      gst_query_parse_caps (query, &filter);
      result = gst_live_adder_sink_getcaps (adder, pad, filter);
      gst_query_set_caps_result (query, result);
      gst_caps_unref (result);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static gboolean
forward_event_func (const GValue * item, GValue * ret, gpointer user_data)
{
  GstPad *pad = GST_PAD (g_value_get_object (item));
  GstEvent *event = user_data;

  gst_event_ref (event);
  GST_LOG_OBJECT (pad, "About to send event %s", GST_EVENT_TYPE_NAME (event));
  if (!gst_pad_push_event (pad, event)) {
    g_value_set_boolean (ret, FALSE);
    GST_WARNING_OBJECT (pad, "Sending event  %p (%s) failed.",
        event, GST_EVENT_TYPE_NAME (event));
  } else {
    GST_LOG_OBJECT (pad, "Sent event  %p (%s).",
        event, GST_EVENT_TYPE_NAME (event));
  }
  return TRUE;
}

/* forwards the event to all sinkpads, takes ownership of the
 * event
 *
 * Returns: TRUE if the event could be forwarded on all
 * sinkpads.
 */
static gboolean
forward_event (GstLiveAdder * adder, GstEvent * event)
{
  gboolean ret;
  GstIterator *it;
  GValue vret = { 0 };

  GST_LOG_OBJECT (adder, "Forwarding event %p (%s)", event,
      GST_EVENT_TYPE_NAME (event));

  ret = TRUE;

  g_value_init (&vret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&vret, TRUE);
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (adder));
  gst_iterator_fold (it, forward_event_func, &vret, event);
  gst_iterator_free (it);

  ret = g_value_get_boolean (&vret);

  return ret;
}


static gboolean
gst_live_adder_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstLiveAdder *adder = GST_LIVE_ADDER (parent);
  gboolean result;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      /* TODO : QoS might be tricky */
      result = FALSE;
      break;
    case GST_EVENT_NAVIGATION:
      /* TODO : navigation is rather pointless. */
      result = FALSE;
      break;
    default:
      /* just forward the rest for now */
      result = forward_event (adder, event);
      break;
  }

  gst_event_unref (event);

  return result;
}

static guint
gst_live_adder_length_from_duration (GstLiveAdder * adder,
    GstClockTime duration)
{
  guint64 ret = GST_AUDIO_INFO_BPF (&adder->info) *
      gst_util_uint64_scale_int_round (duration,
      GST_AUDIO_INFO_RATE (&adder->info), GST_SECOND);

  return (guint) ret;
}

static GstFlowReturn
gst_live_live_adder_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstLiveAdder *adder = GST_LIVE_ADDER (parent);
  GstLiveAdderPadPrivate *padprivate = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  GList *item = NULL;
  GstClockTime skip = 0;
  gint64 drift = 0;             /* Positive if new buffer after old buffer */

  GST_OBJECT_LOCK (adder);

  ret = adder->srcresult;

  GST_DEBUG ("Incoming buffer time:%" GST_TIME_FORMAT " duration:%"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (adder, "Passing non-ok result from src: %s",
        gst_flow_get_name (ret));
    gst_buffer_unref (buffer);
    goto out;
  }

  padprivate = gst_pad_get_element_private (pad);

  if (!padprivate) {
    ret = GST_FLOW_NOT_LINKED;
    gst_buffer_unref (buffer);
    goto out;
  }

  if (padprivate->eos) {
    GST_DEBUG_OBJECT (adder, "Received buffer after EOS");
    ret = GST_FLOW_EOS;
    gst_buffer_unref (buffer);
    goto out;
  }

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    goto invalid_timestamp;

  if (padprivate->segment.format == GST_FORMAT_UNDEFINED) {
    GST_WARNING_OBJECT (adder, "No new-segment received,"
        " initializing segment with time 0..-1");
    gst_segment_init (&padprivate->segment, GST_FORMAT_TIME);
  }

  buffer = gst_buffer_make_writable (buffer);

  drift = GST_BUFFER_TIMESTAMP (buffer) - padprivate->expected_timestamp;

  /* Just see if we receive invalid timestamp/durations */
  if (GST_CLOCK_TIME_IS_VALID (padprivate->expected_timestamp) &&
      !GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT) &&
      (drift != 0)) {
    GST_LOG_OBJECT (adder,
        "Timestamp discontinuity without the DISCONT flag set"
        " (expected %" GST_TIME_FORMAT ", got %" GST_TIME_FORMAT
        " drift:%" G_GINT64_FORMAT "ms)",
        GST_TIME_ARGS (padprivate->expected_timestamp),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)), drift / GST_MSECOND);

    /* We accept drifts of 10ms */
    if (ABS (drift) < (10 * GST_MSECOND)) {
      GST_DEBUG ("Correcting minor drift");
      GST_BUFFER_TIMESTAMP (buffer) = padprivate->expected_timestamp;
    }
  }


  /* If there is no duration, lets set one */
  if (!GST_BUFFER_DURATION_IS_VALID (buffer)) {
    GST_BUFFER_DURATION (buffer) = (gst_buffer_get_size (buffer) * GST_SECOND) /
        (GST_AUDIO_INFO_BPF (&adder->info) *
        GST_AUDIO_INFO_RATE (&adder->info));
    padprivate->expected_timestamp = GST_CLOCK_TIME_NONE;
  } else {
    padprivate->expected_timestamp = GST_BUFFER_TIMESTAMP (buffer) +
        GST_BUFFER_DURATION (buffer);
  }


  /*
   * Lets clip the buffer to the segment (so we don't have to worry about
   * cliping afterwards).
   * This should also guarantee us that we'll have valid timestamps and
   * durations afterwards
   */

  buffer = gst_audio_buffer_clip (buffer, &padprivate->segment,
      GST_AUDIO_INFO_RATE (&adder->info), GST_AUDIO_INFO_BPF (&adder->info));

  /* buffer can be NULL if it's completely outside of the segment */
  if (!buffer) {
    GST_DEBUG ("Buffer completely outside of configured segment, dropping it");
    goto out;
  }

  /*
   * Make sure all incoming buffers share the same timestamping
   */
  GST_BUFFER_TIMESTAMP (buffer) =
      gst_segment_to_running_time (&padprivate->segment,
      padprivate->segment.format, GST_BUFFER_TIMESTAMP (buffer));


  if (GST_CLOCK_TIME_IS_VALID (adder->next_timestamp) &&
      GST_BUFFER_TIMESTAMP (buffer) < adder->next_timestamp) {
    if (GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer) <
        adder->next_timestamp) {
      GST_DEBUG_OBJECT (adder, "Buffer is late, dropping (ts: %" GST_TIME_FORMAT
          " duration: %" GST_TIME_FORMAT ")",
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));
      gst_buffer_unref (buffer);
      goto out;
    } else {
      skip = adder->next_timestamp - GST_BUFFER_TIMESTAMP (buffer);
      GST_DEBUG_OBJECT (adder, "Buffer is partially late, skipping %"
          GST_TIME_FORMAT, GST_TIME_ARGS (skip));
    }
  }

  /* If our new buffer's head is higher than the queue's head, lets wake up,
   * we may not have to wait for as long
   */
  if (adder->clock_id &&
      g_queue_peek_head (adder->buffers) != NULL &&
      GST_BUFFER_TIMESTAMP (buffer) + skip <
      GST_BUFFER_TIMESTAMP (g_queue_peek_head (adder->buffers)))
    gst_clock_id_unschedule (adder->clock_id);

  for (item = g_queue_peek_head_link (adder->buffers);
      item; item = g_list_next (item)) {
    GstBuffer *oldbuffer = item->data;
    GstClockTime old_skip = 0;
    GstClockTime mix_duration = 0;
    GstClockTime mix_start = 0;
    GstClockTime mix_end = 0;
    GstMapInfo oldmap, map;

    /* We haven't reached our place yet */
    if (GST_BUFFER_TIMESTAMP (buffer) + skip >=
        GST_BUFFER_TIMESTAMP (oldbuffer) + GST_BUFFER_DURATION (oldbuffer))
      continue;

    /* We're past our place, lets insert ouselves here */
    if (GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer) <=
        GST_BUFFER_TIMESTAMP (oldbuffer))
      break;

    /* if we reach this spot, we have overlap, so we must mix */

    /* First make a subbuffer with the non-overlapping part */
    if (GST_BUFFER_TIMESTAMP (buffer) + skip < GST_BUFFER_TIMESTAMP (oldbuffer)) {
      GstBuffer *subbuffer = NULL;
      GstClockTime subbuffer_duration = GST_BUFFER_TIMESTAMP (oldbuffer) -
          (GST_BUFFER_TIMESTAMP (buffer) + skip);

      subbuffer = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL,
          gst_live_adder_length_from_duration (adder, skip),
          gst_live_adder_length_from_duration (adder, subbuffer_duration));

      GST_BUFFER_TIMESTAMP (subbuffer) = GST_BUFFER_TIMESTAMP (buffer) + skip;
      GST_BUFFER_DURATION (subbuffer) = subbuffer_duration;

      skip += subbuffer_duration;

      g_queue_insert_before (adder->buffers, item, subbuffer);
    }

    /* Now we are on the overlapping part */
    oldbuffer = gst_buffer_make_writable (oldbuffer);
    item->data = oldbuffer;

    old_skip = GST_BUFFER_TIMESTAMP (buffer) + skip -
        GST_BUFFER_TIMESTAMP (oldbuffer);

    mix_start = GST_BUFFER_TIMESTAMP (oldbuffer) + old_skip;

    if (GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer) <
        GST_BUFFER_TIMESTAMP (oldbuffer) + GST_BUFFER_DURATION (oldbuffer))
      mix_end = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    else
      mix_end = GST_BUFFER_TIMESTAMP (oldbuffer) +
          GST_BUFFER_DURATION (oldbuffer);

    mix_duration = mix_end - mix_start;

    if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
      GST_BUFFER_FLAG_UNSET (oldbuffer, GST_BUFFER_FLAG_GAP);
      gst_buffer_map (oldbuffer, &oldmap, GST_MAP_WRITE);
      gst_buffer_map (buffer, &map, GST_MAP_READ);
      adder->func (oldmap.data +
          gst_live_adder_length_from_duration (adder, old_skip),
          map.data +
          gst_live_adder_length_from_duration (adder, skip),
          gst_live_adder_length_from_duration (adder, mix_duration));
      gst_buffer_unmap (oldbuffer, &oldmap);
      gst_buffer_unmap (buffer, &map);
    }
    skip += mix_duration;
  }

  g_cond_broadcast (&adder->not_empty_cond);

  if (skip == GST_BUFFER_DURATION (buffer)) {
    gst_buffer_unref (buffer);
  } else {
    if (skip) {
      GstClockTime subbuffer_duration = GST_BUFFER_DURATION (buffer) - skip;
      GstClockTime subbuffer_ts = GST_BUFFER_TIMESTAMP (buffer) + skip;
      GstBuffer *new_buffer = gst_buffer_copy_region (buffer,
          GST_BUFFER_COPY_ALL,
          gst_live_adder_length_from_duration (adder, skip),
          gst_live_adder_length_from_duration (adder, subbuffer_duration));
      gst_buffer_unref (buffer);
      buffer = new_buffer;
      GST_BUFFER_PTS (buffer) = subbuffer_ts;
      GST_BUFFER_DURATION (buffer) = subbuffer_duration;
    }

    if (item)
      g_queue_insert_before (adder->buffers, item, buffer);
    else
      g_queue_push_tail (adder->buffers, buffer);
  }

out:

  GST_OBJECT_UNLOCK (adder);

  return ret;

invalid_timestamp:

  GST_OBJECT_UNLOCK (adder);
  gst_buffer_unref (buffer);
  GST_ELEMENT_ERROR (adder, STREAM, FAILED,
      ("Buffer without a valid timestamp received"),
      ("Invalid timestamp received on buffer"));

  return GST_FLOW_ERROR;
}

/*
 * This only works because the GstObject lock is taken
 *
 * It checks if all sink pads are EOS
 */
static gboolean
check_eos_locked (GstLiveAdder * adder)
{
  GList *item;

  /* We can't be EOS if we have no sinkpads */
  if (adder->sinkpads == NULL)
    return FALSE;

  for (item = adder->sinkpads; item; item = g_list_next (item)) {
    GstPad *pad = item->data;
    GstLiveAdderPadPrivate *padprivate = gst_pad_get_element_private (pad);

    if (padprivate && padprivate->eos != TRUE)
      return FALSE;
  }

  return TRUE;
}

static void
gst_live_adder_loop (gpointer data)
{
  GstLiveAdder *adder = GST_LIVE_ADDER (data);
  GstClockTime buffer_timestamp = 0;
  GstClockTime sync_time = 0;
  GstClock *clock = NULL;
  GstClockID id = NULL;
  GstClockReturn ret;
  GstBuffer *buffer = NULL;
  GstFlowReturn result;

  GST_OBJECT_LOCK (adder);

again:

  for (;;) {
    if (adder->srcresult != GST_FLOW_OK)
      goto flushing;
    if (!g_queue_is_empty (adder->buffers))
      break;
    if (check_eos_locked (adder))
      goto eos;
    g_cond_wait (&adder->not_empty_cond, GST_OBJECT_GET_LOCK (adder));
  }

  buffer_timestamp = GST_BUFFER_TIMESTAMP (g_queue_peek_head (adder->buffers));

  clock = GST_ELEMENT_CLOCK (adder);

  /* If we have no clock, then we can't do anything.. error */
  if (!clock) {
    if (adder->playing)
      goto no_clock;
    else
      goto push_buffer;
  }

  GST_DEBUG_OBJECT (adder, "sync to timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (buffer_timestamp));

  sync_time = buffer_timestamp + GST_ELEMENT_CAST (adder)->base_time;
  /* add latency, this includes our own latency and the peer latency. */
  sync_time += adder->latency_ms * GST_MSECOND;
  sync_time += adder->peer_latency;

  /* create an entry for the clock */
  id = adder->clock_id = gst_clock_new_single_shot_id (clock, sync_time);
  GST_OBJECT_UNLOCK (adder);

  ret = gst_clock_id_wait (id, NULL);

  GST_OBJECT_LOCK (adder);

  /* and free the entry */
  gst_clock_id_unref (id);
  adder->clock_id = NULL;

  /* at this point, the clock could have been unlocked by a timeout, a new
   * head element was added to the queue or because we are shutting down. Check
   * for shutdown first. */

  if (adder->srcresult != GST_FLOW_OK)
    goto flushing;

  if (ret == GST_CLOCK_UNSCHEDULED) {
    GST_DEBUG_OBJECT (adder,
        "Wait got unscheduled, will retry to push with new buffer");
    goto again;
  }

  if (ret != GST_CLOCK_OK && ret != GST_CLOCK_EARLY)
    goto clock_error;

push_buffer:

  buffer = g_queue_pop_head (adder->buffers);

  if (!buffer)
    goto again;

  /*
   * We make sure the timestamps are exactly contiguous
   * If its only small skew (due to rounding errors), we correct it
   * silently. Otherwise we put the discont flag
   */
  if (GST_CLOCK_TIME_IS_VALID (adder->next_timestamp) &&
      GST_BUFFER_TIMESTAMP (buffer) != adder->next_timestamp) {
    GstClockTimeDiff diff = GST_CLOCK_DIFF (GST_BUFFER_TIMESTAMP (buffer),
        adder->next_timestamp);
    if (diff < 0)
      diff = -diff;

    if (diff < GST_SECOND / GST_AUDIO_INFO_RATE (&adder->info)) {
      GST_BUFFER_TIMESTAMP (buffer) = adder->next_timestamp;
      GST_DEBUG_OBJECT (adder, "Correcting slight skew");
      GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
    } else {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      GST_DEBUG_OBJECT (adder, "Expected buffer at %" GST_TIME_FORMAT
          ", but is at %" GST_TIME_FORMAT ", setting discont",
          GST_TIME_ARGS (adder->next_timestamp),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
    }
  } else {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DISCONT);
  }

  GST_BUFFER_OFFSET (buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (buffer) = GST_BUFFER_OFFSET_NONE;

  if (GST_BUFFER_DURATION_IS_VALID (buffer))
    adder->next_timestamp = GST_BUFFER_TIMESTAMP (buffer) +
        GST_BUFFER_DURATION (buffer);
  else
    adder->next_timestamp = GST_CLOCK_TIME_NONE;
  GST_OBJECT_UNLOCK (adder);

  GST_LOG_OBJECT (adder, "About to push buffer time:%" GST_TIME_FORMAT
      " duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));

  result = gst_pad_push (adder->srcpad, buffer);
  if (result != GST_FLOW_OK)
    goto pause;

  return;

flushing:
  {
    GST_DEBUG_OBJECT (adder, "we are flushing");
    gst_pad_pause_task (adder->srcpad);
    GST_OBJECT_UNLOCK (adder);
    return;
  }

clock_error:
  {
    gst_pad_pause_task (adder->srcpad);
    GST_OBJECT_UNLOCK (adder);
    GST_ELEMENT_ERROR (adder, STREAM, MUX, ("Error with the clock"),
        ("Error with the clock: %d", ret));
    GST_ERROR_OBJECT (adder, "Error with the clock: %d", ret);
    return;
  }

no_clock:
  {
    gst_pad_pause_task (adder->srcpad);
    GST_OBJECT_UNLOCK (adder);
    GST_ELEMENT_ERROR (adder, STREAM, MUX, ("No available clock"),
        ("No available clock"));
    GST_ERROR_OBJECT (adder, "No available clock");
    return;
  }

pause:
  {
    GST_DEBUG_OBJECT (adder, "pausing task, reason %s",
        gst_flow_get_name (result));

    GST_OBJECT_LOCK (adder);

    /* store result */
    adder->srcresult = result;
    /* we don't post errors or anything because upstream will do that for us
     * when we pass the return value upstream. */
    gst_pad_pause_task (adder->srcpad);
    GST_OBJECT_UNLOCK (adder);
    return;
  }

eos:
  {
    /* store result, we are flushing now */
    GST_DEBUG_OBJECT (adder, "We are EOS, pushing EOS downstream");
    adder->srcresult = GST_FLOW_EOS;
    gst_pad_pause_task (adder->srcpad);
    GST_OBJECT_UNLOCK (adder);
    gst_pad_push_event (adder->srcpad, gst_event_new_eos ());
    return;
  }
}

static GstPad *
gst_live_adder_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * ignored_name, const GstCaps * caps)
{
  gchar *name;
  GstLiveAdder *adder;
  GstPad *newpad;
  gint padcount;
  GstLiveAdderPadPrivate *padprivate = NULL;

  if (templ->direction != GST_PAD_SINK)
    goto not_sink;

  adder = GST_LIVE_ADDER (element);

  /* increment pad counter */
#if GLIB_CHECK_VERSION(2,29,5)
  padcount = g_atomic_int_add (&adder->padcount, 1);
#else
  padcount = g_atomic_int_exchange_and_add (&adder->padcount, 1);
#endif

  name = g_strdup_printf ("sink_%u", padcount);
  newpad = gst_pad_new_from_template (templ, name);
  GST_DEBUG_OBJECT (adder, "request new pad %s", name);
  g_free (name);

  gst_pad_set_event_function (newpad,
      GST_DEBUG_FUNCPTR (gst_live_adder_sink_event));
  gst_pad_set_query_function (newpad,
      GST_DEBUG_FUNCPTR (gst_live_adder_sink_query));

  padprivate = g_new0 (GstLiveAdderPadPrivate, 1);

  gst_segment_init (&padprivate->segment, GST_FORMAT_UNDEFINED);
  padprivate->eos = FALSE;
  padprivate->expected_timestamp = GST_CLOCK_TIME_NONE;

  gst_pad_set_element_private (newpad, padprivate);

  gst_pad_set_chain_function (newpad, gst_live_live_adder_chain);


  if (!gst_pad_set_active (newpad, TRUE))
    goto could_not_activate;

  /* takes ownership of the pad */
  if (!gst_element_add_pad (GST_ELEMENT (adder), newpad))
    goto could_not_add;

  GST_OBJECT_LOCK (adder);
  adder->sinkpads = g_list_prepend (adder->sinkpads, newpad);
  GST_OBJECT_UNLOCK (adder);

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
    g_free (padprivate);
    gst_object_unref (newpad);
    return NULL;
  }
could_not_activate:
  {
    GST_DEBUG_OBJECT (adder, "could not activate new pad");
    g_free (padprivate);
    gst_object_unref (newpad);
    return NULL;
  }
}

static void
gst_live_adder_release_pad (GstElement * element, GstPad * pad)
{
  GstLiveAdder *adder;
  GstLiveAdderPadPrivate *padprivate;

  adder = GST_LIVE_ADDER (element);

  GST_DEBUG_OBJECT (adder, "release pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  GST_OBJECT_LOCK (element);
  padprivate = gst_pad_get_element_private (pad);
  gst_pad_set_element_private (pad, NULL);
  adder->sinkpads = g_list_remove_all (adder->sinkpads, pad);
  GST_OBJECT_UNLOCK (element);

  g_free (padprivate);

  gst_element_remove_pad (element, pad);
}

static void
reset_pad_private (GstPad * pad)
{
  GstLiveAdderPadPrivate *padprivate;

  padprivate = gst_pad_get_element_private (pad);

  if (!padprivate)
    return;

  gst_segment_init (&padprivate->segment, GST_FORMAT_UNDEFINED);

  padprivate->expected_timestamp = GST_CLOCK_TIME_NONE;
  padprivate->eos = FALSE;
}

static GstStateChangeReturn
gst_live_adder_change_state (GstElement * element, GstStateChange transition)
{
  GstLiveAdder *adder;
  GstStateChangeReturn ret;

  adder = GST_LIVE_ADDER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (adder);
      adder->segment_pending = TRUE;
      adder->peer_latency = 0;
      adder->next_timestamp = GST_CLOCK_TIME_NONE;
      g_list_foreach (adder->sinkpads, (GFunc) reset_pad_private, NULL);
      GST_OBJECT_UNLOCK (adder);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_OBJECT_LOCK (adder);
      adder->playing = FALSE;
      GST_OBJECT_UNLOCK (adder);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_live_adder_parent_class)->change_state (element,
      transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_OBJECT_LOCK (adder);
      adder->playing = TRUE;
      GST_OBJECT_UNLOCK (adder);
      break;
    default:
      break;
  }

  return ret;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "liveadder", GST_RANK_NONE,
          GST_TYPE_LIVE_ADDER)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    liveadder,
    "Adds multiple live discontinuous streams",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
