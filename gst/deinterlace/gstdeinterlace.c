/*
 * GStreamer
 * Copyright (C) 2005 Martin Eikermann <meiker@upb.de>
 * Copyright (C) 2008-2010 Sebastian Dröge <slomo@collabora.co.uk>
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
 * SECTION:element-deinterlace
 *
 * deinterlace deinterlaces interlaced video frames to progressive video frames.
 * For this different algorithms can be selected which will be described later.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=/path/to/file ! decodebin2 ! ffmpegcolorspace ! deinterlace ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline deinterlaces a video file with the default deinterlacing options.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdeinterlace.h"
#include "tvtime/plugins.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (deinterlace_debug);
#define GST_CAT_DEFAULT (deinterlace_debug)

/* Properties */

#define DEFAULT_MODE            GST_DEINTERLACE_MODE_INTERLACED
#define DEFAULT_METHOD          GST_DEINTERLACE_GREEDY_H
#define DEFAULT_FIELDS          GST_DEINTERLACE_ALL
#define DEFAULT_FIELD_LAYOUT    GST_DEINTERLACE_LAYOUT_AUTO

enum
{
  PROP_0,
  PROP_MODE,
  PROP_METHOD,
  PROP_FIELDS,
  PROP_FIELD_LAYOUT,
  PROP_LAST
};

#define GST_TYPE_DEINTERLACE_METHODS (gst_deinterlace_methods_get_type ())
static GType
gst_deinterlace_methods_get_type (void)
{
  static GType deinterlace_methods_type = 0;

  static const GEnumValue methods_types[] = {
    {GST_DEINTERLACE_TOMSMOCOMP, "Motion Adaptive: Motion Search",
        "tomsmocomp"},
    {GST_DEINTERLACE_GREEDY_H, "Motion Adaptive: Advanced Detection",
        "greedyh"},
    {GST_DEINTERLACE_GREEDY_L, "Motion Adaptive: Simple Detection", "greedyl"},
    {GST_DEINTERLACE_VFIR, "Blur Vertical", "vfir"},
    {GST_DEINTERLACE_LINEAR, "Television: Full resolution", "linear"},
    {GST_DEINTERLACE_LINEAR_BLEND, "Blur: Temporal", "linearblend"},
    {GST_DEINTERLACE_SCALER_BOB, "Double lines", "scalerbob"},
    {GST_DEINTERLACE_WEAVE, "Weave", "weave"},
    {GST_DEINTERLACE_WEAVE_TFF, "Progressive: Top Field First", "weavetff"},
    {GST_DEINTERLACE_WEAVE_BFF, "Progressive: Bottom Field First", "weavebff"},
    {0, NULL, NULL},
  };

  if (!deinterlace_methods_type) {
    deinterlace_methods_type =
        g_enum_register_static ("GstDeinterlaceMethods", methods_types);
  }
  return deinterlace_methods_type;
}

#define GST_TYPE_DEINTERLACE_FIELDS (gst_deinterlace_fields_get_type ())
static GType
gst_deinterlace_fields_get_type (void)
{
  static GType deinterlace_fields_type = 0;

  static const GEnumValue fields_types[] = {
    {GST_DEINTERLACE_ALL, "All fields", "all"},
    {GST_DEINTERLACE_TF, "Top fields only", "top"},
    {GST_DEINTERLACE_BF, "Bottom fields only", "bottom"},
    {0, NULL, NULL},
  };

  if (!deinterlace_fields_type) {
    deinterlace_fields_type =
        g_enum_register_static ("GstDeinterlaceFields", fields_types);
  }
  return deinterlace_fields_type;
}

#define GST_TYPE_DEINTERLACE_FIELD_LAYOUT (gst_deinterlace_field_layout_get_type ())
static GType
gst_deinterlace_field_layout_get_type (void)
{
  static GType deinterlace_field_layout_type = 0;

  static const GEnumValue field_layout_types[] = {
    {GST_DEINTERLACE_LAYOUT_AUTO, "Auto detection", "auto"},
    {GST_DEINTERLACE_LAYOUT_TFF, "Top field first", "tff"},
    {GST_DEINTERLACE_LAYOUT_BFF, "Bottom field first", "bff"},
    {0, NULL, NULL},
  };

  if (!deinterlace_field_layout_type) {
    deinterlace_field_layout_type =
        g_enum_register_static ("GstDeinterlaceFieldLayout",
        field_layout_types);
  }
  return deinterlace_field_layout_type;
}

#define GST_TYPE_DEINTERLACE_MODES (gst_deinterlace_modes_get_type ())
static GType
gst_deinterlace_modes_get_type (void)
{
  static GType deinterlace_modes_type = 0;

  static const GEnumValue modes_types[] = {
    {GST_DEINTERLACE_MODE_AUTO, "Auto detection", "auto"},
    {GST_DEINTERLACE_MODE_INTERLACED, "Force deinterlacing", "interlaced"},
    {GST_DEINTERLACE_MODE_DISABLED, "Run in passthrough mode", "disabled"},
    {0, NULL, NULL},
  };

  if (!deinterlace_modes_type) {
    deinterlace_modes_type =
        g_enum_register_static ("GstDeinterlaceModes", modes_types);
  }
  return deinterlace_modes_type;
}

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2") ";"
        GST_VIDEO_CAPS_YUV ("YVYU") ";" GST_VIDEO_CAPS_YUV ("I420") ";"
        GST_VIDEO_CAPS_YUV ("YV12") ";" GST_VIDEO_CAPS_YUV ("Y444") ";"
        GST_VIDEO_CAPS_YUV ("Y42B") ";" GST_VIDEO_CAPS_YUV ("Y41B"))
    );

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2") ";"
        GST_VIDEO_CAPS_YUV ("YVYU") ";" GST_VIDEO_CAPS_YUV ("I420") ";"
        GST_VIDEO_CAPS_YUV ("YV12") ";" GST_VIDEO_CAPS_YUV ("Y444") ";"
        GST_VIDEO_CAPS_YUV ("Y42B") ";" GST_VIDEO_CAPS_YUV ("Y41B"))
    );

