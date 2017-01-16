/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2005 David Schleef <ds@schleef.org>
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
 * SECTION:element-capsfilter
 * @title: capsfilter
 *
 * The element does not modify data as such, but can enforce limitations on the
 * data format.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 videotestsrc ! capsfilter caps=video/x-raw,format=GRAY8 ! videoconvert ! autovideosink
 * ]| Limits acceptable video from videotestsrc to be grayscale. Equivalent to
 * |[
 * gst-launch-1.0 videotestsrc ! video/x-raw,format=GRAY8 ! videoconvert ! autovideosink
 * ]| which is a short notation for the capsfilter element.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "../../gst/gst-i18n-lib.h"
#include "gstcapsfilter.h"

enum
{
  PROP_0,
  PROP_FILTER_CAPS,
  PROP_CAPS_CHANGE_MODE
};

#define DEFAULT_CAPS_CHANGE_MODE (GST_CAPS_FILTER_CAPS_CHANGE_MODE_IMMEDIATE)

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


GST_DEBUG_CATEGORY_STATIC (gst_capsfilter_debug);
#define GST_CAT_DEFAULT gst_capsfilter_debug

/* TODO: Add a drop-buffers mode */
#define GST_TYPE_CAPS_FILTER_CAPS_CHANGE_MODE (gst_caps_filter_caps_change_mode_get_type())
static GType
gst_caps_filter_caps_change_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue data[] = {
    {GST_CAPS_FILTER_CAPS_CHANGE_MODE_IMMEDIATE,
        "Only accept the current filter caps", "immediate"},
    {GST_CAPS_FILTER_CAPS_CHANGE_MODE_DELAYED,
        "Temporarily accept previous filter caps", "delayed"},
    {0, NULL, NULL},
  };

  if (!type) {
    type = g_enum_register_static ("GstCapsFilterCapsChangeMode", data);
  }
  return type;
}

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_capsfilter_debug, "capsfilter", 0, \
    "capsfilter element");
#define gst_capsfilter_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCapsFilter, gst_capsfilter, GST_TYPE_BASE_TRANSFORM,
    _do_init);


static void gst_capsfilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_capsfilter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_capsfilter_dispose (GObject * object);

static GstCaps *gst_capsfilter_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_capsfilter_accept_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps);
static GstFlowReturn gst_capsfilter_transform_ip (GstBaseTransform * base,
    GstBuffer * buf);
static GstFlowReturn gst_capsfilter_prepare_buf (GstBaseTransform * trans,
    GstBuffer * input, GstBuffer ** buf);
static gboolean gst_capsfilter_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_capsfilter_stop (GstBaseTransform * trans);

static void
gst_capsfilter_class_init (GstCapsFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *trans_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = gst_capsfilter_set_property;
  gobject_class->get_property = gst_capsfilter_get_property;
  gobject_class->dispose = gst_capsfilter_dispose;

  g_object_class_install_property (gobject_class, PROP_FILTER_CAPS,
      g_param_spec_boxed ("caps", _("Filter caps"),
          _("Restrict the possible allowed capabilities (NULL means ANY). "
              "Setting this property takes a reference to the supplied GstCaps "
              "object."), GST_TYPE_CAPS,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CAPS_CHANGE_MODE,
      g_param_spec_enum ("caps-change-mode", _("Caps Change Mode"),
          _("Filter caps change behaviour"),
          GST_TYPE_CAPS_FILTER_CAPS_CHANGE_MODE, DEFAULT_CAPS_CHANGE_MODE,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  gstelement_class = GST_ELEMENT_CLASS (klass);
  gst_element_class_set_static_metadata (gstelement_class,
      "CapsFilter",
      "Generic",
      "Pass data without modification, limiting formats",
      "David Schleef <ds@schleef.org>");
  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_capsfilter_transform_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_capsfilter_transform_ip);
  trans_class->accept_caps = GST_DEBUG_FUNCPTR (gst_capsfilter_accept_caps);
  trans_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_capsfilter_prepare_buf);
  trans_class->sink_event = GST_DEBUG_FUNCPTR (gst_capsfilter_sink_event);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_capsfilter_stop);
}

static void
gst_capsfilter_init (GstCapsFilter * filter)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (filter);
  gst_base_transform_set_gap_aware (trans, TRUE);
  gst_base_transform_set_prefer_passthrough (trans, FALSE);
  filter->filter_caps = gst_caps_new_any ();
  filter->filter_caps_used = FALSE;
  filter->got_sink_caps = FALSE;
  filter->caps_change_mode = DEFAULT_CAPS_CHANGE_MODE;
}

