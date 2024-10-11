/*
 * GStreamer
 * Copyright (C) 2005 Martin Eikermann <meiker@upb.de>
 * Copyright (C) 2008-2010 Sebastian Dröge <slomo@collabora.co.uk>
 * Copyright (C) 2011 Robert Swain <robert.swain@collabora.co.uk>
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
 * SECTION:element-deinterlace
 * @title: deinterlace
 *
 * deinterlace deinterlaces interlaced video frames to progressive video frames.
 * For this different algorithms can be selected which will be described later.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v filesrc location=/path/to/file ! decodebin ! videoconvert ! deinterlace ! videoconvert ! autovideosink
 * ]| This pipeline deinterlaces a video file with the default deinterlacing options.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdeinterlace.h"
#include "tvtime/plugins.h"
#include "yadif.h"

#include <string.h>

#if HAVE_ORC
#include <orc/orc.h>
#endif

GST_DEBUG_CATEGORY_STATIC (deinterlace_debug);
#define GST_CAT_DEFAULT (deinterlace_debug)

/* Properties */

#define DEFAULT_MODE            GST_DEINTERLACE_MODE_AUTO
#define DEFAULT_METHOD          GST_DEINTERLACE_LINEAR
#define DEFAULT_FIELDS          GST_DEINTERLACE_ALL
#define DEFAULT_FIELD_LAYOUT    GST_DEINTERLACE_LAYOUT_AUTO
#define DEFAULT_LOCKING         GST_DEINTERLACE_LOCKING_NONE
#define DEFAULT_IGNORE_OBSCURE  TRUE
#define DEFAULT_DROP_ORPHANS    TRUE

enum
{
  PROP_0,
  PROP_MODE,
  PROP_METHOD,
  PROP_FIELDS,
  PROP_FIELD_LAYOUT,
  PROP_LOCKING,
  PROP_IGNORE_OBSCURE,
  PROP_DROP_ORPHANS
};

/* P is progressive, meaning the top and bottom fields belong to
 * the same frame, i.e. they were sampled at the same time */
#define GST_DEINTERLACE_BUFFER_STATE_P    (1<<0)
/* I is interlaced meaning that the two fields were sampled at
 * different times, usually equidistant in time so one at 1/60,
 * the other at 2/60 */
#define GST_DEINTERLACE_BUFFER_STATE_I    (1<<1)
/* TC is telecine, B means bottom, T means top */
#define GST_DEINTERLACE_BUFFER_STATE_TC_B (1<<2)
#define GST_DEINTERLACE_BUFFER_STATE_TC_T (1<<3)
/* TC_P means telecine progressive meaning that the two fields
 * in the frame were sampled at the same time */
#define GST_DEINTERLACE_BUFFER_STATE_TC_P (1<<4)
/* TC_M i think means telecine mixed, meaning that the two fields
 * are sampled at different times so you need to find the other field
 * in the previous or next frame */
#define GST_DEINTERLACE_BUFFER_STATE_TC_M (1<<5)
/* RFF means repeat field flag and indicates a field that has
 * previously been seen */
#define GST_DEINTERLACE_BUFFER_STATE_RFF  (1<<6)

#define GST_ONE \
  (GST_DEINTERLACE_BUFFER_STATE_TC_T | GST_DEINTERLACE_BUFFER_STATE_TC_B)
#define GST_PRG \
  (GST_DEINTERLACE_BUFFER_STATE_P | GST_DEINTERLACE_BUFFER_STATE_TC_P)
#define GST_INT \
  (GST_DEINTERLACE_BUFFER_STATE_I | GST_DEINTERLACE_BUFFER_STATE_TC_M)
#define GST_RFF (GST_DEINTERLACE_BUFFER_STATE_RFF)

#define GST_DEINTERLACE_OBSCURE_THRESHOLD 5

static const TelecinePattern telecine_patterns[] = {
  /* 60i -> 60p or 50i -> 50p (NOTE THE WEIRD RATIOS) */
  {"1:1", 1, 2, 1, {GST_ONE,}},
  /* 60i -> 30p or 50i -> 25p */
  {"2:2", 1, 1, 1, {GST_INT,}},
  /* 60i telecine -> 24p */
  {"2:3-RFF", 4, 4, 5, {GST_PRG, GST_RFF, GST_PRG, GST_RFF,}},
  {"2:3", 5, 4, 5, {GST_PRG, GST_PRG, GST_ONE, GST_ONE, GST_PRG,}},
  {"3:2:2:3-RFF", 4, 4, 5, {GST_RFF, GST_PRG, GST_PRG, GST_RFF,}},
  {"3:2:2:3", 5, 4, 5, {GST_PRG, GST_ONE, GST_INT, GST_ONE, GST_PRG,}},
  /* fieldanalysis should indicate this using RFF on the second and fourth
   * buffers and not send the third buffer at all. it will be identified as
   * 3:2:2:3-RFF */
  /* {"2:3:3:2", 5, 4, 5, {GST_PRG, GST_PRG, GST_DRP, GST_PRG, GST_PRG,}}, */

  /* The following patterns are obscure and are ignored if ignore-obscure is
   * set to true. If any patterns are added above this line, check and edit
   * GST_DEINTERLACE_OBSCURE_THRESHOLD */

  /* 50i Euro pulldown -> 24p */
  {"2-11:3", 25, 24, 25, {GST_PRG, GST_PRG, GST_PRG, GST_PRG, GST_PRG,
              GST_PRG, GST_PRG, GST_PRG, GST_PRG, GST_PRG,
              GST_PRG, GST_PRG, GST_ONE, GST_INT, GST_INT,
              GST_INT, GST_INT, GST_INT, GST_INT, GST_INT,
          GST_INT, GST_INT, GST_INT, GST_ONE, GST_PRG,}},
#if 0
  /* haven't figured out how fieldanalysis should handle these yet */
  /* 60i (NTSC 30000/1001) -> 16p (16000/1001) */
  {"3:4-3", 15, 8, 15, {GST_PRG, GST_DRP, GST_PRG, GST_DRP, GST_PRG,
              GST_DRP, GST_PRG, GST_DRP, GST_PRG, GST_DRP,
          GST_PRG, GST_DRP, GST_PRG, GST_DRP, GST_PRG,}},
  /* 50i (PAL) -> 16p */
  {"3-7:4", 25, 16, 25, {GST_PRG, GST_DRP, GST_PRG, GST_PRG, GST_DRP,
              GST_PRG, GST_PRG, GST_DRP, GST_PRG, GST_PRG,
              GST_DRP, GST_PRG, GST_DRP, GST_PRG, GST_PRG,
              GST_DRP, GST_PRG, GST_PRG, GST_DRP, GST_PRG,
          GST_PRG, GST_DRP, GST_PRG, GST_PRG, GST_DRP,}},
  /* NTSC 60i -> 18p */
  {"3:3:4", 5, 3, 5, {GST_PRG, GST_DRP, GST_PRG, GST_DRP, GST_PRG,}},
  /* NTSC 60i -> 20p */
  {"3:3", 3, 2, 3, {GST_PRG, GST_DRP, GST_PRG,}},
#endif
  /* NTSC 60i -> 27.5 */
  {"3:2-4", 11, 10, 11, {GST_PRG, GST_PRG, GST_PRG, GST_PRG, GST_PRG,
              GST_PRG, GST_ONE, GST_INT, GST_INT, GST_INT,
          GST_ONE,}},
  /* PAL 50i -> 27.5 */
  {"1:2-4", 9, 9, 10, {GST_PRG, GST_PRG, GST_PRG, GST_PRG, GST_INT,
          GST_INT, GST_INT, GST_INT, GST_INT,}},
};

static const GEnumValue methods_types[] = {
  {GST_DEINTERLACE_TOMSMOCOMP, "Motion Adaptive: Motion Search",
      "tomsmocomp"},
  {GST_DEINTERLACE_GREEDY_H, "Motion Adaptive: Advanced Detection",
      "greedyh"},
  {GST_DEINTERLACE_GREEDY_L, "Motion Adaptive: Simple Detection", "greedyl"},
  {GST_DEINTERLACE_VFIR, "Blur Vertical", "vfir"},
  {GST_DEINTERLACE_LINEAR, "Linear", "linear"},
  {GST_DEINTERLACE_LINEAR_BLEND, "Blur: Temporal (Do Not Use)",
      "linearblend"},
  {GST_DEINTERLACE_SCALER_BOB, "Double lines", "scalerbob"},
  {GST_DEINTERLACE_WEAVE, "Weave (Do Not Use)", "weave"},
  {GST_DEINTERLACE_WEAVE_TFF, "Progressive: Top Field First (Do Not Use)",
      "weavetff"},
  {GST_DEINTERLACE_WEAVE_BFF, "Progressive: Bottom Field First (Do Not Use)",
      "weavebff"},
  {GST_DEINTERLACE_YADIF, "YADIF Adaptive Deinterlacer", "yadif"},
  {0, NULL, NULL},
};

static const GEnumValue locking_types[] = {
  {GST_DEINTERLACE_LOCKING_NONE,
      "No pattern locking", "none"},
  {GST_DEINTERLACE_LOCKING_AUTO,
        "Choose passive/active locking depending on whether upstream is live",
      "auto"},
  {GST_DEINTERLACE_LOCKING_ACTIVE,
        "Block until pattern-locked. Use accurate timestamp interpolation within a pattern repeat.",
      "active"},
  {GST_DEINTERLACE_LOCKING_PASSIVE,
        "Do not block. Use naïve timestamp adjustment until pattern-locked based on state history.",
      "passive"},
  {0, NULL, NULL},
};


#define GST_TYPE_DEINTERLACE_METHODS (gst_deinterlace_methods_get_type ())
static GType
gst_deinterlace_methods_get_type (void)
{
  static GType deinterlace_methods_type = 0;

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
    {GST_DEINTERLACE_FIELDS_AUTO, "Automatically detect", "auto"},
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
    {GST_DEINTERLACE_MODE_AUTO, "Auto detection (best effort)", "auto"},
    {GST_DEINTERLACE_MODE_INTERLACED, "Force deinterlacing", "interlaced"},
    {GST_DEINTERLACE_MODE_DISABLED, "Run in passthrough mode", "disabled"},
    {GST_DEINTERLACE_MODE_AUTO_STRICT, "Auto detection (strict)",
        "auto-strict"},
    {0, NULL, NULL},
  };

  if (!deinterlace_modes_type) {
    deinterlace_modes_type =
        g_enum_register_static ("GstDeinterlaceModes", modes_types);
  }
  return deinterlace_modes_type;
}

#define GST_TYPE_DEINTERLACE_LOCKING (gst_deinterlace_locking_get_type ())
static GType
gst_deinterlace_locking_get_type (void)
{
  static GType deinterlace_locking_type = 0;

  if (!deinterlace_locking_type) {
    deinterlace_locking_type =
        g_enum_register_static ("GstDeinterlaceLocking", locking_types);
  }

  return deinterlace_locking_type;
}

#if G_BYTE_ORDER == G_BIG_ENDIAN
#define DEINTERLACE_VIDEO_FORMATS \
    "{ AYUV, ARGB, ABGR, RGBA, BGRA, Y444, xRGB, xBGR, RGBx, BGRx, RGB, " \
    "BGR, YUY2, YVYU, UYVY, Y42B, I420, YV12, Y41B, NV12, NV21, " \
    "Y444_16BE, Y444_12BE, Y444_10BE, I422_12BE, I422_10BE, I420_12BE, I420_10BE } "
#else
#define DEINTERLACE_VIDEO_FORMATS \
    "{ AYUV, ARGB, ABGR, RGBA, BGRA, Y444, xRGB, xBGR, RGBx, BGRx, RGB, " \
    "BGR, YUY2, YVYU, UYVY, Y42B, I420, YV12, Y41B, NV12, NV21, " \
    "Y444_16LE, Y444_12LE, Y444_10LE, I422_12LE, I422_10LE, I420_12LE, I420_10LE } "
#endif

#define DEINTERLACE_CAPS GST_VIDEO_CAPS_MAKE(DEINTERLACE_VIDEO_FORMATS)

#define DEINTERLACE_ALL_CAPS DEINTERLACE_CAPS ";" \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS_ANY)

static GstStaticCaps progressive_caps =
GST_STATIC_CAPS ("video/x-raw(ANY),interlace-mode=(string)progressive");
static GstStaticCaps deinterlace_caps = GST_STATIC_CAPS (DEINTERLACE_CAPS);

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DEINTERLACE_ALL_CAPS)
    );

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DEINTERLACE_ALL_CAPS)
    );

