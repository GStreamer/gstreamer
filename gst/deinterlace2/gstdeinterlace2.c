/*
 * GStreamer
 * Copyright (C) 2005 Martin Eikermann <meiker@upb.de>
 * Copyright (C) 2008-2009 Sebastian Dröge <slomo@collabora.co.uk>
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

#include <gst/gst.h>
#include <gst/video/video.h>
#include <liboil/liboil.h>

#include "gstdeinterlace2.h"
#include "tvtime/plugins.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (deinterlace2_debug);
#define GST_CAT_DEFAULT (deinterlace2_debug)

/* Object signals and args */
enum
{
  LAST_SIGNAL
};

/* Properties */

#define DEFAULT_MODE            GST_DEINTERLACE2_MODE_INTERLACED
#define DEFAULT_METHOD          GST_DEINTERLACE2_GREEDY_H
#define DEFAULT_FIELDS          GST_DEINTERLACE2_ALL
#define DEFAULT_FIELD_LAYOUT    GST_DEINTERLACE2_LAYOUT_AUTO

enum
{
  PROP_0,
  PROP_MODE,
  PROP_METHOD,
  PROP_FIELDS,
  PROP_FIELD_LAYOUT,
  PROP_LAST
};

G_DEFINE_TYPE (GstDeinterlaceMethod, gst_deinterlace_method, GST_TYPE_OBJECT);

static void
gst_deinterlace_method_class_init (GstDeinterlaceMethodClass * klass)
{

}

static void
gst_deinterlace_method_init (GstDeinterlaceMethod * self)
{

}

