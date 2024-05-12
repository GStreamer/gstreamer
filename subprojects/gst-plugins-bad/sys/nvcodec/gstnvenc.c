/* GStreamer NVENC plugin
 * Copyright (C) 2015 Centricular Ltd
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

#include "gstnvenc.h"
#include <gst/cuda/gstcudautils.h>
#include <string.h>

#include <gmodule.h>

#ifdef HAVE_CUDA_GST_GL
#include <gst/gl/gl.h>
#endif

#ifdef _WIN32
#ifdef _WIN64
#define NVENC_LIBRARY_NAME "nvEncodeAPI64.dll"
#else
#define NVENC_LIBRARY_NAME "nvEncodeAPI.dll"
#endif
#else
#define NVENC_LIBRARY_NAME "libnvidia-encode.so.1"
#endif

/* For backward compatibility */
#define GST_NVENC_MIN_API_MAJOR_VERSION 10
#define GST_NVENC_MIN_API_MINOR_VERSION 0

#define GST_NVENCAPI_VERSION(major,minor) ((major) | ((minor) << 24))
#define GST_NVENCAPI_STRUCT_VERSION(ver,api_ver) ((uint32_t)(api_ver) | ((ver)<<16) | (0x7 << 28))

static guint32 gst_nvenc_api_version = NVENCAPI_VERSION;
static gboolean gst_nvenc_supports_cuda_stream = FALSE;

typedef NVENCSTATUS NVENCAPI
tNvEncodeAPICreateInstance (NV_ENCODE_API_FUNCTION_LIST * functionList);
tNvEncodeAPICreateInstance *nvEncodeAPICreateInstance;

typedef NVENCSTATUS NVENCAPI
tNvEncodeAPIGetMaxSupportedVersion (uint32_t * version);
tNvEncodeAPIGetMaxSupportedVersion *nvEncodeAPIGetMaxSupportedVersion;

GST_DEBUG_CATEGORY_EXTERN (gst_nvenc_debug);
#define GST_CAT_DEFAULT gst_nvenc_debug

static NV_ENCODE_API_FUNCTION_LIST nvenc_api;

NVENCSTATUS NVENCAPI
NvEncOpenEncodeSessionEx (NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS * params,
    void **encoder)
{
  g_assert (nvenc_api.nvEncOpenEncodeSessionEx != NULL);
  return nvenc_api.nvEncOpenEncodeSessionEx (params, encoder);
}

NVENCSTATUS NVENCAPI
NvEncDestroyEncoder (void *encoder)
{
  g_assert (nvenc_api.nvEncDestroyEncoder != NULL);
  return nvenc_api.nvEncDestroyEncoder (encoder);
}

const char *NVENCAPI
NvEncGetLastErrorString (void *encoder)
{
  g_assert (nvenc_api.nvEncGetLastErrorString != NULL);
  return nvenc_api.nvEncGetLastErrorString (encoder);
}

NVENCSTATUS NVENCAPI
NvEncGetEncodeGUIDs (void *encoder, GUID * array, uint32_t array_size,
    uint32_t * count)
{
  g_assert (nvenc_api.nvEncGetEncodeGUIDs != NULL);
  return nvenc_api.nvEncGetEncodeGUIDs (encoder, array, array_size, count);
}

NVENCSTATUS NVENCAPI
NvEncGetEncodeProfileGUIDCount (void *encoder, GUID encodeGUID,
    uint32_t * encodeProfileGUIDCount)
{
  g_assert (nvenc_api.nvEncGetEncodeProfileGUIDCount != NULL);
  return nvenc_api.nvEncGetEncodeProfileGUIDCount (encoder, encodeGUID,
      encodeProfileGUIDCount);
}

NVENCSTATUS NVENCAPI
NvEncGetEncodeProfileGUIDs (void *encoder, GUID encodeGUID,
    GUID * profileGUIDs, uint32_t guidArraySize, uint32_t * GUIDCount)
{
  g_assert (nvenc_api.nvEncGetEncodeProfileGUIDs != NULL);
  return nvenc_api.nvEncGetEncodeProfileGUIDs (encoder, encodeGUID,
      profileGUIDs, guidArraySize, GUIDCount);
}

