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

#include <va/va.h>
#include <va/va_drmcommon.h>
#include "gstmsdkallocator.h"
#include "gstmsdkallocator_libva.h"
#include "msdk_libva.h"

mfxStatus
gst_msdk_frame_alloc (mfxHDL pthis, mfxFrameAllocRequest * req,
    mfxFrameAllocResponse * resp)
{
  VAStatus va_status;
  mfxStatus status = MFX_ERR_NONE;
  gint i;
  guint format;
  guint va_fourcc = 0;
  VASurfaceID *surfaces = NULL;
  VASurfaceAttrib attrib;
  mfxMemId *mids = NULL;
  GstMsdkContext *context = (GstMsdkContext *) pthis;
  GstMsdkMemoryID *msdk_mids = NULL;
  GstMsdkAllocResponse *msdk_resp = NULL;
  mfxU32 fourcc = req->Info.FourCC;
  mfxU16 surfaces_num = req->NumFrameSuggested;

  if (req->Type & MFX_MEMTYPE_EXTERNAL_FRAME) {
    GstMsdkAllocResponse *cached =
        gst_msdk_context_get_cached_alloc_responses_by_request (context, req);
    if (cached) {
      /* check if enough frames were allocated */
      if (req->NumFrameSuggested > cached->response->NumFrameActual)
        return MFX_ERR_MEMORY_ALLOC;

      *resp = *cached->response;
      return MFX_ERR_NONE;
    }
  }

  /* The VA API does not define any surface types and the application can use either
   * MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET or
   * MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET to indicate data in video memory.
   */
  if (!(req->Type & (MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET |
              MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET)))
    return MFX_ERR_UNSUPPORTED;

  va_fourcc = gst_msdk_get_va_fourcc_from_mfx_fourcc (fourcc);

  msdk_mids =
      (GstMsdkMemoryID *) g_slice_alloc0 (surfaces_num *
      sizeof (GstMsdkMemoryID));
  mids = (mfxMemId *) g_slice_alloc0 (surfaces_num * sizeof (mfxMemId));
  surfaces =
      (VASurfaceID *) g_slice_alloc0 (surfaces_num * sizeof (VASurfaceID));
  msdk_resp =
      (GstMsdkAllocResponse *) g_slice_alloc0 (sizeof (GstMsdkAllocResponse));

  if (va_fourcc != VA_FOURCC_P208) {
    attrib.type = VASurfaceAttribPixelFormat;
    attrib.flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrib.value.type = VAGenericValueTypeInteger;
    attrib.value.value.i = va_fourcc;

    format =
        gst_msdk_get_va_rt_format_from_mfx_rt_format (req->Info.ChromaFormat);

    if (format == VA_RT_FORMAT_YUV420 && va_fourcc == VA_FOURCC_P010)
      format = VA_RT_FORMAT_YUV420_10;

    va_status = vaCreateSurfaces (gst_msdk_context_get_handle (context),
        format,
        req->Info.Width, req->Info.Height, surfaces, surfaces_num, &attrib, 1);

    status = gst_msdk_get_mfx_status_from_va_status (va_status);
    if (status != MFX_ERR_NONE) {
      GST_WARNING ("failed to create VA surface");
      return status;
    }

    for (i = 0; i < surfaces_num; i++) {
      /* Get dmabuf handle if MFX_MEMTYPE_EXPORT_FRAME */
      if (req->Type & MFX_MEMTYPE_EXPORT_FRAME) {
        msdk_mids[i].info.mem_type = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
        va_status =
            vaDeriveImage (gst_msdk_context_get_handle (context), surfaces[i],
            &msdk_mids[i].image);
        status = gst_msdk_get_mfx_status_from_va_status (va_status);

        if (MFX_ERR_NONE != status) {
          GST_ERROR ("failed to derive image");
          return status;
        }

        va_status =
            vaAcquireBufferHandle (gst_msdk_context_get_handle (context),
            msdk_mids[i].image.buf, &msdk_mids[i].info);
        status = gst_msdk_get_mfx_status_from_va_status (va_status);

        if (MFX_ERR_NONE != status) {
          GST_ERROR ("failed to get dmabuf handle");
          va_status = vaDestroyImage (gst_msdk_context_get_handle (context),
              msdk_mids[i].image.image_id);
          if (va_status == VA_STATUS_SUCCESS) {
            msdk_mids[i].image.image_id = VA_INVALID_ID;
            msdk_mids[i].image.buf = VA_INVALID_ID;
          }
        }
      } else {
        /* useful to check the image mapping state later */
        msdk_mids[i].image.image_id = VA_INVALID_ID;
        msdk_mids[i].image.buf = VA_INVALID_ID;
      }

      msdk_mids[i].surface = &surfaces[i];
      mids[i] = (mfxMemId *) & msdk_mids[i];
    }
  } else {
    /* This is requested from the driver when h265 encoding.
     * These buffers will be used inside the driver and released by
     * gst_msdk_frame_free functions. Application doesn't need to handle these buffers.
     *
     * See https://github.com/Intel-Media-SDK/samples/issues/13 for more details.
     */
    VAContextID context_id = req->AllocId;
    gint width32 = 32 * ((req->Info.Width + 31) >> 5);
    gint height32 = 32 * ((req->Info.Height + 31) >> 5);
    guint64 codedbuf_size = (width32 * height32) * 400LL / (16 * 16);

    for (i = 0; i < surfaces_num; i++) {
      VABufferID coded_buf;

      va_status = vaCreateBuffer (gst_msdk_context_get_handle (context),
          context_id, VAEncCodedBufferType, codedbuf_size, 1, NULL, &coded_buf);

      status = gst_msdk_get_mfx_status_from_va_status (va_status);
      if (status < MFX_ERR_NONE) {
        GST_ERROR ("failed to create buffer");
        return status;
      }

      surfaces[i] = coded_buf;
      msdk_mids[i].surface = &surfaces[i];
      msdk_mids[i].fourcc = fourcc;
      mids[i] = (mfxMemId *) & msdk_mids[i];
    }
  }

  resp->mids = mids;
  resp->NumFrameActual = surfaces_num;

  msdk_resp->response = resp;
  msdk_resp->mem_ids = mids;
  msdk_resp->request = *req;

  gst_msdk_context_add_alloc_response (context, msdk_resp);

  return status;
}

