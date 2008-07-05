/*
 * GStreamer
 * Copyright (C) 2005 Martin Eikermann <meiker@upb.de>
 * Copyright (C) 2008 Sebastian Dröge <slomo@collabora.co.uk>
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

#include "gstdeinterlace2.h"
#include <gst/gst.h>
#include <gst/video/video.h>

#include "tvtime/plugins.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (deinterlace2_debug);
#define GST_CAT_DEFAULT (deinterlace2_debug)

/* Object signals and args */
enum
{
  LAST_SIGNAL
};

/* Arguments */
enum
{
  ARG_0,
  ARG_METHOD,
  ARG_FIELDS,
  ARG_FIELD_LAYOUT
};

G_DEFINE_TYPE (GstDeinterlaceMethod, gst_deinterlace_method, GST_TYPE_OBJECT);

static void
gst_deinterlace_method_class_init (GstDeinterlaceMethodClass * klass)
{
  klass->available = TRUE;
}

static void
gst_deinterlace_method_init (GstDeinterlaceMethod * self)
{

}

static void
gst_deinterlace_method_deinterlace_frame (GstDeinterlaceMethod * self,
    GstDeinterlace2 * object)
{
  GstDeinterlaceMethodClass *klass = GST_DEINTERLACE_METHOD_GET_CLASS (self);

  klass->deinterlace_frame (self, object);
}

static gint
gst_deinterlace_method_get_fields_required (GstDeinterlaceMethod * self)
{
  GstDeinterlaceMethodClass *klass = GST_DEINTERLACE_METHOD_GET_CLASS (self);

  return klass->fields_required;
}

static gint
gst_deinterlace_method_get_latency (GstDeinterlaceMethod * self)
{
  GstDeinterlaceMethodClass *klass = GST_DEINTERLACE_METHOD_GET_CLASS (self);

  return klass->latency;
}

#define GST_TYPE_DEINTERLACE2_METHODS (gst_deinterlace2_methods_get_type ())
static GType
gst_deinterlace2_methods_get_type (void)
{
  static GType deinterlace2_methods_type = 0;

  static const GEnumValue methods_types[] = {
    {GST_DEINTERLACE2_TOMSMOCOMP, "Toms Motion Compensation", "tomsmocomp"},
    {GST_DEINTERLACE2_GREEDY_H, "Greedy High Motion", "greedyh"},
    {GST_DEINTERLACE2_GREEDY_L, "Greedy Low Motion", "greedyl"},
    {GST_DEINTERLACE2_VFIR, "Vertical Blur", "vfir"},
    {0, NULL, NULL},
  };

  if (!deinterlace2_methods_type) {
    deinterlace2_methods_type =
        g_enum_register_static ("GstDeinterlace2Methods", methods_types);
  }
  return deinterlace2_methods_type;
}

#define GST_TYPE_DEINTERLACE2_FIELDS (gst_deinterlace2_fields_get_type ())
static GType
gst_deinterlace2_fields_get_type (void)
{
  static GType deinterlace2_fields_type = 0;

  static const GEnumValue fields_types[] = {
    {GST_DEINTERLACE2_ALL, "All fields", "all"},
    {GST_DEINTERLACE2_TF, "Top fields only", "top"},
    {GST_DEINTERLACE2_BF, "Bottom fields only", "bottom"},
    {0, NULL, NULL},
  };

  if (!deinterlace2_fields_type) {
    deinterlace2_fields_type =
        g_enum_register_static ("GstDeinterlace2Fields", fields_types);
  }
  return deinterlace2_fields_type;
}

#define GST_TYPE_DEINTERLACE2_FIELD_LAYOUT (gst_deinterlace2_field_layout_get_type ())
static GType
gst_deinterlace2_field_layout_get_type (void)
{
  static GType deinterlace2_field_layout_type = 0;

  static const GEnumValue field_layout_types[] = {
    {GST_DEINTERLACE2_LAYOUT_AUTO, "Auto detection", "auto"},
    {GST_DEINTERLACE2_LAYOUT_TFF, "Top field first", "tff"},
    {GST_DEINTERLACE2_LAYOUT_BFF, "Bottom field first", "bff"},
    {0, NULL, NULL},
  };

  if (!deinterlace2_field_layout_type) {
    deinterlace2_field_layout_type =
        g_enum_register_static ("GstDeinterlace2FieldLayout",
        field_layout_types);
  }
  return deinterlace2_field_layout_type;
}

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2"))
    );

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2"))
    );

