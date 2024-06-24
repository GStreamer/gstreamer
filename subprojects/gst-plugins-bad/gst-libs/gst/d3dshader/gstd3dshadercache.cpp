/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#include "gstd3dshadercache.h"
#include "gstd3dcompile.h"
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <utility>
#include <wrl.h>
#include "converter-hlsl/hlsl.h"
#include "plugin-hlsl/hlsl.h"

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

#ifdef HLSL_PRECOMPILED
#include "converter_hlsl_ps.h"
#include "converter_hlsl_vs.h"
#include "converter_hlsl_cs.h"
#include "plugin_hlsl_ps.h"
#include "plugin_hlsl_vs.h"
#else
static std::unordered_map<std::string, std::pair<const BYTE *, SIZE_T>> g_converter_ps_table;
static std::unordered_map<std::string, std::pair<const BYTE *, SIZE_T>> g_converter_vs_table;
static std::unordered_map<std::string, std::pair<const BYTE *, SIZE_T>> g_converter_cs_table;
static std::unordered_map<std::string, std::pair<const BYTE *, SIZE_T>> g_plugin_ps_table;
static std::unordered_map<std::string, std::pair<const BYTE *, SIZE_T>> g_plugin_vs_table;
#endif

static std::vector<std::pair<std::string, ID3DBlob *>> g_compiled_blobs;
static std::mutex g_blob_lock;

/* *INDENT-ON* */

struct ShaderItem
{
  guint type;
  const gchar *name;
  const gchar *source;
  gsize source_size;
};

#define BUILD_SOURCE(name) G_STRINGIFY (name), str_ ##name, sizeof (str_##name)

static const ShaderItem g_ps_map[] = {
  {GST_D3D_PLUGIN_PS_CHECKER_LUMA, BUILD_SOURCE (PSMain_checker_luma)},
  {GST_D3D_PLUGIN_PS_CHECKER_RGB, BUILD_SOURCE (PSMain_checker_rgb)},
  {GST_D3D_PLUGIN_PS_CHECKER_VUYA, BUILD_SOURCE (PSMain_checker_vuya)},
  {GST_D3D_PLUGIN_PS_CHECKER, BUILD_SOURCE (PSMain_checker)},
  {GST_D3D_PLUGIN_PS_COLOR, BUILD_SOURCE (PSMain_color)},
  {GST_D3D_PLUGIN_PS_SAMPLE_PREMULT, BUILD_SOURCE (PSMain_sample_premul)},
  {GST_D3D_PLUGIN_PS_SAMPLE, BUILD_SOURCE (PSMain_sample)},
  {GST_D3D_PLUGIN_PS_SNOW, BUILD_SOURCE (PSMain_snow)},
};

static const ShaderItem g_vs_map[] = {
  {GST_D3D_PLUGIN_VS_COLOR, BUILD_SOURCE (VSMain_color)},
  {GST_D3D_PLUGIN_VS_COORD, BUILD_SOURCE (VSMain_coord)},
  {GST_D3D_PLUGIN_VS_POS, BUILD_SOURCE (VSMain_pos)},
};

#undef BUILD_SOURCE

static const gchar * g_sm_map[] = {
  "4_0",
  "5_0",
  "5_1",
};

gboolean
gst_d3d_plugin_shader_get_vs_blob (GstD3DPluginVS type,
    GstD3DShaderModel shader_model, GstD3DShaderByteCode * byte_code)
{
  g_return_val_if_fail (type < GST_D3D_PLUGIN_VS_LAST, FALSE);
  g_return_val_if_fail (shader_model < GST_D3D_SM_LAST, FALSE);
  g_return_val_if_fail (byte_code, FALSE);

  static std::mutex cache_lock;

  auto shader_name = std::string (g_vs_map[type].name) + "_" +
      std::string (g_sm_map[shader_model]);

  std::lock_guard <std::mutex> lk (cache_lock);
  auto it = g_plugin_vs_table.find (shader_name);
  if (it != g_plugin_vs_table.end ()) {
    byte_code->byte_code = it->second.first;
    byte_code->byte_code_len = it->second.second;

    return TRUE;
  }

  auto target = std::string ("vs_") + g_sm_map[shader_model];

  ID3DBlob *blob = nullptr;
  ComPtr<ID3DBlob> error_msg;

  auto hr = gst_d3d_compile (g_vs_map[type].source, g_vs_map[type].source_size,
      nullptr, nullptr, nullptr, "ENTRY_POINT", target.c_str (), 0, 0,
      &blob, &error_msg);
  if (FAILED (hr)) {
    const gchar *err = nullptr;
    if (error_msg)
      err = (const gchar *) error_msg->GetBufferPointer ();

    GST_ERROR ("Couldn't compile code, hr: 0x%x, error detail: %s, "
        "source code: \n%s", (guint) hr, GST_STR_NULL (err),
        g_ps_map[type].source);
    return FALSE;
  }

  byte_code->byte_code = blob->GetBufferPointer ();
  byte_code->byte_code_len = blob->GetBufferSize ();

  g_plugin_vs_table[shader_name] = { (const BYTE *) blob->GetBufferPointer (),
      blob->GetBufferSize ()};

  std::lock_guard <std::mutex> blk (g_blob_lock);
  g_compiled_blobs.push_back ({ shader_name, blob });

  return TRUE;
}

