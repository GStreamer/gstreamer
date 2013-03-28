/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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

#ifndef __GST_WASAPI_UTIL_H__
#define __GST_WASAPI_UTIL_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiosrc.h>
#include <gst/audio/gstaudiosink.h>

#include <audioclient.h>

const gchar *
gst_wasapi_util_hresult_to_string (HRESULT hr);

gboolean
gst_wasapi_util_get_default_device_client (GstElement * element,
                                           gboolean capture,
                                           IAudioClient ** ret_client);

gboolean gst_wasapi_util_get_render_client (GstElement * element,
                                            IAudioClient *client,
                                            IAudioRenderClient ** ret_render_client);

gboolean gst_wasapi_util_get_capture_client (GstElement * element,
                                             IAudioClient * client,
                                             IAudioCaptureClient ** ret_capture_client);

gboolean gst_wasapi_util_get_clock (GstElement * element,
                                    IAudioClient * client,
                                    IAudioClock ** ret_clock);

void
gst_wasapi_util_audio_info_to_waveformatex (GstAudioInfo *info,
                                       WAVEFORMATEXTENSIBLE *format);

#endif /* __GST_WASAPI_UTIL_H__ */