static void gst_deinterlace_finalize (GObject * self);
static void gst_deinterlace_set_property (GObject * self, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_deinterlace_get_property (GObject * self, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_deinterlace_getcaps (GstDeinterlace * self, GstPad * pad,
    GstCaps * filter);
static gboolean gst_deinterlace_setcaps (GstDeinterlace * self, GstPad * pad,
    GstCaps * caps, gboolean force);
static gboolean gst_deinterlace_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_deinterlace_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstFlowReturn gst_deinterlace_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstStateChangeReturn gst_deinterlace_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_deinterlace_set_allocation (GstDeinterlace * self,
    GstBufferPool * pool, GstAllocator * allocator,
    GstAllocationParams * params);

static gboolean gst_deinterlace_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_deinterlace_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static GstFlowReturn gst_deinterlace_output_frame (GstDeinterlace * self,
    gboolean flushing);
static void gst_deinterlace_reset (GstDeinterlace * self);
static void gst_deinterlace_update_qos (GstDeinterlace * self,
    gdouble proportion, GstClockTimeDiff diff, GstClockTime time);
static void gst_deinterlace_reset_qos (GstDeinterlace * self);
static void gst_deinterlace_read_qos (GstDeinterlace * self,
    gdouble * proportion, GstClockTime * time);
static gboolean deinterlace_element_init (GstPlugin * plugin);

#define IS_TELECINE(m) ((m) == GST_VIDEO_INTERLACE_MODE_MIXED && self->pattern > 1)

/* FIXME: what's the point of the childproxy interface here? What can you
 * actually do with it? The method objects seem to have no properties */
#if 0
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
#endif

#define parent_class gst_deinterlace_parent_class
G_DEFINE_TYPE (GstDeinterlace, gst_deinterlace, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE_CUSTOM (deinterlace, deinterlace_element_init);

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
      gst_deinterlace_method_weave_bff_get_type}, {
      gst_deinterlace_method_yadif_get_type}
};

static void
gst_deinterlace_set_method (GstDeinterlace * self, GstDeinterlaceMethods method)
{
  GType method_type;
  gint width, height;
  GstVideoFormat format;

  GST_DEBUG_OBJECT (self, "Setting new method %d", method);

  width = GST_VIDEO_INFO_WIDTH (&self->vinfo);
  height = GST_VIDEO_INFO_HEIGHT (&self->vinfo);
  format = GST_VIDEO_INFO_FORMAT (&self->vinfo);

  if (self->method) {
    if (self->method_id == method &&
        gst_deinterlace_method_supported (G_TYPE_FROM_INSTANCE (self->method),
            format, width, height)) {
      GST_DEBUG_OBJECT (self, "Reusing current method");
      return;
    }
#if 0
    gst_child_proxy_child_removed (GST_OBJECT (self),
        GST_OBJECT (self->method));
#endif

    GST_OBJECT_LOCK (self);
    gst_object_unparent (GST_OBJECT (self->method));
    self->method = NULL;
    GST_OBJECT_UNLOCK (self);
  }

  method_type =
      _method_types[method].get_type !=
      NULL ? _method_types[method].get_type () : G_TYPE_INVALID;
  if (method_type == G_TYPE_INVALID
      || !gst_deinterlace_method_supported (method_type, format,
          width, height)) {
    GType tmp;
    gint i;

    method_type = G_TYPE_INVALID;

    GST_WARNING_OBJECT (self, "Method doesn't support requested format");
    for (i = 0; i < G_N_ELEMENTS (_method_types); i++) {
      if (_method_types[i].get_type == NULL)
        continue;
      tmp = _method_types[i].get_type ();
      if (gst_deinterlace_method_supported (tmp, format, width, height)) {
        GST_DEBUG_OBJECT (self, "Using method %d", i);
        method_type = tmp;
        method = i;
        break;
      }
    }
    /* If we get here we must have invalid caps! */
    g_assert (method_type != G_TYPE_INVALID);
  }

  self->method_id = method;

  GST_OBJECT_LOCK (self);
  self->method = g_object_new (method_type, "name", "method", NULL);
  gst_object_set_parent (GST_OBJECT (self->method), GST_OBJECT (self));
  GST_OBJECT_UNLOCK (self);

#if 0
  gst_child_proxy_child_added (GST_OBJECT (self), GST_OBJECT (self->method));
#endif

  if (self->method)
    gst_deinterlace_method_setup (self->method, &self->vinfo);
}

static gboolean
gst_deinterlace_clip_buffer (GstDeinterlace * self, GstBuffer * buffer)
{
  gboolean ret = TRUE;
  GstClockTime start, stop;
  guint64 cstart, cstop;

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
gst_deinterlace_class_init (GstDeinterlaceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  GstElementClass *element_class = (GstElementClass *) klass;

  gst_element_class_add_static_pad_template (element_class, &src_templ);
  gst_element_class_add_static_pad_template (element_class, &sink_templ);

  gst_element_class_set_static_metadata (element_class,
      "Deinterlacer",
      "Filter/Effect/Video/Deinterlace",
      "Deinterlace Methods ported from DScaler/TvTime",
      "Martin Eikermann <meiker@upb.de>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gobject_class->set_property = gst_deinterlace_set_property;
  gobject_class->get_property = gst_deinterlace_get_property;
  gobject_class->finalize = gst_deinterlace_finalize;

  /**
   * GstDeinterlace:mode:
   *
   * This selects whether the deinterlacing methods should
   * always be applied or if they should only be applied
   * on content that has the "interlaced" flag on the caps.
   */
  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode",
          "Mode",
          "Deinterlace Mode",
          GST_TYPE_DEINTERLACE_MODES,
          DEFAULT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  /**
   * GstDeinterlace:method:
   *
   * Selects the different deinterlacing algorithms that can be used.
   * These provide different quality and CPU usage.
   *
   * Some methods provide parameters which can be set by getting
   * the "method" child via the #GstChildProxy interface and
   * setting the appropriate properties on it.
   *
   * * tomsmocomp  Motion Adaptive: Motion Search
   * * greedyh Motion Adaptive: Advanced Detection
   * * greedyl Motion Adaptive: Simple Detection
   * * vfir Blur vertical
   * * linear Linear interpolation
   * * linearblend Linear interpolation in time domain.
   *   Any motion causes significant ghosting, so this
   *   method should not be used.
   * * scalerbob Double lines
   * * weave Weave. Bad quality, do not use.
   * * weavetff Progressive: Top Field First.  Bad quality, do not use.
   * * weavebff Progressive: Bottom Field First.  Bad quality, do not use.
   * * yadif YADIF Adaptive.
   */
  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method",
          "Method",
          "Deinterlace Method",
          GST_TYPE_DEINTERLACE_METHODS,
          DEFAULT_METHOD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  /**
   * GstDeinterlace:fields:
   *
   * This selects which fields should be output. If "all" is selected
   * the output framerate will be double.
   */
  g_object_class_install_property (gobject_class, PROP_FIELDS,
      g_param_spec_enum ("fields",
          "fields",
          "Fields to use for deinterlacing",
          GST_TYPE_DEINTERLACE_FIELDS,
          DEFAULT_FIELDS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  /**
   * GstDeinterlace:layout:
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

  /**
   * GstDeinterlace:locking:
   *
   * This selects which approach to pattern locking is used which affects
   * processing latency and accuracy of timestamp adjustment for telecine
   * streams.
   */
  g_object_class_install_property (gobject_class, PROP_LOCKING,
      g_param_spec_enum ("locking", "locking", "Pattern locking mode",
          GST_TYPE_DEINTERLACE_LOCKING, DEFAULT_LOCKING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstDeinterlace:ignore-obscure:
   *
   * This selects whether to ignore obscure/rare telecine patterns.
   * NTSC 2:3 pulldown variants are the only really common patterns.
   */
  g_object_class_install_property (gobject_class, PROP_IGNORE_OBSCURE,
      g_param_spec_boolean ("ignore-obscure", "ignore-obscure",
          "Ignore obscure telecine patterns (only consider P, I and 2:3 "
          "variants).", DEFAULT_IGNORE_OBSCURE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstDeinterlace:drop-orphans:
   *
   * This selects whether to drop orphan fields at the beginning of telecine
   * patterns in active locking mode.
   */
  g_object_class_install_property (gobject_class, PROP_DROP_ORPHANS,
      g_param_spec_boolean ("drop-orphans", "drop-orphans",
          "Drop orphan fields at the beginning of telecine patterns in "
          "active locking mode.", DEFAULT_DROP_ORPHANS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_deinterlace_change_state);

  gst_type_mark_as_plugin_api (GST_TYPE_DEINTERLACE_METHODS, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_DEINTERLACE_FIELDS, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_DEINTERLACE_FIELD_LAYOUT, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_DEINTERLACE_MODES, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_DEINTERLACE_LOCKING, 0);
}

#if 0
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
#endif

static void
gst_deinterlace_init (GstDeinterlace * self)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_chain));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_sink_event));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_sink_query));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_src_event));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_deinterlace_src_query));
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->mode = DEFAULT_MODE;
  self->user_set_method_id = DEFAULT_METHOD;
  gst_video_info_init (&self->vinfo);
  gst_video_info_init (&self->vinfo_out);
  gst_deinterlace_set_method (self, self->user_set_method_id);
  self->fields = DEFAULT_FIELDS;
  self->user_set_fields = DEFAULT_FIELDS;
  self->field_layout = DEFAULT_FIELD_LAYOUT;
  self->locking = DEFAULT_LOCKING;
  self->ignore_obscure = DEFAULT_IGNORE_OBSCURE;
  self->drop_orphans = DEFAULT_DROP_ORPHANS;

  self->low_latency = -1;
  self->pattern = -1;
  self->pattern_phase = -1;
  self->pattern_count = 0;
  self->output_count = 0;
  self->pattern_base_ts = GST_CLOCK_TIME_NONE;
  self->pattern_buf_dur = GST_CLOCK_TIME_NONE;
  self->still_frame_mode = FALSE;
  self->telecine_tc_warned = FALSE;

  gst_deinterlace_reset (self);
}

static GstVideoFrame *
gst_video_frame_new_and_map (GstVideoInfo * vinfo, GstBuffer * buffer,
    GstMapFlags flags)
{
  GstVideoFrame *frame = g_malloc0 (sizeof (GstVideoFrame));
  if (!gst_video_frame_map (frame, vinfo, buffer, flags)) {
    g_free (frame);
    g_return_val_if_reached (NULL);
    return NULL;
  }
  return frame;
}

static void
gst_video_frame_unmap_and_free (GstVideoFrame * frame)
{
  gst_video_frame_unmap (frame);
  g_free (frame);
}

static GstVideoFrame *
gst_deinterlace_pop_history (GstDeinterlace * self)
{
  GstVideoFrame *frame;

  g_return_val_if_fail (self->history_count > 0, NULL);

  GST_DEBUG_OBJECT (self, "Pop last history frame -- current history size %d",
      self->history_count);

  frame = self->field_history[self->history_count - 1].frame;

  self->history_count--;
  if (self->locking != GST_DEINTERLACE_LOCKING_NONE && (!self->history_count
          || GST_VIDEO_FRAME_PLANE_DATA (frame, 0) !=
          GST_VIDEO_FRAME_PLANE_DATA (self->field_history[self->history_count -
                  1].frame, 0))) {
    if (!self->low_latency)
      self->state_count--;
    if (self->pattern_lock) {
      self->pattern_count++;
      if (self->pattern != -1
          && self->pattern_count >= telecine_patterns[self->pattern].length) {
        self->pattern_count = 0;
        self->output_count = 0;
      }
    }
  }

  GST_DEBUG_OBJECT (self, "Returning frame: %p %" GST_TIME_FORMAT
      " with duration %" GST_TIME_FORMAT " and size %" G_GSIZE_FORMAT, frame,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (frame->buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (frame->buffer)),
      GST_VIDEO_FRAME_SIZE (frame));

  return frame;
}

static void
gst_deinterlace_delete_meta_at (GstDeinterlace * self, gint idx)
{
  GST_DEBUG_OBJECT (self, "idx:%d frame %p", idx,
      self->field_history[idx].frame);

  if (self->field_history[idx].tc) {
    gst_video_time_code_free (self->field_history[idx].tc);
    self->field_history[idx].tc = NULL;
  }
  if (self->field_history[idx].caption) {
    g_free (self->field_history[idx].caption->data);
    g_free (self->field_history[idx].caption);
    self->field_history[idx].caption = NULL;
  }
}

static void
gst_deinterlace_pop_and_clear (GstDeinterlace * self)
{
  gint idx;

  if (self->history_count <= 0)
    return;

  idx = self->history_count - 1;
  gst_deinterlace_delete_meta_at (self, idx);

  /* FIXME: pop_history should return a structure with the frame and its meta.
   * Currently we're just doing guesswork with the indices. Maybe just
   * refactor the history functionality to make something clearer */
  gst_video_frame_unmap_and_free (gst_deinterlace_pop_history (self));
}

static void
gst_deinterlace_reset_history (GstDeinterlace * self, gboolean drop_all)
{
  gint i;

  if (!drop_all) {
    GST_DEBUG_OBJECT (self, "Flushing history (count %d)", self->history_count);
    while (self->history_count > 0) {
      if (gst_deinterlace_output_frame (self, TRUE) != GST_FLOW_OK) {
        /* Encountered error, or flushing -> skip and drop all remaining */
        drop_all = TRUE;
        break;
      }
    }
  }
  if (drop_all) {
    GST_DEBUG_OBJECT (self, "Resetting history (count %d)",
        self->history_count);

    for (i = 0; i < self->history_count; i++) {
      if (self->field_history[i].frame) {
        gst_video_frame_unmap_and_free (self->field_history[i].frame);
        self->field_history[i].frame = NULL;
        gst_deinterlace_delete_meta_at (self, i);
      }
    }
  }
  memset (self->field_history, 0,
      GST_DEINTERLACE_MAX_FIELD_HISTORY * sizeof (GstDeinterlaceField));
  self->history_count = 0;
  memset (self->buf_states, 0,
      GST_DEINTERLACE_MAX_BUFFER_STATE_HISTORY *
      sizeof (GstDeinterlaceBufferState));
  self->state_count = 0;
  self->pattern_lock = FALSE;
  self->pattern_refresh = TRUE;
  self->cur_field_idx = -1;

  if (!self->still_frame_mode && self->last_buffer) {
    gst_buffer_unref (self->last_buffer);
    self->last_buffer = NULL;
  }
}

