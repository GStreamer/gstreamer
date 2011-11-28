/* GStreamer
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstfrei0r.h"
#include "gstfrei0rmixer.h"

#include <gst/controller/gstcontroller.h>

GST_DEBUG_CATEGORY_EXTERN (frei0r_debug);
#define GST_CAT_DEFAULT frei0r_debug

typedef struct
{
  f0r_plugin_info_t info;
  GstFrei0rFuncTable ftable;
} GstFrei0rMixerClassData;

static void
gst_frei0r_mixer_reset (GstFrei0rMixer * self)
{
  GstFrei0rMixerClass *klass = GST_FREI0R_MIXER_GET_CLASS (self);
  GstEvent **p_ev;

  if (self->f0r_instance) {
    klass->ftable->destruct (self->f0r_instance);
    self->f0r_instance = NULL;
  }

  if (self->property_cache)
    gst_frei0r_property_cache_free (klass->properties, self->property_cache,
        klass->n_properties);
  self->property_cache = NULL;

  gst_caps_replace (&self->caps, NULL);
  p_ev = &self->newseg_event;
  gst_event_replace (p_ev, NULL);

  self->fmt = GST_VIDEO_FORMAT_UNKNOWN;
  self->width = self->height = 0;
}

static void
gst_frei0r_mixer_finalize (GObject * object)
{
  GstFrei0rMixer *self = GST_FREI0R_MIXER (object);
  GstFrei0rMixerClass *klass = GST_FREI0R_MIXER_GET_CLASS (object);

  if (self->property_cache)
    gst_frei0r_property_cache_free (klass->properties, self->property_cache,
        klass->n_properties);
  self->property_cache = NULL;

  if (self->collect)
    gst_object_unref (self->collect);
  self->collect = NULL;

  G_OBJECT_CLASS (g_type_class_peek_parent (klass))->finalize (object);
}

static void
gst_frei0r_mixer_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFrei0rMixer *self = GST_FREI0R_MIXER (object);
  GstFrei0rMixerClass *klass = GST_FREI0R_MIXER_GET_CLASS (object);

  GST_OBJECT_LOCK (self);
  if (!gst_frei0r_get_property (self->f0r_instance, klass->ftable,
          klass->properties, klass->n_properties, self->property_cache, prop_id,
          value))
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  GST_OBJECT_UNLOCK (self);
}

static void
gst_frei0r_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFrei0rMixer *self = GST_FREI0R_MIXER (object);
  GstFrei0rMixerClass *klass = GST_FREI0R_MIXER_GET_CLASS (object);

  GST_OBJECT_LOCK (self);
  if (!gst_frei0r_set_property (self->f0r_instance, klass->ftable,
          klass->properties, klass->n_properties, self->property_cache, prop_id,
          value))
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  GST_OBJECT_UNLOCK (self);
}

static GstStateChangeReturn
gst_frei0r_mixer_change_state (GstElement * element, GstStateChange transition)
{
  GstFrei0rMixer *self = GST_FREI0R_MIXER (element);
  GstFrei0rMixerClass *klass = GST_FREI0R_MIXER_GET_CLASS (self);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (self->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  /* Stop before calling the parent's state change function as
   * GstCollectPads might take locks and we would deadlock in that
   * case
   */
  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY)
    gst_collect_pads_stop (self->collect);

  ret =
      GST_ELEMENT_CLASS (g_type_class_peek_parent (klass))->change_state
      (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_frei0r_mixer_reset (self);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static GstCaps *
gst_frei0r_mixer_get_caps (GstPad * pad)
{
  GstFrei0rMixer *self = GST_FREI0R_MIXER (gst_pad_get_parent (pad));
  GstCaps *caps = NULL;

  if (self->caps) {
    caps = gst_caps_ref (self->caps);
  } else {
    GstCaps *tmp, *tmp1;

    tmp = gst_caps_copy (gst_pad_get_pad_template_caps (self->src));
    tmp1 = gst_pad_peer_get_caps (pad);
    if (tmp1) {
      caps = gst_caps_intersect (tmp, tmp1);
      gst_caps_unref (tmp1);
      gst_caps_unref (tmp);
    } else {
      caps = tmp;
    }

    tmp = caps;
    tmp1 = gst_pad_peer_get_caps (self->sink0);
    if (tmp1) {
      caps = gst_caps_intersect (tmp, tmp1);
      gst_caps_unref (tmp);
      gst_caps_unref (tmp1);
    }

    tmp = caps;
    tmp1 = gst_pad_peer_get_caps (self->sink1);
    if (tmp1) {
      caps = gst_caps_intersect (tmp, tmp1);
      gst_caps_unref (tmp);
      gst_caps_unref (tmp1);
    }

    if (self->sink2) {
      tmp = caps;
      tmp1 = gst_pad_peer_get_caps (self->sink2);
      if (tmp1) {
        caps = gst_caps_intersect (tmp, tmp1);
        gst_caps_unref (tmp);
        gst_caps_unref (tmp1);
      }
    }
  }

  gst_object_unref (self);

  return caps;
}

static gboolean
gst_frei0r_mixer_set_caps (GstPad * pad, GstCaps * caps)
{
  GstFrei0rMixer *self = GST_FREI0R_MIXER (gst_pad_get_parent (pad));
  gboolean ret = TRUE;

  gst_caps_replace (&self->caps, caps);

  if (pad != self->src)
    ret &= gst_pad_set_caps (self->src, caps);
  if (pad != self->sink0)
    ret &= gst_pad_set_caps (self->sink0, caps);
  if (pad != self->sink1)
    ret &= gst_pad_set_caps (self->sink1, caps);
  if (pad != self->sink2 && self->sink2)
    ret &= gst_pad_set_caps (self->sink2, caps);

  if (ret) {
    if (!gst_video_format_parse_caps (caps, &self->fmt, &self->width,
            &self->height)) {
      ret = FALSE;
      goto out;
    }
  }
out:

  gst_object_unref (self);

  return ret;
}

static gboolean
gst_frei0r_mixer_src_query_duration (GstFrei0rMixer * self, GstQuery * query)
{
  gint64 min;
  gboolean res;
  GstFormat format;
  GstIterator *it;
  gboolean done;

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  min = -1;
  res = TRUE;
  done = FALSE;

  /* Take minimum of all durations */
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
        /* take min from all valid return values */
        if (res) {
          /* valid unknown length, stop searching */
          if (duration == -1) {
            min = duration;
            done = TRUE;
          }
          /* else see if smaller than current min */
          else if (duration < min)
            min = duration;
        }
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        min = -1;
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
    /* and store the min */
    GST_DEBUG_OBJECT (self, "Total duration in format %s: %"
        GST_TIME_FORMAT, gst_format_get_name (format), GST_TIME_ARGS (min));
    gst_query_set_duration (query, format, min);
  }

  return res;
}

