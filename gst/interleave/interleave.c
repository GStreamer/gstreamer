/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2007 Andy Wingo <wingo at pobox.com>
 *                    2008 Sebastian Dröge <slomo@circular-chaos.rg>
 *
 * interleave.c: interleave samples, mostly based on adder.
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

/* TODO:
 *       - handle caps changes
 *       - set channel positions / keep from upstream
 *       - handle more queries
 */

/**
 * SECTION:element-interleave
 *
 * <refsect2>
 * <para>
 * Merges separate mono inputs into one interleaved stream.
 * </para>
 * <para>
 * This element handles all raw floating point sample formats and all signed integer sample formats. The first
 * caps on one of the sinkpads will set the caps of the output so usually an audioconvert element should be
 * placed before every sinkpad of interleave.
 * </para>
 * <para>
 * It's possible to change the number of channels while the pipeline is running by adding or removing
 * some of the request pads but this will change the caps of the output buffers. Changing the input
 * caps is _not_ supported yet.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch-0.10 filesrc location=file.mp3 ! decodebin ! audioconvert ! "audio/x-raw-int,channels=2" ! deinterleave name=d  interleave name=i ! audioconvert ! wavenc ! filesink location=test.wav    d.src0 ! queue ! audioconvert ! i.sink1    d.src1 ! queue ! audioconvert ! i.sink0
 * </programlisting>
 * Decodes and deinterleaves a Stereo MP3 file into separate channels and then interleaves the channels
 * again to a WAV file with the channel with the channels exchanged.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch-0.10 interleave name=i ! audioconvert ! wavenc ! filesink location=file.wav  filesrc location=file1.wav ! decodebin ! audioconvert ! "audio/x-raw-int,channels=1" ! queue ! i.sink0   filesrc location=file2.wav ! decodebin ! audioconvert ! "audio/x-raw-int,channels=1" ! queue ! i.sink1
 * </programlisting>
 * Interleaves two Mono WAV files to a single Stereo WAV file.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>
#include "interleave.h"

GST_DEBUG_CATEGORY_STATIC (gst_interleave_debug);
#define GST_CAT_DEFAULT gst_interleave_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) 1, "
        "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) [ 1, 32 ], "
        "signed = (boolean) true; "
        "audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) 1, "
        "endianness = (int) { LITTLE_ENDIAN , BIG_ENDIAN }, "
        "width = (int) { 32, 64 }")
    );
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) [ 1, 32 ], "
        "signed = (boolean) true; "
        "audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) { LITTLE_ENDIAN , BIG_ENDIAN }, "
        "width = (int) { 32, 64 }")
    );

#define MAKE_FUNC(type) \
static void interleave_##type (guint##type *out, guint##type *in, \
    guint stride, guint nframes) \
{ \
  gint i; \
  \
  for (i = 0; i < nframes; i++) { \
    *out = in[i]; \
    out += stride; \
  } \
}

MAKE_FUNC (8);
MAKE_FUNC (16);
MAKE_FUNC (32);
MAKE_FUNC (64);

static void
interleave_24 (guint8 * out, guint8 * in, guint stride, guint nframes)
{
  gint i;

  for (i = 0; i < nframes; i++) {
    memcpy (out, in, 3);
    out += stride * 3;
    in += 3;
  }
}

typedef struct
{
  GstCollectData data;
  guint channel;
} GstInterleaveCollectData;

GST_BOILERPLATE (GstInterleave, gst_interleave, GstElement, GST_TYPE_ELEMENT);

static GstPad *gst_interleave_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_interleave_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn gst_interleave_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_interleave_src_query (GstPad * pad, GstQuery * query);
static gboolean gst_interleave_src_event (GstPad * pad, GstEvent * event);

static gboolean gst_interleave_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_interleave_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_interleave_sink_getcaps (GstPad * pad);

static GstFlowReturn gst_interleave_collected (GstCollectPads * pads,
    GstInterleave * self);

static void
gst_interleave_finalize (GObject * object)
{
  GstInterleave *self = GST_INTERLEAVE (object);

  if (self->collect) {
    gst_object_unref (self->collect);
    self->collect = NULL;
  }

  gst_caps_replace (&self->sinkcaps, NULL);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_interleave_base_init (gpointer g_class)
{
  gst_element_class_set_details_simple (g_class, "Audio interleaver",
      "Filter/Converter/Audio",
      "Folds many mono channels into one interleaved audio stream",
      "Andy Wingo <wingo at pobox.com>, "
      "Sebastian Dröge <slomo@circular-chaos.org>");

  gst_element_class_add_pad_template (g_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (g_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_interleave_class_init (GstInterleaveClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_interleave_debug, "interleave", 0,
      "interleave element");

  gobject_class->finalize = gst_interleave_finalize;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_interleave_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_interleave_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_interleave_change_state);
}

static void
gst_interleave_init (GstInterleave * self, GstInterleaveClass * klass)
{
  self->src = gst_pad_new_from_static_template (&src_template, "src");

  gst_pad_set_query_function (self->src,
      GST_DEBUG_FUNCPTR (gst_interleave_src_query));
  gst_pad_set_event_function (self->src,
      GST_DEBUG_FUNCPTR (gst_interleave_src_event));

  gst_element_add_pad (GST_ELEMENT (self), self->src);

  self->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (self->collect,
      (GstCollectPadsFunction) gst_interleave_collected, self);
}

static GstPad *
gst_interleave_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * req_name)
{
  GstInterleave *self = GST_INTERLEAVE (element);
  GstPad *new_pad;
  gchar *pad_name;
  gint channels;
  GstInterleaveCollectData *cdata;

  if (templ->direction != GST_PAD_SINK)
    goto not_sink_pad;

  channels = g_atomic_int_exchange_and_add (&self->channels, 1);

  pad_name = g_strdup_printf ("sink%d", channels);
  new_pad = gst_pad_new_from_template (templ, pad_name);
  GST_DEBUG_OBJECT (self, "requested new pad %s", pad_name);
  g_free (pad_name);

  gst_pad_set_setcaps_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_interleave_sink_setcaps));
  gst_pad_set_getcaps_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_interleave_sink_getcaps));

  cdata = (GstInterleaveCollectData *)
      gst_collect_pads_add_pad (self->collect, new_pad,
      sizeof (GstInterleaveCollectData));
  cdata->channel = channels;

  /* FIXME: hacked way to override/extend the event function of
   * GstCollectPads; because it sets its own event function giving the
   * element no access to events */
  self->collect_event = (GstPadEventFunction) GST_PAD_EVENTFUNC (new_pad);
  gst_pad_set_event_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_interleave_sink_event));

  if (!gst_element_add_pad (element, new_pad))
    goto could_not_add;

  /* Update the src caps if we already have them */
  if (self->sinkcaps) {
    GstCaps *srccaps;
    GstStructure *s;

    /* Take lock to make sure processing finishes first */
    GST_OBJECT_LOCK (self->collect);

    srccaps = gst_caps_copy (self->sinkcaps);
    s = gst_caps_get_structure (srccaps, 0);

    gst_structure_set (s, "channels", G_TYPE_INT, self->channels, NULL);

    gst_pad_set_caps (self->src, srccaps);
    gst_caps_unref (srccaps);

    GST_OBJECT_UNLOCK (self->collect);
  }

  return new_pad;

  /* errors */
