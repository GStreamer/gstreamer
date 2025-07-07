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

#include "gsthip.h"
#include <hip/hiprtc.h>
#include <mutex>
#include <vector>
#include <string>
#include <gmodule.h>
#include <string.h>
#include "gsthiputils-private.h"

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

typedef gpointer nvrtcProgram;

typedef enum {
  NVRTC_SUCCESS = 0,
} nvrtcResult;


struct GstHipRtcFuncTableNvidia
{
  gboolean loaded = FALSE;

  nvrtcResult (*nvrtcCompileProgram) (nvrtcProgram prog, int numOptions,
      const char **options);
  nvrtcResult (*nvrtcCreateProgram) (nvrtcProgram * prog, const char *src,
      const char *name, int numHeaders, const char **headers,
      const char **includeNames);
  nvrtcResult (*nvrtcDestroyProgram) (nvrtcProgram * prog);
  nvrtcResult (*nvrtcGetPTX) (nvrtcProgram prog, char *ptx);
  nvrtcResult (*nvrtcGetPTXSize) (nvrtcProgram prog, size_t * ptxSizeRet);
  nvrtcResult (*nvrtcGetProgramLog) (nvrtcProgram prog, char *log);
  nvrtcResult (*nvrtcGetProgramLogSize) (nvrtcProgram prog,
      size_t * logSizeRet);
};

/* *INDENT-ON* */

static GstHipRtcFuncTableAmd amd_ftable = { };
static GstHipRtcFuncTableNvidia nvidia_ftable = { };