static void
gst_deinterlace_method_deinterlace_frame (GstDeinterlaceMethod * self,
    GstDeinterlace2 * parent, GstBuffer * outbuf)
{
  GstDeinterlaceMethodClass *klass = GST_DEINTERLACE_METHOD_GET_CLASS (self);

  klass->deinterlace_frame (self, parent, outbuf);
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


G_DEFINE_TYPE (GstDeinterlaceSimpleMethod, gst_deinterlace_simple_method,
    GST_TYPE_DEINTERLACE_METHOD);

static void
gst_deinterlace_simple_method_interpolate_scanline (GstDeinterlaceMethod * self,
    GstDeinterlace2 * parent, guint8 * out,
    GstDeinterlaceScanlineData * scanlines, gint width)
{
  oil_memcpy (out, scanlines->m1, parent->row_stride);
}

static void
gst_deinterlace_simple_method_copy_scanline (GstDeinterlaceMethod * self,
    GstDeinterlace2 * parent, guint8 * out,
    GstDeinterlaceScanlineData * scanlines, gint width)
{
  oil_memcpy (out, scanlines->m0, parent->row_stride);
}

static void
gst_deinterlace_simple_method_deinterlace_frame (GstDeinterlaceMethod * self,
    GstDeinterlace2 * parent, GstBuffer * outbuf)
{
  GstDeinterlaceSimpleMethodClass *dsm_class =
      GST_DEINTERLACE_SIMPLE_METHOD_GET_CLASS (self);
  GstDeinterlaceMethodClass *dm_class = GST_DEINTERLACE_METHOD_GET_CLASS (self);
  GstDeinterlaceScanlineData scanlines;
  guint8 *out = GST_BUFFER_DATA (outbuf);
  guint8 *field0 = NULL, *field1 = NULL, *field2 = NULL, *field3 = NULL;
  gint cur_field_idx = parent->history_count - dm_class->fields_required;
  guint cur_field_flags = parent->field_history[cur_field_idx].flags;
  gint line;

  field0 = GST_BUFFER_DATA (parent->field_history[cur_field_idx].buf);

  g_assert (dm_class->fields_required <= 4);

  if (dm_class->fields_required >= 2)
    field1 = GST_BUFFER_DATA (parent->field_history[cur_field_idx + 1].buf);
  if (dm_class->fields_required >= 3)
    field2 = GST_BUFFER_DATA (parent->field_history[cur_field_idx + 2].buf);
  if (dm_class->fields_required >= 4)
    field3 = GST_BUFFER_DATA (parent->field_history[cur_field_idx + 3].buf);


  if (cur_field_flags == PICTURE_INTERLACED_BOTTOM) {
    /* double the first scanline of the bottom field */
    oil_memcpy (out, field0, parent->row_stride);
    out += parent->row_stride;
  }

  oil_memcpy (out, field0, parent->row_stride);
  out += parent->row_stride;

  for (line = 2; line <= parent->field_height; line++) {

    memset (&scanlines, 0, sizeof (scanlines));
    scanlines.bottom_field = (cur_field_flags == PICTURE_INTERLACED_BOTTOM);

    /* interp. scanline */
    scanlines.t0 = field0;
    scanlines.b0 = field0 + parent->field_stride;

    if (field1 != NULL) {
      scanlines.tt1 = field1;
      scanlines.m1 = field1 + parent->field_stride;
      scanlines.bb1 = field1 + parent->field_stride * 2;
      field1 += parent->field_stride;
    }

    if (field2 != NULL) {
      scanlines.t2 = field2;
      scanlines.b2 = field2 + parent->field_stride;
    }

    if (field3 != NULL) {
      scanlines.tt3 = field3;
      scanlines.m3 = field3 + parent->field_stride;
      scanlines.bb3 = field3 + parent->field_stride * 2;
      field3 += parent->field_stride;
    }

    /* set valid data for corner cases */
    if (line == 2) {
      scanlines.tt1 = scanlines.bb1;
      scanlines.tt3 = scanlines.bb3;
    } else if (line == parent->field_height) {
      scanlines.bb1 = scanlines.tt1;
      scanlines.bb3 = scanlines.tt3;
    }

    dsm_class->interpolate_scanline (self, parent, out, &scanlines,
        parent->frame_width);
    out += parent->row_stride;

    memset (&scanlines, 0, sizeof (scanlines));
    scanlines.bottom_field = (cur_field_flags == PICTURE_INTERLACED_BOTTOM);

    /* copy a scanline */
    scanlines.tt0 = field0;
    scanlines.m0 = field0 + parent->field_stride;
    scanlines.bb0 = field0 + parent->field_stride * 2;
    field0 += parent->field_stride;

    if (field1 != NULL) {
      scanlines.t1 = field1;
      scanlines.b1 = field1 + parent->field_stride;
    }

    if (field2 != NULL) {
      scanlines.tt2 = field2;
      scanlines.m2 = field2 + parent->field_stride;
      scanlines.bb2 = field2 + parent->field_stride * 2;
      field2 += parent->field_stride;
    }

    if (field3 != NULL) {
      scanlines.t3 = field3;
      scanlines.b3 = field3 + parent->field_stride;
    }

    /* set valid data for corner cases */
    if (line == parent->field_height) {
      scanlines.bb0 = scanlines.tt0;
      scanlines.b1 = scanlines.t1;
      scanlines.bb2 = scanlines.tt2;
      scanlines.b3 = scanlines.t3;
    }

    dsm_class->copy_scanline (self, parent, out, &scanlines,
        parent->frame_width);
    out += parent->row_stride;
  }

  if (cur_field_flags == PICTURE_INTERLACED_TOP) {
    /* double the last scanline of the top field */
    oil_memcpy (out, field0, parent->row_stride);
  }
}

static void
gst_deinterlace_simple_method_class_init (GstDeinterlaceSimpleMethodClass *
    klass)
{
  GstDeinterlaceMethodClass *dm_class = (GstDeinterlaceMethodClass *) klass;

  dm_class->deinterlace_frame = gst_deinterlace_simple_method_deinterlace_frame;
  dm_class->fields_required = 2;

  klass->interpolate_scanline =
      gst_deinterlace_simple_method_interpolate_scanline;
  klass->copy_scanline = gst_deinterlace_simple_method_copy_scanline;
}

static void
gst_deinterlace_simple_method_init (GstDeinterlaceSimpleMethod * self)
{
}

#define GST_TYPE_DEINTERLACE2_METHODS (gst_deinterlace2_methods_get_type ())
static GType
gst_deinterlace2_methods_get_type (void)
{
  static GType deinterlace2_methods_type = 0;

  static const GEnumValue methods_types[] = {
    {GST_DEINTERLACE2_TOMSMOCOMP, "Motion Adaptive: Motion Search",
        "tomsmocomp"},
    {GST_DEINTERLACE2_GREEDY_H, "Motion Adaptive: Advanced Detection",
        "greedyh"},
    {GST_DEINTERLACE2_GREEDY_L, "Motion Adaptive: Simple Detection", "greedyl"},
    {GST_DEINTERLACE2_VFIR, "Blur Vertical", "vfir"},
    {GST_DEINTERLACE2_LINEAR, "Television: Full resolution", "linear"},
    {GST_DEINTERLACE2_LINEAR_BLEND, "Blur: Temporal", "linearblend"},
    {GST_DEINTERLACE2_SCALER_BOB, "Double lines", "scalerbob"},
    {GST_DEINTERLACE2_WEAVE, "Weave", "weave"},
    {GST_DEINTERLACE2_WEAVE_TFF, "Progressive: Top Field First", "weavetff"},
    {GST_DEINTERLACE2_WEAVE_BFF, "Progressive: Bottom Field First", "weavebff"},
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

#define GST_TYPE_DEINTERLACE2_MODES (gst_deinterlace2_modes_get_type ())
static GType
gst_deinterlace2_modes_get_type (void)
{
  static GType deinterlace2_modes_type = 0;

  static const GEnumValue modes_types[] = {
    {GST_DEINTERLACE2_MODE_AUTO, "Auto detection", "auto"},
    {GST_DEINTERLACE2_MODE_INTERLACED, "Enfore deinterlacing", "interlaced"},
    {0, NULL, NULL},
  };

  if (!deinterlace2_modes_type) {
    deinterlace2_modes_type =
        g_enum_register_static ("GstDeinterlace2Modes", modes_types);
  }
  return deinterlace2_modes_type;
}

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2") ";"
        GST_VIDEO_CAPS_YUV ("YVYU"))
    );

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2") ";"
        GST_VIDEO_CAPS_YUV ("YVYU"))
    );

