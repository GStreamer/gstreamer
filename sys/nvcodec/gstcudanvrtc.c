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

#include "gstcudanvrtc.h"

GST_DEBUG_CATEGORY_STATIC (gst_cuda_nvrtc_debug);
#define GST_CAT_DEFAULT gst_cuda_nvrtc_debug

static void
_init_debug (void)
{
  static gsize once_init = 0;

  if (g_once_init_enter (&once_init)) {

    GST_DEBUG_CATEGORY_INIT (gst_cuda_nvrtc_debug, "cudanvrtc", 0,
        "CUDA runtime compiler");
    g_once_init_leave (&once_init, 1);
  }
}

gchar *
gst_cuda_nvrtc_compile (const gchar * source)
{
  nvrtcProgram prog;
  nvrtcResult ret;
  CUresult curet;
  const gchar *opts[] = { "--gpu-architecture=compute_30" };
  gsize ptx_size;
  gchar *ptx = NULL;
  int driverVersion;

  g_return_val_if_fail (source != NULL, FALSE);

  _init_debug ();

  GST_TRACE ("CUDA kernel source \n%s", source);

  curet = CuDriverGetVersion (&driverVersion);
  if (curet != CUDA_SUCCESS) {
    GST_ERROR ("Failed to query CUDA Driver version, ret %d", curet);
    return NULL;
  }

  GST_DEBUG ("CUDA Driver Version %d.%d", driverVersion / 1000,
      (driverVersion % 1000) / 10);

  ret = NvrtcCreateProgram (&prog, source, NULL, 0, NULL, NULL);
  if (ret != NVRTC_SUCCESS) {
    GST_ERROR ("couldn't create nvrtc program, ret %d", ret);
    return NULL;
  }

  /* Starting from CUDA 11, the lowest supported architecture is 5.2 */
  if (driverVersion >= 11000)
    opts[0] = "--gpu-architecture=compute_52";

  ret = NvrtcCompileProgram (prog, 1, opts);
  if (ret != NVRTC_SUCCESS) {
    gsize log_size;

    GST_ERROR ("couldn't compile nvrtc program, ret %d", ret);
    if (NvrtcGetProgramLogSize (prog, &log_size) == NVRTC_SUCCESS &&
        log_size > 0) {
      gchar *compile_log = g_alloca (log_size);
      if (NvrtcGetProgramLog (prog, compile_log) == NVRTC_SUCCESS) {
        GST_ERROR ("nvrtc compile log %s", compile_log);
      }
    }

    goto error;
  }

  ret = NvrtcGetPTXSize (prog, &ptx_size);
  if (ret != NVRTC_SUCCESS) {
    GST_ERROR ("unknown ptx size, ret %d", ret);

    goto error;
  }

  ptx = g_malloc0 (ptx_size);
  ret = NvrtcGetPTX (prog, ptx);
  if (ret != NVRTC_SUCCESS) {
    GST_ERROR ("couldn't get ptx, ret %d", ret);
    g_free (ptx);

    goto error;
  }

  NvrtcDestroyProgram (&prog);

  GST_TRACE ("compiled CUDA PTX %s\n", ptx);

  return ptx;

error:
  NvrtcDestroyProgram (&prog);

  return NULL;
}
