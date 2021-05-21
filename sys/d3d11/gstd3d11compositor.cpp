/*
 * GStreamer
 * Copyright (C) 2004, 2008 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
 * Copyright (C) 2014 Thibault Saunier <tsaunier@gnome.org>
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
 * SECTION:element-d3d11compositorelement
 * @title: d3d11compositorelement
 *
 * A Direct3D11 based video compositing element.
 *
 * Since: 1.20
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d11compositor.h"
#include "gstd3d11converter.h"
#include "gstd3d11shader.h"
#include "gstd3d11pluginutils.h"
#include <string.h>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_compositor_debug);
#define GST_CAT_DEFAULT gst_d3d11_compositor_debug

G_END_DECLS
/* *INDENT-ON* */

/**
 * GstD3D11CompositorBlendOperation:
 * @GST_D3D11_COMPOSITOR_BLEND_OP_ADD:
 *      Add source 1 and source 2
 * @GST_D3D11_COMPOSITOR_BLEND_OP_SUBTRACT:
 *      Subtract source 1 from source 2
 * @GST_D3D11_COMPOSITOR_BLEND_OP_REV_SUBTRACT:
 *      Subtract source 2 from source 1
 * @GST_D3D11_COMPOSITOR_BLEND_OP_MIN:
 *      Find the minimum of source 1 and source 2
 * @GST_D3D11_COMPOSITOR_BLEND_OP_MAX:
 *      Find the maximum of source 1 and source 2
 *
 * Since: 1.20
 */
GType
gst_d3d11_compositor_blend_operation_get_type (void)
{
  static GType blend_operation_type = 0;

  static const GEnumValue blend_operator[] = {
    {GST_D3D11_COMPOSITOR_BLEND_OP_ADD, "Add source and background",
        "add"},
    {GST_D3D11_COMPOSITOR_BLEND_OP_SUBTRACT,
          "Subtract source from background",
        "subtract"},
    {GST_D3D11_COMPOSITOR_BLEND_OP_REV_SUBTRACT,
          "Subtract background from source",
        "rev-subtract"},
    {GST_D3D11_COMPOSITOR_BLEND_OP_MIN,
        "Minimum of source and background", "min"},
    {GST_D3D11_COMPOSITOR_BLEND_OP_MAX,
        "Maximum of source and background", "max"},
    {0, NULL, NULL},
  };

  if (!blend_operation_type) {
    blend_operation_type =
        g_enum_register_static ("GstD3D11CompositorBlendOperation",
        blend_operator);
  }
  return blend_operation_type;
}

static GstD3D11CompositorBlendOperation
gst_d3d11_compositor_blend_operation_from_native (D3D11_BLEND_OP blend_op)
{
  switch (blend_op) {
    case D3D11_BLEND_OP_ADD:
      return GST_D3D11_COMPOSITOR_BLEND_OP_ADD;
    case D3D11_BLEND_OP_SUBTRACT:
      return GST_D3D11_COMPOSITOR_BLEND_OP_SUBTRACT;
    case D3D11_BLEND_OP_REV_SUBTRACT:
      return GST_D3D11_COMPOSITOR_BLEND_OP_REV_SUBTRACT;
    case D3D11_BLEND_OP_MIN:
      return GST_D3D11_COMPOSITOR_BLEND_OP_MIN;
    case D3D11_BLEND_OP_MAX:
      return GST_D3D11_COMPOSITOR_BLEND_OP_MAX;
    default:
      g_assert_not_reached ();
      break;
  }

  return GST_D3D11_COMPOSITOR_BLEND_OP_ADD;
}

static D3D11_BLEND_OP
gst_d3d11_compositor_blend_operation_to_native (GstD3D11CompositorBlendOperation
    op)
{
  switch (op) {
    case GST_D3D11_COMPOSITOR_BLEND_OP_ADD:
      return D3D11_BLEND_OP_ADD;
    case GST_D3D11_COMPOSITOR_BLEND_OP_SUBTRACT:
      return D3D11_BLEND_OP_SUBTRACT;
    case GST_D3D11_COMPOSITOR_BLEND_OP_REV_SUBTRACT:
      return D3D11_BLEND_OP_REV_SUBTRACT;
    case GST_D3D11_COMPOSITOR_BLEND_OP_MIN:
      return D3D11_BLEND_OP_MIN;
    case GST_D3D11_COMPOSITOR_BLEND_OP_MAX:
      return D3D11_BLEND_OP_MAX;
    default:
      g_assert_not_reached ();
      break;
  }

  return D3D11_BLEND_OP_ADD;
}

/**
 * GstD3D11CompositorBlend:
 * @GST_D3D11_COMPOSITOR_BLEND_ZERO:
 *      The blend factor is (0, 0, 0, 0). No pre-blend operation.
 * @GST_D3D11_COMPOSITOR_BLEND_ONE:
 *      The blend factor is (1, 1, 1, 1). No pre-blend operation.
 * @GST_D3D11_COMPOSITOR_BLEND_SRC_COLOR:
 *      The blend factor is (Rs, Gs, Bs, As),
 *      that is color data (RGB) from a pixel shader. No pre-blend operation.
 * @GST_D3D11_COMPOSITOR_BLEND_INV_SRC_COLOR:
 *      The blend factor is (1 - Rs, 1 - Gs, 1 - Bs, 1 - As),
 *      that is color data (RGB) from a pixel shader.
 *      The pre-blend operation inverts the data, generating 1 - RGB.
 * @GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA:
 *      The blend factor is (As, As, As, As),
 *      that is alpha data (A) from a pixel shader. No pre-blend operation.
 * @GST_D3D11_COMPOSITOR_BLEND_INV_SRC_ALPHA:
 *      The blend factor is ( 1 - As, 1 - As, 1 - As, 1 - As),
 *      that is alpha data (A) from a pixel shader.
 *      The pre-blend operation inverts the data, generating 1 - A.
 * @GST_D3D11_COMPOSITOR_BLEND_DEST_ALPHA:
 *      The blend factor is (Ad, Ad, Ad, Ad),
 *      that is alpha data from a render target. No pre-blend operation.
 * @GST_D3D11_COMPOSITOR_BLEND_INV_DEST_ALPHA:
 *      The blend factor is (1 - Ad, 1 - Ad, 1 - Ad, 1 - Ad),
 *      that is alpha data from a render target.
 *      The pre-blend operation inverts the data, generating 1 - A.
 * @GST_D3D11_COMPOSITOR_BLEND_DEST_COLOR:
 *      The blend factor is (Rd, Gd, Bd, Ad),
 *      that is color data from a render target. No pre-blend operation.
 * @GST_D3D11_COMPOSITOR_BLEND_INV_DEST_COLOR:
 *      The blend factor is (1 - Rd, 1 - Gd, 1 - Bd, 1 - Ad),
 *      that is color data from a render target.
 *      The pre-blend operation inverts the data, generating 1 - RGB.
 * @GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA_SAT:
 *      The blend factor is (f, f, f, 1); where f = min(As, 1 - Ad).
 *      The pre-blend operation clamps the data to 1 or less.
 * @GST_D3D11_COMPOSITOR_BLEND_BLEND_FACTOR:
 *      The blend factor is the blend factor set with
 *      ID3D11DeviceContext::OMSetBlendState. No pre-blend operation.
 * @GST_D3D11_COMPOSITOR_BLEND_INV_BLEND_FACTOR:
 *      The blend factor is the blend factor set with
 *      ID3D11DeviceContext::OMSetBlendState.
 *      The pre-blend operation inverts the blend factor,
 *      generating 1 - blend_factor.
 *
 * Since: 1.20
 */
GType
gst_d3d11_compositor_blend_get_type (void)
{
  static GType blend_type = 0;

  static const GEnumValue blend[] = {
    {GST_D3D11_COMPOSITOR_BLEND_ZERO,
        "The blend factor is (0, 0, 0, 0)", "zero"},
    {GST_D3D11_COMPOSITOR_BLEND_ONE,
        "The blend factor is (1, 1, 1, 1)", "one"},
    {GST_D3D11_COMPOSITOR_BLEND_SRC_COLOR,
        "The blend factor is (Rs, Gs, Bs, As)", "src-color"},
    {GST_D3D11_COMPOSITOR_BLEND_INV_SRC_COLOR,
          "The blend factor is (1 - Rs, 1 - Gs, 1 - Bs, 1 - As)",
        "inv-src-color"},
    {GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA,
        "The blend factor is (As, As, As, As)", "src-alpha"},
    {GST_D3D11_COMPOSITOR_BLEND_INV_SRC_ALPHA,
          "The blend factor is (1 - As, 1 - As, 1 - As, 1 - As)",
        "inv-src-alpha"},
    {GST_D3D11_COMPOSITOR_BLEND_DEST_ALPHA,
        "The blend factor is (Ad, Ad, Ad, Ad)", "dest-alpha"},
    {GST_D3D11_COMPOSITOR_BLEND_INV_DEST_ALPHA,
          "The blend factor is (1 - Ad, 1 - Ad, 1 - Ad, 1 - Ad)",
        "inv-dest-alpha"},
    {GST_D3D11_COMPOSITOR_BLEND_DEST_COLOR,
        "The blend factor is (Rd, Gd, Bd, Ad)", "dest-color"},
    {GST_D3D11_COMPOSITOR_BLEND_INV_DEST_COLOR,
          "The blend factor is (1 - Rd, 1 - Gd, 1 - Bd, 1 - Ad)",
        "inv-dest-color"},
    {GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA_SAT,
          "The blend factor is (f, f, f, 1); where f = min(As, 1 - Ad)",
        "src-alpha-sat"},
    {GST_D3D11_COMPOSITOR_BLEND_BLEND_FACTOR,
        "User defined blend factor", "blend-factor"},
    {GST_D3D11_COMPOSITOR_BLEND_INV_BLEND_FACTOR,
        "Inverse of user defined blend factor", "inv-blend-factor"},
    {0, NULL, NULL},
  };

  if (!blend_type) {
    blend_type = g_enum_register_static ("GstD3D11CompositorBlend", blend);
  }
  return blend_type;
}

