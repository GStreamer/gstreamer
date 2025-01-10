/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12.h"
#include "gstd3d12-private.h"
#include "gstd3d12converter-builder.h"
#include "gstd3d12converter-private.h"
#include "gstd3d12converter-pack.h"
#include "gstd3d12converter-unpack.h"
#include <directx/d3dx12.h>
#include <wrl.h>
#include <string.h>
#include <math.h>
#include <map>
#include <vector>
#include <memory>
#include <queue>

#ifndef HAVE_DIRECTX_MATH_SIMD
#define _XM_NO_INTRINSICS_
#endif
#include <DirectXMath.h>

GST_DEBUG_CATEGORY (gst_d3d12_converter_debug);
#define GST_CAT_DEFAULT gst_d3d12_converter_debug

GType
gst_d3d12_converter_sampler_filter_get_type (void)
{
  static GType filter_type = 0;
  static const GEnumValue filter_types[] = {
    {D3D12_FILTER_MIN_MAG_MIP_POINT,
        "D3D12_FILTER_MIN_MAG_MIP_POINT", "min-mag-mip-point"},
    {D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT,
        "D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT", "min-linear-mag-mip-point"},
    {D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
        "D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT", "min-mag-linear-mip-point"},
    {D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        "D3D12_FILTER_MIN_MAG_MIP_LINEAR", "min-mag-mip-linear"},
    {D3D12_FILTER_ANISOTROPIC, "D3D12_FILTER_ANISOTROPIC", "anisotropic"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    filter_type = g_enum_register_static ("GstD3D12ConverterSamplerFilter",
        filter_types);
  } GST_D3D12_CALL_ONCE_END;

  return filter_type;
}

GType
gst_d3d12_converter_alpha_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue alpha_mode[] = {
    {GST_D3D12_CONVERTER_ALPHA_MODE_UNSPECIFIED,
        "GST_D3D12_CONVERTER_ALPHA_MODE_UNSPECIFIED", "unspecified"},
    {GST_D3D12_CONVERTER_ALPHA_MODE_PREMULTIPLIED,
        "GST_D3D12_CONVERTER_ALPHA_MODE_PREMULTIPLIED", "premultiplied"},
    {GST_D3D12_CONVERTER_ALPHA_MODE_STRAIGHT,
        "GST_D3D12_CONVERTER_ALPHA_MODE_STRAIGHT", "straight"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12ConverterAlphaMode", alpha_mode);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

GType
gst_d3d12_converter_color_balance_get_type (void)
{
  static GType type = 0;
  static const GEnumValue color_balance[] = {
    {GST_D3D12_CONVERTER_COLOR_BALANCE_DISABLED,
        "GST_D3D12_CONVERTER_COLOR_BALANCE_DISABLED", "disabled"},
    {GST_D3D12_CONVERTER_COLOR_BALANCE_ENABLED,
        "GST_D3D12_CONVERTER_COLOR_BALANCE_ENABLED", "enabled"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12ConverterColorBalance",
        color_balance);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

GType
gst_d3d12_converter_mip_gen_get_type (void)
{
  static GType type = 0;
  static const GEnumValue mipgen[] = {
    {GST_D3D12_CONVERTER_MIP_GEN_DISABLED,
        "GST_D3D12_CONVERTER_MIP_GEN_DISABLED", "disabled"},
    {GST_D3D12_CONVERTER_MIP_GEN_ENABLED,
        "GST_D3D12_CONVERTER_MIP_GEN_ENABLED", "enabled"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12ConverterMipGen", mipgen);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
using namespace DirectX;
/* *INDENT-ON* */

#define GAMMA_LUT_SIZE 4096
#define DEFAULT_BUFFER_COUNT 2
#define DEFAULT_SAMPLER_FILTER D3D12_FILTER_MIN_MAG_MIP_LINEAR
#define DEFAULT_BORDER_COLOR G_GUINT64_CONSTANT(0xffff000000000000)
#define DEFAULT_HUE 0.0
#define DEFAULT_SATURATION 1.0
#define DEFAULT_BRIGHTNESS 0.0
#define DEFAULT_CONTRAST 1.0
#define DEFAULT_MAX_MIP_LEVELS 1

static const WORD g_indices[6] = { 0, 1, 2, 3, 0, 2 };

struct PSColorSpace
{
  /* + 1 for 16bytes alignment  */
  FLOAT coeffX[4];
  FLOAT coeffY[4];
  FLOAT coeffZ[4];
  FLOAT offset[4];
  FLOAT min[4];
  FLOAT max[4];
};

struct PSConstBuffer
{
  PSColorSpace preCoeff;
  PSColorSpace postCoeff;
  PSColorSpace primariesCoeff;
};

struct PSConstBufferDyn
{
  float alphaFactor;
  float padding[3];
  float hsvcFactor[4];
};

struct VertexData
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
};

/* *INDENT-OFF* */
static const XMFLOAT4X4A g_matrix_identity = XMFLOAT4X4A (
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f);

static const XMFLOAT4X4A g_matrix_90r = XMFLOAT4X4A (
    0.0f, -1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f);

static const XMFLOAT4X4A g_matrix_180 = XMFLOAT4X4A (
    -1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, -1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f);

static const XMFLOAT4X4A g_matrix_90l = XMFLOAT4X4A (
    0.0f, 1.0f, 0.0f, 0.0f,
    -1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f);

static const XMFLOAT4X4A g_matrix_horiz = XMFLOAT4X4A (
    -1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f);

static const XMFLOAT4X4A g_matrix_vert = XMFLOAT4X4A (
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, -1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f);

static const XMFLOAT4X4A g_matrix_ul_lr = XMFLOAT4X4A (
    0.0f, -1.0f, 0.0f, 0.0f,
    -1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f);

static const XMFLOAT4X4A g_matrix_ur_ll = XMFLOAT4X4A (
    0.0f, 1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f);

constexpr UINT g_vertex_buf_size = sizeof (VertexData) * 4;
constexpr UINT g_index_buf_size = sizeof (g_indices);
constexpr UINT g_const_buf_size = sizeof (PSConstBuffer);
/* *INDENT-ON* */

enum
{
  PROP_0,
  PROP_SRC_X,
  PROP_SRC_Y,
  PROP_SRC_WIDTH,
  PROP_SRC_HEIGHT,
  PROP_DEST_X,
  PROP_DEST_Y,
  PROP_DEST_WIDTH,
  PROP_DEST_HEIGHT,
  PROP_ALPHA,
  PROP_FILL_BORDER,
  PROP_BORDER_COLOR,
  PROP_VIDEO_DIRECTION,
  PROP_SAMPLER_FILTER,
  PROP_HUE,
  PROP_SATURATION,
  PROP_BRIGHTNESS,
  PROP_CONTRAST,
  PROP_MAX_MIP_LEVELS,
};

/* *INDENT-OFF* */
struct QuadData
{
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = { };
  ComPtr<ID3D12PipelineState> pso;
  guint num_rtv;
};

struct PipelineData
{
  PixelShaderBlobList psblob_list;
  ConverterRootSignaturePtr crs;
  ComPtr<ID3D12RootSignature> rs;
  std::vector<QuadData> quad_data;
};

struct ConvertCtxCommon
{
  ConvertCtxCommon()
  {
    const_data_dyn.alphaFactor = 1.0;
    const_data_dyn.hsvcFactor[0] = DEFAULT_HUE;
    const_data_dyn.hsvcFactor[1] = DEFAULT_SATURATION;
    const_data_dyn.hsvcFactor[2] = DEFAULT_BRIGHTNESS;
    const_data_dyn.hsvcFactor[3] = DEFAULT_CONTRAST;
  }

  ~ConvertCtxCommon()
  {
    waitSetup();
  }

  void waitSetup()
  {
    auto fence = setup_fence.Detach ();

    if (fence) {
      auto completed = fence->GetCompletedValue ();
      if (completed < setup_fence_val)
        fence->SetEventOnCompletion (setup_fence_val, nullptr);

      fence->Release ();
    }
  }

  D3D12_VERTEX_BUFFER_VIEW vbv;
  D3D12_INDEX_BUFFER_VIEW ibv;
  D3D12_GPU_VIRTUAL_ADDRESS const_buf_addr[2];
  D3D12_FILTER sampler_filter = DEFAULT_SAMPLER_FILTER;
  ComPtr<ID3D12Resource> shader_buf;
  ComPtr<ID3D12Resource> gamma_dec_lut;
  ComPtr<ID3D12Resource> gamma_enc_lut;
  ComPtr<ID3D12DescriptorHeap> gamma_lut_heap;
  ComPtr<ID3D12DescriptorHeap> sampler_heap;
  D3D12_VIEWPORT viewport[GST_VIDEO_MAX_PLANES];
  D3D12_RECT scissor_rect[GST_VIDEO_MAX_PLANES];
  ComPtr<ID3D12Fence> setup_fence;
  guint64 setup_fence_val = 0;
  gboolean have_lut = FALSE;
  gboolean need_color_balance = FALSE;
  PSConstBufferDyn const_data_dyn;
};

typedef std::shared_ptr<ConvertCtxCommon> ConvertCtxCommonPtr;

struct ConvertCtx
{
  ConvertCtx()
  {
  }

  ~ConvertCtx()
  {
    waitSetup ();
  }

  void waitSetup()
  {
    if (comm)
      comm->waitSetup ();
  }

  GstVideoInfo in_info;
  GstVideoInfo out_info;
  std::vector<PipelineData> pipeline_data;
  ComPtr<ID3D12Resource> vertex_upload;
  ConvertCtxCommonPtr comm;
};

typedef std::shared_ptr<ConvertCtx> ConvertCtxPtr;

#define STATE_VERTEX_AND_INDEX \
  (D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER)

struct _GstD3D12ConverterPrivate
{
  _GstD3D12ConverterPrivate ()
  {
    transform = g_matrix_identity;
    custom_transform = g_matrix_identity;
    blend_desc = CD3DX12_BLEND_DESC (D3D12_DEFAULT);
    for (guint i = 0; i < 4; i++)
      blend_factor[i] = 1.0f;

    sample_desc.Count = 1;
    sample_desc.Quality = 0;
  }

  ~_GstD3D12ConverterPrivate ()
  {
    if (fence_val > 0 && cq)
      gst_d3d12_cmd_queue_fence_wait (cq, fence_val);

    main_ctx = nullptr;
    mipgen_ctx = nullptr;
    post_mipgen_ctx = nullptr;
    gst_clear_buffer (&mipgen_buf);

    gst_clear_object (&mipgen_srv_heap_pool);
    gst_clear_object (&srv_heap_pool);
    gst_clear_object (&cq);
    gst_clear_object (&pack);
    gst_clear_object (&unpack);
    gst_clear_object (&mipgen);
  }

  GstD3D12CmdQueue *cq = nullptr;
  GstD3D12Unpack *unpack = nullptr;
  GstD3D12Pack *pack = nullptr;
  GstD3D12MipGen *mipgen = nullptr;

  GstVideoInfo in_info;
  GstVideoInfo mipgen_info;
  GstVideoInfo out_info;

  D3D12_BLEND_DESC blend_desc;
  D3D12_INPUT_ELEMENT_DESC input_desc[2];
  FLOAT blend_factor[4];
  DXGI_SAMPLE_DESC sample_desc;
  gboolean update_pso = FALSE;
  gboolean update_sampler = FALSE;

  GstD3D12DescHeapPool *srv_heap_pool = nullptr;
  GstD3D12DescHeapPool *mipgen_srv_heap_pool = nullptr;

  guint srv_inc_size;
  guint rtv_inc_size;
  guint sampler_inc_size;

  ConvertCtxPtr main_ctx;
  ConvertCtxPtr mipgen_ctx;
  ConvertCtxPtr post_mipgen_ctx;

  guint64 input_texture_width;
  guint input_texture_height;
  gboolean update_src_rect = FALSE;
  gboolean update_dest_rect = FALSE;
  gboolean update_transform = FALSE;
  XMFLOAT4X4A transform;
  XMFLOAT4X4A custom_transform;

  gboolean clear_background = FALSE;
  FLOAT clear_color[4][4];
  GstD3D12ColorMatrix clear_color_matrix;

  GstVideoOrientationMethod video_direction;
  gboolean color_balance_enabled = FALSE;
  gboolean mipgen_enabled = FALSE;

  D3D12_SHADER_RESOURCE_VIEW_DESC mipgen_srv_desc = { };
  D3D12_RESOURCE_DESC mipgen_desc = { };
  GstBuffer *mipgen_buf = nullptr;
  guint auto_mipgen_level = 1;

  std::mutex prop_lock;
  guint64 fence_val = 0;

  /* properties */
  gint src_x = 0;
  gint src_y = 0;
  gint src_width = 0;
  gint src_height = 0;
  gint dest_x = 0;
  gint dest_y = 0;
  gint dest_width = 0;
  gint dest_height = 0;
  gboolean fill_border = FALSE;
  guint64 border_color = DEFAULT_BORDER_COLOR;
  GstD3D12ConverterAlphaMode src_alpha_mode =
      GST_D3D12_CONVERTER_ALPHA_MODE_UNSPECIFIED;
  GstD3D12ConverterAlphaMode dst_alpha_mode =
      GST_D3D12_CONVERTER_ALPHA_MODE_UNSPECIFIED;
  guint mip_levels = DEFAULT_MAX_MIP_LEVELS;
};
/* *INDENT-ON* */

static void gst_d3d12_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d12_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d12_converter_finalize (GObject * object);
static void
gst_d3d12_converter_calculate_border_color (GstD3D12Converter * self);

#define gst_d3d12_converter_parent_class parent_class
G_DEFINE_TYPE (GstD3D12Converter, gst_d3d12_converter, GST_TYPE_OBJECT);

static void
gst_d3d12_converter_class_init (GstD3D12ConverterClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto param_flags = (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  object_class->set_property = gst_d3d12_converter_set_property;
  object_class->get_property = gst_d3d12_converter_get_property;
  object_class->finalize = gst_d3d12_converter_finalize;

  g_object_class_install_property (object_class, PROP_SRC_X,
      g_param_spec_int ("src-x", "Src-X",
          "Source x poisition to start conversion", G_MININT, G_MAXINT, 0,
          param_flags));
  g_object_class_install_property (object_class, PROP_SRC_Y,
      g_param_spec_int ("src-y", "Src-Y",
          "Source y poisition to start conversion", G_MININT, G_MAXINT, 0,
          param_flags));
  g_object_class_install_property (object_class, PROP_SRC_WIDTH,
      g_param_spec_int ("src-width", "Src-Width",
          "Source width to convert", 0, G_MAXINT, 0, param_flags));
  g_object_class_install_property (object_class, PROP_SRC_HEIGHT,
      g_param_spec_int ("src-height", "Src-Height",
          "Source height to convert", 0, G_MAXINT, 0, param_flags));
  g_object_class_install_property (object_class, PROP_DEST_X,
      g_param_spec_int ("dest-x", "Dest-X",
          "x poisition in the destination frame", G_MININT, G_MAXINT, 0,
          param_flags));
  g_object_class_install_property (object_class, PROP_DEST_Y,
      g_param_spec_int ("dest-y", "Dest-Y",
          "y poisition in the destination frame", G_MININT, G_MAXINT, 0,
          param_flags));
  g_object_class_install_property (object_class, PROP_DEST_WIDTH,
      g_param_spec_int ("dest-width", "Dest-Width",
          "Width in the destination frame", 0, G_MAXINT, 0, param_flags));
  g_object_class_install_property (object_class, PROP_DEST_HEIGHT,
      g_param_spec_int ("dest-height", "Dest-Height",
          "Height in the destination frame", 0, G_MAXINT, 0, param_flags));
  g_object_class_install_property (object_class, PROP_ALPHA,
      g_param_spec_double ("alpha", "Alpha",
          "The alpha color value to use", 0, 1.0, 1.0, param_flags));
  g_object_class_install_property (object_class, PROP_FILL_BORDER,
      g_param_spec_boolean ("fill-border", "Fill border",
          "Fill border with \"border-color\" if destination rectangle does not "
          "fill the complete destination image", FALSE, param_flags));
  g_object_class_install_property (object_class, PROP_BORDER_COLOR,
      g_param_spec_uint64 ("border-color", "Border Color",
          "ARGB representation of the border color to use",
          0, G_MAXUINT64, DEFAULT_BORDER_COLOR, param_flags));
  g_object_class_install_property (object_class, PROP_VIDEO_DIRECTION,
      g_param_spec_enum ("video-direction", "Video Direction",
          "Video direction", GST_TYPE_VIDEO_ORIENTATION_METHOD,
          GST_VIDEO_ORIENTATION_IDENTITY, param_flags));
  g_object_class_install_property (object_class, PROP_SAMPLER_FILTER,
      g_param_spec_enum ("sampler-filter", "Sampler Filter",
          "Sampler Filter", GST_TYPE_D3D12_CONVERTER_SAMPLER_FILTER,
          DEFAULT_SAMPLER_FILTER, param_flags));
  g_object_class_install_property (object_class, PROP_HUE,
      g_param_spec_double ("hue", "Hue", "hue", -1.0, 1.0, DEFAULT_HUE,
          param_flags));
  g_object_class_install_property (object_class, PROP_SATURATION,
      g_param_spec_double ("saturation", "Saturation", "saturation", 0.0, 2.0,
          DEFAULT_SATURATION, param_flags));
  g_object_class_install_property (object_class, PROP_BRIGHTNESS,
      g_param_spec_double ("brightness", "Brightness", "brightness", -1.0, 1.0,
          DEFAULT_BRIGHTNESS, param_flags));
  g_object_class_install_property (object_class, PROP_CONTRAST,
      g_param_spec_double ("contrast", "Contrast", "contrast",
          0.0, 2.0, DEFAULT_CONTRAST, param_flags));
  g_object_class_install_property (object_class, PROP_MAX_MIP_LEVELS,
      g_param_spec_uint ("max-mip-levels", "Max Mip Levels",
          "Maximum mip levels of shader resource to create "
          "if render viewport size is smaller than shader resource "
          "(0 = maximum level)", 0, G_MAXUINT16, DEFAULT_MAX_MIP_LEVELS,
          param_flags));

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_converter_debug,
      "d3d12converter", 0, "d3d12converter");
}

static void
gst_d3d12_converter_init (GstD3D12Converter * self)
{
  self->priv = new GstD3D12ConverterPrivate ();
}

static void
gst_d3d12_converter_finalize (GObject * object)
{
  auto self = GST_D3D12_CONVERTER (object);

  delete self->priv;
  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_src_rect (GstD3D12Converter * self, gint * old_val,
    const GValue * new_val)
{
  auto priv = self->priv;
  gint tmp;

  tmp = g_value_get_int (new_val);
  if (tmp != *old_val) {
    priv->update_src_rect = TRUE;
    *old_val = tmp;
  }
}

static void
update_dest_rect (GstD3D12Converter * self, gint * old_val,
    const GValue * new_val)
{
  auto priv = self->priv;
  gint tmp;

  tmp = g_value_get_int (new_val);
  if (tmp != *old_val) {
    priv->update_dest_rect = TRUE;
    *old_val = tmp;
  }
}

static void
on_color_balance_updated (GstD3D12Converter * self)
{
  auto priv = self->priv;

  if (!priv->color_balance_enabled)
    return;

  auto comm = priv->main_ctx->comm;

  comm->need_color_balance =
      gst_d3d12_converter_is_color_balance_needed (comm->
      const_data_dyn.hsvcFactor[0], comm->const_data_dyn.hsvcFactor[1],
      comm->const_data_dyn.hsvcFactor[2], comm->const_data_dyn.hsvcFactor[3]);
}

static void
gst_d3d12_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_CONVERTER (object);
  auto priv = self->priv;
  auto comm = priv->main_ctx->comm;

  std::lock_guard < std::mutex > lk (priv->prop_lock);
  switch (prop_id) {
    case PROP_SRC_X:
      update_src_rect (self, &priv->src_x, value);
      break;
    case PROP_SRC_Y:
      update_src_rect (self, &priv->src_y, value);
      break;
    case PROP_SRC_WIDTH:
      update_src_rect (self, &priv->src_width, value);
      break;
    case PROP_SRC_HEIGHT:
      update_src_rect (self, &priv->src_height, value);
      break;
    case PROP_DEST_X:
      update_dest_rect (self, &priv->dest_x, value);
      break;
    case PROP_DEST_Y:
      update_dest_rect (self, &priv->dest_y, value);
      break;
    case PROP_DEST_WIDTH:
      update_dest_rect (self, &priv->dest_width, value);
      break;
    case PROP_DEST_HEIGHT:
      update_dest_rect (self, &priv->dest_height, value);
      break;
    case PROP_ALPHA:
      comm->const_data_dyn.alphaFactor = g_value_get_double (value);
      break;
    case PROP_FILL_BORDER:
    {
      gboolean fill_border = g_value_get_boolean (value);

      if (fill_border != priv->fill_border) {
        priv->update_dest_rect = TRUE;
        priv->fill_border = fill_border;
      }
      break;
    }
    case PROP_BORDER_COLOR:
    {
      guint64 border_color = g_value_get_uint64 (value);

      if (border_color != priv->border_color) {
        priv->border_color = border_color;
        gst_d3d12_converter_calculate_border_color (self);
      }
      break;
    }
    case PROP_VIDEO_DIRECTION:
    {
      GstVideoOrientationMethod video_direction =
          (GstVideoOrientationMethod) g_value_get_enum (value);
      if (video_direction != priv->video_direction) {
        priv->video_direction = video_direction;
        priv->update_transform = TRUE;
      }
      break;
    }
    case PROP_SAMPLER_FILTER:
    {
      auto filter = (D3D12_FILTER) g_value_get_enum (value);
      if (filter != comm->sampler_filter) {
        comm->sampler_filter = filter;
        priv->update_sampler = TRUE;
      }
      break;
    }
    case PROP_HUE:
      if (priv->color_balance_enabled) {
        comm->const_data_dyn.hsvcFactor[0] = g_value_get_double (value);
        on_color_balance_updated (self);
      }
      break;
    case PROP_SATURATION:
      if (priv->color_balance_enabled) {
        comm->const_data_dyn.hsvcFactor[1] = g_value_get_double (value);
        on_color_balance_updated (self);
      }
      break;
    case PROP_BRIGHTNESS:
      if (priv->color_balance_enabled) {
        comm->const_data_dyn.hsvcFactor[2] = g_value_get_double (value);
        on_color_balance_updated (self);
      }
      break;
    case PROP_CONTRAST:
      if (priv->color_balance_enabled) {
        comm->const_data_dyn.hsvcFactor[3] = g_value_get_double (value);
        on_color_balance_updated (self);
      }
      break;
    case PROP_MAX_MIP_LEVELS:
      priv->mip_levels = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_CONVERTER (object);
  auto priv = self->priv;
  auto comm = priv->main_ctx->comm;

  std::lock_guard < std::mutex > lk (priv->prop_lock);
  switch (prop_id) {
    case PROP_SRC_X:
      g_value_set_int (value, priv->src_x);
      break;
    case PROP_SRC_Y:
      g_value_set_int (value, priv->src_y);
      break;
    case PROP_SRC_WIDTH:
      g_value_set_int (value, priv->src_width);
      break;
    case PROP_SRC_HEIGHT:
      g_value_set_int (value, priv->src_height);
      break;
    case PROP_DEST_X:
      g_value_set_int (value, priv->dest_x);
      break;
    case PROP_DEST_Y:
      g_value_set_int (value, priv->dest_y);
      break;
    case PROP_DEST_WIDTH:
      g_value_set_int (value, priv->dest_width);
      break;
    case PROP_DEST_HEIGHT:
      g_value_set_int (value, priv->dest_height);
      break;
    case PROP_ALPHA:
      g_value_set_double (value, comm->const_data_dyn.alphaFactor);
      break;
    case PROP_FILL_BORDER:
      g_value_set_boolean (value, priv->fill_border);
      break;
    case PROP_BORDER_COLOR:
      g_value_set_uint64 (value, priv->border_color);
      break;
    case PROP_VIDEO_DIRECTION:
      g_value_set_enum (value, priv->video_direction);
      break;
    case PROP_SAMPLER_FILTER:
      g_value_set_enum (value, comm->sampler_filter);
      break;
    case PROP_HUE:
      g_value_set_double (value, comm->const_data_dyn.hsvcFactor[0]);
      break;
    case PROP_SATURATION:
      g_value_set_double (value, comm->const_data_dyn.hsvcFactor[1]);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_double (value, comm->const_data_dyn.hsvcFactor[2]);
      break;
    case PROP_CONTRAST:
      g_value_set_double (value, comm->const_data_dyn.hsvcFactor[3]);
      break;
    case PROP_MAX_MIP_LEVELS:
      g_value_set_uint (value, priv->mip_levels);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static guint
reorder_rtv_index (GstVideoFormat output_format, guint index)
{
  switch (output_format) {
    case GST_VIDEO_FORMAT_A420:
    case GST_VIDEO_FORMAT_A420_10LE:
    case GST_VIDEO_FORMAT_A420_12LE:
    case GST_VIDEO_FORMAT_A420_16LE:
    case GST_VIDEO_FORMAT_A422:
    case GST_VIDEO_FORMAT_A422_10LE:
    case GST_VIDEO_FORMAT_A422_12LE:
    case GST_VIDEO_FORMAT_A422_16LE:
    {
      switch (index) {
        case 0:
          return 0;
        case 1:
          return 3;
        case 2:
          return 1;
        case 3:
          return 2;
        default:
          g_assert_not_reached ();
          break;
      }
      return 0;
    }
    case GST_VIDEO_FORMAT_AV12:
    {
      switch (index) {
        case 0:
          return 0;
        case 1:
          return 2;
        case 2:
          return 1;
        case 3:
          return 3;
        default:
          g_assert_not_reached ();
          break;
      }
      return 0;
    }
    default:
      break;
  }

  return index;
}

static gboolean
gst_d3d12_converter_create_sampler (GstD3D12Converter * self,
    D3D12_FILTER filter, ID3D12DescriptorHeap ** heap)
{
  ComPtr < ID3D12DescriptorHeap > sampler_heap;
  auto hr = gst_d3d12_device_get_sampler_state (self->device, filter,
      &sampler_heap);
  if (!gst_d3d12_result (hr, self->device))
    return FALSE;

  *heap = sampler_heap.Detach ();

  return TRUE;
}

static ConvertCtxPtr
gst_d3d12_converter_setup_resource (GstD3D12Converter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info,
    D3D12_FILTER sampler_filter, const DXGI_SAMPLE_DESC * sample_desc,
    const D3D12_BLEND_DESC * blend_desc,
    CONVERT_TYPE * convert_type, gboolean have_lut,
    gboolean color_balance_enabled, GstD3D12ConverterAlphaMode src_alpha,
    GstD3D12ConverterAlphaMode dst_alpha, PSConstBuffer * const_data,
    ConvertCtxCommonPtr ref)
{
  auto priv = self->priv;
  HRESULT hr;
  VertexData vertex_data[4];
  ComPtr < ID3D12Resource > upload_buf;
  GstD3D12Format in_format, out_format;

  if (!gst_d3d12_device_get_format (self->device,
          GST_VIDEO_INFO_FORMAT (in_info), &in_format)) {
    GST_ERROR_OBJECT (self, "%s couldn't be converted to d3d12 format",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)));
    return nullptr;
  }

  if (!gst_d3d12_device_get_format (self->device,
          GST_VIDEO_INFO_FORMAT (out_info), &out_format)) {
    GST_ERROR_OBJECT (self, "%s couldn't be converted to d3d12 format",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
    return nullptr;
  }

  auto device = gst_d3d12_device_get_device_handle (self->device);
  auto ctx = std::make_shared < ConvertCtx > ();

  ctx->in_info = *in_info;
  ctx->out_info = *out_info;
  ctx->comm = ref;

  if (color_balance_enabled)
    ctx->pipeline_data.resize (2);
  else
    ctx->pipeline_data.resize (1);

  for (size_t i = 0; i < ctx->pipeline_data.size (); i++) {
    ComPtr < ID3DBlob > rs_blob;
    ctx->pipeline_data[i].crs =
        gst_d3d12_get_converter_root_signature (self->device,
        GST_VIDEO_INFO_FORMAT (in_info), convert_type[i]);
    if (!ctx->pipeline_data[i].crs) {
      GST_ERROR_OBJECT (self, "Couldn't get root signature blob");
      return nullptr;
    }

    ctx->pipeline_data[i].crs->GetBlob (&rs_blob);
    hr = device->CreateRootSignature (0, rs_blob->GetBufferPointer (),
        rs_blob->GetBufferSize (), IID_PPV_ARGS (&ctx->pipeline_data[i].rs));
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create root signature");
      return nullptr;
    }

    ctx->pipeline_data[i].psblob_list =
        gst_d3d12_get_converter_pixel_shader_blob (GST_VIDEO_INFO_FORMAT
        (in_info), GST_VIDEO_INFO_FORMAT (out_info),
        src_alpha == GST_D3D12_CONVERTER_ALPHA_MODE_PREMULTIPLIED,
        dst_alpha == GST_D3D12_CONVERTER_ALPHA_MODE_PREMULTIPLIED,
        convert_type[i]);

    auto psblob_size = ctx->pipeline_data[i].psblob_list.size ();
    if (psblob_size == 0) {
      GST_ERROR_OBJECT (self, "Couldn't get pixel shader blob");
      return nullptr;
    }

    ctx->pipeline_data[i].quad_data.resize (psblob_size);
  }

  D3D12_SHADER_BYTECODE vs_blob;
  hr = gst_d3d12_get_converter_vertex_shader_blob (&vs_blob, priv->input_desc);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't get vertex shader blob");
    return nullptr;
  }

  std::queue < DXGI_FORMAT > rtv_formats;
  auto output_format = GST_VIDEO_INFO_FORMAT (out_info);
  for (guint i = 0; i < 4; i++) {
    auto index = reorder_rtv_index (output_format, i);
    auto format = out_format.resource_format[index];
    if (format == DXGI_FORMAT_UNKNOWN)
      break;

    rtv_formats.push (format);
  }

  for (size_t i = 0; i < ctx->pipeline_data.size (); i++) {
    auto & pipeline_data = ctx->pipeline_data[i];
    for (size_t j = 0; j < pipeline_data.quad_data.size (); j++) {
      auto & pso_desc = pipeline_data.quad_data[j].desc;

      pso_desc.pRootSignature = pipeline_data.rs.Get ();
      pso_desc.VS = vs_blob;
      pso_desc.PS = pipeline_data.psblob_list[j].bytecode;
      pso_desc.BlendState = *blend_desc;
      pso_desc.SampleMask = UINT_MAX;
      pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC (D3D12_DEFAULT);
      pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
      pso_desc.DepthStencilState.DepthEnable = FALSE;
      pso_desc.DepthStencilState.StencilEnable = FALSE;
      pso_desc.InputLayout.pInputElementDescs = priv->input_desc;
      pso_desc.InputLayout.NumElements = 2;
      pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      pso_desc.NumRenderTargets = pipeline_data.psblob_list[j].num_rtv;
      pso_desc.SampleDesc = *sample_desc;

      for (UINT k = 0; k < pso_desc.NumRenderTargets; k++) {
        if (i == 0) {
          pso_desc.RTVFormats[k] = rtv_formats.front ();
          rtv_formats.pop ();
        } else {
          pso_desc.RTVFormats[k] =
              ctx->pipeline_data[0].quad_data[j].desc.RTVFormats[k];
        }
      }

      ComPtr < ID3D12PipelineState > pso;
      hr = device->CreateGraphicsPipelineState (&pso_desc, IID_PPV_ARGS (&pso));
      if (!gst_d3d12_result (hr, self->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create PSO");
        return nullptr;
      }

      pipeline_data.quad_data[j].pso = pso;
      pipeline_data.quad_data[j].num_rtv = pipeline_data.psblob_list[j].num_rtv;
    }
  }

  if (ctx->comm)
    return ctx;

  ctx->comm = std::make_shared < ConvertCtxCommon > ();
  auto comm = ctx->comm;
  comm->have_lut = have_lut;

  if (!gst_d3d12_converter_create_sampler (self, sampler_filter,
          &comm->sampler_heap)) {
    if (sampler_filter != DEFAULT_SAMPLER_FILTER) {
      sampler_filter = DEFAULT_SAMPLER_FILTER;
      if (!gst_d3d12_converter_create_sampler (self, sampler_filter,
              &comm->sampler_heap)) {
        return nullptr;
      }
    } else {
      return nullptr;
    }
  }

  comm->sampler_filter = sampler_filter;

  if (!priv->srv_heap_pool) {
    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = { };
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.NumDescriptors = ctx->pipeline_data[0].crs->GetNumSrv ();
    if (comm->have_lut)
      srv_heap_desc.NumDescriptors += 2;

    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    priv->srv_heap_pool = gst_d3d12_desc_heap_pool_new (device, &srv_heap_desc);
  }

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

  /* vertex, index and constant buffers */
  D3D12_HEAP_PROPERTIES heap_prop;
  D3D12_RESOURCE_DESC resource_desc;
  CD3DX12_RANGE range (0, 0);
  guint8 *data;
  D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
  if (gst_d3d12_device_non_zeroed_supported (self->device))
    heap_flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

  {
    guint vertex_index_size = g_vertex_buf_size + g_index_buf_size;
    vertex_index_size = GST_ROUND_UP_N (vertex_index_size,
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    guint const_size = GST_ROUND_UP_N (g_const_buf_size,
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    guint other_const_off = GST_ROUND_UP_N (vertex_index_size + const_size,
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    heap_prop = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
    if (color_balance_enabled) {
      resource_desc =
          CD3DX12_RESOURCE_DESC::Buffer (other_const_off + const_size);
    } else {
      resource_desc =
          CD3DX12_RESOURCE_DESC::Buffer (vertex_index_size + const_size);
    }
    hr = device->CreateCommittedResource (&heap_prop, heap_flags,
        &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS (&comm->shader_buf));
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create vertex buffer");
      return nullptr;
    }

    comm->vbv.BufferLocation = comm->shader_buf->GetGPUVirtualAddress ();
    comm->vbv.SizeInBytes = g_vertex_buf_size;
    comm->vbv.StrideInBytes = sizeof (VertexData);

    comm->ibv.BufferLocation = comm->vbv.BufferLocation + g_vertex_buf_size;
    comm->ibv.SizeInBytes = g_index_buf_size;
    comm->ibv.Format = DXGI_FORMAT_R16_UINT;

    comm->const_buf_addr[0] = comm->vbv.BufferLocation + vertex_index_size;
    comm->const_buf_addr[1] = comm->vbv.BufferLocation + other_const_off;

    heap_prop = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD);
    hr = device->CreateCommittedResource (&heap_prop,
        heap_flags, &resource_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS (&upload_buf));
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create vertex buffer upload");
      return nullptr;
    }

    hr = upload_buf->Map (0, &range, (void **) &data);
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't map vertext buffer");
      return nullptr;
    }

    memcpy (data, vertex_data, g_vertex_buf_size);
    memcpy (data + g_vertex_buf_size, g_indices, g_index_buf_size);
    memcpy (data + vertex_index_size, &const_data[0], g_const_buf_size);
    if (color_balance_enabled)
      memcpy (data + other_const_off, &const_data[1], g_const_buf_size);
    upload_buf->Unmap (0, nullptr);
  }

  auto in_trc = in_info->colorimetry.transfer;
  auto out_trc = in_info->colorimetry.transfer;

  if (convert_type[0] == CONVERT_TYPE::GAMMA ||
      convert_type[0] == CONVERT_TYPE::PRIMARY) {
    out_trc = out_info->colorimetry.transfer;
  }

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (out_info); i++) {
    comm->viewport[i].TopLeftX = 0;
    comm->viewport[i].TopLeftY = 0;
    comm->viewport[i].Width = GST_VIDEO_INFO_COMP_WIDTH (out_info, i);
    comm->viewport[i].Height = GST_VIDEO_INFO_COMP_HEIGHT (out_info, i);
    comm->viewport[i].MinDepth = 0.0f;
    comm->viewport[i].MaxDepth = 1.0f;

    comm->scissor_rect[i].left = 0;
    comm->scissor_rect[i].top = 0;
    comm->scissor_rect[i].right = GST_VIDEO_INFO_COMP_WIDTH (out_info, i);
    comm->scissor_rect[i].bottom = GST_VIDEO_INFO_COMP_HEIGHT (out_info, i);
  }

  if (have_lut) {
    hr = gst_d3d12_device_get_converter_resources (self->device,
        comm->shader_buf.Get (), upload_buf.Get (), &comm->vbv, &comm->ibv,
        in_trc, &comm->gamma_dec_lut, out_trc, &comm->gamma_enc_lut,
        &comm->setup_fence, &comm->setup_fence_val);
  } else {
    hr = gst_d3d12_device_get_converter_resources (self->device,
        comm->shader_buf.Get (), upload_buf.Get (), &comm->vbv, &comm->ibv,
        in_trc, nullptr, out_trc, nullptr, &comm->setup_fence,
        &comm->setup_fence_val);
  }

  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't execute command list");
    return nullptr;
  }

  if (have_lut) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = { };
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 2;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    auto hr = device->CreateDescriptorHeap (&desc,
        IID_PPV_ARGS (&comm->gamma_lut_heap));
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create gamma lut heap");
      return nullptr;
    }

    auto cpu_handle =
        CD3DX12_CPU_DESCRIPTOR_HANDLE (GetCPUDescriptorHandleForHeapStart
        (comm->gamma_lut_heap));

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture1D.MipLevels = 1;

    device->CreateShaderResourceView (comm->gamma_dec_lut.Get (), &srv_desc,
        cpu_handle);
    cpu_handle.Offset (priv->srv_inc_size);

    device->CreateShaderResourceView (comm->gamma_enc_lut.Get (), &srv_desc,
        cpu_handle);
  }

  return ctx;
}

static void
gst_d3d12_converter_update_clear_background (GstD3D12Converter * self)
{
  auto priv = self->priv;
  const GstVideoInfo *out_info = &priv->out_info;

  if (priv->fill_border && (priv->dest_x != 0 || priv->dest_y != 0 ||
          priv->dest_width != out_info->width ||
          priv->dest_height != out_info->height ||
          priv->video_direction == GST_VIDEO_ORIENTATION_CUSTOM)) {
    GST_DEBUG_OBJECT (self, "Enable background color");
    priv->clear_background = TRUE;
  } else {
    GST_DEBUG_OBJECT (self, "Disable background color");
    priv->clear_background = FALSE;
  }
}

static gboolean
gst_d3d12_converter_apply_orientation (GstD3D12Converter * self)
{
  auto priv = self->priv;

  switch (priv->video_direction) {
    case GST_VIDEO_ORIENTATION_IDENTITY:
    case GST_VIDEO_ORIENTATION_AUTO:
    default:
      priv->transform = g_matrix_identity;
      break;
    case GST_VIDEO_ORIENTATION_90R:
      priv->transform = g_matrix_90r;
      break;
    case GST_VIDEO_ORIENTATION_180:
      priv->transform = g_matrix_180;
      break;
    case GST_VIDEO_ORIENTATION_90L:
      priv->transform = g_matrix_90l;
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      priv->transform = g_matrix_horiz;
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      priv->transform = g_matrix_vert;
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      priv->transform = g_matrix_ul_lr;
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      priv->transform = g_matrix_ur_ll;
      break;
    case GST_VIDEO_ORIENTATION_CUSTOM:
      priv->transform = priv->custom_transform;
  }

  return TRUE;
}

static gboolean
gst_d3d12_converter_update_transform (GstD3D12Converter * self)
{
  auto priv = self->priv;

  if (!priv->update_transform)
    return TRUE;

  priv->update_transform = FALSE;

  gst_d3d12_converter_update_clear_background (self);

  return gst_d3d12_converter_apply_orientation (self);
}

static gboolean
gst_d3d12_converter_update_src_rect (GstD3D12Converter * self)
{
  auto priv = self->priv;
  VertexData vertex_data[4];
  HRESULT hr;
  FLOAT u0, u1, v0, v1, off_u, off_v;
  gint texture_width = priv->input_texture_width;
  gint texture_height = priv->input_texture_height;

  if (!priv->update_src_rect)
    return TRUE;

  priv->update_src_rect = FALSE;

  GST_DEBUG_OBJECT (self, "Updating vertex buffer");

  auto ctx = priv->main_ctx;
  if (!ctx->vertex_upload) {
    D3D12_HEAP_PROPERTIES heap_prop =
        CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC buffer_desc =
        CD3DX12_RESOURCE_DESC::Buffer (g_vertex_buf_size);
    auto device = gst_d3d12_device_get_device_handle (self->device);
    hr = device->CreateCommittedResource (&heap_prop, D3D12_HEAP_FLAG_NONE,
        &buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS (&ctx->vertex_upload));
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create vertex buffer upload");
      return FALSE;
    }
  }

  /*
   *  (u0, v0) -- (u1, v0)
   *     |            |
   *  (u0, v1) -- (u1, v1)
   */
  off_u = 0.5f / texture_width;
  off_v = 0.5f / texture_height;

  if (priv->src_x > 0)
    u0 = (priv->src_x / (gfloat) texture_width) + off_u;
  else
    u0 = 0.0f;

  if ((priv->src_x + priv->src_width) != texture_width)
    u1 = ((priv->src_x + priv->src_width) / (gfloat) texture_width) - off_u;
  else
    u1 = 1.0f;

  if (priv->src_y > 0)
    v0 = (priv->src_y / (gfloat) texture_height) + off_v;
  else
    v0 = 0.0;

  if ((priv->src_y + priv->src_height) != texture_height)
    v1 = ((priv->src_y + priv->src_height) / (gfloat) texture_height) - off_v;
  else
    v1 = 1.0f;

  /* bottom left */
  vertex_data[0].position.x = -1.0f;
  vertex_data[0].position.y = -1.0f;
  vertex_data[0].position.z = 0.0f;
  vertex_data[0].texture.u = u0;
  vertex_data[0].texture.v = v1;

  /* top left */
  vertex_data[1].position.x = -1.0f;
  vertex_data[1].position.y = 1.0f;
  vertex_data[1].position.z = 0.0f;
  vertex_data[1].texture.u = u0;
  vertex_data[1].texture.v = v0;

  /* top right */
  vertex_data[2].position.x = 1.0f;
  vertex_data[2].position.y = 1.0f;
  vertex_data[2].position.z = 0.0f;
  vertex_data[2].texture.u = u1;
  vertex_data[2].texture.v = v0;

  /* bottom right */
  vertex_data[3].position.x = 1.0f;
  vertex_data[3].position.y = -1.0f;
  vertex_data[3].position.z = 0.0f;
  vertex_data[3].texture.u = u1;
  vertex_data[3].texture.v = v1;

  guint8 *data;
  CD3DX12_RANGE range (0, 0);
  hr = ctx->vertex_upload->Map (0, &range, (void **) &data);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't map vertex buffer, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  memcpy (data, vertex_data, g_vertex_buf_size);
  ctx->vertex_upload->Unmap (0, nullptr);

  return TRUE;
}

static gboolean
gst_d3d12_converter_update_dest_rect (GstD3D12Converter * self)
{
  auto priv = self->priv;

  if (!priv->update_dest_rect)
    return TRUE;

  auto comm = priv->main_ctx->comm;

  comm->viewport[0].TopLeftX = priv->dest_x;
  comm->viewport[0].TopLeftY = priv->dest_y;
  comm->viewport[0].Width = priv->dest_width;
  comm->viewport[0].Height = priv->dest_height;

  comm->scissor_rect[0].left = priv->dest_x;
  comm->scissor_rect[0].top = priv->dest_y;
  comm->scissor_rect[0].right = priv->dest_width + priv->dest_x;
  comm->scissor_rect[0].bottom = priv->dest_height + priv->dest_y;

  GST_DEBUG_OBJECT (self,
      "Update viewport, TopLeftX: %f, TopLeftY: %f, Width: %f, Height %f",
      comm->viewport[0].TopLeftX, comm->viewport[0].TopLeftY,
      comm->viewport[0].Width, comm->viewport[0].Height);

  gst_d3d12_converter_update_clear_background (self);

  auto format = GST_VIDEO_INFO_FORMAT (&priv->out_info);
  switch (format) {
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_YVU9:
      comm->viewport[1].TopLeftX = comm->viewport[0].TopLeftX / 4;
      comm->viewport[1].TopLeftY = comm->viewport[0].TopLeftY / 4;
      comm->viewport[1].Width = comm->viewport[0].Width / 4;
      comm->viewport[1].Height = comm->viewport[0].Height / 4;

      comm->scissor_rect[1].left = comm->scissor_rect[0].left / 4;
      comm->scissor_rect[1].top = comm->scissor_rect[0].top / 4;
      comm->scissor_rect[1].right = comm->scissor_rect[0].right / 4;
      comm->scissor_rect[1].bottom = comm->scissor_rect[0].bottom / 4;
      for (guint i = 2; i < GST_VIDEO_INFO_N_PLANES (&priv->out_info); i++) {
        comm->viewport[i] = comm->viewport[1];
        comm->scissor_rect[i] = comm->scissor_rect[1];
      }
      break;
    case GST_VIDEO_FORMAT_Y41B:
      comm->viewport[1].TopLeftX = comm->viewport[0].TopLeftX / 4;
      comm->viewport[1].TopLeftY = comm->viewport[0].TopLeftY;
      comm->viewport[1].Width = comm->viewport[0].Width / 4;
      comm->viewport[1].Height = comm->viewport[0].Height;

      comm->scissor_rect[1].left = comm->scissor_rect[0].left / 4;
      comm->scissor_rect[1].top = comm->scissor_rect[0].top;
      comm->scissor_rect[1].right = comm->scissor_rect[0].right / 4;
      comm->scissor_rect[1].bottom = comm->scissor_rect[0].bottom;
      for (guint i = 2; i < GST_VIDEO_INFO_N_PLANES (&priv->out_info); i++) {
        comm->viewport[i] = comm->viewport[1];
        comm->scissor_rect[i] = comm->scissor_rect[1];
      }
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_A420:
    case GST_VIDEO_FORMAT_A420_10LE:
    case GST_VIDEO_FORMAT_A420_12LE:
    case GST_VIDEO_FORMAT_A420_16LE:
    case GST_VIDEO_FORMAT_AV12:
      comm->viewport[1].TopLeftX = comm->viewport[0].TopLeftX / 2;
      comm->viewport[1].TopLeftY = comm->viewport[0].TopLeftY / 2;
      comm->viewport[1].Width = comm->viewport[0].Width / 2;
      comm->viewport[1].Height = comm->viewport[0].Height / 2;

      comm->scissor_rect[1].left = comm->scissor_rect[0].left / 2;
      comm->scissor_rect[1].top = comm->scissor_rect[0].top / 2;
      comm->scissor_rect[1].right = comm->scissor_rect[0].right / 2;
      comm->scissor_rect[1].bottom = comm->scissor_rect[0].bottom / 2;

      for (guint i = 2; i < GST_VIDEO_INFO_N_PLANES (&priv->out_info); i++) {
        comm->viewport[i] = comm->viewport[1];
        comm->scissor_rect[i] = comm->scissor_rect[1];
      }
      break;
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_A422:
    case GST_VIDEO_FORMAT_A422_10LE:
    case GST_VIDEO_FORMAT_A422_12LE:
    case GST_VIDEO_FORMAT_A422_16LE:
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV61:
      comm->viewport[1].TopLeftX = comm->viewport[0].TopLeftX / 2;
      comm->viewport[1].TopLeftY = comm->viewport[0].TopLeftY;
      comm->viewport[1].Width = comm->viewport[0].Width / 2;
      comm->viewport[1].Height = comm->viewport[0].Height;

      comm->scissor_rect[1].left = comm->scissor_rect[0].left / 2;
      comm->scissor_rect[1].top = comm->scissor_rect[0].top;
      comm->scissor_rect[1].right = comm->scissor_rect[0].right / 2;
      comm->scissor_rect[1].bottom = comm->scissor_rect[0].bottom;

      for (guint i = 2; i < GST_VIDEO_INFO_N_PLANES (&priv->out_info); i++) {
        comm->viewport[i] = comm->viewport[1];
        comm->scissor_rect[i] = comm->scissor_rect[1];
      }
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBR_16LE:
    case GST_VIDEO_FORMAT_GBRA:
    case GST_VIDEO_FORMAT_GBRA_10LE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
    case GST_VIDEO_FORMAT_A444:
    case GST_VIDEO_FORMAT_A444_10LE:
    case GST_VIDEO_FORMAT_A444_12LE:
    case GST_VIDEO_FORMAT_A444_16LE:
    case GST_VIDEO_FORMAT_NV24:
      for (guint i = 1; i < GST_VIDEO_INFO_N_PLANES (&priv->out_info); i++) {
        comm->viewport[i] = comm->viewport[0];
        comm->scissor_rect[i] = comm->scissor_rect[0];
      }
      break;
    default:
      break;
  }

  priv->update_dest_rect = FALSE;

  return TRUE;
}

static void
convert_info_gray_to_yuv (const GstVideoInfo * gray, GstVideoInfo * yuv)
{
  GstVideoInfo tmp;

  if (GST_VIDEO_INFO_IS_YUV (gray)) {
    *yuv = *gray;
    return;
  }

  if (gray->finfo->depth[0] == 8) {
    gst_video_info_set_format (&tmp,
        GST_VIDEO_FORMAT_Y444, gray->width, gray->height);
  } else {
    gst_video_info_set_format (&tmp,
        GST_VIDEO_FORMAT_Y444_16LE, gray->width, gray->height);
  }

  tmp.colorimetry.range = gray->colorimetry.range;
  if (tmp.colorimetry.range == GST_VIDEO_COLOR_RANGE_UNKNOWN)
    tmp.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

  tmp.colorimetry.primaries = gray->colorimetry.primaries;
  if (tmp.colorimetry.primaries == GST_VIDEO_COLOR_PRIMARIES_UNKNOWN)
    tmp.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;

  tmp.colorimetry.transfer = gray->colorimetry.transfer;
  if (tmp.colorimetry.transfer == GST_VIDEO_TRANSFER_UNKNOWN)
    tmp.colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;

  tmp.colorimetry.matrix = gray->colorimetry.matrix;
  if (tmp.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN)
    tmp.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;

  *yuv = tmp;
}

static gboolean
gst_d3d12_converter_calculate_matrix (GstD3D12Converter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info,
    CONVERT_TYPE convert_type, PSConstBuffer * const_data)
{
  GstD3D12ColorMatrix pre_coeff;
  GstD3D12ColorMatrix post_coeff;
  GstD3D12ColorMatrix primaries_coeff;
  GstVideoInfo rgb_info;

  gst_d3d12_color_matrix_init (&pre_coeff);
  gst_d3d12_color_matrix_init (&post_coeff);
  gst_d3d12_color_matrix_init (&primaries_coeff);

  switch (convert_type) {
    case CONVERT_TYPE::RANGE:
      gst_d3d12_color_range_adjust_matrix_unorm (in_info, out_info,
          &post_coeff);
      break;
    case CONVERT_TYPE::SIMPLE:
      if (GST_VIDEO_INFO_IS_RGB (in_info)) {
        gst_d3d12_rgb_to_yuv_matrix_unorm (in_info, out_info, &post_coeff);
      } else {
        gst_d3d12_yuv_to_rgb_matrix_unorm (in_info, out_info, &post_coeff);
      }
      break;
    case CONVERT_TYPE::GAMMA:
    case CONVERT_TYPE::PRIMARY:
    case CONVERT_TYPE::COLOR_BALANCE:
    case CONVERT_TYPE::PRIMARY_AND_COLOR_BALANCE:
      if (GST_VIDEO_INFO_IS_RGB (in_info)) {
        rgb_info = *in_info;
        if (in_info->colorimetry.range == GST_VIDEO_COLOR_RANGE_16_235) {
          rgb_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

          gst_d3d12_color_range_adjust_matrix_unorm (in_info,
              &rgb_info, &pre_coeff);
        }
      } else {
        gst_video_info_set_format (&rgb_info,
            in_info->finfo->depth[0] == 8 ? GST_VIDEO_FORMAT_RGBA :
            GST_VIDEO_FORMAT_RGBA64_LE, in_info->width, in_info->height);
        rgb_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
        rgb_info.colorimetry.transfer = in_info->colorimetry.transfer;
        rgb_info.colorimetry.primaries = in_info->colorimetry.primaries;

        gst_d3d12_yuv_to_rgb_matrix_unorm (in_info, &rgb_info, &pre_coeff);
      }

      if (convert_type == CONVERT_TYPE::PRIMARY ||
          convert_type == CONVERT_TYPE::PRIMARY_AND_COLOR_BALANCE) {
        const GstVideoColorPrimariesInfo *in_pinfo;
        const GstVideoColorPrimariesInfo *out_pinfo;

        in_pinfo =
            gst_video_color_primaries_get_info (in_info->colorimetry.primaries);
        out_pinfo =
            gst_video_color_primaries_get_info (out_info->
            colorimetry.primaries);

        gst_d3d12_color_primaries_matrix_unorm (in_pinfo, out_pinfo,
            &primaries_coeff);
      }

      if (GST_VIDEO_INFO_IS_RGB (out_info)) {
        if (out_info->colorimetry.range == GST_VIDEO_COLOR_RANGE_16_235) {
          rgb_info = *out_info;
          rgb_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

          gst_d3d12_color_range_adjust_matrix_unorm (&rgb_info,
              out_info, &post_coeff);
        }
      } else {
        gst_d3d12_rgb_to_yuv_matrix_unorm (&rgb_info, out_info, &post_coeff);
      }
      break;
    default:
      break;
  }

  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_DEBUG) {
    gchar *matrix_dump;
    matrix_dump = gst_d3d12_dump_color_matrix (&pre_coeff);
    GST_DEBUG_OBJECT (self, "PreCoeff \n%s", matrix_dump);
    g_free (matrix_dump);

    matrix_dump = gst_d3d12_dump_color_matrix (&primaries_coeff);
    GST_DEBUG_OBJECT (self, "PrimaryCoeff \n%s", matrix_dump);
    g_free (matrix_dump);

    matrix_dump = gst_d3d12_dump_color_matrix (&post_coeff);
    GST_DEBUG_OBJECT (self, "PostCoeff \n%s", matrix_dump);
    g_free (matrix_dump);
  }

  PSColorSpace *preCoeff = &const_data->preCoeff;
  PSColorSpace *postCoeff = &const_data->postCoeff;
  PSColorSpace *primariesCoeff = &const_data->primariesCoeff;

  for (guint i = 0; i < 3; i++) {
    preCoeff->coeffX[i] = pre_coeff.matrix[0][i];
    preCoeff->coeffY[i] = pre_coeff.matrix[1][i];
    preCoeff->coeffZ[i] = pre_coeff.matrix[2][i];
    preCoeff->offset[i] = pre_coeff.offset[i];
    preCoeff->min[i] = pre_coeff.min[i];
    preCoeff->max[i] = pre_coeff.max[i];

    postCoeff->coeffX[i] = post_coeff.matrix[0][i];
    postCoeff->coeffY[i] = post_coeff.matrix[1][i];
    postCoeff->coeffZ[i] = post_coeff.matrix[2][i];
    postCoeff->offset[i] = post_coeff.offset[i];
    postCoeff->min[i] = post_coeff.min[i];
    postCoeff->max[i] = post_coeff.max[i];

    primariesCoeff->coeffX[i] = primaries_coeff.matrix[0][i];
    primariesCoeff->coeffY[i] = primaries_coeff.matrix[1][i];
    primariesCoeff->coeffZ[i] = primaries_coeff.matrix[2][i];
    primariesCoeff->offset[i] = primaries_coeff.offset[i];
    primariesCoeff->min[i] = primaries_coeff.min[i];
    primariesCoeff->max[i] = primaries_coeff.max[i];
  }

  return TRUE;
}

static gboolean
is_custom_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_BGRA64_LE:
    case GST_VIDEO_FORMAT_BGR10A2_LE:
    case GST_VIDEO_FORMAT_RBGA:
    case GST_VIDEO_FORMAT_ARGB64_LE:
      return TRUE;
    default:
      break;
  }

  return FALSE;
}

