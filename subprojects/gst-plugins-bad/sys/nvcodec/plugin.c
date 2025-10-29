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

#include <gst/cuda/gstcuda.h>
#ifdef HAVE_NVCODEC_DGPU
#include "gstnvdec.h"
#include "gstnvenc.h"
#include "gstnvav1dec.h"
#include "gstnvh264dec.h"
#include "gstnvh265dec.h"
#include "gstnvvp8dec.h"
#include "gstnvvp9dec.h"
#include "gstnvdecoder.h"
#endif
#include "gstcudamemorycopy.h"
#include "gstcudaconvertscale.h"
#include <gst/cuda/gstcudanvmm-private.h>

#ifdef G_OS_WIN32
#include <gst/d3d11/gstd3d11.h>
#endif
#ifdef HAVE_NVCODEC_DGPU
#include "gstnvh264encoder.h"
#include "gstnvh265encoder.h"
#include "gstnvav1encoder.h"
#include "gstnvjpegenc.h"
#endif

#include "gstcudaipcsink.h"
#include "gstcudaipcsrc.h"
#include "gstnvcodecutils.h"
#include "gstcudacompositor.h"

#include <glib/gi18n-lib.h>

GST_DEBUG_CATEGORY (gst_nvcodec_debug);
GST_DEBUG_CATEGORY (gst_nvdec_debug);
GST_DEBUG_CATEGORY (gst_nvenc_debug);
GST_DEBUG_CATEGORY (gst_nv_decoder_debug);

#define GST_CAT_DEFAULT gst_nvcodec_debug

#ifdef G_OS_WIN32
#define CUDA_LIBNAME "nvcuda.dll"
#define NVCUVID_LIBNAME "nvcuvid.dll"
#ifdef _WIN64
#define NVENC_LIBNAME "nvEncodeAPI64.dll"
#else
#define NVENC_LIBNAME "nvEncodeAPI.dll"
#endif
#define NVRTC_LIBNAME "nvrtc64_*_0.dll"
#else /* G_OS_WIN32 */
#define CUDA_LIBNAME "libcuda.so.1"
#define NVCUVID_LIBNAME "libnvcuvid.so.1"
#define NVENC_LIBNAME "libnvidia-encode.so.1"
#define NVRTC_LIBNAME "libnvrtc.so"
#endif /* G_OS_WIN32 */

/* X-macro listing all GstNvEncoderDeviceCaps fields for serialization */
#define GST_NV_ENCODER_DEVICE_CAPS_FIELDS(X) \
  X(max_bframes) \
  X(ratecontrol_modes) \
  X(field_encoding) \
  X(monochrome) \
  X(fmo) \
  X(qpelmv) \
  X(bdirect_mode) \
  X(cabac) \
  X(adaptive_transform) \
  X(stereo_mvc) \
  X(temoral_layers) \
  X(hierarchical_pframes) \
  X(hierarchical_bframes) \
  X(level_max) \
  X(level_min) \
  X(separate_colour_plane) \
  X(width_max) \
  X(height_max) \
  X(temporal_svc) \
  X(dyn_res_change) \
  X(dyn_bitrate_change) \
  X(dyn_force_constqp) \
  X(dyn_rcmode_change) \
  X(subframe_readback) \
  X(constrained_encoding) \
  X(intra_refresh) \
  X(custom_vbv_buf_size) \
  X(dynamic_slice_mode) \
  X(ref_pic_invalidation) \
  X(preproc_support) \
  X(async_encoding_support) \
  X(mb_num_max) \
  X(mb_per_sec_max) \
  X(yuv444_encode) \
  X(lossless_encode) \
  X(sao) \
  X(meonly_mode) \
  X(lookahead) \
  X(temporal_aq) \
  X(supports_10bit_encode) \
  X(num_max_ltr_frames) \
  X(weighted_prediction) \
  X(bframe_ref_mode) \
  X(emphasis_level_map) \
  X(width_min) \
  X(height_min) \
  X(multiple_ref_frames)

static void
plugin_deinit (gpointer data)
{
  gst_cuda_ipc_client_deinit ();
}

static gboolean
check_runtime_compiler (void)
{
  /* *INDENT-OFF* */
  const gchar *nvrtc_test_source =
    "__global__ void\n"
    "my_kernel (void) {}";
  /* *INDENT-ON* */

  gchar *test_ptx;

  if (!gst_cuda_nvrtc_load_library ())
    return FALSE;

  test_ptx = gst_cuda_nvrtc_compile (nvrtc_test_source);
  if (!test_ptx)
    return FALSE;

  g_free (test_ptx);

  return TRUE;
}

