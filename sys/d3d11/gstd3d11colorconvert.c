/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005-2012 David Schleef <ds@schleef.org>
 * Copyright (C) 2012-2014 Matthew Waters <ystree00@gmail.com>
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) <2019> Jeongki Kim <jeongki.kim@jeongki.kim>
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
 * SECTION:element-d3d11colorconvert
 * @title: d3d11colorconvert
 *
 * Convert video frames between supported video formats.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v videotestsrc ! video/x-raw,format=NV12 ! d3d11upload ! d3d11videocolorconvert ! d3d11videosink
 * ]|
 *  This will output a test video (generated in NV12 format) in a video
 * window. If the video sink selected does not support NV12
 * d3d11colorconvert will automatically convert the video to a format understood
 * by the video sink.
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstd3d11colorconvert.h"
#include "gstd3d11utils.h"
#include "gstd3d11memory.h"
#include "gstd3d11device.h"
#include "gstd3d11bufferpool.h"

#include <string.h>
#include <d3dcompiler.h>

/* *INDENT-OFF* */
typedef struct
{
  FLOAT trans_matrix[12];
  FLOAT padding[4];
} PixelShaderColorTransform;

typedef struct
{
  struct {
    FLOAT x;
    FLOAT y;
    FLOAT z;
  } position;
  struct {
    FLOAT x;
    FLOAT y;
  } texture;
} VertexData;

typedef struct
{
  const gchar *constant_buffer;
  const gchar *func;
} PixelShaderTemplate;

#define COLOR_TRANSFORM_COEFF \
    "cbuffer PixelShaderColorTransform : register(b0)\n" \
    "{\n" \
    "  float3x4 trans_matrix;\n" \
    "  float3 padding;\n" \
    "};\n"

#define HLSL_FUNC_YUV_TO_RGB \
    "float3 yuv_to_rgb (float3 yuv)\n" \
    "{\n" \
    "  yuv += float3(-0.062745f, -0.501960f, -0.501960f);\n" \
    "  yuv = mul(yuv, trans_matrix);\n" \
    "  return saturate(yuv);\n" \
    "}\n"

#define HLSL_FUNC_RGB_TO_YUV \
    "float3 rgb_to_yuv (float3 rgb)\n" \
    "{\n" \
    "  float3 yuv;\n" \
    "  yuv = mul(rgb, trans_matrix);\n" \
    "  yuv += float3(0.062745f, 0.501960f, 0.501960f);\n" \
    "  return saturate(yuv);\n" \
    "}\n"

static const PixelShaderTemplate templ_REORDER =
    { NULL, NULL };

static const PixelShaderTemplate templ_YUV_to_RGB =
    { COLOR_TRANSFORM_COEFF, HLSL_FUNC_YUV_TO_RGB };

#if 0
static const PixelShaderTemplate templ_RGB_to_YUV =
    { COLOR_TRANSFORM_COEFF, HLSL_FUNC_RGB_TO_YUV };
#endif

static const gchar templ_REORDER_BODY[] =
    "  float4 sample;\n"
    "  sample  = shaderTexture[0].Sample(samplerState, input.Texture);\n"
    /* alpha channel */
    "  %s\n"
    "  return sample;\n";

static const gchar templ_VUYA_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).z;\n"
    "  sample.y  = shaderTexture[0].Sample(samplerState, input.Texture).y;\n"
    "  sample.z  = shaderTexture[0].Sample(samplerState, input.Texture).x;\n"
    "  sample.a  = shaderTexture[0].Sample(samplerState, input.Texture).a;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = sample.a;\n"
    "  return rgba;\n";

#if 0
static const gchar templ_RGB_to_VUYA_BODY[] =
    "  float4 sample, vuya;\n"
    "  sample = shaderTexture[0].Sample(samplerState, input.Texture);\n"
    "  vuya.zyx = rgb_to_yuv (sample.rgb);\n"
    "  vuya.a = %s;\n"
    "  return vuya;\n";
#endif

/* YUV to RGB conversion */
static const gchar templ_PLANAR_YUV_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).x;\n"
    "  sample.y  = shaderTexture[1].Sample(samplerState, input.Texture).x;\n"
    "  sample.z  = shaderTexture[2].Sample(samplerState, input.Texture).x;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = 1.0;\n"
    "  return rgba;\n";

static const gchar templ_PLANAR_YUV_HIGH_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).x * %d;\n"
    "  sample.y  = shaderTexture[1].Sample(samplerState, input.Texture).x * %d;\n"
    "  sample.z  = shaderTexture[2].Sample(samplerState, input.Texture).x * %d;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = 1.0;\n"
    "  return rgba;\n";

/* FIXME: add RGB to planar */

static const gchar templ_SEMI_PLANAR_to_RGB_BODY[] =
    "  float4 sample, rgba;\n"
    "  sample.x  = shaderTexture[0].Sample(samplerState, input.Texture).x;\n"
    "  sample.yz = shaderTexture[1].Sample(samplerState, input.Texture).xy;\n"
    "  rgba.rgb = yuv_to_rgb (sample.xyz);\n"
    "  rgba.a = 1.0;\n"
    "  return rgba;\n";

/* FIXME: add RGB to semi-planar */

static const gchar templ_pixel_shader[] =
    /* constant buffer */
    "%s\n"
    "Texture2D shaderTexture[4];\n"
    "SamplerState samplerState;\n"
    "\n"
    "struct PS_INPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "  float3 Texture: TEXCOORD0;\n"
    "};\n"
    "\n"
    /* rgb <-> yuv function */
    "%s\n"
    "float4 main(PS_INPUT input): SV_TARGET\n"
    "{\n"
    "%s"
    "}\n";

static const gchar templ_vertex_shader[] =
    "struct VS_INPUT\n"
    "{\n"
    "  float4 Position : POSITION;\n"
    "  float4 Texture : TEXCOORD0;\n"
    "};\n"
    "\n"
    "struct VS_OUTPUT\n"
    "{\n"
    "  float4 Position: SV_POSITION;\n"
    "  float4 Texture: TEXCOORD0;\n"
    "};\n"
    "\n"
    "VS_OUTPUT main(VS_INPUT input)\n"
    "{\n"
    "  return input;\n"
    "}\n";

/* *INDENT-ON* */

typedef struct
{
  const PixelShaderTemplate *templ;
  gchar *ps_body;
  PixelShaderColorTransform transform;
} ConvertInfo;

struct _GstD3D11ColorConvertPrivate
{
  ConvertInfo convert_info;
};

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_color_convert_debug);
#define GST_CAT_DEFAULT gst_d3d11_color_convert_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_FORMATS))
    );

#define gst_d3d11_color_convert_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D11ColorConvert,
    gst_d3d11_color_convert, GST_TYPE_D3D11_BASE_FILTER);

static void gst_d3d11_color_convert_dispose (GObject * object);
static GstCaps *gst_d3d11_color_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d11_color_convert_fixate_caps (GstBaseTransform *
    base, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_d3d11_color_convert_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params);