static void gst_deinterlace2_finalize (GObject * object);
static void gst_deinterlace2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_deinterlace2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_deinterlace2_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_deinterlace2_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_deinterlace2_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn gst_deinterlace2_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_deinterlace2_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_deinterlace2_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_deinterlace2_src_query_types (GstPad * pad);

static void gst_deinterlace2_reset (GstDeinterlace2 * object);

static void gst_deinterlace2_child_proxy_interface_init (gpointer g_iface,
    gpointer iface_data);

static void
_do_init (GType object_type)
{
  const GInterfaceInfo child_proxy_interface_info = {
    (GInterfaceInitFunc) gst_deinterlace2_child_proxy_interface_init,
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_CHILD_PROXY,
      &child_proxy_interface_info);
}

GST_BOILERPLATE_FULL (GstDeinterlace2, gst_deinterlace2, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_deinterlace2_set_method (GstDeinterlace2 * object,
    GstDeinterlace2Methods method)
{

  if (object->method) {
    gst_child_proxy_child_removed (GST_OBJECT (object),
        GST_OBJECT (object->method));
    gst_object_unparent (GST_OBJECT (object->method));
    object->method = NULL;
  }

  switch (method) {
    case GST_DEINTERLACE2_TOMSMOCOMP:
      object->method = g_object_new (GST_TYPE_DEINTERLACE_TOMSMOCOMP, NULL);
      break;
    case GST_DEINTERLACE2_GREEDY_H:
      object->method = g_object_new (GST_TYPE_DEINTERLACE_GREEDY_H, NULL);
      break;
    case GST_DEINTERLACE2_GREEDY_L:
      object->method = g_object_new (GST_TYPE_DEINTERLACE_GREEDY_L, NULL);
      break;
    case GST_DEINTERLACE2_VFIR:
      object->method = g_object_new (GST_TYPE_DEINTERLACE_VFIR, NULL);
      break;
    default:
      GST_WARNING ("Invalid Deinterlacer Method");
      return;
  }

  object->method_id = method;

  gst_object_set_name (GST_OBJECT (object->method), "method");
  gst_object_set_parent (GST_OBJECT (object->method), GST_OBJECT (object));
  gst_child_proxy_child_added (GST_OBJECT (object),
      GST_OBJECT (object->method));
}

static void
gst_deinterlace2_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));

  gst_element_class_set_details_simple (element_class,
      "Deinterlacer",
      "Filter/Video",
      "Deinterlace Methods ported from DScaler/TvTime",
      "Martin Eikermann <meiker@upb.de>, "
      "Sebastian Dröge <slomo@circular-chaos.org>");
}

static void
gst_deinterlace2_class_init (GstDeinterlace2Class * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  GstElementClass *element_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_deinterlace2_set_property;
  gobject_class->get_property = gst_deinterlace2_get_property;
  gobject_class->finalize = gst_deinterlace2_finalize;

  g_object_class_install_property (gobject_class, ARG_METHOD,
      g_param_spec_enum ("method",
          "Method",
          "Deinterlace Method",
          GST_TYPE_DEINTERLACE2_METHODS,
          GST_DEINTERLACE2_TOMSMOCOMP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  g_object_class_install_property (gobject_class, ARG_FIELDS,
      g_param_spec_enum ("fields",
          "fields",
          "Fields to use for deinterlacing",
          GST_TYPE_DEINTERLACE2_FIELDS,
          GST_DEINTERLACE2_ALL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  g_object_class_install_property (gobject_class, ARG_FIELD_LAYOUT,
      g_param_spec_enum ("tff",
          "tff",
          "Deinterlace top field first",
          GST_TYPE_DEINTERLACE2_FIELD_LAYOUT,
          GST_DEINTERLACE2_LAYOUT_AUTO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_deinterlace2_change_state);
}

static GstObject *
gst_deinterlace2_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GstDeinterlace2 *self = GST_DEINTERLACE2 (child_proxy);

  g_return_val_if_fail (index == 0, NULL);

  return gst_object_ref (self->method);
}

static guint
gst_deinterlace2_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  return 1;
}

static void
gst_deinterlace2_child_proxy_interface_init (gpointer g_iface,
    gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  iface->get_child_by_index = gst_deinterlace2_child_proxy_get_child_by_index;
  iface->get_children_count = gst_deinterlace2_child_proxy_get_children_count;
}

static void
gst_deinterlace2_init (GstDeinterlace2 * object, GstDeinterlace2Class * klass)
{
  object->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_chain_function (object->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_chain));
  gst_pad_set_event_function (object->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_sink_event));
  gst_pad_set_setcaps_function (object->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_setcaps));
  gst_pad_set_getcaps_function (object->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_element_add_pad (GST_ELEMENT (object), object->sinkpad);

  object->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (object->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_src_event));
  gst_pad_set_query_type_function (object->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_src_query_types));
  gst_pad_set_query_function (object->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_src_query));
  gst_pad_set_setcaps_function (object->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_setcaps));
  gst_pad_set_getcaps_function (object->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_element_add_pad (GST_ELEMENT (object), object->srcpad);

  gst_element_no_more_pads (GST_ELEMENT (object));

  gst_deinterlace2_set_method (object, GST_DEINTERLACE2_TOMSMOCOMP);
  object->field_layout = GST_DEINTERLACE2_LAYOUT_AUTO;
  object->fields = GST_DEINTERLACE2_ALL;

  gst_deinterlace2_reset (object);
}

