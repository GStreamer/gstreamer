/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include <gst/d3d12/gstd3d12.h>
#include <gst/d3d12/gstd3d12-private.h>
#include <dml_provider_factory.h>
#include <directml.h>
#include <wrl.h>
#include "gstonnx-dml.h"

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("onnx-dml", 0, "onnx-dml");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif

struct _GstOnnxDmlCtx
{
  ~_GstOnnxDmlCtx ()
  {
    gst_clear_object (&device_12);
  }

  GstD3D12Device *device_12 = nullptr;
  ComPtr < IDMLDevice > device_ml;
};

GstOnnxDmlCtx *
gst_onnx_dml_create_context (GstD3D12Device * device_12,
    OrtSessionOptions * opt, const OrtApi * api)
{
  const OrtDmlApi *dml_api = nullptr;
  auto status = api->GetExecutionProviderApi ("DML", ORT_API_VERSION,
      reinterpret_cast < const void **>(&dml_api));
  if (status) {
    GST_ERROR_OBJECT (device_12, "Couldn't get OrtDmlApi, %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    return nullptr;
  }

  auto device = gst_d3d12_device_get_device_handle (device_12);
  ComPtr < IDMLDevice > device_ml;

  auto hr = DMLCreateDevice (device,
      DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS (&device_ml));
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (device_12, "Couldn't create DML device, hr: 0x%x",
        (guint) hr);
    return nullptr;
  }

  auto cq = gst_d3d12_device_get_cmd_queue (device_12,
      D3D12_COMMAND_LIST_TYPE_COMPUTE);

  status = dml_api->SessionOptionsAppendExecutionProvider_DML1 (opt,
      device_ml.Get (), gst_d3d12_cmd_queue_get_handle (cq));
  if (status) {
    GST_ERROR_OBJECT (device_12, "Couldn't append DML EP, %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    return nullptr;
  }

  status = api->SetSessionExecutionMode (opt, ORT_SEQUENTIAL);
  if (status) {
    GST_ERROR_OBJECT (device_12, "SetSessionExecutionMode failed, %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    return nullptr;
  }

  status = api->DisableMemPattern (opt);
  if (status) {
    GST_ERROR_OBJECT (device_12, "DisableMemPattern failed, %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    return nullptr;
  }

  auto ctx = new GstOnnxDmlCtx ();
  ctx->device_12 = (GstD3D12Device *) gst_object_ref (device_12);
  ctx->device_ml = device_ml;

  return ctx;
}

void
gst_onnx_dml_free_context (GstOnnxDmlCtx * ctx)
{
  if (!ctx)
    return;

  delete ctx;
}