#ifdef HAVE_NVCODEC_DGPU

static gchar *
gst_cuda_uuid_to_string (const CUuuid * uuid)
{
  const guint8 *b = (const guint8 *) uuid->bytes;

  return
      g_strdup_printf
      ("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11],
      b[12], b[13], b[14], b[15]);
}

typedef struct
{
  gchar *device_uuid;
  gint64 adapter_luid;
  GList *decoders;
  GList *encoders;
} DeviceCachedCodecs;

static GList *
gst_nvcodec_load_decoders_from_cache (GstPlugin * plugin,
    const GstStructure * device_struct)
{
  const GValue *decoders_arr;
  guint i, n_decoders;
  GList *decoder_list = NULL;

  decoders_arr = gst_structure_get_value (device_struct, "decoders");
  if (!decoders_arr)
    return NULL;
  g_return_val_if_fail (GST_VALUE_HOLDS_ARRAY (decoders_arr), NULL);

  n_decoders = gst_value_array_get_size (decoders_arr);
  GST_INFO ("Loading %u decoders from cache", n_decoders);

  for (i = 0; i < n_decoders; i++) {
    const GValue *decoder_val = gst_value_array_get_value (decoders_arr, i);
    if (decoder_val == NULL || !GST_VALUE_HOLDS_STRUCTURE (decoder_val)) {
      GST_WARNING ("Invalid cached decoder data");
      g_list_free_full (decoder_list, (GDestroyNotify) g_free);
      return NULL;
    }
    const GstStructure *decoder_struct = gst_value_get_structure (decoder_val);
    GstNvDecoderClassData *class_data = g_new0 (GstNvDecoderClassData, 1);
    if (!gst_structure_get (decoder_struct,
            "codec", G_TYPE_STRING, &class_data->codec_name,
            "sink_caps", GST_TYPE_CAPS, &class_data->sink_caps,
            "src_caps", GST_TYPE_CAPS, &class_data->src_caps, NULL)) {
      GST_WARNING ("Invalid cached nvcodec decoder data");

      g_free (class_data);
      g_list_free_full (decoder_list, (GDestroyNotify) g_free);
      return NULL;
    }

    decoder_list = g_list_append (decoder_list, class_data);
  }

  return decoder_list;
}

static gboolean
gst_nvcodec_load_encoder_device_caps (const GstStructure * caps_struct,
    GstNvEncoderDeviceCaps * caps)
{
#define GET_CAPS_FIELD(field) \
  #field, G_TYPE_INT, &caps->field,

  gst_structure_get (caps_struct,
      GST_NV_ENCODER_DEVICE_CAPS_FIELDS (GET_CAPS_FIELD)
      NULL);

#undef GET_CAPS_FIELD

  return TRUE;
}

static GstNvEncoderClassData *
gst_nvcodec_load_encoder_class_data (const GstStructure *
    encoder_struct, const gchar * codec_name)
{
  GstNvEncoderClassData *cdata;
  GstCaps *sink_caps = NULL, *src_caps = NULL;
  const gchar *device_mode_str;
  const GValue *formats_arr, *profiles_arr;
  const GstStructure *device_caps_struct;
  guint i, n;
  cudaVideoCodec codec;

  gst_structure_get (encoder_struct,
      "sink_caps", GST_TYPE_CAPS, &sink_caps,
      "src_caps", GST_TYPE_CAPS, &src_caps, NULL);
  device_mode_str = gst_structure_get_string (encoder_struct, "device_mode");

  if (!sink_caps || !src_caps || !device_mode_str) {
    gst_clear_caps (&sink_caps);
    gst_clear_caps (&src_caps);
    return NULL;
  }

  /* Convert codec name to cudaVideoCodec */
  codec = gst_cuda_video_codec_from_string (codec_name);
  if (codec == cudaVideoCodec_NumCodecs) {
    GST_WARNING ("Unknown codec name: %s", codec_name);
    gst_caps_unref (sink_caps);
    gst_caps_unref (src_caps);
    return NULL;
  }

  cdata = g_new0 (GstNvEncoderClassData, 1);
  cdata->ref_count = 1;
  cdata->codec = codec;
  cdata->sink_caps = sink_caps;
  cdata->src_caps = src_caps;

  if (g_strcmp0 (device_mode_str, "cuda") == 0)
    cdata->device_mode = GST_NV_ENCODER_DEVICE_CUDA;
  else if (g_strcmp0 (device_mode_str, "d3d11") == 0)
    cdata->device_mode = GST_NV_ENCODER_DEVICE_D3D11;
  else
    cdata->device_mode = GST_NV_ENCODER_DEVICE_AUTO_SELECT;

  gst_structure_get_uint (encoder_struct, "cuda_device_id",
      &cdata->cuda_device_id);

  /* Parse formats list */
  formats_arr = gst_structure_get_value (encoder_struct, "formats");
  if (formats_arr) {
    n = gst_value_array_get_size (formats_arr);
    for (i = 0; i < n; i++) {
      const GValue *format_val = gst_value_array_get_value (formats_arr, i);
      const gchar *format_str = g_value_get_string (format_val);
      cdata->formats = g_list_append (cdata->formats, g_strdup (format_str));
    }
  }

  /* Parse profiles list */
  profiles_arr = gst_structure_get_value (encoder_struct, "profiles");
  if (profiles_arr) {
    n = gst_value_array_get_size (profiles_arr);
    for (i = 0; i < n; i++) {
      const GValue *profile_val = gst_value_array_get_value (profiles_arr, i);
      const gchar *profile_str = g_value_get_string (profile_val);
      cdata->profiles = g_list_append (cdata->profiles, g_strdup (profile_str));
    }
  }

  /* Parse device capabilities */
  if (gst_structure_get (encoder_struct, "device_caps", GST_TYPE_STRUCTURE,
          &device_caps_struct, NULL)) {
    gst_nvcodec_load_encoder_device_caps (device_caps_struct,
        &cdata->device_caps);
  }

  return cdata;
}

