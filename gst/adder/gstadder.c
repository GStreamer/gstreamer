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
 * <refsect2>
 * <para>
 * The Adder allows to mix several streams into one by adding the data.
 * Mixed data is clamped to the min/max values of the data format.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch audiotestsrc freq=100 ! adder name=mix ! audioconvert ! alsasink audiotestsrc freq=500 ! mix.
 * </programlisting>
 * This pipeline produces two sine waves mixed together.
 * </para>
 * <para>
 * The Adder currently mixes all data received on the sinkpads as soon as possible
 * without trying to synchronize the streams.
 * </para>
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

static void gst_adder_class_init (GstAdderClass * klass);
static void gst_adder_init (GstAdder * adder);
static void gst_adder_finalize (GObject * object);

static gboolean gst_adder_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_adder_query (GstPad * pad, GstQuery * query);
static gboolean gst_adder_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_adder_sink_event (GstPad * pad, GstEvent * event);

static GstPad *gst_adder_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused);
static void gst_adder_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_adder_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_adder_collected (GstCollectPads * pads,
    gpointer user_data);

static GstElementClass *parent_class = NULL;

GType
gst_adder_get_type (void)
{
  static GType adder_type = 0;

  if (G_UNLIKELY (adder_type == 0)) {
    static const GTypeInfo adder_info = {
      sizeof (GstAdderClass), NULL, NULL,
      (GClassInitFunc) gst_adder_class_init, NULL, NULL,
      sizeof (GstAdder), 0,
      (GInstanceInitFunc) gst_adder_init,
    };

    adder_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAdder",
        &adder_info, 0);
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "adder", 0,
        "audio channel mixing element");
  }
  return adder_type;
}

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
MAKE_FUNC (add_int32, gint32, gint64, MIN_INT_32, MAX_INT_32)
MAKE_FUNC (add_int16, gint16, gint32, MIN_INT_16, MAX_INT_16)
MAKE_FUNC (add_int8, gint8, gint16, MIN_INT_8, MAX_INT_8)
MAKE_FUNC (add_uint32, guint32, guint64, MIN_UINT_32, MAX_UINT_32)
MAKE_FUNC (add_uint16, guint16, guint32, MIN_UINT_16, MAX_UINT_16)
MAKE_FUNC (add_uint8, guint8, guint16, MIN_UINT_8, MAX_UINT_8)
MAKE_FUNC_NC (add_float64, gdouble, gdouble)
MAKE_FUNC_NC (add_float32, gfloat, gfloat)
/* *INDENT-ON* */

/* we can only accept caps that we and downstream can handle. */
static GstCaps *
gst_adder_sink_getcaps (GstPad * pad)
{
  GstAdder *adder;
  GstCaps *result, *peercaps, *sinkcaps;

  adder = GST_ADDER (GST_PAD_PARENT (pad));

  GST_OBJECT_LOCK (adder);
  /* get the downstream possible caps */
  peercaps = gst_pad_peer_get_caps (adder->srcpad);
  /* get the allowed caps on this sinkpad, we use the fixed caps function so
   * that it does not call recursively in this function. */
  sinkcaps = gst_pad_get_fixed_caps_func (pad);
  if (peercaps) {
    /* if the peer has caps, intersect */
    GST_DEBUG_OBJECT (adder, "intersecting peer and template caps");
    result = gst_caps_intersect (peercaps, sinkcaps);
    gst_caps_unref (peercaps);
    gst_caps_unref (sinkcaps);
  } else {
    /* the peer has no caps (or there is no peer), just use the allowed caps
     * of this sinkpad. */
    GST_DEBUG_OBJECT (adder, "no peer caps, using sinkcaps");
    result = sinkcaps;
  }
  GST_OBJECT_UNLOCK (adder);

  return result;
}