static void gst_deinterlace2_finalize (GObject * self);
static void gst_deinterlace2_set_property (GObject * self, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_deinterlace2_get_property (GObject * self, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_deinterlace2_getcaps (GstPad * pad);
static gboolean gst_deinterlace2_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_deinterlace2_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_deinterlace2_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn gst_deinterlace2_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_deinterlace2_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_deinterlace2_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_deinterlace2_src_query_types (GstPad * pad);

static void gst_deinterlace2_reset (GstDeinterlace2 * self);

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
gst_deinterlace2_set_method (GstDeinterlace2 * self,
    GstDeinterlace2Methods method)
{

  if (self->method) {
    gst_child_proxy_child_removed (GST_OBJECT (self),
        GST_OBJECT (self->method));
    gst_object_unparent (GST_OBJECT (self->method));
    self->method = NULL;
  }

  switch (method) {
    case GST_DEINTERLACE2_TOMSMOCOMP:
      self->method = g_object_new (GST_TYPE_DEINTERLACE_TOMSMOCOMP, NULL);
      break;
    case GST_DEINTERLACE2_GREEDY_H:
      self->method = g_object_new (GST_TYPE_DEINTERLACE_GREEDY_H, NULL);
      break;
    case GST_DEINTERLACE2_GREEDY_L:
      self->method = g_object_new (GST_TYPE_DEINTERLACE_GREEDY_L, NULL);
      break;
    case GST_DEINTERLACE2_VFIR:
      self->method = g_object_new (GST_TYPE_DEINTERLACE_VFIR, NULL);
      break;
    case GST_DEINTERLACE2_LINEAR:
      self->method = g_object_new (GST_TYPE_DEINTERLACE_LINEAR, NULL);
      break;
    case GST_DEINTERLACE2_LINEAR_BLEND:
      self->method = g_object_new (GST_TYPE_DEINTERLACE_LINEAR_BLEND, NULL);
      break;
    case GST_DEINTERLACE2_SCALER_BOB:
      self->method = g_object_new (GST_TYPE_DEINTERLACE_SCALER_BOB, NULL);
      break;
    case GST_DEINTERLACE2_WEAVE:
      self->method = g_object_new (GST_TYPE_DEINTERLACE_WEAVE, NULL);
      break;
    case GST_DEINTERLACE2_WEAVE_TFF:
      self->method = g_object_new (GST_TYPE_DEINTERLACE_WEAVE_TFF, NULL);
      break;
    case GST_DEINTERLACE2_WEAVE_BFF:
      self->method = g_object_new (GST_TYPE_DEINTERLACE_WEAVE_BFF, NULL);
      break;
    default:
      GST_WARNING_OBJECT (self, "Invalid Deinterlacer Method");
      return;
  }

  self->method_id = method;

  gst_object_set_name (GST_OBJECT (self->method), "method");
  gst_object_set_parent (GST_OBJECT (self->method), GST_OBJECT (self));
  gst_child_proxy_child_added (GST_OBJECT (self), GST_OBJECT (self->method));
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

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode",
          "Mode",
          "Deinterlace Mode",
          GST_TYPE_DEINTERLACE2_MODES,
          DEFAULT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method",
          "Method",
          "Deinterlace Method",
          GST_TYPE_DEINTERLACE2_METHODS,
          DEFAULT_METHOD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  g_object_class_install_property (gobject_class, PROP_FIELDS,
      g_param_spec_enum ("fields",
          "fields",
          "Fields to use for deinterlacing",
          GST_TYPE_DEINTERLACE2_FIELDS,
          DEFAULT_FIELDS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  g_object_class_install_property (gobject_class, PROP_FIELD_LAYOUT,
      g_param_spec_enum ("tff",
          "tff",
          "Deinterlace top field first",
          GST_TYPE_DEINTERLACE2_FIELD_LAYOUT,
          DEFAULT_FIELD_LAYOUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
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
gst_deinterlace2_init (GstDeinterlace2 * self, GstDeinterlace2Class * klass)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_sink_event));
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_setcaps));
  gst_pad_set_getcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_getcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_src_event));
  gst_pad_set_query_type_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_src_query_types));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_src_query));
  gst_pad_set_setcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_setcaps));
  gst_pad_set_getcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace2_getcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  gst_element_no_more_pads (GST_ELEMENT (self));

  self->mode = DEFAULT_MODE;
  gst_deinterlace2_set_method (self, DEFAULT_METHOD);
  self->fields = DEFAULT_FIELDS;
  self->field_layout = DEFAULT_FIELD_LAYOUT;

  gst_deinterlace2_reset (self);
}