static GList *
gst_nvcodec_load_encoder_from_cache (GstPlugin * plugin,
    const GstStructure * device_struct)
{
  const GValue *encoders_arr;
  guint i, n_encoders;
  GList *encoder_list = NULL;

  encoders_arr = gst_structure_get_value (device_struct, "encoders");
  if (!encoders_arr)
    return NULL;

  n_encoders = gst_value_array_get_size (encoders_arr);
  GST_INFO ("Loading %u encoders from cache", n_encoders);

  for (i = 0; i < n_encoders; i++) {
    const GValue *encoder_val = gst_value_array_get_value (encoders_arr, i);
    const GstStructure *encoder_struct = gst_value_get_structure (encoder_val);
    const gchar *codec_name;
    GstNvEncoderClassData *cdata;

    codec_name = gst_structure_get_string (encoder_struct, "codec");
    if (!codec_name) {
      GST_WARNING ("Invalid cached encoder data");
      continue;
    }

    cdata = gst_nvcodec_load_encoder_class_data (encoder_struct, codec_name);
    if (!cdata) {
      GST_WARNING ("Failed to parse cached encoder data for %s", codec_name);
      continue;
    }

    /* Add to unified encoder list */
    encoder_list = g_list_append (encoder_list, cdata);
  }

  return encoder_list;
}

/* Forward declaration */
static void gst_nvcodec_build_encoder_cache_data (GValue * encoders_arr,
    const gchar * codec_name, GstNvEncoderClassData * cdata);

static void
gst_nvcodec_save_cache (GstPlugin * plugin, guint api_major, guint api_minor,
    GArray * devices)
{
  GstStructure *cache_data;
  GstStructure *devices_save;
  guint i;

  cache_data = gst_structure_new ("nvcodec-cache",
      "cuda_api_major", G_TYPE_UINT, api_major,
      "cuda_api_minor", G_TYPE_UINT, api_minor, NULL);

  devices_save = gst_structure_new_empty ("devices");

  for (i = 0; i < devices->len; i++) {
    DeviceCachedCodecs *device =
        &g_array_index (devices, DeviceCachedCodecs, i);
    GstStructure *device_struct;
    GValue decoders_arr = { 0, };
    GValue encoders_arr = { 0, };

    if (!device->device_uuid) {
      GST_WARNING ("Could not get UUID for device %u, skipping cache save", i);
      continue;
    }

    device_struct = gst_structure_new ("device",
        "device_id", G_TYPE_UINT, i, NULL);

    /* Save decoders */
    g_value_init (&decoders_arr, GST_TYPE_ARRAY);
    for (GList * l = device->decoders; l; l = l->next) {
      GstNvDecoderClassData *dec_cdata = (GstNvDecoderClassData *) l->data;
      GstStructure *decoder_struct = gst_structure_new_empty ("decoder");

      gst_structure_set (decoder_struct,
          "codec", G_TYPE_STRING, dec_cdata->codec_name,
          "sink_caps", GST_TYPE_CAPS, dec_cdata->sink_caps,
          "src_caps", GST_TYPE_CAPS, dec_cdata->src_caps, NULL);

      GValue decoder_val = { 0, };
      g_value_init (&decoder_val, GST_TYPE_STRUCTURE);
      g_value_take_boxed (&decoder_val, decoder_struct);
      gst_value_array_append_and_take_value (&decoders_arr, &decoder_val);
    }
    gst_structure_set_value (device_struct, "decoders", &decoders_arr);
    g_value_unset (&decoders_arr);

    /* Save encoders */
    g_value_init (&encoders_arr, GST_TYPE_ARRAY);
    for (GList * l = device->encoders; l; l = l->next) {
      GstNvEncoderClassData *enc_cdata = (GstNvEncoderClassData *) l->data;
      gst_nvcodec_build_encoder_cache_data (&encoders_arr,
          gst_cuda_video_codec_to_string (enc_cdata->codec), enc_cdata);
    }
    gst_structure_set_value (device_struct, "encoders", &encoders_arr);
    g_value_unset (&encoders_arr);

    /* Add device to devices structure using UUID as key */
    GValue device_value = G_VALUE_INIT;
    g_value_init (&device_value, GST_TYPE_STRUCTURE);
    gst_value_set_structure (&device_value, device_struct);
    gst_structure_take_value (devices_save, device->device_uuid, &device_value);
    gst_structure_free (device_struct);
  }

  /* Store devices structure in cache */
  GValue devices_value = G_VALUE_INIT;
  g_value_init (&devices_value, GST_TYPE_STRUCTURE);
  gst_value_set_structure (&devices_value, devices_save);
  gst_structure_take_value (cache_data, "devices", &devices_value);
  gst_structure_free (devices_save);

  GST_INFO ("Saving complete cache data");
  gst_plugin_set_cache_data (plugin, cache_data);
}