gboolean
gst_d3d_plugin_shader_get_ps_blob (GstD3DPluginPS type,
    GstD3DShaderModel shader_model, GstD3DShaderByteCode * byte_code)
{
  g_return_val_if_fail (type < GST_D3D_PLUGIN_PS_LAST, FALSE);
  g_return_val_if_fail (shader_model < GST_D3D_SM_LAST, FALSE);
  g_return_val_if_fail (byte_code, FALSE);

  static std::mutex cache_lock;

  auto shader_name = std::string (g_ps_map[type].name) + "_" +
      std::string (g_sm_map[shader_model]);

  std::lock_guard <std::mutex> lk (cache_lock);
  auto it = g_plugin_ps_table.find (shader_name);
  if (it != g_plugin_ps_table.end ()) {
    byte_code->byte_code = it->second.first;
    byte_code->byte_code_len = it->second.second;

    return TRUE;
  }

  auto target = std::string ("ps_") + g_sm_map[shader_model];

  ID3DBlob *blob = nullptr;
  ComPtr<ID3DBlob> error_msg;

  auto hr = gst_d3d_compile (g_ps_map[type].source, g_ps_map[type].source_size,
      nullptr, nullptr, nullptr, "ENTRY_POINT", target.c_str (), 0, 0,
      &blob, &error_msg);
  if (FAILED (hr)) {
    const gchar *err = nullptr;
    if (error_msg)
      err = (const gchar *) error_msg->GetBufferPointer ();

    GST_ERROR ("Couldn't compile code, hr: 0x%x, error detail: %s, "
        "source code: \n%s", (guint) hr, GST_STR_NULL (err),
        g_ps_map[type].source);
    return FALSE;
  }

  byte_code->byte_code = blob->GetBufferPointer ();
  byte_code->byte_code_len = blob->GetBufferSize ();

  g_plugin_ps_table[shader_name] = { (const BYTE *) blob->GetBufferPointer (),
      blob->GetBufferSize ()};

  std::lock_guard <std::mutex> blk (g_blob_lock);
  g_compiled_blobs.push_back ({ shader_name, blob });

  return TRUE;
}

gboolean
gst_d3d_converter_shader_get_vs_blob (GstD3DShaderModel shader_model,
    GstD3DShaderByteCode * byte_code)
{
  g_return_val_if_fail (shader_model < GST_D3D_SM_LAST, FALSE);
  g_return_val_if_fail (byte_code, FALSE);

  static std::mutex cache_lock;

  auto shader_name = std::string ("VSMain_converter_") +
      std::string (g_sm_map[shader_model]);

  std::lock_guard <std::mutex> lk (cache_lock);
  auto it = g_converter_vs_table.find (shader_name);
  if (it != g_converter_vs_table.end ()) {
    byte_code->byte_code = it->second.first;
    byte_code->byte_code_len = it->second.second;

    return TRUE;
  }

  auto target = std::string ("vs_") + g_sm_map[shader_model];

  ID3DBlob *blob = nullptr;
  ComPtr<ID3DBlob> error_msg;

  auto hr = gst_d3d_compile (str_VSMain_converter,
      sizeof (str_VSMain_converter),
      nullptr, nullptr, nullptr, "ENTRY_POINT", target.c_str (), 0, 0,
      &blob, &error_msg);
  if (FAILED (hr)) {
    const gchar *err = nullptr;
    if (error_msg)
      err = (const gchar *) error_msg->GetBufferPointer ();

    GST_ERROR ("Couldn't compile code, hr: 0x%x, error detail: %s, "
        "source code: \n%s", (guint) hr, GST_STR_NULL (err),
        str_VSMain_converter);
    return FALSE;
  }

  byte_code->byte_code = blob->GetBufferPointer ();
  byte_code->byte_code_len = blob->GetBufferSize ();

  g_converter_vs_table[shader_name] = {(const BYTE *) blob->GetBufferPointer (),
      blob->GetBufferSize ()};

  std::lock_guard <std::mutex> blk (g_blob_lock);
  g_compiled_blobs.push_back ({ shader_name, blob });

  return TRUE;
}