static void
gst_deinterlace2_reset_history (GstDeinterlace2 * self)
{
  gint i;

  for (i = 0; i < self->history_count; i++) {
    if (self->field_history[i].buf) {
      gst_buffer_unref (self->field_history[i].buf);
      self->field_history[i].buf = NULL;
    }
  }
  memset (self->field_history, 0, MAX_FIELD_HISTORY * sizeof (GstPicture));
  self->history_count = 0;
}

static void
gst_deinterlace2_reset (GstDeinterlace2 * self)
{
  self->row_stride = 0;
  self->frame_width = 0;
  self->frame_height = 0;
  self->frame_rate_n = 0;
  self->frame_rate_d = 0;
  self->field_height = 0;
  self->field_stride = 0;

  gst_deinterlace2_reset_history (self);
}

static void
gst_deinterlace2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDeinterlace2 *self;

  g_return_if_fail (GST_IS_DEINTERLACE2 (object));
  self = GST_DEINTERLACE2 (object);

  switch (prop_id) {
    case PROP_MODE:
      if (GST_STATE (self) >= GST_STATE_PAUSED) {
        g_warning ("Setting the 'mode' property is only allowed in "
            "states other than PAUSED and PLAYING");
      } else {
        self->mode = g_value_get_enum (value);
      }
      break;
    case PROP_METHOD:
      gst_deinterlace2_set_method (self, g_value_get_enum (value));
      break;
    case PROP_FIELDS:{
      gint oldfields;

      GST_OBJECT_LOCK (self);
      oldfields = self->fields;
      self->fields = g_value_get_enum (value);
      if (self->fields != oldfields && GST_PAD_CAPS (self->srcpad))
        gst_deinterlace2_setcaps (self->sinkpad, GST_PAD_CAPS (self->sinkpad));
      GST_OBJECT_UNLOCK (self);
      break;
    }
    case PROP_FIELD_LAYOUT:
      self->field_layout = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
  }

}

