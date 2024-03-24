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

#include "gstd3d11converter-builder.h"
#include "gstd3d11device-private.h"
#include "gstd3d11-private.h"
#include "gstd3d11utils.h"
#include <gst/d3dshader/gstd3dshader.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_converter_debug);
#define GST_CAT_DEFAULT gst_d3d11_converter_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

PixelShaderList
gst_d3d11_get_converter_pixel_shader (GstD3D11Device * device,
    GstVideoFormat in_format, GstVideoFormat out_format, gboolean in_premul,
    gboolean out_premul, CONVERT_TYPE type)
{
  GstD3DConverterType conv_type;
  GstD3DShaderModel sm;
  PixelShaderList ret;

  switch (type) {
    case CONVERT_TYPE::IDENTITY:
      conv_type = GST_D3D_CONVERTER_IDENTITY;
      break;
    case CONVERT_TYPE::SIMPLE:
      conv_type = GST_D3D_CONVERTER_SIMPLE;
      break;
    case CONVERT_TYPE::RANGE:
      conv_type = GST_D3D_CONVERTER_RANGE;
      break;
    case CONVERT_TYPE::GAMMA:
      conv_type = GST_D3D_CONVERTER_GAMMA;
      break;
    case CONVERT_TYPE::PRIMARY:
      conv_type = GST_D3D_CONVERTER_PRIMARY;
      break;
    default:
      g_assert_not_reached ();
      return ret;
  }

  auto handle = gst_d3d11_device_get_device_handle (device);
  auto level = handle->GetFeatureLevel ();
  if (level >= D3D_FEATURE_LEVEL_11_0)
    sm = GST_D3D_SM_5_0;
  else
    sm = GST_D3D_SM_4_0;

  GstD3DConverterPSByteCode blobs[4];
  auto num_blobs = gst_d3d_converter_shader_get_ps_blob (in_format, out_format,
      in_premul, out_premul, conv_type, sm, blobs);

  if (!num_blobs) {
    GST_ERROR_OBJECT (device, "Couldn't get compiled bytecode");
    return ret;
  }

  for (guint i = 0; i < num_blobs; i++) {
    ComPtr < ID3D11PixelShader > shader;
    auto blob = &blobs[i];
    auto hr = handle->CreatePixelShader (blob->byte_code.byte_code,
        blob->byte_code.byte_code_len, nullptr, &shader);
    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (device, "Couldn't create pixel shader");
      ret.clear ();
      return ret;
    }

    auto ps = std::make_shared < PixelShader > ();
    ps->shader = shader;
    ps->num_rtv = blob->num_rtv;

    ret.push_back (ps);
  }

  return ret;
}

HRESULT
gst_d3d11_get_converter_vertex_shader (GstD3D11Device * device,
    ID3D11VertexShader ** vs, ID3D11InputLayout ** layout)
{
  static gint64 token = 0;

  GST_D3D11_CALL_ONCE_BEGIN {
    token = gst_d3d11_vertex_shader_token_new ();
  } GST_D3D11_CALL_ONCE_END;

  auto handle = gst_d3d11_device_get_device_handle (device);
  auto level = handle->GetFeatureLevel ();
  GstD3DShaderModel sm;
  if (level >= D3D_FEATURE_LEVEL_11_0)
    sm = GST_D3D_SM_5_0;
  else
    sm = GST_D3D_SM_4_0;

  GstD3DShaderByteCode bytecode = { };
  if (!gst_d3d_converter_shader_get_vs_blob (sm, &bytecode)) {
    GST_ERROR_OBJECT (device, "Couldn't get compiled bytecode");
    return E_FAIL;
  }

  D3D11_INPUT_ELEMENT_DESC input_desc[2];

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

  return gst_d3d11_device_get_vertex_shader (device, token, "VSMain_converter",
      &bytecode, input_desc, G_N_ELEMENTS (input_desc), vs, layout);
}