/* the first caps we receive on any of the sinkpads will define the caps for all
 * the other sinkpads because we can only mix streams with the same caps.
 * */
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
    GST_DEBUG_OBJECT (adder, "parse_caps sets adder to format int");
    adder->format = GST_ADDER_FORMAT_INT;
    gst_structure_get_int (structure, "width", &adder->width);
    gst_structure_get_int (structure, "depth", &adder->depth);
    gst_structure_get_int (structure, "endianness", &adder->endianness);
    gst_structure_get_boolean (structure, "signed", &adder->is_signed);

    if (adder->endianness != G_BYTE_ORDER)
      goto not_supported;

    switch (adder->width) {
      case 8:
        adder->func = (adder->is_signed ?
            (GstAdderFunction) add_int8 : (GstAdderFunction) add_uint8);
        break;
      case 16:
        adder->func = (adder->is_signed ?
            (GstAdderFunction) add_int16 : (GstAdderFunction) add_uint16);
        break;
      case 32:
        adder->func = (adder->is_signed ?
            (GstAdderFunction) add_int32 : (GstAdderFunction) add_uint32);
        break;
      default:
        goto not_supported;
    }
  } else if (strcmp (media_type, "audio/x-raw-float") == 0) {
    GST_DEBUG_OBJECT (adder, "parse_caps sets adder to format float");
    adder->format = GST_ADDER_FORMAT_FLOAT;
    gst_structure_get_int (structure, "width", &adder->width);
    gst_structure_get_int (structure, "endianness", &adder->endianness);

    if (adder->endianness != G_BYTE_ORDER)
      goto not_supported;

    switch (adder->width) {
      case 32:
        adder->func = (GstAdderFunction) add_float32;
        break;
      case 64:
        adder->func = (GstAdderFunction) add_float64;
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

  if (res) {
    /* and store the max */
    gst_query_set_duration (query, format, max);
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
    default:
      /* FIXME, needs a custom query handler because we have multiple
       * sinkpads */
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (adder);
  return res;
}

static gboolean
forward_event_func (GstPad * pad, GValue * ret, GstEvent * event)
{
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
  gst_object_unref (pad);
  return TRUE;
}

/* forwards the event to all sinkpads, takes ownership of the
 * event
 *
 * Returns: TRUE if the event could be forwarded on all
 * sinkpads.
 */
static gboolean
forward_event (GstAdder * adder, GstEvent * event)
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
  gst_iterator_fold (it, (GstIteratorFoldFunction) forward_event_func, &vret,
      event);
  gst_iterator_free (it);
  gst_event_unref (event);

  ret = g_value_get_boolean (&vret);

  return ret;
}

static gboolean
gst_adder_src_event (GstPad * pad, GstEvent * event)
{
  GstAdder *adder;
  gboolean result;

  adder = GST_ADDER (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      /* QoS might be tricky */
      result = FALSE;
      break;
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      GstSeekType curtype;
      gint64 cur;

      /* parse the seek parameters */
      gst_event_parse_seek (event, &adder->segment_rate, NULL, &flags, &curtype,
          &cur, NULL, NULL);

      /* check if we are flushing */
      if (flags & GST_SEEK_FLAG_FLUSH) {
        /* make sure we accept nothing anymore and return WRONG_STATE */
        gst_collect_pads_set_flushing (adder->collect, TRUE);

        /* flushing seek, start flush downstream, the flush will be done
         * when all pads received a FLUSH_STOP. */
        gst_pad_push_event (adder->srcpad, gst_event_new_flush_start ());
      }

      /* now wait for the collected to be finished and mark a new
       * segment */
      GST_OBJECT_LOCK (adder->collect);
      if (curtype == GST_SEEK_TYPE_SET)
        adder->segment_position = cur;
      else
        adder->segment_position = 0;
      adder->segment_pending = TRUE;
      GST_OBJECT_UNLOCK (adder->collect);

      result = forward_event (adder, event);
      break;
    }
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      result = FALSE;
      break;
    default:
      /* just forward the rest for now */
      result = forward_event (adder, event);
      break;
  }
  gst_object_unref (adder);

  return result;
}

static gboolean
gst_adder_sink_event (GstPad * pad, GstEvent * event)
{
  GstAdder *adder;
  gboolean ret;

  adder = GST_ADDER (gst_pad_get_parent (pad));

  GST_DEBUG ("Got %s event on pad %s:%s", GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      /* mark a pending new segment. This event is synchronized
       * with the streaming thread so we can safely update the
       * variable without races. It's somewhat weird because we
       * assume the collectpads forwarded the FLUSH_STOP past us
       * and downstream (using our source pad, the bastard!).
       */
      adder->segment_pending = TRUE;
      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  ret = adder->collect_event (pad, event);

  gst_object_unref (adder);
  return ret;
}

static void
gst_adder_class_init (GstAdderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_adder_finalize;

  gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_adder_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_adder_sink_template));
  gst_element_class_set_details_simple (gstelement_class, "Adder",
      "Generic/Audio",
      "Add N audio channels together", "Thomas <thomas@apestaart.org>");

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->request_new_pad = gst_adder_request_new_pad;
  gstelement_class->release_pad = gst_adder_release_pad;
  gstelement_class->change_state = gst_adder_change_state;
}

static void
gst_adder_init (GstAdder * adder)
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

  /* keep track of the sinkpads requested */
  adder->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (adder->collect,
      GST_DEBUG_FUNCPTR (gst_adder_collected), adder);
}