static GArray *
gst_nvcodec_get_or_build_cache (GstPlugin * plugin, guint current_api_major,
    guint current_api_minor, gint dev_count, gboolean nvenc_available,
    gboolean nvdec_available)
{
  const GstStructure *cache_data;
  const GstStructure *devices_struct = NULL;
  guint i;
  guint cached_api_major = 0, cached_api_minor = 0;
  gboolean cache_valid = FALSE;
  gboolean cache_needs_save = FALSE;
  GArray *result = g_array_new (FALSE, FALSE, sizeof (DeviceCachedCodecs));

  cache_data = gst_plugin_get_cache_data (plugin);
  if (cache_data) {
    GST_INFO ("Found serialized cache, validating...");

    /* Check CUDA API version for cache invalidation (detects driver updates) */
    if (gst_structure_get_uint (cache_data, "cuda_api_major", &cached_api_major)
        && gst_structure_get_uint (cache_data, "cuda_api_minor",
            &cached_api_minor)) {
      if (cached_api_major == current_api_major
          && cached_api_minor == current_api_minor) {
        /* Get devices structure (UUID-keyed) */
        if (gst_structure_get (cache_data, "devices", GST_TYPE_STRUCTURE,
                &devices_struct, NULL)) {
          cache_valid = TRUE;
          GST_INFO ("Serialized cache is valid (CUDA API %u.%u)",
              cached_api_major, cached_api_minor);
        }
      } else {
        GST_INFO ("CUDA API version mismatch. Cached: %u.%u, Current: %u.%u",
            cached_api_major, cached_api_minor, current_api_major,
            current_api_minor);
      }
    }
  } else {
    GST_INFO ("No serialized cache found");
  }

  /* Build complete cache for all devices */
  for (i = 0; i < (guint) dev_count; i++) {
    DeviceCachedCodecs device_cache = { 0, };
    const GstStructure *cached_device_struct = NULL;
    gboolean use_cached_data = FALSE;

    /* Get current device UUID for cache lookup and later saving */
    CUuuid current_uuid;
    CUresult cuda_ret = CuDeviceGetUuid (&current_uuid, i);
    if (cuda_ret == CUDA_SUCCESS) {
      device_cache.device_uuid = gst_cuda_uuid_to_string (&current_uuid);
    }

    if (cache_valid && device_cache.device_uuid && devices_struct) {
      if (gst_structure_get (devices_struct, device_cache.device_uuid,
              GST_TYPE_STRUCTURE, &cached_device_struct, NULL)) {
        use_cached_data = TRUE;
        GST_INFO ("Device %u: found matching cache entry by UUID %s", i,
            device_cache.device_uuid);
      } else {
        GST_INFO ("Device %u: no matching cache entry found for UUID %s, "
            "will query hardware", i, device_cache.device_uuid);
      }
    }

    /* Use cached data or query hardware */
    if (use_cached_data) {
      /* Load from serialized cache */
      device_cache.decoders =
          gst_nvcodec_load_decoders_from_cache (plugin, cached_device_struct);
      device_cache.encoders =
          gst_nvcodec_load_encoder_from_cache (plugin, cached_device_struct);

#ifdef G_OS_WIN32
      /* Query current LUID - it changes per boot so can't use cached value */
      device_cache.adapter_luid = gst_cuda_context_find_dxgi_adapter_luid (i);

      /* Update encoder class data with current LUID */
      for (GList * l = device_cache.encoders; l; l = l->next) {
        GstNvEncoderClassData *enc_cdata = (GstNvEncoderClassData *) l->data;
        enc_cdata->adapter_luid = device_cache.adapter_luid;
      }
#endif
    } else {
      cache_needs_save = TRUE;
      /* Query hardware to build cache */
      GstCudaContext *context = gst_cuda_context_new (i);

      if (!context) {
        GST_WARNING ("Failed to create context for device %d", i);
        g_array_append_val (result, device_cache);
        continue;
      }

#ifdef G_OS_WIN32
      g_object_get (context, "dxgi-adapter-luid", &device_cache.adapter_luid,
          NULL);
#endif

      GST_INFO ("Device %u: querying hardware to build cache", i);

      /* Build decoder list */
      if (nvdec_available) {
        for (gint j = 0; j < cudaVideoCodec_NumCodecs; j++) {
          GstCaps *sink_template = NULL;
          GstCaps *src_template = NULL;
          cudaVideoCodec codec = (cudaVideoCodec) j;

          if (gst_nv_decoder_check_device_caps (context, NULL,
                  codec, &sink_template, &src_template)) {
            GstNvDecoderClassData *dec_cdata =
                g_new0 (GstNvDecoderClassData, 1);
            dec_cdata->codec_name =
                g_strdup (gst_cuda_video_codec_to_string (codec));
            dec_cdata->sink_caps = sink_template;
            dec_cdata->src_caps = src_template;
            dec_cdata->cuda_device_id = i;
            device_cache.decoders =
                g_list_append (device_cache.decoders, dec_cdata);
          }
        }
      }

      /* Build encoder list */
      if (nvenc_available) {
        GstNvEncoderClassData *enc_cdata;

        enc_cdata = gst_nv_h264_encoder_inspect (plugin, context);
        if (enc_cdata)
          device_cache.encoders =
              g_list_append (device_cache.encoders, enc_cdata);

        enc_cdata = gst_nv_h265_encoder_inspect (plugin, context);
        if (enc_cdata)
          device_cache.encoders =
              g_list_append (device_cache.encoders, enc_cdata);

        enc_cdata = gst_nv_av1_encoder_inspect (plugin, context);
        if (enc_cdata)
          device_cache.encoders =
              g_list_append (device_cache.encoders, enc_cdata);
      }

      gst_object_unref (context);
    }

    g_array_append_val (result, device_cache);
  }

  /* Only save cache if we had to query hardware */
  if (cache_needs_save)
    gst_nvcodec_save_cache (plugin, current_api_major, current_api_minor,
        result);

  return result;
}

