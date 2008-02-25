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
 * @short_description: 1-to-N stream selectoring
 * @see_also: #GstTee, #GstInputSelector
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

static const GstElementDetails gst_output_selector_details =
GST_ELEMENT_DETAILS ("Output selector",
    "Generic",
    "1-to-N output stream selectoring",
    "Stefan Kost <stefan.kost@nokia.com>");

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
  PROP_ACTIVE_PAD = 1,
  PROP_RESEND_LATEST
};

static void gst_output_selector_dispose (GObject * object);
static void gst_output_selector_init (GstOutputSelector * sel);
static void gst_output_selector_base_init (GstOutputSelectorClass * klass);
static void gst_output_selector_class_init (GstOutputSelectorClass * klass);
static void gst_output_selector_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_output_selector_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstPad *gst_output_selector_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * unused);
static void gst_output_selector_release_pad (GstElement * element,
    GstPad * pad);
static GstFlowReturn gst_output_selector_chain (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn gst_output_selector_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_output_selector_handle_sink_event (GstPad * pad,
    GstEvent * event);

static GstElementClass *parent_class = NULL;

GType
gst_output_selector_get_type (void)
{
  static GType output_selector_type = 0;

  if (!output_selector_type) {
    static const GTypeInfo output_selector_info = {
      sizeof (GstOutputSelectorClass),
      (GBaseInitFunc) gst_output_selector_base_init,
      NULL,
      (GClassInitFunc) gst_output_selector_class_init,
      NULL,
      NULL,
      sizeof (GstOutputSelector),
      0,
      (GInstanceInitFunc) gst_output_selector_init,
    };
    output_selector_type =
        g_type_register_static (GST_TYPE_ELEMENT,
        "GstOutputSelector", &output_selector_info, 0);
    GST_DEBUG_CATEGORY_INIT (output_selector_debug,
        "output-selector", 0, "An output stream selector element");
  }

  return output_selector_type;
}

static void
gst_output_selector_base_init (GstOutputSelectorClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &gst_output_selector_details);
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
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_output_selector_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_output_selector_get_property);
  g_object_class_install_property (gobject_class, PROP_ACTIVE_PAD,
      g_param_spec_string ("active-pad", "Active pad",
          "Name of the currently active src pad", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_RESEND_LATEST,
      g_param_spec_boolean ("resend-latest", "Resend latest buffer",
          "Resend latest buffer after a switch to a new pad", FALSE,
          G_PARAM_READWRITE));
  gobject_class->dispose = gst_output_selector_dispose;
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_output_selector_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_output_selector_release_pad);
  gstelement_class->change_state = gst_output_selector_change_state;

}

static void
gst_output_selector_init (GstOutputSelector * sel)
{
  sel->sinkpad =
      gst_pad_new_from_static_template (&gst_output_selector_sink_factory,
      "sink");
  gst_pad_set_chain_function (sel->sinkpad,
      GST_DEBUG_FUNCPTR (gst_output_selector_chain));
  gst_pad_set_event_function (sel->sinkpad,
      GST_DEBUG_FUNCPTR (gst_output_selector_handle_sink_event));

  gst_element_add_pad (GST_ELEMENT (sel), sel->sinkpad);

  /*
     gst_pad_set_bufferalloc_function (sel->sinkpad,
     GST_DEBUG_FUNCPTR (gst_output_selector_bufferalloc));
     gst_pad_set_setcaps_function (sel->sinkpad,
     GST_DEBUG_FUNCPTR (gst_pad_proxy_setcaps));
     gst_pad_set_getcaps_function (sel->sinkpad,
     GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
   */
  /* srcpad management */
  sel->active_srcpad = NULL;
  sel->nb_srcpads = 0;
  gst_segment_init (&sel->segment, GST_FORMAT_UNDEFINED);
  sel->pending_srcpad = NULL;

  sel->resend_latest = FALSE;
  sel->latest_buffer = NULL;
}