NVENCSTATUS NVENCAPI
NvEncGetInputFormats (void *encoder, GUID enc_guid,
    NV_ENC_BUFFER_FORMAT * array, uint32_t size, uint32_t * num)
{
  g_assert (nvenc_api.nvEncGetInputFormats != NULL);
  return nvenc_api.nvEncGetInputFormats (encoder, enc_guid, array, size, num);
}

NVENCSTATUS NVENCAPI
NvEncGetEncodePresetCount (void *encoder, GUID encodeGUID,
    uint32_t * encodePresetGUIDCount)
{
  g_assert (nvenc_api.nvEncGetEncodeProfileGUIDCount != NULL);
  return nvenc_api.nvEncGetEncodePresetCount (encoder, encodeGUID,
      encodePresetGUIDCount);
}

NVENCSTATUS NVENCAPI
NvEncGetEncodePresetGUIDs (void *encoder, GUID encodeGUID,
    GUID * presetGUIDs, uint32_t guidArraySize, uint32_t * GUIDCount)
{
  g_assert (nvenc_api.nvEncGetEncodeProfileGUIDs != NULL);
  return nvenc_api.nvEncGetEncodePresetGUIDs (encoder, encodeGUID,
      presetGUIDs, guidArraySize, GUIDCount);
}

NVENCSTATUS NVENCAPI
NvEncGetEncodePresetConfig (void *encoder, GUID encodeGUID,
    GUID presetGUID, NV_ENC_PRESET_CONFIG * presetConfig)
{
  g_assert (nvenc_api.nvEncGetEncodePresetConfig != NULL);
  return nvenc_api.nvEncGetEncodePresetConfig (encoder, encodeGUID, presetGUID,
      presetConfig);
}

NVENCSTATUS NVENCAPI
NvEncGetEncodePresetConfigEx (void *encoder, GUID encodeGUID,
    GUID presetGUID, NV_ENC_TUNING_INFO tuningInfo,
    NV_ENC_PRESET_CONFIG * presetConfig)
{
  if (!nvenc_api.nvEncGetEncodePresetConfigEx)
    return NV_ENC_ERR_UNIMPLEMENTED;

  return nvenc_api.nvEncGetEncodePresetConfigEx (encoder, encodeGUID,
      presetGUID, tuningInfo, presetConfig);
}

NVENCSTATUS NVENCAPI
NvEncGetEncodeCaps (void *encoder, GUID encodeGUID,
    NV_ENC_CAPS_PARAM * capsParam, int *capsVal)
{
  g_assert (nvenc_api.nvEncGetEncodeCaps != NULL);
  return nvenc_api.nvEncGetEncodeCaps (encoder, encodeGUID, capsParam, capsVal);
}

NVENCSTATUS NVENCAPI
NvEncGetSequenceParams (void *encoder,
    NV_ENC_SEQUENCE_PARAM_PAYLOAD * sequenceParamPayload)
{
  g_assert (nvenc_api.nvEncGetSequenceParams != NULL);
  return nvenc_api.nvEncGetSequenceParams (encoder, sequenceParamPayload);
}

NVENCSTATUS NVENCAPI
NvEncInitializeEncoder (void *encoder, NV_ENC_INITIALIZE_PARAMS * params)
{
  g_assert (nvenc_api.nvEncInitializeEncoder != NULL);
  return nvenc_api.nvEncInitializeEncoder (encoder, params);
}

NVENCSTATUS NVENCAPI
NvEncReconfigureEncoder (void *encoder, NV_ENC_RECONFIGURE_PARAMS * params)
{
  g_assert (nvenc_api.nvEncReconfigureEncoder != NULL);
  return nvenc_api.nvEncReconfigureEncoder (encoder, params);
}

NVENCSTATUS NVENCAPI
NvEncRegisterResource (void *encoder, NV_ENC_REGISTER_RESOURCE * params)
{
  g_assert (nvenc_api.nvEncRegisterResource != NULL);
  return nvenc_api.nvEncRegisterResource (encoder, params);
}