static gboolean
gst_d3d11_color_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean
gst_d3d11_color_convert_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);

static GstFlowReturn gst_d3d11_color_convert_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_d3d11_color_convert_set_info (GstD3D11BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);

/* copies the given caps */
static GstCaps *
gst_d3d11_color_convert_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY);

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_is_equal (f, feature))
      gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
          NULL);

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }
  gst_caps_features_free (feature);

  return res;
}

/*
 * This is an incomplete matrix of in formats and a score for the prefered output
 * format.
 *
 *         out: RGB24   RGB16  ARGB  AYUV  YUV444  YUV422 YUV420 YUV411 YUV410  PAL  GRAY
 *  in
 * RGB24          0      2       1     2     2       3      4      5      6      7    8
 * RGB16          1      0       1     2     2       3      4      5      6      7    8
 * ARGB           2      3       0     1     4       5      6      7      8      9    10
 * AYUV           3      4       1     0     2       5      6      7      8      9    10
 * YUV444         2      4       3     1     0       5      6      7      8      9    10
 * YUV422         3      5       4     2     1       0      6      7      8      9    10
 * YUV420         4      6       5     3     2       1      0      7      8      9    10
 * YUV411         4      6       5     3     2       1      7      0      8      9    10
 * YUV410         6      8       7     5     4       3      2      1      0      9    10
 * PAL            1      3       2     6     4       6      7      8      9      0    10
 * GRAY           1      4       3     2     1       5      6      7      8      9    0
 *
 * PAL or GRAY are never prefered, if we can we would convert to PAL instead
 * of GRAY, though
 * less subsampling is prefered and if any, preferably horizontal
 * We would like to keep the alpha, even if we would need to to colorspace conversion
 * or lose depth.
 */
#define SCORE_FORMAT_CHANGE       1
#define SCORE_DEPTH_CHANGE        1
#define SCORE_ALPHA_CHANGE        1
#define SCORE_CHROMA_W_CHANGE     1
#define SCORE_CHROMA_H_CHANGE     1
#define SCORE_PALETTE_CHANGE      1

#define SCORE_COLORSPACE_LOSS     2     /* RGB <-> YUV */
#define SCORE_DEPTH_LOSS          4     /* change bit depth */
#define SCORE_ALPHA_LOSS          8     /* lose the alpha channel */
#define SCORE_CHROMA_W_LOSS      16     /* vertical subsample */
#define SCORE_CHROMA_H_LOSS      32     /* horizontal subsample */
#define SCORE_PALETTE_LOSS       64     /* convert to palette format */
#define SCORE_COLOR_LOSS        128     /* convert to GRAY */

#define COLORSPACE_MASK (GST_VIDEO_FORMAT_FLAG_YUV | \
                         GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_GRAY)
#define ALPHA_MASK      (GST_VIDEO_FORMAT_FLAG_ALPHA)
#define PALETTE_MASK    (GST_VIDEO_FORMAT_FLAG_PALETTE)

/* calculate how much loss a conversion would be */
static void
score_value (GstBaseTransform * base, const GstVideoFormatInfo * in_info,
    const GValue * val, gint * min_loss, const GstVideoFormatInfo ** out_info)
{
  const gchar *fname;
  const GstVideoFormatInfo *t_info;
  GstVideoFormatFlags in_flags, t_flags;
  gint loss;

  fname = g_value_get_string (val);
  t_info = gst_video_format_get_info (gst_video_format_from_string (fname));
  if (!t_info)
    return;

  /* accept input format immediately without loss */
  if (in_info == t_info) {
    *min_loss = 0;
    *out_info = t_info;
    return;
  }

  loss = SCORE_FORMAT_CHANGE;

  in_flags = GST_VIDEO_FORMAT_INFO_FLAGS (in_info);
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  t_flags = GST_VIDEO_FORMAT_INFO_FLAGS (t_info);
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  if ((t_flags & PALETTE_MASK) != (in_flags & PALETTE_MASK)) {
    loss += SCORE_PALETTE_CHANGE;
    if (t_flags & PALETTE_MASK)
      loss += SCORE_PALETTE_LOSS;
  }

  if ((t_flags & COLORSPACE_MASK) != (in_flags & COLORSPACE_MASK)) {
    loss += SCORE_COLORSPACE_LOSS;
    if (t_flags & GST_VIDEO_FORMAT_FLAG_GRAY)
      loss += SCORE_COLOR_LOSS;
  }

  if ((t_flags & ALPHA_MASK) != (in_flags & ALPHA_MASK)) {
    loss += SCORE_ALPHA_CHANGE;
    if (in_flags & ALPHA_MASK)
      loss += SCORE_ALPHA_LOSS;
  }

  if ((in_info->h_sub[1]) != (t_info->h_sub[1])) {
    loss += SCORE_CHROMA_H_CHANGE;
    if ((in_info->h_sub[1]) < (t_info->h_sub[1]))
      loss += SCORE_CHROMA_H_LOSS;
  }
  if ((in_info->w_sub[1]) != (t_info->w_sub[1])) {
    loss += SCORE_CHROMA_W_CHANGE;
    if ((in_info->w_sub[1]) < (t_info->w_sub[1]))
      loss += SCORE_CHROMA_W_LOSS;
  }

  if ((in_info->bits) != (t_info->bits)) {
    loss += SCORE_DEPTH_CHANGE;
    if ((in_info->bits) > (t_info->bits))
      loss += SCORE_DEPTH_LOSS + (in_info->bits - t_info->bits);
  }

  GST_DEBUG_OBJECT (base, "score %s -> %s = %d",
      GST_VIDEO_FORMAT_INFO_NAME (in_info),
      GST_VIDEO_FORMAT_INFO_NAME (t_info), loss);

  if (loss < *min_loss) {
    GST_DEBUG_OBJECT (base, "found new best %d", loss);
    *out_info = t_info;
    *min_loss = loss;
  }
}

static void
gst_d3d11_color_convert_class_init (GstD3D11ColorConvertClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstD3D11BaseFilterClass *bfilter_class = GST_D3D11_BASE_FILTER_CLASS (klass);

  gobject_class->dispose = gst_d3d11_color_convert_dispose;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 Colorspace converter",
      "Filter/Converter/Video/Hardware",
      "Converts video from one colorspace to another using D3D11",
      "Seungha Yang <seungha.yang@navercorp.com>, "
      "Jeongki Kim <jeongki.kim@jeongki.kim>");

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_fixate_caps);
  trans_class->filter_meta =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_filter_meta);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_decide_allocation);
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_transform);

  bfilter_class->set_info =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_set_info);

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_color_convert_debug,
      "d3d11colorconvert", 0, "Video Colorspace Convert via D3D11");
}

static void
gst_d3d11_color_convert_init (GstD3D11ColorConvert * self)
{
  self->priv = gst_d3d11_color_convert_get_instance_private (self);
}

