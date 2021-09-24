/* GStreamer valve element
 *  Copyright 2007-2009 Collabora Ltd
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
 *  Copyright 2007-2009 Nokia Corporation
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
 *
 */

/**
 * SECTION:element-valve
 * @title: valve
 *
 * The valve is a simple element that drops buffers when the #GstValve:drop
 * property is set to %TRUE and lets then through otherwise.
 *
 * Any downstream error received while the #GstValve:drop property is %TRUE
 * is ignored. So downstream element can be set to  %GST_STATE_NULL and removed,
 * without using pad blocking.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvalve.h"
#include "gstcoreelementselements.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (valve_debug);
#define GST_CAT_DEFAULT (valve_debug)

#define GST_TYPE_VALVE_DROP_MODE (gst_valve_drop_mode_get_type ())
static GType
gst_valve_drop_mode_get_type (void)
{
  static GType drop_mode_type = 0;
  static const GEnumValue drop_mode[] = {
    {GST_VALVE_DROP_MODE_DROP_ALL, "Drop all buffers and events", "drop-all"},
    {GST_VALVE_DROP_MODE_FORWARD_STICKY_EVENTS,
        "Drop all buffers but forward sticky events", "forward-sticky-events"},
    {GST_VALVE_DROP_MODE_TRANSFORM_TO_GAP,
          "Convert all dropped buffers into gap events and forward sticky events",
        "transform-to-gap"},
    {0, NULL, NULL},
  };

  if (!drop_mode_type) {
    drop_mode_type = g_enum_register_static ("GstValveDropMode", drop_mode);
  }
  return drop_mode_type;
}

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_DROP,
  PROP_DROP_MODE
};

#define DEFAULT_DROP FALSE
#define DEFAULT_DROP_MODE GST_VALVE_DROP_MODE_DROP_ALL

static void gst_valve_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_valve_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_valve_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_valve_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_valve_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (valve_debug, "valve", 0, "Valve");
#define gst_valve_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstValve, gst_valve, GST_TYPE_ELEMENT, _do_init);
GST_ELEMENT_REGISTER_DEFINE (valve, "valve", GST_RANK_NONE, GST_TYPE_VALVE);

static void
gst_valve_class_init (GstValveClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) (klass);

  gobject_class->set_property = gst_valve_set_property;
  gobject_class->get_property = gst_valve_get_property;

  g_object_class_install_property (gobject_class, PROP_DROP,
      g_param_spec_boolean ("drop", "Drop buffers and events",
          "Whether to drop buffers and events or let them through",
          DEFAULT_DROP, G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  /**
   * GstValve:drop-mode
   *
   * Drop mode to use. By default all buffers and events are dropped.
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_DROP_MODE,
      g_param_spec_enum ("drop-mode", "Drop mode",
          "The drop mode to use", GST_TYPE_VALVE_DROP_MODE,
          DEFAULT_DROP_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gst_element_class_set_static_metadata (gstelement_class, "Valve element",
      "Filter", "Drops buffers and events or lets them through",
      "Olivier Crete <olivier.crete@collabora.co.uk>");

  gst_type_mark_as_plugin_api (GST_TYPE_VALVE_DROP_MODE, 0);
}

static void
gst_valve_init (GstValve * valve)
{
  valve->drop = FALSE;
  valve->drop_mode = DEFAULT_DROP;
  valve->discont = FALSE;

  valve->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  gst_pad_set_query_function (valve->srcpad,
      GST_DEBUG_FUNCPTR (gst_valve_query));
  GST_PAD_SET_PROXY_CAPS (valve->srcpad);
  gst_element_add_pad (GST_ELEMENT (valve), valve->srcpad);

  valve->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (valve->sinkpad,
      GST_DEBUG_FUNCPTR (gst_valve_chain));
  gst_pad_set_event_function (valve->sinkpad,
      GST_DEBUG_FUNCPTR (gst_valve_sink_event));
  gst_pad_set_query_function (valve->sinkpad,
      GST_DEBUG_FUNCPTR (gst_valve_query));
  GST_PAD_SET_PROXY_CAPS (valve->sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION (valve->sinkpad);
  gst_element_add_pad (GST_ELEMENT (valve), valve->sinkpad);
}


static void
gst_valve_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstValve *valve = GST_VALVE (object);

  switch (prop_id) {
    case PROP_DROP:
      g_atomic_int_set (&valve->drop, g_value_get_boolean (value));
      gst_pad_push_event (valve->sinkpad, gst_event_new_reconfigure ());
      break;
    case PROP_DROP_MODE:
      valve->drop_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_valve_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstValve *valve = GST_VALVE (object);

  switch (prop_id) {
    case PROP_DROP:
      g_value_set_boolean (value, g_atomic_int_get (&valve->drop));
      break;
    case PROP_DROP_MODE:
      g_value_set_enum (value, valve->drop_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
forward_sticky_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstValve *valve = user_data;

  if (!gst_pad_push_event (valve->srcpad, gst_event_ref (*event)))
    valve->need_repush_sticky = TRUE;

  return TRUE;
}

static void
gst_valve_repush_sticky (GstValve * valve)
{
  valve->need_repush_sticky = FALSE;
  gst_pad_sticky_events_foreach (valve->sinkpad, forward_sticky_events, valve);
}

static GstFlowReturn
gst_valve_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstValve *valve = GST_VALVE (parent);
  GstFlowReturn ret = GST_FLOW_OK;

  if (g_atomic_int_get (&valve->drop)) {
    if (valve->drop_mode == GST_VALVE_DROP_MODE_TRANSFORM_TO_GAP) {
      GstEvent *ev = gst_event_new_gap (GST_BUFFER_PTS (buffer),
          GST_BUFFER_DURATION (buffer));
      gst_pad_push_event (valve->srcpad, ev);
    }
    gst_buffer_unref (buffer);
    valve->discont = TRUE;
  } else {
    if (valve->discont) {
      buffer = gst_buffer_make_writable (buffer);
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      valve->discont = FALSE;
    }

    if (valve->need_repush_sticky)
      gst_valve_repush_sticky (valve);

    ret = gst_pad_push (valve->srcpad, buffer);
  }


  /* Ignore errors if "drop" was changed while the thread was blocked
   * downwards
   */
  if (g_atomic_int_get (&valve->drop))
    ret = GST_FLOW_OK;

  return ret;
}