static void
gst_deinterlace_reset (GstDeinterlace * self)
{
  GST_DEBUG_OBJECT (self, "Resetting internal state");

  gst_video_info_init (&self->vinfo);
  gst_video_info_init (&self->vinfo_out);

  self->passthrough = FALSE;

  self->reconfigure = FALSE;
  if ((gint) self->new_mode != -1)
    self->mode = self->new_mode;
  if ((gint) self->new_fields != -1)
    self->user_set_fields = self->new_fields;
  self->new_mode = -1;
  self->new_fields = -1;

  gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);

  if (self->request_caps)
    gst_caps_unref (self->request_caps);
  self->request_caps = NULL;

  gst_deinterlace_reset_history (self, TRUE);

  gst_deinterlace_reset_qos (self);

  self->need_more = FALSE;
  self->have_eos = FALSE;

  self->discont = TRUE;
  self->telecine_tc_warned = FALSE;

  gst_deinterlace_set_allocation (self, NULL, NULL, NULL);
}

static void
gst_deinterlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDeinterlace *self;

  self = GST_DEINTERLACE (object);

  switch (prop_id) {
    case PROP_MODE:{
      gint new_mode;

      GST_OBJECT_LOCK (self);
      new_mode = g_value_get_enum (value);
      if (self->mode != new_mode && gst_pad_has_current_caps (self->srcpad)) {
        self->reconfigure = TRUE;
        self->new_mode = new_mode;
      } else {
        self->mode = new_mode;
      }
      GST_OBJECT_UNLOCK (self);
      break;
    }
    case PROP_METHOD:
      self->user_set_method_id = g_value_get_enum (value);
      gst_deinterlace_set_method (self, self->user_set_method_id);
      break;
    case PROP_FIELDS:{
      gint new_fields;

      GST_OBJECT_LOCK (self);
      new_fields = g_value_get_enum (value);
      if (self->user_set_fields != new_fields
          && gst_pad_has_current_caps (self->srcpad)) {
        self->reconfigure = TRUE;
        self->new_fields = new_fields;
      } else {
        self->user_set_fields = new_fields;
      }
      GST_OBJECT_UNLOCK (self);
      break;
    }
    case PROP_FIELD_LAYOUT:
      self->field_layout = g_value_get_enum (value);
      break;
    case PROP_LOCKING:
      self->locking = g_value_get_enum (value);
      break;
    case PROP_IGNORE_OBSCURE:
      self->ignore_obscure = g_value_get_boolean (value);
      break;
    case PROP_DROP_ORPHANS:
      self->drop_orphans = g_value_get_boolean (value);
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

  self = GST_DEINTERLACE (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;
    case PROP_METHOD:
      g_value_set_enum (value, self->user_set_method_id);
      break;
    case PROP_FIELDS:
      g_value_set_enum (value, self->user_set_fields);
      break;
    case PROP_FIELD_LAYOUT:
      g_value_set_enum (value, self->field_layout);
      break;
    case PROP_LOCKING:
      g_value_set_enum (value, self->locking);
      break;
    case PROP_IGNORE_OBSCURE:
      g_value_set_boolean (value, self->ignore_obscure);
      break;
    case PROP_DROP_ORPHANS:
      g_value_set_boolean (value, self->drop_orphans);
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

static void
gst_deinterlace_update_pattern_timestamps (GstDeinterlace * self)
{
  gint state_idx;
  if (self->low_latency) {
    /* in low-latency mode the buffer state history contains old buffer
     * states as well as the current one and perhaps some future ones.
     * the current buffer's state is given by the number of field pairs
     * rounded up, minus 1. the below is equivalent */
    state_idx = (self->history_count - 1) >> 1;
  } else {
    /* in high-latency mode state_count - 1 is the current buffer's state */
    state_idx = self->state_count - 1;
  }

  self->pattern_base_ts = self->buf_states[state_idx].timestamp;
  if (self->buf_states[state_idx].state != GST_RFF) {
    self->pattern_buf_dur =
        (self->buf_states[state_idx].duration *
        telecine_patterns[self->pattern].ratio_d) /
        telecine_patterns[self->pattern].ratio_n;
  } else {
    self->pattern_buf_dur =
        (self->buf_states[state_idx].duration *
        telecine_patterns[self->pattern].ratio_d * 2) /
        (telecine_patterns[self->pattern].ratio_n * 3);
  }
  GST_DEBUG_OBJECT (self,
      "Starting a new pattern repeat with base ts %" GST_TIME_FORMAT
      " and dur %" GST_TIME_FORMAT, GST_TIME_ARGS (self->pattern_base_ts),
      GST_TIME_ARGS (self->pattern_buf_dur));
}

static void
gst_deinterlace_get_buffer_state (GstDeinterlace * self, GstVideoFrame * frame,
    guint8 * state, GstVideoInterlaceMode * i_mode)
{
  GstVideoInterlaceMode interlacing_mode;

  if (!(i_mode || state))
    return;

  interlacing_mode = GST_VIDEO_INFO_INTERLACE_MODE (&frame->info);
  if (self->mode == GST_DEINTERLACE_MODE_INTERLACED)
    interlacing_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;

  if (state) {
    if (interlacing_mode == GST_VIDEO_INTERLACE_MODE_MIXED ||
        interlacing_mode == GST_VIDEO_INTERLACE_MODE_ALTERNATE) {
      if (GST_VIDEO_FRAME_IS_RFF (frame)) {
        *state = GST_DEINTERLACE_BUFFER_STATE_RFF;
      } else if (GST_VIDEO_FRAME_IS_ONEFIELD (frame)) {
        /* tc top if tff, tc bottom otherwise */
        if (GST_VIDEO_FRAME_IS_TFF (frame)) {
          *state = GST_DEINTERLACE_BUFFER_STATE_TC_T;
        } else {
          *state = GST_DEINTERLACE_BUFFER_STATE_TC_B;
        }
      } else if (GST_VIDEO_FRAME_IS_INTERLACED (frame)) {
        *state = GST_DEINTERLACE_BUFFER_STATE_TC_M;
      } else {
        *state = GST_DEINTERLACE_BUFFER_STATE_TC_P;
      }
    } else {
      if (interlacing_mode == GST_VIDEO_INTERLACE_MODE_INTERLEAVED) {
        *state = GST_DEINTERLACE_BUFFER_STATE_I;
      } else {
        *state = GST_DEINTERLACE_BUFFER_STATE_P;
      }
    }
  }

  if (i_mode)
    *i_mode = interlacing_mode;
}

#define STATE_TO_STRING(s) ((s) == GST_DEINTERLACE_BUFFER_STATE_P ? "P" : \
  (s) == GST_DEINTERLACE_BUFFER_STATE_I ? "I" : \
  (s) == GST_DEINTERLACE_BUFFER_STATE_TC_B ? "B" : \
  (s) == GST_DEINTERLACE_BUFFER_STATE_TC_T ? "T" : \
  (s) == GST_DEINTERLACE_BUFFER_STATE_TC_P ? "TCP" : \
  (s) == GST_DEINTERLACE_BUFFER_STATE_TC_M ? "TCM" : "RFF")

#define MODE_TO_STRING(m) ((m) == GST_VIDEO_INTERLACE_MODE_MIXED ? "MIXED" : \
  (m) == GST_VIDEO_INTERLACE_MODE_INTERLEAVED ? "I" : \
  (m) == GST_VIDEO_INTERLACE_MODE_ALTERNATE ? "A" : \
  (m) == GST_VIDEO_INTERLACE_MODE_FIELDS ? "FIELDS" : "P")