static void
clear_shader_resource (GstD3D11Device * device, GstD3D11ColorConvert * self)
{
  gint i;

  if (self->sampler) {
    ID3D11SamplerState_Release (self->sampler);
    self->sampler = NULL;
  }

  if (self->pixel_shader) {
    ID3D11PixelShader_Release (self->pixel_shader);
    self->pixel_shader = NULL;
  }

  if (self->vertex_shader) {
    ID3D11VertexShader_Release (self->vertex_shader);
    self->vertex_shader = NULL;
  }

  if (self->layout) {
    ID3D11InputLayout_Release (self->layout);
    self->layout = NULL;
  }

  if (self->const_buffer) {
    ID3D11Buffer_Release (self->const_buffer);
    self->const_buffer = NULL;
  }

  if (self->vertex_buffer) {
    ID3D11Buffer_Release (self->vertex_buffer);
    self->vertex_buffer = NULL;
  }

  if (self->index_buffer) {
    ID3D11Buffer_Release (self->index_buffer);
    self->index_buffer = NULL;
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (self->shader_resource_view[i]) {
      ID3D11ShaderResourceView_Release (self->shader_resource_view[i]);
      self->shader_resource_view[i] = NULL;
    }

    if (self->render_target_view[i]) {
      ID3D11RenderTargetView_Release (self->render_target_view[i]);
      self->render_target_view[i] = NULL;
    }
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (self->in_texture[i]) {
      ID3D11Texture2D_Release (self->in_texture[i]);
      self->in_texture[i] = NULL;
    }

    if (self->out_texture[i]) {
      ID3D11Texture2D_Release (self->out_texture[i]);
      self->out_texture[i] = NULL;
    }
  }
}

static void
gst_d3d11_color_convert_clear_shader_resource (GstD3D11ColorConvert * self)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (self);

  if (filter->device) {
    gst_d3d11_device_thread_add (filter->device,
        (GstD3D11DeviceThreadFunc) clear_shader_resource, self);
  }
}

static void
gst_d3d11_color_convert_dispose (GObject * object)
{
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (object);

  gst_d3d11_color_convert_clear_shader_resource (self);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_d3d11_color_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_d3d11_color_convert_caps_remove_format_info (caps);

  if (filter) {
    tmp2 = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
    tmp = tmp2;
  }

  result = tmp;

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

/* fork of gstvideoconvert */
static void
gst_d3d11_color_convert_fixate_format (GstBaseTransform * base,
    GstCaps * caps, GstCaps * result)
{
  GstStructure *ins, *outs;
  const gchar *in_format;
  const GstVideoFormatInfo *in_info, *out_info = NULL;
  gint min_loss = G_MAXINT;
  guint i, capslen;

  ins = gst_caps_get_structure (caps, 0);
  in_format = gst_structure_get_string (ins, "format");
  if (!in_format)
    return;

  GST_DEBUG_OBJECT (base, "source format %s", in_format);

  in_info =
      gst_video_format_get_info (gst_video_format_from_string (in_format));
  if (!in_info)
    return;

  outs = gst_caps_get_structure (result, 0);

  capslen = gst_caps_get_size (result);
  GST_DEBUG_OBJECT (base, "iterate %d structures", capslen);
  for (i = 0; i < capslen; i++) {
    GstStructure *tests;
    const GValue *format;

    tests = gst_caps_get_structure (result, i);
    format = gst_structure_get_value (tests, "format");
    /* should not happen */
    if (format == NULL)
      continue;

    if (GST_VALUE_HOLDS_LIST (format)) {
      gint j, len;

      len = gst_value_list_get_size (format);
      GST_DEBUG_OBJECT (base, "have %d formats", len);
      for (j = 0; j < len; j++) {
        const GValue *val;

        val = gst_value_list_get_value (format, j);
        if (G_VALUE_HOLDS_STRING (val)) {
          score_value (base, in_info, val, &min_loss, &out_info);
          if (min_loss == 0)
            break;
        }
      }
    } else if (G_VALUE_HOLDS_STRING (format)) {
      score_value (base, in_info, format, &min_loss, &out_info);
    }
  }
  if (out_info)
    gst_structure_set (outs, "format", G_TYPE_STRING,
        GST_VIDEO_FORMAT_INFO_NAME (out_info), NULL);
}

static GstCaps *
gst_d3d11_color_convert_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result;

  GST_DEBUG_OBJECT (trans, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  result = gst_caps_intersect (othercaps, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = othercaps;
  } else {
    gst_caps_unref (othercaps);
  }

  GST_DEBUG_OBJECT (trans, "now fixating %" GST_PTR_FORMAT, result);

  result = gst_caps_make_writable (result);
  gst_d3d11_color_convert_fixate_format (trans, caps, result);

  /* fixate remaining fields */
  result = gst_caps_fixate (result);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      gst_caps_replace (&result, caps);
    }
  }

  return result;
}

static gboolean
gst_d3d11_color_convert_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params)
{
  /* This element cannot passthrough the crop meta, because it would convert the
   * wrong sub-region of the image, and worst, our output image may not be large
   * enough for the crop to be applied later */
  if (api == GST_VIDEO_CROP_META_API_TYPE)
    return FALSE;

  /* propose all other metadata upstream */
  return TRUE;
}

static gboolean
gst_d3d11_color_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstCaps *caps;
  guint n_pools, i;
  GstStructure *config;
  guint size;
  GstD3D11AllocationParams *d3d11_params;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (decide_query == NULL)
    return TRUE;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  n_pools = gst_query_get_n_allocation_pools (query);
  for (i = 0; i < n_pools; i++) {
    gst_query_parse_nth_allocation_pool (query, i, &pool, NULL, NULL, NULL);
    if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = NULL;
    }
  }

  if (!pool)
    pool = gst_d3d11_buffer_pool_new (filter->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!d3d11_params)
    d3d11_params = gst_d3d11_allocation_params_new (&info,
        GST_D3D11_ALLOCATION_FLAG_USE_RESOURCE_FORMAT);

  /* Set bind flag */
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
    d3d11_params->desc[i].BindFlags |= D3D11_BIND_SHADER_RESOURCE;
  }

  gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
  gst_d3d11_allocation_params_free (d3d11_params);

  /* size will be updated by d3d11 buffer pool */
  gst_buffer_pool_config_set_params (config, caps, 0, 0, 0);

  if (!gst_buffer_pool_set_config (pool, config))
    goto config_failed;

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;
  gst_query_add_allocation_pool (query, pool, size, 0, 0);

  gst_object_unref (pool);

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (filter, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }
}