static void gst_deinterlace_finalize (GObject * self);
static void gst_deinterlace_set_property (GObject * self, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_deinterlace_get_property (GObject * self, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_deinterlace_getcaps (GstPad * pad);
static gboolean gst_deinterlace_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_deinterlace_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_deinterlace_sink_query (GstPad * pad, GstQuery * query);
static GstFlowReturn gst_deinterlace_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_deinterlace_alloc_buffer (GstPad * pad, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);
static GstStateChangeReturn gst_deinterlace_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_deinterlace_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_deinterlace_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_deinterlace_src_query_types (GstPad * pad);

static void gst_deinterlace_reset (GstDeinterlace * self);
static void gst_deinterlace_update_qos (GstDeinterlace * self,
    gdouble proportion, GstClockTimeDiff diff, GstClockTime time);
static void gst_deinterlace_reset_qos (GstDeinterlace * self);
static void gst_deinterlace_read_qos (GstDeinterlace * self,
    gdouble * proportion, GstClockTime * time);

static void gst_deinterlace_child_proxy_interface_init (gpointer g_iface,
    gpointer iface_data);

static void
_do_init (GType object_type)
{
  const GInterfaceInfo child_proxy_interface_info = {
    (GInterfaceInitFunc) gst_deinterlace_child_proxy_interface_init,
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_CHILD_PROXY,
      &child_proxy_interface_info);
}

GST_BOILERPLATE_FULL (GstDeinterlace, gst_deinterlace, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static const struct
{
  GType (*get_type) (void);
} _method_types[] = {
  {
  gst_deinterlace_method_tomsmocomp_get_type}, {
  gst_deinterlace_method_greedy_h_get_type}, {
  gst_deinterlace_method_greedy_l_get_type}, {
  gst_deinterlace_method_vfir_get_type}, {
  gst_deinterlace_method_linear_get_type}, {
  gst_deinterlace_method_linear_blend_get_type}, {
  gst_deinterlace_method_scaler_bob_get_type}, {
  gst_deinterlace_method_weave_get_type}, {
  gst_deinterlace_method_weave_tff_get_type}, {
  gst_deinterlace_method_weave_bff_get_type}
};

static void
gst_deinterlace_set_method (GstDeinterlace * self, GstDeinterlaceMethods method)
{
  GType method_type;

  GST_DEBUG_OBJECT (self, "Setting new method %d", method);

  if (self->method) {
    if (self->method_id == method &&
        gst_deinterlace_method_supported (G_TYPE_FROM_INSTANCE (self->method),
            self->format, self->width, self->height)) {
      GST_DEBUG_OBJECT (self, "Reusing current method");
      return;
    }

    gst_child_proxy_child_removed (GST_OBJECT (self),
        GST_OBJECT (self->method));
    gst_object_unparent (GST_OBJECT (self->method));
    self->method = NULL;
  }

  method_type =
      _method_types[method].get_type !=
      NULL ? _method_types[method].get_type () : G_TYPE_INVALID;
  if (method_type == G_TYPE_INVALID
      || !gst_deinterlace_method_supported (method_type, self->format,
          self->width, self->height)) {
    GType tmp;
    gint i;

    method_type = G_TYPE_INVALID;

    GST_WARNING_OBJECT (self, "Method doesn't support requested format");
    for (i = 0; i < G_N_ELEMENTS (_method_types); i++) {
      if (_method_types[i].get_type == NULL)
        continue;
      tmp = _method_types[i].get_type ();
      if (gst_deinterlace_method_supported (tmp, self->format, self->width,
              self->height)) {
        GST_DEBUG_OBJECT (self, "Using method %d", i);
        method_type = tmp;
        break;
      }
    }
    /* If we get here we must have invalid caps! */
    g_assert (method_type != G_TYPE_INVALID);
  }

  self->method = g_object_new (method_type, NULL);
  self->method_id = method;

  gst_object_set_name (GST_OBJECT (self->method), "method");
  gst_object_set_parent (GST_OBJECT (self->method), GST_OBJECT (self));
  gst_child_proxy_child_added (GST_OBJECT (self), GST_OBJECT (self->method));

  if (self->method)
    gst_deinterlace_method_setup (self->method, self->format, self->width,
        self->height);
}

static gboolean
gst_deinterlace_clip_buffer (GstDeinterlace * self, GstBuffer * buffer)
{
  gboolean ret = TRUE;
  GstClockTime start, stop;
  gint64 cstart, cstop;

  GST_DEBUG_OBJECT (self,
      "Clipping buffer to the current segment: %" GST_TIME_FORMAT " -- %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));
  GST_DEBUG_OBJECT (self, "Current segment: %" GST_SEGMENT_FORMAT,
      &self->segment);

  if (G_UNLIKELY (self->segment.format != GST_FORMAT_TIME))
    goto beach;
  if (G_UNLIKELY (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer)))
    goto beach;

  start = GST_BUFFER_TIMESTAMP (buffer);
  stop = start + GST_BUFFER_DURATION (buffer);

  if (!(ret = gst_segment_clip (&self->segment, GST_FORMAT_TIME,
              start, stop, &cstart, &cstop)))
    goto beach;

  GST_BUFFER_TIMESTAMP (buffer) = cstart;
  if (GST_CLOCK_TIME_IS_VALID (cstop))
    GST_BUFFER_DURATION (buffer) = cstop - cstart;

beach:
  if (ret)
    GST_DEBUG_OBJECT (self,
        "Clipped buffer to the current segment: %" GST_TIME_FORMAT " -- %"
        GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));
  else
    GST_DEBUG_OBJECT (self, "Buffer outside the current segment -- dropping");

  return ret;
}

static void
gst_deinterlace_base_init (gpointer klass)
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
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_deinterlace_class_init (GstDeinterlaceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  GstElementClass *element_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_deinterlace_set_property;
  gobject_class->get_property = gst_deinterlace_get_property;
  gobject_class->finalize = gst_deinterlace_finalize;

  /**
   * GstDeinterlace:mode
   * 
   * This selects whether the deinterlacing methods should
   * always be applied or if they should only be applied
   * on content that has the "interlaced" flag on the caps.
   *
   */
  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode",
          "Mode",
          "Deinterlace Mode",
          GST_TYPE_DEINTERLACE_MODES,
          DEFAULT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  /**
   * GstDeinterlace:method
   * 
   * Selects the different deinterlacing algorithms that can be used.
   * These provide different quality and CPU usage.
   *
   * Some methods provide parameters which can be set by getting
   * the "method" child via the #GstChildProxy interface and
   * setting the appropiate properties on it.
   *
   * <itemizedlist>
   * <listitem>
   * <para>
   * tomsmocomp
   * Motion Adaptive: Motion Search
   * </para>
   * </listitem>
   * <listitem>
   * <para>
   * greedyh
   * Motion Adaptive: Advanced Detection
   * </para>
   * </listitem>
   * <listitem>
   * <para>
   * greedyl
   * Motion Adaptive: Simple Detection
   * </para>
   * </listitem>
   * <listitem>
   * <para>
   * vfir
   * Blur vertical
   * </para>
   * </listitem>
   * <listitem>
   * <para>
   * linear
   * Linear interpolation
   * </para>
   * </listitem>
   * <listitem>
   * <para>
   * linearblend
   * Linear interpolation in time domain
   * </para>
   * </listitem>
   * <listitem>
   * <para>
   * scalerbob
   * Double lines
   * </para>
   * </listitem>
   * <listitem>
   * <para>
   * weave
   * Weave
   * </para>
   * </listitem>
   * <listitem>
   * <para>
   * weavetff
   * Progressive: Top Field First
   * </para>
   * </listitem>
   * <listitem>
   * <para>
   * weavebff
   * Progressive: Bottom Field First
   * </para>
   * </listitem>
   * </itemizedlist>
   */
  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method",
          "Method",
          "Deinterlace Method",
          GST_TYPE_DEINTERLACE_METHODS,
          DEFAULT_METHOD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  /**
   * GstDeinterlace:fields
   *
   * This selects which fields should be output. If "all" is selected
   * the output framerate will be double.
   *
   */
  g_object_class_install_property (gobject_class, PROP_FIELDS,
      g_param_spec_enum ("fields",
          "fields",
          "Fields to use for deinterlacing",
          GST_TYPE_DEINTERLACE_FIELDS,
          DEFAULT_FIELDS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  /**
   * GstDeinterlace:layout
   *
   * This selects which fields is the first in time.
   *
   */
  g_object_class_install_property (gobject_class, PROP_FIELD_LAYOUT,
      g_param_spec_enum ("tff",
          "tff",
          "Deinterlace top field first",
          GST_TYPE_DEINTERLACE_FIELD_LAYOUT,
          DEFAULT_FIELD_LAYOUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_deinterlace_change_state);
}

static GstObject *
gst_deinterlace_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GstDeinterlace *self = GST_DEINTERLACE (child_proxy);

  g_return_val_if_fail (index == 0, NULL);

  return gst_object_ref (self->method);
}

static guint
gst_deinterlace_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  GstDeinterlace *self = GST_DEINTERLACE (child_proxy);

  return ((self->method) ? 1 : 0);
}

static void
gst_deinterlace_child_proxy_interface_init (gpointer g_iface,
    gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  iface->get_child_by_index = gst_deinterlace_child_proxy_get_child_by_index;
  iface->get_children_count = gst_deinterlace_child_proxy_get_children_count;
}

static void
gst_deinterlace_init (GstDeinterlace * self, GstDeinterlaceClass * klass)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_sink_event));
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_setcaps));
  gst_pad_set_getcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_getcaps));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_sink_query));
  gst_pad_set_bufferalloc_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_alloc_buffer));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_src_event));
  gst_pad_set_query_type_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_src_query_types));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_src_query));
  gst_pad_set_getcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_getcaps));
  gst_pad_set_setcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_setcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->mode = DEFAULT_MODE;
  gst_deinterlace_set_method (self, DEFAULT_METHOD);
  self->fields = DEFAULT_FIELDS;
  self->field_layout = DEFAULT_FIELD_LAYOUT;

  self->still_frame_mode = FALSE;

  gst_deinterlace_reset (self);
}