static GstFlowReturn
gst_deinterlace_push_history (GstDeinterlace * self, GstBuffer * buffer)
{
  int i = 1;
  GstDeinterlaceFieldLayout field_layout = self->field_layout;
  gboolean tff;
  gboolean onefield;
  GstVideoFrame *field1, *field2 = NULL;
  guint fields_to_push;
  guint field1_flags, field2_flags;
  GstVideoInterlaceMode interlacing_mode;
  guint8 buf_state;

  /* we will only read from this buffer and write into fresh output buffers
   * if this is not the case, change the map flags as appropriate
   */
  field1 = gst_video_frame_new_and_map (&self->vinfo, buffer, GST_MAP_READ);
  if (G_UNLIKELY (field1 == NULL)) {
    GST_ERROR_OBJECT (self, "Failed to map video frame for %" GST_PTR_FORMAT,
        buffer);
    return GST_FLOW_ERROR;
  }

  tff = GST_VIDEO_FRAME_IS_TFF (field1);
  onefield = GST_VIDEO_FRAME_IS_ONEFIELD (field1);
  fields_to_push = (onefield) ? 1 : 2;

  if (G_UNLIKELY (self->history_count >=
          GST_DEINTERLACE_MAX_FIELD_HISTORY - fields_to_push)) {
    GST_WARNING_OBJECT (self, "history count exceeded limit");
    gst_video_frame_unmap_and_free (field1);
    return GST_FLOW_OK;         /* When does this happen? */
  }

  field2 = gst_video_frame_new_and_map (&self->vinfo, buffer, GST_MAP_READ);
  if (G_UNLIKELY (field2 == NULL)) {
    GST_ERROR_OBJECT (self, "Failed to map video frame for %" GST_PTR_FORMAT,
        buffer);
    gst_video_frame_unmap_and_free (field1);
    return GST_FLOW_ERROR;
  }

  gst_deinterlace_get_buffer_state (self, field1, &buf_state,
      &interlacing_mode);

  GST_DEBUG_OBJECT (self,
      "Pushing new frame as %d fields to the history (count before %d): ptr %p at %"
      GST_TIME_FORMAT " with duration %" GST_TIME_FORMAT
      ", size %" G_GSIZE_FORMAT ", state %s, interlacing mode %s",
      fields_to_push, self->history_count, field1,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
      gst_buffer_get_size (buffer),
      STATE_TO_STRING (buf_state), MODE_TO_STRING (interlacing_mode));

  /* move up for new state */
  memmove (&self->buf_states[1], &self->buf_states[0],
      (GST_DEINTERLACE_MAX_BUFFER_STATE_HISTORY - 1) *
      sizeof (GstDeinterlaceBufferState));
  self->buf_states[0].state = buf_state;
  self->buf_states[0].timestamp = GST_BUFFER_TIMESTAMP (buffer);
  self->buf_states[0].duration = GST_BUFFER_DURATION (buffer);
  if (self->state_count < GST_DEINTERLACE_MAX_BUFFER_STATE_HISTORY)
    self->state_count++;

  for (i = GST_DEINTERLACE_MAX_FIELD_HISTORY - 1; i >= fields_to_push; i--) {
    self->field_history[i].frame =
        self->field_history[i - fields_to_push].frame;
    self->field_history[i].flags =
        self->field_history[i - fields_to_push].flags;
    self->field_history[i].tc = self->field_history[i - fields_to_push].tc;
    self->field_history[i].caption =
        self->field_history[i - fields_to_push].caption;
    self->field_history[i - fields_to_push].frame = NULL;
    self->field_history[i - fields_to_push].tc = NULL;
    self->field_history[i - fields_to_push].caption = NULL;
  }

  if (field_layout == GST_DEINTERLACE_LAYOUT_AUTO) {
    if (!GST_VIDEO_INFO_IS_INTERLACED (&self->vinfo)) {
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
    field1_flags = PICTURE_INTERLACED_TOP;
    field2_flags = PICTURE_INTERLACED_BOTTOM;
  } else {
    GST_DEBUG_OBJECT (self, "Bottom field first");
    field1_flags = PICTURE_INTERLACED_BOTTOM;
    field2_flags = PICTURE_INTERLACED_TOP;
  }

  /* Swap for reverse playback */
  if (self->segment.rate < 0) {
    field1_flags = field1_flags ^ field2_flags;
    field2_flags = field1_flags ^ field2_flags;
    field1_flags = field1_flags ^ field2_flags;
  }

  if (!onefield) {
    GstVideoTimeCodeMeta *meta = gst_buffer_get_video_time_code_meta (buffer);
    GstVideoCaptionMeta *cc_meta = gst_buffer_get_video_caption_meta (buffer);

    GST_DEBUG_OBJECT (self, "Two fields");
    self->field_history[1].frame = field1;
    self->field_history[1].flags = field1_flags;

    self->field_history[0].frame = field2;
    self->field_history[0].flags = field2_flags;

    if (meta) {
      self->field_history[0].tc = gst_video_time_code_copy (&meta->tc);
      self->field_history[0].tc->config.flags &=
          ~GST_VIDEO_TIME_CODE_FLAGS_INTERLACED;
      self->field_history[1].tc = gst_video_time_code_copy (&meta->tc);
      self->field_history[1].tc->config.flags &=
          ~GST_VIDEO_TIME_CODE_FLAGS_INTERLACED;
    }

    if (cc_meta) {
      self->field_history[0].caption = g_new (GstVideoCaptionMeta, 1);
      self->field_history[0].caption->data = g_malloc (cc_meta->size);
      self->field_history[0].caption->caption_type = cc_meta->caption_type;
      self->field_history[0].caption->size = cc_meta->size;
      memcpy (self->field_history[0].caption->data, cc_meta->data,
          cc_meta->size);
      self->field_history[1].caption = g_new (GstVideoCaptionMeta, 1);
      self->field_history[1].caption->data = g_malloc (cc_meta->size);
      self->field_history[1].caption->caption_type = cc_meta->caption_type;
      self->field_history[1].caption->size = cc_meta->size;
      memcpy (self->field_history[1].caption->data, cc_meta->data,
          cc_meta->size);
    }
  } else {                      /* onefield */
    GstVideoTimeCodeMeta *meta = gst_buffer_get_video_time_code_meta (buffer);
    GstVideoCaptionMeta *cc_meta = gst_buffer_get_video_caption_meta (buffer);

    GST_DEBUG_OBJECT (self, "One field");
    self->field_history[0].frame = field1;
    self->field_history[0].flags = field1_flags;
    if (meta) {
      self->field_history[0].tc = gst_video_time_code_copy (&meta->tc);
      self->field_history[0].tc->config.flags &=
          ~GST_VIDEO_TIME_CODE_FLAGS_INTERLACED;
    }

    if (cc_meta) {
      self->field_history[0].caption = g_new (GstVideoCaptionMeta, 1);
      self->field_history[0].caption->data = g_malloc (cc_meta->size);
      self->field_history[0].caption->caption_type = cc_meta->caption_type;
      self->field_history[0].caption->size = cc_meta->size;
      memcpy (self->field_history[0].caption->data, cc_meta->data,
          cc_meta->size);
    }
    gst_video_frame_unmap_and_free (field2);
  }

  self->history_count += fields_to_push;
  self->cur_field_idx += fields_to_push;

  GST_DEBUG_OBJECT (self, "Pushed buffer -- current history size %d, index %d",
      self->history_count, self->cur_field_idx);

  gst_buffer_replace (&self->last_buffer, buffer);

  return GST_FLOW_OK;
}

static void
gst_deinterlace_update_qos (GstDeinterlace * self, gdouble proportion,
    GstClockTimeDiff diff, GstClockTime timestamp)
{
  GST_DEBUG_OBJECT (self,
      "Updating QoS: proportion %lf, diff %" GST_STIME_FORMAT ", timestamp %"
      GST_TIME_FORMAT, proportion, GST_STIME_ARGS (diff),
      GST_TIME_ARGS (timestamp));

  GST_OBJECT_LOCK (self);
  self->proportion = proportion;
  if (G_LIKELY (timestamp != GST_CLOCK_TIME_NONE)) {
    if (G_UNLIKELY (diff > 0))
      self->earliest_time =
          timestamp + MIN (2 * diff, GST_SECOND) + ((self->fields ==
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
  self->processed = 0;
  self->dropped = 0;
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
gst_deinterlace_do_qos (GstDeinterlace * self, const GstBuffer * buffer)
{
  GstClockTime qostime, earliest_time;
  GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);
  gdouble proportion;

  /* no timestamp, can't do QoS => process frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp))) {
    GST_LOG_OBJECT (self, "invalid timestamp, can't do QoS, process frame");
    goto keep_frame;
  }

  /* get latest QoS observation values */
  gst_deinterlace_read_qos (self, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) => process frame */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (earliest_time))) {
    GST_LOG_OBJECT (self, "no observation yet, process frame");
    goto keep_frame;
  }

  /* qos is done on running time */
  qostime = gst_segment_to_running_time (&self->segment, GST_FORMAT_TIME,
      timestamp);

  /* see how our next timestamp relates to the latest qos timestamp */
  GST_LOG_OBJECT (self, "qostime %" GST_TIME_FORMAT ", earliest %"
      GST_TIME_FORMAT, GST_TIME_ARGS (qostime), GST_TIME_ARGS (earliest_time));

  if (qostime != GST_CLOCK_TIME_NONE && qostime <= earliest_time) {
    GstClockTime stream_time, jitter;
    GstMessage *qos_msg;

    GST_DEBUG_OBJECT (self, "we are late, drop frame");
    self->dropped++;
    stream_time =
        gst_segment_to_stream_time (&self->segment, GST_FORMAT_TIME, timestamp);
    jitter = GST_CLOCK_DIFF (qostime, earliest_time);
    qos_msg =
        gst_message_new_qos (GST_OBJECT (self), FALSE, qostime, stream_time,
        timestamp, GST_BUFFER_DURATION (buffer));
    gst_message_set_qos_values (qos_msg, jitter, proportion, 1000000);
    gst_message_set_qos_stats (qos_msg, GST_FORMAT_BUFFERS,
        self->processed, self->dropped);
    gst_element_post_message (GST_ELEMENT (self), qos_msg);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "process frame");
keep_frame:
  self->processed++;
  return TRUE;
}

static gboolean
gst_deinterlace_fix_timestamps (GstDeinterlace * self,
    GstVideoFrame * field1, GstVideoFrame * field2)
{
  GstVideoFrame *field3, *field4;
  GstVideoInterlaceMode interlacing_mode;

  /* FIXME: This is broken for rate < 0 */
  if (self->pattern_lock && self->pattern > -1) {
    /* accurate pattern-locked timestamp adjustment */
    if (!self->pattern_count)
      gst_deinterlace_update_pattern_timestamps (self);

    GST_BUFFER_TIMESTAMP (field1->buffer) =
        self->pattern_base_ts + self->output_count * self->pattern_buf_dur;
    GST_BUFFER_DURATION (field1->buffer) = self->pattern_buf_dur;
    self->output_count++;
  } else {
    /* naive (but low-latency) timestamp adjustment based on subsequent
     * fields/buffers */
    if (field2
        && GST_VIDEO_FRAME_PLANE_DATA (field1,
            0) != GST_VIDEO_FRAME_PLANE_DATA (field2, 0)) {
      if (GST_BUFFER_TIMESTAMP (field1->buffer) +
          GST_BUFFER_DURATION (field1->buffer) ==
          GST_BUFFER_TIMESTAMP (field2->buffer)) {
        GST_BUFFER_TIMESTAMP (field1->buffer) =
            GST_BUFFER_TIMESTAMP (field2->buffer) =
            (GST_BUFFER_TIMESTAMP (field1->buffer) +
            GST_BUFFER_TIMESTAMP (field2->buffer)) / 2;
      } else {
        GST_BUFFER_TIMESTAMP (field2->buffer) =
            GST_BUFFER_TIMESTAMP (field1->buffer);
      }
    }

    if (self->history_count < 3) {
      GST_DEBUG_OBJECT (self, "Need more fields (have %d, need 3)",
          self->history_count);
      return FALSE;
    }

    field3 = self->field_history[self->history_count - 3].frame;
    interlacing_mode = GST_VIDEO_INFO_INTERLACE_MODE (&field3->info);
    if (IS_TELECINE (interlacing_mode)) {
      if (self->history_count < 4) {
        GST_DEBUG_OBJECT (self, "Need more fields (have %d, need 4)",
            self->history_count);
        return FALSE;
      }

      field4 = self->field_history[self->history_count - 4].frame;
      if (GST_VIDEO_FRAME_PLANE_DATA (field3,
              0) != GST_VIDEO_FRAME_PLANE_DATA (field4, 0)) {
        /* telecine fields in separate buffers */
        GST_BUFFER_TIMESTAMP (field3->buffer) =
            (GST_BUFFER_TIMESTAMP (field3->buffer) +
            GST_BUFFER_TIMESTAMP (field4->buffer)) / 2;
      }
    }

    GST_BUFFER_DURATION (field1->buffer) =
        GST_BUFFER_TIMESTAMP (field3->buffer) -
        GST_BUFFER_TIMESTAMP (field1->buffer);
  }

  GST_DEBUG_OBJECT (self,
      "Field 1 adjusted to ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (field1->buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (field1->buffer)));
  return TRUE;
}

static void
gst_deinterlace_get_pattern_lock (GstDeinterlace * self, gboolean * flush_one)
{
  /* loop over all possible patterns and all possible phases
   * giving each a score. the highest score gets the lock */
  /* the score is calculated as the number of matched buffers in the
   * sequence starting at the phase offset with those from the history
   * then the longest duration pattern match is taken. if there is more than
   * one pattern matching all buffers, we take the longest pattern of those.
   * matches to complete patterns are preferred. if no non-trivial pattern is
   * matched, trivial patterns are tested. */
  gint i, j, k, score, pattern, phase;
  const gint state_count = self->state_count;
  const gint n_required = self->ignore_obscure ?
      GST_DEINTERLACE_OBSCURE_THRESHOLD :
      GST_DEINTERLACE_MAX_BUFFER_STATE_HISTORY;

  /* set unknown pattern as this is used in logic outside this function */
  self->pattern = -1;

  /* wait for more buffers */
  if (!self->have_eos && state_count < n_required) {
    GST_DEBUG_OBJECT (self, "Need more buffers in state history - %d/%d",
        state_count, n_required);
    return;
  }

  score = pattern = phase = -1;

  /* loop over all patterns */
  for (i = 0; i < G_N_ELEMENTS (telecine_patterns); i++) {
    const guint8 length = telecine_patterns[i].length;

    if (self->ignore_obscure && i >= GST_DEINTERLACE_OBSCURE_THRESHOLD)
      break;

    if (state_count < length)
      continue;

    /* loop over all phases */
    for (j = 0; j < length; j++) {
      /* low-latency mode looks at past buffers, high latency at future buffers */
      const gint state_idx =
          self->low_latency ? (self->history_count - 1) >> 1 : state_count - 1;
      /* loop over history, breaking on differing buffer states */
      for (k = 0; k < length && k < state_count; k++) {
        const guint8 hist = self->buf_states[state_idx - k].state;
        const guint8 patt = telecine_patterns[i].states[(j + k) % length];
        if (!(hist & patt))
          break;
      }

      /* make complete matches more significant */
      if (k == length)
        k += GST_DEINTERLACE_MAX_BUFFER_STATE_HISTORY;

      /* take as new best pattern if the number of matched buffers is more than
       * for other patterns */
      if (k > score) {
        score = k;
        pattern = i;
        phase = j;
      }
    }
  }

  if (pattern < 0) {
    GST_WARNING_OBJECT (self, "Failed to select a pattern");
    return;
  }

  GST_DEBUG_OBJECT (self,
      "Final pattern match result: pa %d, ph %d, l %d, s %d", pattern, phase,
      telecine_patterns[pattern].length, score);
  self->pattern = pattern;
  self->pattern_phase = phase;
  self->pattern_count = 0;
  self->output_count = 0;
  self->pattern_lock = TRUE;

  for (i = 0; i < telecine_patterns[pattern].length; i++) {
    gint state_idx =
        self->low_latency ? (self->history_count - 1) >> 1 : self->state_count -
        1;
    state_idx -= i;
    GST_LOG_OBJECT (self, "buf[%d] %s", i,
        STATE_TO_STRING (self->buf_states[state_idx].state));
  }

  /* check for the case that the first field of the pattern is an orphan */
  if (pattern > 1
      && telecine_patterns[pattern].states[phase] & (GST_ONE | GST_INT)) {
    gint i = phase, field_count = 0;
    guint8 state = telecine_patterns[pattern].states[i];

    do {
      if (state & GST_ONE) {
        field_count++;
#if 0
      } else if (!(state & GST_DRP)) {
#endif
      } else {
        field_count += 2;
      }
      i++;
      i %= telecine_patterns[pattern].length;
      state = telecine_patterns[pattern].states[i];
    } while (!(state & GST_PRG));

    /* if field_count is odd, we have an orphan field at the beginning of the
     * sequence
     * note - don't do this in low-latency mode as we are somewhere within the
     * pattern already */
    if (!self->low_latency && (*flush_one = field_count & 1)) {
      GST_DEBUG_OBJECT (self, "Orphan field detected at the beginning of the "
          "pattern - it will be deinterlaced.");
    }
  }
}

static GstFlowReturn
gst_deinterlace_output_frame (GstDeinterlace * self, gboolean flushing)
{
  GstClockTime timestamp;
  GstFlowReturn ret;
  gint fields_required;
  GstBuffer *buf, *outbuf;
  GstVideoFrame *outframe = NULL;
  GstDeinterlaceField *field1, *field2;
  GstVideoInterlaceMode interlacing_mode;
  guint8 buf_state;
  gboolean hl_no_lock;          /* indicates high latency timestamp adjustment but no pattern lock (could be ONEF or I) */
  gboolean same_buffer;         /* are field1 and field2 in the same buffer? */
  gboolean flush_one;           /* used for flushing one field when in high latency mode and not locked */
  TelecinePattern pattern;
  guint8 phase, count;
  const GstDeinterlaceLocking locking = self->locking;
  gboolean cc_added = FALSE;

  memset (&pattern, 0, sizeof (pattern));

restart:
  ret = GST_FLOW_OK;
  hl_no_lock = FALSE;
  flush_one = FALSE;
  self->need_more = FALSE;
  phase = self->pattern_phase;
  count = self->pattern_count;

  if (!self->history_count) {
    GST_DEBUG_OBJECT (self, "History is empty, waiting for more buffers!");
    goto need_more;
  }

  field1 = &self->field_history[self->history_count - 1];

  if (locking != GST_DEINTERLACE_LOCKING_NONE) {
    GstCaps *sinkcaps;

    if (!self->state_count) {
      GST_ERROR_OBJECT (self,
          "BROKEN! Fields in history + no states should not happen!");
      return GST_FLOW_ERROR;
    }

    gst_deinterlace_get_buffer_state (self, field1->frame, &buf_state,
        &interlacing_mode);

    if (self->pattern != -1)
      pattern = telecine_patterns[self->pattern];

    /* patterns 0 and 1 are interlaced, the rest are telecine */
    if (self->pattern > 1)
      interlacing_mode = GST_VIDEO_INTERLACE_MODE_MIXED;

    if (self->pattern == -1 || self->pattern_refresh
        || !(buf_state & pattern.states[(phase + count) % pattern.length])) {
      if (self->pattern == -1) {
        GST_DEBUG_OBJECT (self, "No pattern lock - refresh lock");
      } else if (self->pattern_refresh) {
        GST_DEBUG_OBJECT (self, "Pattern refresh - refresh lock");
      } else {
        GST_DEBUG_OBJECT (self, "Unexpected buffer state - refresh lock");
      }
      /* no pattern, pattern refresh set or unexpected buffer state */
      self->pattern_lock = FALSE;
      self->pattern_refresh = TRUE;

      /* refresh pattern lock */
      gst_deinterlace_get_pattern_lock (self, &flush_one);

      if (self->pattern != -1) {
        /* locked onto a valid pattern so refresh complete */
        GST_DEBUG_OBJECT (self, "Pattern locked! %s starting at %d",
            telecine_patterns[self->pattern].nick, self->pattern_phase);
        self->pattern_refresh = FALSE;
      } else if (!self->low_latency) {
        if (!self->pattern_lock) {
          goto need_more;
        } else {
          hl_no_lock = TRUE;
        }
      }

      /* setcaps on sink and src pads */
      sinkcaps = gst_pad_get_current_caps (self->sinkpad);
      if (!sinkcaps
          || !gst_deinterlace_setcaps (self, self->sinkpad, sinkcaps, FALSE)) {
        if (sinkcaps)
          gst_caps_unref (sinkcaps);
        return GST_FLOW_NOT_NEGOTIATED;
      }

      gst_caps_unref (sinkcaps);

      if (flush_one && self->drop_orphans) {
        GST_DEBUG_OBJECT (self, "Dropping orphan first field");
        self->cur_field_idx--;
        gst_deinterlace_pop_and_clear (self);
        goto restart;
      }
    }
  } else {
    gst_deinterlace_get_buffer_state (self, field1->frame, NULL,
        &interlacing_mode);
  }

  same_buffer = self->history_count >= 2
      && (GST_VIDEO_FRAME_PLANE_DATA (field1->frame, 0) ==
      GST_VIDEO_FRAME_PLANE_DATA (self->field_history[self->history_count -
              2].frame, 0));

  if ((flushing && self->history_count == 1) || (flush_one
          && !self->drop_orphans) || (hl_no_lock && (self->history_count == 1
              || !same_buffer))) {
    /* This case is for flushing a single field:
     * - flushing and 1 field in the history
     * - flush one (due to orphans in the pattern) and do not drop orphans
     * - high-latency pattern locking with no possible lock given the current
     *   state and either only one field in the history or the tip two fields
     *   are in separate buffers */
    GST_DEBUG_OBJECT (self, "Flushing one field using linear method");
    gst_deinterlace_set_method (self, GST_DEINTERLACE_LINEAR);
    fields_required = gst_deinterlace_method_get_fields_required (self->method);
  } else if (interlacing_mode == GST_VIDEO_INTERLACE_MODE_PROGRESSIVE ||
      (interlacing_mode == GST_VIDEO_INTERLACE_MODE_MIXED &&
          !GST_VIDEO_FRAME_IS_INTERLACED (field1->frame))) {
    /* This case is for processing progressive buffers, telecine or plain
     * progressive */
    GstVideoFrame *field1_frame;
    GstBuffer *field1_buffer;

    /* progressive */
    fields_required = 2;

    /* Not enough fields in the history */
    if (!flushing && self->history_count < fields_required) {
      GST_DEBUG_OBJECT (self, "Need more fields (have %d, need %d)",
          self->history_count, self->cur_field_idx + fields_required);
      goto need_more;
    }

    field2 = &self->field_history[self->history_count - 2];
    if (GST_VIDEO_FRAME_PLANE_DATA (field1->frame,
            0) != GST_VIDEO_FRAME_PLANE_DATA (field2->frame, 0)) {
      /* ERROR - next two fields in field history are not one progressive buffer - weave? */
      GST_ERROR_OBJECT (self,
          "Progressive buffer but two fields at tip aren't in the same buffer!");
    }

    if (IS_TELECINE (interlacing_mode)
        && !gst_deinterlace_fix_timestamps (self, field1->frame, field2->frame)
        && !flushing)
      goto need_more;

    GST_DEBUG_OBJECT (self,
        "Frame type: Progressive; pushing buffer as a frame");
    /* pop and push */
    gst_deinterlace_delete_meta_at (self, self->history_count - 1);
    self->cur_field_idx--;
    field1_frame = gst_deinterlace_pop_history (self);
    field1_buffer = field1_frame->buffer;
    gst_buffer_ref (field1_buffer);
    gst_video_frame_unmap_and_free (field1_frame);

    /* field2 is the same buffer as field1, but we need to remove it from the
     * history anyway */
    self->cur_field_idx--;
    gst_deinterlace_pop_and_clear (self);
    GST_DEBUG_OBJECT (self,
        "[OUT] ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT ", end %"
        GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (field1_buffer)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (field1_buffer)),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (field1_buffer) +
            GST_BUFFER_DURATION (field1_buffer)));
    if (self->discont) {
      GST_BUFFER_FLAG_SET (field1_buffer, GST_BUFFER_FLAG_DISCONT);
      self->discont = FALSE;
    }
    return gst_pad_push (self->srcpad, field1_buffer);
  } else if (IS_TELECINE (interlacing_mode)
      && GST_VIDEO_FRAME_IS_INTERLACED (field1->frame) && !same_buffer) {
    /* This case needs to identify telecine mixed buffers that require weaving
     * of two fields in different buffers.
     * - interlacing mode is mixed
     * - locked on to a telecine pattern
     * - frame is interlaced
     * - fields are in separate buffers
     * If we don't yet have a pattern lock, we will have to deinterlace as we
     * don't explicitly know we have a telecine sequence and so we drop through
     * to the plain deinterlace case */
    fields_required = 2;
    if (!flushing && self->history_count < fields_required) {
      GST_DEBUG_OBJECT (self, "Need more fields (have %d, need %d)",
          self->history_count, self->cur_field_idx + fields_required);
      goto need_more;
    }

    field2 = &self->field_history[self->history_count - 2];
    if (!gst_deinterlace_fix_timestamps (self, field1->frame, field2->frame)
        && !flushing)
      goto need_more;

    /* check field1 and field2 buffer caps and flags are corresponding */
    if (field1->flags == field2->flags) {
      /* ERROR - fields are of same parity - what should be done here?
       * perhaps deinterlace the tip field and start again? */
      GST_ERROR_OBJECT (self, "Telecine mixed with fields of same parity!");
    }
    GST_DEBUG_OBJECT (self,
        "Frame type: Telecine Mixed; weaving tip two fields into a frame");
    /* set method to WEAVE */
    gst_deinterlace_set_method (self, GST_DEINTERLACE_WEAVE);
  } else {
    /* This is the final catch-all case that applies the selected deinterlacing
     * method. At this point the fields to be processed are either definitely
     * interlaced or we do not yet know that we have a telecine pattern lock
     * and so the best we can do is to deinterlace the fields. */
    gst_deinterlace_set_method (self, self->user_set_method_id);
    fields_required = gst_deinterlace_method_get_fields_required (self->method);
    if (flushing && self->history_count < fields_required) {
      /* note: we already checked for flushing with history count == 1 above
       * so we must have 2 or more fields in here */
      gst_deinterlace_set_method (self, GST_DEINTERLACE_VFIR);
      fields_required =
          gst_deinterlace_method_get_fields_required (self->method);
      GST_DEBUG_OBJECT (self, "Flushing field(s) using %s method",
          methods_types[self->method_id].value_nick);
    }

    /* Not enough fields in the history */
    if (!flushing && self->history_count < fields_required) {
      GST_DEBUG_OBJECT (self, "Need more fields (have %d, need %d)",
          self->history_count, self->cur_field_idx + fields_required);
      goto need_more;
    }

    GST_DEBUG_OBJECT (self,
        "Frame type: Interlaced; deinterlacing using %s method",
        methods_types[self->method_id].value_nick);
  }

  if (!flushing && self->cur_field_idx < 1) {
    goto need_more;
  } else if (self->cur_field_idx < 0 && flushing) {
    self->cur_field_idx++;
  }

  if (self->fields == GST_DEINTERLACE_ALL || IS_TELECINE (interlacing_mode))
    GST_DEBUG_OBJECT (self, "All fields");
  else if (self->fields == GST_DEINTERLACE_TF)
    GST_DEBUG_OBJECT (self, "Top fields");
  else if (self->fields == GST_DEINTERLACE_BF)
    GST_DEBUG_OBJECT (self, "Bottom fields");

  if ((self->field_history[self->cur_field_idx].flags == PICTURE_INTERLACED_TOP
          && (self->fields == GST_DEINTERLACE_TF
              || IS_TELECINE (interlacing_mode)))
      || (self->fields == GST_DEINTERLACE_ALL
          && !IS_TELECINE (interlacing_mode))) {
    gint index;

    GST_DEBUG_OBJECT (self, "deinterlacing top field");

    /* create new buffer */
    ret = gst_buffer_pool_acquire_buffer (self->pool, &outbuf, NULL);
    if (ret != GST_FLOW_OK)
      goto no_buffer;

    g_return_val_if_fail (self->history_count >=
        1 + gst_deinterlace_method_get_latency (self->method), GST_FLOW_ERROR);

    index =
        self->history_count - 1 -
        gst_deinterlace_method_get_latency (self->method);
    buf = self->field_history[index].frame->buffer;

    if (self->field_history[index].tc) {
      gst_buffer_add_video_time_code_meta (outbuf,
          self->field_history[index].tc);
    }
    if (self->field_history[index].caption) {
      g_assert (self->field_history[index].caption->data != NULL);
      g_assert (!cc_added);
      gst_buffer_add_video_caption_meta (outbuf,
          self->field_history[index].caption->caption_type,
          self->field_history[index].caption->data,
          self->field_history[index].caption->size);
      cc_added = TRUE;
    }
    if (IS_TELECINE (interlacing_mode) && !self->telecine_tc_warned) {
      self->telecine_tc_warned = TRUE;
      GST_FIXME_OBJECT (self,
          "Detected telecine timecodes when deinterlacing. This is not "
          "supported yet. Resulting timecode may be wrong");
    }
    if (self->fields == GST_DEINTERLACE_ALL) {
      GstVideoTimeCodeMeta *meta = gst_buffer_get_video_time_code_meta (outbuf);
      if (meta) {
        meta->tc.config.fps_n = 2 * meta->tc.config.fps_n;
        meta->tc.frames = 2 * meta->tc.frames;
      }
    }
    if (!IS_TELECINE (interlacing_mode)) {
      timestamp = GST_BUFFER_TIMESTAMP (buf);

      if (self->fields == GST_DEINTERLACE_ALL) {
        if (self->segment.rate < 0)
          GST_BUFFER_TIMESTAMP (outbuf) = timestamp + self->field_duration;
        else
          GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
        GST_BUFFER_DURATION (outbuf) = self->field_duration;
      } else {
        GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
        GST_BUFFER_DURATION (outbuf) = 2 * self->field_duration;
      }
      GST_DEBUG_OBJECT (self,
          "[ADJUST] ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT ", end %"
          GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf) +
              GST_BUFFER_DURATION (outbuf)));
    } else {
      GST_BUFFER_TIMESTAMP (outbuf) =
          GST_BUFFER_TIMESTAMP (field1->frame->buffer);
      GST_BUFFER_DURATION (outbuf) =
          GST_BUFFER_DURATION (field1->frame->buffer);
    }

    /* Check if we need to drop the frame because of QoS */
    if (!gst_deinterlace_do_qos (self, buf)) {
      self->cur_field_idx--;
      gst_deinterlace_pop_and_clear (self);
      gst_buffer_unref (outbuf);
      outbuf = NULL;
      ret = GST_FLOW_OK;
    } else {
      if (self->cur_field_idx < 0 && flushing) {
        if (self->history_count == 1) {
          gst_deinterlace_pop_and_clear (self);
          goto need_more;
        }
        self->cur_field_idx++;
      }
      if (self->cur_field_idx < 0) {
        goto need_more;
      }
      if (!flushing && self->cur_field_idx < 1) {
        goto need_more;
      }

      /* map the frame so the deinterlace methods can write the data to the
       * correct memory locations */
      outframe =
          gst_video_frame_new_and_map (&self->vinfo_out, outbuf, GST_MAP_WRITE);

      /* do magic calculus */
      gst_deinterlace_method_deinterlace_frame (self->method,
          self->field_history, self->history_count, outframe,
          self->cur_field_idx);

      gst_video_frame_unmap_and_free (outframe);

      self->cur_field_idx--;
      /* need to remove the field in the telecine weaving case */
      if ((IS_TELECINE (interlacing_mode)
              && self->method_id == GST_DEINTERLACE_WEAVE)
          || self->cur_field_idx + 1 +
          gst_deinterlace_method_get_latency (self->method) <
          self->history_count || flushing) {
        gst_deinterlace_pop_and_clear (self);
      }

      if (gst_deinterlace_clip_buffer (self, outbuf)) {
        GST_DEBUG_OBJECT (self,
            "[OUT] ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT ", end %"
            GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
            GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf) +
                GST_BUFFER_DURATION (outbuf)));
        if (self->discont) {
          GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
          self->discont = FALSE;
        }
        ret = gst_pad_push (self->srcpad, outbuf);
      } else {
        ret = GST_FLOW_OK;
        gst_buffer_unref (outbuf);
      }

      outbuf = NULL;
      if (ret != GST_FLOW_OK)
        return ret;
      if (IS_TELECINE (interlacing_mode)
          && self->method_id == GST_DEINTERLACE_WEAVE) {
        /* pop off the second field */
        GST_DEBUG_OBJECT (self, "Removing unused field (count: %d)",
            self->history_count);
        self->cur_field_idx--;
        gst_deinterlace_pop_and_clear (self);
        interlacing_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
        return ret;
      }
    }

    if (flush_one && !self->drop_orphans) {
      GST_DEBUG_OBJECT (self, "Orphan field deinterlaced - reconfiguring");
      goto restart;
    }
  }
  /* no calculation done: remove excess field */
  else if (self->field_history[self->cur_field_idx].flags ==
      PICTURE_INTERLACED_TOP && (self->fields == GST_DEINTERLACE_BF
          && !IS_TELECINE (interlacing_mode))) {
    GST_DEBUG_OBJECT (self, "Removing unused top field");
    self->cur_field_idx--;
    gst_deinterlace_pop_and_clear (self);

    if (flush_one && !self->drop_orphans) {
      GST_DEBUG_OBJECT (self, "Orphan field deinterlaced - reconfiguring");
      goto restart;
    }
  }

  if (self->history_count < fields_required)
    return ret;

  if (self->cur_field_idx < 0)
    return ret;

  /* deinterlace bottom_field */
  if ((self->field_history[self->cur_field_idx].flags ==
          PICTURE_INTERLACED_BOTTOM && (self->fields == GST_DEINTERLACE_BF
              || IS_TELECINE (interlacing_mode)))
      || (self->fields == GST_DEINTERLACE_ALL
          && !IS_TELECINE (interlacing_mode))) {
    gint index;

    GST_DEBUG_OBJECT (self, "deinterlacing bottom field");

    /* create new buffer */
    ret = gst_buffer_pool_acquire_buffer (self->pool, &outbuf, NULL);
    if (ret != GST_FLOW_OK)
      goto no_buffer;

    g_return_val_if_fail (self->history_count >=
        gst_deinterlace_method_get_latency (self->method) + 1, GST_FLOW_ERROR);

    index =
        self->history_count - 1 -
        gst_deinterlace_method_get_latency (self->method);
    buf = self->field_history[index].frame->buffer;

    if (self->field_history[index].tc) {
      gst_buffer_add_video_time_code_meta (outbuf,
          self->field_history[index].tc);
    }
    if (self->field_history[index].caption && !cc_added) {
      g_assert (self->field_history[index].caption->data != NULL);
      gst_buffer_add_video_caption_meta (outbuf,
          self->field_history[index].caption->caption_type,
          self->field_history[index].caption->data,
          self->field_history[index].caption->size);
      cc_added = TRUE;
    }
    if (IS_TELECINE (interlacing_mode) && !self->telecine_tc_warned) {
      self->telecine_tc_warned = TRUE;
      GST_FIXME_OBJECT (self,
          "Detected telecine timecodes when deinterlacing. This is not "
          "supported yet. Resulting timecode may be wrong");
    }
    if (self->fields == GST_DEINTERLACE_ALL) {
      GstVideoTimeCodeMeta *meta = gst_buffer_get_video_time_code_meta (outbuf);
      if (meta) {
        meta->tc.config.fps_n = 2 * meta->tc.config.fps_n;
        meta->tc.frames = 2 * meta->tc.frames + 1;
      }
    }
    if (!IS_TELECINE (interlacing_mode)) {
      timestamp = GST_BUFFER_TIMESTAMP (buf);

      if (self->fields == GST_DEINTERLACE_ALL) {
        if (self->segment.rate < 0)
          GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
        else
          GST_BUFFER_TIMESTAMP (outbuf) = timestamp + self->field_duration;
        GST_BUFFER_DURATION (outbuf) = self->field_duration;
      } else {
        GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
        GST_BUFFER_DURATION (outbuf) = 2 * self->field_duration;
      }
      GST_DEBUG_OBJECT (self,
          "[ADJUST] ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT ", end %"
          GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf) +
              GST_BUFFER_DURATION (outbuf)));
    } else {
      GST_BUFFER_TIMESTAMP (outbuf) =
          GST_BUFFER_TIMESTAMP (field1->frame->buffer);
      GST_BUFFER_DURATION (outbuf) =
          GST_BUFFER_DURATION (field1->frame->buffer);
    }

    /* Check if we need to drop the frame because of QoS */
    if (!gst_deinterlace_do_qos (self, buf)) {
      self->cur_field_idx--;
      gst_deinterlace_pop_and_clear (self);
      gst_buffer_unref (outbuf);
      outbuf = NULL;
      ret = GST_FLOW_OK;
    } else {
      /* map the frame so the deinterlace methods can write the data to the
       * correct memory locations */
      outframe =
          gst_video_frame_new_and_map (&self->vinfo_out, outbuf, GST_MAP_WRITE);

      /* do magic calculus */
      gst_deinterlace_method_deinterlace_frame (self->method,
          self->field_history, self->history_count, outframe,
          self->cur_field_idx);

      gst_video_frame_unmap_and_free (outframe);

      self->cur_field_idx--;
      /* need to remove the field in the telecine weaving case */
      if ((IS_TELECINE (interlacing_mode)
              && self->method_id == GST_DEINTERLACE_WEAVE)
          || self->cur_field_idx + 1 +
          gst_deinterlace_method_get_latency (self->method) <
          self->history_count) {
        gst_deinterlace_pop_and_clear (self);
      }

      if (gst_deinterlace_clip_buffer (self, outbuf)) {
        GST_DEBUG_OBJECT (self,
            "[OUT] ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT ", end %"
            GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
            GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf) +
                GST_BUFFER_DURATION (outbuf)));
        ret = gst_pad_push (self->srcpad, outbuf);
      } else {
        ret = GST_FLOW_OK;
        gst_buffer_unref (outbuf);
      }

      outbuf = NULL;
      if (ret != GST_FLOW_OK)
        return ret;
      if (IS_TELECINE (interlacing_mode)
          && self->method_id == GST_DEINTERLACE_WEAVE) {
        /* pop off the second field */
        GST_DEBUG_OBJECT (self, "Removing unused field (count: %d)",
            self->history_count);
        self->cur_field_idx--;
        gst_deinterlace_pop_and_clear (self);
        interlacing_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
        return ret;
      }
    }

    if (flush_one && !self->drop_orphans) {
      GST_DEBUG_OBJECT (self, "Orphan field deinterlaced - reconfiguring");
      goto restart;
    }
  }
  /* no calculation done: remove excess field */
  else if (self->field_history[self->cur_field_idx].flags ==
      PICTURE_INTERLACED_BOTTOM && (self->fields == GST_DEINTERLACE_TF
          && !IS_TELECINE (interlacing_mode))) {
    GST_DEBUG_OBJECT (self, "Removing unused bottom field");
    self->cur_field_idx--;
    gst_deinterlace_pop_and_clear (self);

    if (flush_one && !self->drop_orphans) {
      GST_DEBUG_OBJECT (self, "Orphan field deinterlaced - reconfiguring");
      goto restart;
    }
  }

  return ret;