static void
gst_nvcodec_build_encoder_device_caps_structure (GstStructure * encoder_struct,
    const GstNvEncoderDeviceCaps * caps)
{
  GstStructure *caps_struct = gst_structure_new_empty ("device_caps");
  GValue v = { 0, };

#define SET_CAPS_FIELD(field) \
  #field, G_TYPE_INT, caps->field,

  gst_structure_set (caps_struct,
      GST_NV_ENCODER_DEVICE_CAPS_FIELDS (SET_CAPS_FIELD)
      NULL);

#undef SET_CAPS_FIELD

  g_value_init (&v, GST_TYPE_STRUCTURE);
  g_value_take_boxed (&v, caps_struct);
  gst_structure_take_value (encoder_struct, "device_caps", &v);
}

static void
gst_nvcodec_build_encoder_cache_data (GValue * encoders_arr,
    const gchar * codec_name, GstNvEncoderClassData * cdata)
{
  GstStructure *encoder_struct = gst_structure_new_empty ("encoder");
  GValue encoder_val = { 0, };
  const gchar *device_mode_str;
  GValue formats_arr = { 0, };
  GValue profiles_arr = { 0, };
  GList *l;

  /* Codec name */
  gst_structure_set (encoder_struct, "codec", G_TYPE_STRING, codec_name, NULL);

  gst_structure_set (encoder_struct,
      "sink_caps", GST_TYPE_CAPS, cdata->sink_caps,
      "src_caps", GST_TYPE_CAPS, cdata->src_caps, NULL);

  /* Device mode */
  switch (cdata->device_mode) {
    case GST_NV_ENCODER_DEVICE_CUDA:
      device_mode_str = "cuda";
      break;
    case GST_NV_ENCODER_DEVICE_D3D11:
      device_mode_str = "d3d11";
      break;
    default:
      device_mode_str = "auto";
      break;
  }
  gst_structure_set (encoder_struct,
      "device_mode", G_TYPE_STRING, device_mode_str,
      "cuda_device_id", G_TYPE_UINT, cdata->cuda_device_id, NULL);

  /* Formats array */
  g_value_init (&formats_arr, GST_TYPE_ARRAY);
  for (l = cdata->formats; l; l = l->next) {
    GValue format_val = { 0, };
    g_value_init (&format_val, G_TYPE_STRING);
    g_value_set_string (&format_val, (const gchar *) l->data);
    gst_value_array_append_value (&formats_arr, &format_val);
    g_value_unset (&format_val);
  }
  gst_structure_set_value (encoder_struct, "formats", &formats_arr);
  g_value_unset (&formats_arr);

  /* Profiles array */
  g_value_init (&profiles_arr, GST_TYPE_ARRAY);
  for (l = cdata->profiles; l; l = l->next) {
    GValue profile_val = { 0, };
    g_value_init (&profile_val, G_TYPE_STRING);
    g_value_set_string (&profile_val, (const gchar *) l->data);
    gst_value_array_append_value (&profiles_arr, &profile_val);
    g_value_unset (&profile_val);
  }
  gst_structure_set_value (encoder_struct, "profiles", &profiles_arr);
  g_value_unset (&profiles_arr);

  /* Device capabilities */
  gst_nvcodec_build_encoder_device_caps_structure (encoder_struct,
      &cdata->device_caps);

  /* Add to encoders array */
  g_value_init (&encoder_val, GST_TYPE_STRUCTURE);
  g_value_take_boxed (&encoder_val, encoder_struct);
  gst_value_array_append_value (encoders_arr, &encoder_val);
  g_value_unset (&encoder_val);
}