mfxStatus
gst_msdk_frame_free (mfxHDL pthis, mfxFrameAllocResponse * resp)
{
  GstMsdkContext *context = (GstMsdkContext *) pthis;
  VAStatus va_status = VA_STATUS_SUCCESS;
  mfxStatus status;
  GstMsdkMemoryID *mem_id;
  VADisplay dpy;
  gint i;

  if (!gst_msdk_context_remove_alloc_response (context, resp))
    return MFX_ERR_NONE;

  mem_id = resp->mids[0];
  dpy = gst_msdk_context_get_handle (context);

  if (mem_id->fourcc != MFX_FOURCC_P8) {
    /* Make sure that all the vaImages are destroyed */
    for (i = 0; i < resp->NumFrameActual; i++) {
      GstMsdkMemoryID *mem = resp->mids[i];

      /* Release dmabuf handle if used */
      if (mem->info.mem_type == VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME)
        vaReleaseBufferHandle (dpy, mem->image.buf);

      if (mem->image.image_id != VA_INVALID_ID &&
          vaDestroyImage (dpy, mem->image.image_id) == VA_STATUS_SUCCESS) {
        mem_id->image.image_id = VA_INVALID_ID;
        mem_id->image.buf = VA_INVALID_ID;
      }
    }

    va_status =
        vaDestroySurfaces (dpy, (VASurfaceID *) mem_id->surface,
        resp->NumFrameActual);
  } else {
    VASurfaceID *surfaces = mem_id->surface;

    for (i = 0; i < resp->NumFrameActual; i++) {
      va_status = vaDestroyBuffer (dpy, surfaces[i]);
    }
  }

  g_slice_free1 (resp->NumFrameActual * sizeof (VASurfaceID), mem_id->surface);
  g_slice_free1 (resp->NumFrameActual * sizeof (GstMsdkMemoryID), mem_id);
  g_slice_free1 (resp->NumFrameActual * sizeof (mfxMemId), resp->mids);

  status = gst_msdk_get_mfx_status_from_va_status (va_status);
  return status;
}