static void
gst_deinterlace2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDeinterlace2 *self;

  g_return_if_fail (GST_IS_DEINTERLACE2 (object));
  self = GST_DEINTERLACE2 (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;
    case PROP_METHOD:
      g_value_set_enum (value, self->method_id);
      break;
    case PROP_FIELDS:
      g_value_set_enum (value, self->fields);
      break;
    case PROP_FIELD_LAYOUT:
      g_value_set_enum (value, self->field_layout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
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
gst_deinterlace2_pop_history (GstDeinterlace2 * self)
{
  GstBuffer *buffer = NULL;

  g_assert (self->history_count > 0);

  buffer = self->field_history[self->history_count - 1].buf;

  self->history_count--;
  GST_DEBUG_OBJECT (self, "pop, size(history): %d", self->history_count);

  return buffer;
}

#if 0
static GstBuffer *
gst_deinterlace2_head_history (GstDeinterlace2 * self)
{
  return self->field_history[self->history_count - 1].buf;
}
#endif


/* invariant: field with smallest timestamp is self->field_history[self->history_count-1]

*/

static void
gst_deinterlace2_push_history (GstDeinterlace2 * self, GstBuffer * buffer)
{
  int i = 1;
  GstClockTime timestamp;
  GstDeinterlace2FieldLayout field_layout = self->field_layout;
  gboolean repeated = GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_RFF);
  gboolean tff = GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_TFF);
  gboolean onefield =
      GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_ONEFIELD);
  GstBuffer *field1, *field2;
  guint fields_to_push = (onefield) ? 1 : (!repeated) ? 2 : 3;
  gint field1_flags, field2_flags;

  g_assert (self->history_count < MAX_FIELD_HISTORY - fields_to_push);

  for (i = MAX_FIELD_HISTORY - 1; i >= fields_to_push; i--) {
    self->field_history[i].buf = self->field_history[i - fields_to_push].buf;
    self->field_history[i].flags =
        self->field_history[i - fields_to_push].flags;
  }

  if (field_layout == GST_DEINTERLACE2_LAYOUT_AUTO) {
    if (!self->interlaced) {
      GST_WARNING_OBJECT (self, "Can't detect field layout -- assuming TFF");
      field_layout = GST_DEINTERLACE2_LAYOUT_TFF;
    } else if (tff) {
      field_layout = GST_DEINTERLACE2_LAYOUT_TFF;
    } else {
      field_layout = GST_DEINTERLACE2_LAYOUT_BFF;
    }
  }

  if (field_layout == GST_DEINTERLACE2_LAYOUT_TFF) {
    GST_DEBUG_OBJECT (self, "Top field first");
    field1 = gst_buffer_ref (buffer);
    field1_flags = PICTURE_INTERLACED_TOP;
    field2 = gst_buffer_create_sub (buffer, self->row_stride,
        GST_BUFFER_SIZE (buffer) - self->row_stride);
    field2_flags = PICTURE_INTERLACED_BOTTOM;
  } else {
    GST_DEBUG_OBJECT (self, "Bottom field first");
    field1 = gst_buffer_create_sub (buffer, self->row_stride,
        GST_BUFFER_SIZE (buffer) - self->row_stride);
    field1_flags = PICTURE_INTERLACED_BOTTOM;
    field2 = gst_buffer_ref (buffer);
    field2_flags = PICTURE_INTERLACED_TOP;
  }

  /* Timestamps are assigned to the field buffers under the assumption that
     the timestamp of the buffer equals the first fields timestamp */

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_TIMESTAMP (field1) = timestamp;
  GST_BUFFER_TIMESTAMP (field2) = timestamp + self->field_duration;
  if (repeated)
    GST_BUFFER_TIMESTAMP (field2) += self->field_duration;

  if (repeated) {
    self->field_history[0].buf = field2;
    self->field_history[0].flags = field2_flags;
    self->field_history[1].buf = gst_buffer_ref (field1);
    GST_BUFFER_TIMESTAMP (self->field_history[1].buf) += self->field_duration;
    self->field_history[1].flags = field1_flags;
    self->field_history[2].buf = field1;
    self->field_history[2].flags = field1_flags;
  } else if (!onefield) {
    self->field_history[0].buf = field2;
    self->field_history[0].flags = field2_flags;
    self->field_history[1].buf = field1;
    self->field_history[1].flags = field1_flags;
  } else {                      /* onefield */
    self->field_history[0].buf = field1;
    self->field_history[0].flags = field1_flags;
    gst_buffer_unref (field2);
  }

  self->history_count += fields_to_push;
  GST_DEBUG_OBJECT (self, "push, size(history): %d", self->history_count);

  gst_buffer_unref (buffer);
}