static gboolean
gst_frei0r_mixer_src_query_latency (GstFrei0rMixer * self, GstQuery * query)
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
    GST_DEBUG_OBJECT (self, "Calculated total latency: live %s, min %"
        GST_TIME_FORMAT ", max %" GST_TIME_FORMAT,
        (live ? "yes" : "no"), GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    gst_query_set_latency (query, live, min, max);
  }

  return res;
}

static gboolean
gst_frei0r_mixer_src_query (GstPad * pad, GstQuery * query)
{
  GstFrei0rMixer *self = GST_FREI0R_MIXER (gst_pad_get_parent (pad));
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      ret = gst_pad_query (self->sink0, query);
      break;
    case GST_QUERY_DURATION:
      ret = gst_frei0r_mixer_src_query_duration (self, query);
      break;
    case GST_QUERY_LATENCY:
      ret = gst_frei0r_mixer_src_query_latency (self, query);
      break;
    default:
      ret = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (self);

  return ret;
}

static gboolean
gst_frei0r_mixer_sink_query (GstPad * pad, GstQuery * query)
{
  GstFrei0rMixer *self = GST_FREI0R_MIXER (gst_pad_get_parent (pad));
  gboolean ret = gst_pad_query (self->src, query);

  gst_object_unref (self);

  return ret;
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
forward_event (GstFrei0rMixer * self, GstEvent * event)
{
  GstIterator *it;
  GValue vret = { 0 };

  GST_LOG_OBJECT (self, "Forwarding event %p (%s)", event,
      GST_EVENT_TYPE_NAME (event));

  g_value_init (&vret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&vret, TRUE);
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (self));
  gst_iterator_fold (it, (GstIteratorFoldFunction) forward_event_func, &vret,
      event);
  gst_iterator_free (it);
  gst_event_unref (event);

  return g_value_get_boolean (&vret);
}

