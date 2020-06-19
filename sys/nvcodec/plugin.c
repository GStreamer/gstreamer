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
#include "gstnvh264dec.h"
#include "gstnvh265dec.h"
#include "gstnvdecoder.h"

GST_DEBUG_CATEGORY (gst_nvcodec_debug);
GST_DEBUG_CATEGORY (gst_nvdec_debug);
GST_DEBUG_CATEGORY (gst_nvenc_debug);
GST_DEBUG_CATEGORY (gst_nv_decoder_debug);

#define GST_CAT_DEFAULT gst_nvcodec_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  CUresult cuda_ret;
  gint dev_count = 0;
  gint i;
  gboolean nvdec_available = TRUE;
  gboolean nvenc_available = TRUE;
  /* hardcoded minimum supported version */
  guint api_major_ver = 8;
  guint api_minor_ver = 1;
  const gchar *env;
  gboolean use_h264_sl_dec = FALSE;
  gboolean use_h265_sl_dec = FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_nvcodec_debug, "nvcodec", 0, "nvcodec");
  GST_DEBUG_CATEGORY_INIT (gst_nvdec_debug, "nvdec", 0, "nvdec");
  GST_DEBUG_CATEGORY_INIT (gst_nvenc_debug, "nvenc", 0, "nvenc");
  GST_DEBUG_CATEGORY_INIT (gst_nv_decoder_debug, "nvdecoder", 0, "nvdecoder");

  if (!gst_cuda_load_library ()) {
    GST_WARNING ("Failed to load cuda library");
    return TRUE;
  }

  /* get available API version from nvenc and it will be passed to
   * nvdec */
  if (!gst_nvenc_load_library (&api_major_ver, &api_minor_ver)) {
    GST_WARNING ("Failed to load nvenc library");
    nvenc_available = FALSE;
  }

  if (!gst_cuvid_load_library (api_major_ver, api_minor_ver)) {
    GST_WARNING ("Failed to load nvdec library");
    nvdec_available = FALSE;
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

  /* check environment to determine primary h264decoder */
  env = g_getenv ("GST_USE_NV_STATELESS_CODEC");
  if (env) {
    gchar **split;
    gchar **iter;

    split = g_strsplit (env, ",", 0);

    for (iter = split; *iter; iter++) {
      if (g_ascii_strcasecmp (*iter, "h264") == 0) {
        GST_INFO ("Found %s in GST_USE_NV_STATELESS_CODEC environment", *iter);
        use_h264_sl_dec = TRUE;
      } else if (g_ascii_strcasecmp (*iter, "h265") == 0) {
        GST_INFO ("Found %s in GST_USE_NV_STATELESS_CODEC environment", *iter);
        use_h265_sl_dec = TRUE;
      }
    }

    g_strfreev (split);
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

    if (nvdec_available) {
      gint j;

      for (j = 0; j < cudaVideoCodec_NumCodecs; j++) {
        GstCaps *sink_template = NULL;
        GstCaps *src_template = NULL;
        cudaVideoCodec codec = (cudaVideoCodec) j;
        gboolean register_cuviddec = TRUE;

        if (gst_nv_decoder_check_device_caps (cuda_ctx,
                codec, &sink_template, &src_template)) {
          const gchar *codec_name = gst_cuda_video_codec_to_string (codec);

          GST_INFO ("CUDA video codec %s, sink template %" GST_PTR_FORMAT
              "src template %" GST_PTR_FORMAT, codec_name,
              sink_template, src_template);

          switch (codec) {
            case cudaVideoCodec_H264:
              gst_nv_h264_dec_register (plugin,
                  i, GST_RANK_SECONDARY, sink_template, src_template, FALSE);
              if (use_h264_sl_dec) {
                GST_INFO ("Skip register cuvid parser based nvh264dec");
                register_cuviddec = FALSE;

                gst_nv_h264_dec_register (plugin,
                    i, GST_RANK_PRIMARY, sink_template, src_template, TRUE);
              }
              break;
            case cudaVideoCodec_HEVC:
              gst_nv_h265_dec_register (plugin,
                  i, GST_RANK_SECONDARY, sink_template, src_template, FALSE);
              if (use_h265_sl_dec) {
                GST_INFO ("Skip register cuvid parser based nvh264dec");
                register_cuviddec = FALSE;

                gst_nv_h265_dec_register (plugin,
                    i, GST_RANK_PRIMARY, sink_template, src_template, TRUE);
              }
              break;
            default:
              break;
          }

          if (register_cuviddec) {
            gst_nvdec_plugin_init (plugin,
                i, codec, codec_name, sink_template, src_template);
          }

          gst_caps_unref (sink_template);
          gst_caps_unref (src_template);
        }
      }
    }

    if (nvenc_available)
      gst_nvenc_plugin_init (plugin, i, cuda_ctx);

    CuCtxDestroy (cuda_ctx);
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, nvcodec,
    "GStreamer NVCODEC plugin", plugin_init, VERSION, "LGPL",
    GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