gboolean
gst_d3d_converter_shader_get_cs_blob (GstVideoFormat in_format,
    GstVideoFormat out_format, GstD3DShaderModel shader_model,
    GstD3DConverterCSByteCode * byte_code)
{
  g_return_val_if_fail (shader_model < GST_D3D_SM_LAST, FALSE);
  g_return_val_if_fail (byte_code, FALSE);

  static std::mutex cache_lock;

  DXGI_FORMAT srv_format = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT uav_format = DXGI_FORMAT_UNKNOWN;
  std::string in_format_str;
  std::string out_format_str;
  guint x_unit = 8;
  guint y_unit = 8;

  switch (in_format) {
    case GST_VIDEO_FORMAT_YUY2:
      srv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      in_format_str = "YUY2";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      srv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      in_format_str = "UYVY";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_VYUY:
      srv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      in_format_str = "VYUY";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_YVYU:
      srv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      in_format_str = "YVYU";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_Y210:
    case GST_VIDEO_FORMAT_Y212_LE:
    case GST_VIDEO_FORMAT_Y216_LE:
      srv_format = DXGI_FORMAT_R16G16B16A16_UNORM;
      in_format_str = "YUY2";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_v210:
      srv_format = DXGI_FORMAT_R10G10B10A2_UNORM;
      in_format_str = "v210";
      x_unit = 48;
      break;
    case GST_VIDEO_FORMAT_v216:
      srv_format = DXGI_FORMAT_R16G16B16A16_UNORM;
      in_format_str = "UYVY";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_v308:
      srv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      in_format_str = "v308";
      x_unit = 32;
      break;
    case GST_VIDEO_FORMAT_IYU2:
      srv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      in_format_str = "IYU2";
      x_unit = 32;
      break;
    case GST_VIDEO_FORMAT_RGB:
      srv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      in_format_str = "RGB";
      x_unit = 32;
      break;
    case GST_VIDEO_FORMAT_BGR:
      srv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      in_format_str = "BGR";
      x_unit = 32;
      break;
    case GST_VIDEO_FORMAT_RGB16:
      srv_format = DXGI_FORMAT_R16_UINT;
      in_format_str = "RGB16";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_BGR16:
      srv_format = DXGI_FORMAT_R16_UINT;
      in_format_str = "BGR16";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_RGB15:
      srv_format = DXGI_FORMAT_R16_UINT;
      in_format_str = "RGB15";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_BGR15:
      srv_format = DXGI_FORMAT_R16_UINT;
      in_format_str = "BGR15";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_r210:
      srv_format = DXGI_FORMAT_R32_UINT;
      in_format_str = "r210";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      srv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      in_format_str = "AYUV";
      break;
    case GST_VIDEO_FORMAT_AYUV64:
      srv_format = DXGI_FORMAT_R16G16B16A16_UNORM;
      in_format_str = "AYUV";
      break;
    case GST_VIDEO_FORMAT_RGBA:
      srv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      in_format_str = "RGBA";
      break;
    case GST_VIDEO_FORMAT_RGB10A2_LE:
      srv_format = DXGI_FORMAT_R10G10B10A2_UNORM;
      in_format_str = "RGBA";
      break;
    case GST_VIDEO_FORMAT_RGBA64_LE:
      srv_format = DXGI_FORMAT_R16G16B16A16_UNORM;
      in_format_str = "RGBA";
      break;
    default:
      return FALSE;
  }

  switch (out_format) {
    case GST_VIDEO_FORMAT_YUY2:
      uav_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out_format_str = "YUY2";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      uav_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out_format_str = "UYVY";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_VYUY:
      uav_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out_format_str = "VYUY";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_YVYU:
      uav_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out_format_str = "YVYU";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_Y210:
    case GST_VIDEO_FORMAT_Y212_LE:
    case GST_VIDEO_FORMAT_Y216_LE:
      uav_format = DXGI_FORMAT_R16G16B16A16_UNORM;
      out_format_str = "YUY2";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_v210:
      uav_format = DXGI_FORMAT_R10G10B10A2_UNORM;
      out_format_str = "v210";
      x_unit = 48;
      break;
    case GST_VIDEO_FORMAT_v216:
      uav_format = DXGI_FORMAT_R16G16B16A16_UNORM;
      out_format_str = "UYVY";
      x_unit = 16;
      break;
    case GST_VIDEO_FORMAT_v308:
      uav_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out_format_str = "v308";
      x_unit = 32;
      break;
    case GST_VIDEO_FORMAT_IYU2:
      uav_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out_format_str = "IYU2";
      x_unit = 32;
      break;
    case GST_VIDEO_FORMAT_Y410:
      uav_format = DXGI_FORMAT_R10G10B10A2_UNORM;
      out_format_str = "Y410";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_Y412_LE:
    case GST_VIDEO_FORMAT_Y416_LE:
      uav_format = DXGI_FORMAT_R16G16B16A16_UNORM;
      out_format_str = "Y410";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_RGB:
      uav_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out_format_str = "RGB";
      x_unit = 32;
      break;
    case GST_VIDEO_FORMAT_BGR:
      uav_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out_format_str = "BGR";
      x_unit = 32;
      break;
    case GST_VIDEO_FORMAT_RGB16:
      uav_format = DXGI_FORMAT_R16_UINT;
      out_format_str = "RGB16";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_BGR16:
      uav_format = DXGI_FORMAT_R16_UINT;
      out_format_str = "BGR16";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_RGB15:
      uav_format = DXGI_FORMAT_R16_UINT;
      out_format_str = "RGB15";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_BGR15:
      uav_format = DXGI_FORMAT_R16_UINT;
      out_format_str = "BGR15";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_r210:
      uav_format = DXGI_FORMAT_R32_UINT;
      out_format_str = "r210";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_BGRA64_LE:
      uav_format = DXGI_FORMAT_R16G16B16A16_UNORM;
      out_format_str = "BGRA";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_BGR10A2_LE:
      uav_format = DXGI_FORMAT_R10G10B10A2_UNORM;
      out_format_str = "BGRA";
      x_unit = 8;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      uav_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out_format_str = "AYUV";
      break;
    case GST_VIDEO_FORMAT_AYUV64:
      uav_format = DXGI_FORMAT_R16G16B16A16_UNORM;
      out_format_str = "AYUV";
      break;
    case GST_VIDEO_FORMAT_RGBA:
      uav_format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out_format_str = "RGBA";
      break;
    case GST_VIDEO_FORMAT_RGB10A2_LE:
      uav_format = DXGI_FORMAT_R10G10B10A2_UNORM;
      out_format_str = "RGBA";
      break;
    default:
      return FALSE;
  }

  byte_code->x_unit = x_unit;
  byte_code->y_unit = y_unit;
  byte_code->srv_format = srv_format;
  byte_code->uav_format = uav_format;

  auto shader_def = "CSMain_" + in_format_str + "_to_" + out_format_str;
  auto shader_name = shader_def + "_" + std::string (g_sm_map[shader_model]);

  std::lock_guard <std::mutex> lk (cache_lock);
  auto it = g_converter_cs_table.find (shader_name);
  if (it != g_converter_cs_table.end ()) {
    byte_code->byte_code.byte_code = it->second.first;
    byte_code->byte_code.byte_code_len = it->second.second;

    return TRUE;
  }

  auto target = std::string ("cs_") + g_sm_map[shader_model];
  std::vector<std::pair<std::string,std::string>> macro_str_pairs;
  std::vector<D3D_SHADER_MACRO> macros;

  macro_str_pairs.push_back ({"BUILDING_" + shader_def, "1"});

  for (const auto & def : macro_str_pairs)
    macros.push_back({def.first.c_str (), def.second.c_str ()});

  macros.push_back({nullptr, nullptr});

  ID3DBlob *blob = nullptr;
  ComPtr<ID3DBlob> error_msg;

  auto hr = gst_d3d_compile (str_CSMain_converter,
      sizeof (str_CSMain_converter),
      nullptr, macros.data (), nullptr, "ENTRY_POINT", target.c_str (), 0, 0,
      &blob, &error_msg);
  if (FAILED (hr)) {
    const gchar *err = nullptr;
    if (error_msg)
      err = (const gchar *) error_msg->GetBufferPointer ();

    GST_ERROR ("Couldn't compile code, hr: 0x%x, error detail: %s, "
        "source code: \n%s", (guint) hr, GST_STR_NULL (err),
        str_VSMain_converter);
    return FALSE;
  }

  byte_code->byte_code.byte_code = blob->GetBufferPointer ();
  byte_code->byte_code.byte_code_len = blob->GetBufferSize ();

  g_converter_cs_table[shader_name] = {(const BYTE *) blob->GetBufferPointer (),
      blob->GetBufferSize ()};

  std::lock_guard <std::mutex> blk (g_blob_lock);
  g_compiled_blobs.push_back ({ shader_name, blob });

  return TRUE;
}