static gboolean
gst_d3d11_color_convert_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstCaps *outcaps = NULL;
  GstBufferPool *pool = NULL;
  guint size, min = 0, max = 0;
  GstStructure *config;
  GstD3D11AllocationParams *d3d11_params;
  gboolean update_pool = FALSE;
  GstVideoInfo info;
  gint i;

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, outcaps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool && !GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = NULL;
    }

    update_pool = TRUE;
  }

  if (!pool)
    pool = gst_d3d11_buffer_pool_new (filter->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!d3d11_params)
    d3d11_params = gst_d3d11_allocation_params_new (&info,
        GST_D3D11_ALLOCATION_FLAG_USE_RESOURCE_FORMAT);

  /* Set bind flag */
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
    d3d11_params->desc[i].BindFlags |=
        (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
  }

  gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
  gst_d3d11_allocation_params_free (d3d11_params);

  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

/* from video-converter.c */
typedef struct
{
  gfloat dm[4][4];
} MatrixData;

static void
color_matrix_set_identity (MatrixData * m)
{
  gint i, j;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      m->dm[i][j] = (i == j);
    }
  }
}

static void
color_matrix_copy (MatrixData * d, const MatrixData * s)
{
  gint i, j;

  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++)
      d->dm[i][j] = s->dm[i][j];
}

/* Perform 4x4 matrix multiplication:
 *  - @dst@ = @a@ * @b@
 *  - @dst@ may be a pointer to @a@ andor @b@
 */
static void
color_matrix_multiply (MatrixData * dst, MatrixData * a, MatrixData * b)
{
  MatrixData tmp;
  gint i, j, k;

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 4; j++) {
      gfloat x = 0;
      for (k = 0; k < 4; k++) {
        x += a->dm[i][k] * b->dm[k][j];
      }
      tmp.dm[i][j] = x;
    }
  }
  color_matrix_copy (dst, &tmp);
}

static void
color_matrix_offset_components (MatrixData * m, gfloat a1, gfloat a2, gfloat a3)
{
  MatrixData a;

  color_matrix_set_identity (&a);
  a.dm[0][3] = a1;
  a.dm[1][3] = a2;
  a.dm[2][3] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_scale_components (MatrixData * m, gfloat a1, gfloat a2, gfloat a3)
{
  MatrixData a;

  color_matrix_set_identity (&a);
  a.dm[0][0] = a1;
  a.dm[1][1] = a2;
  a.dm[2][2] = a3;
  color_matrix_multiply (m, &a, m);
}

static void
color_matrix_debug (GstD3D11ColorConvert * self, const MatrixData * s)
{
  GST_DEBUG_OBJECT (self,
      "[%f %f %f %f]", s->dm[0][0], s->dm[0][1], s->dm[0][2], s->dm[0][3]);
  GST_DEBUG_OBJECT (self,
      "[%f %f %f %f]", s->dm[1][0], s->dm[1][1], s->dm[1][2], s->dm[1][3]);
  GST_DEBUG_OBJECT (self,
      "[%f %f %f %f]", s->dm[2][0], s->dm[2][1], s->dm[2][2], s->dm[2][3]);
  GST_DEBUG_OBJECT (self,
      "[%f %f %f %f]", s->dm[3][0], s->dm[3][1], s->dm[3][2], s->dm[3][3]);
}

static void
color_matrix_YCbCr_to_RGB (MatrixData * m, gfloat Kr, gfloat Kb)
{
  gfloat Kg = 1.0 - Kr - Kb;
  MatrixData k = {
    {
          {1., 0., 2 * (1 - Kr), 0.},
          {1., -2 * Kb * (1 - Kb) / Kg, -2 * Kr * (1 - Kr) / Kg, 0.},
          {1., 2 * (1 - Kb), 0., 0.},
          {0., 0., 0., 1.},
        }
  };

  color_matrix_multiply (m, &k, m);
}

static void
color_matrix_RGB_to_YCbCr (MatrixData * m, gfloat Kr, gfloat Kb)
{
  gfloat Kg = 1.0 - Kr - Kb;
  MatrixData k;
  gfloat x;

  k.dm[0][0] = Kr;
  k.dm[0][1] = Kg;
  k.dm[0][2] = Kb;
  k.dm[0][3] = 0;

  x = 1 / (2 * (1 - Kb));
  k.dm[1][0] = -x * Kr;
  k.dm[1][1] = -x * Kg;
  k.dm[1][2] = x * (1 - Kb);
  k.dm[1][3] = 0;

  x = 1 / (2 * (1 - Kr));
  k.dm[2][0] = x * (1 - Kr);
  k.dm[2][1] = -x * Kg;
  k.dm[2][2] = -x * Kb;
  k.dm[2][3] = 0;

  k.dm[3][0] = 0;
  k.dm[3][1] = 0;
  k.dm[3][2] = 0;
  k.dm[3][3] = 1;

  color_matrix_multiply (m, &k, m);
}

static void
compute_matrix_to_RGB (GstD3D11ColorConvert * self, MatrixData * data,
    GstVideoInfo * info)
{
  gdouble Kr = 0, Kb = 0;
  gint offset[4], scale[4];

  /* bring color components to [0..1.0] range */
  gst_video_color_range_offsets (info->colorimetry.range, info->finfo, offset,
      scale);

  color_matrix_offset_components (data, -offset[0], -offset[1], -offset[2]);
  color_matrix_scale_components (data, 1 / ((float) scale[0]),
      1 / ((float) scale[1]), 1 / ((float) scale[2]));

  if (!GST_VIDEO_INFO_IS_RGB (info)) {
    /* bring components to R'G'B' space */
    if (gst_video_color_matrix_get_Kr_Kb (info->colorimetry.matrix, &Kr, &Kb))
      color_matrix_YCbCr_to_RGB (data, Kr, Kb);
  }
  color_matrix_debug (self, data);
}

static void
compute_matrix_to_YUV (GstD3D11ColorConvert * self, MatrixData * data,
    GstVideoInfo * info)
{
  gdouble Kr = 0, Kb = 0;
  gint offset[4], scale[4];

  if (!GST_VIDEO_INFO_IS_RGB (info)) {
    /* bring components to YCbCr space */
    if (gst_video_color_matrix_get_Kr_Kb (info->colorimetry.matrix, &Kr, &Kb))
      color_matrix_RGB_to_YCbCr (data, Kr, Kb);
  }

  /* bring color components to nominal range */
  gst_video_color_range_offsets (info->colorimetry.range, info->finfo, offset,
      scale);

  color_matrix_scale_components (data, (float) scale[0], (float) scale[1],
      (float) scale[2]);
  color_matrix_offset_components (data, offset[0], offset[1], offset[2]);

  color_matrix_debug (self, data);
}

static gboolean
converter_get_matrix (GstD3D11ColorConvert * self, MatrixData * matrix,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  gboolean same_matrix;
  guint in_bits, out_bits;

  in_bits = GST_VIDEO_INFO_COMP_DEPTH (in_info, 0);
  out_bits = GST_VIDEO_INFO_COMP_DEPTH (out_info, 0);

  same_matrix = in_info->colorimetry.matrix == out_info->colorimetry.matrix;

  GST_DEBUG_OBJECT (self, "matrix %d -> %d (%d)", in_info->colorimetry.matrix,
      out_info->colorimetry.matrix, same_matrix);

  color_matrix_set_identity (matrix);

  if (same_matrix) {
    GST_DEBUG ("conversion matrix is not required");
    return FALSE;
  }

  if (in_bits < out_bits) {
    gint scale = 1 << (out_bits - in_bits);
    color_matrix_scale_components (matrix,
        1 / (float) scale, 1 / (float) scale, 1 / (float) scale);
  }

  GST_DEBUG_OBJECT (self, "to RGB matrix");
  compute_matrix_to_RGB (self, matrix, in_info);
  GST_DEBUG_OBJECT (self, "current matrix");
  color_matrix_debug (self, matrix);

  GST_DEBUG_OBJECT (self, "to YUV matrix");
  compute_matrix_to_YUV (self, matrix, out_info);
  GST_DEBUG_OBJECT (self, "current matrix");
  color_matrix_debug (self, matrix);

  if (in_bits > out_bits) {
    gint scale = 1 << (in_bits - out_bits);
    color_matrix_scale_components (matrix,
        (float) scale, (float) scale, (float) scale);
  }

  GST_DEBUG_OBJECT (self, "final matrix");
  color_matrix_debug (self, matrix);

  return TRUE;
}

static gboolean
setup_convert_info_rgb_to_rgb (GstD3D11ColorConvert * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *convert_info = &self->priv->convert_info;
  GstVideoFormat in_format = GST_VIDEO_INFO_FORMAT (in_info);

#define IS_RGBX_FORMAT(f) \
  ((f) == GST_VIDEO_FORMAT_RGBx || \
   (f) == GST_VIDEO_FORMAT_xRGB || \
   (f) == GST_VIDEO_FORMAT_BGRx || \
   (f) == GST_VIDEO_FORMAT_xBGR)

  convert_info->templ = &templ_REORDER;
  convert_info->ps_body = g_strdup_printf (templ_REORDER_BODY,
      IS_RGBX_FORMAT (in_format) ? "sample.a = 1.0f;" : "");

#undef IS_RGBX_FORMAT

  return TRUE;
}

static gboolean
setup_convert_info_yuv_to_rgb (GstD3D11ColorConvert * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  ConvertInfo *info = &self->priv->convert_info;

  info->templ = &templ_YUV_to_RGB;

  switch (GST_VIDEO_INFO_FORMAT (in_info)) {
    case GST_VIDEO_FORMAT_VUYA:
      info->ps_body = g_strdup_printf (templ_VUYA_to_RGB_BODY);
      break;
    case GST_VIDEO_FORMAT_I420:
      info->ps_body = g_strdup_printf (templ_PLANAR_YUV_to_RGB_BODY);
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
      info->ps_body =
          g_strdup_printf (templ_PLANAR_YUV_HIGH_to_RGB_BODY, 64, 64, 64);
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
      info->ps_body = g_strdup_printf (templ_SEMI_PLANAR_to_RGB_BODY);
      break;
    default:
      GST_FIXME_OBJECT (self,
          "Unhandled input format %s",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)));
      return FALSE;
  }

  return TRUE;
}

