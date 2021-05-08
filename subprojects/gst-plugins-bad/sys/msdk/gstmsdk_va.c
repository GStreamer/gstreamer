/* GStreamer Intel MSDK plugin
 * Copyright (c) 2021, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGDECE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _WIN32
#include "gstmsdk_va.h"

#define GST_MAP_VA (GST_MAP_FLAG_LAST << 1)

static GQuark
gst_msdk_va_memory_surface_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("GstMsdkMemoryVASurface");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

static gboolean
gst_msdk_is_va_mem (GstMemory * mem)
{
  GstAllocator *allocator;

  allocator = mem->allocator;
  if (!allocator)
    return FALSE;

  return g_str_equal (allocator->mem_type, "VAMemory");
}

VASurfaceID
gst_msdk_va_peek_buffer_surface (GstBuffer * buffer)
{
  GstMemory *mem;
  VASurfaceID surface;
  GstMapInfo map_info;
  gpointer data;

  g_return_val_if_fail (buffer, VA_INVALID_SURFACE);

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!mem)
    return VA_INVALID_SURFACE;

  if (!gst_msdk_is_va_mem (mem))
    return VA_INVALID_SURFACE;

  data = gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
      gst_msdk_va_memory_surface_quark_get ());
  if (data) {
    surface = *(VASurfaceID *) data;
    g_assert (surface != VA_INVALID_SURFACE);
    return surface;
  }

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ | GST_MAP_VA))
    return VA_INVALID_SURFACE;

  surface = (*(VASurfaceID *) map_info.data);
  gst_buffer_unmap (buffer, &map_info);

  if (surface == VA_INVALID_ID) {
    GST_WARNING ("Fail to get va surface by GST_MAP_VA mapping");
  } else {
    data = g_new (VASurfaceID, 1);
    *(VASurfaceID *) data = surface;
    gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (mem),
        gst_msdk_va_memory_surface_quark_get (), data, g_free);
  }

  return surface;
}

#endif /* _WIN32 */