not_sink_pad:
  {
    g_warning ("interleave: requested new pad that is not a SINK pad\n");
    return NULL;
  }
could_not_add:
  {
    GST_DEBUG_OBJECT (self, "could not add pad %s", GST_PAD_NAME (new_pad));
    gst_collect_pads_remove_pad (self->collect, new_pad);
    gst_object_unref (new_pad);
    return NULL;
  }
}

static void
gst_interleave_release_pad (GstElement * element, GstPad * pad)
{
  GstInterleave *self = GST_INTERLEAVE (element);

  /* Take lock to make sure we're not changing this when processing buffers */
  GST_OBJECT_LOCK (self->collect);

  self->channels--;

  /* Update the src caps if we already have them */
  if (self->sinkcaps) {
    GstCaps *srccaps;
    GstStructure *s;
    GSList *l;
    gint i = 0;

    srccaps = gst_caps_copy (self->sinkcaps);
    s = gst_caps_get_structure (srccaps, 0);

    gst_structure_set (s, "channels", G_TYPE_INT, self->channels, NULL);

    gst_pad_set_caps (self->src, srccaps);
    gst_caps_unref (srccaps);

    /* Update channel numbers */
    for (l = self->collect->data; l != NULL; l = l->next) {
      GstInterleaveCollectData *cdata = l->data;

      if (cdata == NULL || cdata->data.pad == pad)
        continue;
      cdata->channel = i;
      i++;
    }
  }

  GST_OBJECT_UNLOCK (self->collect);

  gst_collect_pads_remove_pad (self->collect, pad);
  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_interleave_change_state (GstElement * element, GstStateChange transition)
{
  GstInterleave *self;
  GstStateChangeReturn ret;

  self = GST_INTERLEAVE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->timestamp = 0;
      self->offset = 0;
      self->segment_pending = TRUE;
      self->segment_position = 0;
      self->segment_rate = 1.0;
      gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
      gst_collect_pads_start (self->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (self->collect);
      gst_pad_set_caps (self->src, NULL);
      gst_caps_replace (&self->sinkcaps, NULL);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
__remove_channels (GstCaps * caps)
{
  GstStructure *s;
  gint i, size;

  size = gst_caps_get_size (caps);
  for (i = 0; i < size; i++) {
    s = gst_caps_get_structure (caps, i);
    gst_structure_remove_field (s, "channel-positions");
    gst_structure_remove_field (s, "channels");
  }
}

static void
__set_channels (GstCaps * caps, gint channels)
{
  GstStructure *s;
  gint i, size;

  size = gst_caps_get_size (caps);
  for (i = 0; i < size; i++) {
    s = gst_caps_get_structure (caps, i);
    if (channels > 0)
      gst_structure_set (s, "channels", G_TYPE_INT, channels, NULL);
    else
      gst_structure_set (s, "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
  }
}

/* we can only accept caps that we and downstream can handle. */
static GstCaps *
gst_interleave_sink_getcaps (GstPad * pad)
{
  GstInterleave *self = GST_INTERLEAVE (gst_pad_get_parent (pad));
  GstCaps *result, *peercaps, *sinkcaps;

  GST_OBJECT_LOCK (self);

  /* If we already have caps on one of the sink pads return them */
  if (self->sinkcaps) {
    result = gst_caps_copy (self->sinkcaps);
  } else {
    /* get the downstream possible caps */
    peercaps = gst_pad_peer_get_caps (self->src);
    /* get the allowed caps on this sinkpad */
    sinkcaps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
    __remove_channels (sinkcaps);
    if (peercaps) {
      __remove_channels (peercaps);
      /* if the peer has caps, intersect */
      GST_DEBUG_OBJECT (pad, "intersecting peer and template caps");
      result = gst_caps_intersect (peercaps, sinkcaps);
      gst_caps_unref (peercaps);
      gst_caps_unref (sinkcaps);
    } else {
      /* the peer has no caps (or there is no peer), just use the allowed caps
       * of this sinkpad. */
      GST_DEBUG_OBJECT (pad, "no peer caps, using sinkcaps");
      result = sinkcaps;
    }
    __set_channels (result, 1);
  }

  GST_OBJECT_UNLOCK (self);

  gst_object_unref (self);

  GST_DEBUG_OBJECT (pad, "Returning caps %" GST_PTR_FORMAT, result);

  return result;
}

static void
gst_interleave_set_process_function (GstInterleave * self)
{
  switch (self->width) {
    case 8:
      self->func = (GstInterleaveFunc) interleave_8;
      break;
    case 16:
      self->func = (GstInterleaveFunc) interleave_16;
      break;
    case 24:
      self->func = (GstInterleaveFunc) interleave_24;
      break;
    case 32:
      self->func = (GstInterleaveFunc) interleave_32;
      break;
    case 64:
      self->func = (GstInterleaveFunc) interleave_64;
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static gboolean
gst_interleave_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstInterleave *self = GST_INTERLEAVE (gst_pad_get_parent (pad));

  /* First caps that are set on a sink pad are used as output caps */
  /* TODO: handle caps changes */
  if (self->sinkcaps && !gst_caps_is_equal (caps, self->sinkcaps)) {
    goto cannot_change_caps;
  } else {
    GstCaps *srccaps;
    GstStructure *s;
    gboolean res;

    s = gst_caps_get_structure (caps, 0);

    if (!gst_structure_get_int (s, "width", &self->width))
      goto no_width;

    if (!gst_structure_get_int (s, "rate", &self->rate))
      goto no_rate;

    gst_interleave_set_process_function (self);

    srccaps = gst_caps_copy (caps);
    s = gst_caps_get_structure (srccaps, 0);

    /* TODO: channel positions */
    gst_structure_set (s, "channels", G_TYPE_INT, self->channels, NULL);
    gst_structure_remove_field (s, "channel-positions");

    res = gst_pad_set_caps (self->src, srccaps);
    gst_caps_unref (srccaps);

    if (!res)
      goto src_did_not_accept;
  }

  if (!self->sinkcaps)
    gst_caps_replace (&self->sinkcaps, caps);

  gst_object_unref (self);

  return TRUE;

cannot_change_caps:
  {
    GST_DEBUG_OBJECT (self, "caps of %" GST_PTR_FORMAT " already set, can't "
        "change", self->sinkcaps);
    gst_object_unref (self);
    return FALSE;
  }
src_did_not_accept:
  {
    GST_DEBUG_OBJECT (self, "src did not accept setcaps()");
    gst_object_unref (self);
    return FALSE;
  }
no_width:
  {
    GST_WARNING_OBJECT (self, "caps did not have width: %" GST_PTR_FORMAT,
        caps);
    gst_object_unref (self);
    return FALSE;
  }
no_rate:
  {
    GST_WARNING_OBJECT (self, "caps did not have rate: %" GST_PTR_FORMAT, caps);
    gst_object_unref (self);
    return FALSE;
  }
}

static gboolean
gst_interleave_sink_event (GstPad * pad, GstEvent * event)
{
  GstInterleave *self = GST_INTERLEAVE (gst_pad_get_parent (pad));
  gboolean ret;

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
      self->segment_pending = TRUE;
      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  ret = self->collect_event (pad, event);

  gst_object_unref (self);
  return ret;
}

static gboolean
gst_interleave_src_query_duration (GstInterleave * self, GstQuery * query)
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

  /* Take maximum of all durations */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (self));
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
gst_interleave_src_query (GstPad * pad, GstQuery * query)
{
  GstInterleave *self = GST_INTERLEAVE (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          /* FIXME, bring to stream time, might be tricky */
          gst_query_set_position (query, format, self->timestamp);
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, format, self->offset);
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_interleave_src_query_duration (self, query);
      break;
    default:
      /* FIXME, needs a custom query handler because we have multiple
       * sinkpads */
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (self);
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

static gboolean
forward_event (GstInterleave * self, GstEvent * event)
{
  gboolean ret;
  GstIterator *it;
  GValue vret = { 0 };

  GST_LOG_OBJECT (self, "Forwarding event %p (%s)", event,
      GST_EVENT_TYPE_NAME (event));

  ret = TRUE;

  g_value_init (&vret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&vret, TRUE);
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (self));
  gst_iterator_fold (it, (GstIteratorFoldFunction) forward_event_func, &vret,
      event);
  gst_iterator_free (it);
  gst_event_unref (event);

  ret = g_value_get_boolean (&vret);

  return ret;
}


static gboolean
gst_interleave_src_event (GstPad * pad, GstEvent * event)
{
  GstInterleave *self = GST_INTERLEAVE (gst_pad_get_parent (pad));
  gboolean result;

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
      gst_event_parse_seek (event, &self->segment_rate, NULL, &flags, &curtype,
          &cur, NULL, NULL);

      /* check if we are flushing */
      if (flags & GST_SEEK_FLAG_FLUSH) {
        /* make sure we accept nothing anymore and return WRONG_STATE */
        gst_collect_pads_set_flushing (self->collect, TRUE);

        /* flushing seek, start flush downstream, the flush will be done
         * when all pads received a FLUSH_STOP. */
        gst_pad_push_event (self->src, gst_event_new_flush_start ());
      }

      /* now wait for the collected to be finished and mark a new
       * segment */
      GST_OBJECT_LOCK (self->collect);
      if (curtype == GST_SEEK_TYPE_SET)
        self->segment_position = cur;
      else
        self->segment_position = 0;
      self->segment_pending = TRUE;
      GST_OBJECT_UNLOCK (self->collect);

      result = forward_event (self, event);
      break;
    }
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      result = FALSE;
      break;
    default:
      /* just forward the rest for now */
      result = forward_event (self, event);
      break;
  }
  gst_object_unref (self);

  return result;
}

static GstFlowReturn
gst_interleave_collected (GstCollectPads * pads, GstInterleave * self)
{
  guint size;
  GstBuffer *outbuf;
  GstFlowReturn ret = GST_FLOW_OK;
  GSList *collected;
  guint nsamples;
  guint ncollected = 0;
  gboolean empty = TRUE;
  gint width = self->width / 8;

  g_return_val_if_fail (self->func != NULL, GST_FLOW_NOT_NEGOTIATED);
  g_return_val_if_fail (self->width > 0, GST_FLOW_NOT_NEGOTIATED);
  g_return_val_if_fail (self->channels > 0, GST_FLOW_NOT_NEGOTIATED);
  g_return_val_if_fail (self->rate > 0, GST_FLOW_NOT_NEGOTIATED);

  size = gst_collect_pads_available (pads);

  g_return_val_if_fail (size % width == 0, GST_FLOW_ERROR);

  GST_DEBUG_OBJECT (self, "Starting to collect %u bytes from %d channels", size,
      self->channels);

  nsamples = size / width;

  ret =
      gst_pad_alloc_buffer (self->src, GST_BUFFER_OFFSET_NONE,
      size * self->channels, GST_PAD_CAPS (self->src), &outbuf);

  if (ret != GST_FLOW_OK) {
    return ret;
  } else if (outbuf == NULL || GST_BUFFER_SIZE (outbuf) < size * self->channels) {
    gst_buffer_unref (outbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  } else if (!gst_caps_is_equal (GST_BUFFER_CAPS (outbuf),
          GST_PAD_CAPS (self->src))) {
    gst_buffer_unref (outbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  memset (GST_BUFFER_DATA (outbuf), 0, size * self->channels);

  for (collected = pads->data; collected != NULL; collected = collected->next) {
    GstInterleaveCollectData *cdata;
    GstBuffer *inbuf;
    guint8 *outdata;

    cdata = (GstInterleaveCollectData *) collected->data;

    inbuf = gst_collect_pads_take_buffer (pads, (GstCollectData *) cdata, size);
    if (inbuf == NULL) {
      GST_DEBUG_OBJECT (cdata->data.pad, "No buffer available");
      goto next;
    }
    ncollected++;

    if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP))
      goto next;

    empty = FALSE;
    outdata = GST_BUFFER_DATA (outbuf) + width * cdata->channel;

    self->func (outdata, GST_BUFFER_DATA (inbuf), self->channels, nsamples);

  next:
    if (inbuf)
      gst_buffer_unref (inbuf);
  }

  if (ncollected == 0)
    goto eos;

  if (self->segment_pending) {
    GstEvent *event;

    event = gst_event_new_new_segment_full (FALSE, self->segment_rate,
        1.0, GST_FORMAT_TIME, self->timestamp, -1, self->segment_position);

    gst_pad_push_event (self->src, event);
    self->segment_pending = FALSE;
    self->segment_position = 0;
  }

  GST_BUFFER_TIMESTAMP (outbuf) = self->timestamp;
  GST_BUFFER_OFFSET (outbuf) = self->offset;

  self->offset += nsamples;
  self->timestamp = gst_util_uint64_scale_int (self->offset,
      GST_SECOND, self->rate);

  GST_BUFFER_DURATION (outbuf) = self->timestamp -
      GST_BUFFER_TIMESTAMP (outbuf);

  if (empty)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_GAP);

  GST_LOG_OBJECT (self, "pushing outbuf, timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
  ret = gst_pad_push (self->src, outbuf);

  return ret;

eos:
  {
    GST_DEBUG_OBJECT (self, "no data available, must be EOS");
    gst_buffer_unref (outbuf);
    gst_pad_push_event (self->src, gst_event_new_eos ());
    return GST_FLOW_UNEXPECTED;
  }
}