NVENCSTATUS NVENCAPI
NvEncUnregisterResource (void *encoder, NV_ENC_REGISTERED_PTR resource)
{
  g_assert (nvenc_api.nvEncUnregisterResource != NULL);
  return nvenc_api.nvEncUnregisterResource (encoder, resource);
}

NVENCSTATUS NVENCAPI
NvEncMapInputResource (void *encoder, NV_ENC_MAP_INPUT_RESOURCE * params)
{
  g_assert (nvenc_api.nvEncMapInputResource != NULL);
  return nvenc_api.nvEncMapInputResource (encoder, params);
}

NVENCSTATUS NVENCAPI
NvEncUnmapInputResource (void *encoder, NV_ENC_INPUT_PTR input_buffer)
{
  g_assert (nvenc_api.nvEncUnmapInputResource != NULL);
  return nvenc_api.nvEncUnmapInputResource (encoder, input_buffer);
}

NVENCSTATUS NVENCAPI
NvEncCreateInputBuffer (void *encoder, NV_ENC_CREATE_INPUT_BUFFER * input_buf)
{
  g_assert (nvenc_api.nvEncCreateInputBuffer != NULL);
  return nvenc_api.nvEncCreateInputBuffer (encoder, input_buf);
}

NVENCSTATUS NVENCAPI
NvEncLockInputBuffer (void *encoder, NV_ENC_LOCK_INPUT_BUFFER * input_buf)
{
  g_assert (nvenc_api.nvEncLockInputBuffer != NULL);
  return nvenc_api.nvEncLockInputBuffer (encoder, input_buf);
}

NVENCSTATUS NVENCAPI
NvEncUnlockInputBuffer (void *encoder, NV_ENC_INPUT_PTR input_buf)
{
  g_assert (nvenc_api.nvEncUnlockInputBuffer != NULL);
  return nvenc_api.nvEncUnlockInputBuffer (encoder, input_buf);
}

NVENCSTATUS NVENCAPI
NvEncDestroyInputBuffer (void *encoder, NV_ENC_INPUT_PTR input_buf)
{
  g_assert (nvenc_api.nvEncDestroyInputBuffer != NULL);
  return nvenc_api.nvEncDestroyInputBuffer (encoder, input_buf);
}

NVENCSTATUS NVENCAPI
NvEncCreateBitstreamBuffer (void *encoder, NV_ENC_CREATE_BITSTREAM_BUFFER * bb)
{
  g_assert (nvenc_api.nvEncCreateBitstreamBuffer != NULL);
  return nvenc_api.nvEncCreateBitstreamBuffer (encoder, bb);
}

NVENCSTATUS NVENCAPI
NvEncLockBitstream (void *encoder, NV_ENC_LOCK_BITSTREAM * lock_bs)
{
  g_assert (nvenc_api.nvEncLockBitstream != NULL);
  return nvenc_api.nvEncLockBitstream (encoder, lock_bs);
}

NVENCSTATUS NVENCAPI
NvEncUnlockBitstream (void *encoder, NV_ENC_OUTPUT_PTR bb)
{
  g_assert (nvenc_api.nvEncUnlockBitstream != NULL);
  return nvenc_api.nvEncUnlockBitstream (encoder, bb);
}

NVENCSTATUS NVENCAPI
NvEncDestroyBitstreamBuffer (void *encoder, NV_ENC_OUTPUT_PTR bit_buf)
{
  g_assert (nvenc_api.nvEncDestroyBitstreamBuffer != NULL);
  return nvenc_api.nvEncDestroyBitstreamBuffer (encoder, bit_buf);
}

NVENCSTATUS NVENCAPI
NvEncEncodePicture (void *encoder, NV_ENC_PIC_PARAMS * pic_params)
{
  g_assert (nvenc_api.nvEncEncodePicture != NULL);
  return nvenc_api.nvEncEncodePicture (encoder, pic_params);
}