static gboolean
setup_convert_info_rgb_to_yuv (GstD3D11ColorConvert * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  GST_FIXME_OBJECT (self, "Implement RGB to YUV format conversion");
  return FALSE;
}

static gboolean
setup_convert_info_yuv_to_yuv (GstD3D11ColorConvert * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info)
{
  GST_FIXME_OBJECT (self, "Implement YUV to YUV format conversion");
  return FALSE;
}

/* called in d3d11 device thread */
static ID3DBlob *
compile_shader (GstD3D11ColorConvert * self, const gchar * shader_source,
    gboolean is_pixel_shader)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (self);
  ID3DBlob *ret;
  ID3DBlob *error = NULL;
  const gchar *shader_target;
  D3D_FEATURE_LEVEL feature_level;
  HRESULT hr;

  feature_level = gst_d3d11_device_get_chosen_feature_level (filter->device);

  if (is_pixel_shader) {
    if (feature_level >= D3D_FEATURE_LEVEL_10_0)
      shader_target = "ps_4_0";
    else if (feature_level >= D3D_FEATURE_LEVEL_9_3)
      shader_target = "ps_4_0_level_9_3";
    else
      shader_target = "ps_4_0_level_9_1";
  } else {
    if (feature_level >= D3D_FEATURE_LEVEL_10_0)
      shader_target = "vs_4_0";
    else if (feature_level >= D3D_FEATURE_LEVEL_9_3)
      shader_target = "vs_4_0_level_9_3";
    else
      shader_target = "vs_4_0_level_9_1";
  }

  hr = D3DCompile (shader_source, strlen (shader_source), NULL, NULL, NULL,
      "main", shader_target, 0, 0, &ret, &error);

  if (FAILED (hr)) {
    const gchar *err = NULL;

    if (error)
      err = ID3D10Blob_GetBufferPointer (error);

    GST_ERROR_OBJECT (self,
        "could not compile source, hr: 0x%x, error detail %s",
        (guint) hr, GST_STR_NULL (err));

    if (error)
      ID3D10Blob_Release (error);

    return NULL;
  }

  return ret;
}

static gboolean
create_pixel_shader (GstD3D11ColorConvert * self, const gchar * shader_source,
    ID3D11PixelShader ** shader)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (self);
  ID3DBlob *ps_blob;
  ID3D11Device *device_handle;
  HRESULT hr;
  gboolean ret = TRUE;

  ps_blob = compile_shader (self, shader_source, TRUE);
  if (!ps_blob) {
    GST_ERROR_OBJECT (self, "Failed to compile shader code");
    return FALSE;
  }

  device_handle = gst_d3d11_device_get_device_handle (filter->device);
  hr = ID3D11Device_CreatePixelShader (device_handle,
      (gpointer) ID3D10Blob_GetBufferPointer (ps_blob),
      ID3D10Blob_GetBufferSize (ps_blob), NULL, shader);

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self,
        "could not create pixel shader, hr: 0x%x", (guint) hr);
    ret = FALSE;
  }

  ID3D10Blob_Release (ps_blob);

  return ret;
}

