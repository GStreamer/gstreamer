/* GStreamer
 * Copyright (C) 2018 Ognyan Tonchev <ognyan@axis.com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "rtsp-latency-bin.h"

struct _GstRTSPLatencyBinPrivate
{
  GstPad *sinkpad;
  GstElement *element;
};

enum
{
  PROP_0,
  PROP_ELEMENT,
  PROP_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_latency_bin_debug);
#define GST_CAT_DEFAULT rtsp_latency_bin_debug

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_rtsp_latency_bin_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_latency_bin_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static gboolean gst_rtsp_latency_bin_element_query (GstElement * element,
    GstQuery * query);
static gboolean gst_rtsp_latency_bin_element_event (GstElement * element,
    GstEvent * event);
static void gst_rtsp_latency_bin_message_handler (GstBin * bin,
    GstMessage * message);
static gboolean gst_rtsp_latency_bin_add_element (GstRTSPLatencyBin *
    latency_bin, GstElement * element);
static GstStateChangeReturn gst_rtsp_latency_bin_change_state (GstElement *
    element, GstStateChange transition);

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPLatencyBin, gst_rtsp_latency_bin,
    GST_TYPE_BIN);

static void
gst_rtsp_latency_bin_class_init (GstRTSPLatencyBinClass * klass)
{
  GObjectClass *gobject_klass = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (klass);
  GstBinClass *gstbin_klass = GST_BIN_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (rtsp_latency_bin_debug,
      "rtsplatencybin", 0, "GstRTSPLatencyBin");

  gobject_klass->get_property = gst_rtsp_latency_bin_get_property;
  gobject_klass->set_property = gst_rtsp_latency_bin_set_property;

  g_object_class_install_property (gobject_klass, PROP_ELEMENT,
      g_param_spec_object ("element", "The Element",
          "The GstElement to prevent from affecting piplines latency",
          GST_TYPE_ELEMENT, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_rtsp_latency_bin_change_state);
  gstelement_klass->query =
      GST_DEBUG_FUNCPTR (gst_rtsp_latency_bin_element_query);
  gstelement_klass->send_event =
      GST_DEBUG_FUNCPTR (gst_rtsp_latency_bin_element_event);

  gstbin_klass->handle_message =
      GST_DEBUG_FUNCPTR (gst_rtsp_latency_bin_message_handler);
}

static void
gst_rtsp_latency_bin_init (GstRTSPLatencyBin * latency_bin)
{
  GST_OBJECT_FLAG_SET (latency_bin, GST_ELEMENT_FLAG_SINK);
}

static void
gst_rtsp_latency_bin_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPLatencyBin *latency_bin = GST_RTSP_LATENCY_BIN (object);
  GstRTSPLatencyBinPrivate *priv =
      gst_rtsp_latency_bin_get_instance_private (latency_bin);

  switch (propid) {
    case PROP_ELEMENT:
      g_value_set_object (value, priv->element);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_latency_bin_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPLatencyBin *latency_bin = GST_RTSP_LATENCY_BIN (object);

  switch (propid) {
    case PROP_ELEMENT:
      if (!gst_rtsp_latency_bin_add_element (latency_bin,
              g_value_get_object (value))) {
        GST_WARNING_OBJECT (latency_bin, "Could not add the element");
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static gboolean
gst_rtsp_latency_bin_add_element (GstRTSPLatencyBin * latency_bin,
    GstElement * element)
{
  GstRTSPLatencyBinPrivate *priv =
      gst_rtsp_latency_bin_get_instance_private (latency_bin);
  GstPad *pad;
  GstPadTemplate *templ;

  GST_DEBUG_OBJECT (latency_bin, "Adding element to latencybin : %s",
      GST_ELEMENT_NAME (element));

  if (!element) {
    goto no_element;
  }

  /* add the element to ourself */
  gst_object_ref (element);
  gst_bin_add (GST_BIN (latency_bin), element);
  priv->element = element;

  /* add ghost pad first */
  templ = gst_static_pad_template_get (&sinktemplate);
  priv->sinkpad = gst_ghost_pad_new_no_target_from_template ("sink", templ);
  gst_object_unref (templ);
  g_assert (priv->sinkpad);

  gst_element_add_pad (GST_ELEMENT (latency_bin), priv->sinkpad);

  /* and link it to our element */
  pad = gst_element_get_static_pad (element, "sink");
  if (!pad) {
    goto no_sink_pad;
  }

  if (!gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (priv->sinkpad), pad)) {
    goto set_target_failed;
  }

  gst_object_unref (pad);

  return TRUE;

  /* ERRORs */
no_element:
  {
    GST_WARNING_OBJECT (latency_bin, "No element, not adding");
    return FALSE;
  }
no_sink_pad:
  {
    GST_WARNING_OBJECT (latency_bin, "The element has no sink pad");
    return FALSE;
  }