need_more:
  {
    self->need_more = TRUE;
    return ret;
  }
no_buffer:
  {
    GST_DEBUG_OBJECT (self, "could not allocate buffer");
    return ret;
  }
}

static gboolean
gst_deinterlace_get_latency (GstDeinterlace * self)
{
  if (self->locking == GST_DEINTERLACE_LOCKING_AUTO) {
    GstQuery *query;

    query = gst_query_new_latency ();
    if ((gst_pad_peer_query (self->sinkpad, query))) {
      gboolean is_live;
      /* if upstream is live, we use low-latency passive locking mode
       * else high-latency active locking mode */
      gst_query_parse_latency (query, &is_live, NULL, NULL);
      GST_DEBUG_OBJECT (self, "Latency query indicates stream is %s",
          is_live ? "live - using passive locking" :
          "not live - using active locking");
      gst_query_unref (query);
      return is_live;
    } else {
      /* conservatively use passive locking if the query fails */
      GST_WARNING_OBJECT (self,
          "Latency query failed - fall back to using passive locking");
      gst_query_unref (query);
      return TRUE;
    }
  } else {
    return self->locking - 2;
  }
}

static GstFlowReturn
gst_deinterlace_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstDeinterlace *self = GST_DEINTERLACE (parent);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_OBJECT_LOCK (self);
  if (self->reconfigure || gst_pad_check_reconfigure (self->srcpad)) {
    GstCaps *caps;
    gboolean force_reconfigure = FALSE, res;

    if ((gint) self->new_fields != -1) {
      force_reconfigure |= (self->user_set_fields != self->new_fields);
      self->user_set_fields = self->new_fields;
    }
    if ((gint) self->new_mode != -1) {
      force_reconfigure |= (self->mode != self->new_mode);
      self->mode = self->new_mode;
    }
    self->new_mode = -1;
    self->new_fields = -1;

    self->reconfigure = FALSE;
    GST_OBJECT_UNLOCK (self);

    caps = gst_pad_get_current_caps (self->sinkpad);
    res = (caps != NULL);

    if (res) {
      res = gst_deinterlace_setcaps (self, self->sinkpad, caps,
          force_reconfigure);
      gst_caps_unref (caps);
    }

    if (!res) {
      gst_pad_mark_reconfigure (self->srcpad);
      if (GST_PAD_IS_FLUSHING (self->srcpad))
        ret = GST_FLOW_FLUSHING;
      else
        ret = GST_FLOW_NOT_NEGOTIATED;
      goto out_unref;
    }
  } else {
    GST_OBJECT_UNLOCK (self);
  }

  GST_DEBUG_OBJECT (self,
      "[IN] ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT ", end %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf)));

  if (self->still_frame_mode || self->passthrough) {
    GST_DEBUG_OBJECT (self,
        "Frame type: Progressive?; pushing buffer using pass-through");
    GST_DEBUG_OBJECT (self,
        "[OUT] ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT ", end %"
        GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf)));

    return gst_pad_push (self->srcpad, buf);
  }

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT)) {
    GST_DEBUG_OBJECT (self, "DISCONT buffer, resetting history");
    gst_deinterlace_reset_history (self, FALSE);
    self->discont = TRUE;
  }

  ret = gst_deinterlace_push_history (self, buf);
  if (ret != GST_FLOW_OK)
    goto out_unref;

  do {
    ret = gst_deinterlace_output_frame (self, FALSE);
  } while (!self->need_more && self->history_count > 0 && ret == GST_FLOW_OK);