NVENCSTATUS NVENCAPI
NvEncRegisterAsyncEvent (void *encoder, NV_ENC_EVENT_PARAMS * event_params)
{
  g_assert (nvenc_api.nvEncRegisterAsyncEvent != NULL);
  return nvenc_api.nvEncRegisterAsyncEvent (encoder, event_params);
}

NVENCSTATUS NVENCAPI
NvEncUnregisterAsyncEvent (void *encoder, NV_ENC_EVENT_PARAMS * event_params)
{
  g_assert (nvenc_api.nvEncUnregisterAsyncEvent != NULL);
  return nvenc_api.nvEncUnregisterAsyncEvent (encoder, event_params);
}

NVENCSTATUS NVENCAPI
NvEncSetIOCudaStreams (void *encoder, NV_ENC_CUSTREAM_PTR input_stream,
    NV_ENC_CUSTREAM_PTR output_stream)
{
  g_assert (nvenc_api.nvEncSetIOCudaStreams != NULL);
  return nvenc_api.nvEncSetIOCudaStreams (encoder, input_stream, output_stream);
}

typedef struct
{
  gint major;
  gint minor;
} GstNvEncVersion;

gboolean
gst_nvenc_load_library (guint * api_major_ver, guint * api_minor_ver)
{
  GModule *module;
  NVENCSTATUS ret = NV_ENC_SUCCESS;
  uint32_t max_supported_version;
  gint major_ver, minor_ver;
  gint i;
  static const GstNvEncVersion version_list[] = {
    {NVENCAPI_MAJOR_VERSION, NVENCAPI_MINOR_VERSION},
    {12, 1},
    {12, 0},
    {11, 1},
    {11, 0},
    {GST_NVENC_MIN_API_MAJOR_VERSION, GST_NVENC_MIN_API_MINOR_VERSION}
  };

  module = g_module_open (NVENC_LIBRARY_NAME, G_MODULE_BIND_LAZY);
  if (module == NULL) {
    GST_WARNING ("Could not open library %s, %s",
        NVENC_LIBRARY_NAME, g_module_error ());
    return FALSE;
  }

  if (!g_module_symbol (module, "NvEncodeAPICreateInstance",
          (gpointer *) & nvEncodeAPICreateInstance)) {
    GST_ERROR ("%s", g_module_error ());
    return FALSE;
  }

  if (!g_module_symbol (module, "NvEncodeAPIGetMaxSupportedVersion",
          (gpointer *) & nvEncodeAPIGetMaxSupportedVersion)) {
    GST_ERROR ("NvEncodeAPIGetMaxSupportedVersion unavailable");
    return FALSE;
  }

  /* WARNING: Any developers who want to bump SDK version must ensure that
   * following macro values were not changed and also need to check ABI compatibility.
   * Otherwise, gst_nvenc_get_ helpers also should be updated.
   * Currently SDK 8.1 and 9.0 compatible
   *
   * NVENCAPI_VERSION (NVENCAPI_MAJOR_VERSION | (NVENCAPI_MINOR_VERSION << 24))
   *
   * NVENCAPI_STRUCT_VERSION(ver) ((uint32_t)NVENCAPI_VERSION | ((ver)<<16) | (0x7 << 28))
   *
   * NV_ENC_CAPS_PARAM_VER                NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_ENCODE_OUT_PARAMS_VER         NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_CREATE_INPUT_BUFFER_VER       NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_CREATE_BITSTREAM_BUFFER_VER   NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_CREATE_MV_BUFFER_VER          NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_RC_PARAMS_VER                 NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_CONFIG_VER                   (NVENCAPI_STRUCT_VERSION(7) | ( 1<<31 ))
   * NV_ENC_INITIALIZE_PARAMS_VER        (NVENCAPI_STRUCT_VERSION(5) | ( 1<<31 ))
   * NV_ENC_RECONFIGURE_PARAMS_VER       (NVENCAPI_STRUCT_VERSION(1) | ( 1<<31 ))
   * NV_ENC_PRESET_CONFIG_VER            (NVENCAPI_STRUCT_VERSION(4) | ( 1<<31 ))
   * NV_ENC_PIC_PARAMS_VER               (NVENCAPI_STRUCT_VERSION(4) | ( 1<<31 ))
   * NV_ENC_MEONLY_PARAMS_VER             NVENCAPI_STRUCT_VERSION(3)
   * NV_ENC_LOCK_BITSTREAM_VER            NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_LOCK_INPUT_BUFFER_VER         NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_MAP_INPUT_RESOURCE_VER        NVENCAPI_STRUCT_VERSION(4)
   * NV_ENC_REGISTER_RESOURCE_VER         NVENCAPI_STRUCT_VERSION(3)
   * NV_ENC_STAT_VER                      NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER    NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_EVENT_PARAMS_VER              NVENCAPI_STRUCT_VERSION(1)
   * NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER NVENCAPI_STRUCT_VERSION(1)
   * NV_ENCODE_API_FUNCTION_LIST_VER      NVENCAPI_STRUCT_VERSION(2)
   */

  ret = nvEncodeAPIGetMaxSupportedVersion (&max_supported_version);

  if (ret != NV_ENC_SUCCESS) {
    GST_ERROR ("Could not query max supported api version, ret %d", ret);
    return FALSE;
  }

  /* 4 LSB: minor version
   * the rest: major version */
  major_ver = max_supported_version >> 4;
  minor_ver = max_supported_version & 0xf;

  GST_INFO ("Maximum supported API version by driver: %d.%d",
      major_ver, minor_ver);

  ret = NV_ENC_ERR_INVALID_VERSION;
  for (i = 0; i < G_N_ELEMENTS (version_list); i++) {
    if (version_list[i].major > major_ver ||
        (version_list[i].major == major_ver
            && version_list[i].minor > minor_ver)) {
      continue;
    }

    GST_INFO ("Checking version %d.%d", version_list[i].major,
        version_list[i].minor);

    gst_nvenc_api_version =
        GST_NVENCAPI_VERSION (version_list[i].major, version_list[i].minor);

    memset (&nvenc_api, 0, sizeof (NV_ENCODE_API_FUNCTION_LIST));
    nvenc_api.version = GST_NVENCAPI_STRUCT_VERSION (2, gst_nvenc_api_version);
    ret = nvEncodeAPICreateInstance (&nvenc_api);

    if (ret == NV_ENC_SUCCESS) {
      GST_INFO ("API version %d.%d load done",
          version_list[i].major, version_list[i].minor);

      *api_major_ver = version_list[i].major;
      *api_minor_ver = version_list[i].minor;

      if ((version_list[i].major > 9 ||
              (version_list[i].major == 9 && version_list[i].minor > 0)) &&
          nvenc_api.nvEncSetIOCudaStreams) {
        GST_INFO ("nvEncSetIOCudaStreams is supported");
        gst_nvenc_supports_cuda_stream = TRUE;
      }

      break;
    } else {
      GST_INFO ("Version %d.%d is not supported", version_list[i].major,
          version_list[i].minor);
    }
  }

  return ret == NV_ENC_SUCCESS;
}

