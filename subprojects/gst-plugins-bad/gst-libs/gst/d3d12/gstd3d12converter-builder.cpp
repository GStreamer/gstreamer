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
#include "gstd3d12converter-builder.h"
#include <gst/d3dshader/gstd3dshader.h>
#include <directx/d3dx12.h>
#include <mutex>
#include <string>
#include <utility>
#include <memory>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_converter_debug);
#define GST_CAT_DEFAULT gst_d3d12_converter_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

PixelShaderBlobList
gst_d3d12_get_converter_pixel_shader_blob (GstVideoFormat in_format,
    GstVideoFormat out_format, gboolean in_premul, gboolean out_premul,
    CONVERT_TYPE type)
{
  GstD3DConverterType conv_type;
  PixelShaderBlobList ret;

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

  GstD3DConverterPSByteCode blobs[4];
  auto num_blobs = gst_d3d_converter_shader_get_ps_blob (in_format, out_format,
      in_premul, out_premul, conv_type, GST_D3D_SM_5_0, blobs);

  if (!num_blobs) {
    GST_ERROR ("Couldn't get compiled bytecode");
    return ret;
  }

  for (guint i = 0; i < num_blobs; i++) {
    auto blob = &blobs[i];
    PixelShaderBlob ps;
    ps.bytecode.pShaderBytecode = blob->byte_code.byte_code;
    ps.bytecode.BytecodeLength = blob->byte_code.byte_code_len;
    ps.num_rtv = blob->num_rtv;
    ret.push_back (ps);
  }

  return ret;
}