static void
gst_adder_finalize (GObject * object)
{
  GstAdder *adder = GST_ADDER (object);

  gst_object_unref (adder->collect);
  adder->collect = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
  padcount = g_atomic_int_exchange_and_add (&adder->padcount, 1);

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

static GstFlowReturn
gst_adder_collected (GstCollectPads * pads, gpointer user_data)
{
  /*
   * combine channels by adding sample values
   * basic algorithm :
   * - this function is called when all pads have a buffer
   * - get available bytes on all pads.
   * - repeat for each input pad :
   *   - read available bytes, copy or add to target buffer
   *   - if there's an EOS event, remove the input channel
   * - push out the output buffer
   */
  GstAdder *adder;
  guint size;
  GSList *collected;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  gpointer outbytes;
  gboolean empty = TRUE;

  adder = GST_ADDER (user_data);

  /* this is fatal */
  if (G_UNLIKELY (adder->func == NULL))
    goto not_negotiated;

  /* get available bytes for reading, this can be 0 which could mean
   * empty buffers or EOS, which we will catch when we loop over the
   * pads. */
  size = gst_collect_pads_available (pads);

  GST_LOG_OBJECT (adder,
      "starting to cycle through channels, %d bytes available (bps = %d)", size,
      adder->bps);

  outbuf = NULL;
  outbytes = NULL;

  for (collected = pads->data; collected; collected = g_slist_next (collected)) {
    GstCollectData *data;
    guint8 *bytes;
    guint len;
    GstBuffer *inbuf;

    data = (GstCollectData *) collected->data;

    /* get a subbuffer of size bytes */
    inbuf = gst_collect_pads_take_buffer (pads, data, size);
    /* NULL means EOS or an empty buffer so we still need to flush in
     * case of an empty buffer. */
    if (inbuf == NULL) {
      GST_LOG_OBJECT (adder, "channel %p: no bytes available", data);
      goto next;
    }

    bytes = GST_BUFFER_DATA (inbuf);
    len = GST_BUFFER_SIZE (inbuf);

    if (outbuf == NULL) {
      GST_LOG_OBJECT (adder, "channel %p: making output buffer of %d bytes",
          data, size);

      /* first buffer, alloc size bytes. FIXME, we can easily subbuffer
       * and _make_writable. */
      outbuf = gst_buffer_new_and_alloc (size);
      outbytes = GST_BUFFER_DATA (outbuf);
      gst_buffer_set_caps (outbuf, GST_PAD_CAPS (adder->srcpad));

      if (!GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
        /* clear if we are only going to fill a partial buffer */
        if (G_UNLIKELY (size > len))
          memset (outbytes, 0, size);

        GST_LOG_OBJECT (adder, "channel %p: copying %d bytes from data %p",
            data, len, bytes);

        /* and copy the data into it */
        memcpy (outbytes, bytes, len);
        empty = FALSE;
      } else {
        GST_LOG_OBJECT (adder, "channel %p: zeroing %d bytes from data %p",
            data, len, bytes);
        memset (outbytes, 0, size);
      }
    } else {
      if (!GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
        GST_LOG_OBJECT (adder, "channel %p: mixing %d bytes from data %p",
            data, len, bytes);
        /* other buffers, need to add them */
        adder->func ((gpointer) outbytes, (gpointer) bytes, len);
        empty = FALSE;
      } else {
        GST_LOG_OBJECT (adder, "channel %p: skipping %d bytes from data %p",
            data, len, bytes);
      }
    }
  next:
    if (inbuf)
      gst_buffer_unref (inbuf);
  }

  /* can only happen when no pads to collect or all EOS */
  if (outbuf == NULL)
    goto eos;

  /* our timestamping is very simple, just an ever incrementing
   * counter, the new segment time will take care of their respective
   * stream time. */
  if (adder->segment_pending) {
    GstEvent *event;

    /* FIXME, use rate/applied_rate as set on all sinkpads.
     * - currently we just set rate as received from last seek-event
     * We could potentially figure out the duration as well using
     * the current segment positions and the stated stop positions.
     * Also we just start from stream time 0 which is rather
     * weird. For non-synchronized mixing, the time should be
     * the min of the stream times of all received segments,
     * rationale being that the duration is at least going to
     * be as long as the earliest stream we start mixing. This
     * would also be correct for synchronized mixing but then
     * the later streams would be delayed until the stream times
     * match.
     */
    event = gst_event_new_new_segment_full (FALSE, adder->segment_rate,
        1.0, GST_FORMAT_TIME, adder->timestamp, -1, adder->segment_position);

    gst_pad_push_event (adder->srcpad, event);
    adder->segment_pending = FALSE;
    adder->segment_position = 0;
  }

  /* set timestamps on the output buffer */
  GST_BUFFER_TIMESTAMP (outbuf) = adder->timestamp;
  GST_BUFFER_OFFSET (outbuf) = adder->offset;

  /* for the next timestamp, use the sample counter, which will
   * never accumulate rounding errors */
  adder->offset += size / adder->bps;
  adder->timestamp = gst_util_uint64_scale_int (adder->offset,
      GST_SECOND, adder->rate);

  /* now we can set the duration of the buffer */
  GST_BUFFER_DURATION (outbuf) = adder->timestamp -
      GST_BUFFER_TIMESTAMP (outbuf);

  /* if we only processed silence, mark output again as silence */
  if (empty)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_GAP);

  /* send it out */
  GST_LOG_OBJECT (adder, "pushing outbuf, timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
  ret = gst_pad_push (adder->srcpad, outbuf);

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
      adder->segment_pending = TRUE;
      adder->segment_position = 0;
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