enum class PS_OUTPUT
{
  PACKED,
  LUMA,
  CHROMA,
  CHROMA_PLANAR,
  LUMA_ALPHA,
  PLANAR,
  PLANAR_FULL,
};

static const std::string
ps_output_to_string (PS_OUTPUT output)
{
  switch (output) {
    case PS_OUTPUT::PACKED:
      return "PS_OUTPUT_PACKED";
    case PS_OUTPUT::LUMA:
      return "PS_OUTPUT_LUMA";
    case PS_OUTPUT::CHROMA:
      return "PS_OUTPUT_CHROMA";
    case PS_OUTPUT::CHROMA_PLANAR:
      return "PS_OUTPUT_CHROMA_PLANAR";
    case PS_OUTPUT::LUMA_ALPHA:
      return "PS_OUTPUT_LUMA_ALPHA";
    case PS_OUTPUT::PLANAR:
      return "PS_OUTPUT_PLANAR";
    case PS_OUTPUT::PLANAR_FULL:
      return "PS_OUTPUT_PLANAR_FULL";
    default:
      g_assert_not_reached ();
      break;
  }

  return "";
}

static guint
ps_output_get_num_rtv (PS_OUTPUT output)
{
  switch (output) {
    case PS_OUTPUT::PACKED:
    case PS_OUTPUT::LUMA:
    case PS_OUTPUT::CHROMA:
      return 1;
    case PS_OUTPUT::CHROMA_PLANAR:
    case PS_OUTPUT::LUMA_ALPHA:
      return 2;
    case PS_OUTPUT::PLANAR:
      return 3;
    case PS_OUTPUT::PLANAR_FULL:
      return 4;
    default:
      g_assert_not_reached ();
      break;
  }

  return 0;
}