static void
load_rtc_amd_func_table (void)
{
  GModule *module = nullptr;
  auto module_name = g_getenv ("GST_HIP_HIPRTC_LIBNAME");
  if (module_name)
    module = g_module_open (module_name, G_MODULE_BIND_LAZY);

  if (!module) {
#ifndef G_OS_WIN32
    // Keep this logic in sync with gsthiploader.cpp to ensure that the order
    // of searching is the same, and both libs are loaded from the same place
    module = g_module_open ("libhiprtc.so.7", G_MODULE_BIND_LAZY);
    if (module) {
      GST_INFO ("Loaded libhiprtc.so.7");
    } else {
      module = g_module_open ("libhiprtc.so.6", G_MODULE_BIND_LAZY);
      if (module)
        GST_INFO ("Loaded libhiprtc.so.6");
    }

    if (!module)
      module = load_hiplib_from_root ("/opt/rocm", "lib", "libhiprtc.so.", "");
#else
    int version = 0;
    auto hip_ret = HipRuntimeGetVersion (GST_HIP_VENDOR_AMD, &version);
    if (hip_ret != hipSuccess)
      return;

    int major = version / 10000000;
    int minor = (version - (major * 10000000)) / 100000;
    auto lib_name = g_strdup_printf ("hiprtc%02d%02d.dll", major, minor);
    /* Prefer hip dll in SDK */
    auto hip_root = g_getenv ("HIP_PATH");
    if (hip_root) {
      auto lib_path = g_build_filename (hip_root, "bin", lib_name, nullptr);
      module = g_module_open (lib_path, G_MODULE_BIND_LAZY);
      g_free (lib_path);
    }

    if (!module)
      module = g_module_open (lib_name, G_MODULE_BIND_LAZY);

    g_free (lib_name);
#endif
  }

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

/* *INDENT-OFF* */
static gboolean
gst_hip_rtc_load_library_amd (void)
{
  static std::once_flag once;
  std::call_once (once,[]() {
    if (!gst_hip_load_library (GST_HIP_VENDOR_AMD))
      return;

    load_rtc_amd_func_table ();
  });

  return amd_ftable.loaded;
}
/* *INDENT-ON* */

static void
load_rtc_nvidia_func_table (void)
{
  GModule *module = nullptr;
  auto module_name = g_getenv ("GST_HIP_NVRTC_LIBNAME");
  if (module_name)
    module = g_module_open (module_name, G_MODULE_BIND_LAZY);

  if (!module) {
#ifndef G_OS_WIN32
    module = g_module_open ("libnvrtc.so", G_MODULE_BIND_LAZY);
#else
    int version = 0;
    auto hip_ret = HipDriverGetVersion (GST_HIP_VENDOR_NVIDIA, &version);
    if (hip_ret != hipSuccess)
      return;

    int major = version / 1000;
    int minor = (version % 1000) / 10;
    auto lib_name = g_strdup_printf ("nvrtc64_%d%d_0.dll", major, minor);
    module = g_module_open (lib_name, G_MODULE_BIND_LAZY);
    g_free (lib_name);

    if (!module) {
      lib_name = g_strdup_printf ("nvrtc64_%d0_0.dll", major);
      module = g_module_open (lib_name, G_MODULE_BIND_LAZY);
      g_free (lib_name);
    }

    if (!module) {
      auto cuda_root = g_getenv ("CUDA_PATH");
      if (cuda_root) {
        auto path = g_build_path (G_DIR_SEPARATOR_S, cuda_root, "bin", nullptr);
        auto dir = g_dir_open (path, 0, nullptr);
        if (dir) {
          const gchar *name;
          while ((name = g_dir_read_name (dir))) {
            if (g_str_has_prefix (name, "nvrtc64_") &&
                g_str_has_suffix (name, "_0.dll")) {
              auto lib_path = g_build_filename (path, name, nullptr);
              module = g_module_open (lib_path, G_MODULE_BIND_LAZY);
              g_free (lib_path);
              break;
            }
          }

          g_dir_close (dir);
        }
        g_free (path);
      }
    }
#endif
  }

  if (!module) {
    GST_INFO ("Couldn't open NVRTC library");
    return;
  }

  auto table = &nvidia_ftable;
  LOAD_SYMBOL (nvrtcCompileProgram);
  LOAD_SYMBOL (nvrtcCreateProgram);
  LOAD_SYMBOL (nvrtcDestroyProgram);
  LOAD_SYMBOL (nvrtcGetPTX);
  LOAD_SYMBOL (nvrtcGetPTXSize);
  LOAD_SYMBOL (nvrtcGetProgramLog);
  LOAD_SYMBOL (nvrtcGetProgramLogSize);

  table->loaded = TRUE;
}

/* *INDENT-OFF* */
static gboolean
gst_hip_rtc_load_library_nvidia (void)
{
  static std::once_flag once;
  std::call_once (once,[]() {
    if (!gst_hip_load_library (GST_HIP_VENDOR_NVIDIA))
      return;

    load_rtc_nvidia_func_table ();
  });

  return nvidia_ftable.loaded;
}
/* *INDENT-ON* */

/**
 * gst_hip_rtc_load_library:
 * @vendor: a #GstHipVendor
 *
 * Opens @vendor specific runtime compiler libraries
 *
 * Returns: %TRUE if succeeded
 *
 * Since: 1.28
 */
gboolean
gst_hip_rtc_load_library (GstHipVendor vendor)
{
  switch (vendor) {
    case GST_HIP_VENDOR_AMD:
      return gst_hip_rtc_load_library_amd ();
    case GST_HIP_VENDOR_NVIDIA:
      return gst_hip_rtc_load_library_nvidia ();
    case GST_HIP_VENDOR_UNKNOWN:
      if (gst_hip_rtc_load_library_amd () || gst_hip_rtc_load_library_nvidia ())
        return TRUE;
      break;
  }

  return FALSE;
}

static gchar *
gst_hip_rtc_compile_amd (GstHipDevice * device,
    const gchar * source, const gchar ** options, guint num_options)
{
  hiprtcProgram prog;
  auto rtc_ret = amd_ftable.hiprtcCreateProgram (&prog, source, "program.cpp",
      0, nullptr, nullptr);

  if (rtc_ret != HIPRTC_SUCCESS) {
    GST_ERROR_OBJECT (device, "Couldn't create program, ret: %d", rtc_ret);
    return nullptr;
  }

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

static gchar *
gst_hip_rtc_compile_nvidia (GstHipDevice * device,
    const gchar * source, const gchar ** options, guint num_options)
{
  nvrtcProgram prog;
  auto rtc_ret = nvidia_ftable.nvrtcCreateProgram (&prog, source, "program.cpp",
      0, nullptr, nullptr);

  if (rtc_ret != NVRTC_SUCCESS) {
    GST_ERROR_OBJECT (device, "Couldn't create program, ret: %d", rtc_ret);
    return nullptr;
  }

  rtc_ret = nvidia_ftable.nvrtcCompileProgram (prog, num_options, options);
  if (rtc_ret != NVRTC_SUCCESS) {
    size_t log_size = 0;
    gchar *err_str = nullptr;
    rtc_ret = nvidia_ftable.nvrtcGetProgramLogSize (prog, &log_size);
    if (rtc_ret == NVRTC_SUCCESS) {
      err_str = (gchar *) g_malloc0 (log_size);
      err_str[log_size - 1] = '\0';
      nvidia_ftable.nvrtcGetProgramLog (prog, err_str);
    }

    GST_ERROR_OBJECT (device, "Couldn't compile program, ret: %d (%s)",
        rtc_ret, GST_STR_NULL (err_str));
    g_free (err_str);
    return nullptr;
  }

  size_t code_size;
  rtc_ret = nvidia_ftable.nvrtcGetPTXSize (prog, &code_size);
  if (rtc_ret != NVRTC_SUCCESS) {
    GST_ERROR_OBJECT (device, "Couldn't get code size, ret: %d", rtc_ret);
    return nullptr;
  }

  auto code = (gchar *) g_malloc0 (code_size);
  rtc_ret = nvidia_ftable.nvrtcGetPTX (prog, code);

  if (rtc_ret != NVRTC_SUCCESS) {
    GST_ERROR_OBJECT (device, "Couldn't get code, ret: %d", rtc_ret);
    g_free (code);
    return nullptr;
  }

  nvidia_ftable.nvrtcDestroyProgram (&prog);

  return code;
}

/**
 * gst_hip_rtc_compile:
 * @device: a #GstHipDevice
 * @source: HIP kernel source
 * @options: array of compile option string
 * @num_options: option array size
 *
 * Compiles @source with given compile options
 *
 * Returns: (transfer full) (nullable): Compiled kernel blob or %NULL if failed.
 *  *
 * Since: 1.28
 */
gchar *
gst_hip_rtc_compile (GstHipDevice * device,
    const gchar * source, const gchar ** options, guint num_options)
{
  auto vendor = gst_hip_device_get_vendor (device);
  if (!gst_hip_rtc_load_library (vendor))
    return nullptr;

  switch (vendor) {
    case GST_HIP_VENDOR_AMD:
      return gst_hip_rtc_compile_amd (device, source, options, num_options);
    case GST_HIP_VENDOR_NVIDIA:
      return gst_hip_rtc_compile_nvidia (device, source, options, num_options);
    default:
      break;
  }

  return nullptr;
}