static GstD3D11CompositorBlend
gst_d3d11_compositor_blend_from_native (D3D11_BLEND blend)
{
  switch (blend) {
    case D3D11_BLEND_ZERO:
      return GST_D3D11_COMPOSITOR_BLEND_ZERO;
    case D3D11_BLEND_ONE:
      return GST_D3D11_COMPOSITOR_BLEND_ONE;
    case D3D11_BLEND_SRC_COLOR:
      return GST_D3D11_COMPOSITOR_BLEND_SRC_COLOR;
    case D3D11_BLEND_INV_SRC_COLOR:
      return GST_D3D11_COMPOSITOR_BLEND_INV_SRC_COLOR;
    case D3D11_BLEND_SRC_ALPHA:
      return GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA;
    case D3D11_BLEND_INV_SRC_ALPHA:
      return GST_D3D11_COMPOSITOR_BLEND_INV_SRC_ALPHA;
    case D3D11_BLEND_DEST_ALPHA:
      return GST_D3D11_COMPOSITOR_BLEND_DEST_ALPHA;
    case D3D11_BLEND_INV_DEST_ALPHA:
      return GST_D3D11_COMPOSITOR_BLEND_INV_DEST_ALPHA;
    case D3D11_BLEND_DEST_COLOR:
      return GST_D3D11_COMPOSITOR_BLEND_DEST_COLOR;
    case D3D11_BLEND_INV_DEST_COLOR:
      return GST_D3D11_COMPOSITOR_BLEND_INV_DEST_COLOR;
    case D3D11_BLEND_SRC_ALPHA_SAT:
      return GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA_SAT;
    case D3D11_BLEND_BLEND_FACTOR:
      return GST_D3D11_COMPOSITOR_BLEND_BLEND_FACTOR;
    case D3D11_BLEND_INV_BLEND_FACTOR:
      return GST_D3D11_COMPOSITOR_BLEND_INV_BLEND_FACTOR;
    default:
      g_assert_not_reached ();
      break;
  }

  return GST_D3D11_COMPOSITOR_BLEND_ZERO;
}

static D3D11_BLEND
gst_d3d11_compositor_blend_to_native (GstD3D11CompositorBlend blend)
{
  switch (blend) {
    case GST_D3D11_COMPOSITOR_BLEND_ZERO:
      return D3D11_BLEND_ZERO;
    case GST_D3D11_COMPOSITOR_BLEND_ONE:
      return D3D11_BLEND_ONE;
    case GST_D3D11_COMPOSITOR_BLEND_SRC_COLOR:
      return D3D11_BLEND_SRC_COLOR;
    case GST_D3D11_COMPOSITOR_BLEND_INV_SRC_COLOR:
      return D3D11_BLEND_INV_SRC_COLOR;
    case GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA:
      return D3D11_BLEND_SRC_ALPHA;
    case GST_D3D11_COMPOSITOR_BLEND_INV_SRC_ALPHA:
      return D3D11_BLEND_INV_SRC_ALPHA;
    case GST_D3D11_COMPOSITOR_BLEND_DEST_ALPHA:
      return D3D11_BLEND_DEST_ALPHA;
    case GST_D3D11_COMPOSITOR_BLEND_INV_DEST_ALPHA:
      return D3D11_BLEND_INV_DEST_ALPHA;
    case GST_D3D11_COMPOSITOR_BLEND_DEST_COLOR:
      return D3D11_BLEND_DEST_COLOR;
    case GST_D3D11_COMPOSITOR_BLEND_INV_DEST_COLOR:
      return D3D11_BLEND_INV_DEST_COLOR;
    case GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA_SAT:
      return D3D11_BLEND_SRC_ALPHA_SAT;
    case GST_D3D11_COMPOSITOR_BLEND_BLEND_FACTOR:
      return D3D11_BLEND_BLEND_FACTOR;
    case GST_D3D11_COMPOSITOR_BLEND_INV_BLEND_FACTOR:
      return D3D11_BLEND_INV_BLEND_FACTOR;
    default:
      g_assert_not_reached ();
      break;
  }

  return D3D11_BLEND_ZERO;
}

/**
 * GstD3D11CompositorBackground:
 *
 * Background mode
 *
 * Since: 1.20
 */
GType
gst_d3d11_compositor_background_get_type (void)
{
  static GType compositor_background_type = 0;

  static const GEnumValue compositor_background[] = {
    {GST_D3D11_COMPOSITOR_BACKGROUND_CHECKER, "Checker pattern", "checker"},
    {GST_D3D11_COMPOSITOR_BACKGROUND_BLACK, "Black", "black"},
    {GST_D3D11_COMPOSITOR_BACKGROUND_WHITE, "White", "white"},
    {GST_D3D11_COMPOSITOR_BACKGROUND_TRANSPARENT,
        "Transparent Background to enable further compositing", "transparent"},
    {0, NULL, NULL},
  };

  if (!compositor_background_type) {
    compositor_background_type =
        g_enum_register_static ("GstD3D11CompositorBackground",
        compositor_background);
  }
  return compositor_background_type;
}

/* *INDENT-OFF* */
static const gchar checker_vs_src[] =
    "struct VS_INPUT\n"
    "{\n"
    "  float4 Position : POSITION;\n"
    "};\n"
    "\n"
    "struct VS_OUTPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "};\n"
    "\n"
    "VS_OUTPUT main(VS_INPUT input)\n"
    "{\n"
    "  return input;\n"
    "}\n";

static const gchar checker_ps_src[] =
    "static const float blocksize = 8.0;\n"
    "static const float4 high = float4(0.667, 0.667, 0.667, 1.0);\n"
    "static const float4 low = float4(0.333, 0.333, 0.333, 1.0);\n"
    "struct PS_INPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "};\n"
    "struct PS_OUTPUT\n"
    "{\n"
    "  float4 Plane: SV_TARGET;\n"
    "};\n"
    "PS_OUTPUT main(PS_INPUT input)\n"
    "{\n"
    "  PS_OUTPUT output;\n"
    "  if ((input.Position.x % (blocksize * 2.0)) >= blocksize) {\n"
    "    if ((input.Position.y % (blocksize * 2.0)) >= blocksize)\n"
    "      output.Plane = low;\n"
    "    else\n"
    "      output.Plane = high;\n"
    "  } else {\n"
    "    if ((input.Position.y % (blocksize * 2.0)) < blocksize)\n"
    "      output.Plane = low;\n"
    "    else\n"
    "      output.Plane = high;\n"
    "  }\n"
    "  return output;\n"
    "}\n";
/* *INDENT-ON* */

/**
 * GstD3D11CompositorPad:
 *
 * Since: 1.20
 */
struct _GstD3D11CompositorPad
{
  GstVideoAggregatorConvertPad parent;

  GstD3D11Converter *convert;

  GstBufferPool *fallback_pool;
  GstBuffer *fallback_buf;

  gboolean position_updated;
  gboolean alpha_updated;
  gboolean blend_desc_updated;
  ID3D11BlendState *blend;

  /* properties */
  gint xpos;
  gint ypos;
  gint width;
  gint height;
  gdouble alpha;
  D3D11_RENDER_TARGET_BLEND_DESC desc;
  gfloat blend_factor[4];
};

struct _GstD3D11Compositor
{
  GstVideoAggregator parent;

  GstD3D11Device *device;

  GstBufferPool *fallback_pool;
  GstBuffer *fallback_buf;

  GstD3D11Quad *checker_background;
  D3D11_VIEWPORT viewport;

  gboolean reconfigured;

  /* properties */
  gint adapter;
  GstD3D11CompositorBackground background;
};

enum
{
  PROP_PAD_0,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
  PROP_PAD_ALPHA,
  PROP_PAD_BLEND_OP_RGB,
  PROP_PAD_BLEND_OP_ALPHA,
  PROP_PAD_BLEND_SRC_RGB,
  PROP_PAD_BLEND_SRC_ALPHA,
  PROP_PAD_BLEND_DEST_RGB,
  PROP_PAD_BLEND_DEST_ALPHA,
  PROP_PAD_BLEND_FACTOR_RED,
  PROP_PAD_BLEND_FACTOR_GREEN,
  PROP_PAD_BLEND_FACTOR_BLUE,
  PROP_PAD_BLEND_FACTOR_ALPHA,
};

#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_WIDTH  0
#define DEFAULT_PAD_HEIGHT 0
#define DEFAULT_PAD_ALPHA  1.0
#define DEFAULT_PAD_BLEND_OP_RGB GST_D3D11_COMPOSITOR_BLEND_OP_ADD
#define DEFAULT_PAD_BLEND_OP_ALPHA GST_D3D11_COMPOSITOR_BLEND_OP_ADD
#define DEFAULT_PAD_BLEND_SRC_RGB GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA
#define DEFAULT_PAD_BLEND_SRC_ALPHA GST_D3D11_COMPOSITOR_BLEND_ONE
#define DEFAULT_PAD_BLEND_DEST_RGB GST_D3D11_COMPOSITOR_BLEND_INV_SRC_ALPHA
#define DEFAULT_PAD_BLEND_DEST_ALPHA GST_D3D11_COMPOSITOR_BLEND_INV_SRC_ALPHA

static void gst_d3d11_compositor_pad_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_compositor_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean
gst_d3d11_compositor_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame);
static void
gst_d3d11_compositor_pad_clean_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstVideoFrame * prepared_frame);
static void
gst_d3d11_compositor_pad_init_blend_options (GstD3D11CompositorPad * pad);