static gboolean
gst_frei0r_mixer_src_event (GstPad * pad, GstEvent * event)
{
  GstFrei0rMixer *self = GST_FREI0R_MIXER (gst_pad_get_parent (pad));
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      /* QoS might be tricky */
      ret = FALSE;
      break;
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;

      /* parse the seek parameters */
      gst_event_parse_seek (event, NULL, NULL, &flags, NULL, NULL, NULL, NULL);

      /* check if we are flushing */
      if (flags & GST_SEEK_FLAG_FLUSH) {
        /* make sure we accept nothing anymore and return WRONG_STATE */
        gst_collect_pads_set_flushing (self->collect, TRUE);

        /* flushing seek, start flush downstream, the flush will be done
         * when all pads received a FLUSH_STOP. */
        gst_pad_push_event (self->src, gst_event_new_flush_start ());
      }

      ret = forward_event (self, event);
      break;
    }
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      ret = FALSE;
      break;
    default:
      /* just forward the rest for now */
      ret = forward_event (self, event);
      break;
  }

  gst_object_unref (self);

  return ret;
}

static gboolean
gst_frei0r_mixer_sink0_event (GstPad * pad, GstEvent * event)
{
  GstFrei0rMixer *self = GST_FREI0R_MIXER (gst_pad_get_parent (pad));
  gboolean ret = FALSE;
  GstEvent **p_ev;

  GST_DEBUG ("Got %s event on pad %s:%s", GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      p_ev = &self->newseg_event;
      gst_event_replace (p_ev, event);
      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  ret = self->collect_event (pad, event);

  gst_object_unref (self);

  return ret;
}

static GstFlowReturn
gst_frei0r_mixer_collected (GstCollectPads * pads, GstFrei0rMixer * self)
{
  GstBuffer *inbuf0 = NULL, *inbuf1 = NULL, *inbuf2 = NULL;
  GstBuffer *outbuf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  GSList *l;
  GstFrei0rMixerClass *klass = GST_FREI0R_MIXER_GET_CLASS (self);
  GstClockTime timestamp;
  gdouble time;
  GstSegment *segment = NULL;

  if (G_UNLIKELY (self->width <= 0 || self->height <= 0))
    return GST_FLOW_NOT_NEGOTIATED;

  if (G_UNLIKELY (!self->f0r_instance)) {
    self->f0r_instance =
        gst_frei0r_instance_construct (klass->ftable, klass->properties,
        klass->n_properties, self->property_cache, self->width, self->height);
    if (G_UNLIKELY (!self->f0r_instance))
      return GST_FLOW_ERROR;
  }

  if (self->newseg_event) {
    gst_pad_push_event (self->src, self->newseg_event);
    self->newseg_event = NULL;
  }

  if ((ret =
          gst_pad_alloc_buffer_and_set_caps (self->src, GST_BUFFER_OFFSET_NONE,
              gst_video_format_get_size (self->fmt, self->width, self->height),
              GST_PAD_CAPS (self->src), &outbuf)) != GST_FLOW_OK)
    return ret;

  for (l = pads->data; l; l = l->next) {
    GstCollectData *cdata = l->data;

    if (cdata->pad == self->sink0) {
      inbuf0 = gst_collect_pads_pop (pads, cdata);
      segment = &cdata->segment;
    } else if (cdata->pad == self->sink1) {
      inbuf1 = gst_collect_pads_pop (pads, cdata);
    } else if (cdata->pad == self->sink2) {
      inbuf2 = gst_collect_pads_pop (pads, cdata);
    }
  }

  if (!inbuf0 || !inbuf1 || (!inbuf2 && self->sink2))
    goto eos;

  g_assert (segment != NULL);
  timestamp = GST_BUFFER_TIMESTAMP (inbuf0);
  timestamp = gst_segment_to_stream_time (segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (self, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    gst_object_sync_values (G_OBJECT (self), timestamp);

  gst_buffer_copy_metadata (outbuf, inbuf0,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);
  time = ((gdouble) GST_BUFFER_TIMESTAMP (outbuf)) / GST_SECOND;

  GST_OBJECT_LOCK (self);
  klass->ftable->update2 (self->f0r_instance, time,
      (const guint32 *) GST_BUFFER_DATA (inbuf0),
      (const guint32 *) GST_BUFFER_DATA (inbuf1),
      (inbuf2) ? (const guint32 *) GST_BUFFER_DATA (inbuf2) : NULL,
      (guint32 *) GST_BUFFER_DATA (outbuf));
  GST_OBJECT_UNLOCK (self);

  gst_buffer_unref (inbuf0);
  gst_buffer_unref (inbuf1);
  if (inbuf2)
    gst_buffer_unref (inbuf2);

  ret = gst_pad_push (self->src, outbuf);

  return ret;

eos:
  {
    GST_DEBUG_OBJECT (self, "no data available, must be EOS");
    gst_buffer_unref (outbuf);

    if (inbuf0)
      gst_buffer_unref (inbuf0);
    if (inbuf1)
      gst_buffer_unref (inbuf1);
    if (inbuf2)
      gst_buffer_unref (inbuf2);

    gst_pad_push_event (self->src, gst_event_new_eos ());
    return GST_FLOW_UNEXPECTED;
  }
}

static void
gst_frei0r_mixer_class_init (GstFrei0rMixerClass * klass,
    GstFrei0rMixerClassData * class_data)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstPadTemplate *templ;
  GstCaps *caps;
  gchar *author;

  klass->ftable = &class_data->ftable;
  klass->info = &class_data->info;

  gobject_class->finalize = gst_frei0r_mixer_finalize;
  gobject_class->set_property = gst_frei0r_mixer_set_property;
  gobject_class->get_property = gst_frei0r_mixer_get_property;

  klass->n_properties = klass->info->num_params;
  klass->properties = g_new0 (GstFrei0rProperty, klass->n_properties);

  gst_frei0r_klass_install_properties (gobject_class, klass->ftable,
      klass->properties, klass->n_properties);

  author =
      g_strdup_printf
      ("Sebastian Dröge <sebastian.droege@collabora.co.uk>, %s",
      class_data->info.author);
  gst_element_class_set_details_simple (gstelement_class, class_data->info.name,
      "Filter/Editor/Video", class_data->info.explanation, author);
  g_free (author);

  caps = gst_frei0r_caps_from_color_model (class_data->info.color_model);

  templ =
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_caps_ref (caps));
  gst_element_class_add_pad_template (gstelement_class, templ);
  gst_object_unref (templ);

  templ =
      gst_pad_template_new ("sink_0", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_ref (caps));
  gst_element_class_add_pad_template (gstelement_class, templ);
  gst_object_unref (templ);

  templ =
      gst_pad_template_new ("sink_1", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_ref (caps));
  gst_element_class_add_pad_template (gstelement_class, templ);
  gst_object_unref (templ);

  if (klass->info->plugin_type == F0R_PLUGIN_TYPE_MIXER3) {
    templ =
        gst_pad_template_new ("sink_2", GST_PAD_SINK, GST_PAD_ALWAYS,
        gst_caps_ref (caps));
    gst_element_class_add_pad_template (gstelement_class, templ);
    gst_object_unref (templ);
  }
  gst_caps_unref (caps);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_change_state);
}

