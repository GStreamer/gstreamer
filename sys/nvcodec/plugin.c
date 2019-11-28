/*
 * Copyright (C) 2017 Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvdec.h"
#include "gstnvenc.h"

GST_DEBUG_CATEGORY (gst_nvcodec_debug);
GST_DEBUG_CATEGORY (gst_nvdec_debug);
GST_DEBUG_CATEGORY (gst_nvenc_debug);

#define GST_CAT_DEFAULT gst_nvcodec_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  CUresult cuda_ret;
  gint dev_count = 0;
  gint i;
  gboolean nvdec_available = TRUE;
  gboolean nvenc_available = TRUE;

  GST_DEBUG_CATEGORY_INIT (gst_nvcodec_debug, "nvcodec", 0, "nvcodec");
  GST_DEBUG_CATEGORY_INIT (gst_nvdec_debug, "nvdec", 0, "nvdec");
  GST_DEBUG_CATEGORY_INIT (gst_nvenc_debug, "nvenc", 0, "nvenc");

  if (!gst_cuda_load_library ()) {
    GST_WARNING ("Failed to load cuda library");
    return TRUE;
  }

  if (!gst_cuvid_load_library ()) {
    GST_WARNING ("Failed to load nvdec library");
    nvdec_available = FALSE;
  }

  if (!gst_nvenc_load_library ()) {
    GST_WARNING ("Failed to load nvenc library");
    nvenc_available = FALSE;
  }

  if (!nvdec_available && !nvenc_available)
    return TRUE;

  cuda_ret = CuInit (0);
  if (cuda_ret != CUDA_SUCCESS) {
    GST_WARNING ("Failed to init cuda, ret: 0x%x", (gint) cuda_ret);
    return TRUE;
  }

  if (CuDeviceGetCount (&dev_count) != CUDA_SUCCESS || !dev_count) {
    GST_WARNING ("No available device, ret: 0x%x", (gint) cuda_ret);
    return TRUE;
  }

  for (i = 0; i < dev_count; i++) {
    CUdevice cuda_device;
    CUcontext cuda_ctx;

    cuda_ret = CuDeviceGet (&cuda_device, i);
    if (cuda_ret != CUDA_SUCCESS) {
      GST_WARNING ("Failed to get device handle %d, ret: 0x%x", i,
          (gint) cuda_ret);
      continue;
    }

    cuda_ret = CuCtxCreate (&cuda_ctx, 0, cuda_device);
    if (cuda_ret != CUDA_SUCCESS) {
      GST_WARNING ("Failed to create cuda context, ret: 0x%x", (gint) cuda_ret);
      continue;
    }

    CuCtxPopCurrent (NULL);

    if (nvdec_available)
      gst_nvdec_plugin_init (plugin, i, cuda_ctx);

    if (nvenc_available)
      gst_nvenc_plugin_init (plugin, i, cuda_ctx);

    CuCtxDestroy (cuda_ctx);
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, nvcodec,
    "GStreamer NVCODEC plugin", plugin_init, VERSION, "LGPL",
    GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