#define gst_d3d11_compositor_pad_parent_class parent_pad_class
G_DEFINE_TYPE (GstD3D11CompositorPad, gst_d3d11_compositor_pad,
    GST_TYPE_VIDEO_AGGREGATOR_PAD);

static void
gst_d3d11_compositor_pad_class_init (GstD3D11CompositorPadClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoAggregatorPadClass *vaggpadclass =
      GST_VIDEO_AGGREGATOR_PAD_CLASS (klass);

  gobject_class->set_property = gst_d3d11_compositor_pad_set_property;
  gobject_class->get_property = gst_d3d11_compositor_pad_get_property;

  g_object_class_install_property (gobject_class, PROP_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAD_WIDTH,
      g_param_spec_int ("width", "Width", "Width of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_WIDTH,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAD_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_HEIGHT,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAD_BLEND_OP_RGB,
      g_param_spec_enum ("blend-op-rgb", "Blend Operation RGB",
          "Blend equation for RGB", GST_TYPE_D3D11_COMPOSITOR_BLEND_OPERATION,
          DEFAULT_PAD_BLEND_OP_RGB,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAD_BLEND_OP_ALPHA,
      g_param_spec_enum ("blend-op-alpha", "Blend Operation Alpha",
          "Blend equation for alpha", GST_TYPE_D3D11_COMPOSITOR_BLEND_OPERATION,
          DEFAULT_PAD_BLEND_OP_ALPHA,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_PAD_BLEND_SRC_RGB,
      g_param_spec_enum ("blend-src-rgb", "Blend Source RGB",
          "Blend factor for source RGB",
          GST_TYPE_D3D11_COMPOSITOR_BLEND,
          DEFAULT_PAD_BLEND_SRC_RGB,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_PAD_BLEND_SRC_ALPHA,
      g_param_spec_enum ("blend-src-alpha",
          "Blend Source Alpha",
          "Blend factor for source alpha, \"*-color\" values are not allowed",
          GST_TYPE_D3D11_COMPOSITOR_BLEND,
          DEFAULT_PAD_BLEND_SRC_ALPHA,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_PAD_BLEND_DEST_RGB,
      g_param_spec_enum ("blend-dest-rgb",
          "Blend Destination RGB",
          "Blend factor for destination RGB",
          GST_TYPE_D3D11_COMPOSITOR_BLEND,
          DEFAULT_PAD_BLEND_DEST_RGB,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class,
      PROP_PAD_BLEND_DEST_ALPHA,
      g_param_spec_enum ("blend-dest-alpha",
          "Blend Destination Alpha",
          "Blend factor for destination alpha, "
          "\"*-color\" values are not allowed",
          GST_TYPE_D3D11_COMPOSITOR_BLEND,
          DEFAULT_PAD_BLEND_DEST_ALPHA,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAD_BLEND_FACTOR_RED,
      g_param_spec_float ("blend-factor-red", "Blend Factor Red",
          "Blend factor for red component "
          "when blend type is \"blend-factor\" or \"inv-blend-factor\"",
          0.0, 1.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAD_BLEND_FACTOR_GREEN,
      g_param_spec_float ("blend-factor-green", "Blend Factor Green",
          "Blend factor for green component "
          "when blend type is \"blend-factor\" or \"inv-blend-factor\"",
          0.0, 1.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAD_BLEND_FACTOR_BLUE,
      g_param_spec_float ("blend-factor-blue", "Blend Factor Blue",
          "Blend factor for blue component "
          "when blend type is \"blend-factor\" or \"inv-blend-factor\"",
          0.0, 1.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PAD_BLEND_FACTOR_ALPHA,
      g_param_spec_float ("blend-factor-alpha", "Blend Factor Alpha",
          "Blend factor for alpha component "
          "when blend type is \"blend-factor\" or \"inv-blend-factor\"",
          0.0, 1.0, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE |
              G_PARAM_STATIC_STRINGS)));

  vaggpadclass->prepare_frame =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_pad_prepare_frame);
  vaggpadclass->clean_frame =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_pad_clean_frame);

  gst_type_mark_as_plugin_api (GST_TYPE_D3D11_COMPOSITOR_BLEND,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_D3D11_COMPOSITOR_BLEND_OPERATION,
      (GstPluginAPIFlags) 0);
}

static void
gst_d3d11_compositor_pad_init (GstD3D11CompositorPad * pad)
{
  pad->xpos = DEFAULT_PAD_XPOS;
  pad->ypos = DEFAULT_PAD_YPOS;
  pad->width = DEFAULT_PAD_WIDTH;
  pad->height = DEFAULT_PAD_HEIGHT;
  pad->alpha = DEFAULT_PAD_ALPHA;

  gst_d3d11_compositor_pad_init_blend_options (pad);
}

static void
gst_d3d11_compositor_pad_update_blend_function (GstD3D11CompositorPad * pad,
    D3D11_BLEND * value, GstD3D11CompositorBlend new_value)
{
  D3D11_BLEND temp = gst_d3d11_compositor_blend_to_native (new_value);

  if (temp == *value)
    return;

  *value = temp;
  pad->blend_desc_updated = TRUE;
}

static void
gst_d3d11_compositor_pad_update_blend_equation (GstD3D11CompositorPad * pad,
    D3D11_BLEND_OP * value, GstD3D11CompositorBlendOperation new_value)
{
  D3D11_BLEND_OP temp =
      gst_d3d11_compositor_blend_operation_to_native (new_value);

  if (temp == *value)
    return;

  *value = temp;
  pad->blend_desc_updated = TRUE;
}

static void
gst_d3d11_compositor_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11CompositorPad *pad = GST_D3D11_COMPOSITOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      pad->xpos = g_value_get_int (value);
      pad->position_updated = TRUE;
      break;
    case PROP_PAD_YPOS:
      pad->ypos = g_value_get_int (value);
      pad->position_updated = TRUE;
      break;
    case PROP_PAD_WIDTH:
      pad->width = g_value_get_int (value);
      pad->position_updated = TRUE;
      break;
    case PROP_PAD_HEIGHT:
      pad->height = g_value_get_int (value);
      pad->position_updated = TRUE;
      break;
    case PROP_PAD_ALPHA:
    {
      gdouble alpha = g_value_get_double (value);
      if (pad->alpha != alpha) {
        pad->alpha_updated = TRUE;
        pad->alpha = alpha;
      }
      break;
    }
    case PROP_PAD_BLEND_OP_RGB:
      gst_d3d11_compositor_pad_update_blend_equation (pad, &pad->desc.BlendOp,
          (GstD3D11CompositorBlendOperation) g_value_get_enum (value));
      break;
    case PROP_PAD_BLEND_OP_ALPHA:
      gst_d3d11_compositor_pad_update_blend_equation (pad,
          &pad->desc.BlendOpAlpha,
          (GstD3D11CompositorBlendOperation) g_value_get_enum (value));
      break;
    case PROP_PAD_BLEND_SRC_RGB:
      gst_d3d11_compositor_pad_update_blend_function (pad, &pad->desc.SrcBlend,
          (GstD3D11CompositorBlend) g_value_get_enum (value));
      break;
    case PROP_PAD_BLEND_SRC_ALPHA:
    {
      GstD3D11CompositorBlend blend =
          (GstD3D11CompositorBlend) g_value_get_enum (value);
      if (blend == GST_D3D11_COMPOSITOR_BLEND_SRC_COLOR ||
          blend == GST_D3D11_COMPOSITOR_BLEND_INV_SRC_COLOR ||
          blend == GST_D3D11_COMPOSITOR_BLEND_DEST_COLOR ||
          blend == GST_D3D11_COMPOSITOR_BLEND_INV_DEST_COLOR) {
        g_warning ("%d is not allowed for %s", blend, pspec->name);
      } else {
        gst_d3d11_compositor_pad_update_blend_function (pad,
            &pad->desc.SrcBlendAlpha, blend);
      }
      break;
    }
    case PROP_PAD_BLEND_DEST_RGB:
      gst_d3d11_compositor_pad_update_blend_function (pad, &pad->desc.DestBlend,
          (GstD3D11CompositorBlend) g_value_get_enum (value));
      break;
    case PROP_PAD_BLEND_DEST_ALPHA:
    {
      GstD3D11CompositorBlend blend =
          (GstD3D11CompositorBlend) g_value_get_enum (value);
      if (blend == GST_D3D11_COMPOSITOR_BLEND_SRC_COLOR ||
          blend == GST_D3D11_COMPOSITOR_BLEND_INV_SRC_COLOR ||
          blend == GST_D3D11_COMPOSITOR_BLEND_DEST_COLOR ||
          blend == GST_D3D11_COMPOSITOR_BLEND_INV_DEST_COLOR) {
        g_warning ("%d is not allowed for %s", blend, pspec->name);
      } else {
        gst_d3d11_compositor_pad_update_blend_function (pad,
            &pad->desc.DestBlendAlpha, blend);
      }
      break;
    }
    case PROP_PAD_BLEND_FACTOR_RED:
      pad->blend_factor[0] = g_value_get_float (value);
      break;
    case PROP_PAD_BLEND_FACTOR_GREEN:
      pad->blend_factor[1] = g_value_get_float (value);
      break;
    case PROP_PAD_BLEND_FACTOR_BLUE:
      pad->blend_factor[2] = g_value_get_float (value);
      break;
    case PROP_PAD_BLEND_FACTOR_ALPHA:
      pad->blend_factor[3] = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_compositor_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11CompositorPad *pad = GST_D3D11_COMPOSITOR_PAD (object);

  switch (prop_id) {
    case PROP_PAD_XPOS:
      g_value_set_int (value, pad->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, pad->ypos);
      break;
    case PROP_PAD_WIDTH:
      g_value_set_int (value, pad->width);
      break;
    case PROP_PAD_HEIGHT:
      g_value_set_int (value, pad->height);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, pad->alpha);
      break;
    case PROP_PAD_BLEND_OP_RGB:
      g_value_set_enum (value,
          gst_d3d11_compositor_blend_operation_from_native (pad->desc.BlendOp));
      break;
    case PROP_PAD_BLEND_OP_ALPHA:
      g_value_set_enum (value,
          gst_d3d11_compositor_blend_operation_from_native (pad->
              desc.BlendOpAlpha));
      break;
    case PROP_PAD_BLEND_SRC_RGB:
      g_value_set_enum (value,
          gst_d3d11_compositor_blend_from_native (pad->desc.SrcBlend));
      break;
    case PROP_PAD_BLEND_SRC_ALPHA:
      g_value_set_enum (value,
          gst_d3d11_compositor_blend_from_native (pad->desc.SrcBlendAlpha));
      break;
    case PROP_PAD_BLEND_DEST_RGB:
      g_value_set_enum (value,
          gst_d3d11_compositor_blend_from_native (pad->desc.DestBlend));
      break;
    case PROP_PAD_BLEND_DEST_ALPHA:
      g_value_set_enum (value,
          gst_d3d11_compositor_blend_from_native (pad->desc.DestBlendAlpha));
      break;
    case PROP_PAD_BLEND_FACTOR_RED:
      g_value_set_float (value, pad->blend_factor[0]);
      break;
    case PROP_PAD_BLEND_FACTOR_GREEN:
      g_value_set_float (value, pad->blend_factor[1]);
      break;
    case PROP_PAD_BLEND_FACTOR_BLUE:
      g_value_set_float (value, pad->blend_factor[2]);
      break;
    case PROP_PAD_BLEND_FACTOR_ALPHA:
      g_value_set_float (value, pad->blend_factor[3]);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_compositor_pad_init_blend_options (GstD3D11CompositorPad * pad)
{
  guint i;

  pad->desc.BlendEnable = TRUE;
  pad->desc.SrcBlend =
      gst_d3d11_compositor_blend_to_native (DEFAULT_PAD_BLEND_SRC_RGB);
  pad->desc.DestBlend =
      gst_d3d11_compositor_blend_to_native (DEFAULT_PAD_BLEND_DEST_RGB);
  pad->desc.BlendOp =
      gst_d3d11_compositor_blend_operation_to_native (DEFAULT_PAD_BLEND_OP_RGB);
  pad->desc.SrcBlendAlpha =
      gst_d3d11_compositor_blend_to_native (DEFAULT_PAD_BLEND_SRC_ALPHA);
  pad->desc.DestBlendAlpha =
      gst_d3d11_compositor_blend_to_native (DEFAULT_PAD_BLEND_DEST_ALPHA);
  pad->desc.BlendOpAlpha =
      gst_d3d11_compositor_blend_operation_to_native
      (DEFAULT_PAD_BLEND_OP_ALPHA);
  pad->desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

  for (i = 0; i < G_N_ELEMENTS (pad->blend_factor); i++)
    pad->blend_factor[i] = 1.0f;
}

static gboolean
gst_d3d11_compositor_configure_fallback_pool (GstD3D11Compositor * self,
    GstVideoInfo * info, gint bind_flags, GstBufferPool ** pool)
{
  GstD3D11AllocationParams *d3d11_params;
  GstBufferPool *new_pool;
  GstCaps *caps;

  if (*pool) {
    gst_buffer_pool_set_active (*pool, FALSE);
    gst_clear_object (pool);
  }

  caps = gst_video_info_to_caps (info);
  if (!caps) {
    GST_ERROR_OBJECT (self, "Couldn't create caps from info");
    return FALSE;
  }

  d3d11_params = gst_d3d11_allocation_params_new (self->device,
      info, (GstD3D11AllocationFlags) 0, bind_flags);

  new_pool = gst_d3d11_buffer_pool_new_with_options (self->device,
      caps, d3d11_params, 0, 0);
  gst_caps_unref (caps);
  gst_d3d11_allocation_params_free (d3d11_params);

  if (!new_pool) {
    GST_ERROR_OBJECT (self, "Failed to configure fallback pool");
    return FALSE;
  }

  gst_buffer_pool_set_active (new_pool, TRUE);
  *pool = new_pool;

  return TRUE;
}

static gboolean
gst_d3d11_compsitor_prepare_fallback_buffer (GstD3D11Compositor * self,
    GstVideoInfo * info, gboolean is_input, GstBufferPool ** pool,
    GstBuffer ** fallback_buffer)
{
  GstBuffer *new_buf = NULL;
  gint bind_flags = D3D11_BIND_SHADER_RESOURCE;
  guint i;

  gst_clear_buffer (fallback_buffer);

  if (!is_input)
    bind_flags = D3D11_BIND_RENDER_TARGET;

  if (*pool == NULL &&
      !gst_d3d11_compositor_configure_fallback_pool (self, info,
          bind_flags, pool)) {
    GST_ERROR_OBJECT (self, "Couldn't configure fallback buffer pool");
    return FALSE;
  }

  if (gst_buffer_pool_acquire_buffer (*pool, &new_buf, NULL)
      != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Couldn't get fallback buffer from pool");
    return FALSE;
  }

  for (i = 0; i < gst_buffer_n_memory (new_buf); i++) {
    GstD3D11Memory *new_mem =
        (GstD3D11Memory *) gst_buffer_peek_memory (new_buf, i);

    if (is_input && !gst_d3d11_memory_get_shader_resource_view_size (new_mem)) {
      GST_ERROR_OBJECT (self, "Couldn't prepare shader resource view");
      gst_buffer_unref (new_buf);
      return FALSE;
    } else if (!is_input &&
        !gst_d3d11_memory_get_render_target_view_size (new_mem)) {
      GST_ERROR_OBJECT (self, "Couldn't prepare render target view");
      gst_buffer_unref (new_buf);
      return FALSE;
    }
  }

  *fallback_buffer = new_buf;

  return TRUE;
}

static gboolean
gst_d3d11_compositor_copy_buffer (GstD3D11Compositor * self,
    GstVideoInfo * info, GstBuffer * src_buf, GstBuffer * dest_buf,
    gboolean do_device_copy)
{
  guint i;

  if (do_device_copy) {
    return gst_d3d11_buffer_copy_into (dest_buf, src_buf, info);
  } else {
    GstVideoFrame src_frame, dest_frame;

    if (!gst_video_frame_map (&src_frame, info, src_buf,
            (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
      GST_ERROR_OBJECT (self, "Couldn't map input buffer");
      return FALSE;
    }

    if (!gst_video_frame_map (&dest_frame, info, dest_buf,
            (GstMapFlags) (GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
      GST_ERROR_OBJECT (self, "Couldn't fallback buffer");
      gst_video_frame_unmap (&src_frame);
      return FALSE;
    }

    for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&src_frame); i++) {
      if (!gst_video_frame_copy_plane (&dest_frame, &src_frame, i)) {
        GST_ERROR_OBJECT (self, "Couldn't copy %dth plane", i);

        gst_video_frame_unmap (&dest_frame);
        gst_video_frame_unmap (&src_frame);

        return FALSE;
      }
    }

    gst_video_frame_unmap (&dest_frame);
    gst_video_frame_unmap (&src_frame);
  }

  return TRUE;
}

static gboolean
gst_d3d11_compositor_check_d3d11_memory (GstD3D11Compositor * self,
    GstBuffer * buffer, gboolean is_input, gboolean * view_available)
{
  guint i;
  gboolean ret = TRUE;

  *view_available = TRUE;

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);
    GstD3D11Memory *dmem;

    if (!gst_is_d3d11_memory (mem)) {
      ret = FALSE;
      goto done;
    }

    dmem = (GstD3D11Memory *) mem;
    if (dmem->device != self->device) {
      ret = FALSE;
      goto done;
    }

    if (is_input) {
      if (!gst_d3d11_memory_get_shader_resource_view_size (dmem))
        *view_available = FALSE;
    } else {
      if (!gst_d3d11_memory_get_render_target_view_size (dmem))
        *view_available = FALSE;
    }
  }

done:
  if (!ret)
    *view_available = FALSE;

  return ret;
}

static void
gst_d3d11_compositor_pad_get_output_size (GstD3D11CompositorPad * comp_pad,
    gint out_par_n, gint out_par_d, gint * width, gint * height)
{
  GstVideoAggregatorPad *vagg_pad = GST_VIDEO_AGGREGATOR_PAD (comp_pad);
  gint pad_width, pad_height;
  guint dar_n, dar_d;

  /* FIXME: Anything better we can do here? */
  if (!vagg_pad->info.finfo
      || vagg_pad->info.finfo->format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_DEBUG_OBJECT (comp_pad, "Have no caps yet");
    *width = 0;
    *height = 0;
    return;
  }

  pad_width =
      comp_pad->width <=
      0 ? GST_VIDEO_INFO_WIDTH (&vagg_pad->info) : comp_pad->width;
  pad_height =
      comp_pad->height <=
      0 ? GST_VIDEO_INFO_HEIGHT (&vagg_pad->info) : comp_pad->height;

  if (!gst_video_calculate_display_ratio (&dar_n, &dar_d, pad_width, pad_height,
          GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
          GST_VIDEO_INFO_PAR_D (&vagg_pad->info), out_par_n, out_par_d)) {
    GST_WARNING_OBJECT (comp_pad, "Cannot calculate display aspect ratio");
    *width = *height = 0;
    return;
  }

  GST_TRACE_OBJECT (comp_pad, "scaling %ux%u by %u/%u (%u/%u / %u/%u)",
      pad_width, pad_height, dar_n, dar_d,
      GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
      GST_VIDEO_INFO_PAR_D (&vagg_pad->info), out_par_n, out_par_d);

  /* Pick either height or width, whichever is an integer multiple of the
   * display aspect ratio. However, prefer preserving the height to account
   * for interlaced video. */
  if (pad_height % dar_n == 0) {
    pad_width = gst_util_uint64_scale_int (pad_height, dar_n, dar_d);
  } else if (pad_width % dar_d == 0) {
    pad_height = gst_util_uint64_scale_int (pad_width, dar_d, dar_n);
  } else {
    pad_width = gst_util_uint64_scale_int (pad_height, dar_n, dar_d);
  }

  *width = pad_width;
  *height = pad_height;
}

static GstVideoRectangle
clamp_rectangle (gint x, gint y, gint w, gint h, gint outer_width,
    gint outer_height)
{
  gint x2 = x + w;
  gint y2 = y + h;
  GstVideoRectangle clamped;

  /* Clamp the x/y coordinates of this frame to the output boundaries to cover
   * the case where (say, with negative xpos/ypos or w/h greater than the output
   * size) the non-obscured portion of the frame could be outside the bounds of
   * the video itself and hence not visible at all */
  clamped.x = CLAMP (x, 0, outer_width);
  clamped.y = CLAMP (y, 0, outer_height);
  clamped.w = CLAMP (x2, 0, outer_width) - clamped.x;
  clamped.h = CLAMP (y2, 0, outer_height) - clamped.y;

  return clamped;
}

static gboolean
gst_d3d11_compositor_pad_check_frame_obscured (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg)
{
  GstD3D11CompositorPad *cpad = GST_D3D11_COMPOSITOR_PAD (pad);
  gint width, height;
  GstVideoInfo *info = &vagg->info;
  /* The rectangle representing this frame, clamped to the video's boundaries.
   * Due to the clamping, this is different from the frame width/height above. */
  GstVideoRectangle frame_rect;

  /* There's three types of width/height here:
   * 1. GST_VIDEO_FRAME_WIDTH/HEIGHT:
   *     The frame width/height (same as pad->info.height/width;
   *     see gst_video_frame_map())
   * 2. cpad->width/height:
   *     The optional pad property for scaling the frame (if zero, the video is
   *     left unscaled)
   */

  gst_d3d11_compositor_pad_get_output_size (cpad, GST_VIDEO_INFO_PAR_N (info),
      GST_VIDEO_INFO_PAR_D (info), &width, &height);

  frame_rect = clamp_rectangle (cpad->xpos, cpad->ypos, width, height,
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));

  if (frame_rect.w == 0 || frame_rect.h == 0) {
    GST_DEBUG_OBJECT (pad, "Resulting frame is zero-width or zero-height "
        "(w: %i, h: %i), skipping", frame_rect.w, frame_rect.h);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_d3d11_compositor_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (vagg);
  GstD3D11CompositorPad *cpad = GST_D3D11_COMPOSITOR_PAD (pad);
  GstBuffer *target_buf = buffer;
  gboolean do_device_copy = FALSE;

  /* Skip this frame */
  if (gst_d3d11_compositor_pad_check_frame_obscured (pad, vagg))
    return TRUE;

  /* Use fallback buffer when input buffer is:
   * - non-d3d11 memory
   * - or, from different d3d11 device
   * - or not bound to shader resource
   */
  if (!gst_d3d11_compositor_check_d3d11_memory (self,
          buffer, TRUE, &do_device_copy) || !do_device_copy) {
    if (!gst_d3d11_compsitor_prepare_fallback_buffer (self, &pad->info, TRUE,
            &cpad->fallback_pool, &cpad->fallback_buf)) {
      GST_ERROR_OBJECT (self, "Couldn't prepare fallback buffer");
      return FALSE;
    }

    if (!gst_d3d11_compositor_copy_buffer (self, &pad->info, buffer,
            cpad->fallback_buf, do_device_copy)) {
      GST_ERROR_OBJECT (self, "Couldn't copy input buffer to fallback buffer");
      gst_clear_buffer (&cpad->fallback_buf);
      return FALSE;
    }

    target_buf = cpad->fallback_buf;
  }

  if (!gst_video_frame_map (prepared_frame, &pad->info, target_buf,
          (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
    GST_WARNING_OBJECT (pad, "Couldn't map input buffer");
    return FALSE;
  }

  return TRUE;
}

static void
gst_d3d11_compositor_pad_clean_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstVideoFrame * prepared_frame)
{
  GstD3D11CompositorPad *cpad = GST_D3D11_COMPOSITOR_PAD (pad);

  GST_VIDEO_AGGREGATOR_PAD_CLASS (parent_pad_class)->clean_frame (pad,
      vagg, prepared_frame);

  gst_clear_buffer (&cpad->fallback_buf);
}

static gboolean
gst_d3d11_compositor_pad_setup_converter (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg)
{
  GstD3D11CompositorPad *cpad = GST_D3D11_COMPOSITOR_PAD (pad);
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (vagg);
  RECT rect;
  gint width, height;
  GstVideoInfo *info = &vagg->info;
  GstVideoRectangle frame_rect;
  gboolean is_first = FALSE;
#ifndef GST_DISABLE_GST_DEBUG
  guint zorder = 0;
#endif

  if (!cpad->convert || self->reconfigured) {
    GstStructure *config;

    if (cpad->convert)
      gst_d3d11_converter_free (cpad->convert);

    config = gst_structure_new_empty ("config");
    if (cpad->alpha <= 1.0) {
      gst_structure_set (config, GST_D3D11_CONVERTER_OPT_ALPHA_VALUE,
          G_TYPE_DOUBLE, cpad->alpha, nullptr);
    }

    cpad->convert =
        gst_d3d11_converter_new (self->device, &pad->info, &vagg->info, config);

    if (!cpad->convert) {
      GST_ERROR_OBJECT (pad, "Couldn't create converter");
      return FALSE;
    }

    is_first = TRUE;
  } else if (cpad->alpha_updated) {
    GstStructure *config;

    config = gst_structure_new_empty ("config");
    if (cpad->alpha <= 1.0) {
      gst_structure_set (config, GST_D3D11_CONVERTER_OPT_ALPHA_VALUE,
          G_TYPE_DOUBLE, cpad->alpha, nullptr);
    }

    gst_d3d11_converter_update_config (cpad->convert, config);
    cpad->alpha_updated = FALSE;
  }

  if (!cpad->blend || cpad->blend_desc_updated) {
    HRESULT hr;
    D3D11_BLEND_DESC desc = { 0, };
    ID3D11BlendState *blend = NULL;
    ID3D11Device *device_handle =
        gst_d3d11_device_get_device_handle (self->device);

    GST_D3D11_CLEAR_COM (cpad->blend);

    desc.AlphaToCoverageEnable = FALSE;
    desc.IndependentBlendEnable = FALSE;
    desc.RenderTarget[0] = cpad->desc;

    hr = device_handle->CreateBlendState (&desc, &blend);
    if (!gst_d3d11_result (hr, self->device)) {
      GST_ERROR_OBJECT (pad, "Couldn't create blend staten, hr: 0x%x",
          (guint) hr);
      return FALSE;
    }

    cpad->blend = blend;
  }

  if (!is_first && !cpad->position_updated)
    return TRUE;

  gst_d3d11_compositor_pad_get_output_size (cpad, GST_VIDEO_INFO_PAR_N (info),
      GST_VIDEO_INFO_PAR_D (info), &width, &height);

  frame_rect = clamp_rectangle (cpad->xpos, cpad->ypos, width, height,
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));

  rect.left = frame_rect.x;
  rect.top = frame_rect.y;
  rect.right = frame_rect.x + frame_rect.w;
  rect.bottom = frame_rect.y + frame_rect.h;

#ifndef GST_DISABLE_GST_DEBUG
  g_object_get (pad, "zorder", &zorder, NULL);

  GST_LOG_OBJECT (pad, "Update position, pad-xpos %d, pad-ypos %d, "
      "pad-zorder %d, pad-width %d, pad-height %d, in-resolution %dx%d, "
      "out-resoution %dx%d, dst-{left,top,right,bottom} %d-%d-%d-%d",
      cpad->xpos, cpad->ypos, zorder, cpad->width, cpad->height,
      GST_VIDEO_INFO_WIDTH (&pad->info), GST_VIDEO_INFO_HEIGHT (&pad->info),
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info),
      (gint) rect.left, (gint) rect.top, (gint) rect.right, (gint) rect.bottom);
#endif

  cpad->position_updated = FALSE;

  return gst_d3d11_converter_update_dest_rect (cpad->convert, &rect);
}

static GstStaticCaps pad_template_caps =
GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, "{ RGBA, BGRA }"));

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_BACKGROUND,
};

#define DEFAULT_ADAPTER -1
#define DEFAULT_BACKGROUND GST_D3D11_COMPOSITOR_BACKGROUND_CHECKER

static void gst_d3d11_compositor_child_proxy_init (gpointer g_iface,
    gpointer iface_data);
static void gst_d3d11_compositor_dispose (GObject * object);
static void gst_d3d11_compositor_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_d3d11_compositor_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *gst_d3d11_compositor_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_d3d11_compositor_release_pad (GstElement * element,
    GstPad * pad);
static void gst_d3d11_compositor_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_compositor_start (GstAggregator * aggregator);
static gboolean gst_d3d11_compositor_stop (GstAggregator * aggregator);
static gboolean gst_d3d11_compositor_sink_query (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstQuery * query);
static gboolean gst_d3d11_compositor_src_query (GstAggregator * aggregator,
    GstQuery * query);
static GstCaps *gst_d3d11_compositor_fixate_src_caps (GstAggregator *
    aggregator, GstCaps * caps);
static gboolean gst_d3d11_compositor_propose_allocation (GstAggregator *
    aggregator, GstAggregatorPad * pad, GstQuery * decide_query,
    GstQuery * query);
static gboolean gst_d3d11_compositor_decide_allocation (GstAggregator *
    aggregator, GstQuery * query);
static GstFlowReturn
gst_d3d11_compositor_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf);
static GstFlowReturn
gst_d3d11_compositor_create_output_buffer (GstVideoAggregator * vagg,
    GstBuffer ** outbuffer);

#define gst_d3d11_compositor_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstD3D11Compositor, gst_d3d11_compositor,
    GST_TYPE_VIDEO_AGGREGATOR, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_d3d11_compositor_child_proxy_init));