#endif /* HAVE_NVCODEC_DGPU */

static gboolean
plugin_init (GstPlugin * plugin)
{
  CUresult cuda_ret;
  const char *err_name = NULL, *err_desc = NULL;
  gint dev_count = 0;
  guint i;
#ifdef HAVE_NVCODEC_DGPU
  gboolean nvdec_available = TRUE;
  gboolean nvenc_available = TRUE;
  /* hardcoded minimum supported version */
  guint api_major_ver = 8;
  guint api_minor_ver = 1;
  GList *h264_enc_cdata = NULL;
  GList *h265_enc_cdata = NULL;
  GList *av1_enc_cdata = NULL;
  gboolean have_nvjpegenc = FALSE;
#endif
  gboolean have_nvrtc = FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_nvcodec_debug, "nvcodec", 0, "nvcodec");
  GST_DEBUG_CATEGORY_INIT (gst_nvdec_debug, "nvdec", 0, "nvdec");
  GST_DEBUG_CATEGORY_INIT (gst_nvenc_debug, "nvenc", 0, "nvenc");
  GST_DEBUG_CATEGORY_INIT (gst_nv_decoder_debug, "nvdecoder", 0, "nvdecoder");
  if (!gst_cuda_load_library ()) {
    gst_plugin_add_status_warning (plugin,
        "CUDA library \"" CUDA_LIBNAME "\" was not found.");
    return TRUE;
  }
#ifdef HAVE_NVCODEC_DGPU
  /* get available API version from nvenc and it will be passed to
   * nvdec */
  if (!gst_nvenc_load_library (&api_major_ver, &api_minor_ver)) {
    gst_plugin_add_status_warning (plugin,
        "NVENC library \"" NVENC_LIBNAME "\" was not found.");
    nvenc_available = FALSE;
  }

  if (!gst_cuvid_load_library (api_major_ver, api_minor_ver)) {
    GST_WARNING ("Failed to load nvdec library version %u.%u", api_major_ver,
        api_minor_ver);
    gst_plugin_add_status_warning (plugin,
        "NVDEC library \"" NVCUVID_LIBNAME "\" was not found.");
    nvdec_available = FALSE;
  }

  if (!nvdec_available && !nvenc_available)
    return TRUE;