static void
gst_deinterlace_reset_history (GstDeinterlace * self)
{
  gint i;

  GST_DEBUG_OBJECT (self, "Resetting history");

  for (i = 0; i < self->history_count; i++) {
    if (self->field_history[i].buf) {
      gst_buffer_unref (self->field_history[i].buf);
      self->field_history[i].buf = NULL;
    }
  }
  memset (self->field_history, 0,
      GST_DEINTERLACE_MAX_FIELD_HISTORY * sizeof (GstDeinterlaceField));
  self->history_count = 0;

  if (self->last_buffer)
    gst_buffer_unref (self->last_buffer);
  self->last_buffer = NULL;
}

static void
gst_deinterlace_update_passthrough (GstDeinterlace * self)
{
  self->passthrough = (self->mode == GST_DEINTERLACE_MODE_DISABLED
      || (!self->interlaced && self->mode != GST_DEINTERLACE_MODE_INTERLACED));
  GST_DEBUG_OBJECT (self, "Passthrough: %d", self->passthrough);
}

static void
gst_deinterlace_reset (GstDeinterlace * self)
{
  GST_DEBUG_OBJECT (self, "Resetting internal state");

  self->format = GST_VIDEO_FORMAT_UNKNOWN;
  self->width = 0;
  self->height = 0;
  self->frame_size = 0;
  self->fps_n = self->fps_d = 0;
  self->passthrough = FALSE;

  gst_segment_init (&self->segment, GST_FORMAT_TIME);

  if (self->sink_caps)
    gst_caps_unref (self->sink_caps);
  self->sink_caps = NULL;

  if (self->src_caps)
    gst_caps_unref (self->src_caps);
  self->src_caps = NULL;

  if (self->request_caps)
    gst_caps_unref (self->request_caps);
  self->request_caps = NULL;

  gst_deinterlace_reset_history (self);

  gst_deinterlace_reset_qos (self);
}