static void
gst_d3d11_compositor_class_init (GstD3D11CompositorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *aggregator_class = GST_AGGREGATOR_CLASS (klass);
  GstVideoAggregatorClass *vagg_class = GST_VIDEO_AGGREGATOR_CLASS (klass);
  GstCaps *caps;

  gobject_class->dispose = gst_d3d11_compositor_dispose;
  gobject_class->set_property = gst_d3d11_compositor_set_property;
  gobject_class->get_property = gst_d3d11_compositor_get_property;

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

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_release_pad);
  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_set_context);

  aggregator_class->start = GST_DEBUG_FUNCPTR (gst_d3d11_compositor_start);
  aggregator_class->stop = GST_DEBUG_FUNCPTR (gst_d3d11_compositor_stop);
  aggregator_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_sink_query);
  aggregator_class->src_query =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_src_query);
  aggregator_class->fixate_src_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_fixate_src_caps);
  aggregator_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_propose_allocation);
  aggregator_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_decide_allocation);

  vagg_class->aggregate_frames =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_aggregate_frames);
  vagg_class->create_output_buffer =
      GST_DEBUG_FUNCPTR (gst_d3d11_compositor_create_output_buffer);

  caps = gst_d3d11_get_updated_template_caps (&pad_template_caps);
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new_with_gtype ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
          caps, GST_TYPE_D3D11_COMPOSITOR_PAD));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new_with_gtype ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          caps, GST_TYPE_AGGREGATOR_PAD));
  gst_caps_unref (caps);

  gst_element_class_set_static_metadata (element_class, "Direct3D11 Compositor",
      "Filter/Editor/Video/Compositor",
      "A Direct3D11 compositor", "Seungha Yang <seungha@centricular.com>");

  gst_type_mark_as_plugin_api (GST_TYPE_D3D11_COMPOSITOR_BACKGROUND,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_D3D11_COMPOSITOR_PAD,
      (GstPluginAPIFlags) 0);
}

