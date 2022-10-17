/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel Corporation
 * Copyright (c) 2018, Igalia S.L.
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

#include <gst/d3d11/gstd3d11.h>
#include "gstmsdkallocator.h"

#define GST_MSDK_FRAME_SURFACE gst_msdk_frame_surface_quark_get ()
static GQuark
gst_msdk_frame_surface_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("GstMsdkFrameSurface");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

mfxStatus
gst_msdk_frame_alloc (mfxHDL pthis, mfxFrameAllocRequest * req,
    mfxFrameAllocResponse * resp)
{
  return MFX_ERR_NONE;
}

mfxStatus
gst_msdk_frame_free (mfxHDL pthis, mfxFrameAllocResponse * resp)
{
  return MFX_ERR_NONE;
}

mfxStatus
gst_msdk_frame_lock (mfxHDL pthis, mfxMemId mid, mfxFrameData * data)
{
  return MFX_ERR_NONE;
}

mfxStatus
gst_msdk_frame_unlock (mfxHDL pthis, mfxMemId mid, mfxFrameData * ptr)
{
  return MFX_ERR_NONE;
}

mfxStatus
gst_msdk_frame_get_hdl (mfxHDL pthis, mfxMemId mid, mfxHDL * hdl)
{
  GstMsdkMemoryID *mem_id;
  mfxHDLPair *pair;

  if (!hdl || !mid)
    return MFX_ERR_INVALID_HANDLE;

  mem_id = (GstMsdkMemoryID *) mid;
  pair = (mfxHDLPair *) hdl;
  pair->first = (mfxHDL) mem_id->texture;
  pair->second = (mfxHDL) GUINT_TO_POINTER (mem_id->subresource_index);

  return MFX_ERR_NONE;
}

GstMsdkSurface *
gst_msdk_import_to_msdk_surface (GstBuffer * buf, GstMsdkContext * msdk_context,
    GstVideoInfo * vinfo, guint map_flag)
{
  GstMemory *mem = NULL;
  mfxFrameInfo frame_info = { 0, };
  GstMsdkSurface *msdk_surface = NULL;
  mfxFrameSurface1 *mfx_surface = NULL;
  GstMsdkMemoryID *msdk_mid = NULL;
  GstMapInfo map_info;

  mem = gst_buffer_peek_memory (buf, 0);
  msdk_surface = g_slice_new0 (GstMsdkSurface);

  if (!gst_is_d3d11_memory (mem) || gst_buffer_n_memory (buf) > 1) {
    /* d3d11 buffer should hold single memory object */
    g_slice_free (GstMsdkSurface, msdk_surface);
    return NULL;
  }

  if (!gst_buffer_map (buf, &map_info, map_flag | GST_MAP_D3D11)) {
    GST_ERROR ("Failed to map buffer");
    g_slice_free (GstMsdkSurface, msdk_surface);
    return NULL;
  }

  /* If buffer has qdata pointing to mfxFrameSurface1, directly extract it */
  if ((mfx_surface = gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (mem),
              GST_MSDK_FRAME_SURFACE))) {
    msdk_surface->from_qdata = TRUE;
    msdk_surface->surface = mfx_surface;
    gst_buffer_unmap (buf, &map_info);
    return msdk_surface;
  }

  mfx_surface = g_slice_new0 (mfxFrameSurface1);
  msdk_mid = g_slice_new0 (GstMsdkMemoryID);
  mfx_surface->Data.MemId = (mfxMemId) msdk_mid;

  msdk_mid->texture = (ID3D11Texture2D *) (gpointer) map_info.data;
  msdk_mid->subresource_index = GPOINTER_TO_UINT (map_info.user_data[0]);

  gst_buffer_unmap (buf, &map_info);

  gst_msdk_set_mfx_frame_info_from_video_info (&frame_info, vinfo);
  mfx_surface->Info = frame_info;

  /* Set mfxFrameSurface1 as qdata in buffer */
  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (mem),
      GST_MSDK_FRAME_SURFACE, mfx_surface, NULL);

  msdk_surface->surface = mfx_surface;

  return msdk_surface;
}

void
gst_msdk_set_frame_allocator (GstMsdkContext * context)
{
  mfxFrameAllocator gst_msdk_frame_allocator = {
    .pthis = context,
    .Alloc = gst_msdk_frame_alloc,
    .Lock = gst_msdk_frame_lock,
    .Unlock = gst_msdk_frame_unlock,
    .GetHDL = gst_msdk_frame_get_hdl,
    .Free = gst_msdk_frame_free,
  };

  gst_msdk_context_set_frame_allocator (context, &gst_msdk_frame_allocator);
}