static gboolean
create_vertex_shader (GstD3D11ColorConvert * self, const gchar * shader_source,
    const D3D11_INPUT_ELEMENT_DESC * input_desc,
    guint desc_size, ID3D11VertexShader ** shader, ID3D11InputLayout ** layout)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (self);
  ID3DBlob *vs_blob;
  ID3D11Device *device_handle;
  HRESULT hr;
  ID3D11VertexShader *vshader = NULL;
  ID3D11InputLayout *in_layout = NULL;

  vs_blob = compile_shader (self, shader_source, FALSE);
  if (!vs_blob) {
    GST_ERROR_OBJECT (self, "Failed to compile shader code");
    return FALSE;
  }

  device_handle = gst_d3d11_device_get_device_handle (filter->device);

  hr = ID3D11Device_CreateVertexShader (device_handle,
      (gpointer) ID3D10Blob_GetBufferPointer (vs_blob),
      ID3D10Blob_GetBufferSize (vs_blob), NULL, &vshader);

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self,
        "could not create vertex shader, hr: 0x%x", (guint) hr);
    ID3D10Blob_Release (vs_blob);
    return FALSE;
  }

  hr = ID3D11Device_CreateInputLayout (device_handle, input_desc,
      desc_size, (gpointer) ID3D10Blob_GetBufferPointer (vs_blob),
      ID3D10Blob_GetBufferSize (vs_blob), &in_layout);

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self,
        "could not create input layout shader, hr: 0x%x", (guint) hr);
    ID3D10Blob_Release (vs_blob);
    ID3D11VertexShader_Release (vshader);
    return FALSE;
  }

  ID3D10Blob_Release (vs_blob);

  *shader = vshader;
  *layout = in_layout;

  return TRUE;
}

static gboolean
create_shader_input_resource (GstD3D11ColorConvert * self,
    GstD3D11Device * device, const GstD3D11Format * format, GstVideoInfo * info)
{
  D3D11_TEXTURE2D_DESC texture_desc = { 0, };
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = { 0 };
  HRESULT hr;
  ID3D11Device *device_handle;
  ID3D11Texture2D *tex[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11ShaderResourceView *view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i;

  device_handle = gst_d3d11_device_get_device_handle (device);

  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  if (format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
        break;

      texture_desc.Width = GST_VIDEO_INFO_COMP_WIDTH (info, i);
      texture_desc.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);
      texture_desc.Format = format->resource_format[i];

      hr = ID3D11Device_CreateTexture2D (device_handle,
          &texture_desc, NULL, &tex[i]);
      if (FAILED (hr)) {
        GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
        goto error;
      }
    }
  } else {
    texture_desc.Width = GST_VIDEO_INFO_WIDTH (info);
    texture_desc.Height = GST_VIDEO_INFO_HEIGHT (info);
    texture_desc.Format = format->dxgi_format;

    hr = ID3D11Device_CreateTexture2D (device_handle,
        &texture_desc, NULL, &tex[0]);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
      goto error;
    }

    if (format->dxgi_format == DXGI_FORMAT_NV12 ||
        format->dxgi_format == DXGI_FORMAT_P010) {
      ID3D11Resource_AddRef (tex[0]);
      tex[1] = tex[0];
    }
  }

  view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  view_desc.Texture2D.MipLevels = 1;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
      break;

    view_desc.Format = format->resource_format[i];
    hr = ID3D11Device_CreateShaderResourceView (device_handle,
        (ID3D11Resource *) tex[i], &view_desc, &view[i]);

    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self,
          "Failed to create resource view (0x%x)", (guint) hr);
      goto error;
    }
  }

  self->num_input_view = i;

  GST_DEBUG_OBJECT (self,
      "%d shader resource view created", self->num_input_view);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    self->in_texture[i] = tex[i];
    self->shader_resource_view[i] = view[i];
  }

  return TRUE;

error:
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (view[i])
      ID3D11ShaderResourceView_Release (view[i]);
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (tex[i])
      ID3D11Texture2D_Release (tex[i]);
  }

  return FALSE;
}

static gboolean
create_shader_output_resource (GstD3D11ColorConvert * self,
    GstD3D11Device * device, const GstD3D11Format * format, GstVideoInfo * info)
{
  D3D11_TEXTURE2D_DESC texture_desc = { 0, };
  D3D11_RENDER_TARGET_VIEW_DESC view_desc = { 0, };
  HRESULT hr;
  ID3D11Device *device_handle;
  ID3D11Texture2D *tex[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11RenderTargetView *view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i;

  device_handle = gst_d3d11_device_get_device_handle (device);

  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags =
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

  if (format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
        break;

      texture_desc.Width = GST_VIDEO_INFO_COMP_WIDTH (info, i);
      texture_desc.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);
      texture_desc.Format = format->resource_format[i];

      hr = ID3D11Device_CreateTexture2D (device_handle,
          &texture_desc, NULL, &tex[i]);
      if (FAILED (hr)) {
        GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
        goto error;
      }
    }
  } else {
    texture_desc.Width = GST_VIDEO_INFO_WIDTH (info);
    texture_desc.Height = GST_VIDEO_INFO_HEIGHT (info);
    texture_desc.Format = format->dxgi_format;

    hr = ID3D11Device_CreateTexture2D (device_handle,
        &texture_desc, NULL, &tex[0]);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
      goto error;
    }

    if (format->dxgi_format == DXGI_FORMAT_NV12 ||
        format->dxgi_format == DXGI_FORMAT_P010) {
      ID3D11Resource_AddRef (tex[0]);
      tex[1] = tex[0];
    }
  }

  view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  view_desc.Texture2D.MipSlice = 0;
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
      break;

    view_desc.Format = format->resource_format[i];
    hr = ID3D11Device_CreateRenderTargetView (device_handle,
        (ID3D11Resource *) tex[i], &view_desc, &view[i]);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self,
          "Failed to create %dth render target view (0x%x)", i, (guint) hr);
      goto error;
    }
  }

  self->num_output_view = i;

  GST_DEBUG_OBJECT (self, "%d render view created", self->num_output_view);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    self->out_texture[i] = tex[i];
    self->render_target_view[i] = view[i];
  }

  return TRUE;

error:
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (view[i])
      ID3D11RenderTargetView_Release (view[i]);
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (tex[i])
      ID3D11Texture2D_Release (tex[i]);
  }

  return FALSE;
}

typedef struct
{
  GstD3D11ColorConvert *self;
  GstVideoInfo *in_info;
  GstVideoInfo *out_info;
  gboolean ret;
} SetupShaderData;