static void
gst_frei0r_mixer_init (GstFrei0rMixer * self, GstFrei0rMixerClass * klass)
{
  self->property_cache =
      gst_frei0r_property_cache_init (klass->properties, klass->n_properties);

  self->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (self->collect,
      (GstCollectPadsFunction) gst_frei0r_mixer_collected, self);

  self->src =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (klass), "src"), "src");
  gst_pad_set_getcaps_function (self->src,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_get_caps));
  gst_pad_set_setcaps_function (self->src,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_set_caps));
  gst_pad_set_query_function (self->src,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_src_query));
  gst_pad_set_event_function (self->src,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_src_event));
  gst_element_add_pad (GST_ELEMENT_CAST (self), self->src);

  self->sink0 =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (klass), "sink_0"), "sink_0");
  gst_pad_set_getcaps_function (self->sink0,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_get_caps));
  gst_pad_set_setcaps_function (self->sink0,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_set_caps));
  gst_pad_set_query_function (self->sink0,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_sink_query));
  gst_collect_pads_add_pad (self->collect, self->sink0,
      sizeof (GstCollectData));
  self->collect_event = (GstPadEventFunction) GST_PAD_EVENTFUNC (self->sink0);
  gst_pad_set_event_function (self->sink0,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_sink0_event));
  gst_element_add_pad (GST_ELEMENT_CAST (self), self->sink0);

  self->sink1 =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (klass), "sink_1"), "sink_1");
  gst_pad_set_getcaps_function (self->sink1,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_get_caps));
  gst_pad_set_setcaps_function (self->sink1,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_set_caps));
  gst_pad_set_query_function (self->sink0,
      GST_DEBUG_FUNCPTR (gst_frei0r_mixer_sink_query));
  gst_collect_pads_add_pad (self->collect, self->sink1,
      sizeof (GstCollectData));
  gst_element_add_pad (GST_ELEMENT_CAST (self), self->sink1);

  if (klass->info->plugin_type == F0R_PLUGIN_TYPE_MIXER3) {
    self->sink2 =
        gst_pad_new_from_template (gst_element_class_get_pad_template
        (GST_ELEMENT_CLASS (klass), "sink_2"), "sink_2");
    gst_pad_set_getcaps_function (self->sink2,
        GST_DEBUG_FUNCPTR (gst_frei0r_mixer_get_caps));
    gst_pad_set_setcaps_function (self->sink2,
        GST_DEBUG_FUNCPTR (gst_frei0r_mixer_set_caps));
    gst_pad_set_query_function (self->sink0,
        GST_DEBUG_FUNCPTR (gst_frei0r_mixer_sink_query));
    gst_collect_pads_add_pad (self->collect, self->sink2,
        sizeof (GstCollectData));
    gst_element_add_pad (GST_ELEMENT_CAST (self), self->sink2);
  }

}