static void
gst_d3d12_converter_calculate_border_color (GstD3D12Converter * self)
{
  auto priv = self->priv;
  GstD3D12ColorMatrix *m = &priv->clear_color_matrix;
  const GstVideoInfo *out_info = &priv->out_info;
  gdouble a;
  gdouble rgb[3];
  gdouble converted[3];
  GstVideoFormat format = GST_VIDEO_INFO_FORMAT (out_info);

  a = ((priv->border_color & 0xffff000000000000) >> 48) / (gdouble) G_MAXUINT16;
  rgb[0] =
      ((priv->border_color & 0x0000ffff00000000) >> 32) / (gdouble) G_MAXUINT16;
  rgb[1] =
      ((priv->border_color & 0x00000000ffff0000) >> 16) / (gdouble) G_MAXUINT16;
  rgb[2] = (priv->border_color & 0x000000000000ffff) / (gdouble) G_MAXUINT16;

  for (guint i = 0; i < 3; i++) {
    converted[i] = 0;
    for (guint j = 0; j < 3; j++) {
      converted[i] += m->matrix[i][j] * rgb[j];
    }
    converted[i] += m->offset[i];
    converted[i] = CLAMP (converted[i], m->min[i], m->max[i]);
  }

  GST_DEBUG_OBJECT (self, "Calculated background color ARGB: %f, %f, %f, %f",
      a, converted[0], converted[1], converted[2]);

  /* scale down if output is planar high bitdepth format */
  switch (format) {
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBRA_10LE:
    case GST_VIDEO_FORMAT_A420_10LE:
    case GST_VIDEO_FORMAT_A422_10LE:
    case GST_VIDEO_FORMAT_A444_10LE:
      for (guint i = 0; i < 3; i++) {
        converted[i] /= 64.0;
      }
      a /= 64.0;
      break;
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBRA_12LE:
    case GST_VIDEO_FORMAT_A420_12LE:
    case GST_VIDEO_FORMAT_A422_12LE:
    case GST_VIDEO_FORMAT_A444_12LE:
      for (guint i = 0; i < 3; i++) {
        converted[i] /= 16.0;
      }
      a /= 16.0;
      break;
    default:
      break;
  }

  if ((GST_VIDEO_INFO_IS_RGB (out_info) &&
          GST_VIDEO_INFO_N_PLANES (out_info) == 1 &&
          !is_custom_format (format)) || GST_VIDEO_INFO_IS_GRAY (out_info)) {
    for (guint i = 0; i < 3; i++)
      priv->clear_color[0][i] = converted[i];
    priv->clear_color[0][3] = a;
  } else {
    switch (format) {
      case GST_VIDEO_FORMAT_VUYA:
        priv->clear_color[0][0] = converted[2];
        priv->clear_color[0][1] = converted[1];
        priv->clear_color[0][2] = converted[0];
        priv->clear_color[0][3] = a;
        break;
      case GST_VIDEO_FORMAT_AYUV:
      case GST_VIDEO_FORMAT_AYUV64:
        priv->clear_color[0][0] = a;
        priv->clear_color[0][1] = converted[0];
        priv->clear_color[0][2] = converted[1];
        priv->clear_color[0][3] = converted[2];
        break;
      case GST_VIDEO_FORMAT_ARGB:
      case GST_VIDEO_FORMAT_xRGB:
      case GST_VIDEO_FORMAT_ARGB64_LE:
        priv->clear_color[0][0] = a;
        priv->clear_color[0][1] = converted[0];
        priv->clear_color[0][2] = converted[1];
        priv->clear_color[0][3] = converted[2];
        break;
      case GST_VIDEO_FORMAT_ABGR:
      case GST_VIDEO_FORMAT_xBGR:
        priv->clear_color[0][0] = a;
        priv->clear_color[0][1] = converted[2];
        priv->clear_color[0][2] = converted[1];
        priv->clear_color[0][3] = converted[0];
        break;
      case GST_VIDEO_FORMAT_RBGA:
        priv->clear_color[0][0] = converted[0];
        priv->clear_color[0][1] = converted[2];
        priv->clear_color[0][2] = converted[1];
        priv->clear_color[0][3] = a;
        break;
      case GST_VIDEO_FORMAT_NV12:
      case GST_VIDEO_FORMAT_NV21:
      case GST_VIDEO_FORMAT_NV16:
      case GST_VIDEO_FORMAT_NV61:
      case GST_VIDEO_FORMAT_NV24:
      case GST_VIDEO_FORMAT_P010_10LE:
      case GST_VIDEO_FORMAT_P012_LE:
      case GST_VIDEO_FORMAT_P016_LE:
        priv->clear_color[0][0] = converted[0];
        priv->clear_color[0][1] = 0;
        priv->clear_color[0][2] = 0;
        priv->clear_color[0][3] = 1.0;
        if (format == GST_VIDEO_FORMAT_NV21 || format == GST_VIDEO_FORMAT_NV61) {
          priv->clear_color[1][0] = converted[2];
          priv->clear_color[1][1] = converted[1];
        } else {
          priv->clear_color[1][0] = converted[1];
          priv->clear_color[1][1] = converted[2];
        }
        priv->clear_color[1][2] = 0;
        priv->clear_color[1][3] = 1.0;
        break;
      case GST_VIDEO_FORMAT_AV12:
        priv->clear_color[0][0] = converted[0];
        priv->clear_color[1][0] = converted[1];
        priv->clear_color[1][1] = converted[2];
        priv->clear_color[2][0] = a;
        break;
      case GST_VIDEO_FORMAT_YUV9:
      case GST_VIDEO_FORMAT_YVU9:
      case GST_VIDEO_FORMAT_Y41B:
      case GST_VIDEO_FORMAT_I420:
      case GST_VIDEO_FORMAT_YV12:
      case GST_VIDEO_FORMAT_I420_10LE:
      case GST_VIDEO_FORMAT_I420_12LE:
      case GST_VIDEO_FORMAT_Y42B:
      case GST_VIDEO_FORMAT_I422_10LE:
      case GST_VIDEO_FORMAT_I422_12LE:
      case GST_VIDEO_FORMAT_Y444:
      case GST_VIDEO_FORMAT_Y444_10LE:
      case GST_VIDEO_FORMAT_Y444_12LE:
      case GST_VIDEO_FORMAT_Y444_16LE:
        priv->clear_color[0][0] = converted[0];
        priv->clear_color[0][1] = 0;
        priv->clear_color[0][2] = 0;
        priv->clear_color[0][3] = 1.0;
        if (format == GST_VIDEO_FORMAT_YV12 || format == GST_VIDEO_FORMAT_YVU9) {
          priv->clear_color[1][0] = converted[2];
          priv->clear_color[2][0] = converted[1];
        } else {
          priv->clear_color[1][0] = converted[1];
          priv->clear_color[2][0] = converted[2];
        }
        priv->clear_color[1][1] = 0;
        priv->clear_color[1][2] = 0;
        priv->clear_color[1][3] = 1.0;
        priv->clear_color[2][1] = 0;
        priv->clear_color[2][2] = 0;
        priv->clear_color[2][3] = 1.0;
        break;
      case GST_VIDEO_FORMAT_A420:
      case GST_VIDEO_FORMAT_A420_10LE:
      case GST_VIDEO_FORMAT_A420_12LE:
      case GST_VIDEO_FORMAT_A420_16LE:
      case GST_VIDEO_FORMAT_A422:
      case GST_VIDEO_FORMAT_A422_10LE:
      case GST_VIDEO_FORMAT_A422_12LE:
      case GST_VIDEO_FORMAT_A422_16LE:
      case GST_VIDEO_FORMAT_A444:
      case GST_VIDEO_FORMAT_A444_10LE:
      case GST_VIDEO_FORMAT_A444_12LE:
      case GST_VIDEO_FORMAT_A444_16LE:
        priv->clear_color[0][0] = converted[0];
        priv->clear_color[1][0] = converted[1];
        priv->clear_color[2][0] = converted[2];
        priv->clear_color[3][0] = a;
        break;
      case GST_VIDEO_FORMAT_RGBP:
        priv->clear_color[0][0] = converted[0];
        priv->clear_color[1][0] = converted[1];
        priv->clear_color[2][0] = converted[2];
        break;
      case GST_VIDEO_FORMAT_BGRP:
        priv->clear_color[0][0] = converted[2];
        priv->clear_color[1][0] = converted[1];
        priv->clear_color[2][0] = converted[0];
        break;
      case GST_VIDEO_FORMAT_GBR:
      case GST_VIDEO_FORMAT_GBR_10LE:
      case GST_VIDEO_FORMAT_GBR_12LE:
      case GST_VIDEO_FORMAT_GBR_16LE:
        priv->clear_color[0][0] = converted[1];
        priv->clear_color[1][0] = converted[2];
        priv->clear_color[2][0] = converted[0];
        break;
      case GST_VIDEO_FORMAT_GBRA:
      case GST_VIDEO_FORMAT_GBRA_10LE:
      case GST_VIDEO_FORMAT_GBRA_12LE:
        priv->clear_color[0][0] = converted[1];
        priv->clear_color[1][0] = converted[2];
        priv->clear_color[2][0] = converted[0];
        priv->clear_color[3][0] = a;
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }
}

static gboolean
gst_d3d12_converter_setup_colorspace (GstD3D12Converter * self,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info,
    gboolean allow_gamma, gboolean allow_primaries,
    gboolean color_balance_enabled, gboolean * have_lut,
    CONVERT_TYPE * convert_type, PSConstBuffer * const_data)
{
  GstVideoInfo matrix_in_info;
  GstVideoInfo matrix_out_info;

  *have_lut = FALSE;

  convert_type[0] = CONVERT_TYPE::IDENTITY;
  convert_type[1] = CONVERT_TYPE::COLOR_BALANCE;
  if (GST_VIDEO_INFO_IS_RGB (in_info) != GST_VIDEO_INFO_IS_RGB (out_info)) {
    convert_type[0] = CONVERT_TYPE::SIMPLE;
  } else if (in_info->colorimetry.range != GST_VIDEO_COLOR_RANGE_UNKNOWN &&
      out_info->colorimetry.range != GST_VIDEO_COLOR_RANGE_UNKNOWN &&
      in_info->colorimetry.range != out_info->colorimetry.range) {
    convert_type[0] = CONVERT_TYPE::RANGE;
  }

  if (allow_gamma &&
      in_info->colorimetry.transfer != GST_VIDEO_TRANSFER_UNKNOWN &&
      out_info->colorimetry.transfer != GST_VIDEO_TRANSFER_UNKNOWN &&
      !gst_video_transfer_function_is_equivalent (in_info->colorimetry.transfer,
          GST_VIDEO_INFO_COMP_DEPTH (in_info, 0),
          out_info->colorimetry.transfer, GST_VIDEO_INFO_COMP_DEPTH (out_info,
              0))) {
    GST_DEBUG_OBJECT (self, "Different transfer function %d -> %d",
        in_info->colorimetry.transfer, out_info->colorimetry.transfer);
    convert_type[0] = CONVERT_TYPE::GAMMA;
  }

  if (allow_primaries &&
      in_info->colorimetry.primaries != GST_VIDEO_COLOR_PRIMARIES_UNKNOWN &&
      out_info->colorimetry.primaries != GST_VIDEO_COLOR_PRIMARIES_UNKNOWN &&
      in_info->colorimetry.transfer != GST_VIDEO_TRANSFER_UNKNOWN &&
      out_info->colorimetry.transfer != GST_VIDEO_TRANSFER_UNKNOWN &&
      !gst_video_color_primaries_is_equivalent (in_info->colorimetry.primaries,
          out_info->colorimetry.primaries)) {
    GST_DEBUG_OBJECT (self, "Different primaries %d -> %d",
        in_info->colorimetry.primaries, out_info->colorimetry.primaries);
    convert_type[0] = CONVERT_TYPE::PRIMARY;
    convert_type[1] = CONVERT_TYPE::PRIMARY_AND_COLOR_BALANCE;
  }

  if (GST_VIDEO_INFO_IS_RGB (in_info)) {
    matrix_in_info = *in_info;
  } else {
    convert_info_gray_to_yuv (in_info, &matrix_in_info);
    if (matrix_in_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
        matrix_in_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
      matrix_in_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
    }
  }

  if (GST_VIDEO_INFO_IS_RGB (out_info)) {
    matrix_out_info = *out_info;
  } else {
    convert_info_gray_to_yuv (out_info, &matrix_out_info);
    if (matrix_out_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
        matrix_out_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
      matrix_out_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
    }
  }

  if (!gst_d3d12_converter_calculate_matrix (self,
          &matrix_in_info, &matrix_out_info, convert_type[0], &const_data[0])) {
    return FALSE;
  }

  if (color_balance_enabled &&
      !gst_d3d12_converter_calculate_matrix (self,
          &matrix_in_info, &matrix_out_info, convert_type[1], &const_data[1])) {
    return FALSE;
  }

  if (convert_type[0] == CONVERT_TYPE::GAMMA ||
      convert_type[0] == CONVERT_TYPE::PRIMARY || color_balance_enabled) {
    *have_lut = TRUE;
  }

  return TRUE;
}

/**
 * gst_d3d12_converter_new:
 * @device: a #GstD3D12Device
 * @queue: (allow-none): a #GstD3D12CmdQueue
 * @in_info: a #GstVideoInfo
 * @out_info: a #GstVideoInfo
 * @blend_desc: (nullable): D3D12_BLEND_DESC
 * @blend_factor: (nullable): blend factor value
 * @config: (nullable): converter config
 *
 * Creates a new converter instance
 *
 * Returns: (transfer full) (nullable): a new #GstD3D12Converter instance
 * or %NULL if conversion is not supported
 *
 * Since: 1.26
 */
GstD3D12Converter *
gst_d3d12_converter_new (GstD3D12Device * device, GstD3D12CmdQueue * queue,
    const GstVideoInfo * in_info, const GstVideoInfo * out_info,
    const D3D12_BLEND_DESC * blend_desc, const gfloat blend_factor[4],
    GstStructure * config)
{
  GstD3D12Converter *self;
  gboolean allow_gamma = FALSE;
  gboolean allow_primaries = FALSE;
  D3D12_FILTER sampler_filter = DEFAULT_SAMPLER_FILTER;
  guint sample_count = 1;
  guint sample_quality = 0;

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (in_info != nullptr, nullptr);
  g_return_val_if_fail (out_info != nullptr, nullptr);
  g_return_val_if_fail (!queue || GST_IS_D3D12_CMD_QUEUE (queue), nullptr);

  self = (GstD3D12Converter *) g_object_new (GST_TYPE_D3D12_CONVERTER, nullptr);
  gst_object_ref_sink (self);
  auto priv = self->priv;
  priv->cq = queue;
  if (!priv->cq) {
    priv->cq = gst_d3d12_device_get_cmd_queue (device,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
  }
  gst_object_ref (priv->cq);

  priv->unpack = gst_d3d12_unpack_new (device, in_info);
  if (!priv->unpack) {
    GST_ERROR_OBJECT (self, "Couldn't create unpack object");
    gst_object_unref (self);
    return nullptr;
  }

  priv->pack = gst_d3d12_pack_new (device, out_info);
  if (!priv->pack) {
    GST_ERROR_OBJECT (self, "Couldn't create pack object");
    gst_object_unref (self);
    return nullptr;
  }

  if (blend_desc)
    priv->blend_desc = *blend_desc;

  if (blend_factor) {
    for (guint i = 0; i < 4; i++)
      priv->blend_factor[i] = blend_factor[i];
  }

  if (config) {
    gint value;
    if (gst_structure_get_enum (config, GST_D3D12_CONVERTER_OPT_GAMMA_MODE,
            GST_TYPE_VIDEO_GAMMA_MODE, &value) &&
        (GstVideoGammaMode) value != GST_VIDEO_GAMMA_MODE_NONE) {
      allow_gamma = TRUE;
    }

    if (gst_structure_get_enum (config, GST_D3D12_CONVERTER_OPT_PRIMARIES_MODE,
            GST_TYPE_VIDEO_PRIMARIES_MODE, &value) &&
        (GstVideoPrimariesMode) value != GST_VIDEO_PRIMARIES_MODE_NONE) {
      allow_primaries = TRUE;
    }

    if (gst_structure_get_enum (config, GST_D3D12_CONVERTER_OPT_COLOR_BALANCE,
            GST_TYPE_D3D12_CONVERTER_COLOR_BALANCE, &value) &&
        (GstD3D12ConverterColorBalance) value !=
        GST_D3D12_CONVERTER_COLOR_BALANCE_DISABLED) {
      priv->color_balance_enabled = TRUE;
    }

    if (gst_structure_get_enum (config, GST_D3D12_CONVERTER_OPT_MIP_GEN,
            GST_TYPE_D3D12_CONVERTER_MIP_GEN, &value) &&
        (GstD3D12ConverterMipGen) value !=
        GST_D3D12_CONVERTER_MIP_GEN_DISABLED) {
      priv->mipgen_enabled = TRUE;
    }

    gst_structure_get_enum (config, GST_D3D12_CONVERTER_OPT_SAMPLER_FILTER,
        GST_TYPE_D3D12_CONVERTER_SAMPLER_FILTER, (int *) &sampler_filter);

    gst_structure_get_enum (config, GST_D3D12_CONVERTER_OPT_SRC_ALPHA_MODE,
        GST_TYPE_D3D12_CONVERTER_ALPHA_MODE, (int *) &priv->src_alpha_mode);
    gst_structure_get_enum (config, GST_D3D12_CONVERTER_OPT_DEST_ALPHA_MODE,
        GST_TYPE_D3D12_CONVERTER_ALPHA_MODE, (int *) &priv->dst_alpha_mode);

    gst_structure_get_uint (config,
        GST_D3D12_CONVERTER_OPT_PSO_SAMPLE_DESC_COUNT, &sample_count);
    gst_structure_get_uint (config,
        GST_D3D12_CONVERTER_OPT_PSO_SAMPLE_DESC_QUALITY, &sample_quality);

    priv->sample_desc.Count = sample_count;
    priv->sample_desc.Quality = sample_quality;

    gst_structure_free (config);
  }

  GST_DEBUG_OBJECT (self,
      "Setup converter with format %s -> %s, "
      "allow gamma conversion: %d, allow primaries conversion: %d ",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)),
      allow_gamma, allow_primaries);

  self->device = (GstD3D12Device *) gst_object_ref (device);
  gst_d3d12_unpack_get_video_info (priv->unpack, &priv->in_info);
  gst_d3d12_pack_get_video_info (priv->pack, &priv->out_info);

  priv->mipgen_info = priv->in_info;

  /* Init properties */
  priv->src_width = GST_VIDEO_INFO_WIDTH (in_info);
  priv->src_height = GST_VIDEO_INFO_HEIGHT (in_info);
  priv->dest_width = GST_VIDEO_INFO_WIDTH (out_info);
  priv->dest_height = GST_VIDEO_INFO_HEIGHT (out_info);
  priv->input_texture_width = GST_VIDEO_INFO_WIDTH (in_info);
  priv->input_texture_height = GST_VIDEO_INFO_HEIGHT (in_info);

  auto device_handle = gst_d3d12_device_get_device_handle (device);
  priv->srv_inc_size = device_handle->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  priv->rtv_inc_size = device_handle->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  priv->sampler_inc_size = device_handle->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

  if (GST_VIDEO_INFO_IS_RGB (&priv->out_info)) {
    GstVideoInfo rgb_info = priv->out_info;
    rgb_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
    gst_d3d12_color_range_adjust_matrix_unorm (&rgb_info, &priv->out_info,
        &priv->clear_color_matrix);
  } else {
    GstVideoInfo rgb_info;
    GstVideoInfo yuv_info;

    gst_video_info_set_format (&rgb_info, GST_VIDEO_FORMAT_RGBA64_LE,
        priv->out_info.width, priv->out_info.height);
    convert_info_gray_to_yuv (&priv->out_info, &yuv_info);

    if (yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN ||
        yuv_info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
      GST_WARNING_OBJECT (self, "Invalid matrix is detected");
      yuv_info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
    }

    gst_d3d12_rgb_to_yuv_matrix_unorm (&rgb_info,
        &yuv_info, &priv->clear_color_matrix);
  }

  gst_d3d12_converter_calculate_border_color (self);

  PSConstBuffer const_data[2];
  CONVERT_TYPE convert_type[2];
  gboolean have_lut = FALSE;

  if (priv->mipgen_enabled) {
    GstVideoFormat mipgen_format = GST_VIDEO_FORMAT_RGBA;
    GstD3DPluginCS mipgen_cs_type = GST_D3D_PLUGIN_CS_MIP_GEN;
    if (GST_VIDEO_INFO_IS_GRAY (&priv->in_info)) {
      mipgen_cs_type = GST_D3D_PLUGIN_CS_MIP_GEN_GRAY;
      if (GST_VIDEO_INFO_COMP_DEPTH (&priv->in_info, 0) > 8) {
        mipgen_format = GST_VIDEO_FORMAT_GRAY16_LE;
      } else {
        mipgen_format = GST_VIDEO_FORMAT_GRAY8;
      }
    } else if (GST_VIDEO_INFO_IS_YUV (&priv->in_info)) {
      if (GST_VIDEO_INFO_COMP_DEPTH (&priv->in_info, 0) > 8) {
        mipgen_format = GST_VIDEO_FORMAT_AYUV64;
        if (!GST_VIDEO_INFO_HAS_ALPHA (in_info)) {
          mipgen_cs_type = GST_D3D_PLUGIN_CS_MIP_GEN_AYUV;
        }
      } else {
        mipgen_format = GST_VIDEO_FORMAT_VUYA;
        if (!GST_VIDEO_INFO_HAS_ALPHA (in_info)) {
          mipgen_cs_type = GST_D3D_PLUGIN_CS_MIP_GEN_VUYA;
        }
      }
    } else {
      if (GST_VIDEO_INFO_COMP_DEPTH (&priv->in_info, 0) > 8) {
        mipgen_format = GST_VIDEO_FORMAT_RGBA64_LE;
      } else {
        mipgen_format = GST_VIDEO_FORMAT_RGBA;
      }
    }

    gst_video_info_set_format (&priv->mipgen_info, mipgen_format,
        in_info->width, in_info->height);
    priv->mipgen_info.colorimetry = in_info->colorimetry;

    /* Create intermediate conversion pipeline if input format is not
     * a supported mip format */
    if (mipgen_format != GST_VIDEO_INFO_FORMAT (&priv->in_info)) {
      if (!gst_d3d12_converter_setup_colorspace (self, &priv->in_info,
              &priv->mipgen_info, FALSE, FALSE, FALSE, &have_lut, convert_type,
              const_data)) {
        gst_object_unref (self);
        return nullptr;
      }

      DXGI_SAMPLE_DESC sample_desc_default = { };
      sample_desc_default.Count = 1;
      sample_desc_default.Quality = 0;
      D3D12_BLEND_DESC blend_desc_default = CD3DX12_BLEND_DESC (D3D12_DEFAULT);

      priv->mipgen_ctx =
          gst_d3d12_converter_setup_resource (self, &priv->in_info,
          &priv->mipgen_info, DEFAULT_SAMPLER_FILTER, &sample_desc_default,
          &blend_desc_default, convert_type, have_lut, FALSE,
          GST_D3D12_CONVERTER_ALPHA_MODE_STRAIGHT,
          GST_D3D12_CONVERTER_ALPHA_MODE_STRAIGHT, const_data, nullptr);
      if (!priv->mipgen_ctx) {
        gst_object_unref (self);
        return nullptr;
      }
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = { };
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.NumDescriptors = 1;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    priv->mipgen_srv_heap_pool = gst_d3d12_desc_heap_pool_new (device_handle,
        &srv_heap_desc);

    priv->mipgen = gst_d3d12_mip_gen_new (self->device, mipgen_cs_type);
    if (!priv->mipgen) {
      GST_ERROR_OBJECT (self, "Couldn't create mipgen object");
      gst_object_unref (self);
      return nullptr;
    }

    GstD3D12Format mipgen_dev_format;
    gst_d3d12_device_get_format (self->device,
        mipgen_format, &mipgen_dev_format);
    DXGI_FORMAT mipgen_dxgi_format = mipgen_dev_format.dxgi_format;
    if (mipgen_dxgi_format == DXGI_FORMAT_UNKNOWN)
      mipgen_dxgi_format = mipgen_dev_format.resource_format[0];

    priv->mipgen_desc = CD3DX12_RESOURCE_DESC::Tex2D (mipgen_dxgi_format,
        priv->in_info.width, priv->in_info.height, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    priv->mipgen_srv_desc.Format = mipgen_dxgi_format;
    if (mipgen_dxgi_format == DXGI_FORMAT_AYUV)
      priv->mipgen_srv_desc.Format = mipgen_dev_format.resource_format[0];

    priv->mipgen_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    priv->mipgen_srv_desc.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    priv->mipgen_srv_desc.Texture2D.PlaneSlice = 0;
    priv->mipgen_srv_desc.Texture2D.MipLevels = 1;
  }

  if (!gst_d3d12_converter_setup_colorspace (self, &priv->in_info,
          &priv->out_info, allow_gamma, allow_primaries,
          priv->color_balance_enabled, &have_lut, convert_type, const_data)) {
    gst_object_unref (self);
    return nullptr;
  }

  priv->main_ctx = gst_d3d12_converter_setup_resource (self, &priv->in_info,
      &priv->out_info, sampler_filter, &priv->sample_desc, &priv->blend_desc,
      convert_type, have_lut, priv->color_balance_enabled,
      priv->src_alpha_mode, priv->dst_alpha_mode, const_data, nullptr);
  if (!priv->main_ctx) {
    gst_object_unref (self);
    return nullptr;
  }

  if (priv->mipgen_ctx) {
    if (!gst_d3d12_converter_setup_colorspace (self, &priv->mipgen_info,
            &priv->out_info, allow_gamma, allow_primaries,
            priv->color_balance_enabled, &have_lut, convert_type, const_data)) {
      gst_object_unref (self);
      return nullptr;
    }

    priv->post_mipgen_ctx = gst_d3d12_converter_setup_resource (self,
        &priv->mipgen_info, &priv->out_info, sampler_filter, &priv->sample_desc,
        &priv->blend_desc, convert_type, have_lut, priv->color_balance_enabled,
        priv->src_alpha_mode, priv->dst_alpha_mode, const_data,
        priv->main_ctx->comm);
    if (!priv->post_mipgen_ctx) {
      gst_object_unref (self);
      return nullptr;
    }
  }

  return self;
}

static gboolean
gst_d3d12_converter_update_context_pso (GstD3D12Converter * self,
    ConvertCtxPtr ctx)
{
  auto priv = self->priv;

  auto device = gst_d3d12_device_get_device_handle (self->device);
  for (size_t i = 0; i < ctx->pipeline_data.size (); i++) {
    auto & pipeline_data = ctx->pipeline_data[i];

    std::vector < QuadData > quad_data;
    quad_data.resize (pipeline_data.quad_data.size ());

    for (size_t j = 0; j < quad_data.size (); j++) {
      D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc =
          pipeline_data.quad_data[j].desc;
      pso_desc.BlendState = priv->blend_desc;
      pso_desc.SampleDesc = priv->sample_desc;

      ComPtr < ID3D12PipelineState > pso;
      auto hr =
          device->CreateGraphicsPipelineState (&pso_desc, IID_PPV_ARGS (&pso));
      if (!gst_d3d12_result (hr, self->device)) {
        GST_ERROR_OBJECT (self, "Couldn't create pso");
        return FALSE;
      }

      quad_data[j].desc = pso_desc;
      quad_data[j].pso = pso;
      quad_data[j].num_rtv = pipeline_data.quad_data[j].num_rtv;
    }

    pipeline_data.quad_data = quad_data;
  }

  return TRUE;
}

static gboolean
gst_d3d12_converter_update_pso (GstD3D12Converter * self)
{
  auto priv = self->priv;
  if (!priv->update_pso)
    return TRUE;

  priv->update_pso = FALSE;

  if (!gst_d3d12_converter_update_context_pso (self, priv->main_ctx))
    return FALSE;

  if (!priv->post_mipgen_ctx)
    return TRUE;

  return gst_d3d12_converter_update_context_pso (self, priv->post_mipgen_ctx);
}

static void
gst_d3d12_converter_update_sampler (GstD3D12Converter * self)
{
  auto priv = self->priv;
  if (!priv->update_sampler)
    return;

  priv->update_sampler = FALSE;
  auto comm = priv->main_ctx->comm;
  ComPtr < ID3D12DescriptorHeap > sampler_heap;
  if (gst_d3d12_converter_create_sampler (self, comm->sampler_filter,
          &sampler_heap)) {
    comm->sampler_heap = sampler_heap;
  }
}

static void
reorder_rtv_handles (GstVideoFormat output_format,
    D3D12_CPU_DESCRIPTOR_HANDLE * src, D3D12_CPU_DESCRIPTOR_HANDLE * dst)
{
  for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    auto index = reorder_rtv_index (output_format, i);
    dst[i] = src[index];
  }
}

static gboolean
gst_d3d12_converter_execute (GstD3D12Converter * self, GstD3D12Frame * in_frame,
    GstD3D12Frame * out_frame, ConvertCtxPtr & ctx, gboolean is_internal,
    GstD3D12FenceData * fence_data, ID3D12GraphicsCommandList * cl)
{
  auto priv = self->priv;
  auto & comm = ctx->comm;

  if (!is_internal) {
    auto desc = GetDesc (in_frame->data[0]);
    if (desc.Width != priv->input_texture_width ||
        desc.Height != priv->input_texture_height) {
      GST_DEBUG_OBJECT (self, "Texture resolution changed %ux%u -> %ux%u",
          (guint) priv->input_texture_width, priv->input_texture_height,
          (guint) desc.Width, desc.Height);
      priv->input_texture_width = desc.Width;
      priv->input_texture_height = desc.Height;
      priv->update_src_rect = TRUE;
    }

    desc = GetDesc (out_frame->data[0]);
    if (desc.SampleDesc.Count != priv->sample_desc.Count ||
        desc.SampleDesc.Quality != priv->sample_desc.Quality) {
      GST_DEBUG_OBJECT (self, "Sample desc updated");
      priv->sample_desc = desc.SampleDesc;
      priv->update_pso = TRUE;
    }

    if (!gst_d3d12_converter_update_dest_rect (self)) {
      GST_ERROR_OBJECT (self, "Failed to update dest rect");
      return FALSE;
    }

    if (!gst_d3d12_converter_update_src_rect (self)) {
      GST_ERROR_OBJECT (self, "Failed to update src rect");
      return FALSE;
    }

    if (!gst_d3d12_converter_update_transform (self)) {
      GST_ERROR_OBJECT (self, "Failed to update transform matrix");
      return FALSE;
    }

    if (!gst_d3d12_converter_update_pso (self)) {
      GST_ERROR_OBJECT (self, "Failed to update pso");
      return FALSE;
    }

    gst_d3d12_converter_update_sampler (self);
  }

  if (ctx->vertex_upload) {
    auto barrier =
        CD3DX12_RESOURCE_BARRIER::Transition (comm->shader_buf.Get (),
        STATE_VERTEX_AND_INDEX, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->ResourceBarrier (1, &barrier);

    cl->CopyBufferRegion (comm->shader_buf.Get (), 0,
        ctx->vertex_upload.Get (), 0, g_vertex_buf_size);
    barrier =
        CD3DX12_RESOURCE_BARRIER::Transition (comm->shader_buf.Get (),
        D3D12_RESOURCE_STATE_COPY_DEST, STATE_VERTEX_AND_INDEX);
    cl->ResourceBarrier (1, &barrier);

    GST_DEBUG_OBJECT (self, "Vertex updated");
  }

  auto device = gst_d3d12_device_get_device_handle (self->device);

  GstD3D12DescHeap *descriptor;
  if (!gst_d3d12_desc_heap_pool_acquire (priv->srv_heap_pool, &descriptor)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire srv heap");
    return FALSE;
  }

  auto srv_heap = gst_d3d12_desc_heap_get_handle (descriptor);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (descriptor));

  auto cpu_handle =
      CD3DX12_CPU_DESCRIPTOR_HANDLE (GetCPUDescriptorHandleForHeapStart
      (srv_heap));

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (&in_frame->info); i++) {
    device->CopyDescriptorsSimple (1, cpu_handle, in_frame->srv_desc_handle[i],
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpu_handle.Offset (priv->srv_inc_size);
  }

  if (comm->have_lut) {
    device->CopyDescriptorsSimple (2, cpu_handle,
        GetCPUDescriptorHandleForHeapStart (comm->gamma_lut_heap),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }

  if (priv->clear_background) {
    for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (&out_frame->info); i++) {
      cl->ClearRenderTargetView (out_frame->rtv_desc_handle[i],
          priv->clear_color[i], 1, &out_frame->plane_rect[i]);
    }
  }

  D3D12_CPU_DESCRIPTOR_HANDLE reordered_rtv_handle[GST_VIDEO_MAX_PLANES];
  reorder_rtv_handles (GST_VIDEO_INFO_FORMAT (&out_frame->info),
      out_frame->rtv_desc_handle, reordered_rtv_handle);

  guint pipeline_index = comm->need_color_balance ? 1 : 0;
  auto & pipeline_data = ctx->pipeline_data[pipeline_index];

  auto pso = pipeline_data.quad_data[0].pso.Get ();
  cl->SetGraphicsRootSignature (pipeline_data.rs.Get ());
  cl->SetPipelineState (pso);

  ID3D12DescriptorHeap *heaps[] = { srv_heap, comm->sampler_heap.Get () };
  cl->SetDescriptorHeaps (2, heaps);
  cl->SetGraphicsRootDescriptorTable (pipeline_data.crs->GetPsSrvIdx (),
      GetGPUDescriptorHandleForHeapStart (srv_heap));
  cl->SetGraphicsRootDescriptorTable (pipeline_data.crs->GetPsSamplerIdx (),
      GetGPUDescriptorHandleForHeapStart (comm->sampler_heap));
  cl->SetGraphicsRoot32BitConstants (pipeline_data.crs->GetVsRootConstIdx (),
      16, &priv->transform, 0);
  cl->SetGraphicsRoot32BitConstants (pipeline_data.crs->GetPsRootConstIdx (),
      sizeof (comm->const_data_dyn) / 4, &comm->const_data_dyn, 0);
  cl->SetGraphicsRootConstantBufferView (pipeline_data.crs->GetPsCbvIdx (),
      comm->const_buf_addr[pipeline_index]);

  cl->IASetIndexBuffer (&comm->ibv);
  cl->IASetVertexBuffers (0, 1, &comm->vbv);
  cl->IASetPrimitiveTopology (D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  cl->RSSetViewports (1, comm->viewport);
  cl->RSSetScissorRects (1, comm->scissor_rect);
  cl->OMSetRenderTargets (pipeline_data.quad_data[0].num_rtv,
      reordered_rtv_handle, FALSE, nullptr);
  if (!is_internal)
    cl->OMSetBlendFactor (priv->blend_factor);
  cl->DrawIndexedInstanced (6, 1, 0, 0, 0);

  pso->AddRef ();
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_COM (pso));

  auto offset = pipeline_data.quad_data[0].num_rtv;
  if (pipeline_data.quad_data.size () == 2) {
    pso = pipeline_data.quad_data[1].pso.Get ();

    cl->SetPipelineState (pso);
    cl->RSSetViewports (1, &comm->viewport[offset]);
    cl->RSSetScissorRects (1, &comm->scissor_rect[offset]);
    cl->OMSetRenderTargets (pipeline_data.quad_data[1].num_rtv,
        reordered_rtv_handle + offset, FALSE, nullptr);
    cl->DrawIndexedInstanced (6, 1, 0, 0, 0);

    pso->AddRef ();
    gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_COM (pso));
  }

  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_buffer_ref (in_frame->buffer)));
  if (ctx->vertex_upload) {
    gst_d3d12_fence_data_push (fence_data,
        FENCE_NOTIFY_COM (ctx->vertex_upload.Detach ()));
  }

  auto sampler = comm->sampler_heap.Get ();
  sampler->AddRef ();
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_COM (sampler));

  return TRUE;
}