out_unref:
  gst_buffer_unref (buf);
  return ret;
}

static gboolean
gst_deinterlace_acceptcaps (GstDeinterlace * self, GstPad * pad, GstCaps * caps)
{
  gboolean ret;
  GstCaps *ourcaps;

  /* In AUTO/DISABLED mode we accept everything that is compatible with
   * our template caps. In INTERLACED mode we force deinterlacing, meaning
   * we can only possibly support the deinterlace caps.
   * In AUTO_STRICT mode we accept all progressive formats, but only those
   * interlaced format that we can actually deinterlace */
  if (self->mode == GST_DEINTERLACE_MODE_DISABLED
      || self->mode == GST_DEINTERLACE_MODE_AUTO) {
    ourcaps = gst_pad_get_pad_template_caps (pad);
    ret = gst_caps_is_subset (caps, ourcaps);
    gst_caps_unref (ourcaps);
  } else if (self->mode == GST_DEINTERLACE_MODE_INTERLACED) {
    ourcaps = gst_static_caps_get (&deinterlace_caps);
    ret = gst_caps_is_subset (caps, ourcaps);
    gst_caps_unref (ourcaps);
  } else if (self->mode == GST_DEINTERLACE_MODE_AUTO_STRICT) {
    ourcaps = gst_static_caps_get (&progressive_caps);
    ret = gst_caps_is_subset (caps, ourcaps);
    gst_caps_unref (ourcaps);

    if (!ret) {
      ourcaps = gst_static_caps_get (&deinterlace_caps);
      ret = gst_caps_is_subset (caps, ourcaps);
      gst_caps_unref (ourcaps);
    }
  } else {
    g_assert_not_reached ();
  }

  GST_DEBUG_OBJECT (pad, "accept-caps result:%d for caps %" GST_PTR_FORMAT,
      ret, caps);

  return ret;
}

static gboolean
gst_deinterlace_fraction_double (gint * n_out, gint * d_out, gboolean half)
{
  gint n, d, gcd;

  n = *n_out;
  d = *d_out;

  if (d == 0)
    return FALSE;

  if (n == 0)
    return TRUE;

  gcd = gst_util_greatest_common_divisor (n, d);
  n /= gcd;
  d /= gcd;

  if (half) {
    if (G_MAXINT / 2 >= ABS (d)) {
      d *= 2;
    } else if (n >= 2 && n != G_MAXINT) {
      n /= 2;
    } else {
      d = G_MAXINT;
    }
  } else {
    if (G_MAXINT / 2 >= ABS (n)) {
      n *= 2;
    } else if (d >= 2 && d != G_MAXINT) {
      d /= 2;
    } else {
      n = G_MAXINT;
    }
  }

  *n_out = n;
  *d_out = d;

  return TRUE;
}