static void
gst_d3d11_compositor_init (GstD3D11Compositor * self)
{
  self->adapter = DEFAULT_ADAPTER;
  self->background = DEFAULT_BACKGROUND;
}

static void
gst_d3d11_compositor_dispose (GObject * object)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (object);

  gst_clear_object (&self->device);
  gst_clear_buffer (&self->fallback_buf);
  gst_clear_object (&self->fallback_pool);
  g_clear_pointer (&self->checker_background, gst_d3d11_quad_free);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_d3d11_compositor_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      self->adapter = g_value_get_int (value);
      break;
    case PROP_BACKGROUND:
      self->background =
          (GstD3D11CompositorBackground) g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_compositor_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, self->adapter);
      break;
    case PROP_BACKGROUND:
      g_value_set_enum (value, self->background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GObject *
gst_d3d11_compositor_child_proxy_get_child_by_index (GstChildProxy * proxy,
    guint index)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (proxy);
  GObject *obj = NULL;

  GST_OBJECT_LOCK (self);
  obj = (GObject *) g_list_nth_data (GST_ELEMENT_CAST (self)->sinkpads, index);
  if (obj)
    gst_object_ref (obj);
  GST_OBJECT_UNLOCK (self);

  return obj;
}

static guint
gst_d3d11_compositor_child_proxy_get_children_count (GstChildProxy * proxy)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (proxy);
  guint count = 0;

  GST_OBJECT_LOCK (self);
  count = GST_ELEMENT_CAST (self)->numsinkpads;
  GST_OBJECT_UNLOCK (self);
  GST_INFO_OBJECT (self, "Children Count: %d", count);

  return count;
}