static std::string
conv_ps_make_input (GstVideoFormat format, gboolean premul)
{
  switch (format) {
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBA64_LE:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
    case GST_VIDEO_FORMAT_BGRA:
      if (premul)
        return "RGBAPremul";
      return "RGBA";
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      return "RGBx";
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ARGB64_LE:
      if (premul)
        return "ARGBPremul";
      return "ARGB";
    case GST_VIDEO_FORMAT_xRGB:
      return "xRGB";
    case GST_VIDEO_FORMAT_ABGR:
      if (premul)
        return "ABGRPremul";
      return "ABGR";
    case GST_VIDEO_FORMAT_xBGR:
      return "xBGR";
    case GST_VIDEO_FORMAT_VUYA:
      if (premul)
        return "VUYAPremul";
      return "VUYA";
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_AYUV64:
      return "AYUV";
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV24:
      return "NV12";
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_NV61:
      return "NV21";
    case GST_VIDEO_FORMAT_AV12:
      return "AV12";
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_16LE:
      return "I420";
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YVU9:
      return "YV12";
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
      return "I420_10";
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
      return "I420_12";
    case GST_VIDEO_FORMAT_Y410:
      return "Y410";
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_LE:
      return "GRAY";
    case GST_VIDEO_FORMAT_RGBP:
      return "RGBP";
    case GST_VIDEO_FORMAT_BGRP:
      return "BGRP";
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_16LE:
      return "GBR";
    case GST_VIDEO_FORMAT_GBR_10LE:
      return "GBR_10";
    case GST_VIDEO_FORMAT_GBR_12LE:
      return "GBR_12";
    case GST_VIDEO_FORMAT_GBRA:
      if (premul)
        return "GBRAPremul";
      return "GBRA";
    case GST_VIDEO_FORMAT_GBRA_10LE:
      if (premul)
        return "GBRAPremul_10";
      return "GBRA_10";
    case GST_VIDEO_FORMAT_GBRA_12LE:
      if (premul)
        return "GBRAPremul_12";
      return "GBRA_12";
    case GST_VIDEO_FORMAT_Y412_LE:
    case GST_VIDEO_FORMAT_Y416_LE:
      if (premul)
        return "Y412Premul";
      return "Y412";
    case GST_VIDEO_FORMAT_BGR10A2_LE:
      return "BGR10A2";
    case GST_VIDEO_FORMAT_BGRA64_LE:
      if (premul)
        return "BGRA64Premul";
      return "BGRA64";
    case GST_VIDEO_FORMAT_RBGA:
      if (premul)
        return "RBGAPremul";
      return "RBGA";
    case GST_VIDEO_FORMAT_RGB16:
      return "RGB16";
    case GST_VIDEO_FORMAT_BGR16:
      return "BGR16";
    case GST_VIDEO_FORMAT_RGB15:
      return "RGB15";
    case GST_VIDEO_FORMAT_BGR15:
      return "BGR15";
    case GST_VIDEO_FORMAT_A420:
    case GST_VIDEO_FORMAT_A420_16LE:
    case GST_VIDEO_FORMAT_A422:
    case GST_VIDEO_FORMAT_A422_16LE:
    case GST_VIDEO_FORMAT_A444:
    case GST_VIDEO_FORMAT_A444_16LE:
      return "A420";
    case GST_VIDEO_FORMAT_A420_10LE:
    case GST_VIDEO_FORMAT_A422_10LE:
    case GST_VIDEO_FORMAT_A444_10LE:
      return "A420_10";
    case GST_VIDEO_FORMAT_A420_12LE:
    case GST_VIDEO_FORMAT_A422_12LE:
    case GST_VIDEO_FORMAT_A444_12LE:
      return "A420_12";
    default:
      g_assert_not_reached ();
      break;
  }

  return "";
}

