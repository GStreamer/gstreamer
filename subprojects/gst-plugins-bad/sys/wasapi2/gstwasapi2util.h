/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifndef __GST_WASAPI2_UTIL_H__
#define __GST_WASAPI2_UTIL_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <string.h>
#include <mmdeviceapi.h>

G_BEGIN_DECLS

/* Static Caps shared between source, sink, and device provider */
#define GST_WASAPI2_STATIC_CAPS "audio/x-raw, " \
        "format = (string) " GST_AUDIO_FORMATS_ALL ", " \
        "layout = (string) interleaved, " \
        "rate = " GST_AUDIO_RATE_RANGE ", " \
        "channels = " GST_AUDIO_CHANNELS_RANGE

#define GST_WASAPI2_CLEAR_COM(obj) G_STMT_START { \
    if (obj) { \
      (obj)->Release (); \
      (obj) = NULL; \
    } \
  } G_STMT_END

gboolean  _gst_wasapi2_result (HRESULT hr,
                               GstDebugCategory * cat,
                               const gchar * file,
                               const gchar * function,
                               gint line);

#ifndef GST_DISABLE_GST_DEBUG
#define gst_wasapi2_result(result) \
    _gst_wasapi2_result (result, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__)
#else
#define gst_wasapi2_result(result) \
    _gst_wasapi2_result (result, NULL, __FILE__, GST_FUNCTION, __LINE__)
#endif

typedef enum
{
  GST_WASAPI2_ENDPOINT_CLASS_CAPTURE = 0,
  GST_WASAPI2_ENDPOINT_CLASS_RENDER,
  GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE,
  GST_WASAPI2_ENDPOINT_CLASS_INCLUDE_PROCESS_LOOPBACK_CAPTURE,
  GST_WASAPI2_ENDPOINT_CLASS_EXCLUDE_PROCESS_LOOPBACK_CAPTURE,
} GstWasapi2EndpointClass;

static inline gboolean
gst_wasapi2_is_loopback_class (GstWasapi2EndpointClass device_class)
{
  switch (device_class) {
    case GST_WASAPI2_ENDPOINT_CLASS_LOOPBACK_CAPTURE:
      return TRUE;
    default:
      break;
  }

  return FALSE;
}

static inline gboolean
gst_wasapi2_is_process_loopback_class (GstWasapi2EndpointClass device_class)
{
  switch (device_class) {
    case GST_WASAPI2_ENDPOINT_CLASS_INCLUDE_PROCESS_LOOPBACK_CAPTURE:
    case GST_WASAPI2_ENDPOINT_CLASS_EXCLUDE_PROCESS_LOOPBACK_CAPTURE:
      return TRUE;
    default:
      break;
  }

  return FALSE;
}

guint64       gst_wasapi2_util_waveformatex_to_channel_mask (WAVEFORMATEX * format,
                                                             GstAudioChannelPosition ** out_position);

const gchar * gst_wasapi2_util_waveformatex_to_audio_format (WAVEFORMATEX * format);

gboolean      gst_wasapi2_util_parse_waveformatex (WAVEFORMATEX * format,
                                                   GstCaps ** out_caps,
                                                   GstAudioChannelPosition ** out_positions);

gchar *       gst_wasapi2_util_get_error_message  (HRESULT hr);

gboolean      gst_wasapi2_can_automatic_stream_routing (void);

gboolean      gst_wasapi2_can_process_loopback (void);

WAVEFORMATEX * gst_wasapi2_get_default_mix_format (void);

const wchar_t * gst_wasapi2_get_default_device_id_wide (EDataFlow flow);

const char * gst_wasapi2_get_default_device_id (EDataFlow flow);

const gchar * gst_wasapi2_data_flow_to_string (EDataFlow flow);

const gchar * gst_wasapi2_role_to_string (ERole role);

void gst_wasapi2_free_wfx (WAVEFORMATEX * wfx);

void gst_wasapi2_clear_wfx (WAVEFORMATEX ** wfx);

WAVEFORMATEX * gst_wasapi2_copy_wfx (WAVEFORMATEX * format);

gboolean gst_wasapi2_get_exclusive_formats (IAudioClient * client,
                                            IPropertyStore * props,
                                            GPtrArray * list);

GstCaps * gst_wasapi2_wfx_list_to_caps (GPtrArray * list);

void      gst_wasapi2_sort_wfx (GPtrArray * list,
                                WAVEFORMATEX * wfx);

G_END_DECLS

#ifdef __cplusplus
#include <mutex>

#define GST_WASAPI2_CALL_ONCE_BEGIN \
    static std::once_flag __once_flag; \
    std::call_once (__once_flag, [&]()

#define GST_WASAPI2_CALL_ONCE_END )
#endif

#endif /* __GST_WASAPI_UTIL_H__ */