#endif

  cuda_ret = CuInit (0);
  if (cuda_ret != CUDA_SUCCESS) {
    CuGetErrorName (cuda_ret, &err_name);
    CuGetErrorString (cuda_ret, &err_desc);
    GST_ERROR ("Failed to init cuda, cuInit ret: 0x%x: %s: %s",
        (int) cuda_ret, err_name, err_desc);

    /* to abort if GST_CUDA_CRITICAL_ERRORS is configured */
    gst_cuda_result (CUDA_ERROR_NO_DEVICE);

    gst_plugin_add_status_error (plugin,
        N_("Unable to initialize CUDA library."));

    return TRUE;
  }

  cuda_ret = CuDeviceGetCount (&dev_count);
  if (cuda_ret != CUDA_SUCCESS || !dev_count) {
    CuGetErrorName (cuda_ret, &err_name);
    CuGetErrorString (cuda_ret, &err_desc);
    GST_ERROR ("No available device, cuDeviceGetCount ret: 0x%x: %s %s",
        (int) cuda_ret, err_name, err_desc);

    gst_plugin_add_status_warning (plugin,
        N_("No NVIDIA graphics cards detected!"));

    return TRUE;
  }

#ifdef NVCODEC_CUDA_PRECOMPILED
  /* If cuda kernels are precompiled, just try library load library.
   * Even if we have compiled kernel bytecode, we may need to use
   * this runtime compiler as a fallback */
  gst_cuda_nvrtc_load_library ();
  have_nvrtc = TRUE;
#else
  have_nvrtc = check_runtime_compiler ();
#endif
  if (!have_nvrtc) {
    gst_plugin_add_status_info (plugin,
        "CUDA runtime compilation library \"" NVRTC_LIBNAME "\" was not found, "
        "check CUDA toolkit package installation");
  }

  /* Get or build complete cache (loads from serialized data or queries hardware) */
  GArray *cache = gst_nvcodec_get_or_build_cache (plugin, api_major_ver,
      api_minor_ver, dev_count, nvenc_available, nvdec_available);

  /* Register elements from cache (cache was already built/loaded above) */
  if (cache && cache->len > 0) {
    for (i = 0; i < cache->len; i++) {
      DeviceCachedCodecs *device =
          &g_array_index (cache, DeviceCachedCodecs, i);

#ifdef HAVE_NVCODEC_DGPU
      /* Register decoders from cache */
      if (nvdec_available && device->decoders) {
        for (GList * l = device->decoders; l; l = l->next) {
          GstNvDecoderClassData *dec_cdata = (GstNvDecoderClassData *) l->data;
          const gchar *codec_name = dec_cdata->codec_name;
          cudaVideoCodec codec = gst_cuda_video_codec_from_string (codec_name);
          gboolean register_cuviddec = FALSE;

          GST_INFO ("Registering decoder for codec %s", codec_name);

          switch (codec) {
            case cudaVideoCodec_H264:
              gst_nv_h264_dec_register (plugin, i, dec_cdata->adapter_luid,
                  GST_RANK_PRIMARY + 1, dec_cdata->sink_caps,
                  dec_cdata->src_caps);
              break;
            case cudaVideoCodec_HEVC:
              gst_nv_h265_dec_register (plugin, i, dec_cdata->adapter_luid,
                  GST_RANK_PRIMARY + 1, dec_cdata->sink_caps,
                  dec_cdata->src_caps);
              break;
            case cudaVideoCodec_VP8:
              gst_nv_vp8_dec_register (plugin, i, dec_cdata->adapter_luid,
                  GST_RANK_PRIMARY, dec_cdata->sink_caps, dec_cdata->src_caps);
              break;
            case cudaVideoCodec_VP9:
              gst_nv_vp9_dec_register (plugin, i, dec_cdata->adapter_luid,
                  GST_RANK_PRIMARY, dec_cdata->sink_caps, dec_cdata->src_caps);
              break;
            case cudaVideoCodec_AV1:
              gst_nv_av1_dec_register (plugin, i, dec_cdata->adapter_luid,
                  GST_RANK_PRIMARY + 1, dec_cdata->sink_caps,
                  dec_cdata->src_caps);
              break;
            default:
              register_cuviddec = TRUE;
              break;
          }

          if (register_cuviddec) {
            gst_nvdec_plugin_init (plugin, i, codec, codec_name,
                dec_cdata->sink_caps, dec_cdata->src_caps);
          }
        }
      }

      /* Register encoders from cache */
      if (nvenc_available && device->encoders) {
#ifdef G_OS_WIN32
        /* Register D3D11 encoders on Windows */
        if (g_win32_check_windows_version (6, 0, 0, G_WIN32_OS_ANY)) {
          GstD3D11Device *d3d11_device;

          d3d11_device =
              gst_d3d11_device_new_for_adapter_luid (device->adapter_luid,
              D3D11_CREATE_DEVICE_BGRA_SUPPORT);
          if (d3d11_device) {
            GstNvEncoderClassData *cdata;

            cdata = gst_nv_h264_encoder_register_d3d11 (plugin,
                d3d11_device, GST_RANK_NONE);
            if (cdata)
              h264_enc_cdata = g_list_append (h264_enc_cdata, cdata);

            cdata = gst_nv_h265_encoder_register_d3d11 (plugin,
                d3d11_device, GST_RANK_NONE);
            if (cdata)
              h265_enc_cdata = g_list_append (h265_enc_cdata, cdata);

            cdata = gst_nv_av1_encoder_register_d3d11 (plugin,
                d3d11_device, GST_RANK_NONE);
            if (cdata)
              av1_enc_cdata = g_list_append (av1_enc_cdata, cdata);

            gst_object_unref (d3d11_device);
          }
        }
#endif // G_OS_WIN32

        /* Register CUDA encoders */
        for (GList * l = device->encoders; l; l = l->next) {
          GstNvEncoderClassData *enc_cdata = (GstNvEncoderClassData *) l->data;

          GST_INFO ("Registering encoder for codec %s",
              gst_cuda_video_codec_to_string (enc_cdata->codec));

          switch (enc_cdata->codec) {
            case cudaVideoCodec_H264:
              gst_nv_h264_encoder_register (plugin,
                  gst_nv_encoder_class_data_ref (enc_cdata),
                  GST_RANK_PRIMARY + 1);
              h264_enc_cdata = g_list_append (h264_enc_cdata, enc_cdata);
              break;
            case cudaVideoCodec_HEVC:
              gst_nv_h265_encoder_register (plugin,
                  gst_nv_encoder_class_data_ref (enc_cdata),
                  GST_RANK_PRIMARY + 1);
              h265_enc_cdata = g_list_append (h265_enc_cdata, enc_cdata);
              break;
            case cudaVideoCodec_AV1:
              gst_nv_av1_encoder_register (plugin,
                  gst_nv_encoder_class_data_ref (enc_cdata),
                  GST_RANK_PRIMARY + 1);
              av1_enc_cdata = g_list_append (av1_enc_cdata, enc_cdata);
              break;
            default:
              GST_WARNING ("Unknown encoder codec: %d", enc_cdata->codec);
              break;
          }
        }
      }

      /* Register JPEG encoder */
      if (gst_nv_jpeg_enc_register (plugin, i, GST_RANK_NONE, have_nvrtc,
              FALSE))
        have_nvjpegenc = TRUE;
#endif // HAVE_NVCODEC_DGPU
    }
  }