GstFrei0rPluginRegisterReturn
gst_frei0r_mixer_register (GstPlugin * plugin, const gchar * vendor,
    const f0r_plugin_info_t * info, const GstFrei0rFuncTable * ftable)
{
  GTypeInfo typeinfo = {
    sizeof (GstFrei0rMixerClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_frei0r_mixer_class_init,
    NULL,
    NULL,
    sizeof (GstFrei0rMixer),
    0,
    (GInstanceInitFunc) gst_frei0r_mixer_init
  };
  GType type;
  gchar *type_name, *tmp;
  GstFrei0rMixerClassData *class_data;
  GstFrei0rPluginRegisterReturn ret = GST_FREI0R_PLUGIN_REGISTER_RETURN_FAILED;

  if (ftable->update2 == NULL)
    return GST_FREI0R_PLUGIN_REGISTER_RETURN_FAILED;

  if (vendor)
    tmp = g_strdup_printf ("frei0r-mixer-%s-%s", vendor, info->name);
  else
    tmp = g_strdup_printf ("frei0r-mixer-%s", info->name);
  type_name = g_ascii_strdown (tmp, -1);
  g_free (tmp);
  g_strcanon (type_name, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-+", '-');

  if (g_type_from_name (type_name)) {
    GST_DEBUG ("Type '%s' already exists", type_name);
    return GST_FREI0R_PLUGIN_REGISTER_RETURN_ALREADY_REGISTERED;
  }

  class_data = g_new0 (GstFrei0rMixerClassData, 1);
  memcpy (&class_data->info, info, sizeof (f0r_plugin_info_t));
  memcpy (&class_data->ftable, ftable, sizeof (GstFrei0rFuncTable));
  typeinfo.class_data = class_data;

  type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
  if (gst_element_register (plugin, type_name, GST_RANK_NONE, type))
    ret = GST_FREI0R_PLUGIN_REGISTER_RETURN_OK;

  g_free (type_name);
  return ret;
}