static GstCaps *
gst_deinterlace_caps_double_framerate (GstCaps * caps, gboolean half)
{
  guint len;

  for (len = gst_caps_get_size (caps); len > 0; len--) {
    GstStructure *s = gst_caps_get_structure (caps, len - 1);
    const GValue *val;

    val = gst_structure_get_value (s, "framerate");
    if (!val)
      continue;

    if (G_VALUE_TYPE (val) == GST_TYPE_FRACTION) {
      gint n, d;

      n = gst_value_get_fraction_numerator (val);
      d = gst_value_get_fraction_denominator (val);

      if (!gst_deinterlace_fraction_double (&n, &d, half)) {
        gst_caps_remove_structure (caps, len - 1);
        continue;
      }

      gst_structure_set (s, "framerate", GST_TYPE_FRACTION, n, d, NULL);
    } else if (G_VALUE_TYPE (val) == GST_TYPE_FRACTION_RANGE) {
      const GValue *min, *max;
      GValue nrange = { 0, }, nmin = {
        0,
      }, nmax = {
        0,
      };
      gint n, d;

      g_value_init (&nrange, GST_TYPE_FRACTION_RANGE);
      g_value_init (&nmin, GST_TYPE_FRACTION);
      g_value_init (&nmax, GST_TYPE_FRACTION);

      min = gst_value_get_fraction_range_min (val);
      max = gst_value_get_fraction_range_max (val);

      n = gst_value_get_fraction_numerator (min);
      d = gst_value_get_fraction_denominator (min);

      if (!gst_deinterlace_fraction_double (&n, &d, half)) {
        g_value_unset (&nrange);
        g_value_unset (&nmax);
        g_value_unset (&nmin);
        gst_caps_remove_structure (caps, len - 1);
        continue;
      }

      gst_value_set_fraction (&nmin, n, d);

      n = gst_value_get_fraction_numerator (max);
      d = gst_value_get_fraction_denominator (max);

      if (!gst_deinterlace_fraction_double (&n, &d, half)) {
        g_value_unset (&nrange);
        g_value_unset (&nmax);
        g_value_unset (&nmin);
        gst_caps_remove_structure (caps, len - 1);
        continue;
      }

      gst_value_set_fraction (&nmax, n, d);
      gst_value_set_fraction_range (&nrange, &nmin, &nmax);

      gst_structure_take_value (s, "framerate", &nrange);

      g_value_unset (&nmin);
      g_value_unset (&nmax);
    } else if (G_VALUE_TYPE (val) == GST_TYPE_LIST) {
      const GValue *lval;
      GValue nlist = { 0, };
      GValue nval = { 0, };
      gint i;

      g_value_init (&nlist, GST_TYPE_LIST);
      for (i = gst_value_list_get_size (val); i > 0; i--) {
        gint n, d;

        lval = gst_value_list_get_value (val, i - 1);

        if (G_VALUE_TYPE (lval) != GST_TYPE_FRACTION)
          continue;

        n = gst_value_get_fraction_numerator (lval);
        d = gst_value_get_fraction_denominator (lval);

        /* Double/Half the framerate but if this fails simply
         * skip this value from the list */
        if (!gst_deinterlace_fraction_double (&n, &d, half)) {
          continue;
        }

        g_value_init (&nval, GST_TYPE_FRACTION);

        gst_value_set_fraction (&nval, n, d);
        gst_value_list_append_and_take_value (&nlist, &nval);
      }
      gst_structure_take_value (s, "framerate", &nlist);
    }
  }

  return caps;
}

static GstCaps *
dup_caps_with_alternate (GstCaps * caps)
{
  GstCaps *with_alternate;
  GstCapsFeatures *features;

  with_alternate = gst_caps_copy (caps);
  features =
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_FORMAT_INTERLACED,
      NULL);
  gst_caps_set_features_simple (with_alternate, features);

  gst_caps_set_simple (with_alternate, "interlace-mode", G_TYPE_STRING,
      "alternate", NULL);

  return with_alternate;
}