#ifdef HAVE_NVCODEC_DGPU
  if (h264_enc_cdata) {
    gst_nv_h264_encoder_register_auto_select (plugin, h264_enc_cdata,
        GST_RANK_NONE);
  }

  if (h265_enc_cdata) {
    gst_nv_h265_encoder_register_auto_select (plugin, h265_enc_cdata,
        GST_RANK_NONE);
  }

  if (av1_enc_cdata) {
    gst_nv_av1_encoder_register_auto_select (plugin, av1_enc_cdata,
        GST_RANK_NONE);
  }

  if (have_nvjpegenc)
    gst_nv_jpeg_enc_register (plugin, 0, GST_RANK_NONE, have_nvrtc, TRUE);
#endif

  gst_cuda_memory_copy_register (plugin, GST_RANK_NONE);

  if (have_nvrtc) {
    gst_element_register (plugin, "cudaconvert", GST_RANK_NONE,
        GST_TYPE_CUDA_CONVERT);
    gst_element_register (plugin, "cudascale", GST_RANK_NONE,
        GST_TYPE_CUDA_SCALE);
    gst_element_register (plugin, "cudaconvertscale", GST_RANK_NONE,
        GST_TYPE_CUDA_CONVERT_SCALE);
    gst_element_register (plugin, "cudacompositor", GST_RANK_NONE,
        GST_TYPE_CUDA_COMPOSITOR);
  }
  gst_element_register (plugin,
      "cudaipcsink", GST_RANK_NONE, GST_TYPE_CUDA_IPC_SINK);
  gst_element_register (plugin,
      "cudaipcsrc", GST_RANK_NONE, GST_TYPE_CUDA_IPC_SRC);

  gst_cuda_memory_init_once ();
  if (gst_cuda_nvmm_init_once ())
    GST_INFO ("Enable NVMM support");

  g_object_set_data_full (G_OBJECT (plugin),
      "plugin-nvcodec-shutdown", (gpointer) "shutdown-data",
      (GDestroyNotify) plugin_deinit);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, nvcodec,
    "GStreamer NVCODEC plugin", plugin_init, VERSION, "LGPL",
    GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