static GstFlowReturn
gst_deinterlace2_chain (GstPad * pad, GstBuffer * buf)
{
  GstDeinterlace2 *self = NULL;
  GstClockTime timestamp;
  GstFlowReturn ret = GST_FLOW_OK;
  gint fields_required = 0;
  gint cur_field_idx = 0;
  GstBuffer *outbuf;

  self = GST_DEINTERLACE2 (GST_PAD_PARENT (pad));

  if (!self->interlaced && self->mode != GST_DEINTERLACE2_MODE_INTERLACED)
    return gst_pad_push (self->srcpad, buf);

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    GST_DEBUG_OBJECT (self, "DISCONT buffer, resetting history");
    gst_deinterlace2_reset_history (self);
  }

  gst_deinterlace2_push_history (self, buf);
  buf = NULL;

  fields_required = gst_deinterlace_method_get_fields_required (self->method);

  /* Not enough fields in the history */
  if (self->history_count < fields_required + 1) {
    /* TODO: do bob or just forward frame */
    GST_DEBUG_OBJECT (self, "HistoryCount=%d", self->history_count);
    return GST_FLOW_OK;
  }

  while (self->history_count >= fields_required) {
    if (self->fields == GST_DEINTERLACE2_ALL)
      GST_DEBUG_OBJECT (self, "All fields");
    if (self->fields == GST_DEINTERLACE2_TF)
      GST_DEBUG_OBJECT (self, "Top fields");
    if (self->fields == GST_DEINTERLACE2_BF)
      GST_DEBUG_OBJECT (self, "Bottom fields");

    cur_field_idx = self->history_count - fields_required;

    if ((self->field_history[cur_field_idx].flags == PICTURE_INTERLACED_TOP
            && self->fields == GST_DEINTERLACE2_TF) ||
        self->fields == GST_DEINTERLACE2_ALL) {
      GST_DEBUG_OBJECT (self, "deinterlacing top field");

      /* create new buffer */
      ret = gst_pad_alloc_buffer_and_set_caps (self->srcpad,
          GST_BUFFER_OFFSET_NONE, self->frame_size,
          GST_PAD_CAPS (self->srcpad), &outbuf);
      if (ret != GST_FLOW_OK)
        return ret;

      /* do magic calculus */
      gst_deinterlace_method_deinterlace_frame (self->method, self, outbuf);

      g_assert (self->history_count - 1 -
          gst_deinterlace_method_get_latency (self->method) >= 0);
      buf =
          self->field_history[self->history_count - 1 -
          gst_deinterlace_method_get_latency (self->method)].buf;
      timestamp = GST_BUFFER_TIMESTAMP (buf);

      gst_buffer_unref (gst_deinterlace2_pop_history (self));

      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      if (self->fields == GST_DEINTERLACE2_ALL)
        GST_BUFFER_DURATION (outbuf) = self->field_duration;
      else
        GST_BUFFER_DURATION (outbuf) = 2 * self->field_duration;

      ret = gst_pad_push (self->srcpad, outbuf);
      outbuf = NULL;
      if (ret != GST_FLOW_OK)
        return ret;
    }
    /* no calculation done: remove excess field */
    else if (self->field_history[cur_field_idx].flags ==
        PICTURE_INTERLACED_TOP && self->fields == GST_DEINTERLACE2_BF) {
      GST_DEBUG_OBJECT (self, "Removing unused top field");
      gst_buffer_unref (gst_deinterlace2_pop_history (self));
    }

    cur_field_idx = self->history_count - fields_required;
    if (self->history_count < fields_required)
      break;

    /* deinterlace bottom_field */
    if ((self->field_history[cur_field_idx].flags == PICTURE_INTERLACED_BOTTOM
            && self->fields == GST_DEINTERLACE2_BF) ||
        self->fields == GST_DEINTERLACE2_ALL) {
      GST_DEBUG_OBJECT (self, "deinterlacing bottom field");

      /* create new buffer */
      ret = gst_pad_alloc_buffer_and_set_caps (self->srcpad,
          GST_BUFFER_OFFSET_NONE, self->frame_size,
          GST_PAD_CAPS (self->srcpad), &outbuf);
      if (ret != GST_FLOW_OK)
        return ret;

      /* do magic calculus */
      gst_deinterlace_method_deinterlace_frame (self->method, self, outbuf);

      g_assert (self->history_count - 1 -
          gst_deinterlace_method_get_latency (self->method) >= 0);
      buf =
          self->field_history[self->history_count - 1 -
          gst_deinterlace_method_get_latency (self->method)].buf;
      timestamp = GST_BUFFER_TIMESTAMP (buf);

      gst_buffer_unref (gst_deinterlace2_pop_history (self));

      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      if (self->fields == GST_DEINTERLACE2_ALL)
        GST_BUFFER_DURATION (outbuf) = self->field_duration;
      else
        GST_BUFFER_DURATION (outbuf) = 2 * self->field_duration;

      ret = gst_pad_push (self->srcpad, outbuf);
      outbuf = NULL;

      if (ret != GST_FLOW_OK)
        return ret;
    }
    /* no calculation done: remove excess field */
    else if (self->field_history[cur_field_idx].flags ==
        PICTURE_INTERLACED_BOTTOM && self->fields == GST_DEINTERLACE2_TF) {
      GST_DEBUG_OBJECT (self, "Removing unused bottom field");
      gst_buffer_unref (gst_deinterlace2_pop_history (self));
    }
  }

  GST_DEBUG_OBJECT (self, "----chain end ----\n\n");

  return ret;
}

static gint
gst_greatest_common_divisor (gint a, gint b)
{
  while (b != 0) {
    int temp = a;

    a = b;
    b = temp % b;
  }

  return ABS (a);
}

static gboolean
gst_fraction_double (gint * n_out, gint * d_out, gboolean half)
{
  gint n, d, gcd;

  n = *n_out;
  d = *d_out;

  if (d == 0)
    return FALSE;

  if (n == 0 || (n == G_MAXINT && d == 1))
    return TRUE;

  gcd = gst_greatest_common_divisor (n, d);
  n /= gcd;
  d /= gcd;

  if (!half) {
    if (G_MAXINT / 2 >= ABS (n)) {
      n *= 2;
    } else if (d >= 2) {
      d /= 2;
    } else {
      return FALSE;
    }
  } else {
    if (G_MAXINT / 2 >= ABS (d)) {
      d *= 2;
    } else if (n >= 2) {
      n /= 2;
    } else {
      return FALSE;
    }
  }

  *n_out = n;
  *d_out = d;

  return TRUE;
}