static void
gst_d3d11_color_convert_setup_shader (GstD3D11Device * device,
    SetupShaderData * data)
{
  GstD3D11ColorConvert *self = data->self;
  HRESULT hr;
  D3D11_SAMPLER_DESC sampler_desc = { 0, };
  D3D11_INPUT_ELEMENT_DESC input_desc[2] = { 0, };
  D3D11_BUFFER_DESC buffer_desc = { 0, };
  D3D11_MAPPED_SUBRESOURCE map;
  VertexData *vertex_data;
  WORD *indices;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *context_handle;
  gchar *shader_code = NULL;
  ConvertInfo *convert_info = &self->priv->convert_info;
  GstVideoInfo *in_info = data->in_info;
  GstVideoInfo *out_info = data->out_info;

  data->ret = TRUE;

  device_handle = gst_d3d11_device_get_device_handle (device);
  context_handle = gst_d3d11_device_get_device_context_handle (device);

  /* bilinear filtering */
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  sampler_desc.MinLOD = 0;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

  hr = ID3D11Device_CreateSamplerState (device_handle,
      &sampler_desc, &self->sampler);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create sampler state, hr: 0x%x",
        (guint) hr);
    goto error;
  }

  shader_code = g_strdup_printf (templ_pixel_shader,
      convert_info->templ->constant_buffer ?
      convert_info->templ->constant_buffer : "",
      convert_info->templ->func ? convert_info->templ->func : "",
      convert_info->ps_body);

  GST_LOG_OBJECT (self, "Create Pixel Shader \n%s", shader_code);

  if (!create_pixel_shader (self, shader_code, &self->pixel_shader)) {
    GST_ERROR_OBJECT (self, "Couldn't create pixel shader");
    goto error;
  }

  if (convert_info->templ->constant_buffer) {
    D3D11_BUFFER_DESC const_buffer_desc = { 0, };

    const_buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    const_buffer_desc.ByteWidth = sizeof (PixelShaderColorTransform);
    const_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    const_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    const_buffer_desc.MiscFlags = 0;
    const_buffer_desc.StructureByteStride = 0;

    hr = ID3D11Device_CreateBuffer (device_handle, &const_buffer_desc, NULL,
        &self->const_buffer);

    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self, "Couldn't create constant buffer, hr: 0x%x",
          (guint) hr);
      goto error;
    }

    hr = ID3D11DeviceContext_Map (context_handle,
        (ID3D11Resource *) self->const_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
        &map);

    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self, "Couldn't map constant buffer, hr: 0x%x",
          (guint) hr);
      goto error;
    }

    memcpy (map.pData, &convert_info->transform,
        sizeof (PixelShaderColorTransform));

    ID3D11DeviceContext_Unmap (context_handle,
        (ID3D11Resource *) self->const_buffer, 0);
  }

  input_desc[0].SemanticName = "POSITION";
  input_desc[0].SemanticIndex = 0;
  input_desc[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
  input_desc[0].InputSlot = 0;
  input_desc[0].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc[0].InstanceDataStepRate = 0;

  input_desc[1].SemanticName = "TEXCOORD";
  input_desc[1].SemanticIndex = 0;
  input_desc[1].Format = DXGI_FORMAT_R32G32_FLOAT;
  input_desc[1].InputSlot = 0;
  input_desc[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
  input_desc[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
  input_desc[1].InstanceDataStepRate = 0;

  if (!create_vertex_shader (self, templ_vertex_shader,
          input_desc, G_N_ELEMENTS (input_desc), &self->vertex_shader,
          &self->layout)) {
    GST_ERROR_OBJECT (self, "Couldn't vertex pixel shader");
    goto error;
  }

  /* setup vertext buffer and index buffer */
  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (VertexData) * 4;
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = ID3D11Device_CreateBuffer (device_handle, &buffer_desc, NULL,
      &self->vertex_buffer);

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create vertex buffer, hr: 0x%x",
        (guint) hr);
    goto error;
  }

  /* two triangle */
  self->index_count = 2 * 3;

  buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  buffer_desc.ByteWidth = sizeof (WORD) * self->index_count;
  buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
  buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  hr = ID3D11Device_CreateBuffer (device_handle, &buffer_desc, NULL,
      &self->index_buffer);

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create index buffer, hr: 0x%x",
        (guint) hr);
    goto error;
  }

  hr = ID3D11DeviceContext_Map (context_handle,
      (ID3D11Resource *) self->vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    goto error;
  }

  vertex_data = (VertexData *) map.pData;

  hr = ID3D11DeviceContext_Map (context_handle,
      (ID3D11Resource *) self->index_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
      &map);

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't map index buffer, hr: 0x%x", (guint) hr);
    ID3D11DeviceContext_Unmap (context_handle,
        (ID3D11Resource *) self->vertex_buffer, 0);
    goto error;
  }

  indices = (WORD *) map.pData;

  /* bottom left */
  vertex_data[0].position.x = -1.0f;
  vertex_data[0].position.y = -1.0f;
  vertex_data[0].position.z = 0.0f;
  vertex_data[0].texture.x = 0.0f;
  vertex_data[0].texture.y = 1.0f;

  /* top left */
  vertex_data[1].position.x = -1.0f;
  vertex_data[1].position.y = 1.0f;
  vertex_data[1].position.z = 0.0f;
  vertex_data[1].texture.x = 0.0f;
  vertex_data[1].texture.y = 0.0f;

  /* top right */
  vertex_data[2].position.x = 1.0f;
  vertex_data[2].position.y = 1.0f;
  vertex_data[2].position.z = 0.0f;
  vertex_data[2].texture.x = 1.0f;
  vertex_data[2].texture.y = 0.0f;

  /* bottom right */
  vertex_data[3].position.x = 1.0f;
  vertex_data[3].position.y = -1.0f;
  vertex_data[3].position.z = 0.0f;
  vertex_data[3].texture.x = 1.0f;
  vertex_data[3].texture.y = 1.0f;

  /* clockwise indexing */
  indices[0] = 0;               /* bottom left */
  indices[1] = 1;               /* top left */
  indices[2] = 2;               /* top right */

  indices[3] = 3;               /* bottom right */
  indices[4] = 0;               /* bottom left  */
  indices[5] = 2;               /* top right */

  ID3D11DeviceContext_Unmap (context_handle,
      (ID3D11Resource *) self->vertex_buffer, 0);
  ID3D11DeviceContext_Unmap (context_handle,
      (ID3D11Resource *) self->index_buffer, 0);

  /* create output texture */
  if (!create_shader_input_resource (self,
          device, self->in_d3d11_format, in_info))
    goto error;

  if (!create_shader_output_resource (self,
          device, self->out_d3d11_format, out_info))
    goto error;

  return;

error:
  clear_shader_resource (device, self);

  g_free (shader_code);
  data->ret = FALSE;

  return;
}