HRESULT
gst_d3d12_get_converter_vertex_shader_blob (D3D12_SHADER_BYTECODE * vs,
    D3D12_INPUT_ELEMENT_DESC input_desc[2])
{
  GstD3DShaderByteCode bytecode = { };
  if (!gst_d3d_converter_shader_get_vs_blob (GST_D3D_SM_5_0, &bytecode)) {
    GST_ERROR ("Couldn't get compiled bytecode");
    return E_FAIL;
  }

  input_desc[0].SemanticName = "POSITION";
  input_desc[0].SemanticIndex = 0;
  input_desc[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
  input_desc[0].InputSlot = 0;
  input_desc[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
  input_desc[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
  input_desc[0].InstanceDataStepRate = 0;

  input_desc[1].SemanticName = "TEXCOORD";
  input_desc[1].SemanticIndex = 0;
  input_desc[1].Format = DXGI_FORMAT_R32G32_FLOAT;
  input_desc[1].InputSlot = 0;
  input_desc[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
  input_desc[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
  input_desc[1].InstanceDataStepRate = 0;

  vs->BytecodeLength = bytecode.byte_code_len;
  vs->pShaderBytecode = bytecode.byte_code;

  return S_OK;
}

/* root signature
 *
 * +-----+---------+------------------+
 * | RS  | size in |                  |
 * | idx |  DWORD  |                  |
 * +-----+---------+------------------+
 * | 0   |  1      | table (SRV)      |
 * +-----+---------+------------------+
 * | 1   |  1      | table (Sampler)  |
 * +-----+---------+------------------+
 * | 2   |  16     |  VS matrix       |
 * +-----+---------+------------------+
 * | 3   |  1      |   PS alpha       |
 * +-----+---------+------------------+
 * | 4   |  2      |   PS CBV         |
 * +-----+---------+------------------+
 */

static const D3D12_ROOT_SIGNATURE_FLAGS rs_flags_ =
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
    D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;

ConverterRootSignature::ConverterRootSignature (D3D_ROOT_SIGNATURE_VERSION
    version, UINT num_srv, bool build_lut)
{
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = { };

  num_srv_ = num_srv;
  have_lut_ = build_lut;

  std::vector < D3D12_DESCRIPTOR_RANGE1 > range_v1_1;
  std::vector < D3D12_DESCRIPTOR_RANGE1 > sampler_range_v1_1;
  std::vector < D3D12_ROOT_PARAMETER1 > param_list_v1_1;

  CD3DX12_ROOT_PARAMETER1 param;
  ps_srv_ = 0;
  for (UINT i = 0; i < num_srv; i++) {
    range_v1_1.push_back (CD3DX12_DESCRIPTOR_RANGE1
        (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, i, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE));
  }

  if (build_lut) {
    range_v1_1.push_back (CD3DX12_DESCRIPTOR_RANGE1
        (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE));
    range_v1_1.push_back (CD3DX12_DESCRIPTOR_RANGE1
        (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE));
  }

  param.InitAsDescriptorTable (range_v1_1.size (),
      range_v1_1.data (), D3D12_SHADER_VISIBILITY_PIXEL);
  param_list_v1_1.push_back (param);

  /* sampler state, can be updated */
  ps_sampler_ = (UINT) param_list_v1_1.size ();
  sampler_range_v1_1.push_back (CD3DX12_DESCRIPTOR_RANGE1
      (D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0,
          D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE));
  if (build_lut) {
    sampler_range_v1_1.push_back (CD3DX12_DESCRIPTOR_RANGE1
        (D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 1, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE));
  }
  param.InitAsDescriptorTable (sampler_range_v1_1.size (),
      sampler_range_v1_1.data (), D3D12_SHADER_VISIBILITY_PIXEL);
  param_list_v1_1.push_back (param);

  /* VS root const, maybe updated */
  vs_root_const_ = (UINT) param_list_v1_1.size ();
  param.InitAsConstants (16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
  param_list_v1_1.push_back (param);

  /* PS alpha constant value, maybe updated */
  ps_root_const_ = (UINT) param_list_v1_1.size ();
  param.InitAsConstants (1, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
  param_list_v1_1.push_back (param);

  /* PS CBV, this is static */
  ps_cbv_ = (UINT) param_list_v1_1.size ();
  param.InitAsConstantBufferView (2, 0,
      D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE,
      D3D12_SHADER_VISIBILITY_PIXEL);
  param_list_v1_1.push_back (param);

  CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC::Init_1_1 (desc,
      param_list_v1_1.size (), param_list_v1_1.data (), 0, nullptr, rs_flags_);

  ComPtr < ID3DBlob > error_blob;
  hr_ = D3DX12SerializeVersionedRootSignature (&desc,
      version, &blob_, &error_blob);
  if (FAILED (hr_)) {
    const gchar *error_msg = nullptr;
    if (error_blob)
      error_msg = (const gchar *) error_blob->GetBufferPointer ();
    GST_ERROR ("Couldn't serialize root signature, hr: 0x%x, error detail: %s",
        (guint) hr_, GST_STR_NULL (error_msg));
  }
}

ConverterRootSignaturePtr
gst_d3d12_get_converter_root_signature (GstD3D12Device * device,
    GstVideoFormat in_format, CONVERT_TYPE type)
{
  auto info = gst_video_format_get_info (in_format);
  auto num_planes = GST_VIDEO_FORMAT_INFO_N_PLANES (info);
  bool build_lut = false;

  auto device_handle = gst_d3d12_device_get_device_handle (device);
  D3D_ROOT_SIGNATURE_VERSION rs_version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = { };
  feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
  auto hr = device_handle->CheckFeatureSupport (D3D12_FEATURE_ROOT_SIGNATURE,
      &feature_data, sizeof (feature_data));
  if (FAILED (hr)) {
    rs_version = D3D_ROOT_SIGNATURE_VERSION_1_0;
  } else {
    GST_INFO_OBJECT (device, "Device supports version 1.1 root signature");
  }

  if (type == CONVERT_TYPE::GAMMA || type == CONVERT_TYPE::PRIMARY)
    build_lut = true;

  auto rs = std::make_shared < ConverterRootSignature >
      (rs_version, num_planes, build_lut);
  if (!rs->IsValid ())
    return nullptr;

  return rs;
}