static void
gst_deinterlace2_reset_history (GstDeinterlace2 * object)
{
  gint i;

  for (i = 0; i < object->history_count; i++) {
    if (object->field_history[i].buf) {
      gst_buffer_unref (object->field_history[i].buf);
      object->field_history[i].buf = NULL;
    }
  }
  memset (object->field_history, 0, MAX_FIELD_HISTORY * sizeof (GstPicture));
  object->history_count = 0;
}

static void
gst_deinterlace2_reset (GstDeinterlace2 * object)
{
  if (object->out_buf) {
    gst_buffer_unref (object->out_buf);
    object->out_buf = NULL;
  }

  object->output_stride = 0;
  object->line_length = 0;
  object->frame_width = 0;
  object->frame_height = 0;
  object->frame_rate_n = 0;
  object->frame_rate_d = 0;
  object->field_height = 0;
  object->field_stride = 0;

  gst_deinterlace2_reset_history (object);
}

static void
gst_deinterlace2_set_property (GObject * _object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDeinterlace2 *object;

  g_return_if_fail (GST_IS_DEINTERLACE2 (_object));
  object = GST_DEINTERLACE2 (_object);

  switch (prop_id) {
    case ARG_METHOD:
      gst_deinterlace2_set_method (object, g_value_get_enum (value));
      break;
    case ARG_FIELDS:{
      gint oldfields;

      GST_OBJECT_LOCK (object);
      oldfields = object->fields;
      object->fields = g_value_get_enum (value);
      if (object->fields != oldfields && GST_PAD_CAPS (object->srcpad))
        gst_deinterlace2_setcaps (object->sinkpad,
            GST_PAD_CAPS (object->sinkpad));
      GST_OBJECT_UNLOCK (object);
      break;
    }
    case ARG_FIELD_LAYOUT:
      object->field_layout = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }

}

