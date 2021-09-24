/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-d3d11compositor
 * @title: d3d11compositor
 *
 * A convenient bin which wraps #d3d11compositorelement for video composition
 * with other helper elements to handle color conversion and memory transfer
 * between Direct3D11 and system memory space.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 d3d11compositor name=c ! d3d11videosink \
 *     videotestsrc ! video/x-raw,width=320,height=240 ! c. \
 *     videotestsrc pattern=ball ! video/x-raw,width=100,height=100 ! c.
 * ```
 *
 * Since: 1.20
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/controller/gstproxycontrolbinding.h>
#include "gstd3d11compositorbin.h"
#include "gstd3d11compositor.h"
#include "gstd3d11pluginutils.h"

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_compositor_debug);
#define GST_CAT_DEFAULT gst_d3d11_compositor_debug

/****************************
 * GstD3D11CompositorBinPad *
 ****************************/

enum
{
  PROP_PAD_0,
  /* GstAggregatorPad */
  PROP_PAD_EMIT_SIGNALS,
};

/* GstAggregatorPad */
#define DEFAULT_PAD_EMIT_SIGNALS FALSE

enum
{
  /* GstAggregatorPad */
  SIGNAL_PAD_BUFFER_CONSUMED = 0,
  SIGNAL_PAD_LAST,
};

static guint gst_d3d11_compositor_bin_pad_signals[SIGNAL_PAD_LAST] = { 0 };

/**
 * GstD3D11CompositorBinPad:
 *
 * Since: 1.20
 */
struct _GstD3D11CompositorBinPad
{
  GstGhostPad parent;

  /* Holds ref */
  GstPad *target;
  gulong sig_id;
};

static void gst_d3d11_compositor_bin_pad_dispose (GObject * object);
static void gst_d3d11_compositor_bin_pad_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_compositor_bin_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void
gst_d3d11_compositor_bin_pad_set_target_default (GstD3D11CompositorBinPad * pad,
    GstPad * target);

G_DEFINE_TYPE (GstD3D11CompositorBinPad, gst_d3d11_compositor_bin_pad,
    GST_TYPE_GHOST_PAD);

static void
gst_d3d11_compositor_bin_pad_class_init (GstD3D11CompositorBinPadClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_d3d11_compositor_bin_pad_dispose;
  gobject_class->set_property = gst_d3d11_compositor_bin_pad_set_property;
  gobject_class->get_property = gst_d3d11_compositor_bin_pad_get_property;

  /* GstAggregatorPad */
  g_object_class_install_property (gobject_class, PROP_PAD_EMIT_SIGNALS,
      g_param_spec_boolean ("emit-signals", "Emit signals",
          "Send signals to signal data consumption",
          DEFAULT_PAD_EMIT_SIGNALS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_d3d11_compositor_bin_pad_signals[SIGNAL_PAD_BUFFER_CONSUMED] =
      g_signal_new ("buffer-consumed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GST_TYPE_BUFFER);

  klass->set_target =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_bin_pad_set_target_default);
}

static void
gst_d3d11_compositor_bin_pad_init (GstD3D11CompositorBinPad * self)
{
}

static void
gst_d3d11_compositor_bin_pad_dispose (GObject * object)
{
  GstD3D11CompositorBinPad *self = GST_D3D11_COMPOSITOR_BIN_PAD (object);

  gst_clear_object (&self->target);

  G_OBJECT_CLASS (gst_d3d11_compositor_bin_pad_parent_class)->dispose (object);
}

static void
gst_d3d11_compositor_bin_pad_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstD3D11CompositorBinPad *self = GST_D3D11_COMPOSITOR_BIN_PAD (object);

  if (self->target)
    g_object_set_property (G_OBJECT (self->target), pspec->name, value);
}

static void
gst_d3d11_compositor_bin_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstD3D11CompositorBinPad *self = GST_D3D11_COMPOSITOR_BIN_PAD (object);

  if (self->target)
    g_object_get_property (G_OBJECT (self->target), pspec->name, value);
}

static void
gst_d3d11_compositor_bin_pad_on_buffer_consumed (GstAggregatorPad * pad,
    GstBuffer * buffer, GstD3D11CompositorBinPad * self)
{
  g_signal_emit (self,
      gst_d3d11_compositor_bin_pad_signals[SIGNAL_PAD_BUFFER_CONSUMED],
      0, buffer);
}

/**
 * gst_d3d11_compositor_bin_pad_set_target:
 * @self: a #GstD3D11CompositorBinPad
 * @target: (transfer full): a #GstAggregatorPad
 */
static void
gst_d3d11_compositor_bin_pad_set_target (GstD3D11CompositorBinPad * pad,
    GstPad * target)
{
  GstD3D11CompositorBinPadClass *klass =
      GST_D3D11_COMPOSITOR_BIN_PAD_GET_CLASS (pad);

  klass->set_target (pad, target);
}

static void
gst_d3d11_compositor_bin_pad_set_target_default (GstD3D11CompositorBinPad * pad,
    GstPad * target)
{
  pad->target = target;
  pad->sig_id = g_signal_connect (target, "buffer-consumed",
      G_CALLBACK (gst_d3d11_compositor_bin_pad_on_buffer_consumed), pad);
}

static void
gst_d3d11_compositor_bin_pad_unset_target (GstD3D11CompositorBinPad * self)
{
  if (!self->target)
    return;

  if (self->sig_id)
    g_signal_handler_disconnect (self->target, self->sig_id);
  self->sig_id = 0;
  gst_clear_object (&self->target);
}

/******************************
 * GstD3D11CompositorBinInput *
 ******************************/

enum
{
  PROP_INPUT_0,
  /* GstVideoAggregatorPad */
  PROP_INPUT_ZORDER,
  PROP_INPUT_REPEAT_AFTER_EOS,
  PROP_INPUT_MAX_LAST_BUFFER_REPEAT,
  /* GstD3D11CompositorPad */
  PROP_INPUT_XPOS,
  PROP_INPUT_YPOS,
  PROP_INPUT_WIDTH,
  PROP_INPUT_HEIGHT,
  PROP_INPUT_ALPHA,
  PROP_INPUT_BLEND_OP_RGB,
  PROP_INPUT_BLEND_OP_ALPHA,
  PROP_INPUT_BLEND_SRC_RGB,
  PROP_INPUT_BLEND_SRC_ALPHA,
  PROP_INPUT_BLEND_DEST_RGB,
  PROP_INPUT_BLEND_DEST_ALPHA,
  PROP_INPUT_BLEND_FACTOR_RED,
  PROP_INPUT_BLEND_FACTOR_GREEN,
  PROP_INPUT_BLEND_FACTOR_BLUE,
  PROP_INPUT_BLEND_FACTOR_ALPHA,
  PROP_INPUT_SIZING_POLICY,
};

/* GstVideoAggregatorPad */
#define DEFAULT_INPUT_ZORDER 0
#define DEFAULT_INPUT_REPEAT_AFTER_EOS FALSE
#define DEFAULT_INPUT_MAX_LAST_BUFFER_REPEAT GST_CLOCK_TIME_NONE
/* GstD3D11CompositorPad */
#define DEFAULT_INPUT_XPOS   0
#define DEFAULT_INPUT_YPOS   0
#define DEFAULT_INPUT_WIDTH  0
#define DEFAULT_INPUT_HEIGHT 0
#define DEFAULT_INPUT_ALPHA  1.0
#define DEFAULT_INPUT_BLEND_OP_RGB GST_D3D11_COMPOSITOR_BLEND_OP_ADD
#define DEFAULT_INPUT_BLEND_OP_ALPHA GST_D3D11_COMPOSITOR_BLEND_OP_ADD
#define DEFAULT_INPUT_BLEND_SRC_RGB GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA
#define DEFAULT_INPUT_BLEND_SRC_ALPHA GST_D3D11_COMPOSITOR_BLEND_ONE
#define DEFAULT_INPUT_BLEND_DEST_RGB GST_D3D11_COMPOSITOR_BLEND_INV_SRC_ALPHA
#define DEFAULT_INPUT_BLEND_DEST_ALPHA GST_D3D11_COMPOSITOR_BLEND_INV_SRC_ALPHA
#define DEFAULT_INPUT_SIZING_POLICY GST_D3D11_COMPOSITOR_SIZING_POLICY_NONE

/**
 * GstD3D11CompositorBinInput:
 *
 * Since: 1.20
 */
struct _GstD3D11CompositorBinInput
{
  GstD3D11CompositorBinPad parent;
};

static void gst_d3d11_compositor_bin_input_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_compositor_bin_input_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void
gst_d3d11_compositor_bin_input_set_target (GstD3D11CompositorBinPad * pad,
    GstPad * target);

#define gst_d3d11_compositor_bin_input_parent_class input_parent_class
G_DEFINE_TYPE (GstD3D11CompositorBinInput, gst_d3d11_compositor_bin_input,
    GST_TYPE_D3D11_COMPOSITOR_BIN_PAD);

static void
gst_d3d11_compositor_bin_input_class_init (GstD3D11CompositorBinInputClass *
    klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstD3D11CompositorBinPadClass *pad_class =
      GST_D3D11_COMPOSITOR_BIN_PAD_CLASS (klass);

  gobject_class->set_property = gst_d3d11_compositor_bin_input_set_property;
  gobject_class->get_property = gst_d3d11_compositor_bin_input_get_property;

  /* GstVideoAggregatorPad */
  g_object_class_install_property (gobject_class, PROP_INPUT_ZORDER,
      g_param_spec_uint ("zorder", "Z-Order", "Z Order of the picture",
          0, G_MAXUINT, DEFAULT_INPUT_ZORDER,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_REPEAT_AFTER_EOS,
      g_param_spec_boolean ("repeat-after-eos", "Repeat After EOS",
          "Repeat the " "last frame after EOS until all pads are EOS",
          DEFAULT_INPUT_REPEAT_AFTER_EOS,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_INPUT_MAX_LAST_BUFFER_REPEAT,
      g_param_spec_uint64 ("max-last-buffer-repeat", "Max Last Buffer Repeat",
          "Repeat last buffer for time (in ns, -1=until EOS), "
          "behaviour on EOS is not affected", 0, G_MAXUINT64,
          DEFAULT_INPUT_MAX_LAST_BUFFER_REPEAT,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_STATIC_STRINGS)));

  /* GstD3D11CompositorPad */
  g_object_class_install_property (gobject_class, PROP_INPUT_XPOS,
      g_param_spec_int ("xpos", "X Position", "X position of the picture",
          G_MININT, G_MAXINT, DEFAULT_INPUT_XPOS,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y position of the picture",
          G_MININT, G_MAXINT, DEFAULT_INPUT_YPOS,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_WIDTH,
      g_param_spec_int ("width", "Width", "Width of the picture",
          G_MININT, G_MAXINT, DEFAULT_INPUT_WIDTH,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of the picture",
          G_MININT, G_MAXINT, DEFAULT_INPUT_HEIGHT,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_INPUT_ALPHA,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_BLEND_OP_RGB,
      g_param_spec_enum ("blend-op-rgb", "Blend Operation RGB",
          "Blend equation for RGB", GST_TYPE_D3D11_COMPOSITOR_BLEND_OPERATION,
          DEFAULT_INPUT_BLEND_OP_RGB,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_BLEND_OP_ALPHA,
      g_param_spec_enum ("blend-op-alpha", "Blend Operation Alpha",
          "Blend equation for alpha", GST_TYPE_D3D11_COMPOSITOR_BLEND_OPERATION,
          DEFAULT_INPUT_BLEND_OP_ALPHA,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_SRC_RGB,
      g_param_spec_enum ("blend-src-rgb", "Blend Source RGB",
          "Blend factor for source RGB",
          GST_TYPE_D3D11_COMPOSITOR_BLEND,
          DEFAULT_INPUT_BLEND_SRC_RGB,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_SRC_ALPHA,
      g_param_spec_enum ("blend-src-alpha",
          "Blend Source Alpha",
          "Blend factor for source alpha, \"*-color\" values are not allowed",
          GST_TYPE_D3D11_COMPOSITOR_BLEND,
          DEFAULT_INPUT_BLEND_SRC_ALPHA,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_DEST_RGB,
      g_param_spec_enum ("blend-dest-rgb",
          "Blend Destination RGB",
          "Blend factor for destination RGB",
          GST_TYPE_D3D11_COMPOSITOR_BLEND,
          DEFAULT_INPUT_BLEND_DEST_RGB,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_INPUT_BLEND_DEST_ALPHA,
      g_param_spec_enum ("blend-dest-alpha",
          "Blend Destination Alpha",
          "Blend factor for destination alpha, "
          "\"*-color\" values are not allowed",
          GST_TYPE_D3D11_COMPOSITOR_BLEND,
          DEFAULT_INPUT_BLEND_DEST_ALPHA,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_BLEND_FACTOR_RED,
      g_param_spec_float ("blend-factor-red", "Blend Factor Red",
          "Blend factor for red component "
          "when blend type is \"blend-factor\" or \"inv-blend-factor\"",
          0.0, 1.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_BLEND_FACTOR_GREEN,
      g_param_spec_float ("blend-factor-green", "Blend Factor Green",
          "Blend factor for green component "
          "when blend type is \"blend-factor\" or \"inv-blend-factor\"",
          0.0, 1.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_BLEND_FACTOR_BLUE,
      g_param_spec_float ("blend-factor-blue", "Blend Factor Blue",
          "Blend factor for blue component "
          "when blend type is \"blend-factor\" or \"inv-blend-factor\"",
          0.0, 1.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_BLEND_FACTOR_ALPHA,
      g_param_spec_float ("blend-factor-alpha", "Blend Factor Alpha",
          "Blend factor for alpha component "
          "when blend type is \"blend-factor\" or \"inv-blend-factor\"",
          0.0, 1.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_SIZING_POLICY,
      g_param_spec_enum ("sizing-policy", "Sizing policy",
          "Sizing policy to use for image scaling",
          GST_TYPE_D3D11_COMPOSITOR_SIZING_POLICY, DEFAULT_INPUT_SIZING_POLICY,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  pad_class->set_target =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_bin_input_set_target);
}

static void
gst_d3d11_compositor_bin_input_init (GstD3D11CompositorBinInput * self)
{
}

static void
gst_d3d11_compositor_bin_input_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstD3D11CompositorBinPad *pad = GST_D3D11_COMPOSITOR_BIN_PAD (object);

  if (pad->target)
    g_object_set_property (G_OBJECT (pad->target), pspec->name, value);
}

static void
gst_d3d11_compositor_bin_input_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstD3D11CompositorBinPad *pad = GST_D3D11_COMPOSITOR_BIN_PAD (object);

  if (pad->target)
    g_object_get_property (G_OBJECT (pad->target), pspec->name, value);
}

static void
gst_d3d11_compositor_bin_input_set_target (GstD3D11CompositorBinPad * pad,
    GstPad * target)
{
  GST_D3D11_COMPOSITOR_BIN_PAD_CLASS (input_parent_class)->set_target (pad,
      target);

#define ADD_BINDING(obj,ref,prop) \
    gst_object_add_control_binding (GST_OBJECT (obj), \
        gst_proxy_control_binding_new (GST_OBJECT (obj), prop, \
            GST_OBJECT (ref), prop));
  /* GstVideoAggregatorPad */
  ADD_BINDING (target, pad, "zorder");
  ADD_BINDING (target, pad, "repeat-after-eos");
  /* GstD3D11CompositorPad */
  ADD_BINDING (target, pad, "xpos");
  ADD_BINDING (target, pad, "ypos");
  ADD_BINDING (target, pad, "width");
  ADD_BINDING (target, pad, "height");
  ADD_BINDING (target, pad, "alpha");
  ADD_BINDING (target, pad, "blend-op-rgb");
  ADD_BINDING (target, pad, "blend-op-alpha");
  ADD_BINDING (target, pad, "blend-src-rgb");
  ADD_BINDING (target, pad, "blend-src-alpha");
  ADD_BINDING (target, pad, "blend-dest-rgb");
  ADD_BINDING (target, pad, "blend-dest-alpha");
  ADD_BINDING (target, pad, "blend-factor-red");
  ADD_BINDING (target, pad, "blend-factor-green");
  ADD_BINDING (target, pad, "blend-factor-blue");
  ADD_BINDING (target, pad, "blend-factor-alpha");
  ADD_BINDING (target, pad, "sizing-policy");
#undef ADD_BINDING
}

/*************************
 * GstD3D11CompositorBin *
 *************************/

static GstStaticCaps sink_template_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_SINK_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE (GST_D3D11_SINK_FORMATS));

static GstStaticCaps src_template_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_SRC_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE (GST_D3D11_SRC_FORMATS));

enum
{
  PROP_0,
  PROP_MIXER,
  /* GstAggregator */
  PROP_LATENCY,
  PROP_MIN_UPSTREAM_LATENCY,
  PROP_START_TIME_SELECTION,
  PROP_START_TIME,
  PROP_EMIT_SIGNALS,
  /* GstD3D11Compositor */
  PROP_ADAPTER,
  PROP_BACKGROUND,
  PROP_LAST
};

/* GstAggregator */
#define DEFAULT_LATENCY              0
#define DEFAULT_MIN_UPSTREAM_LATENCY 0
#define DEFAULT_START_TIME_SELECTION GST_AGGREGATOR_START_TIME_SELECTION_ZERO
#define DEFAULT_START_TIME           (-1)
#define DEFAULT_EMIT_SIGNALS         FALSE

/* GstD3D11Compositor */
#define DEFAULT_ADAPTER -1
#define DEFAULT_BACKGROUND GST_D3D11_COMPOSITOR_BACKGROUND_CHECKER

typedef struct _GstD3D11CompositorBinChain
{
  /* without ref */
  GstD3D11CompositorBin *self;
  GstD3D11CompositorBinPad *ghost_pad;
  GstElement *upload;
  GstElement *convert;

  gulong probe_id;
} GstD3D11CompositorBinChain;

struct _GstD3D11CompositorBin
{
  GstBin parent;

  GstElement *compositor;

  GList *input_chains;
  gboolean running;

  gint adapter;
};

static void gst_d3d11_compositor_bin_child_proxy_init (gpointer g_iface,
    gpointer iface_data);
static void gst_d3d11_compositor_bin_dispose (GObject * object);
static void gst_d3d11_compositor_bin_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_compositor_bin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn
gst_d3d11_compositor_bin_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_d3d11_compositor_bin_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_d3d11_compositor_bin_release_pad (GstElement * element,
    GstPad * pad);

#define gst_d3d11_compositor_bin_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstD3D11CompositorBin, gst_d3d11_compositor_bin,
    GST_TYPE_BIN, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_d3d11_compositor_bin_child_proxy_init));

static void
gst_d3d11_compositor_bin_class_init (GstD3D11CompositorBinClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstCaps *caps;

  gobject_class->dispose = gst_d3d11_compositor_bin_dispose;
  gobject_class->set_property = gst_d3d11_compositor_bin_set_property;
  gobject_class->get_property = gst_d3d11_compositor_bin_get_property;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_bin_change_state);
  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_bin_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_bin_release_pad);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 Compositor Bin",
      "Filter/Editor/Video/Compositor",
      "A Direct3D11 compositor bin", "Seungha Yang <seungha@centricular.com>");

  caps = gst_d3d11_get_updated_template_caps (&sink_template_caps);
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new_with_gtype ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
          caps, GST_TYPE_D3D11_COMPOSITOR_BIN_INPUT));
  gst_caps_unref (caps);

  caps = gst_d3d11_get_updated_template_caps (&src_template_caps);
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new_with_gtype ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          caps, GST_TYPE_D3D11_COMPOSITOR_BIN_PAD));
  gst_caps_unref (caps);

  g_object_class_install_property (gobject_class, PROP_MIXER,
      g_param_spec_object ("mixer", "D3D11 mixer element",
          "The d3d11 mixer chain to use",
          GST_TYPE_ELEMENT,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  /*GstAggregator */
  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint64 ("latency", "Buffer latency",
          "Additional latency in live mode to allow upstream "
          "to take longer to produce buffers for the current "
          "position (in nanoseconds)", 0, G_MAXUINT64,
          DEFAULT_LATENCY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_MIN_UPSTREAM_LATENCY,
      g_param_spec_uint64 ("min-upstream-latency", "Buffer latency",
          "When sources with a higher latency are expected to be plugged "
          "in dynamically after the aggregator has started playing, "
          "this allows overriding the minimum latency reported by the "
          "initial source(s). This is only taken into account when larger "
          "than the actually reported minimum latency. (nanoseconds)",
          0, G_MAXUINT64,
          DEFAULT_LATENCY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_START_TIME_SELECTION,
      g_param_spec_enum ("start-time-selection", "Start Time Selection",
          "Decides which start time is output",
          gst_aggregator_start_time_selection_get_type (),
          DEFAULT_START_TIME_SELECTION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_START_TIME,
      g_param_spec_uint64 ("start-time", "Start Time",
          "Start time to use if start-time-selection=set", 0,
          G_MAXUINT64,
          DEFAULT_START_TIME,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_EMIT_SIGNALS,
      g_param_spec_boolean ("emit-signals", "Emit signals",
          "Send signals", DEFAULT_EMIT_SIGNALS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /* GstD3D11Compositor */
  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "Adapter index for creating device (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BACKGROUND,
      g_param_spec_enum ("background", "Background", "Background type",
          GST_TYPE_D3D11_COMPOSITOR_BACKGROUND,
          DEFAULT_BACKGROUND,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_type_mark_as_plugin_api (GST_TYPE_D3D11_COMPOSITOR_BIN_PAD,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_D3D11_COMPOSITOR_BIN_INPUT,
      (GstPluginAPIFlags) 0);
}

static void
gst_d3d11_compositor_bin_init (GstD3D11CompositorBin * self)
{
  GstPad *pad;
  GstPad *gpad;
  GstElement *out_convert, *download;

  self->compositor = gst_element_factory_make ("d3d11compositorelement", NULL);
  out_convert = gst_element_factory_make ("d3d11colorconvert", NULL);
  download = gst_element_factory_make ("d3d11download", NULL);

  gst_bin_add_many (GST_BIN (self),
      self->compositor, out_convert, download, NULL);
  gst_element_link_many (self->compositor, out_convert, download, NULL);

  gpad = (GstPad *) g_object_new (GST_TYPE_D3D11_COMPOSITOR_BIN_PAD,
      "name", "src", "direction", GST_PAD_SRC, NULL);
  pad = gst_element_get_static_pad (self->compositor, "src");
  /* GstD3D11CompositorBinPad will hold reference of this compositor srcpad */
  gst_d3d11_compositor_bin_pad_set_target ((GstD3D11CompositorBinPad *) gpad,
      pad);

  pad = gst_element_get_static_pad (download, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (gpad), pad);
  gst_object_unref (pad);

  gst_element_add_pad (GST_ELEMENT_CAST (self), gpad);
}

static void
gst_d3d11_compositor_bin_dispose (GObject * object)
{
  GstD3D11CompositorBin *self = GST_D3D11_COMPOSITOR_BIN (object);
  GList *iter;

  for (iter = self->input_chains; iter; iter = g_list_next (iter)) {
    GstD3D11CompositorBinChain *chain =
        (GstD3D11CompositorBinChain *) iter->data;

    if (self->compositor && chain->ghost_pad && chain->ghost_pad->target) {
      gst_element_release_request_pad (GST_ELEMENT_CAST (self->compositor),
          chain->ghost_pad->target);
      gst_d3d11_compositor_bin_pad_unset_target (chain->ghost_pad);
    }
  }

  if (self->input_chains)
    g_list_free_full (self->input_chains, (GDestroyNotify) g_free);
  self->input_chains = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_compositor_bin_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstD3D11CompositorBin *self = GST_D3D11_COMPOSITOR_BIN (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      self->adapter = g_value_get_int (value);
      /* fallthrough */
    default:
      g_object_set_property (G_OBJECT (self->compositor), pspec->name, value);
      break;
  }
}

static void
gst_d3d11_compositor_bin_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstD3D11CompositorBin *self = GST_D3D11_COMPOSITOR_BIN (object);

  switch (prop_id) {
    case PROP_MIXER:
      g_value_set_object (value, self->compositor);
      break;
    case PROP_ADAPTER:
      g_value_set_int (value, self->adapter);
      break;
    default:
      g_object_get_property (G_OBJECT (self->compositor), pspec->name, value);
      break;
  }
}

static GstStateChangeReturn
gst_d3d11_compositor_bin_change_state (GstElement * element,
    GstStateChange transition)
{
  GstD3D11CompositorBin *self = GST_D3D11_COMPOSITOR_BIN (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      GST_OBJECT_LOCK (element);
      self->running = TRUE;
      GST_OBJECT_UNLOCK (element);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_OBJECT_LOCK (self);
      self->running = FALSE;
      GST_OBJECT_UNLOCK (self);
    default:
      break;
  }

  return ret;
}

static GstD3D11CompositorBinChain *
gst_d3d11_compositor_bin_input_chain_new (GstD3D11CompositorBin * self,
    GstPad * compositor_pad)
{
  GstD3D11CompositorBinChain *chain;
  GstPad *pad;

  chain = g_new0 (GstD3D11CompositorBinChain, 1);

  chain->self = self;

  chain->upload = gst_element_factory_make ("d3d11upload", NULL);
  chain->convert = gst_element_factory_make ("d3d11colorconvert", NULL);

  /* 1. Create child elements and like */
  gst_bin_add_many (GST_BIN (self), chain->upload, chain->convert, NULL);

  gst_element_link (chain->upload, chain->convert);
  pad = gst_element_get_static_pad (chain->convert, "src");
  gst_pad_link (pad, compositor_pad);
  gst_object_unref (pad);

  chain->ghost_pad = (GstD3D11CompositorBinPad *)
      g_object_new (GST_TYPE_D3D11_COMPOSITOR_BIN_INPUT, "name",
      GST_OBJECT_NAME (compositor_pad), "direction", GST_PAD_SINK, NULL);

  /* transfer ownership of compositor pad */
  gst_d3d11_compositor_bin_pad_set_target (chain->ghost_pad, compositor_pad);

  pad = gst_element_get_static_pad (chain->upload, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (chain->ghost_pad), pad);
  gst_object_unref (pad);

  GST_OBJECT_LOCK (self);
  if (self->running)
    gst_pad_set_active (GST_PAD (chain->ghost_pad), TRUE);
  GST_OBJECT_UNLOCK (self);

  gst_element_add_pad (GST_ELEMENT_CAST (self),
      GST_PAD_CAST (chain->ghost_pad));

  gst_element_sync_state_with_parent (chain->upload);
  gst_element_sync_state_with_parent (chain->convert);

  return chain;
}

static void
gst_d3d11_compositor_bin_input_chain_free (GstD3D11CompositorBinChain * chain)
{
  if (!chain)
    return;

  if (chain->ghost_pad && chain->probe_id) {
    gst_pad_remove_probe (GST_PAD_CAST (chain->ghost_pad), chain->probe_id);
    chain->probe_id = 0;
  }

  if (chain->upload) {
    gst_element_set_state (chain->upload, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (chain->self), chain->upload);
  }

  if (chain->convert) {
    gst_element_set_state (chain->convert, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (chain->self), chain->convert);
  }

  if (chain->ghost_pad && chain->ghost_pad->target) {
    gst_element_release_request_pad (chain->self->compositor,
        chain->ghost_pad->target);
    gst_d3d11_compositor_bin_pad_unset_target (chain->ghost_pad);
  }

  g_free (chain);
}

static GstPad *
gst_d3d11_compositor_bin_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstD3D11CompositorBin *self = GST_D3D11_COMPOSITOR_BIN (element);
  GstElementClass *compositor_class = GST_ELEMENT_GET_CLASS (self->compositor);
  GstPad *compositor_pad;
  GstD3D11CompositorBinChain *chain;
  GstPadTemplate *compositor_templ = NULL;
  GList *templ_list;
  GList *iter;

  templ_list = gst_element_class_get_pad_template_list (compositor_class);
  for (iter = templ_list; iter; iter = g_list_next (iter)) {
    GstPadTemplate *t = (GstPadTemplate *) iter->data;
    if (GST_PAD_TEMPLATE_DIRECTION (t) != GST_PAD_SINK ||
        GST_PAD_TEMPLATE_PRESENCE (t) != GST_PAD_REQUEST)
      continue;

    compositor_templ = t;
    break;
  }

  g_assert (compositor_templ);

  compositor_pad =
      gst_element_request_pad (self->compositor, compositor_templ, name, caps);
  if (!compositor_pad) {
    GST_WARNING_OBJECT (self, "Failed to request pad");
    return NULL;
  }

  chain = gst_d3d11_compositor_bin_input_chain_new (self, compositor_pad);
  g_assert (chain);

  GST_OBJECT_LOCK (self);
  self->input_chains = g_list_append (self->input_chains, chain);
  GST_OBJECT_UNLOCK (self);

  gst_child_proxy_child_added (GST_CHILD_PROXY (self),
      G_OBJECT (chain->ghost_pad), GST_OBJECT_NAME (chain->ghost_pad));

  GST_DEBUG_OBJECT (element, "Created new pad %s:%s",
      GST_DEBUG_PAD_NAME (chain->ghost_pad));

  return GST_PAD_CAST (chain->ghost_pad);
}

static void
gst_d3d11_compositor_bin_release_pad (GstElement * element, GstPad * pad)
{
  GstD3D11CompositorBin *self = GST_D3D11_COMPOSITOR_BIN (element);
  GList *iter;
  gboolean found = FALSE;

  GST_DEBUG_OBJECT (self, "Releasing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  GST_OBJECT_LOCK (self);
  for (iter = self->input_chains; iter; iter = g_list_next (iter)) {
    GstD3D11CompositorBinChain *chain =
        (GstD3D11CompositorBinChain *) iter->data;

    if (pad == GST_PAD_CAST (chain->ghost_pad)) {
      self->input_chains = g_list_delete_link (self->input_chains, iter);
      GST_OBJECT_UNLOCK (self);

      gst_d3d11_compositor_bin_input_chain_free (chain);
      found = TRUE;
      break;
    }
  }

  if (!found) {
    GST_OBJECT_UNLOCK (self);
    GST_WARNING_OBJECT (self, "Unknown pad to release %s:%s",
        GST_DEBUG_PAD_NAME (pad));
  }

  gst_element_remove_pad (element, pad);
}

static GObject *
gst_d3d11_compositor_bin_child_proxy_get_child_by_index (GstChildProxy * proxy,
    guint index)
{
  GstD3D11CompositorBin *self = GST_D3D11_COMPOSITOR_BIN (proxy);
  GstBin *bin = GST_BIN_CAST (proxy);
  GObject *res = NULL;

  GST_OBJECT_LOCK (self);
  /* XXX: not exactly thread safe with ordering */
  if (index < (guint) bin->numchildren) {
    if ((res = (GObject *) g_list_nth_data (bin->children, index)))
      gst_object_ref (res);
  } else {
    GstD3D11CompositorBinChain *chain;
    if ((chain =
            (GstD3D11CompositorBinChain *) g_list_nth_data (self->input_chains,
                index - bin->numchildren))) {
      res = (GObject *) gst_object_ref (chain->ghost_pad);
    }
  }
  GST_OBJECT_UNLOCK (self);

  return res;
}

static guint
gst_d3d11_compositor_bin_child_proxy_get_children_count (GstChildProxy * proxy)
{
  GstD3D11CompositorBin *self = GST_D3D11_COMPOSITOR_BIN (proxy);
  guint count = 0;

  GST_OBJECT_LOCK (self);
  count = GST_BIN_CAST (self)->numchildren + g_list_length (self->input_chains);
  GST_OBJECT_UNLOCK (self);
  GST_INFO_OBJECT (self, "Children Count: %d", count);

  return count;
}

static void
gst_d3d11_compositor_bin_child_proxy_init (gpointer g_iface,
    gpointer iface_data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index =
      gst_d3d11_compositor_bin_child_proxy_get_child_by_index;
  iface->get_children_count =
      gst_d3d11_compositor_bin_child_proxy_get_children_count;
}
