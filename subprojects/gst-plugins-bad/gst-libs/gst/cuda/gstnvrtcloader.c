/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#include "cuda-gst.h"
#include "gstnvrtcloader.h"
#include "gstcudaloader.h"

#include <gmodule.h>

GST_DEBUG_CATEGORY (gst_nvrtcloader_debug);
#define GST_CAT_DEFAULT gst_nvrtcloader_debug

#ifndef G_OS_WIN32
#define NVRTC_LIBNAME "libnvrtc.so"
#else
#define NVRTC_LIBNAME "nvrtc64_%d%d_0.dll"
#endif

#define LOAD_SYMBOL(name,func) G_STMT_START { \
  if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &vtable->func)) { \
    GST_ERROR ("Failed to load '%s' from %s, %s", G_STRINGIFY (name), fname, g_module_error()); \
    goto error; \
  } \
} G_STMT_END;

/* *INDENT-OFF* */
typedef struct _GstNvCodecNvrtcVtahle
{
  gboolean loaded;

  nvrtcResult (*NvrtcCompileProgram) (nvrtcProgram prog, int numOptions,
      const char **options);
  nvrtcResult (*NvrtcCreateProgram) (nvrtcProgram * prog, const char *src,
      const char *name, int numHeaders, const char **headers,
      const char **includeNames);
  nvrtcResult (*NvrtcDestroyProgram) (nvrtcProgram * prog);
  nvrtcResult (*NvrtcGetPTX) (nvrtcProgram prog, char *ptx);
  nvrtcResult (*NvrtcGetPTXSize) (nvrtcProgram prog, size_t * ptxSizeRet);
  nvrtcResult (*NvrtcGetProgramLog) (nvrtcProgram prog, char *log);
  nvrtcResult (*NvrtcGetProgramLogSize) (nvrtcProgram prog,
      size_t * logSizeRet);
} GstNvCodecNvrtcVtahle;
/* *INDENT-ON* */

static GstNvCodecNvrtcVtahle gst_nvrtc_vtable = { 0, };

/**
 * gst_nvrtc_load_library:
 *
 * Loads the nvrtc library.
 *
 * Returns: %TRUE if the library could be loaded, %FALSE otherwise
 *
 * Since: 1.22
 */
gboolean
gst_nvrtc_load_library (void)
{
  GModule *module = NULL;
  gchar *filename = NULL;
  const gchar *filename_env;
  const gchar *fname;
  gint cuda_version;
  GstNvCodecNvrtcVtahle *vtable;
  static gsize debug_initialized = FALSE;

  if (gst_nvrtc_vtable.loaded)
    return TRUE;


  if (g_once_init_enter (&debug_initialized)) {
    GST_DEBUG_CATEGORY_INIT (gst_nvrtcloader_debug, "nvrtcloader", 0,
        "nvrtcloader");

    g_once_init_leave (&debug_initialized, TRUE);
  }

  CuDriverGetVersion (&cuda_version);

  fname = filename_env = g_getenv ("GST_NVCODEC_NVRTC_LIBNAME");
  if (filename_env)
    module = g_module_open (filename_env, G_MODULE_BIND_LAZY);

  if (!module) {
#ifndef G_OS_WIN32
    filename = g_strdup (NVRTC_LIBNAME);
    fname = filename;
    module = g_module_open (filename, G_MODULE_BIND_LAZY);
#else
    /* XXX: On Windows, minor version of nvrtc library might not be exactly
     * same as CUDA library */
    {
      gint cuda_major_version = cuda_version / 1000;
      gint cuda_minor_version = (cuda_version % 1000) / 10;
      gint minor_version;

      for (minor_version = cuda_minor_version; minor_version >= 0;
          minor_version--) {
        g_free (filename);
        filename = g_strdup_printf (NVRTC_LIBNAME, cuda_major_version,
            minor_version);
        fname = filename;

        module = g_module_open (filename, G_MODULE_BIND_LAZY);
        if (module) {
          GST_INFO ("%s is available", filename);
          break;
        }

        GST_DEBUG ("Couldn't open library %s", filename);
      }
    }
#endif
  }

  if (module == NULL) {
    GST_WARNING ("Could not open library %s, %s", filename, g_module_error ());
    g_free (filename);
    return FALSE;
  }

  vtable = &gst_nvrtc_vtable;

  LOAD_SYMBOL (nvrtcCompileProgram, NvrtcCompileProgram);
  LOAD_SYMBOL (nvrtcCreateProgram, NvrtcCreateProgram);
  LOAD_SYMBOL (nvrtcDestroyProgram, NvrtcDestroyProgram);
  LOAD_SYMBOL (nvrtcGetPTX, NvrtcGetPTX);
  LOAD_SYMBOL (nvrtcGetPTXSize, NvrtcGetPTXSize);
  LOAD_SYMBOL (nvrtcGetProgramLog, NvrtcGetProgramLog);
  LOAD_SYMBOL (nvrtcGetProgramLogSize, NvrtcGetProgramLogSize);

  vtable->loaded = TRUE;
  g_free (filename);

  return TRUE;

error:
  g_module_close (module);
  g_free (filename);

  return FALSE;
}

nvrtcResult
NvrtcCompileProgram (nvrtcProgram prog, int numOptions, const char **options)
{
  g_assert (gst_nvrtc_vtable.NvrtcCompileProgram != NULL);

  return gst_nvrtc_vtable.NvrtcCompileProgram (prog, numOptions, options);
}

nvrtcResult
NvrtcCreateProgram (nvrtcProgram * prog, const char *src, const char *name,
    int numHeaders, const char **headers, const char **includeNames)
{
  g_assert (gst_nvrtc_vtable.NvrtcCreateProgram != NULL);

  return gst_nvrtc_vtable.NvrtcCreateProgram (prog, src, name, numHeaders,
      headers, includeNames);
}

nvrtcResult
NvrtcDestroyProgram (nvrtcProgram * prog)
{
  g_assert (gst_nvrtc_vtable.NvrtcDestroyProgram != NULL);

  return gst_nvrtc_vtable.NvrtcDestroyProgram (prog);
}

nvrtcResult
NvrtcGetPTX (nvrtcProgram prog, char *ptx)
{
  g_assert (gst_nvrtc_vtable.NvrtcGetPTX != NULL);

  return gst_nvrtc_vtable.NvrtcGetPTX (prog, ptx);
}

/* *INDENT-OFF* */
nvrtcResult
NvrtcGetPTXSize (nvrtcProgram prog, size_t * ptxSizeRet)
{
  g_assert (gst_nvrtc_vtable.NvrtcGetPTXSize != NULL);

  return gst_nvrtc_vtable.NvrtcGetPTXSize (prog, ptxSizeRet);
}
/* *INDENT-ON* */

nvrtcResult
NvrtcGetProgramLog (nvrtcProgram prog, char *log)
{
  g_assert (gst_nvrtc_vtable.NvrtcGetProgramLog != NULL);

  return gst_nvrtc_vtable.NvrtcGetProgramLog (prog, log);
}

/* *INDENT-OFF* */
nvrtcResult
NvrtcGetProgramLogSize (nvrtcProgram prog, size_t * logSizeRet)
{
  g_assert (gst_nvrtc_vtable.NvrtcGetProgramLogSize != NULL);

  return gst_nvrtc_vtable.NvrtcGetProgramLogSize (prog, logSizeRet);
}
/* *INDENT-ON* */
