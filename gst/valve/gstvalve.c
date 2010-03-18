/*
 * Farsight Voice+Video library
 *
 *  Copyright 2007 Collabora Ltd, 
 *  Copyright 2007 Nokia Corporation
 *   @author: Olivier Crete <olivier.crete@collabora.co.uk>
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
 *
 */
/**
 * SECTION:element-valve
 *
 * The valve is a simple element that drops buffers when the #GstValve::drop
 * property is set to %TRUE and lets then through otherwise.
 *
 * Any downstream error received while the #GstValve::drop property is %FALSE
 * is ignored. So downstream element can be set to  %GST_STATE_NULL and removed,
 * without using pad blocking.
 *
 * Last reviewed on 2008-02-10 (0.10.11)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvalve.h"

#include <string.h>

GST_DEBUG_CATEGORY (valve_debug);
#define GST_CAT_DEFAULT (valve_debug)

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* Valve signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DROP,
};




static void gst_valve_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_valve_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_valve_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_valve_buffer_alloc (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);
static GstFlowReturn gst_valve_chain (GstPad * pad, GstBuffer * buffer);
static GstCaps *gst_valve_getcaps (GstPad * pad);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (valve_debug, "valve", 0, "Valve");
}

GST_BOILERPLATE_FULL (GstValve, gst_valve, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_valve_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details_simple (element_class, "Valve element",
      "Filter",
      "This element drops all packets when drop is TRUE",
      "Olivier Crete <olivier.crete@collabora.co.uk>");
}

static void
gst_valve_class_init (GstValveClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_valve_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_valve_get_property);

  g_object_class_install_property (gobject_class, ARG_DROP,
      g_param_spec_boolean ("drop",
          "Drops all buffers if TRUE",
          "If this property if TRUE, the element will drop all buffers, if its FALSE, it will let them through",
          FALSE, G_PARAM_READWRITE));

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_valve_init (GstValve * valve, GstValveClass * klass)
{
  valve->drop = FALSE;
  valve->discont = FALSE;

  valve->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  gst_pad_set_getcaps_function (valve->srcpad,
      GST_DEBUG_FUNCPTR (gst_valve_getcaps));
  gst_element_add_pad (GST_ELEMENT (valve), valve->srcpad);

  valve->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (valve->sinkpad,
      GST_DEBUG_FUNCPTR (gst_valve_chain));
  gst_pad_set_event_function (valve->sinkpad,
      GST_DEBUG_FUNCPTR (gst_valve_event));
  gst_pad_set_bufferalloc_function (valve->sinkpad,
      GST_DEBUG_FUNCPTR (gst_valve_buffer_alloc));
  gst_pad_set_getcaps_function (valve->sinkpad,
      GST_DEBUG_FUNCPTR (gst_valve_getcaps));
  gst_element_add_pad (GST_ELEMENT (valve), valve->sinkpad);
}


static void
gst_valve_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstValve *valve = GST_VALVE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    case ARG_DROP:
      GST_OBJECT_LOCK (object);
      valve->drop = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (object);
      break;
  }
}

static void
gst_valve_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstValve *valve = GST_VALVE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    case ARG_DROP:
      GST_OBJECT_LOCK (object);
      g_value_set_boolean (value, valve->drop);
      GST_OBJECT_UNLOCK (object);
      break;
  }
}

static GstFlowReturn
gst_valve_chain (GstPad * pad, GstBuffer * buffer)
{
  GstValve *valve = GST_VALVE (gst_pad_get_parent_element (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean drop;

  GST_OBJECT_LOCK (GST_OBJECT (valve));
  drop = valve->drop;

  if (!drop && valve->discont) {
    buffer = gst_buffer_make_metadata_writable (buffer);
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    valve->discont = FALSE;
  }
  GST_OBJECT_UNLOCK (GST_OBJECT (valve));

  if (drop)
    gst_buffer_unref (buffer);
  else
    ret = gst_pad_push (valve->srcpad, buffer);


  GST_OBJECT_LOCK (GST_OBJECT (valve));
  if (valve->drop)
    ret = GST_FLOW_OK;
  GST_OBJECT_UNLOCK (GST_OBJECT (valve));

  gst_object_unref (valve);

  return ret;
}


static gboolean
gst_valve_event (GstPad * pad, GstEvent * event)
{
  GstValve *valve = GST_VALVE (gst_pad_get_parent_element (pad));
  gboolean ret = TRUE;
  gboolean drop;

  GST_OBJECT_LOCK (GST_OBJECT (valve));
  drop = valve->drop;
  GST_OBJECT_UNLOCK (GST_OBJECT (valve));

  if (drop)
    gst_event_unref (event);
  else
    ret = gst_pad_push_event (valve->srcpad, event);

  GST_OBJECT_LOCK (GST_OBJECT (valve));
  if (valve->drop)
    ret = TRUE;
  GST_OBJECT_UNLOCK (GST_OBJECT (valve));

  gst_object_unref (valve);
  return ret;
}

static GstFlowReturn
gst_valve_buffer_alloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstValve *valve = GST_VALVE (gst_pad_get_parent_element (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean drop;

  GST_OBJECT_LOCK (GST_OBJECT (valve));
  drop = valve->drop;
  GST_OBJECT_UNLOCK (GST_OBJECT (valve));

  if (drop)
    *buf = NULL;
  else
    ret = gst_pad_alloc_buffer (valve->srcpad, offset, size, caps, buf);

  GST_OBJECT_LOCK (GST_OBJECT (valve));
  if (valve->drop)
    ret = GST_FLOW_OK;
  GST_OBJECT_UNLOCK (GST_OBJECT (valve));

  gst_object_unref (valve);

  return ret;
}

static GstCaps *
gst_valve_getcaps (GstPad * pad)
{
  GstValve *valve = GST_VALVE (gst_pad_get_parent (pad));
  GstCaps *caps;

  if (pad == valve->sinkpad)
    caps = gst_pad_peer_get_caps (valve->srcpad);
  else
    caps = gst_pad_peer_get_caps (valve->sinkpad);

  if (caps == NULL)
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  gst_object_unref (valve);

  return caps;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "valve",
      GST_RANK_MARGINAL, GST_TYPE_VALVE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "valve",
    "Valve",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