static gboolean
gst_d3d11_color_convert_set_info (GstD3D11BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (filter);
  SetupShaderData data;
  const GstVideoInfo *unknown_info;
  gboolean is_supported = FALSE;
  MatrixData matrix;

  gst_d3d11_color_convert_clear_shader_resource (self);

  GST_DEBUG_OBJECT (self, "Setup convert with format %s -> %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));

  /* these must match */
  if (in_info->width != out_info->width || in_info->height != out_info->height
      || in_info->fps_n != out_info->fps_n || in_info->fps_d != out_info->fps_d)
    goto format_mismatch;

  /* if present, these must match too */
  if (in_info->par_n != out_info->par_n || in_info->par_d != out_info->par_d)
    goto format_mismatch;

  /* if present, these must match too */
  if (in_info->interlace_mode != out_info->interlace_mode)
    goto format_mismatch;

  self->in_d3d11_format =
      gst_d3d11_format_from_gst (GST_VIDEO_INFO_FORMAT (in_info));
  if (!self->in_d3d11_format) {
    unknown_info = in_info;
    goto format_unknown;
  }

  self->out_d3d11_format =
      gst_d3d11_format_from_gst (GST_VIDEO_INFO_FORMAT (out_info));
  if (!self->out_d3d11_format) {
    unknown_info = out_info;
    goto format_unknown;
  }

  if (GST_VIDEO_INFO_IS_RGB (in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      is_supported = setup_convert_info_rgb_to_rgb (self, in_info, out_info);
    } else if (GST_VIDEO_INFO_IS_YUV (out_info)) {
      is_supported = setup_convert_info_rgb_to_yuv (self, in_info, out_info);
    }
  } else if (GST_VIDEO_INFO_IS_YUV (in_info)) {
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      is_supported = setup_convert_info_yuv_to_rgb (self, in_info, out_info);
    } else if (GST_VIDEO_INFO_IS_YUV (out_info)) {
      is_supported = setup_convert_info_yuv_to_yuv (self, in_info, out_info);
    }
  }

  if (!is_supported) {
    goto conversion_not_supported;
  }

  if (converter_get_matrix (self, &matrix, in_info, out_info)) {
    PixelShaderColorTransform *transform = &self->priv->convert_info.transform;

    /* padding the last column for 16bytes alignment */
    transform->trans_matrix[0] = matrix.dm[0][0];
    transform->trans_matrix[1] = matrix.dm[0][1];
    transform->trans_matrix[2] = matrix.dm[0][2];
    transform->trans_matrix[3] = 0;
    transform->trans_matrix[4] = matrix.dm[1][0];
    transform->trans_matrix[5] = matrix.dm[1][1];
    transform->trans_matrix[6] = matrix.dm[1][2];
    transform->trans_matrix[7] = 0;
    transform->trans_matrix[8] = matrix.dm[2][0];
    transform->trans_matrix[9] = matrix.dm[2][1];
    transform->trans_matrix[10] = matrix.dm[2][2];
    transform->trans_matrix[11] = 0;
  }

  self->viewport.TopLeftX = 0;
  self->viewport.TopLeftY = 0;
  self->viewport.Width = GST_VIDEO_INFO_WIDTH (out_info);
  self->viewport.Height = GST_VIDEO_INFO_HEIGHT (out_info);
  self->viewport.MinDepth = 0.0f;
  self->viewport.MaxDepth = 1.0f;

  data.self = self;
  data.in_info = in_info;
  data.out_info = out_info;
  gst_d3d11_device_thread_add (filter->device,
      (GstD3D11DeviceThreadFunc) gst_d3d11_color_convert_setup_shader, &data);

  if (!data.ret) {
    GST_ERROR_OBJECT (self, "Couldn't setup shader");
    return FALSE;
  }

  return TRUE;

  /* ERRORS */
format_mismatch:
  {
    GST_ERROR_OBJECT (self, "input and output formats do not match");
    return FALSE;
  }
format_unknown:
  {
    GST_ERROR_OBJECT (self,
        "%s couldn't be converted to d3d11 format",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (unknown_info)));
    return FALSE;
  }
conversion_not_supported:
  {
    GST_ERROR_OBJECT (self,
        "Conversion %s to %s not supported",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
    return FALSE;
  }
}

typedef struct
{
  GstD3D11ColorConvert *self;
  GstBuffer *in_buf;
  GstBuffer *out_buf;
} DoConvertData;

static void
do_convert (GstD3D11Device * device, DoConvertData * data)
{
  GstD3D11ColorConvert *self = data->self;
  ID3D11DeviceContext *context_handle;
  UINT vertex_stride = sizeof (VertexData);
  UINT offsets = 0;
  ID3D11ShaderResourceView *clear_view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i;

  context_handle = gst_d3d11_device_get_device_context_handle (device);

  for (i = 0; i < gst_buffer_n_memory (data->in_buf); i++) {
    GstMemory *mem = gst_buffer_peek_memory (data->in_buf, i);
    GstD3D11Memory *d3d11_mem;
    GstMapInfo info;

    g_assert (gst_is_d3d11_memory (mem));

    /* map to transfer pending staging data if any */
    gst_memory_map (mem, &info, GST_MAP_READ | GST_MAP_D3D11);
    gst_memory_unmap (mem, &info);

    d3d11_mem = (GstD3D11Memory *) mem;

    ID3D11DeviceContext_CopySubresourceRegion (context_handle,
        (ID3D11Resource *) self->in_texture[i], 0, 0, 0, 0,
        (ID3D11Resource *) d3d11_mem->texture, 0, NULL);
  }

  ID3D11DeviceContext_OMSetRenderTargets (context_handle,
      self->num_output_view, self->render_target_view, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology (context_handle,
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_IASetInputLayout (context_handle, self->layout);
  ID3D11DeviceContext_IASetVertexBuffers (context_handle,
      0, 1, &self->vertex_buffer, &vertex_stride, &offsets);
  ID3D11DeviceContext_IASetIndexBuffer (context_handle,
      self->index_buffer, DXGI_FORMAT_R16_UINT, 0);

  ID3D11DeviceContext_VSSetShader (context_handle,
      self->vertex_shader, NULL, 0);

  ID3D11DeviceContext_PSSetSamplers (context_handle, 0, 1, &self->sampler);

  if (self->const_buffer) {
    ID3D11DeviceContext_PSSetConstantBuffers (context_handle,
        0, 1, &self->const_buffer);
  }

  ID3D11DeviceContext_PSSetShaderResources (context_handle,
      0, self->num_input_view, self->shader_resource_view);

  /* FIXME: handle multiple shader case */
  ID3D11DeviceContext_PSSetShader (context_handle, self->pixel_shader, NULL, 0);
  ID3D11DeviceContext_RSSetViewports (context_handle, 1, &self->viewport);
  ID3D11DeviceContext_DrawIndexed (context_handle, self->index_count, 0, 0);

  /* unbind resources */
  ID3D11DeviceContext_PSSetShaderResources (context_handle,
      0, self->num_input_view, clear_view);

  for (i = 0; i < gst_buffer_n_memory (data->out_buf); i++) {
    GstMemory *mem = gst_buffer_peek_memory (data->out_buf, i);
    GstD3D11Memory *d3d11_mem;

    g_assert (gst_is_d3d11_memory (mem));

    d3d11_mem = (GstD3D11Memory *) mem;

    ID3D11DeviceContext_CopySubresourceRegion (context_handle,
        (ID3D11Resource *) d3d11_mem->texture, 0, 0, 0, 0,
        (ID3D11Resource *) self->out_texture[i], 0, NULL);
  }
}

static GstFlowReturn
gst_d3d11_color_convert_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (trans);
  DoConvertData data;

  data.self = self;
  data.in_buf = inbuf;
  data.out_buf = outbuf;

  gst_d3d11_device_thread_add (filter->device,
      (GstD3D11DeviceThreadFunc) do_convert, &data);

  return GST_FLOW_OK;
}
