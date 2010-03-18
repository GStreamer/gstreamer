/* GStreamer
 * Copyright (C) 2008 Nokia Corporation. (contact <stefan.kost@nokia.com>)
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
 * SECTION:element-output-selector
 * @see_also: #GstOutputSelector, #GstInputSelector
 *
 * Direct input stream to one out of N output pads.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstoutputselector.h"

GST_DEBUG_CATEGORY_STATIC (output_selector_debug);
#define GST_CAT_DEFAULT output_selector_debug

static GstStaticPadTemplate gst_output_selector_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_output_selector_src_factory =
GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_ACTIVE_PAD,
  PROP_RESEND_LATEST,
  PROP_LAST
};

GST_BOILERPLATE (GstOutputSelector, gst_output_selector, GstElement,
    GST_TYPE_ELEMENT);

static void gst_output_selector_dispose (GObject * object);
static void gst_output_selector_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_output_selector_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstPad *gst_output_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);
static void gst_output_selector_release_pad (GstElement * element,
    GstPad * pad);
static GstFlowReturn gst_output_selector_chain (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_output_selector_buffer_alloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static GstStateChangeReturn gst_output_selector_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_output_selector_handle_sink_event (GstPad * pad,
    GstEvent * event);

static void
gst_output_selector_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Output selector",
      "Generic",
      "1-to-N output stream selectoring",
      "Stefan Kost <stefan.kost@nokia.com>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_output_selector_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_output_selector_src_factory));
}

static void
gst_output_selector_class_init (GstOutputSelectorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_output_selector_dispose;

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_output_selector_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_output_selector_get_property);

  g_object_class_install_property (gobject_class, PROP_ACTIVE_PAD,
      g_param_spec_object ("active-pad", "Active pad",
          "Currently active src pad", GST_TYPE_PAD, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_RESEND_LATEST,
      g_param_spec_boolean ("resend-latest", "Resend latest buffer",
          "Resend latest buffer after a switch to a new pad", FALSE,
          G_PARAM_READWRITE));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_output_selector_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_output_selector_release_pad);

  gstelement_class->change_state = gst_output_selector_change_state;

  GST_DEBUG_CATEGORY_INIT (output_selector_debug,
      "output-selector", 0, "An output stream selector element");
}

static void
gst_output_selector_init (GstOutputSelector * sel,
    GstOutputSelectorClass * g_class)
{
  sel->sinkpad =
      gst_pad_new_from_static_template (&gst_output_selector_sink_factory,
      "sink");
  gst_pad_set_chain_function (sel->sinkpad,
      GST_DEBUG_FUNCPTR (gst_output_selector_chain));
  gst_pad_set_event_function (sel->sinkpad,
      GST_DEBUG_FUNCPTR (gst_output_selector_handle_sink_event));
  gst_pad_set_bufferalloc_function (sel->sinkpad,
      GST_DEBUG_FUNCPTR (gst_output_selector_buffer_alloc));
  /*
     gst_pad_set_setcaps_function (sel->sinkpad,
     GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
     gst_pad_set_getcaps_function (sel->sinkpad,
     GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
   */

  gst_element_add_pad (GST_ELEMENT (sel), sel->sinkpad);

  /* srcpad management */
  sel->active_srcpad = NULL;
  sel->nb_srcpads = 0;
  gst_segment_init (&sel->segment, GST_FORMAT_TIME);
  sel->pending_srcpad = NULL;

  sel->resend_latest = FALSE;
  sel->latest_buffer = NULL;
}

static void
gst_output_selector_reset (GstOutputSelector * osel)
{
  if (osel->pending_srcpad != NULL) {
    gst_object_unref (osel->pending_srcpad);
    osel->pending_srcpad = NULL;
  }
  if (osel->latest_buffer != NULL) {
    gst_buffer_unref (osel->latest_buffer);
    osel->latest_buffer = NULL;
  }
  gst_segment_init (&osel->segment, GST_FORMAT_UNDEFINED);
}