mfxStatus
gst_msdk_frame_lock (mfxHDL pthis, mfxMemId mid, mfxFrameData * data)
{
  GstMsdkContext *context = (GstMsdkContext *) pthis;
  VAStatus va_status;
  mfxStatus status;
  mfxU8 *buf = NULL;
  VASurfaceID *va_surface;
  VADisplay dpy;
  GstMsdkMemoryID *mem_id;

  mem_id = (GstMsdkMemoryID *) mid;
  va_surface = mem_id->surface;
  dpy = gst_msdk_context_get_handle (context);

  if (mem_id->info.mem_type == VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME) {
    GST_WARNING ("Couldn't map the buffer since dmabuf is already in use");
    return MFX_ERR_LOCK_MEMORY;
  }

  if (mem_id->fourcc != MFX_FOURCC_P8) {
    va_status = vaDeriveImage (dpy, *va_surface, &mem_id->image);
    status = gst_msdk_get_mfx_status_from_va_status (va_status);

    if (status != MFX_ERR_NONE) {
      GST_WARNING ("failed to derive image");
      return status;
    }

    va_status = vaMapBuffer (dpy, mem_id->image.buf, (void **) &buf);
    status = gst_msdk_get_mfx_status_from_va_status (va_status);

    if (status != MFX_ERR_NONE) {
      GST_WARNING ("failed to map");
      if (vaDestroyImage (dpy, mem_id->image.image_id) == VA_STATUS_SUCCESS) {
        mem_id->image.image_id = VA_INVALID_ID;
        mem_id->image.buf = VA_INVALID_ID;
      }
      return status;
    }

    switch (mem_id->image.format.fourcc) {
      case VA_FOURCC_NV12:
      case VA_FOURCC_P010:
        data->Pitch = mem_id->image.pitches[0];
        data->Y = buf + mem_id->image.offsets[0];
        data->UV = buf + mem_id->image.offsets[1];
        break;
      case VA_FOURCC_YV12:
        data->Pitch = mem_id->image.pitches[0];
        data->Y = buf + mem_id->image.offsets[0];
        data->U = buf + mem_id->image.offsets[2];
        data->V = buf + mem_id->image.offsets[1];
        break;
      case VA_FOURCC_YUY2:
        data->Pitch = mem_id->image.pitches[0];
        data->Y = buf + mem_id->image.offsets[0];
        data->U = data->Y + 1;
        data->V = data->Y + 3;
        break;
      case VA_FOURCC_UYVY:
        data->Pitch = mem_id->image.pitches[0];
        data->Y = buf + mem_id->image.offsets[0];
        data->U = data->U + 1;
        data->V = data->U + 2;
        break;
      case VA_FOURCC_ARGB:
        data->Pitch = mem_id->image.pitches[0];
        data->R = buf + mem_id->image.offsets[0];
        data->G = data->R + 1;
        data->B = data->R + 2;
        data->A = data->R + 3;
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  } else {
    VACodedBufferSegment *coded_buffer_segment;
    va_status =
        vaMapBuffer (dpy, *va_surface, (void **) (&coded_buffer_segment));
    status = gst_msdk_get_mfx_status_from_va_status (va_status);
    if (MFX_ERR_NONE == status)
      data->Y = (mfxU8 *) coded_buffer_segment->buf;
  }

  return status;
}

mfxStatus
gst_msdk_frame_unlock (mfxHDL pthis, mfxMemId mid, mfxFrameData * ptr)
{
  GstMsdkContext *context = (GstMsdkContext *) pthis;
  VAStatus va_status;
  mfxStatus status;
  VADisplay dpy;
  GstMsdkMemoryID *mem_id;

  mem_id = (GstMsdkMemoryID *) mid;
  dpy = gst_msdk_context_get_handle (context);

  if (mem_id->fourcc != MFX_FOURCC_P8) {
    vaUnmapBuffer (dpy, mem_id->image.buf);
    va_status = vaDestroyImage (dpy, mem_id->image.image_id);

    if (va_status == VA_STATUS_SUCCESS) {
      mem_id->image.image_id = VA_INVALID_ID;
      mem_id->image.buf = VA_INVALID_ID;
    }
  } else {
    va_status = vaUnmapBuffer (dpy, *(mem_id->surface));
  }

  status = gst_msdk_get_mfx_status_from_va_status (va_status);

  return status;
}

mfxStatus
gst_msdk_frame_get_hdl (mfxHDL pthis, mfxMemId mid, mfxHDL * hdl)
{
  GstMsdkMemoryID *mem_id;

  if (!hdl || !mid)
    return MFX_ERR_INVALID_HANDLE;

  mem_id = mid;
  *hdl = mem_id->surface;

  return MFX_ERR_NONE;
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

gboolean
gst_msdk_get_dmabuf_info_from_surface (mfxFrameSurface1 * surface,
    gint * handle, gsize * size)
{
  GstMsdkMemoryID *mem_id;
  g_return_val_if_fail (surface, FALSE);

  mem_id = (GstMsdkMemoryID *) surface->Data.MemId;
  if (handle)
    *handle = mem_id->info.handle;
  if (size)
    *size = mem_id->info.mem_size;

  return TRUE;
}

gboolean
gst_msdk_export_dmabuf_to_vasurface (GstMsdkContext * context,
    GstVideoInfo * vinfo, gint fd, VASurfaceID * surface_id)
{
  GstVideoFormat format;
  guint width, height, size, i;
  unsigned long extbuf_handle;
  guint va_fourcc = 0, va_chroma = 0;
  VASurfaceAttrib attribs[2], *attrib;
  VASurfaceAttribExternalBuffers extbuf;
  VAStatus va_status;
  mfxStatus status = MFX_ERR_NONE;

  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (vinfo != NULL, FALSE);
  g_return_val_if_fail (fd >= 0, FALSE);

  extbuf_handle = (guintptr) (fd);

  format = GST_VIDEO_INFO_FORMAT (vinfo);
  width = GST_VIDEO_INFO_WIDTH (vinfo);
  height = GST_VIDEO_INFO_HEIGHT (vinfo);
  size = GST_VIDEO_INFO_SIZE (vinfo);

  /* Fixme: Move to common format handling util */
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      va_chroma = VA_RT_FORMAT_YUV420;
      va_fourcc = VA_FOURCC_NV12;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      va_chroma = VA_RT_FORMAT_YUV444;
      va_fourcc = VA_FOURCC_BGRA;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      va_chroma = VA_RT_FORMAT_YUV422;
      va_fourcc = VA_FOURCC_YUY2;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      va_chroma = VA_RT_FORMAT_YUV420_10;
      va_fourcc = VA_FOURCC_P010;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      va_chroma = VA_RT_FORMAT_YUV422;
      va_fourcc = VA_FOURCC_UYVY;
      break;
    default:
      goto error_unsupported_format;
  }

  /* Fill the VASurfaceAttribExternalBuffers */
  extbuf.pixel_format = va_fourcc;
  extbuf.width = width;
  extbuf.height = height;
  extbuf.data_size = size;
  extbuf.num_planes = GST_VIDEO_INFO_N_PLANES (vinfo);
  for (i = 0; i < extbuf.num_planes; i++) {
    extbuf.pitches[i] = GST_VIDEO_INFO_PLANE_STRIDE (vinfo, i);
    extbuf.offsets[i] = GST_VIDEO_INFO_PLANE_OFFSET (vinfo, i);
  }
  extbuf.buffers = (uintptr_t *) & extbuf_handle;
  extbuf.num_buffers = 1;
  extbuf.flags = 0;
  extbuf.private_data = NULL;

  /* Fill the Surface Attributes */
  attrib = attribs;
  attrib->type = VASurfaceAttribMemoryType;
  attrib->flags = VA_SURFACE_ATTRIB_SETTABLE;
  attrib->value.type = VAGenericValueTypeInteger;
  attrib->value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
  attrib++;
  attrib->type = VASurfaceAttribExternalBufferDescriptor;
  attrib->flags = VA_SURFACE_ATTRIB_SETTABLE;
  attrib->value.type = VAGenericValueTypePointer;
  attrib->value.value.p = &extbuf;
  attrib++;

  va_status = vaCreateSurfaces (gst_msdk_context_get_handle (context),
      va_chroma, width, height, surface_id, 1, attribs, attrib - attribs);
  status = gst_msdk_get_mfx_status_from_va_status (va_status);
  if (status != MFX_ERR_NONE)
    goto error_create_surface;

  return TRUE;

error_unsupported_format:
  {
    GST_ERROR ("Unsupported Video format %s, Can't export dmabuf to vaSurface",
        gst_video_format_to_string (format));
    return FALSE;
  }
error_create_surface:
  {
    GST_ERROR ("Failed to create the VASurface from DRM_PRIME FD");
    return FALSE;
  }
}

/**
 * gst_msdk_replace_mfx_memid:
 * This method replace the internal VA Suface in mfxSurface with a new one
 *
 * Caution: Not a thread-safe routine, this method is here to work around
 * the dmabuf-import use case with dynamic memID replacement where msdk
 * originally Inited with fake memIDs.
 *
 * Don't use anywhere else unless you really know what you are doing!
 */
gboolean
gst_msdk_replace_mfx_memid (GstMsdkContext * context,
    mfxFrameSurface1 * mfx_surface, VASurfaceID surface_id)
{
  GstMsdkMemoryID *msdk_mid = NULL;
  VADisplay dpy;
  VASurfaceID *old_surface_id;
  VAStatus va_status;
  mfxStatus status = MFX_ERR_NONE;

  g_return_val_if_fail (mfx_surface != NULL, FALSE);
  g_return_val_if_fail (context != NULL, FALSE);

  msdk_mid = (GstMsdkMemoryID *) mfx_surface->Data.MemId;
  dpy = gst_msdk_context_get_handle (context);

  /* Destory the underlined VAImage if already mapped */
  if (msdk_mid->image.image_id != VA_INVALID_ID
      && msdk_mid->image.buf != VA_INVALID_ID) {
    status =
        gst_msdk_frame_unlock ((mfxHDL) context, (mfxMemId) msdk_mid, NULL);
    if (status != MFX_ERR_NONE)
      goto error_destroy_va_image;
  }

  /* Destroy the associated VASurface */
  old_surface_id = msdk_mid->surface;
  if (*old_surface_id != VA_INVALID_ID) {
    va_status = vaDestroySurfaces (dpy, old_surface_id, 1);
    status = gst_msdk_get_mfx_status_from_va_status (va_status);
    if (status != MFX_ERR_NONE)
      goto error_destroy_va_surface;
  }

  *msdk_mid->surface = surface_id;

  return TRUE;

error_destroy_va_image:
  {
    GST_ERROR ("Failed to Destroy the VAImage");
    return FALSE;
  }
error_destroy_va_surface:
  {
    GST_ERROR ("Failed to Destroy the VASurfaceID %x", *old_surface_id);
    return FALSE;
  }
}