static void
gst_d3d11_compositor_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index =
      gst_d3d11_compositor_child_proxy_get_child_by_index;
  iface->get_children_count =
      gst_d3d11_compositor_child_proxy_get_children_count;
}

static GstPad *
gst_d3d11_compositor_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstPad *pad;

  pad = GST_ELEMENT_CLASS (parent_class)->request_new_pad (element,
      templ, name, caps);

  if (pad == NULL)
    goto could_not_create;

  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_DEBUG_OBJECT (element, "Created new pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  return pad;

could_not_create:
  {
    GST_DEBUG_OBJECT (element, "could not create/add pad");
    return NULL;
  }
}

static gboolean
gst_d3d11_compositor_pad_clear_resource (GstD3D11Compositor * self,
    GstD3D11CompositorPad * cpad, gpointer user_data)
{
  gst_clear_buffer (&cpad->fallback_buf);
  if (cpad->fallback_pool) {
    gst_buffer_pool_set_active (cpad->fallback_pool, FALSE);
    gst_clear_object (&cpad->fallback_pool);
  }
  g_clear_pointer (&cpad->convert, gst_d3d11_converter_free);
  GST_D3D11_CLEAR_COM (cpad->blend);

  return TRUE;
}

static void
gst_d3d11_compositor_release_pad (GstElement * element, GstPad * pad)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (element);
  GstD3D11CompositorPad *cpad = GST_D3D11_COMPOSITOR_PAD (pad);

  GST_DEBUG_OBJECT (self, "Releasing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  gst_child_proxy_child_removed (GST_CHILD_PROXY (self), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  gst_d3d11_compositor_pad_clear_resource (self, cpad, NULL);

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);
}

static void
gst_d3d11_compositor_set_context (GstElement * element, GstContext * context)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (element);

  gst_d3d11_handle_set_context (element, context, self->adapter, &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_compositor_start (GstAggregator * aggregator)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (aggregator);

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self),
          self->adapter, &self->device)) {
    GST_ERROR_OBJECT (self, "Failed to get D3D11 device");
    return FALSE;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->start (aggregator);
}

static gboolean
gst_d3d11_compositor_stop (GstAggregator * aggregator)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (aggregator);

  g_clear_pointer (&self->checker_background, gst_d3d11_quad_free);
  gst_clear_object (&self->device);

  return GST_AGGREGATOR_CLASS (parent_class)->stop (aggregator);
}

static GstCaps *
gst_d3d11_compositor_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *sinkcaps;
  GstCaps *template_caps;
  GstCaps *filtered_caps;
  GstCaps *returned_caps;

  template_caps = gst_pad_get_pad_template_caps (pad);

  sinkcaps = gst_pad_get_current_caps (pad);
  if (sinkcaps == NULL) {
    sinkcaps = gst_caps_ref (template_caps);
  } else {
    sinkcaps = gst_caps_merge (sinkcaps, gst_caps_ref (template_caps));
  }

  if (filter) {
    filtered_caps = gst_caps_intersect (sinkcaps, filter);
    gst_caps_unref (sinkcaps);
  } else {
    filtered_caps = sinkcaps;   /* pass ownership */
  }

  returned_caps = gst_caps_intersect (filtered_caps, template_caps);

  gst_caps_unref (template_caps);
  gst_caps_unref (filtered_caps);

  GST_DEBUG_OBJECT (pad, "returning %" GST_PTR_FORMAT, returned_caps);

  return returned_caps;
}

static gboolean
gst_d3d11_compositor_sink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret;
  GstCaps *template_caps;

  GST_DEBUG_OBJECT (pad, "try accept caps of %" GST_PTR_FORMAT, caps);

  template_caps = gst_pad_get_pad_template_caps (pad);
  template_caps = gst_caps_make_writable (template_caps);

  ret = gst_caps_can_intersect (caps, template_caps);
  GST_DEBUG_OBJECT (pad, "%saccepted caps %" GST_PTR_FORMAT,
      (ret ? "" : "not "), caps);
  gst_caps_unref (template_caps);

  return ret;
}

static gboolean
gst_d3d11_compositor_sink_query (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstQuery * query)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (aggregator);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      gboolean ret;
      ret = gst_d3d11_handle_context_query (GST_ELEMENT (aggregator), query,
          self->device);
      if (ret)
        return TRUE;
      break;
    }
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_d3d11_compositor_sink_getcaps (GST_PAD (pad), filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      gboolean ret;

      gst_query_parse_accept_caps (query, &caps);
      ret = gst_d3d11_compositor_sink_acceptcaps (GST_PAD (pad), caps);
      gst_query_set_accept_caps_result (query, ret);
      return TRUE;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (aggregator,
      pad, query);
}

static gboolean
gst_d3d11_compositor_src_query (GstAggregator * aggregator, GstQuery * query)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (aggregator);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      gboolean ret;
      ret = gst_d3d11_handle_context_query (GST_ELEMENT (aggregator), query,
          self->device);
      if (ret)
        return TRUE;
      break;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (aggregator, query);
}