static GstCaps *
gst_deinterlace2_getcaps (GstPad * pad)
{
  GstCaps *ret;
  GstDeinterlace2 *self = GST_DEINTERLACE2 (gst_pad_get_parent (pad));
  GstPad *otherpad;
  gint len;
  const GstCaps *ourcaps;
  GstCaps *peercaps;

  GST_OBJECT_LOCK (self);

  otherpad = (pad == self->srcpad) ? self->sinkpad : self->srcpad;

  ourcaps = gst_pad_get_pad_template_caps (pad);
  peercaps = gst_pad_peer_get_caps (otherpad);

  if (peercaps) {
    ret = gst_caps_intersect (ourcaps, peercaps);
    gst_caps_unref (peercaps);
  } else {
    ret = gst_caps_copy (ourcaps);
  }

  GST_OBJECT_UNLOCK (self);

  if ((self->interlaced || self->mode == GST_DEINTERLACE2_MODE_INTERLACED) &&
      self->fields == GST_DEINTERLACE2_ALL) {
    for (len = gst_caps_get_size (ret); len > 0; len--) {
      GstStructure *s = gst_caps_get_structure (ret, len - 1);
      const GValue *val;

      val = gst_structure_get_value (s, "framerate");
      if (!val)
        continue;

      if (G_VALUE_TYPE (val) == GST_TYPE_FRACTION) {
        gint n, d;

        n = gst_value_get_fraction_numerator (val);
        d = gst_value_get_fraction_denominator (val);

        if (!gst_fraction_double (&n, &d, pad != self->srcpad)) {
          goto error;
        }

        gst_structure_set (s, "framerate", GST_TYPE_FRACTION, n, d, NULL);
      } else if (G_VALUE_TYPE (val) == GST_TYPE_FRACTION_RANGE) {
        const GValue *min, *max;
        GValue nrange = { 0, }, nmin = {
        0,}, nmax = {
        0,};
        gint n, d;

        g_value_init (&nrange, GST_TYPE_FRACTION_RANGE);
        g_value_init (&nmin, GST_TYPE_FRACTION);
        g_value_init (&nmax, GST_TYPE_FRACTION);

        min = gst_value_get_fraction_range_min (val);
        max = gst_value_get_fraction_range_max (val);

        n = gst_value_get_fraction_numerator (min);
        d = gst_value_get_fraction_denominator (min);

        if (!gst_fraction_double (&n, &d, pad != self->srcpad)) {
          g_value_unset (&nrange);
          g_value_unset (&nmax);
          g_value_unset (&nmin);
          goto error;
        }

        gst_value_set_fraction (&nmin, n, d);

        n = gst_value_get_fraction_numerator (max);
        d = gst_value_get_fraction_denominator (max);

        if (!gst_fraction_double (&n, &d, pad != self->srcpad)) {
          g_value_unset (&nrange);
          g_value_unset (&nmax);
          g_value_unset (&nmin);
          goto error;
        }

        gst_value_set_fraction (&nmax, n, d);
        gst_value_set_fraction_range (&nrange, &nmin, &nmax);

        gst_structure_set_value (s, "framerate", &nrange);

        g_value_unset (&nmin);
        g_value_unset (&nmax);
        g_value_unset (&nrange);
      } else if (G_VALUE_TYPE (val) == GST_TYPE_LIST) {
        const GValue *lval;
        GValue nlist = { 0, };
        GValue nval = { 0, };
        gint i;

        g_value_init (&nlist, GST_TYPE_LIST);
        for (i = gst_value_list_get_size (val); i > 0; i--) {
          gint n, d;

          lval = gst_value_list_get_value (val, i);

          if (G_VALUE_TYPE (lval) != GST_TYPE_FRACTION)
            continue;

          n = gst_value_get_fraction_numerator (lval);
          d = gst_value_get_fraction_denominator (lval);

          /* Double/Half the framerate but if this fails simply
           * skip this value from the list */
          if (!gst_fraction_double (&n, &d, pad != self->srcpad)) {
            continue;
          }

          g_value_init (&nval, GST_TYPE_FRACTION);

          gst_value_set_fraction (&nval, n, d);
          gst_value_list_append_value (&nlist, &nval);
          g_value_unset (&nval);
        }
        gst_structure_set_value (s, "framerate", &nlist);
        g_value_unset (&nlist);
      }
    }
  }

  GST_DEBUG_OBJECT (pad, "Returning caps %" GST_PTR_FORMAT, ret);

  return ret;

error:
  GST_ERROR_OBJECT (pad, "Unable to transform peer caps");
  gst_caps_unref (ret);
  return NULL;
}