static void
gst_output_selector_dispose (GObject * object)
{
  GstOutputSelector *osel = GST_OUTPUT_SELECTOR (object);

  gst_output_selector_reset (osel);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_output_selector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOutputSelector *sel = GST_OUTPUT_SELECTOR (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:
    {
      GstPad *next_pad;

      next_pad = g_value_get_object (value);

      GST_INFO_OBJECT (sel, "Activating pad %s:%s",
          GST_DEBUG_PAD_NAME (next_pad));

      GST_OBJECT_LOCK (object);
      if (next_pad != sel->active_srcpad) {
        /* switch to new srcpad in next chain run */
        if (sel->pending_srcpad != NULL) {
          GST_INFO ("replacing pending switch");
          gst_object_unref (sel->pending_srcpad);
        }
        if (next_pad)
          gst_object_ref (next_pad);
        sel->pending_srcpad = next_pad;
      } else {
        GST_INFO ("pad already active");
        if (sel->pending_srcpad != NULL) {
          gst_object_unref (sel->pending_srcpad);
          sel->pending_srcpad = NULL;
        }
      }
      GST_OBJECT_UNLOCK (object);
      break;
    }
    case PROP_RESEND_LATEST:{
      sel->resend_latest = g_value_get_boolean (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_output_selector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOutputSelector *sel = GST_OUTPUT_SELECTOR (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:
      GST_OBJECT_LOCK (object);
      g_value_set_object (value,
          sel->pending_srcpad ? sel->pending_srcpad : sel->active_srcpad);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_RESEND_LATEST:{
      GST_OBJECT_LOCK (object);
      g_value_set_boolean (value, sel->resend_latest);
      GST_OBJECT_UNLOCK (object);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_output_selector_buffer_alloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstOutputSelector *sel;
  GstFlowReturn res;
  GstPad *allocpad;

  sel = GST_OUTPUT_SELECTOR (GST_PAD_PARENT (pad));
  res = GST_FLOW_NOT_LINKED;

  GST_OBJECT_LOCK (sel);
  allocpad = sel->pending_srcpad ? sel->pending_srcpad : sel->active_srcpad;
  if (allocpad) {
    /* if we had a previous pad we used for allocating a buffer, continue using
     * it. */
    GST_DEBUG_OBJECT (sel, "using pad %s:%s for alloc",
        GST_DEBUG_PAD_NAME (allocpad));
    gst_object_ref (allocpad);
    GST_OBJECT_UNLOCK (sel);

    res = gst_pad_alloc_buffer (allocpad, offset, size, caps, buf);
    gst_object_unref (allocpad);

    GST_OBJECT_LOCK (sel);
  } else {
    /* fallback case, allocate a buffer of our own, add pad caps. */
    GST_DEBUG_OBJECT (pad, "fallback buffer alloc");
    *buf = NULL;
    res = GST_FLOW_OK;
  }
  GST_OBJECT_UNLOCK (sel);

  GST_DEBUG_OBJECT (sel, "buffer alloc finished: %s", gst_flow_get_name (res));

  return res;
}

static GstPad *
gst_output_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  gchar *padname;
  GstPad *srcpad;
  GstOutputSelector *osel;

  osel = GST_OUTPUT_SELECTOR (element);

  GST_DEBUG_OBJECT (osel, "requesting pad");

  GST_OBJECT_LOCK (osel);
  padname = g_strdup_printf ("src%d", osel->nb_srcpads++);
  srcpad = gst_pad_new_from_template (templ, padname);
  GST_OBJECT_UNLOCK (osel);

  gst_pad_set_active (srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (osel), srcpad);

  /* Set the first requested src pad as active by default */
  if (osel->active_srcpad == NULL) {
    osel->active_srcpad = srcpad;
  }
  g_free (padname);

  return srcpad;
}

static void
gst_output_selector_release_pad (GstElement * element, GstPad * pad)
{
  GstOutputSelector *osel;

  osel = GST_OUTPUT_SELECTOR (element);

  GST_DEBUG_OBJECT (osel, "releasing pad");

  gst_pad_set_active (pad, FALSE);

  gst_element_remove_pad (GST_ELEMENT_CAST (osel), pad);
}

static gboolean
gst_output_selector_switch (GstOutputSelector * osel)
{
  gboolean res = FALSE;
  GstEvent *ev = NULL;
  GstSegment *seg = NULL;
  gint64 start = 0, position = 0;

  /* Switch */
  GST_OBJECT_LOCK (GST_OBJECT (osel));
  GST_INFO ("switching to pad %" GST_PTR_FORMAT, osel->pending_srcpad);
  if (gst_pad_is_linked (osel->pending_srcpad)) {
    osel->active_srcpad = osel->pending_srcpad;
    res = TRUE;
  }
  gst_object_unref (osel->pending_srcpad);
  osel->pending_srcpad = NULL;
  GST_OBJECT_UNLOCK (GST_OBJECT (osel));

  /* Send NEWSEGMENT event and latest buffer if switching succeeded */
  if (res) {
    /* Send NEWSEGMENT to the pad we are going to switch to */
    seg = &osel->segment;
    /* If resending then mark newsegment start and position accordingly */
    if (osel->resend_latest && osel->latest_buffer &&
        GST_BUFFER_TIMESTAMP_IS_VALID (osel->latest_buffer)) {
      start = position = GST_BUFFER_TIMESTAMP (osel->latest_buffer);
    } else {
      start = position = seg->last_stop;
    }
    ev = gst_event_new_new_segment (TRUE, seg->rate,
        seg->format, start, seg->stop, position);
    if (!gst_pad_push_event (osel->active_srcpad, ev)) {
      GST_WARNING_OBJECT (osel,
          "newsegment handling failed in %" GST_PTR_FORMAT,
          osel->active_srcpad);
    }

    /* Resend latest buffer to newly switched pad */
    if (osel->resend_latest && osel->latest_buffer) {
      GST_INFO ("resending latest buffer");
      gst_pad_push (osel->active_srcpad, osel->latest_buffer);
      osel->latest_buffer = NULL;
    }
  } else {
    GST_WARNING_OBJECT (osel, "switch failed, pad not linked");
  }

  return res;
}

static GstFlowReturn
gst_output_selector_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn res;
  GstOutputSelector *osel;
  GstClockTime last_stop, duration;

  osel = GST_OUTPUT_SELECTOR (gst_pad_get_parent (pad));

  if (osel->pending_srcpad) {
    /* Do the switch */
    gst_output_selector_switch (osel);
  }

  if (osel->latest_buffer) {
    gst_buffer_unref (osel->latest_buffer);
    osel->latest_buffer = NULL;
  }

  if (osel->resend_latest) {
    /* Keep reference to latest buffer to resend it after switch */
    osel->latest_buffer = gst_buffer_ref (buf);
  }

  /* Keep track of last stop and use it in NEWSEGMENT start after 
     switching to a new src pad */
  last_stop = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (last_stop)) {
    duration = GST_BUFFER_DURATION (buf);
    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      last_stop += duration;
    }
    GST_LOG_OBJECT (osel, "setting last stop %" GST_TIME_FORMAT,
        GST_TIME_ARGS (last_stop));
    gst_segment_set_last_stop (&osel->segment, osel->segment.format, last_stop);
  }

  GST_LOG_OBJECT (osel, "pushing buffer to %" GST_PTR_FORMAT,
      osel->active_srcpad);
  res = gst_pad_push (osel->active_srcpad, buf);
  gst_object_unref (osel);

  return res;
}

static GstStateChangeReturn
gst_output_selector_change_state (GstElement * element,
    GstStateChange transition)
{
  GstOutputSelector *sel;
  GstStateChangeReturn result;

  sel = GST_OUTPUT_SELECTOR (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_output_selector_reset (sel);
      break;
    default:
      break;
  }

  return result;
}

static gboolean
gst_output_selector_handle_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstOutputSelector *sel;
  GstPad *output_pad = NULL;

  sel = GST_OUTPUT_SELECTOR (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      GST_DEBUG_OBJECT (sel,
          "configured NEWSEGMENT update %d, rate %lf, applied rate %lf, "
          "format %d, " "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
          G_GINT64_FORMAT, update, rate, arate, format, start, stop, time);

      gst_segment_set_newsegment_full (&sel->segment, update,
          rate, arate, format, start, stop, time);

      /* Send newsegment to all src pads */
      gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_EOS:
      /* Send eos to all src pads */
      gst_pad_event_default (pad, event);
      break;
    default:
      /* Send other events to pending or active src pad */
      output_pad =
          sel->pending_srcpad ? sel->pending_srcpad : sel->active_srcpad;
      res = gst_pad_push_event (output_pad, event);
      break;
  }

  gst_object_unref (sel);

  return res;
}