static std::vector<std::pair<PS_OUTPUT, std::string>>
conv_ps_make_output (GstVideoFormat format, gboolean premul)
{
  std::vector<std::pair<PS_OUTPUT, std::string>> ret;

  switch (format) {
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBA64_LE:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
    case GST_VIDEO_FORMAT_BGRA:
      if (premul)
        ret.push_back({PS_OUTPUT::PACKED, "RGBAPremul"});
      else
        ret.push_back({PS_OUTPUT::PACKED, "RGBA"});
      break;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      ret.push_back({PS_OUTPUT::PACKED, "RGBx"});
      break;
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ARGB64_LE:
      if (premul)
        ret.push_back({PS_OUTPUT::PACKED, "ARGBPremul"});
      else
        ret.push_back({PS_OUTPUT::PACKED, "ARGB"});
      break;
    case GST_VIDEO_FORMAT_xRGB:
      ret.push_back({PS_OUTPUT::PACKED, "xRGB"});
      break;
    case GST_VIDEO_FORMAT_ABGR:
      if (premul)
        ret.push_back({PS_OUTPUT::PACKED, "ABGRPremul"});
      else
        ret.push_back({PS_OUTPUT::PACKED, "ABGR"});
      break;
    case GST_VIDEO_FORMAT_xBGR:
      ret.push_back({PS_OUTPUT::PACKED, "xBGR"});
      break;
    case GST_VIDEO_FORMAT_VUYA:
      if (premul)
        ret.push_back({PS_OUTPUT::PACKED, "VUYAPremul"});
      else
        ret.push_back({PS_OUTPUT::PACKED, "VUYA"});
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_AYUV64:
      ret.push_back({PS_OUTPUT::PACKED, "AYUV"});
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_NV16:
    case GST_VIDEO_FORMAT_NV24:
      ret.push_back({PS_OUTPUT::LUMA, "Luma"});
      ret.push_back({PS_OUTPUT::CHROMA, "ChromaNV12"});
      break;
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_NV61:
      ret.push_back({PS_OUTPUT::LUMA, "Luma"});
      ret.push_back({PS_OUTPUT::CHROMA, "ChromaNV21"});
      break;
    case GST_VIDEO_FORMAT_AV12:
      ret.push_back({PS_OUTPUT::LUMA_ALPHA, "LumaAlphaA420"});
      ret.push_back({PS_OUTPUT::CHROMA, "ChromaNV12"});
      break;
    case GST_VIDEO_FORMAT_YUV9:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_Y42B:
      ret.push_back({PS_OUTPUT::LUMA, "Luma"});
      ret.push_back({PS_OUTPUT::CHROMA_PLANAR, "ChromaI420"});
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_16LE:
      ret.push_back({PS_OUTPUT::PLANAR, "Y444"});
      break;
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_YVU9:
      ret.push_back({PS_OUTPUT::LUMA, "Luma"});
      ret.push_back({PS_OUTPUT::CHROMA_PLANAR, "ChromaYV12"});
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I422_10LE:
      ret.push_back({PS_OUTPUT::LUMA, "Luma_10"});
      ret.push_back({PS_OUTPUT::CHROMA_PLANAR, "ChromaI420_10"});
      break;
    case GST_VIDEO_FORMAT_A420:
    case GST_VIDEO_FORMAT_A420_16LE:
    case GST_VIDEO_FORMAT_A422:
    case GST_VIDEO_FORMAT_A422_16LE:
      ret.push_back({PS_OUTPUT::LUMA_ALPHA, "LumaAlphaA420"});
      ret.push_back({PS_OUTPUT::CHROMA_PLANAR, "ChromaI420"});
      break;
    case GST_VIDEO_FORMAT_A420_10LE:
    case GST_VIDEO_FORMAT_A422_10LE:
      ret.push_back({PS_OUTPUT::LUMA_ALPHA, "LumaAlphaA420_10"});
      ret.push_back({PS_OUTPUT::CHROMA_PLANAR, "ChromaI420_10"});
      break;
    case GST_VIDEO_FORMAT_A420_12LE:
    case GST_VIDEO_FORMAT_A422_12LE:
      ret.push_back({PS_OUTPUT::LUMA_ALPHA, "LumaAlphaA420_12"});
      ret.push_back({PS_OUTPUT::CHROMA_PLANAR, "ChromaI420_12"});
      break;
    case GST_VIDEO_FORMAT_Y444_10LE:
      ret.push_back({PS_OUTPUT::PLANAR, "Y444_10"});
      break;
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I422_12LE:
      ret.push_back({PS_OUTPUT::LUMA, "Luma_12"});
      ret.push_back({PS_OUTPUT::CHROMA_PLANAR, "ChromaI420_12"});
      break;
    case GST_VIDEO_FORMAT_Y444_12LE:
      ret.push_back({PS_OUTPUT::PLANAR, "Y444_12"});
      break;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_LE:
      ret.push_back({PS_OUTPUT::LUMA, "Luma"});
      break;
    case GST_VIDEO_FORMAT_RGBP:
      ret.push_back({PS_OUTPUT::PLANAR, "RGBP"});
      break;
    case GST_VIDEO_FORMAT_BGRP:
      ret.push_back({PS_OUTPUT::PLANAR, "BGRP"});
      break;
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBR_16LE:
      ret.push_back({PS_OUTPUT::PLANAR, "GBR"});
      break;
    case GST_VIDEO_FORMAT_GBR_10LE:
      ret.push_back({PS_OUTPUT::PLANAR, "GBR_10"});
      break;
    case GST_VIDEO_FORMAT_GBR_12LE:
      ret.push_back({PS_OUTPUT::PLANAR, "GBR_12"});
      break;
    case GST_VIDEO_FORMAT_GBRA:
      if (premul)
        ret.push_back({PS_OUTPUT::PLANAR_FULL, "GBRAPremul"});
      else
        ret.push_back({PS_OUTPUT::PLANAR_FULL, "GBRA"});
      break;
    case GST_VIDEO_FORMAT_GBRA_10LE:
      if (premul)
        ret.push_back({PS_OUTPUT::PLANAR_FULL, "GBRAPremul_10"});
      else
        ret.push_back({PS_OUTPUT::PLANAR_FULL, "GBRA_10"});
      break;
    case GST_VIDEO_FORMAT_GBRA_12LE:
      if (premul)
        ret.push_back({PS_OUTPUT::PLANAR_FULL, "GBRAPremul_12"});
      else
        ret.push_back({PS_OUTPUT::PLANAR_FULL, "GBRA_12"});
      break;
    case GST_VIDEO_FORMAT_A444:
    case GST_VIDEO_FORMAT_A444_16LE:
      ret.push_back({PS_OUTPUT::PLANAR_FULL, "A444"});
      break;
    case GST_VIDEO_FORMAT_A444_10LE:
      ret.push_back({PS_OUTPUT::PLANAR_FULL, "A444_10"});
      break;
    case GST_VIDEO_FORMAT_A444_12LE:
      ret.push_back({PS_OUTPUT::PLANAR_FULL, "A444_12"});
      break;
    case GST_VIDEO_FORMAT_RBGA:
      if (premul)
        ret.push_back({PS_OUTPUT::PACKED, "RBGAPremul"});
      else
        ret.push_back({PS_OUTPUT::PACKED, "RBGA"});
      break;
    case GST_VIDEO_FORMAT_RGB16:
      ret.push_back({PS_OUTPUT::PACKED, "RGB16"});
      break;
    case GST_VIDEO_FORMAT_BGR16:
      ret.push_back({PS_OUTPUT::PACKED, "BGR16"});
      break;
    case GST_VIDEO_FORMAT_RGB15:
      ret.push_back({PS_OUTPUT::PACKED, "RGB15"});
      break;
    case GST_VIDEO_FORMAT_BGR15:
      ret.push_back({PS_OUTPUT::PACKED, "BGR15"});
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return ret;
}

guint
gst_d3d_converter_shader_get_ps_blob (GstVideoFormat in_format,
    GstVideoFormat out_format, gboolean in_premul, gboolean out_premul,
    GstD3DConverterType conv_type, GstD3DShaderModel shader_model,
    GstD3DConverterPSByteCode byte_code[4])
{
  static std::mutex cache_lock;

  auto input = conv_ps_make_input (in_format, in_premul);
  auto output = conv_ps_make_output (out_format, out_premul);
  std::string conv_type_str;
  std::string sm_target;
  guint ret = 0;

  switch (conv_type) {
    case GST_D3D_CONVERTER_IDENTITY:
      conv_type_str = "Identity";
      break;
    case GST_D3D_CONVERTER_SIMPLE:
      conv_type_str = "Simple";
      break;
    case GST_D3D_CONVERTER_RANGE:
      conv_type_str = "Range";
      break;
    case GST_D3D_CONVERTER_GAMMA:
      conv_type_str = "Gamma";
      break;
    case GST_D3D_CONVERTER_PRIMARY:
      conv_type_str = "Primary";
      break;
    default:
      g_assert_not_reached ();
      return 0;
  }

  sm_target = std::string ("ps_") + g_sm_map[shader_model];

  std::lock_guard <std::mutex> lk (cache_lock);
  for (const auto & it : output) {
    auto output_builder = it.second;
    std::string shader_name = "PSMain_" + input + "_" + conv_type_str + "_" +
        output_builder + "_" + g_sm_map[shader_model];
    GstD3DConverterPSByteCode *ps_blob = &byte_code[ret];
    ps_blob->num_rtv = ps_output_get_num_rtv (it.first);

    auto cached = g_converter_ps_table.find (shader_name);
    if (cached != g_converter_ps_table.end ()) {
      ps_blob->byte_code.byte_code = cached->second.first;
      ps_blob->byte_code.byte_code_len = cached->second.second;
    } else {
      std::vector<std::pair<std::string,std::string>> macro_str_pairs;
      std::vector<D3D_SHADER_MACRO> macros;
      auto output_type = ps_output_to_string (it.first);

      macro_str_pairs.push_back ({"ENTRY_POINT", shader_name});
      macro_str_pairs.push_back ({"SAMPLER", "Sampler" + input});
      macro_str_pairs.push_back ({"CONVERTER", "Converter" + conv_type_str});
      macro_str_pairs.push_back ({"OUTPUT_TYPE", output_type });
      macro_str_pairs.push_back ({"OUTPUT_BUILDER", "Output" + output_builder});

      for (const auto & def : macro_str_pairs)
        macros.push_back({def.first.c_str (), def.second.c_str ()});

      macros.push_back({nullptr, nullptr});

      ID3DBlob *blob = nullptr;
      ComPtr<ID3DBlob> error_msg;

      auto hr = gst_d3d_compile (str_PSMain_converter,
          sizeof (str_PSMain_converter), nullptr, macros.data (), nullptr,
          shader_name.c_str (), sm_target.c_str (), 0, 0, &blob, &error_msg);
      if (FAILED (hr)) {
        const gchar *err = nullptr;
        if (error_msg)
          err = (const gchar *) error_msg->GetBufferPointer ();

        GST_ERROR ("Couldn't compile code, hr: 0x%x, error detail: %s",
            (guint) hr, GST_STR_NULL (err));
        return 0;
      }

      ps_blob->byte_code.byte_code = blob->GetBufferPointer ();
      ps_blob->byte_code.byte_code_len = blob->GetBufferSize ();

      g_converter_ps_table[shader_name] = {
          (const BYTE *) blob->GetBufferPointer (),
          blob->GetBufferSize () };

      std::lock_guard <std::mutex> blk (g_blob_lock);
      g_compiled_blobs.push_back ({ shader_name, blob });
    }

    ret++;
  }

  return ret;
}
