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
#include <gmodule.h>
#include <string.h>

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

#define LOAD_SYMBOL(name) G_STMT_START { \
  if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &table->name)) { \
    GST_ERROR ("Failed to load '%s', %s", G_STRINGIFY (name), g_module_error()); \
    g_module_close (module); \
    return; \
  } \
} G_STMT_END;

/* *INDENT-OFF* */
struct GstHipRtcFuncTableAmd
{
  gboolean loaded = FALSE;

  hiprtcResult (*hiprtcCreateProgram) (hiprtcProgram * prog,
    const char *src,
    const char *name,
    int numHeaders, const char **headers, const char **includeNames);
  hiprtcResult (*hiprtcCompileProgram) (hiprtcProgram prog,
    int numOptions, const char **options);
  hiprtcResult (*hiprtcGetProgramLog) (hiprtcProgram prog, char *log);
  hiprtcResult (*hiprtcGetProgramLogSize) (hiprtcProgram prog,
    size_t *logSizeRet);
  hiprtcResult (*hiprtcGetCodeSize) (hiprtcProgram prog, size_t *codeSizeRet);
  hiprtcResult (*hiprtcGetCode) (hiprtcProgram prog, char *code);
  hiprtcResult (*hiprtcDestroyProgram) (hiprtcProgram * prog);
};
/* *INDENT-ON* */

static GstHipRtcFuncTableAmd amd_ftable = { };

static void
load_rtc_amd_func_table (void)
{
  GModule *module = nullptr;
#ifndef G_OS_WIN32
  module = g_module_open ("libhiprtc.so", G_MODULE_BIND_LAZY);
  if (!module)
    module = g_module_open ("/opt/rocm/lib/libhiprtc.so", G_MODULE_BIND_LAZY);
#else
  /* Prefer hip dll in SDK */
  auto hip_root = g_getenv ("HIP_PATH");
  if (hip_root) {
    auto path = g_build_path (G_DIR_SEPARATOR_S, hip_root, "bin", nullptr);
    auto dir = g_dir_open (path, 0, nullptr);
    if (dir) {
      const gchar *name;
      while ((name = g_dir_read_name (dir))) {
        if (g_str_has_prefix (name, "hiprtc") && g_str_has_suffix (name,
                ".dll") && !strstr (name, "builtins")) {
          auto lib_path = g_build_filename (path, name, nullptr);
          module = g_module_open (lib_path, G_MODULE_BIND_LAZY);
          break;
        }
      }

      g_dir_close (dir);
    }
    g_free (path);
  }
#endif

  if (!module) {
    GST_INFO ("Couldn't open HIP RTC library");
    return;
  }

  auto table = &amd_ftable;
  LOAD_SYMBOL (hiprtcCreateProgram);
  LOAD_SYMBOL (hiprtcCompileProgram);
  LOAD_SYMBOL (hiprtcGetProgramLog);
  LOAD_SYMBOL (hiprtcGetProgramLogSize);
  LOAD_SYMBOL (hiprtcGetCodeSize);
  LOAD_SYMBOL (hiprtcGetCode);
  LOAD_SYMBOL (hiprtcDestroyProgram);

  table->loaded = TRUE;
}

gboolean
gst_hip_rtc_load_library (void)
{
  static std::once_flag once;
  std::call_once (once,[]() {
        load_rtc_amd_func_table ();
      });

  return amd_ftable.loaded;
}

gchar *
gst_hip_rtc_compile (GstHipDevice * device,
    const gchar * source, const gchar ** options, guint num_options)
{
  if (!gst_hip_rtc_load_library ())
    return nullptr;

  hiprtcProgram prog;
  auto rtc_ret = amd_ftable.hiprtcCreateProgram (&prog, source, "program.cpp",
      0, nullptr, nullptr);

  if (rtc_ret != HIPRTC_SUCCESS) {
    GST_ERROR_OBJECT (device, "Couldn't create program, ret: %d", rtc_ret);
    return nullptr;
  }

  guint device_id;
  g_object_get (device, "device-id", &device_id, nullptr);

  rtc_ret = amd_ftable.hiprtcCompileProgram (prog, num_options, options);
  if (rtc_ret != HIPRTC_SUCCESS) {
    size_t log_size = 0;
    gchar *err_str = nullptr;
    rtc_ret = amd_ftable.hiprtcGetProgramLogSize (prog, &log_size);
    if (rtc_ret == HIPRTC_SUCCESS) {
      err_str = (gchar *) g_malloc0 (log_size);
      err_str[log_size - 1] = '\0';
      amd_ftable.hiprtcGetProgramLog (prog, err_str);
    }

    GST_ERROR_OBJECT (device, "Couldn't compile program, ret: %d (%s)",
        rtc_ret, GST_STR_NULL (err_str));
    g_free (err_str);
    return nullptr;
  }

  size_t code_size;
  rtc_ret = amd_ftable.hiprtcGetCodeSize (prog, &code_size);
  if (rtc_ret != HIPRTC_SUCCESS) {
    GST_ERROR_OBJECT (device, "Couldn't get code size, ret: %d", rtc_ret);
    return nullptr;
  }

  auto code = (gchar *) g_malloc0 (code_size);
  rtc_ret = amd_ftable.hiprtcGetCode (prog, code);

  if (rtc_ret != HIPRTC_SUCCESS) {
    GST_ERROR_OBJECT (device, "Couldn't get code, ret: %d", rtc_ret);
    g_free (code);
    return nullptr;
  }

  amd_ftable.hiprtcDestroyProgram (&prog);

  return code;
}