static void
calculate_auto_mipgen_level (GstD3D12Converter * self)
{
  auto priv = self->priv;

  guint src_width = (guint) priv->mipgen_desc.Width;
  guint src_height = priv->mipgen_desc.Height;
  guint dst_width = priv->dest_width;
  guint dst_height = priv->dest_height;
  switch (priv->video_direction) {
    case GST_VIDEO_ORIENTATION_90R:
    case GST_VIDEO_ORIENTATION_90L:
    case GST_VIDEO_ORIENTATION_UL_LR:
    case GST_VIDEO_ORIENTATION_UR_LL:
      dst_width = priv->dest_height;
      dst_height = priv->dest_width;
      break;
    default:
      break;
  }

  for (UINT16 i = 0; i < priv->mipgen_desc.MipLevels; i++) {
    guint width = src_width >> i;
    guint height = src_height >> i;

    if (width <= dst_width && height <= dst_height) {
      priv->auto_mipgen_level = i + 1;
      return;
    }
  }

  priv->auto_mipgen_level = priv->mipgen_desc.MipLevels;
}

/**
 * gst_d3d12_converter_convert_buffer:
 * @converter: a #GstD3D12Converter
 * @in_buf: a #GstBuffer
 * @out_buf: a #GstBuffer
 * @fence_data: a #GstD3D12FenceData
 * @command_list: a ID3D12GraphicsCommandList
 * @execute_gpu_wait: Executes wait operation against @queue
 *
 * Records command list for conversion operation. converter will attach
 * conversion command associated resources such as command allocator
 * to @fence_data.
 *
 * If @execute_wait is %TRUE and buffers are associated with external fences,
 * this method will schedule GPU wait operation against @queue.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_converter_convert_buffer (GstD3D12Converter * converter,
    GstBuffer * in_buf, GstBuffer * out_buf, GstD3D12FenceData * fence_data,
    ID3D12GraphicsCommandList * command_list, gboolean execute_gpu_wait)
{
  g_return_val_if_fail (GST_IS_D3D12_CONVERTER (converter), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (in_buf), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (out_buf), FALSE);
  g_return_val_if_fail (fence_data, FALSE);
  g_return_val_if_fail (command_list, FALSE);

  GstD3D12Frame in_frame;
  GstD3D12Frame out_frame;

  auto priv = converter->priv;
  std::lock_guard < std::mutex > lk (priv->prop_lock);

  auto render_target = gst_d3d12_pack_acquire_render_target (priv->pack,
      out_buf);
  if (!render_target) {
    GST_ERROR_OBJECT (converter, "Couldn't get render target buffer");
    return FALSE;
  }

  in_buf = gst_d3d12_unpack_execute (priv->unpack, in_buf, fence_data,
      command_list);
  if (!in_buf) {
    GST_ERROR_OBJECT (converter, "Preprocessing failed");
    gst_buffer_unref (render_target);
    return FALSE;
  }

  /* Don't map output memory, we don't actually update output memory here */
  if (!gst_d3d12_frame_map (&out_frame, &priv->out_info, render_target,
          (GstMapFlags) GST_MAP_D3D12, GST_D3D12_FRAME_MAP_FLAG_RTV)) {
    GST_ERROR_OBJECT (converter, "Couldn't map output buffer");
    gst_buffer_unref (render_target);
    gst_buffer_unref (in_buf);
    return FALSE;
  }

  if (!gst_d3d12_frame_map (&in_frame, &priv->in_info,
          in_buf, GST_MAP_READ_D3D12, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
    GST_ERROR_OBJECT (converter, "Couldn't map fallback input");
    gst_d3d12_frame_unmap (&out_frame);
    gst_buffer_unref (render_target);
    gst_buffer_unref (in_buf);
    return FALSE;
  }

  gboolean ret = TRUE;
  guint mip_levels = 1;
  auto in_desc = GetDesc (in_frame.data[0]);

  if (priv->mipgen_enabled) {
    if (in_desc.Width != priv->mipgen_desc.Width ||
        in_desc.Height != priv->mipgen_desc.Height) {
      gst_clear_buffer (&priv->mipgen_buf);
      priv->mipgen_desc.Width = in_desc.Width;
      priv->mipgen_desc.Height = in_desc.Height;
      if (priv->mipgen_ctx) {
        auto & comm = priv->mipgen_ctx->comm;
        comm->viewport[0].Width = (FLOAT) in_desc.Width;
        comm->viewport[0].Height = (FLOAT) in_desc.Height;
        comm->scissor_rect[0].right = (LONG) in_desc.Width;
        comm->scissor_rect[0].bottom = (LONG) in_desc.Height;
      }
    }

    if (priv->mip_levels != 1 && !priv->mipgen_buf) {
      D3D12_HEAP_PROPERTIES heap_props =
          CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
      D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
      if (gst_d3d12_device_non_zeroed_supported (converter->device))
        heap_flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

      priv->mipgen_desc.MipLevels = 0;
      auto mem = gst_d3d12_allocator_alloc (nullptr, converter->device,
          &heap_props, heap_flags, &priv->mipgen_desc,
          D3D12_RESOURCE_STATE_COMMON, nullptr);
      priv->mipgen_desc.MipLevels = 1;
      if (!mem) {
        GST_ERROR_OBJECT (converter, "Couldn't allocate mipmap texture");
        gst_d3d12_frame_unmap (&in_frame);
        gst_d3d12_frame_unmap (&out_frame);

        gst_buffer_unref (in_buf);
        gst_buffer_unref (render_target);

        return FALSE;
      }

      auto resource =
          gst_d3d12_memory_get_resource_handle (GST_D3D12_MEMORY_CAST (mem));
      priv->mipgen_desc = GetDesc (resource);

      priv->mipgen_buf = gst_buffer_new ();
      gst_buffer_append_memory (priv->mipgen_buf, mem);

      guint dst_width = priv->dest_width;
      guint dst_height = priv->dest_height;
      switch (priv->video_direction) {
        case GST_VIDEO_ORIENTATION_90R:
        case GST_VIDEO_ORIENTATION_90L:
        case GST_VIDEO_ORIENTATION_UL_LR:
        case GST_VIDEO_ORIENTATION_UR_LL:
          dst_width = priv->dest_height;
          dst_height = priv->dest_width;
          break;
        default:
          break;
      }

      calculate_auto_mipgen_level (converter);
      GST_DEBUG_OBJECT (converter, "Calculated mip level %d",
          priv->auto_mipgen_level);
    }
  }

  if (priv->mipgen_enabled && priv->mip_levels != 1) {
    if (priv->mip_levels == 0) {
      mip_levels = priv->mipgen_desc.MipLevels;
    } else {
      mip_levels = MIN (priv->mip_levels, priv->mipgen_desc.MipLevels);
    }

    if (priv->update_transform || priv->update_dest_rect) {
      calculate_auto_mipgen_level (converter);
      GST_DEBUG_OBJECT (converter,
          "Calculated mip level on viewport size change %d",
          priv->auto_mipgen_level);
    }

    if (mip_levels > 1)
      mip_levels = MIN (mip_levels, priv->auto_mipgen_level);

    /* Do not need to generate mipmap if input texture has mipmap already */
    if (in_desc.MipLevels >= mip_levels)
      mip_levels = 1;
  }

  if (priv->mipgen_enabled && mip_levels != 1) {
    GST_LOG_OBJECT (converter, "Generating mipmap");

    GstD3D12Frame mipgen_frame;
    if (!gst_d3d12_frame_map (&mipgen_frame, &priv->mipgen_info,
            priv->mipgen_buf,
            GST_MAP_D3D12, GST_D3D12_FRAME_MAP_FLAG_SRV |
            GST_D3D12_FRAME_MAP_FLAG_RTV)) {
      GST_ERROR_OBJECT (converter, "Couldn't map mipmap texture");

      gst_d3d12_frame_unmap (&in_frame);
      gst_d3d12_frame_unmap (&out_frame);

      gst_buffer_unref (in_buf);
      gst_buffer_unref (render_target);

      return FALSE;
    }

    if (priv->mipgen_ctx) {
      if (!gst_d3d12_converter_execute (converter, &in_frame, &mipgen_frame,
              priv->mipgen_ctx, TRUE, fence_data, command_list)) {
        GST_ERROR_OBJECT (converter, "Couldn't convert to mipmap format");
        gst_d3d12_frame_unmap (&in_frame);
        gst_d3d12_frame_unmap (&mipgen_frame);
        gst_d3d12_frame_unmap (&out_frame);

        gst_buffer_unref (in_buf);
        gst_buffer_unref (render_target);

        return FALSE;
      }

      auto barrier = CD3DX12_RESOURCE_BARRIER::Transition (mipgen_frame.data[0],
          D3D12_RESOURCE_STATE_RENDER_TARGET,
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);
      command_list->ResourceBarrier (1, &barrier);
    } else {
      D3D12_BOX src_box;
      src_box.left = 0;
      src_box.top = 0;
      src_box.right = (UINT) priv->mipgen_desc.Width;
      src_box.bottom = (UINT) priv->mipgen_desc.Height;
      src_box.front = 0;
      src_box.back = 1;

      auto copy_src = CD3DX12_TEXTURE_COPY_LOCATION (in_frame.data[0], 0);
      auto copy_dst = CD3DX12_TEXTURE_COPY_LOCATION (mipgen_frame.data[0], 0);
      command_list->CopyTextureRegion (&copy_dst, 0, 0, 0, &copy_src, &src_box);

      auto barrier = CD3DX12_RESOURCE_BARRIER::Transition (mipgen_frame.data[0],
          D3D12_RESOURCE_STATE_COPY_DEST,
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);
      command_list->ResourceBarrier (1, &barrier);
    }

    ret = gst_d3d12_mip_gen_execute_full (priv->mipgen, mipgen_frame.data[0],
        fence_data, command_list, mip_levels,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (!ret) {
      GST_ERROR_OBJECT (converter, "Couldn't generate mip levels");
      gst_d3d12_frame_unmap (&in_frame);
      gst_d3d12_frame_unmap (&mipgen_frame);
      gst_d3d12_frame_unmap (&out_frame);

      gst_buffer_unref (in_buf);
      gst_buffer_unref (render_target);

      return FALSE;
    }

    if (mip_levels != priv->mipgen_desc.MipLevels) {
      GstD3D12DescHeap *desc_heap;
      if (!gst_d3d12_desc_heap_pool_acquire (priv->mipgen_srv_heap_pool,
              &desc_heap)) {
        GST_ERROR_OBJECT (converter, "Couldn't acquire descriptor heap");
        gst_d3d12_frame_unmap (&in_frame);
        gst_d3d12_frame_unmap (&mipgen_frame);
        gst_d3d12_frame_unmap (&out_frame);

        gst_buffer_unref (in_buf);
        gst_buffer_unref (render_target);

        return FALSE;
      }

      auto srv_heap = gst_d3d12_desc_heap_get_handle (desc_heap);
      auto cpu_handle = GetCPUDescriptorHandleForHeapStart (srv_heap);
      gst_d3d12_fence_data_push (fence_data,
          FENCE_NOTIFY_MINI_OBJECT (desc_heap));

      auto device = gst_d3d12_device_get_device_handle (converter->device);
      priv->mipgen_srv_desc.Texture2D.MipLevels = mip_levels;
      device->CreateShaderResourceView (mipgen_frame.data[0],
          &priv->mipgen_srv_desc, cpu_handle);

      mipgen_frame.srv_desc_handle[0] = cpu_handle;
    }

    if (priv->post_mipgen_ctx) {
      ret = gst_d3d12_converter_execute (converter,
          &mipgen_frame, &out_frame, priv->post_mipgen_ctx,
          FALSE, fence_data, command_list);
    } else {
      ret = gst_d3d12_converter_execute (converter,
          &mipgen_frame, &out_frame, priv->main_ctx,
          FALSE, fence_data, command_list);
    }

    gst_d3d12_frame_unmap (&mipgen_frame);
  } else {
    ret = gst_d3d12_converter_execute (converter,
        &in_frame, &out_frame, priv->main_ctx, FALSE, fence_data, command_list);
  }

  if (ret) {
    ret = gst_d3d12_pack_execute (priv->pack, render_target, out_buf,
        fence_data, command_list);
  }

  if (ret && execute_gpu_wait) {
    gst_d3d12_frame_fence_gpu_wait (&in_frame, priv->cq);
    gst_d3d12_frame_fence_gpu_wait (&out_frame, priv->cq);
  }

  priv->main_ctx->waitSetup ();

  gst_d3d12_frame_unmap (&in_frame);
  gst_d3d12_frame_unmap (&out_frame);

  gst_buffer_unref (in_buf);
  gst_buffer_unref (render_target);

  return ret;
}

/**
 * gst_d3d12_converter_update_blend_state:
 * @converter: a #GstD3D12Converter
 * @blend_desc: (nullable): D3D12_BLEND_DESC
 * @blend_factor: (nullable): blend factor values
 *
 * Updates pipeline state object with new @blend_desc. If @blend_desc is %NULL,
 * pipeline state object will be updated with default blend state
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_converter_update_blend_state (GstD3D12Converter * converter,
    const D3D12_BLEND_DESC * blend_desc, const gfloat blend_factor[4])
{
  g_return_val_if_fail (GST_IS_D3D12_CONVERTER (converter), FALSE);

  auto priv = converter->priv;
  std::lock_guard < std::mutex > lk (priv->prop_lock);
  D3D12_BLEND_DESC new_blend = CD3DX12_BLEND_DESC (D3D12_DEFAULT);

  if (blend_desc)
    new_blend = *blend_desc;

  if (memcmp (&priv->blend_desc, &new_blend, sizeof (D3D12_BLEND_DESC)) != 0)
    priv->update_pso = TRUE;

  if (blend_factor) {
    for (guint i = 0; i < 4; i++)
      priv->blend_factor[i] = blend_factor[i];
  } else {
    for (guint i = 0; i < 4; i++)
      priv->blend_factor[i] = 1.0f;
  }

  return TRUE;
}

gboolean
gst_d3d12_converter_apply_transform (GstD3D12Converter * converter,
    GstVideoOrientationMethod orientation, gfloat viewport_width,
    gfloat viewport_height, gfloat fov, gboolean ortho, gfloat rotation_x,
    gfloat rotation_y, gfloat rotation_z, gfloat scale_x, gfloat scale_y)
{
  g_return_val_if_fail (GST_IS_D3D12_CONVERTER (converter), FALSE);

  auto priv = converter->priv;
  std::lock_guard < std::mutex > lk (priv->prop_lock);

  gfloat aspect_ratio;
  gboolean rotated = FALSE;
  XMMATRIX rotate_matrix = XMMatrixIdentity ();

  switch (orientation) {
    case GST_VIDEO_ORIENTATION_IDENTITY:
    case GST_VIDEO_ORIENTATION_AUTO:
    case GST_VIDEO_ORIENTATION_CUSTOM:
    default:
      break;
    case GST_VIDEO_ORIENTATION_90R:
      rotate_matrix = XMLoadFloat4x4A (&g_matrix_90r);
      rotated = TRUE;
      break;
    case GST_VIDEO_ORIENTATION_180:
      rotate_matrix = XMLoadFloat4x4A (&g_matrix_180);
      break;
    case GST_VIDEO_ORIENTATION_90L:
      rotate_matrix = XMLoadFloat4x4A (&g_matrix_90l);
      rotated = TRUE;
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      rotate_matrix = XMLoadFloat4x4A (&g_matrix_horiz);
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      rotate_matrix = XMLoadFloat4x4A (&g_matrix_vert);
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      rotate_matrix = XMLoadFloat4x4A (&g_matrix_ul_lr);
      rotated = TRUE;
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      rotate_matrix = XMLoadFloat4x4A (&g_matrix_ur_ll);
      rotated = TRUE;
      break;
  }

  if (rotated)
    aspect_ratio = viewport_height / viewport_width;
  else
    aspect_ratio = viewport_width / viewport_height;

  /* Apply user specified transform matrix first, then rotate-method */
  XMMATRIX scale = XMMatrixScaling (scale_x * aspect_ratio, scale_y, 1.0);

  XMMATRIX rotate =
      XMMatrixRotationX (XMConvertToRadians (rotation_x)) *
      XMMatrixRotationY (XMConvertToRadians (-rotation_y)) *
      XMMatrixRotationZ (XMConvertToRadians (-rotation_z));

  XMMATRIX view = XMMatrixLookAtLH (XMVectorSet (0.0, 0.0, -1.0, 0.0),
      XMVectorSet (0.0, 0.0, 0.0, 0.0), XMVectorSet (0.0, 1.0, 0.0, 0.0));

  XMMATRIX proj;
  if (ortho) {
    proj = XMMatrixOrthographicOffCenterLH (-aspect_ratio,
        aspect_ratio, -1.0, 1.0, 0.1, 100.0);
  } else {
    proj = XMMatrixPerspectiveFovLH (XMConvertToRadians (fov),
        aspect_ratio, 0.1, 100.0);
  }

  XMMATRIX mvp = scale * rotate * view * proj * rotate_matrix;
  XMStoreFloat4x4A (&priv->custom_transform, mvp);
  priv->update_transform = TRUE;
  priv->video_direction = GST_VIDEO_ORIENTATION_CUSTOM;

  return TRUE;
}

gboolean
gst_d3d12_converter_is_color_balance_needed (gfloat hue, gfloat saturation,
    gfloat brightness, gfloat contrast)
{
  const float min_diff = 0.0000000001f;
  if (fabsf (hue - DEFAULT_HUE) >= min_diff ||
      fabsf (saturation - DEFAULT_SATURATION) >= min_diff ||
      fabsf (brightness - DEFAULT_BRIGHTNESS) >= min_diff ||
      fabsf (contrast - DEFAULT_CONTRAST) >= min_diff) {
    return TRUE;
  }

  return FALSE;
}