set_target_failed:
  {
    GST_WARNING_OBJECT (latency_bin, "Could not set target pad");
    gst_object_unref (pad);
    return FALSE;
  }
}


static gboolean
gst_rtsp_latency_bin_element_query (GstElement * element, GstQuery * query)
{
  gboolean ret = TRUE;

  GST_LOG_OBJECT (element, "got query %s", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
      /* ignoring latency query, we do not want our element to affect latency on
       * the rest of the pipeline */
      GST_DEBUG_OBJECT (element, "ignoring latency query");
      gst_query_set_latency (query, FALSE, 0, -1);
      break;
    default:
      ret =
          GST_ELEMENT_CLASS (gst_rtsp_latency_bin_parent_class)->query
          (GST_ELEMENT (element), query);
      break;
  }

  return ret;
}

static gboolean
gst_rtsp_latency_bin_element_event (GstElement * element, GstEvent * event)
{
  gboolean ret = TRUE;

  GST_LOG_OBJECT (element, "got event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_LATENCY:
      /* ignoring latency event, we will configure latency on our element when
       * going to PLAYING */
      GST_DEBUG_OBJECT (element, "ignoring latency event");
      gst_event_unref (event);
      break;
    default:
      ret =
          GST_ELEMENT_CLASS (gst_rtsp_latency_bin_parent_class)->send_event
          (GST_ELEMENT (element), event);
      break;
  }

  return ret;
}

static gboolean
gst_rtsp_latency_bin_recalculate_latency (GstRTSPLatencyBin * latency_bin)
{
  GstRTSPLatencyBinPrivate *priv =
      gst_rtsp_latency_bin_get_instance_private (latency_bin);
  GstEvent *latency;
  GstQuery *query;
  GstClockTime min_latency;

  GST_DEBUG_OBJECT (latency_bin, "Recalculating latency");

  if (!priv->element) {
    GST_WARNING_OBJECT (latency_bin, "We do not have an element");
    return FALSE;
  }

  query = gst_query_new_latency ();

  if (!gst_element_query (priv->element, query)) {
    GST_WARNING_OBJECT (latency_bin, "Latency query failed");
    gst_query_unref (query);
    return FALSE;
  }

  gst_query_parse_latency (query, NULL, &min_latency, NULL);
  gst_query_unref (query);

  GST_LOG_OBJECT (latency_bin, "Got min_latency from stream: %"
      GST_TIME_FORMAT, GST_TIME_ARGS (min_latency));

  latency = gst_event_new_latency (min_latency);
  if (!gst_element_send_event (priv->element, latency)) {
    GST_WARNING_OBJECT (latency_bin, "Sending latency event to stream failed");
    return FALSE;
  }

  return TRUE;
}

static void
gst_rtsp_latency_bin_message_handler (GstBin * bin, GstMessage * message)
{
  GstRTSPLatencyBin *latency_bin = GST_RTSP_LATENCY_BIN (bin);

  GST_LOG_OBJECT (bin, "Got message %s", GST_MESSAGE_TYPE_NAME (message));

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_LATENCY:{
      if (!gst_rtsp_latency_bin_recalculate_latency (latency_bin)) {
        GST_WARNING_OBJECT (latency_bin, "Could not recalculate latency");
      }
      break;
    }
    default:
      GST_BIN_CLASS (gst_rtsp_latency_bin_parent_class)->handle_message (bin,
          message);
      break;
  }
}

static GstStateChangeReturn
gst_rtsp_latency_bin_change_state (GstElement * element, GstStateChange
    transition)
{
  GstRTSPLatencyBin *latency_bin = GST_RTSP_LATENCY_BIN (element);
  GstStateChangeReturn ret;

  GST_LOG_OBJECT (latency_bin, "Changing state %s",
      gst_state_change_get_name (transition));

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    case GST_STATE_CHANGE_PLAYING_TO_PLAYING:
      if (!gst_rtsp_latency_bin_recalculate_latency (latency_bin)) {
        GST_WARNING_OBJECT (latency_bin, "Could not recalculate latency");
      }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_rtsp_latency_bin_parent_class)->change_state
      (element, transition);

  return ret;
}

/**
 * gst_rtsp_latency_bin_new:
 * @element: (transfer full): a #GstElement
 *
 * Create a bin that encapsulates an @element and prevents it from affecting
 * latency on the whole pipeline.
 *
 * Returns: A newly created #GstRTSPLatencyBin element, or %NULL on failure
 */
GstElement *
gst_rtsp_latency_bin_new (GstElement * element)
{
  GstElement *gst_rtsp_latency_bin;

  g_return_val_if_fail (element, NULL);

  gst_rtsp_latency_bin = g_object_new (GST_RTSP_LATENCY_BIN_TYPE, "element",
      element, NULL);
  gst_object_unref (element);
  return gst_rtsp_latency_bin;
}