static GstCaps *
gst_d3d11_compositor_fixate_src_caps (GstAggregator * aggregator,
    GstCaps * caps)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (aggregator);
  GList *l;
  gint best_width = -1, best_height = -1;
  gint best_fps_n = -1, best_fps_d = -1;
  gint par_n, par_d;
  gdouble best_fps = 0.;
  GstCaps *ret = NULL;
  GstStructure *s;

  ret = gst_caps_make_writable (caps);

  /* we need this to calculate how large to make the output frame */
  s = gst_caps_get_structure (ret, 0);
  if (gst_structure_has_field (s, "pixel-aspect-ratio")) {
    gst_structure_fixate_field_nearest_fraction (s, "pixel-aspect-ratio", 1, 1);
    gst_structure_get_fraction (s, "pixel-aspect-ratio", &par_n, &par_d);
  } else {
    par_n = par_d = 1;
  }

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *vaggpad = GST_VIDEO_AGGREGATOR_PAD (l->data);
    GstD3D11CompositorPad *cpad = GST_D3D11_COMPOSITOR_PAD (vaggpad);
    gint this_width, this_height;
    gint width, height;
    gint fps_n, fps_d;
    gdouble cur_fps;

    fps_n = GST_VIDEO_INFO_FPS_N (&vaggpad->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&vaggpad->info);
    gst_d3d11_compositor_pad_get_output_size (cpad,
        par_n, par_d, &width, &height);

    if (width == 0 || height == 0)
      continue;

    this_width = width + MAX (cpad->xpos, 0);
    this_height = height + MAX (cpad->ypos, 0);

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;

    if (fps_d == 0)
      cur_fps = 0.0;
    else
      gst_util_fraction_to_double (fps_n, fps_d, &cur_fps);

    if (best_fps < cur_fps) {
      best_fps = cur_fps;
      best_fps_n = fps_n;
      best_fps_d = fps_d;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  if (best_fps_n <= 0 || best_fps_d <= 0 || best_fps == 0.0) {
    best_fps_n = 25;
    best_fps_d = 1;
    best_fps = 25.0;
  }

  gst_structure_fixate_field_nearest_int (s, "width", best_width);
  gst_structure_fixate_field_nearest_int (s, "height", best_height);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", best_fps_n,
      best_fps_d);
  ret = gst_caps_fixate (ret);

  GST_LOG_OBJECT (aggregator, "Fixated caps %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_d3d11_compositor_propose_allocation (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (aggregator);
  GstVideoInfo info;
  GstBufferPool *pool;
  GstCaps *caps;
  guint size;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstD3D11AllocationParams *d3d11_params;

    d3d11_params = gst_d3d11_allocation_params_new (self->device, &info,
        (GstD3D11AllocationFlags) 0, D3D11_BIND_SHADER_RESOURCE);

    pool = gst_d3d11_buffer_pool_new_with_options (self->device,
        caps, d3d11_params, 0, 0);
    gst_d3d11_allocation_params_free (d3d11_params);

    if (!pool) {
      GST_ERROR_OBJECT (self, "Failed to create buffer pool");
      return FALSE;
    }

    /* d3d11 buffer pool might update buffer size by self */
    size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_d3d11_compositor_decide_allocation (GstAggregator * aggregator,
    GstQuery * query)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (aggregator);
  GstCaps *caps;
  GstBufferPool *pool = NULL;
  guint n, size, min, max;
  GstVideoInfo info;
  GstStructure *config;
  GstD3D11AllocationParams *d3d11_params;

  gst_query_parse_allocation (query, &caps, NULL);

  if (!caps) {
    GST_DEBUG_OBJECT (self, "No output caps");
    return FALSE;
  }

  gst_video_info_from_caps (&info, caps);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  /* create our own pool */
  if (pool) {
    if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_clear_object (&pool);
    } else {
      GstD3D11BufferPool *dpool = GST_D3D11_BUFFER_POOL (pool);
      if (dpool->device != self->device)
        gst_clear_object (&pool);
    }
  }

  if (!pool) {
    pool = gst_d3d11_buffer_pool_new (self->device);

    min = max = 0;
    size = (guint) info.size;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!d3d11_params) {
    d3d11_params = gst_d3d11_allocation_params_new (self->device,
        &info, (GstD3D11AllocationFlags) 0, D3D11_BIND_RENDER_TARGET);
  } else {
    guint i;

    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
      d3d11_params->desc[i].BindFlags |= D3D11_BIND_RENDER_TARGET;
    }
  }

  gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
  gst_d3d11_allocation_params_free (d3d11_params);

  gst_buffer_pool_set_config (pool, config);
  /* d3d11 buffer pool might update buffer size by self */
  size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;

  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

  self->reconfigured = TRUE;

  return TRUE;
}

typedef struct
{
  struct
  {
    FLOAT x;
    FLOAT y;
    FLOAT z;
  } position;
  struct
  {
    FLOAT u;
    FLOAT v;
  } texture;
} VertexData;

static GstD3D11Quad *
gst_d3d11_compositor_create_checker_quad (GstD3D11Compositor * self)
{
  GstD3D11Quad *quad = NULL;
  VertexData *vertex_data;
  WORD *indices;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *context_handle;
  D3D11_MAPPED_SUBRESOURCE map;
  D3D11_INPUT_ELEMENT_DESC input_desc;
  D3D11_BUFFER_DESC buffer_desc;
  /* *INDENT-OFF* */
  ComPtr<ID3D11Buffer> vertex_buffer;
  ComPtr<ID3D11Buffer> index_buffer;
  ComPtr<ID3D11PixelShader> ps;
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11InputLayout> layout;
  /* *INDENT-ON* */
  HRESULT hr;

  device_handle = gst_d3d11_device_get_device_handle (self->device);
  context_handle = gst_d3d11_device_get_device_context_handle (self->device);

  if (!gst_d3d11_create_pixel_shader (self->device, checker_ps_src, &ps)) {
    GST_ERROR_OBJECT (self, "Couldn't setup pixel shader");
    return NULL;
  }

  memset (&input_desc, 0, sizeof (D3D11_INPUT_ELEMENT_DESC));
  input_desc.SemanticName = "POSITION";
  input_desc.SemanticIndex = 0;
  input_desc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
  input_desc.InputSlot = 0;
  input_desc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc.InstanceDataStepRate = 0;

  if (!gst_d3d11_create_vertex_shader (self->device, checker_vs_src,
          &input_desc, 1, &vs, &layout)) {
    GST_ERROR_OBJECT (self, "Couldn't setup vertex shader");
    return NULL;
  }

  memset (&buffer_desc, 0, sizeof (D3D11_BUFFER_DESC));
  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (VertexData) * 4;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, NULL, &vertex_buffer);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self,
        "Couldn't create vertex buffer, hr: 0x%x", (guint) hr);
    return NULL;
  }

  hr = context_handle->Map (vertex_buffer.Get (),
      0, D3D11_MAP_WRITE_DISCARD, 0, &map);

  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    return NULL;
  }

  vertex_data = (VertexData *) map.pData;
  /* bottom left */
  /* bottom left */
  vertex_data[0].position.x = -1.0f;
  vertex_data[0].position.y = -1.0f;
  vertex_data[0].position.z = 0.0f;
  vertex_data[0].texture.u = 0.0f;
  vertex_data[0].texture.v = 1.0f;

  /* top left */
  vertex_data[1].position.x = -1.0f;
  vertex_data[1].position.y = 1.0f;
  vertex_data[1].position.z = 0.0f;
  vertex_data[1].texture.u = 0.0f;
  vertex_data[1].texture.v = 0.0f;

  /* top right */
  vertex_data[2].position.x = 1.0f;
  vertex_data[2].position.y = 1.0f;
  vertex_data[2].position.z = 0.0f;
  vertex_data[2].texture.u = 1.0f;
  vertex_data[2].texture.v = 0.0f;

  /* bottom right */
  vertex_data[3].position.x = 1.0f;
  vertex_data[3].position.y = -1.0f;
  vertex_data[3].position.z = 0.0f;
  vertex_data[3].texture.u = 1.0f;
  vertex_data[3].texture.v = 1.0f;

  context_handle->Unmap (vertex_buffer.Get (), 0);

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (WORD) * 6;
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = device_handle->CreateBuffer (&buffer_desc, NULL, &index_buffer);
  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self,
        "Couldn't create index buffer, hr: 0x%x", (guint) hr);
    return NULL;
  }

  hr = context_handle->Map (index_buffer.Get (),
      0, D3D11_MAP_WRITE_DISCARD, 0, &map);

  if (!gst_d3d11_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't map index buffer, hr: 0x%x", (guint) hr);
    return NULL;
  }

  indices = (WORD *) map.pData;

  /* clockwise indexing */
  indices[0] = 0;               /* bottom left */
  indices[1] = 1;               /* top left */
  indices[2] = 2;               /* top right */

  indices[3] = 3;               /* bottom right */
  indices[4] = 0;               /* bottom left  */
  indices[5] = 2;               /* top right */

  context_handle->Unmap (index_buffer.Get (), 0);

  quad = gst_d3d11_quad_new (self->device,
      ps.Get (), vs.Get (), layout.Get (), nullptr, 0,
      vertex_buffer.Get (), sizeof (VertexData), index_buffer.Get (),
      DXGI_FORMAT_R16_UINT, 6);
  if (!quad) {
    GST_ERROR_OBJECT (self, "Couldn't setup quad");
    return NULL;
  }

  return quad;
}

static gboolean
gst_d3d11_compositor_draw_background_checker (GstD3D11Compositor * self,
    ID3D11RenderTargetView * rtv)
{
  if (!self->checker_background) {
    GstVideoInfo *info = &GST_VIDEO_AGGREGATOR_CAST (self)->info;

    self->checker_background = gst_d3d11_compositor_create_checker_quad (self);

    if (!self->checker_background)
      return FALSE;

    self->viewport.TopLeftX = 0;
    self->viewport.TopLeftY = 0;
    self->viewport.Width = GST_VIDEO_INFO_WIDTH (info);
    self->viewport.Height = GST_VIDEO_INFO_HEIGHT (info);
    self->viewport.MinDepth = 0.0f;
    self->viewport.MaxDepth = 1.0f;
  }

  return gst_d3d11_draw_quad_unlocked (self->checker_background,
      &self->viewport, 1, NULL, 0, &rtv, 1, NULL, NULL, NULL, 0);
}