static inline gboolean
gst_valve_event_needs_dropping (GstValve * valve, GstEvent * event)
{
  if (!g_atomic_int_get (&valve->drop))
    return FALSE;

  switch (valve->drop_mode) {
    case GST_VALVE_DROP_MODE_DROP_ALL:
      return TRUE;
    case GST_VALVE_DROP_MODE_FORWARD_STICKY_EVENTS:
      return !GST_EVENT_IS_STICKY (event);
    case GST_VALVE_DROP_MODE_TRANSFORM_TO_GAP:
      return (!GST_EVENT_IS_STICKY (event) &&
          GST_EVENT_TYPE (event) != GST_EVENT_GAP);
    default:
      g_assert_not_reached ();
      break;
  }

  return FALSE;
}

static gboolean
gst_valve_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstValve *valve = GST_VALVE (parent);
  gboolean needs_dropping = gst_valve_event_needs_dropping (valve, event);
  gboolean is_sticky = GST_EVENT_IS_STICKY (event);
  gboolean ret = TRUE;

  valve = GST_VALVE (parent);

  if (needs_dropping) {
    valve->need_repush_sticky |= is_sticky;
    gst_event_unref (event);
  } else {
    if (valve->need_repush_sticky)
      gst_valve_repush_sticky (valve);
    ret = gst_pad_event_default (pad, parent, event);
  }

  /* Ignore errors if "drop" was changed while the thread was blocked
   * downwards, or if we're dropping but forwarding sticky events nonetheless.
   */
  if (g_atomic_int_get (&valve->drop)) {
    if (valve->drop_mode == GST_VALVE_DROP_MODE_DROP_ALL)
      valve->need_repush_sticky |= is_sticky;
    ret = TRUE;
  }

  return ret;
}



static gboolean
gst_valve_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstValve *valve = GST_VALVE (parent);

  if (GST_QUERY_IS_SERIALIZED (query) && g_atomic_int_get (&valve->drop))
    return FALSE;

  return gst_pad_query_default (pad, parent, query);
}