static gboolean
gst_deinterlace2_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean res = TRUE;
  GstDeinterlace2 *self = GST_DEINTERLACE2 (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstStructure *structure;
  GstVideoFormat fmt;
  guint32 fourcc;
  GstCaps *othercaps;

  otherpad = (pad == self->srcpad) ? self->sinkpad : self->srcpad;

  structure = gst_caps_get_structure (caps, 0);

  res = gst_structure_get_int (structure, "width", &self->frame_width);
  res &= gst_structure_get_int (structure, "height", &self->frame_height);
  res &=
      gst_structure_get_fraction (structure, "framerate", &self->frame_rate_n,
      &self->frame_rate_d);
  res &= gst_structure_get_fourcc (structure, "format", &fourcc);
  res &= gst_video_format_parse_caps_interlaced (caps, &self->interlaced);
  if (!res)
    goto invalid_caps;

  if ((self->interlaced || self->mode == GST_DEINTERLACE2_MODE_INTERLACED) &&
      self->fields == GST_DEINTERLACE2_ALL) {
    gint fps_n = self->frame_rate_n, fps_d = self->frame_rate_d;

    if (!gst_fraction_double (&fps_n, &fps_d, otherpad != self->srcpad))
      goto invalid_caps;

    othercaps = gst_caps_copy (caps);

    gst_caps_set_simple (othercaps, "framerate", GST_TYPE_FRACTION, fps_n,
        fps_d, NULL);
  } else {
    othercaps = gst_caps_ref (caps);
  }

  if (!gst_pad_set_caps (otherpad, othercaps))
    goto caps_not_accepted;
  gst_caps_unref (othercaps);

  self->field_height = self->frame_height / 2;

  fmt = gst_video_format_from_fourcc (fourcc);

  /* TODO: only true if fields are subbuffers of interlaced frames,
     change when the buffer-fields concept has landed */
  self->field_stride =
      gst_video_format_get_row_stride (fmt, 0, self->frame_width) * 2;

  /* in bytes */
  self->row_stride =
      gst_video_format_get_row_stride (fmt, 0, self->frame_width);
  self->frame_size =
      gst_video_format_get_size (fmt, self->frame_width, self->frame_height);

  if (self->fields == GST_DEINTERLACE2_ALL && otherpad == self->srcpad)
    self->field_duration =
        gst_util_uint64_scale (GST_SECOND, self->frame_rate_d,
        self->frame_rate_n);
  else
    self->field_duration =
        gst_util_uint64_scale (GST_SECOND, self->frame_rate_d,
        2 * self->frame_rate_n);

  GST_DEBUG_OBJECT (self, "Set caps: %" GST_PTR_FORMAT, caps);

done:

  gst_object_unref (self);
  return res;

invalid_caps:
  res = FALSE;
  GST_ERROR_OBJECT (pad, "Invalid caps: %" GST_PTR_FORMAT, caps);
  goto done;

caps_not_accepted:
  res = FALSE;
  GST_ERROR_OBJECT (pad, "Caps not accepted: %" GST_PTR_FORMAT, othercaps);
  gst_caps_unref (othercaps);
  goto done;
}

static gboolean
gst_deinterlace2_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstDeinterlace2 *self = GST_DEINTERLACE2 (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_EOS:
    case GST_EVENT_NEWSEGMENT:
      gst_deinterlace2_reset_history (self);

      /* fall through */
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (self);
  return res;
}

static GstStateChangeReturn
gst_deinterlace2_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDeinterlace2 *self = GST_DEINTERLACE2 (element);

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
      gst_deinterlace2_reset (self);
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
  GstDeinterlace2 *self = GST_DEINTERLACE2 (gst_pad_get_parent (pad));
  gboolean res;

  GST_DEBUG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (self);

  return res;
}

static gboolean
gst_deinterlace2_src_query (GstPad * pad, GstQuery * query)
{
  GstDeinterlace2 *self = GST_DEINTERLACE2 (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  GST_LOG_OBJECT (self, "%s query", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
      if (self->interlaced || self->mode == GST_DEINTERLACE2_MODE_INTERLACED) {
        GstClockTime min, max;
        gboolean live;
        GstPad *peer;

        if ((peer = gst_pad_get_peer (self->sinkpad))) {
          if ((res = gst_pad_query (peer, query))) {
            GstClockTime latency;
            gint fields_required = 0;
            gint method_latency = 0;

            if (self->method) {
              fields_required =
                  gst_deinterlace_method_get_fields_required (self->method);
              method_latency =
                  gst_deinterlace_method_get_latency (self->method);
            }

            gst_query_parse_latency (query, &live, &min, &max);

            GST_DEBUG_OBJECT (self, "Peer latency: min %"
                GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
                GST_TIME_ARGS (min), GST_TIME_ARGS (max));

            /* add our own latency */
            latency = (fields_required + method_latency) * self->field_duration;

            GST_DEBUG_OBJECT (self, "Our latency: min %" GST_TIME_FORMAT
                ", max %" GST_TIME_FORMAT,
                GST_TIME_ARGS (latency), GST_TIME_ARGS (latency));

            min += latency;
            if (max != GST_CLOCK_TIME_NONE)
              max += latency;
            else
              max = latency;

            GST_DEBUG_OBJECT (self, "Calculated total latency : min %"
                GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
                GST_TIME_ARGS (min), GST_TIME_ARGS (max));

            gst_query_set_latency (query, live, min, max);
          }
          gst_object_unref (peer);
        } else {
          res = gst_pad_query_default (pad, query);
        }
        break;
      }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (self);
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