static void
gst_deinterlace2_get_property (GObject * _object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDeinterlace2 *object;

  g_return_if_fail (GST_IS_DEINTERLACE2 (_object));
  object = GST_DEINTERLACE2 (_object);

  switch (prop_id) {
    case ARG_METHOD:
      g_value_set_enum (value, object->method_id);
      break;
    case ARG_FIELDS:
      g_value_set_enum (value, object->fields);
      break;
    case ARG_FIELD_LAYOUT:
      g_value_set_enum (value, object->field_layout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_deinterlace2_finalize (GObject * object)
{
  GstDeinterlace2 *self = GST_DEINTERLACE2 (object);

  gst_deinterlace2_reset (self);

  if (self->method) {
    gst_object_unparent (GST_OBJECT (self->method));
    self->method = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstBuffer *
gst_deinterlace2_pop_history (GstDeinterlace2 * object)
{
  GstBuffer *buffer = NULL;

  g_assert (object->history_count > 0);

  buffer = object->field_history[object->history_count - 1].buf;

  object->history_count--;
  GST_DEBUG ("pop, size(history): %d", object->history_count);

  return buffer;
}

#if 0
static GstBuffer *
gst_deinterlace2_head_history (GstDeinterlace2 * object)
{
  return object->field_history[object->history_count - 1].buf;
}
#endif


/* invariant: field with smallest timestamp is object->field_history[object->history_count-1]

*/

static void
gst_deinterlace2_push_history (GstDeinterlace2 * object, GstBuffer * buffer)
{
  int i = 1;
  GstClockTime timestamp;

  g_assert (object->history_count < MAX_FIELD_HISTORY - 2);

  for (i = MAX_FIELD_HISTORY - 1; i >= 2; i--) {
    object->field_history[i].buf = object->field_history[i - 2].buf;
    object->field_history[i].flags = object->field_history[i - 2].flags;
  }

  if (object->field_layout == GST_DEINTERLACE2_LAYOUT_AUTO) {
    GST_WARNING ("Could not detect field layout. Assuming top field first.");
    object->field_layout = GST_DEINTERLACE2_LAYOUT_TFF;
  }


  if (object->field_layout == GST_DEINTERLACE2_LAYOUT_TFF) {
    GST_DEBUG ("Top field first");
    object->field_history[0].buf =
        gst_buffer_create_sub (buffer, object->line_length,
        GST_BUFFER_SIZE (buffer) - object->line_length);
    object->field_history[0].flags = PICTURE_INTERLACED_BOTTOM;
    object->field_history[1].buf = buffer;
    object->field_history[1].flags = PICTURE_INTERLACED_TOP;
  } else {
    GST_DEBUG ("Bottom field first");
    object->field_history[0].buf = buffer;
    object->field_history[0].flags = PICTURE_INTERLACED_TOP;
    object->field_history[1].buf =
        gst_buffer_create_sub (buffer, object->line_length,
        GST_BUFFER_SIZE (buffer) - object->line_length);
    object->field_history[1].flags = PICTURE_INTERLACED_BOTTOM;
  }

  /* Timestamps are assigned to the field buffers under the assumption that
     the timestamp of the buffer equals the first fields timestamp */

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_TIMESTAMP (object->field_history[0].buf) =
      timestamp + object->field_duration;
  GST_BUFFER_TIMESTAMP (object->field_history[1].buf) = timestamp;

  object->history_count += 2;
  GST_DEBUG ("push, size(history): %d", object->history_count);
}

static GstFlowReturn
gst_deinterlace2_chain (GstPad * pad, GstBuffer * buf)
{
  GstDeinterlace2 *object = NULL;
  GstClockTime timestamp;
  GstFlowReturn ret = GST_FLOW_OK;
  gint fields_required = 0;
  gint cur_field_idx = 0;

  object = GST_DEINTERLACE2 (GST_PAD_PARENT (pad));

  gst_deinterlace2_push_history (object, buf);
  buf = NULL;

  fields_required = gst_deinterlace_method_get_fields_required (object->method);

  /* Not enough fields in the history */
  if (object->history_count < fields_required + 1) {
    /* TODO: do bob or just forward frame */
    GST_DEBUG ("HistoryCount=%d", object->history_count);
    return GST_FLOW_OK;
  }

  while (object->history_count >= fields_required) {
    if (object->fields == GST_DEINTERLACE2_ALL)
      GST_DEBUG ("All fields");
    if (object->fields == GST_DEINTERLACE2_TF)
      GST_DEBUG ("Top fields");
    if (object->fields == GST_DEINTERLACE2_BF)
      GST_DEBUG ("Bottom fields");

    cur_field_idx = object->history_count - fields_required;

    if ((object->field_history[cur_field_idx].flags == PICTURE_INTERLACED_TOP
            && object->fields == GST_DEINTERLACE2_TF) ||
        object->fields == GST_DEINTERLACE2_ALL) {
      GST_DEBUG ("deinterlacing top field");

      /* create new buffer */
      ret = gst_pad_alloc_buffer_and_set_caps (object->srcpad,
          GST_BUFFER_OFFSET_NONE, object->frame_size,
          GST_PAD_CAPS (object->srcpad), &object->out_buf);
      if (ret != GST_FLOW_OK)
        return ret;

      /* do magic calculus */
      gst_deinterlace_method_deinterlace_frame (object->method, object);

      buf = gst_deinterlace2_pop_history (object);
      timestamp = GST_BUFFER_TIMESTAMP (buf);
      gst_buffer_unref (buf);

      GST_BUFFER_TIMESTAMP (object->out_buf) = timestamp;
      if (object->fields == GST_DEINTERLACE2_ALL)
        GST_BUFFER_DURATION (object->out_buf) = object->field_duration;
      else
        GST_BUFFER_DURATION (object->out_buf) = 2 * object->field_duration;

      ret = gst_pad_push (object->srcpad, object->out_buf);
      object->out_buf = NULL;
      if (ret != GST_FLOW_OK)
        return ret;
    }
    /* no calculation done: remove excess field */
    else if (object->field_history[cur_field_idx].flags ==
        PICTURE_INTERLACED_TOP && object->fields == GST_DEINTERLACE2_BF) {
      GST_DEBUG ("Removing unused top field");
      buf = gst_deinterlace2_pop_history (object);
      gst_buffer_unref (buf);
    }

    cur_field_idx = object->history_count - fields_required;
    if (object->history_count < fields_required)
      break;

    /* deinterlace bottom_field */
    if ((object->field_history[cur_field_idx].flags == PICTURE_INTERLACED_BOTTOM
            && object->fields == GST_DEINTERLACE2_BF) ||
        object->fields == GST_DEINTERLACE2_ALL) {
      GST_DEBUG ("deinterlacing bottom field");

      /* create new buffer */
      ret = gst_pad_alloc_buffer_and_set_caps (object->srcpad,
          GST_BUFFER_OFFSET_NONE, object->frame_size,
          GST_PAD_CAPS (object->srcpad), &object->out_buf);
      if (ret != GST_FLOW_OK)
        return ret;

      /* do magic calculus */
      gst_deinterlace_method_deinterlace_frame (object->method, object);

      buf = gst_deinterlace2_pop_history (object);
      timestamp = GST_BUFFER_TIMESTAMP (buf);
      gst_buffer_unref (buf);

      GST_BUFFER_TIMESTAMP (object->out_buf) = timestamp;
      if (object->fields == GST_DEINTERLACE2_ALL)
        GST_BUFFER_DURATION (object->out_buf) = object->field_duration;
      else
        GST_BUFFER_DURATION (object->out_buf) = 2 * object->field_duration;

      ret = gst_pad_push (object->srcpad, object->out_buf);
      object->out_buf = NULL;

      if (ret != GST_FLOW_OK)
        return ret;
    }
    /* no calculation done: remove excess field */
    else if (object->field_history[cur_field_idx].flags ==
        PICTURE_INTERLACED_BOTTOM && object->fields == GST_DEINTERLACE2_TF) {
      GST_DEBUG ("Removing unused bottom field");
      buf = gst_deinterlace2_pop_history (object);
      gst_buffer_unref (buf);
    }
  }

  GST_DEBUG ("----chain end ----\n\n");

  return ret;
}

static gboolean
gst_deinterlace2_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean res = TRUE;
  GstDeinterlace2 *object = GST_DEINTERLACE2 (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstStructure *structure;
  GstVideoFormat fmt;
  guint32 fourcc;
  GstCaps *othercaps;

  otherpad = (pad == object->srcpad) ? object->sinkpad : object->srcpad;

  structure = gst_caps_get_structure (caps, 0);

  res = gst_structure_get_int (structure, "width", &object->frame_width);
  res &= gst_structure_get_int (structure, "height", &object->frame_height);
  res &=
      gst_structure_get_fraction (structure, "framerate", &object->frame_rate_n,
      &object->frame_rate_d);
  res &= gst_structure_get_fourcc (structure, "format", &fourcc);
  /* TODO: get interlaced, field_layout, field_order */
  if (!res)
    goto invalid_caps;

  if (object->fields == GST_DEINTERLACE2_ALL) {
    gint fps_n = object->frame_rate_n, fps_d = object->frame_rate_d;

    othercaps = gst_caps_copy (caps);

    if (otherpad == object->srcpad)
      fps_n *= 2;
    else
      fps_d *= 2;

    gst_caps_set_simple (othercaps, "framerate", GST_TYPE_FRACTION, fps_n,
        fps_d, NULL);
  } else {
    othercaps = gst_caps_ref (caps);
  }

  if (!gst_pad_set_caps (otherpad, othercaps))
    goto caps_not_accepted;
  gst_caps_unref (othercaps);

  /* TODO: introduce object->field_stride */
  object->field_height = object->frame_height / 2;

  fmt = gst_video_format_from_fourcc (fourcc);

  /* TODO: only true if fields are subbuffers of interlaced frames,
     change when the buffer-fields concept has landed */
  object->field_stride =
      gst_video_format_get_row_stride (fmt, 0, object->frame_width) * 2;
  object->output_stride =
      gst_video_format_get_row_stride (fmt, 0, object->frame_width);

  /* in bytes */
  object->line_length =
      gst_video_format_get_row_stride (fmt, 0, object->frame_width);
  object->frame_size =
      gst_video_format_get_size (fmt, object->frame_width,
      object->frame_height);

  if (object->fields == GST_DEINTERLACE2_ALL && otherpad == object->srcpad)
    object->field_duration =
        gst_util_uint64_scale (GST_SECOND, object->frame_rate_d,
        object->frame_rate_n);
  else
    object->field_duration =
        gst_util_uint64_scale (GST_SECOND, object->frame_rate_d,
        2 * object->frame_rate_n);

  GST_DEBUG_OBJECT (object, "Set caps: %" GST_PTR_FORMAT, caps);

done:

  gst_object_unref (object);
  return res;

invalid_caps:
  res = FALSE;
  GST_ERROR_OBJECT (object, "Invalid caps: %" GST_PTR_FORMAT, caps);
  goto done;

caps_not_accepted:
  res = FALSE;
  GST_ERROR_OBJECT (object, "Caps not accepted: %" GST_PTR_FORMAT, othercaps);
  gst_caps_unref (othercaps);
  goto done;
}

static gboolean
gst_deinterlace2_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstDeinterlace2 *object = GST_DEINTERLACE2 (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_EOS:
    case GST_EVENT_NEWSEGMENT:
      gst_deinterlace2_reset_history (object);

      /* fall through */
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (object);
  return res;
}

static GstStateChangeReturn
gst_deinterlace2_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDeinterlace2 *object = GST_DEINTERLACE2 (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_deinterlace2_reset (object);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }

  return ret;
}

static gboolean
gst_deinterlace2_src_event (GstPad * pad, GstEvent * event)
{
  GstDeinterlace2 *object = GST_DEINTERLACE2 (gst_pad_get_parent (pad));
  gboolean res;

  GST_DEBUG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (object);

  return res;
}

static gboolean
gst_deinterlace2_src_query (GstPad * pad, GstQuery * query)
{
  GstDeinterlace2 *object = GST_DEINTERLACE2 (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  GST_LOG_OBJECT (object, "%s query", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;
      GstPad *peer;

      if ((peer = gst_pad_get_peer (object->sinkpad))) {
        if ((res = gst_pad_query (peer, query))) {
          GstClockTime latency;
          gint fields_required = 0;
          gint method_latency = 0;

          if (object->method) {
            fields_required =
                gst_deinterlace_method_get_fields_required (object->method);
            method_latency =
                gst_deinterlace_method_get_latency (object->method);
          }

          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG ("Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          /* add our own latency */
          latency = (fields_required + method_latency) * object->field_duration;

          GST_DEBUG ("Our latency: min %" GST_TIME_FORMAT
              ", max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (latency), GST_TIME_ARGS (latency));

          min += latency;
          if (max != GST_CLOCK_TIME_NONE)
            max += latency;
          else
            max = latency;

          GST_DEBUG ("Calculated total latency : min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          gst_query_set_latency (query, live, min, max);
        }
        gst_object_unref (peer);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (object);
  return res;
}

static const GstQueryType *
gst_deinterlace2_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_LATENCY,
    GST_QUERY_NONE
  };
  return types;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (deinterlace2_debug, "deinterlace2", 0,
      "Deinterlacer");

  oil_init ();

  if (!gst_element_register (plugin, "deinterlace2", GST_RANK_NONE,
          GST_TYPE_DEINTERLACE2)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "deinterlace2",
    "Deinterlacer", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