/* Must be called with d3d11 device lock */
static gboolean
gst_d3d11_compositor_draw_background (GstD3D11Compositor * self,
    ID3D11RenderTargetView * rtv)
{
  ID3D11DeviceContext *device_context =
      gst_d3d11_device_get_device_context_handle (self->device);
  FLOAT rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

  switch (self->background) {
    case GST_D3D11_COMPOSITOR_BACKGROUND_CHECKER:
      return gst_d3d11_compositor_draw_background_checker (self, rtv);
    case GST_D3D11_COMPOSITOR_BACKGROUND_BLACK:
      /* {0, 0, 0, 1} */
      break;
    case GST_D3D11_COMPOSITOR_BACKGROUND_WHITE:
      rgba[0] = 1.0f;
      rgba[1] = 1.0f;
      rgba[2] = 1.0f;
      break;
    case GST_D3D11_COMPOSITOR_BACKGROUND_TRANSPARENT:
      rgba[3] = 0.0f;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  device_context->ClearRenderTargetView (rtv, rgba);

  return TRUE;
}

static GstFlowReturn
gst_d3d11_compositor_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (vagg);
  GList *iter;
  GstBuffer *target_buf = outbuf;
  gboolean need_copy = FALSE;
  gboolean do_device_copy = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;
  ID3D11RenderTargetView *rtv[GST_VIDEO_MAX_PLANES] = { NULL, };
  guint i, j;
  gint view_idx;

  /* Use fallback buffer when output buffer is:
   * - non-d3d11 memory
   * - or, from different d3d11 device
   * - or not bound to render target
   */
  if (!gst_d3d11_compositor_check_d3d11_memory (self,
          outbuf, FALSE, &do_device_copy) || !do_device_copy) {
    if (!gst_d3d11_compsitor_prepare_fallback_buffer (self, &vagg->info, FALSE,
            &self->fallback_pool, &self->fallback_buf)) {
      GST_ERROR_OBJECT (self, "Couldn't prepare fallback buffer");
      return GST_FLOW_ERROR;
    }

    GST_TRACE_OBJECT (self, "Will draw on fallback texture");

    need_copy = TRUE;
    target_buf = self->fallback_buf;
  }

  view_idx = 0;
  for (i = 0; i < gst_buffer_n_memory (target_buf); i++) {
    GstMemory *mem = gst_buffer_peek_memory (target_buf, i);
    GstD3D11Memory *dmem;
    guint rtv_size;

    if (!gst_is_d3d11_memory (mem)) {
      GST_ERROR_OBJECT (self, "Invalid output memory");
      return GST_FLOW_ERROR;
    }

    dmem = (GstD3D11Memory *) mem;
    rtv_size = gst_d3d11_memory_get_render_target_view_size (dmem);
    if (!rtv_size) {
      GST_ERROR_OBJECT (self, "Render target view is unavailable");
      return GST_FLOW_ERROR;
    }

    for (j = 0; j < rtv_size; j++) {
      g_assert (view_idx < GST_VIDEO_MAX_PLANES);

      rtv[view_idx] = gst_d3d11_memory_get_render_target_view (dmem, j);
      view_idx++;
    }

    /* Mark need-download for fallback buffer use case */
    GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
  }

  gst_d3d11_device_lock (self->device);
  /* XXX: the number of render target view must be one here, since we support
   * only RGBA or BGRA */
  if (!gst_d3d11_compositor_draw_background (self, rtv[0])) {
    GST_ERROR_OBJECT (self, "Couldn't draw background");
    gst_d3d11_device_unlock (self->device);
    ret = GST_FLOW_ERROR;
    goto done;
  }

  GST_OBJECT_LOCK (self);

  for (iter = GST_ELEMENT (vagg)->sinkpads; iter; iter = g_list_next (iter)) {
    GstVideoAggregatorPad *pad = GST_VIDEO_AGGREGATOR_PAD (iter->data);
    GstD3D11CompositorPad *cpad = GST_D3D11_COMPOSITOR_PAD (pad);
    GstVideoFrame *prepared_frame =
        gst_video_aggregator_pad_get_prepared_frame (pad);
    ID3D11ShaderResourceView *srv[GST_VIDEO_MAX_PLANES] = { NULL, };
    GstBuffer *buffer;

    if (!prepared_frame)
      continue;

    if (!gst_d3d11_compositor_pad_setup_converter (pad, vagg)) {
      GST_ERROR_OBJECT (self, "Couldn't setup converter");
      ret = GST_FLOW_ERROR;
      break;
    }

    buffer = prepared_frame->buffer;

    view_idx = 0;
    for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
      GstD3D11Memory *dmem =
          (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
      guint srv_size = gst_d3d11_memory_get_shader_resource_view_size (dmem);

      for (j = 0; j < srv_size; j++) {
        g_assert (view_idx < GST_VIDEO_MAX_PLANES);

        srv[view_idx] = gst_d3d11_memory_get_shader_resource_view (dmem, j);
        view_idx++;
      }
    }

    if (!gst_d3d11_converter_convert_unlocked (cpad->convert, srv, rtv,
            cpad->blend, cpad->blend_factor)) {
      GST_ERROR_OBJECT (self, "Couldn't convert frame");
      ret = GST_FLOW_ERROR;
      break;
    }
  }

  self->reconfigured = FALSE;
  GST_OBJECT_UNLOCK (self);
  gst_d3d11_device_unlock (self->device);

  if (ret != GST_FLOW_OK)
    goto done;

  if (need_copy && !gst_d3d11_compositor_copy_buffer (self, &vagg->info,
          target_buf, outbuf, do_device_copy)) {
    GST_ERROR_OBJECT (self, "Couldn't copy input buffer to fallback buffer");
    ret = GST_FLOW_ERROR;
  }

done:
  gst_clear_buffer (&self->fallback_buf);

  return ret;
}

typedef struct
{
  /* without holding ref */
  GstD3D11Device *other_device;
  gboolean have_same_device;
} DeviceCheckData;

static gboolean
gst_d3d11_compositor_check_device_update (GstElement * agg,
    GstVideoAggregatorPad * vpad, DeviceCheckData * data)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (agg);
  GstBuffer *buf;
  GstMemory *mem;
  GstD3D11Memory *dmem;
  gboolean update_device = FALSE;

  buf = gst_video_aggregator_pad_get_current_buffer (vpad);
  if (!buf)
    return TRUE;

  mem = gst_buffer_peek_memory (buf, 0);
  /* FIXME: we should be able to accept non-d3d11 memory later once
   * we remove intermediate elements (d3d11upload and d3d11colorconvert)
   */
  if (!gst_is_d3d11_memory (mem)) {
    GST_ELEMENT_ERROR (agg, CORE, FAILED, (NULL), ("Invalid memory"));
    return FALSE;
  }

  dmem = GST_D3D11_MEMORY_CAST (mem);

  /* We can use existing device */
  if (dmem->device == self->device) {
    data->have_same_device = TRUE;
    return FALSE;
  }

  if (self->adapter < 0) {
    update_device = TRUE;
  } else {
    guint adapter = 0;

    g_object_get (dmem->device, "adapter", &adapter, NULL);
    /* The same GPU as what user wanted, update */
    if (adapter == (guint) self->adapter)
      update_device = TRUE;
  }

  if (!update_device)
    return TRUE;

  data->other_device = dmem->device;

  /* Keep iterate since there might be one buffer which holds the same device
   * as ours */
  return TRUE;
}

static GstFlowReturn
gst_d3d11_compositor_create_output_buffer (GstVideoAggregator * vagg,
    GstBuffer ** outbuffer)
{
  GstD3D11Compositor *self = GST_D3D11_COMPOSITOR (vagg);
  DeviceCheckData data;

  /* Check whether there is at least one sinkpad which holds d3d11 buffer
   * with compatible device, and if not, update our device */
  data.other_device = NULL;
  data.have_same_device = FALSE;

  gst_element_foreach_sink_pad (GST_ELEMENT_CAST (vagg),
      (GstElementForeachPadFunc) gst_d3d11_compositor_check_device_update,
      &data);

  if (data.have_same_device || !data.other_device)
    goto done;

  /* Clear all device dependent resources */
  gst_element_foreach_sink_pad (GST_ELEMENT_CAST (vagg),
      (GstElementForeachPadFunc) gst_d3d11_compositor_pad_clear_resource, NULL);

  gst_clear_buffer (&self->fallback_buf);
  if (self->fallback_pool) {
    gst_buffer_pool_set_active (self->fallback_pool, FALSE);
    gst_clear_object (&self->fallback_pool);
  }
  g_clear_pointer (&self->checker_background, gst_d3d11_quad_free);

  GST_INFO_OBJECT (self, "Updating device %" GST_PTR_FORMAT " -> %"
      GST_PTR_FORMAT, self->device, data.other_device);
  gst_object_unref (self->device);
  self->device = (GstD3D11Device *) gst_object_ref (data.other_device);

  /* We cannot call gst_aggregator_negotiate() here, since GstVideoAggregator
   * is holding GST_VIDEO_AGGREGATOR_LOCK() already.
   * Mark reconfigure and do reconfigure later */
  gst_pad_mark_reconfigure (GST_AGGREGATOR_SRC_PAD (vagg));

  return GST_AGGREGATOR_FLOW_NEED_DATA;

done:
  return GST_VIDEO_AGGREGATOR_CLASS (parent_class)->create_output_buffer (vagg,
      outbuffer);
}