static void
gst_capsfilter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCapsFilter *capsfilter = GST_CAPS_FILTER (object);

  switch (prop_id) {
    case PROP_FILTER_CAPS:{
      GstCaps *new_caps;
      GstCaps *old_caps;
      const GstCaps *new_caps_val = gst_value_get_caps (value);

      if (new_caps_val == NULL) {
        new_caps = gst_caps_new_any ();
      } else {
        new_caps = (GstCaps *) new_caps_val;
        gst_caps_ref (new_caps);
      }

      GST_OBJECT_LOCK (capsfilter);
      old_caps = capsfilter->filter_caps;
      capsfilter->filter_caps = new_caps;
      if (old_caps && capsfilter->filter_caps_used &&
          capsfilter->caps_change_mode ==
          GST_CAPS_FILTER_CAPS_CHANGE_MODE_DELAYED) {
        capsfilter->previous_caps =
            g_list_prepend (capsfilter->previous_caps, gst_caps_ref (old_caps));
      } else if (capsfilter->caps_change_mode !=
          GST_CAPS_FILTER_CAPS_CHANGE_MODE_DELAYED) {
        g_list_free_full (capsfilter->previous_caps,
            (GDestroyNotify) gst_caps_unref);
        capsfilter->previous_caps = NULL;
      }
      capsfilter->filter_caps_used = FALSE;
      GST_OBJECT_UNLOCK (capsfilter);

      gst_caps_unref (old_caps);

      GST_DEBUG_OBJECT (capsfilter, "set new caps %" GST_PTR_FORMAT, new_caps);

      gst_base_transform_reconfigure_sink (GST_BASE_TRANSFORM (object));
      break;
    }
    case PROP_CAPS_CHANGE_MODE:{
      GstCapsFilterCapsChangeMode old_change_mode;

      GST_OBJECT_LOCK (capsfilter);
      old_change_mode = capsfilter->caps_change_mode;
      capsfilter->caps_change_mode = g_value_get_enum (value);

      if (capsfilter->caps_change_mode != old_change_mode) {
        g_list_free_full (capsfilter->previous_caps,
            (GDestroyNotify) gst_caps_unref);
        capsfilter->previous_caps = NULL;
      }
      GST_OBJECT_UNLOCK (capsfilter);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_capsfilter_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCapsFilter *capsfilter = GST_CAPS_FILTER (object);

  switch (prop_id) {
    case PROP_FILTER_CAPS:
      GST_OBJECT_LOCK (capsfilter);
      gst_value_set_caps (value, capsfilter->filter_caps);
      GST_OBJECT_UNLOCK (capsfilter);
      break;
    case PROP_CAPS_CHANGE_MODE:
      g_value_set_enum (value, capsfilter->caps_change_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_capsfilter_dispose (GObject * object)
{
  GstCapsFilter *filter = GST_CAPS_FILTER (object);

  gst_caps_replace (&filter->filter_caps, NULL);
  g_list_free_full (filter->pending_events, (GDestroyNotify) gst_event_unref);
  filter->pending_events = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_capsfilter_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCapsFilter *capsfilter = GST_CAPS_FILTER (base);
  GstCaps *ret, *filter_caps, *tmp;
  gboolean retried = FALSE;
  GstCapsFilterCapsChangeMode caps_change_mode;

  GST_OBJECT_LOCK (capsfilter);
  filter_caps = gst_caps_ref (capsfilter->filter_caps);
  capsfilter->filter_caps_used = TRUE;
  caps_change_mode = capsfilter->caps_change_mode;
  GST_OBJECT_UNLOCK (capsfilter);

retry:
  if (filter) {
    tmp =
        gst_caps_intersect_full (filter, filter_caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (filter_caps);
    filter_caps = tmp;
  }

  ret = gst_caps_intersect_full (filter_caps, caps, GST_CAPS_INTERSECT_FIRST);

  GST_DEBUG_OBJECT (capsfilter, "input:     %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (capsfilter, "filter:    %" GST_PTR_FORMAT, filter);
  GST_DEBUG_OBJECT (capsfilter, "caps filter:    %" GST_PTR_FORMAT,
      filter_caps);
  GST_DEBUG_OBJECT (capsfilter, "intersect: %" GST_PTR_FORMAT, ret);

  if (gst_caps_is_empty (ret)
      && caps_change_mode ==
      GST_CAPS_FILTER_CAPS_CHANGE_MODE_DELAYED && capsfilter->previous_caps
      && !retried) {
    GList *l;

    GST_DEBUG_OBJECT (capsfilter,
        "Current filter caps are not compatible, retry with previous");
    GST_OBJECT_LOCK (capsfilter);
    gst_caps_unref (filter_caps);
    gst_caps_unref (ret);
    filter_caps = gst_caps_new_empty ();
    for (l = capsfilter->previous_caps; l; l = l->next) {
      filter_caps = gst_caps_merge (filter_caps, gst_caps_ref (l->data));
    }
    GST_OBJECT_UNLOCK (capsfilter);
    retried = TRUE;
    goto retry;
  }

  gst_caps_unref (filter_caps);

  return ret;
}

static gboolean
gst_capsfilter_accept_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  GstCapsFilter *capsfilter = GST_CAPS_FILTER (base);
  GstCaps *filter_caps;
  gboolean ret;

  GST_OBJECT_LOCK (capsfilter);
  filter_caps = gst_caps_ref (capsfilter->filter_caps);
  capsfilter->filter_caps_used = TRUE;
  GST_OBJECT_UNLOCK (capsfilter);

  ret = gst_caps_can_intersect (caps, filter_caps);
  GST_DEBUG_OBJECT (capsfilter, "can intersect: %d", ret);
  if (!ret
      && capsfilter->caps_change_mode ==
      GST_CAPS_FILTER_CAPS_CHANGE_MODE_DELAYED) {
    GList *l;

    GST_OBJECT_LOCK (capsfilter);
    for (l = capsfilter->previous_caps; l; l = l->next) {
      ret = gst_caps_can_intersect (caps, l->data);
      if (ret)
        break;
    }
    GST_OBJECT_UNLOCK (capsfilter);

    /* Tell upstream to reconfigure, it's still
     * looking at old caps */
    if (ret)
      gst_base_transform_reconfigure_sink (base);
  }

  gst_caps_unref (filter_caps);

  return ret;
}

static GstFlowReturn
gst_capsfilter_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  /* No actual work here. It's all done in the prepare output buffer
   * func. */
  return GST_FLOW_OK;
}

static void
gst_capsfilter_push_pending_events (GstCapsFilter * filter, GList * events)
{
  GList *l;

  for (l = g_list_last (events); l; l = l->prev) {
    GST_LOG_OBJECT (filter, "Forwarding %s event",
        GST_EVENT_TYPE_NAME (l->data));
    GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (GST_BASE_TRANSFORM_CAST
        (filter), l->data);
  }
  g_list_free (events);
}

/* Ouput buffer preparation ... if the buffer has no caps, and our allowed
 * output caps is fixed, then send the caps downstream, making sure caps are
 * sent before segment event.
 *
 * This ensures that caps event is sent if we can, so that pipelines like:
 *   gst-launch filesrc location=rawsamples.raw !
 *       audio/x-raw,format=S16LE,rate=48000,channels=2 ! alsasink
 * will work.
 */
static GstFlowReturn
gst_capsfilter_prepare_buf (GstBaseTransform * trans, GstBuffer * input,
    GstBuffer ** buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstCapsFilter *filter = GST_CAPS_FILTER (trans);

  /* always return the input as output buffer */
  *buf = input;

  if (GST_PAD_MODE (trans->srcpad) == GST_PAD_MODE_PUSH
      && !filter->got_sink_caps) {

    /* No caps. See if the output pad only supports fixed caps */
    GstCaps *out_caps;
    GList *pending_events = filter->pending_events;

    GST_LOG_OBJECT (trans, "Input pad does not have caps");

    filter->pending_events = NULL;

    out_caps = gst_pad_get_current_caps (trans->srcpad);
    if (out_caps == NULL) {
      out_caps = gst_pad_get_allowed_caps (trans->srcpad);
      g_return_val_if_fail (out_caps != NULL, GST_FLOW_ERROR);
    }

    out_caps = gst_caps_simplify (out_caps);

    if (gst_caps_is_fixed (out_caps) && !gst_caps_is_empty (out_caps)) {
      GST_DEBUG_OBJECT (trans, "Have fixed output caps %"
          GST_PTR_FORMAT " to apply to srcpad", out_caps);

      if (!gst_pad_has_current_caps (trans->srcpad)) {
        if (gst_pad_set_caps (trans->srcpad, out_caps)) {
          if (pending_events) {
            gst_capsfilter_push_pending_events (filter, pending_events);
            pending_events = NULL;
          }
        } else {
          ret = GST_FLOW_NOT_NEGOTIATED;
        }
      } else {
        gst_capsfilter_push_pending_events (filter, pending_events);
        pending_events = NULL;
      }

      g_list_free_full (pending_events, (GDestroyNotify) gst_event_unref);
      gst_caps_unref (out_caps);
    } else {
      gchar *caps_str = gst_caps_to_string (out_caps);

      GST_DEBUG_OBJECT (trans, "Cannot choose caps. Have unfixed output caps %"
          GST_PTR_FORMAT, out_caps);
      gst_caps_unref (out_caps);

      GST_ELEMENT_ERROR (trans, STREAM, FORMAT,
          ("Filter caps do not completely specify the output format"),
          ("Output caps are unfixed: %s", caps_str));

      g_free (caps_str);
      g_list_free_full (pending_events, (GDestroyNotify) gst_event_unref);

      ret = GST_FLOW_ERROR;
    }
  } else if (G_UNLIKELY (filter->pending_events)) {
    GList *events = filter->pending_events;

    filter->pending_events = NULL;

    /* push pending events before a buffer */
    gst_capsfilter_push_pending_events (filter, events);
  }

  return ret;
}

/* Queue the segment event if there was no caps event */
static gboolean
gst_capsfilter_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstCapsFilter *filter = GST_CAPS_FILTER (trans);
  gboolean ret;

  if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
    GList *l;

    for (l = filter->pending_events; l; l = l->next) {
      if (GST_EVENT_TYPE (l->data) == GST_EVENT_SEGMENT ||
          GST_EVENT_TYPE (l->data) == GST_EVENT_EOS) {
        gst_event_unref (l->data);
        filter->pending_events = g_list_delete_link (filter->pending_events, l);
        break;
      }
    }
  }

  if (!GST_EVENT_IS_STICKY (event)
      || GST_EVENT_TYPE (event) <= GST_EVENT_CAPS)
    goto done;

  /* If we get EOS before any buffers, just push all pending events */
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GList *l;

    for (l = g_list_last (filter->pending_events); l; l = l->prev) {
      GST_LOG_OBJECT (trans, "Forwarding %s event",
          GST_EVENT_TYPE_NAME (l->data));
      GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, l->data);
    }
    g_list_free (filter->pending_events);
    filter->pending_events = NULL;
  } else if (!filter->got_sink_caps) {
    GST_LOG_OBJECT (trans, "Got %s event before caps, queueing",
        GST_EVENT_TYPE_NAME (event));

    filter->pending_events = g_list_prepend (filter->pending_events, event);

    return TRUE;
  }

done:

  GST_LOG_OBJECT (trans, "Forwarding %s event", GST_EVENT_TYPE_NAME (event));
  ret =
      GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans,
      gst_event_ref (event));

  if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
    filter->got_sink_caps = TRUE;
    if (filter->caps_change_mode == GST_CAPS_FILTER_CAPS_CHANGE_MODE_DELAYED) {
      GList *l;
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);

      /* Remove all previous caps up to one that works.
       * Note that this might keep some leftover caps if there
       * are multiple compatible caps */
      GST_OBJECT_LOCK (filter);
      for (l = g_list_last (filter->previous_caps); l; l = l->prev) {
        if (gst_caps_can_intersect (caps, l->data)) {
          while (l->next) {
            gst_caps_unref (l->next->data);
            l = g_list_delete_link (l, l->next);
          }
          break;
        }
      }
      if (!l && gst_caps_can_intersect (caps, filter->filter_caps)) {
        g_list_free_full (filter->previous_caps,
            (GDestroyNotify) gst_caps_unref);
        filter->previous_caps = NULL;
        filter->filter_caps_used = TRUE;
      }
      GST_OBJECT_UNLOCK (filter);
    }
  }
  gst_event_unref (event);

  return ret;
}

static gboolean
gst_capsfilter_stop (GstBaseTransform * trans)
{
  GstCapsFilter *filter = GST_CAPS_FILTER (trans);

  g_list_free_full (filter->pending_events, (GDestroyNotify) gst_event_unref);
  filter->pending_events = NULL;

  GST_OBJECT_LOCK (filter);
  g_list_free_full (filter->previous_caps, (GDestroyNotify) gst_caps_unref);
  filter->previous_caps = NULL;
  GST_OBJECT_UNLOCK (filter);

  filter->got_sink_caps = FALSE;

  return TRUE;
}
