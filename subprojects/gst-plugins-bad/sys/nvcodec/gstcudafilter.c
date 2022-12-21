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

#include <gst/cuda/gstcuda.h>

#include "gstcudafilter.h"
#include "gstcudaconvertscale.h"

/* *INDENT-OFF* */
const gchar *nvrtc_test_source =
    "__global__ void\n"
    "my_kernel (void) {}";
/* *INDENT-ON* */

void
gst_cuda_filter_plugin_init (GstPlugin * plugin)
{
  gchar *test_ptx = NULL;

  if (!gst_cuda_nvrtc_load_library ())
    return;

  test_ptx = gst_cuda_nvrtc_compile (nvrtc_test_source);

  if (!test_ptx) {
    return;
  }
  g_free (test_ptx);

  gst_element_register (plugin, "cudaconvert", GST_RANK_NONE,
      GST_TYPE_CUDA_CONVERT);
  gst_element_register (plugin, "cudascale", GST_RANK_NONE,
      GST_TYPE_CUDA_SCALE);
  gst_element_register (plugin, "cudaconvertscale", GST_RANK_NONE,
      GST_TYPE_CUDA_CONVERT_SCALE);
}