/* To verify things when updating SDK */
#define USE_STATIC_SDK_VER 0

guint32
gst_nvenc_get_api_version (void)
{
#if USE_STATIC_SDK_VER
  return NVENCAPI_VERSION;
#else
  /* NVENCAPI_VERSION == (NVENCAPI_MAJOR_VERSION | (NVENCAPI_MINOR_VERSION << 24)) */
  return gst_nvenc_api_version;
#endif
}

guint32
gst_nvenc_get_caps_param_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_CAPS_PARAM_VER;
#else
  /* NV_ENC_CAPS_PARAM_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_encode_out_params_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_ENCODE_OUT_PARAMS_VER;
#else
  /* NV_ENC_ENCODE_OUT_PARAMS_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_create_input_buffer_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_CREATE_INPUT_BUFFER_VER;
#else
  /* NV_ENC_CREATE_INPUT_BUFFER_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_create_bitstream_buffer_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
#else
  /* NV_ENC_CREATE_BITSTREAM_BUFFER_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_create_mv_buffer_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_CREATE_MV_BUFFER_VER;
#else
  /* NV_ENC_CREATE_MV_BUFFER_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_rc_params_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_RC_PARAMS_VER;
#else
  /* NV_ENC_RC_PARAMS_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_config_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_CONFIG_VER;
#else
  /* Version updated since SDK 12.0 */
  if ((gst_nvenc_api_version & 12) == 12)
    return GST_NVENCAPI_STRUCT_VERSION (8, gst_nvenc_api_version) | (1 << 31);

  /* NV_ENC_CONFIG_VER ==
   *   (NVENCAPI_STRUCT_VERSION(7) | ( 1<<31 )) */
  return GST_NVENCAPI_STRUCT_VERSION (7, gst_nvenc_api_version) | (1 << 31);