static void
gst_deinterlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDeinterlace *self;

  g_return_if_fail (GST_IS_DEINTERLACE (object));
  self = GST_DEINTERLACE (object);

  switch (prop_id) {
    case PROP_MODE:{
      gint oldmode;

      GST_OBJECT_LOCK (self);
      oldmode = self->mode;
      self->mode = g_value_get_enum (value);
      gst_deinterlace_update_passthrough (self);
      if (self->mode != oldmode && GST_PAD_CAPS (self->srcpad))
        gst_deinterlace_setcaps (self->sinkpad, GST_PAD_CAPS (self->sinkpad));
      GST_OBJECT_UNLOCK (self);
      break;
    }
    case PROP_METHOD:
      gst_deinterlace_set_method (self, g_value_get_enum (value));
      break;
    case PROP_FIELDS:{
      gint oldfields;

      GST_OBJECT_LOCK (self);
      oldfields = self->fields;
      self->fields = g_value_get_enum (value);
      if (self->fields != oldfields && GST_PAD_CAPS (self->srcpad))
        gst_deinterlace_setcaps (self->sinkpad, GST_PAD_CAPS (self->sinkpad));
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
gst_deinterlace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDeinterlace *self;

  g_return_if_fail (GST_IS_DEINTERLACE (object));
  self = GST_DEINTERLACE (object);

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
gst_deinterlace_finalize (GObject * object)
{
  GstDeinterlace *self = GST_DEINTERLACE (object);

  gst_deinterlace_reset (self);

  if (self->method) {
    gst_object_unparent (GST_OBJECT (self->method));
    self->method = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstBuffer *
gst_deinterlace_pop_history (GstDeinterlace * self)
{
  GstBuffer *buffer;

  g_return_val_if_fail (self->history_count > 0, NULL);

  GST_DEBUG_OBJECT (self, "Pop last history buffer -- current history size %d",
      self->history_count);

  buffer = self->field_history[self->history_count - 1].buf;

  self->history_count--;

  GST_DEBUG_OBJECT (self, "Returning buffer: %" GST_TIME_FORMAT
      " with duration %" GST_TIME_FORMAT " and size %u",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)), GST_BUFFER_SIZE (buffer));

  return buffer;
}

static void
gst_deinterlace_push_history (GstDeinterlace * self, GstBuffer * buffer)
{
  int i = 1;
  GstClockTime timestamp;
  GstDeinterlaceFieldLayout field_layout = self->field_layout;
  gboolean repeated = GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_RFF);
  gboolean tff = GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_TFF);
  gboolean onefield =
      GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_ONEFIELD);
  GstBuffer *field1, *field2;
  guint fields_to_push = (onefield) ? 1 : (!repeated) ? 2 : 3;
  gint field1_flags, field2_flags;

  g_return_if_fail (self->history_count <
      GST_DEINTERLACE_MAX_FIELD_HISTORY - fields_to_push);

  GST_DEBUG_OBJECT (self, "Pushing new buffer to the history: %" GST_TIME_FORMAT
      " with duration %" GST_TIME_FORMAT " and size %u",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)), GST_BUFFER_SIZE (buffer));

  for (i = GST_DEINTERLACE_MAX_FIELD_HISTORY - 1; i >= fields_to_push; i--) {
    self->field_history[i].buf = self->field_history[i - fields_to_push].buf;
    self->field_history[i].flags =
        self->field_history[i - fields_to_push].flags;
  }

  if (field_layout == GST_DEINTERLACE_LAYOUT_AUTO) {
    if (!self->interlaced) {
      GST_WARNING_OBJECT (self, "Can't detect field layout -- assuming TFF");
      field_layout = GST_DEINTERLACE_LAYOUT_TFF;
    } else if (tff) {
      field_layout = GST_DEINTERLACE_LAYOUT_TFF;
    } else {
      field_layout = GST_DEINTERLACE_LAYOUT_BFF;
    }
  }

  if (field_layout == GST_DEINTERLACE_LAYOUT_TFF) {
    GST_DEBUG_OBJECT (self, "Top field first");
    field1 = gst_buffer_ref (buffer);
    field1_flags = PICTURE_INTERLACED_TOP;
    field2 = gst_buffer_ref (buffer);
    field2_flags = PICTURE_INTERLACED_BOTTOM;
  } else {
    GST_DEBUG_OBJECT (self, "Bottom field first");
    field1 = gst_buffer_ref (buffer);
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

  GST_DEBUG_OBJECT (self, "Pushed buffer -- current history size %d",
      self->history_count);

  if (self->last_buffer)
    gst_buffer_unref (self->last_buffer);
  self->last_buffer = buffer;
}