static GstCaps *
gst_deinterlace_getcaps (GstDeinterlace * self, GstPad * pad, GstCaps * filter)
{
  GstCaps *ret, *caps;
  GstPad *otherpad;
  gint len;
  GstCaps *ourcaps;
  GstCaps *peercaps;
  GstCaps *tmp, *tmp2;

  otherpad = (pad == self->srcpad) ? self->sinkpad : self->srcpad;

  ourcaps = gst_pad_get_pad_template_caps (pad);
  peercaps = gst_pad_peer_query_caps (otherpad, NULL);

  /* Filter any peercaps that are available with our template
   * to get started with the subset of caps we actually support */
  if (peercaps) {
    GST_DEBUG_OBJECT (pad, "Peer has caps %" GST_PTR_FORMAT, peercaps);
    caps = gst_caps_make_writable (gst_caps_intersect (ourcaps, peercaps));
    gst_caps_unref (peercaps);
    gst_caps_unref (ourcaps);
    peercaps = ourcaps = NULL;
  } else {
    caps = gst_caps_make_writable (ourcaps);
    ourcaps = NULL;
  }

  GST_DEBUG_OBJECT (pad,
      "Transforming caps %" GST_PTR_FORMAT " with filter %" GST_PTR_FORMAT,
      caps, filter);

  /* If deinterlacing is disabled, we just passthrough the
   * caps and everything */
  if (self->mode == GST_DEINTERLACE_MODE_DISABLED) {
    ret = caps;
    caps = NULL;
    goto done;
  }

  /* If deinterlacing is enforced, we can only accept the
   * caps for which we can actually do deinterlacing */
  if (self->mode == GST_DEINTERLACE_MODE_INTERLACED) {
    tmp = gst_static_caps_get (&deinterlace_caps);
    ret = gst_caps_intersect_full (caps, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
    tmp = NULL;
    gst_caps_unref (caps);
    caps = NULL;
    goto done;
  }

  g_assert (self->mode == GST_DEINTERLACE_MODE_AUTO
      || self->mode == GST_DEINTERLACE_MODE_AUTO_STRICT);

  /* For the auto mode we have to do a bit more than that */
  ret = gst_caps_new_empty ();

  /* We can accept any structure if
   * - they are progressive already
   *
   */
  tmp = gst_static_caps_get (&progressive_caps);
  tmp2 = gst_caps_intersect_full (caps, tmp, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (tmp);
  tmp = NULL;
  ret = gst_caps_merge (ret, tmp2);
  tmp2 = NULL;

  /* or
   * - they have sysmem caps features and a format for which we support
   *   deinterlacing
   * or
   * - they have ANY caps features, in which case we support it for
   *   sysmem caps features for formats we support
   *
   * NOTE: These are the caps where we actually would do deinterlacing
   * ourselves. If fields == ALL we would double the framerate so would
   * have to half the framerate constraints from downstream here
   */
  tmp = gst_static_caps_get (&deinterlace_caps);
  tmp2 = gst_caps_intersect_full (caps, tmp, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (tmp);
  tmp = NULL;

  for (len = gst_caps_get_size (tmp2); len > 0; len--) {
    GstStructure *s = gst_caps_get_structure (tmp2, len - 1);

    /* Drop fields which can be converted by us.
     * Specifically "field-order" here.
     * "field-order" with "progressive" and/or
     * unspecified "interlace-mode" would cause negotiation issue */
    gst_structure_remove_field (s, "field-order");

    if (pad == self->sinkpad) {
      gst_structure_remove_field (s, "interlace-mode");
    } else {
      gst_structure_set (s, "interlace-mode", G_TYPE_STRING, "progressive",
          NULL);
    }
  }

  if (self->user_set_fields == GST_DEINTERLACE_ALL) {
    tmp2 = gst_deinterlace_caps_double_framerate (tmp2, (pad == self->sinkpad));
  }
  if (self->user_set_fields == GST_DEINTERLACE_FIELDS_AUTO) {
    tmp = gst_caps_copy (tmp2);
    tmp = gst_deinterlace_caps_double_framerate (tmp, (pad == self->sinkpad));
  }

  ret = gst_caps_merge (ret, tmp2);
  tmp2 = NULL;
  if (tmp != NULL) {
    ret = gst_caps_merge (ret, tmp);
    tmp = NULL;
  }

  /* or
   * - anything else in which case we would just passthrough again if we're
   *   only in AUTO and not AUTO_STRICT mode
   */
  if (self->mode == GST_DEINTERLACE_MODE_AUTO)
    ret = gst_caps_merge (ret, gst_caps_copy (caps));

  gst_caps_unref (caps);
  caps = NULL;

  if (pad == self->sinkpad) {
    GstCaps *can_deinterlace;

    tmp = gst_static_caps_get (&deinterlace_caps);
    can_deinterlace = gst_caps_intersect (ret, tmp);
    gst_caps_unref (tmp);

    ret = gst_caps_merge (ret, dup_caps_with_alternate (can_deinterlace));
    gst_caps_unref (can_deinterlace);
  }

done:

  if (filter) {
    GstCaps *tmp;

    GST_LOG_OBJECT (pad, "intersecting with %" GST_PTR_FORMAT, filter);
    tmp = gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = tmp;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps %" GST_PTR_FORMAT, ret);

  return ret;
}

/* takes ownership of the pool, allocator and query */
static gboolean
gst_deinterlace_set_allocation (GstDeinterlace * self,
    GstBufferPool * pool, GstAllocator * allocator,
    GstAllocationParams * params)
{
  GstAllocator *oldalloc;
  GstBufferPool *oldpool;

  GST_OBJECT_LOCK (self);
  oldpool = self->pool;
  self->pool = pool;

  oldalloc = self->allocator;
  self->allocator = allocator;

  if (params)
    self->params = *params;
  else
    gst_allocation_params_init (&self->params);
  GST_OBJECT_UNLOCK (self);

  if (oldpool) {
    GST_DEBUG_OBJECT (self, "deactivating old pool %p", oldpool);
    gst_buffer_pool_set_active (oldpool, FALSE);
    gst_object_unref (oldpool);
  }
  if (oldalloc) {
    gst_object_unref (oldalloc);
  }
  if (pool) {
    GST_DEBUG_OBJECT (self, "activating new pool %p", pool);
    gst_buffer_pool_set_active (pool, TRUE);
  }
  return TRUE;
}

static gboolean
gst_deinterlace_do_bufferpool (GstDeinterlace * self, GstCaps * outcaps)
{
  GstQuery *query;
  gboolean result = TRUE;
  GstBufferPool *pool;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstStructure *config;
  guint size, min, max;

  if (self->passthrough) {
    /* we are in passthrough, the input buffer is never copied and always passed
     * along. We never allocate an output buffer on the srcpad. What we do is
     * let the upstream element decide if it wants to use a bufferpool and
     * then we will proxy the downstream pool */
    GST_DEBUG_OBJECT (self, "we're passthough, delay bufferpool");
    gst_deinterlace_set_allocation (self, NULL, NULL, NULL);
    return TRUE;
  }

  /* not passthrough, we need to allocate */
  /* find a pool for the negotiated caps now */
  GST_DEBUG_OBJECT (self, "doing allocation query");
  query = gst_query_new_allocation (outcaps, TRUE);
  if (!gst_pad_peer_query (self->srcpad, query)) {
    /* not a problem, just debug a little */
    GST_DEBUG_OBJECT (self, "peer ALLOCATION query failed");
  }

  GST_DEBUG_OBJECT (self, "ALLOCATION (%d) params: %" GST_PTR_FORMAT, result,
      query);

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
  } else {
    allocator = NULL;
    gst_allocation_params_init (&params);
  }

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
  else {
    GstVideoInfo out_info;

    gst_video_info_from_caps (&out_info, outcaps);

    pool = NULL;
    size = GST_VIDEO_INFO_SIZE (&out_info);
    min =
        MAX ((gst_deinterlace_method_get_fields_required (self->method) +
            1) / 2 + 1, 4);
    max = 0;
  }

  if (pool == NULL) {
    /* no pool, we can make our own */
    GST_DEBUG_OBJECT (self, "no pool, making new pool");
    pool = gst_video_buffer_pool_new ();
  }

  /* now configure */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (pool, config);

  /* now store */
  result = gst_deinterlace_set_allocation (self, pool, allocator, &params);

  gst_query_unref (query);

  return result;
}


static gboolean
gst_deinterlace_setcaps (GstDeinterlace * self, GstPad * pad, GstCaps * caps,
    gboolean force)
{
  GstCaps *srccaps = NULL, *caps_no_feat = NULL;
  GstVideoInterlaceMode interlacing_mode;
  gint fps_n, fps_d;
  GstCaps *peercaps, *current_caps;

  gst_pad_check_reconfigure (self->srcpad);

  /* If the force flag is set, always re-check the downstream caps,
   * and reconfigure as the deinterlace mode has changed */
  if (!force && (current_caps = gst_pad_get_current_caps (pad))) {
    if (gst_caps_is_equal (caps, current_caps)) {
      GST_DEBUG_OBJECT (pad, "Got same caps again, returning");
      gst_caps_unref (current_caps);
      return TRUE;
    }
    gst_deinterlace_reset_history (self, FALSE);
    gst_caps_unref (current_caps);
  }
  peercaps = gst_pad_peer_query_caps (self->srcpad, NULL);

  /* Make sure the peer caps are compatible with the template caps */
  if (peercaps) {
    GstCaps *tmp = gst_pad_get_pad_template_caps (self->srcpad);
    GstCaps *tmp2 = gst_caps_intersect (peercaps, tmp);

    gst_caps_unref (peercaps);
    peercaps = NULL;
    gst_caps_unref (tmp);

    if (gst_caps_is_empty (tmp2)) {
      gst_caps_unref (tmp2);
      GST_ERROR_OBJECT (self, "Peer caps not compatible with template caps");
      goto invalid_caps;
    }
    peercaps = tmp2;
  }

  if (self->locking != GST_DEINTERLACE_LOCKING_NONE) {
    if (self->low_latency == -1)
      self->low_latency = gst_deinterlace_get_latency (self);

    if (self->pattern_lock) {
      /* refresh has been successful - we have a lock now */
      self->pattern_refresh = FALSE;
    } else {
      /* if we were not refreshing (!pattern_refresh) the caps have changed
       * so we need to refresh and we don't have a lock anymore
       * otherwise we have pattern_fresh and !pattern_lock anyway */
      self->pattern_refresh = TRUE;
      self->pattern_lock = FALSE;
    }
  }

  if (!gst_video_info_from_caps (&self->vinfo, caps))
    goto invalid_caps;

  gst_video_info_set_interlaced_format (&self->vinfo_out,
      GST_VIDEO_INFO_FORMAT (&self->vinfo),
      GST_VIDEO_INTERLACE_MODE_PROGRESSIVE,
      GST_VIDEO_INFO_WIDTH (&self->vinfo),
      GST_VIDEO_INFO_HEIGHT (&self->vinfo));

  if (GST_VIDEO_INFO_INTERLACE_MODE (&self->vinfo) ==
      GST_VIDEO_INTERLACE_MODE_ALTERNATE) {
    /* alternate interlace mode uses a caps feature, remove it when interesecting caps
     * and setting the src pad caps. */
    GstCapsFeatures *features;

    caps_no_feat = gst_caps_copy (caps);

    features = gst_caps_get_features (caps_no_feat, 0);
    gst_caps_features_remove (features, GST_CAPS_FEATURE_FORMAT_INTERLACED);
  } else {
    caps_no_feat = gst_caps_ref (caps);
  }

  fps_n = GST_VIDEO_INFO_FPS_N (&self->vinfo);
  fps_d = GST_VIDEO_INFO_FPS_D (&self->vinfo);

  /* Update passthrough information */
  if (self->mode == GST_DEINTERLACE_MODE_DISABLED) {
    self->passthrough = TRUE;
    GST_DEBUG_OBJECT (self, "Passthrough because mode=disabled");
  } else if (self->mode == GST_DEINTERLACE_MODE_INTERLACED) {
    GstCaps *tmp = gst_static_caps_get (&deinterlace_caps);

    if (!gst_caps_can_intersect (caps_no_feat, tmp)) {
      gst_caps_unref (tmp);
      GST_ERROR_OBJECT (self, "Unsupported caps for mode=interlaced");
      goto invalid_caps;
    }

    self->passthrough = FALSE;
    GST_DEBUG_OBJECT (self, "Not passthrough because mode=interlaced");
  } else if (self->mode == GST_DEINTERLACE_MODE_AUTO
      || self->mode == GST_DEINTERLACE_MODE_AUTO_STRICT) {
    GstCaps *tmp = gst_static_caps_get (&deinterlace_caps);

    /* Already progressive? Passthrough */
    if (!GST_VIDEO_INFO_IS_INTERLACED (&self->vinfo)) {
      GST_DEBUG_OBJECT (self,
          "Passthrough because mode=auto and progressive caps");
      self->passthrough = TRUE;
    } else if (gst_caps_can_intersect (caps_no_feat, tmp)) {
      if (peercaps) {
        GstCaps *allowed_caps;
        GstCaps *tmp2;
        GstStructure *s;

        allowed_caps = gst_caps_intersect (peercaps, tmp);

        tmp2 = gst_caps_copy (caps);
        s = gst_caps_get_structure (tmp2, 0);
        gst_structure_set (s, "interlace-mode", G_TYPE_STRING, "progressive",
            NULL);
        gst_structure_remove_field (s, "framerate");

        /* Downstream does not support progressive caps but supports
         * the upstream caps, go passthrough.
         * TODO: We might want to check the framerate compatibility
         * of the caps too here
         */
        if (gst_caps_can_intersect (allowed_caps, caps)
            && !gst_caps_can_intersect (allowed_caps, tmp2)) {
          GST_DEBUG_OBJECT (self,
              "Passthrough because mode=auto, "
              "downstream does not support progressive caps and interlaced caps");
          self->passthrough = TRUE;
        } else {
          GST_DEBUG_OBJECT (self, "Not passthrough because mode=auto, "
              "downstream supports progressive caps and interlaced caps");
          self->passthrough = FALSE;
        }

        gst_caps_unref (allowed_caps);
        gst_caps_unref (tmp2);
      } else {
        GST_DEBUG_OBJECT (self,
            "Not passthrough because mode=auto and interlaced caps");
        self->passthrough = FALSE;
      }
    } else {
      if (self->mode == GST_DEINTERLACE_MODE_AUTO) {
        GST_WARNING_OBJECT (self,
            "Passthrough because mode=auto and unsupported interlaced caps");
        self->passthrough = TRUE;
      } else {
        gst_caps_unref (tmp);
        GST_ERROR_OBJECT (self,
            "Unsupported interlaced caps in mode=auto-strict");
        goto invalid_caps;
      }
    }

    gst_caps_unref (tmp);
  } else {
    g_assert_not_reached ();
  }

  interlacing_mode = GST_VIDEO_INFO_INTERLACE_MODE (&self->vinfo);

  if (!self->passthrough) {
    if (self->pattern_lock) {
      srccaps = gst_caps_copy (caps_no_feat);
      if (self->pattern != -1
          && G_UNLIKELY (!gst_util_fraction_multiply (fps_n, fps_d,
                  telecine_patterns[self->pattern].ratio_n,
                  telecine_patterns[self->pattern].ratio_d, &fps_n, &fps_d)))
        GST_ERROR_OBJECT (self,
            "Multiplying the framerate by the telecine pattern ratio overflowed!");
      gst_caps_set_simple (srccaps, "framerate", GST_TYPE_FRACTION, fps_n,
          fps_d, NULL);
    } else if (self->locking == GST_DEINTERLACE_LOCKING_ACTIVE
        || self->low_latency == 0) {
      /* in high latency pattern locking mode if we don't have a pattern lock,
       * the sink pad caps are the best we know */
      srccaps = gst_caps_copy (caps_no_feat);
    } else if (self->low_latency > 0
        && interlacing_mode == GST_VIDEO_INTERLACE_MODE_MIXED
        && self->pattern == -1) {
      /* for initial buffers of a telecine pattern, until there is a lock we
       * we output naïvely adjusted timestamps in low-latency pattern locking
       * mode */
      srccaps = gst_caps_copy (caps_no_feat);
      gst_caps_set_simple (srccaps, "framerate", GST_TYPE_FRACTION, 0, 1, NULL);
    } else if (self->user_set_fields == GST_DEINTERLACE_FIELDS_AUTO) {
      srccaps = gst_caps_copy (caps_no_feat);
      if (peercaps) {
        gboolean can_be_tf = FALSE;

        /* We already know that we are not passthrough: interlace-mode will
         * be progressive */
        gst_caps_set_simple (srccaps, "interlace-mode", G_TYPE_STRING,
            "progressive", NULL);

        if (gst_caps_can_intersect (peercaps, srccaps)) {
          GST_DEBUG_OBJECT (self, "Can deinterlace top fields");
          can_be_tf = TRUE;
        }
        srccaps = gst_deinterlace_caps_double_framerate (srccaps, FALSE);
        if (!gst_caps_can_intersect (peercaps, srccaps)) {
          if (can_be_tf) {
            GST_DEBUG_OBJECT (self, "Will deinterlace top fields");
            gst_caps_set_simple (srccaps, "framerate", GST_TYPE_FRACTION, fps_n,
                fps_d, NULL);
            self->fields = GST_DEINTERLACE_TF;
          } else {
            GST_DEBUG_OBJECT (self,
                "Can't negotiate upstream and downstream caps");
            gst_caps_unref (srccaps);
            goto invalid_caps;
          }
        } else {
          GST_DEBUG_OBJECT (self, "Deinterlacing all fields");
          self->fields = GST_DEINTERLACE_ALL;
        }
      } else {
        GST_DEBUG_OBJECT (self,
            "No peer caps yet, falling back to deinterlacing all fields");
        self->fields = GST_DEINTERLACE_ALL;
        srccaps = gst_deinterlace_caps_double_framerate (srccaps, FALSE);
      }
    } else {
      self->fields = self->user_set_fields;
      srccaps = gst_caps_copy (caps_no_feat);
      if (self->fields == GST_DEINTERLACE_ALL)
        srccaps = gst_deinterlace_caps_double_framerate (srccaps, FALSE);
    }

    /* If not passthrough, we are going to output progressive content */
    gst_caps_set_simple (srccaps, "interlace-mode", G_TYPE_STRING,
        "progressive", NULL);

    {
      GstStructure *s = gst_caps_get_structure (srccaps, 0);
      gst_structure_remove_field (s, "field-order");
    }

    gst_deinterlace_set_method (self, self->method_id);
    gst_deinterlace_method_setup (self->method, &self->vinfo);
  } else {
    srccaps = gst_caps_ref (caps_no_feat);
  }

  if (fps_n != 0) {
    self->field_duration = gst_util_uint64_scale (GST_SECOND, fps_d, 2 * fps_n);
  } else {
    self->field_duration = 0;
  }

  GST_DEBUG_OBJECT (pad, "Sink caps: %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (pad, "Src  caps: %" GST_PTR_FORMAT, srccaps);

  if (!gst_pad_set_caps (self->srcpad, srccaps))
    goto set_caps_failed;

  if (!gst_deinterlace_do_bufferpool (self, srccaps))
    goto no_bufferpool;

  if (peercaps)
    gst_caps_unref (peercaps);
  gst_caps_unref (srccaps);
  g_clear_pointer (&caps_no_feat, gst_caps_unref);

  return TRUE;

invalid_caps:
  {
    if (peercaps)
      gst_caps_unref (peercaps);
    g_clear_pointer (&caps_no_feat, gst_caps_unref);
    GST_ERROR_OBJECT (pad, "Invalid caps: %" GST_PTR_FORMAT, caps);
    gst_pad_mark_reconfigure (self->srcpad);
    return FALSE;
  }
set_caps_failed:
  {
    GST_INFO_OBJECT (pad, "Failed to set caps: %" GST_PTR_FORMAT, srccaps);
    if (peercaps)
      gst_caps_unref (peercaps);
    gst_caps_unref (srccaps);
    g_clear_pointer (&caps_no_feat, gst_caps_unref);
    gst_pad_mark_reconfigure (self->srcpad);
    return FALSE;
  }
no_bufferpool:
  {
    GST_ERROR_OBJECT (pad, "could not negotiate bufferpool");
    if (peercaps)
      gst_caps_unref (peercaps);
    gst_caps_unref (srccaps);
    g_clear_pointer (&caps_no_feat, gst_caps_unref);
    gst_pad_mark_reconfigure (self->srcpad);
    return FALSE;
  }
}

static gboolean
gst_deinterlace_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = TRUE;
  GstDeinterlace *self = GST_DEINTERLACE (parent);

  GST_LOG_OBJECT (pad, "received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      res = gst_deinterlace_setcaps (self, pad, caps, FALSE);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;

      gst_event_parse_segment (event, &segment);

      gst_deinterlace_reset_qos (self);
      gst_deinterlace_reset_history (self, FALSE);

      if (segment->format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad,
            "Got SEGMENT event in TIME format, passing on (%"
            GST_TIME_FORMAT " - %" GST_TIME_FORMAT ")",
            GST_TIME_ARGS (segment->start), GST_TIME_ARGS (segment->stop));
        gst_segment_copy_into (segment, &self->segment);
      } else {
        GST_WARNING_OBJECT (pad, "Got SEGMENT event in %s format",
            gst_format_get_name (segment->format));
        gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
      }

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
          gst_deinterlace_reset_history (self, FALSE);
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

      res = gst_pad_push_event (self->srcpad, event);
      break;
    }
    case GST_EVENT_EOS:
      self->have_eos = TRUE;
      gst_deinterlace_reset_history (self, FALSE);
      res = gst_pad_push_event (self->srcpad, event);
      break;

    case GST_EVENT_FLUSH_STOP:
      if (self->still_frame_mode) {
        GST_DEBUG_OBJECT (self, "Ending still frames");
        self->still_frame_mode = FALSE;
      }
      self->telecine_tc_warned = FALSE;
      gst_deinterlace_reset_qos (self);
      res = gst_pad_push_event (self->srcpad, event);
      gst_deinterlace_reset_history (self, TRUE);
      break;

    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static gboolean
gst_deinterlace_propose_allocation (GstDeinterlace * self, GstQuery * query)
{
  GstBufferPool *pool;
  GstCaps *caps;
  GstVideoInfo info;
  guint size;
  GstStructure *config;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  pool = gst_video_buffer_pool_new ();

  gst_query_add_allocation_pool (query, pool, size, 0, 0);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size,
      (gst_deinterlace_method_get_fields_required (self->method) + 1) / 2 + 1,
      0);
  gst_buffer_pool_set_config (pool, config);

  gst_object_unref (pool);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_deinterlace_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstDeinterlace *self = GST_DEINTERLACE (parent);
  gboolean res = FALSE;

  GST_LOG_OBJECT (pad, "%s query", GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_deinterlace_getcaps (self, pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      gboolean ret;

      gst_query_parse_accept_caps (query, &caps);
      ret = gst_deinterlace_acceptcaps (self, pad, caps);
      gst_query_set_accept_caps_result (query, ret);
      res = TRUE;
      break;
    }
    case GST_QUERY_ALLOCATION:
      if (self->passthrough)
        res = gst_pad_peer_query (self->srcpad, query);
      else
        res = gst_deinterlace_propose_allocation (self, query);
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
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
gst_deinterlace_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDeinterlace *self = GST_DEINTERLACE (parent);
  gboolean res;

  GST_DEBUG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:{
      GstClockTimeDiff diff;
      GstClockTime timestamp;
      GstQOSType type;
      gdouble proportion;

      gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);

      gst_deinterlace_update_qos (self, proportion, diff, timestamp);
    }
      /* fall through */
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static gboolean
gst_deinterlace_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstDeinterlace *self = GST_DEINTERLACE (parent);
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

            GST_OBJECT_LOCK (self);
            if (self->method) {
              fields_required =
                  gst_deinterlace_method_get_fields_required (self->method);
              method_latency =
                  gst_deinterlace_method_get_latency (self->method);
            }
            GST_OBJECT_UNLOCK (self);

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
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}


static gboolean
deinterlace_element_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (deinterlace_debug, "deinterlace", 0, "Deinterlacer");

#if HAVE_ORC
  orc_init ();
#endif

  return gst_element_register (plugin, "deinterlace", GST_RANK_NONE,
      GST_TYPE_DEINTERLACE);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return GST_ELEMENT_REGISTER (deinterlace, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    deinterlace,
    "Deinterlacer", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