#endif
}

guint32
gst_nvenc_get_initialize_params_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_INITIALIZE_PARAMS_VER;
#else
  /* NV_ENC_INITIALIZE_PARAMS_VER ==
   *   (NVENCAPI_STRUCT_VERSION(5) | ( 1<<31 )) */
  return GST_NVENCAPI_STRUCT_VERSION (5, gst_nvenc_api_version) | (1 << 31);
#endif
}

guint32
gst_nvenc_get_reconfigure_params_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_RECONFIGURE_PARAMS_VER;
#else
  /* NV_ENC_RECONFIGURE_PARAMS_VER ==
   *   (NVENCAPI_STRUCT_VERSION(1) | ( 1<<31 )) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version) | (1 << 31);
#endif
}

guint32
gst_nvenc_get_preset_config_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_PRESET_CONFIG_VER;
#else
  /* NV_ENC_PRESET_CONFIG_VER ==
   *   (NVENCAPI_STRUCT_VERSION(4) | ( 1<<31 )) */
  return GST_NVENCAPI_STRUCT_VERSION (4, gst_nvenc_api_version) | (1 << 31);
#endif
}

guint32
gst_nvenc_get_pic_params_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_PIC_PARAMS_VER;
#else
  /* NV_ENC_PIC_PARAMS_VER ==
   *  (NVENCAPI_STRUCT_VERSION(4) | ( 1<<31 )) */
  return GST_NVENCAPI_STRUCT_VERSION (4, gst_nvenc_api_version) | (1 << 31);
#endif
}

guint32
gst_nvenc_get_meonly_params_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_MEONLY_PARAMS_VER;
#else
  /* NV_ENC_MEONLY_PARAMS_VER == NVENCAPI_STRUCT_VERSION(3) */
  return GST_NVENCAPI_STRUCT_VERSION (3, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_lock_bitstream_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_LOCK_BITSTREAM_VER;
#else
  /* NV_ENC_LOCK_BITSTREAM_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_lock_input_buffer_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_LOCK_INPUT_BUFFER_VER;
#else
  /* NV_ENC_LOCK_INPUT_BUFFER_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_map_input_resource_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_MAP_INPUT_RESOURCE_VER;
#else
  /* NV_ENC_MAP_INPUT_RESOURCE_VER == NVENCAPI_STRUCT_VERSION(4) */
  return GST_NVENCAPI_STRUCT_VERSION (4, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_register_resource_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_REGISTER_RESOURCE_VER;
#else
  /* NV_ENC_REGISTER_RESOURCE_VER == NVENCAPI_STRUCT_VERSION(3) */
  return GST_NVENCAPI_STRUCT_VERSION (3, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_stat_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_STAT_VER;
#else
  /* NV_ENC_STAT_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_sequence_param_payload_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
#else
  /* NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_event_params_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_EVENT_PARAMS_VER;
#else
  /* NV_ENC_EVENT_PARAMS_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

guint32
gst_nvenc_get_open_encode_session_ex_params_version (void)
{
#if USE_STATIC_SDK_VER
  return NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
#else
  /* NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER == NVENCAPI_STRUCT_VERSION(1) */
  return GST_NVENCAPI_STRUCT_VERSION (1, gst_nvenc_api_version);
#endif
}

gboolean
gst_nvenc_have_set_io_cuda_streams (void)
{
  return gst_nvenc_supports_cuda_stream;
}