static void
gst_deinterlace_update_qos (GstDeinterlace * self, gdouble proportion,
    GstClockTimeDiff diff, GstClockTime timestamp)
{
  GST_DEBUG_OBJECT (self,
      "Updating QoS: proportion %lf, diff %s%" GST_TIME_FORMAT ", timestamp %"
      GST_TIME_FORMAT, proportion, (diff < 0) ? "-" : "",
      GST_TIME_ARGS (ABS (diff)), GST_TIME_ARGS (timestamp));

  GST_OBJECT_LOCK (self);
  self->proportion = proportion;
  if (G_LIKELY (timestamp != GST_CLOCK_TIME_NONE)) {
    if (G_UNLIKELY (diff > 0))
      self->earliest_time =
          timestamp + 2 * diff + ((self->fields ==
              GST_DEINTERLACE_ALL) ? self->field_duration : 2 *
          self->field_duration);
    else
      self->earliest_time = timestamp + diff;
  } else {
    self->earliest_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_deinterlace_reset_qos (GstDeinterlace * self)
{
  gst_deinterlace_update_qos (self, 0.5, 0, GST_CLOCK_TIME_NONE);
}

static void
gst_deinterlace_read_qos (GstDeinterlace * self, gdouble * proportion,
    GstClockTime * time)
{
  GST_OBJECT_LOCK (self);
  *proportion = self->proportion;
  *time = self->earliest_time;
  GST_OBJECT_UNLOCK (self);
}

/* Perform qos calculations before processing the next frame. Returns TRUE if
 * the frame should be processed, FALSE if the frame can be dropped entirely */
static gboolean
gst_deinterlace_do_qos (GstDeinterlace * self, GstClockTime timestamp)
{
  GstClockTime qostime, earliest_time;
  gdouble proportion;

  /* no timestamp, can't do QoS => process frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp))) {
    GST_LOG_OBJECT (self, "invalid timestamp, can't do QoS, process frame");
    return TRUE;
  }

  /* get latest QoS observation values */
  gst_deinterlace_read_qos (self, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) => process frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (earliest_time))) {
    GST_LOG_OBJECT (self, "no observation yet, process frame");
    return TRUE;
  }

  /* qos is done on running time */
  qostime = gst_segment_to_running_time (&self->segment, GST_FORMAT_TIME,
      timestamp);

  /* see how our next timestamp relates to the latest qos timestamp */
  GST_LOG_OBJECT (self, "qostime %" GST_TIME_FORMAT ", earliest %"
      GST_TIME_FORMAT, GST_TIME_ARGS (qostime), GST_TIME_ARGS (earliest_time));

  if (qostime != GST_CLOCK_TIME_NONE && qostime <= earliest_time) {
    GST_DEBUG_OBJECT (self, "we are late, drop frame");
    return FALSE;
  }

  GST_LOG_OBJECT (self, "process frame");
  return TRUE;
}

