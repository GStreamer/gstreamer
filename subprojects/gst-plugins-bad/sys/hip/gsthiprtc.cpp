/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gsthiprtc.h"
#include "gsthip.h"
#include <hip/hiprtc.h>
#include <mutex>
#include <vector>
#include <string>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag once;

  std::call_once (once,[&] {
        cat = _gst_debug_category_new ("hiprtc", 0, "hiprtc");
      });

  return cat;
}
#endif

gchar *
gst_hip_rtc_compile (GstHipDevice * device,
    const gchar * source, const gchar ** options, guint num_options)
{
  hiprtcProgram prog;
  auto rtc_ret = hiprtcCreateProgram (&prog, source, "program.cpp",
      0, nullptr, nullptr);

  if (rtc_ret != HIPRTC_SUCCESS) {
    GST_ERROR_OBJECT (device, "Couldn't create program, ret: %d", rtc_ret);
    return nullptr;
  }

  guint device_id;
  g_object_get (device, "device-id", &device_id, nullptr);

  hipDeviceProp_t props = { };
  auto hip_ret = hipGetDeviceProperties (&props, device_id);
  if (!gst_hip_result (hip_ret)) {
    GST_ERROR_OBJECT (device, "Couldn't query device property");
    return nullptr;
  }

  rtc_ret = hiprtcCompileProgram (prog, num_options, options);
  if (rtc_ret != HIPRTC_SUCCESS) {
    size_t log_size = 0;
    gchar *err_str = nullptr;
    rtc_ret = hiprtcGetProgramLogSize (prog, &log_size);
    if (rtc_ret == HIPRTC_SUCCESS) {
      err_str = (gchar *) g_malloc0 (log_size);
      err_str[log_size - 1] = '\0';
      hiprtcGetProgramLog (prog, err_str);
    }

    GST_ERROR_OBJECT (device, "Couldn't compile program, ret: %d (%s)",
        rtc_ret, GST_STR_NULL (err_str));
    g_free (err_str);
    return nullptr;
  }

  size_t code_size;
  rtc_ret = hiprtcGetCodeSize (prog, &code_size);
  if (rtc_ret != HIPRTC_SUCCESS) {
    GST_ERROR_OBJECT (device, "Couldn't get code size, ret: %d", rtc_ret);
    return nullptr;
  }

  auto code = (gchar *) g_malloc0 (code_size);
  rtc_ret = hiprtcGetCode (prog, code);

  if (rtc_ret != HIPRTC_SUCCESS) {
    GST_ERROR_OBJECT (device, "Couldn't get code, ret: %d", rtc_ret);
    g_free (code);
    return nullptr;
  }

  hiprtcDestroyProgram (&prog);

  return code;
}