static void
gst_output_selector_dispose (GObject * object)
{
  GstOutputSelector *osel = GST_OUTPUT_SELECTOR (object);

  if (osel->pending_srcpad != NULL) {
    gst_object_unref (osel->pending_srcpad);
    osel->pending_srcpad = NULL;
  }
  if (osel->latest_buffer != NULL) {
    gst_buffer_unref (osel->latest_buffer);
    osel->latest_buffer = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_output_selector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOutputSelector *sel = GST_OUTPUT_SELECTOR (object);

  switch (prop_id) {
    case PROP_ACTIVE_PAD:{
      GstPad *next_pad =
          gst_element_get_static_pad (GST_ELEMENT (sel),
          g_value_get_string (value));
      if (!next_pad) {
        GST_WARNING ("pad %s not found, activation failed",
            g_value_get_string (value));
        break;
      }
      if (next_pad != sel->active_srcpad) {
        /* switch to new srcpad in next chain run */
        if (sel->pending_srcpad != NULL) {
          GST_INFO ("replacing pending switch");
          gst_object_unref (sel->pending_srcpad);
        }
        sel->pending_srcpad = next_pad;
      } else {
        GST_INFO ("pad already active");
        gst_object_unref (next_pad);
      }
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
    case PROP_ACTIVE_PAD:{
      GST_OBJECT_LOCK (object);
      if (sel->active_srcpad != NULL) {
        g_value_take_string (value, gst_pad_get_name (sel->active_srcpad));
      } else {
        g_value_set_string (value, "");
      }
      GST_OBJECT_UNLOCK (object);
      break;
    }
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
  gboolean res = TRUE;
  GstEvent *ev = NULL;
  GstSegment *seg = NULL;
  gint64 start = 0, position = 0;

  GST_INFO ("switching to pad %" GST_PTR_FORMAT, osel->pending_srcpad);

  if (gst_pad_is_linked (osel->pending_srcpad)) {
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
    if (!gst_pad_push_event (osel->pending_srcpad, ev)) {
      GST_WARNING ("newsegment handling failed in %" GST_PTR_FORMAT,
          osel->pending_srcpad);
    }

    /* Resend latest buffer to newly switched pad */
    if (osel->resend_latest && osel->latest_buffer) {
      GST_INFO ("resending latest buffer");
      gst_pad_push (osel->pending_srcpad, osel->latest_buffer);
      osel->latest_buffer = NULL;
    }

    /* Switch */
    osel->active_srcpad = osel->pending_srcpad;
  } else {
    GST_WARNING ("switch failed, pad not linked");
    res = FALSE;
  }

  gst_object_unref (osel->pending_srcpad);
  osel->pending_srcpad = NULL;

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

  /* Keep reference to latest buffer to resend it after switch */
  if (osel->latest_buffer)
    gst_buffer_unref (osel->latest_buffer);
  osel->latest_buffer = gst_buffer_ref (buf);

  /* Keep track of last stop and use it in NEWSEGMENT start after 
     switching to a new src pad */
  last_stop = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (last_stop)) {
    duration = GST_BUFFER_DURATION (buf);
    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      last_stop += duration;
    }
    GST_LOG ("setting last stop %" GST_TIME_FORMAT, GST_TIME_ARGS (last_stop));
    gst_segment_set_last_stop (&osel->segment, osel->segment.format, last_stop);
  }

  GST_LOG ("pushing buffer to %" GST_PTR_FORMAT, osel->active_srcpad);
  res = gst_pad_push (osel->active_srcpad, buf);
  gst_object_unref (osel);

  return res;
}

static GstStateChangeReturn
gst_output_selector_change_state (GstElement * element,
    GstStateChange transition)
{
  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gboolean
gst_output_selector_handle_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstOutputSelector *sel;

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

      GST_DEBUG ("configured NEWSEGMENT update %d, rate %lf, applied rate %lf, "
          "format %d, "
          "%" G_GINT64_FORMAT " -- %" G_GINT64_FORMAT ", time %"
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
      /* Send other events to active src pad */
      res = gst_pad_push_event (sel->active_srcpad, event);
      break;
  }

  gst_object_unref (sel);

  return res;
}