static GstFlowReturn
gst_deinterlace_chain (GstPad * pad, GstBuffer * buf)
{
  GstDeinterlace *self = GST_DEINTERLACE (GST_PAD_PARENT (pad));
  GstClockTime timestamp;
  GstFlowReturn ret = GST_FLOW_OK;
  gint fields_required = 0;
  gint cur_field_idx = 0;
  GstBuffer *outbuf;

  if (self->still_frame_mode || self->passthrough)
    return gst_pad_push (self->srcpad, buf);

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    GST_DEBUG_OBJECT (self, "DISCONT buffer, resetting history");
    gst_deinterlace_reset_history (self);
  }

  gst_deinterlace_push_history (self, buf);
  buf = NULL;

  fields_required = gst_deinterlace_method_get_fields_required (self->method);

  /* Not enough fields in the history */
  if (self->history_count < fields_required + 1) {
    GST_DEBUG_OBJECT (self, "Need more fields (have %d, need %d)",
        self->history_count, fields_required + 1);
    return GST_FLOW_OK;
  }

  while (self->history_count >= fields_required) {
    if (self->fields == GST_DEINTERLACE_ALL)
      GST_DEBUG_OBJECT (self, "All fields");
    else if (self->fields == GST_DEINTERLACE_TF)
      GST_DEBUG_OBJECT (self, "Top fields");
    else if (self->fields == GST_DEINTERLACE_BF)
      GST_DEBUG_OBJECT (self, "Bottom fields");

    cur_field_idx = self->history_count - fields_required;

    if ((self->field_history[cur_field_idx].flags == PICTURE_INTERLACED_TOP
            && self->fields == GST_DEINTERLACE_TF) ||
        self->fields == GST_DEINTERLACE_ALL) {
      GST_DEBUG_OBJECT (self, "deinterlacing top field");

      /* create new buffer */
      ret = gst_pad_alloc_buffer (self->srcpad,
          GST_BUFFER_OFFSET_NONE, self->frame_size, self->src_caps, &outbuf);
      if (ret != GST_FLOW_OK)
        return ret;

      if (self->src_caps != GST_BUFFER_CAPS (outbuf) &&
          !gst_caps_is_equal (self->src_caps, GST_BUFFER_CAPS (outbuf))) {
        gst_caps_replace (&self->request_caps, GST_BUFFER_CAPS (outbuf));
        GST_DEBUG_OBJECT (self, "Upstream wants new caps %" GST_PTR_FORMAT,
            self->request_caps);

        gst_buffer_unref (outbuf);
        outbuf = gst_buffer_try_new_and_alloc (self->frame_size);

        if (!outbuf)
          return GST_FLOW_ERROR;

        gst_buffer_set_caps (outbuf, self->src_caps);
      }

      g_return_val_if_fail (self->history_count - 1 -
          gst_deinterlace_method_get_latency (self->method) >= 0,
          GST_FLOW_ERROR);

      buf =
          self->field_history[self->history_count - 1 -
          gst_deinterlace_method_get_latency (self->method)].buf;
      timestamp = GST_BUFFER_TIMESTAMP (buf);

      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      if (self->fields == GST_DEINTERLACE_ALL)
        GST_BUFFER_DURATION (outbuf) = self->field_duration;
      else
        GST_BUFFER_DURATION (outbuf) = 2 * self->field_duration;

      /* Check if we need to drop the frame because of QoS */
      if (!gst_deinterlace_do_qos (self, GST_BUFFER_TIMESTAMP (buf))) {
        gst_buffer_unref (gst_deinterlace_pop_history (self));
        gst_buffer_unref (outbuf);
        outbuf = NULL;
        ret = GST_FLOW_OK;
      } else {
        /* do magic calculus */
        gst_deinterlace_method_deinterlace_frame (self->method,
            self->field_history, self->history_count, outbuf);

        gst_buffer_unref (gst_deinterlace_pop_history (self));

        if (gst_deinterlace_clip_buffer (self, outbuf)) {
          ret = gst_pad_push (self->srcpad, outbuf);
        } else {
          ret = GST_FLOW_OK;
          gst_buffer_unref (outbuf);
        }

        outbuf = NULL;
        if (ret != GST_FLOW_OK)
          return ret;
      }
    }
    /* no calculation done: remove excess field */
    else if (self->field_history[cur_field_idx].flags ==
        PICTURE_INTERLACED_TOP && self->fields == GST_DEINTERLACE_BF) {
      GST_DEBUG_OBJECT (self, "Removing unused top field");
      gst_buffer_unref (gst_deinterlace_pop_history (self));
    }

    cur_field_idx = self->history_count - fields_required;
    if (self->history_count < fields_required)
      break;

    /* deinterlace bottom_field */
    if ((self->field_history[cur_field_idx].flags == PICTURE_INTERLACED_BOTTOM
            && self->fields == GST_DEINTERLACE_BF) ||
        self->fields == GST_DEINTERLACE_ALL) {
      GST_DEBUG_OBJECT (self, "deinterlacing bottom field");

      /* create new buffer */
      ret = gst_pad_alloc_buffer (self->srcpad,
          GST_BUFFER_OFFSET_NONE, self->frame_size, self->src_caps, &outbuf);
      if (ret != GST_FLOW_OK)
        return ret;

      if (self->src_caps != GST_BUFFER_CAPS (outbuf) &&
          !gst_caps_is_equal (self->src_caps, GST_BUFFER_CAPS (outbuf))) {
        gst_caps_replace (&self->request_caps, GST_BUFFER_CAPS (outbuf));
        GST_DEBUG_OBJECT (self, "Upstream wants new caps %" GST_PTR_FORMAT,
            self->request_caps);

        gst_buffer_unref (outbuf);
        outbuf = gst_buffer_try_new_and_alloc (self->frame_size);

        if (!outbuf)
          return GST_FLOW_ERROR;

        gst_buffer_set_caps (outbuf, self->src_caps);
      }

      g_return_val_if_fail (self->history_count - 1 -
          gst_deinterlace_method_get_latency (self->method) >= 0,
          GST_FLOW_ERROR);

      buf =
          self->field_history[self->history_count - 1 -
          gst_deinterlace_method_get_latency (self->method)].buf;
      timestamp = GST_BUFFER_TIMESTAMP (buf);

      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      if (self->fields == GST_DEINTERLACE_ALL)
        GST_BUFFER_DURATION (outbuf) = self->field_duration;
      else
        GST_BUFFER_DURATION (outbuf) = 2 * self->field_duration;

      /* Check if we need to drop the frame because of QoS */
      if (!gst_deinterlace_do_qos (self, GST_BUFFER_TIMESTAMP (buf))) {
        gst_buffer_unref (gst_deinterlace_pop_history (self));
        gst_buffer_unref (outbuf);
        outbuf = NULL;
        ret = GST_FLOW_OK;
      } else {
        /* do magic calculus */
        gst_deinterlace_method_deinterlace_frame (self->method,
            self->field_history, self->history_count, outbuf);

        gst_buffer_unref (gst_deinterlace_pop_history (self));

        if (gst_deinterlace_clip_buffer (self, outbuf)) {
          ret = gst_pad_push (self->srcpad, outbuf);
        } else {
          ret = GST_FLOW_OK;
          gst_buffer_unref (outbuf);
        }

        outbuf = NULL;
        if (ret != GST_FLOW_OK)
          return ret;
      }
    }
    /* no calculation done: remove excess field */
    else if (self->field_history[cur_field_idx].flags ==
        PICTURE_INTERLACED_BOTTOM && self->fields == GST_DEINTERLACE_TF) {
      GST_DEBUG_OBJECT (self, "Removing unused bottom field");
      gst_buffer_unref (gst_deinterlace_pop_history (self));
    }
  }

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
gst_deinterlace_getcaps (GstPad * pad)
{
  GstCaps *ret;
  GstDeinterlace *self = GST_DEINTERLACE (gst_pad_get_parent (pad));
  GstPad *otherpad;
  gint len;
  const GstCaps *ourcaps;
  GstCaps *peercaps;

  GST_OBJECT_LOCK (self);

  otherpad = (pad == self->srcpad) ? self->sinkpad : self->srcpad;

  ourcaps = gst_pad_get_pad_template_caps (pad);
  peercaps = gst_pad_peer_get_caps (otherpad);

  if (peercaps) {
    GST_DEBUG_OBJECT (pad, "Peer has caps %" GST_PTR_FORMAT, peercaps);
    ret = gst_caps_intersect (ourcaps, peercaps);
    gst_caps_unref (peercaps);
  } else {
    ret = gst_caps_copy (ourcaps);
  }

  GST_OBJECT_UNLOCK (self);

  if (!self->passthrough && self->fields == GST_DEINTERLACE_ALL) {
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
gst_deinterlace_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean res = TRUE;
  GstDeinterlace *self = GST_DEINTERLACE (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *othercaps;

  otherpad = (pad == self->srcpad) ? self->sinkpad : self->srcpad;

  res =
      gst_video_format_parse_caps (caps, &self->format, &self->width,
      &self->height);
  res &= gst_video_parse_caps_framerate (caps, &self->fps_n, &self->fps_d);
  if (pad == self->sinkpad)
    res &= gst_video_format_parse_caps_interlaced (caps, &self->interlaced);
  if (!res)
    goto invalid_caps;

  gst_deinterlace_update_passthrough (self);

  if (!self->passthrough && self->fields == GST_DEINTERLACE_ALL) {
    gint fps_n = self->fps_n, fps_d = self->fps_d;

    if (!gst_fraction_double (&fps_n, &fps_d, otherpad != self->srcpad))
      goto invalid_caps;

    othercaps = gst_caps_copy (caps);

    gst_caps_set_simple (othercaps, "framerate", GST_TYPE_FRACTION, fps_n,
        fps_d, NULL);
  } else {
    othercaps = gst_caps_ref (caps);
  }

  if (otherpad == self->srcpad && self->mode != GST_DEINTERLACE_MODE_DISABLED) {
    othercaps = gst_caps_make_writable (othercaps);
    gst_caps_set_simple (othercaps, "interlaced", G_TYPE_BOOLEAN, FALSE, NULL);
  }

  if (!gst_pad_set_caps (otherpad, othercaps))
    goto caps_not_accepted;

  self->frame_size =
      gst_video_format_get_size (self->format, self->width, self->height);

  if (self->fields == GST_DEINTERLACE_ALL && otherpad == self->srcpad)
    self->field_duration =
        gst_util_uint64_scale (GST_SECOND, self->fps_d, self->fps_n);
  else
    self->field_duration =
        gst_util_uint64_scale (GST_SECOND, self->fps_d, 2 * self->fps_n);

  if (pad == self->sinkpad) {
    gst_caps_replace (&self->sink_caps, caps);
    gst_caps_replace (&self->src_caps, othercaps);
  } else {
    gst_caps_replace (&self->src_caps, caps);
    gst_caps_replace (&self->sink_caps, othercaps);
  }

  gst_deinterlace_set_method (self, self->method_id);
  gst_deinterlace_method_setup (self->method, self->format, self->width,
      self->height);

  GST_DEBUG_OBJECT (pad, "Set caps: %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (pad, "Other caps: %" GST_PTR_FORMAT, othercaps);

  gst_caps_unref (othercaps);

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
gst_deinterlace_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstDeinterlace *self = GST_DEINTERLACE (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat fmt;
      gboolean is_update;
      gint64 start, end, base;
      gdouble rate;

      gst_event_parse_new_segment (event, &is_update, &rate, &fmt, &start,
          &end, &base);
      if (fmt == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad,
            "Got NEWSEGMENT event in GST_FORMAT_TIME, passing on (%"
            GST_TIME_FORMAT " - %" GST_TIME_FORMAT ")", GST_TIME_ARGS (start),
            GST_TIME_ARGS (end));
        gst_segment_set_newsegment (&self->segment, is_update, rate, fmt, start,
            end, base);
      } else {
        gst_segment_init (&self->segment, GST_FORMAT_TIME);
      }

      gst_deinterlace_reset_qos (self);
      gst_deinterlace_reset_history (self);
      res = gst_pad_push_event (self->srcpad, event);
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:{
      gboolean still_state;

      if (gst_video_event_parse_still_frame (event, &still_state)) {
        GST_DEBUG_OBJECT (self, "Received still frame event, state %d",
            still_state);

        if (still_state) {
          GstFlowReturn ret;

          GST_DEBUG_OBJECT (self, "Handling still frame");
          self->still_frame_mode = TRUE;
          if (self->last_buffer) {
            ret =
                gst_pad_push (self->srcpad, gst_buffer_ref (self->last_buffer));
            GST_DEBUG_OBJECT (self, "Pushed still frame, result: %s",
                gst_flow_get_name (ret));
          } else {
            GST_WARNING_OBJECT (self, "No pending buffer!");
          }
        } else {
          GST_DEBUG_OBJECT (self, "Ending still frames");
          self->still_frame_mode = FALSE;
        }
      }
    }
      /* fall through */
    case GST_EVENT_EOS:
      gst_deinterlace_reset_history (self);

      /* fall through */
    default:
      res = gst_pad_push_event (self->srcpad, event);
      break;

    case GST_EVENT_FLUSH_STOP:
      if (self->still_frame_mode) {
        GST_DEBUG_OBJECT (self, "Ending still frames");
        self->still_frame_mode = FALSE;
      }
      gst_deinterlace_reset_qos (self);
      res = gst_pad_push_event (self->srcpad, event);
      gst_deinterlace_reset_history (self);
      break;
  }

  gst_object_unref (self);
  return res;
}

static gboolean
gst_deinterlace_sink_query (GstPad * pad, GstQuery * query)
{
  GstDeinterlace *self = GST_DEINTERLACE (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  GST_LOG_OBJECT (pad, "%s query", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    default:{
      GstPad *peer = gst_pad_get_peer (self->srcpad);

      if (peer) {
        res = gst_pad_query (peer, query);
        gst_object_unref (peer);
      } else {
        res = FALSE;
      }
      break;
    }
  }

  gst_object_unref (self);
  return res;
}

static GstStateChangeReturn
gst_deinterlace_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDeinterlace *self = GST_DEINTERLACE (element);

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
      gst_deinterlace_reset (self);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }

  return ret;
}

static gboolean
gst_deinterlace_src_event (GstPad * pad, GstEvent * event)
{
  GstDeinterlace *self = GST_DEINTERLACE (gst_pad_get_parent (pad));
  gboolean res;

  GST_DEBUG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:{
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      gdouble proportion;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      gst_deinterlace_update_qos (self, proportion, diff, timestamp);
    }
      /* fall through */
    default:
      res = gst_pad_push_event (self->sinkpad, event);
      break;
  }

  gst_object_unref (self);

  return res;
}

static gboolean
gst_deinterlace_src_query (GstPad * pad, GstQuery * query)
{
  GstDeinterlace *self = GST_DEINTERLACE (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  GST_LOG_OBJECT (pad, "%s query", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
      if (!self->passthrough) {
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

            GST_DEBUG_OBJECT (self, "Calculated total latency : min %"
                GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
                GST_TIME_ARGS (min), GST_TIME_ARGS (max));

            gst_query_set_latency (query, live, min, max);
          }
          gst_object_unref (peer);
        } else {
          res = FALSE;
        }
        break;
      }
    default:{
      GstPad *peer = gst_pad_get_peer (self->sinkpad);

      if (peer) {
        res = gst_pad_query (peer, query);
        gst_object_unref (peer);
      } else {
        res = FALSE;
      }
      break;
    }
  }

  gst_object_unref (self);
  return res;
}

static const GstQueryType *
gst_deinterlace_src_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_LATENCY,
    GST_QUERY_NONE
  };
  return types;
}

static GstFlowReturn
gst_deinterlace_alloc_buffer (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstDeinterlace *self = GST_DEINTERLACE (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_OK;

  *buf = NULL;

  GST_DEBUG_OBJECT (pad, "alloc with caps %" GST_PTR_FORMAT ", size %u", caps,
      size);

  if (self->still_frame_mode || self->passthrough) {
    ret = gst_pad_alloc_buffer (self->srcpad, offset, size, caps, buf);
  } else if (G_LIKELY (!self->request_caps)) {
    *buf = gst_buffer_try_new_and_alloc (size);
    if (G_UNLIKELY (!*buf)) {
      ret = GST_FLOW_ERROR;
    } else {
      gst_buffer_set_caps (*buf, caps);
      GST_BUFFER_OFFSET (*buf) = offset;
    }
  } else {
    gint width, height;
    GstVideoFormat fmt;
    guint new_frame_size;
    GstCaps *new_caps = gst_caps_copy (self->request_caps);

    if (self->fields == GST_DEINTERLACE_ALL) {
      gint n, d;
      GstStructure *s = gst_caps_get_structure (new_caps, 0);

      gst_structure_get_fraction (s, "framerate", &n, &d);

      if (!gst_fraction_double (&n, &d, TRUE)) {
        gst_object_unref (self);
        gst_caps_unref (new_caps);
        return GST_FLOW_OK;
      }

      gst_structure_set (s, "framerate", GST_TYPE_FRACTION, n, d, NULL);
    }

    if (G_UNLIKELY (!gst_video_format_parse_caps (new_caps, &fmt, &width,
                &height))) {
      gst_object_unref (self);
      gst_caps_unref (new_caps);
      return GST_FLOW_OK;
    }

    new_frame_size = gst_video_format_get_size (fmt, width, height);

    *buf = gst_buffer_try_new_and_alloc (new_frame_size);
    if (G_UNLIKELY (!*buf)) {
      ret = GST_FLOW_ERROR;
    } else {
      gst_buffer_set_caps (*buf, new_caps);
      gst_caps_unref (self->request_caps);
      self->request_caps = NULL;
      gst_caps_unref (new_caps);
    }
  }

  gst_object_unref (self);

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (deinterlace_debug, "deinterlace", 0, "Deinterlacer");

  oil_init ();

  if (!gst_element_register (plugin, "deinterlace", GST_RANK_NONE,
          GST_TYPE_DEINTERLACE)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "deinterlace",
    "Deinterlacer", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
