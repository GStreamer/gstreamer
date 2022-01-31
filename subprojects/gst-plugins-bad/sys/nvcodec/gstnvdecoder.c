/* GStreamer
 * Copyright (C) 2017 Ericsson AB. All rights reserved.
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

#ifdef HAVE_NVCODEC_GST_GL
#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>
#endif

#include "gstcudamemory.h"
#include "gstnvdecoder.h"
#include "gstcudabufferpool.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_nv_decoder_debug);
#define GST_CAT_DEFAULT gst_nv_decoder_debug

#ifdef HAVE_NVCODEC_GST_GL
#define SUPPORTED_GL_APIS (GST_GL_API_OPENGL | GST_GL_API_OPENGL3)
#endif

typedef struct _GstNvDecoderFrameInfo
{
  gboolean available;
} GstNvDecoderFrameInfo;

typedef enum
{
  GST_NV_DECODER_OUTPUT_TYPE_SYSTEM = 0,
  GST_NV_DECODER_OUTPUT_TYPE_GL,
  GST_NV_DECODER_OUTPUT_TYPE_CUDA,
  /* FIXME: add support D3D11 memory */
} GstNvDecoderOutputType;

struct _GstNvDecoder
{
  GstObject parent;
  GstCudaContext *context;
  CUstream cuda_stream;
  CUvideodecoder decoder_handle;

  GstNvDecoderFrameInfo *frame_pool;
  guint pool_size;

  GstVideoInfo info;
  GstVideoInfo coded_info;

  gboolean configured;

  /* For OpenGL interop. */
  GstObject *gl_display;
  GstObject *gl_context;
  GstObject *other_gl_context;

  GstNvDecoderOutputType output_type;
};

static void gst_nv_decoder_dispose (GObject * object);
static void gst_nv_decoder_reset (GstNvDecoder * self);

#define parent_class gst_nv_decoder_parent_class
G_DEFINE_TYPE (GstNvDecoder, gst_nv_decoder, GST_TYPE_OBJECT);

static void
gst_nv_decoder_class_init (GstNvDecoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_nv_decoder_dispose;
}

static void
gst_nv_decoder_init (GstNvDecoder * self)
{
}

static void
gst_nv_decoder_dispose (GObject * object)
{
  GstNvDecoder *self = GST_NV_DECODER (object);

  gst_nv_decoder_reset (self);

  if (self->context && self->cuda_stream) {
    if (gst_cuda_context_push (self->context)) {
      gst_cuda_result (CuStreamDestroy (self->cuda_stream));
      gst_cuda_context_pop (NULL);
      self->cuda_stream = NULL;
    }
  }

  gst_clear_object (&self->context);
  gst_clear_object (&self->gl_display);
  gst_clear_object (&self->gl_context);
  gst_clear_object (&self->other_gl_context);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static cudaVideoChromaFormat
chroma_format_from_video_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P010_10BE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_P016_BE:
      return cudaVideoChromaFormat_420;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_Y444_16BE:
      return cudaVideoChromaFormat_444;
    default:
      g_assert_not_reached ();
      break;
  }

  return cudaVideoChromaFormat_420;
}

static cudaVideoSurfaceFormat
output_format_from_video_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      return cudaVideoSurfaceFormat_NV12;
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P010_10BE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_P016_BE:
      return cudaVideoSurfaceFormat_P016;
    case GST_VIDEO_FORMAT_Y444:
      return cudaVideoSurfaceFormat_YUV444;
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_Y444_16BE:
      return cudaVideoSurfaceFormat_YUV444_16Bit;
    default:
      g_assert_not_reached ();
      break;
  }

  return cudaVideoSurfaceFormat_NV12;
}

static gboolean
gst_nv_decoder_prepare_frame_pool (GstNvDecoder * self, guint pool_size)
{
  gint i;

  self->frame_pool = g_new (GstNvDecoderFrameInfo, pool_size);

  for (i = 0; i < pool_size; i++)
    self->frame_pool[i].available = TRUE;

  self->pool_size = pool_size;

  return TRUE;
}

GstNvDecoder *
gst_nv_decoder_new (GstCudaContext * context)
{
  GstNvDecoder *self;

  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (context), NULL);

  self = g_object_new (GST_TYPE_NV_DECODER, NULL);
  self->context = gst_object_ref (context);
  gst_object_ref_sink (self);

  if (gst_cuda_context_push (context)) {
    CUresult cuda_ret;
    cuda_ret = CuStreamCreate (&self->cuda_stream, CU_STREAM_DEFAULT);
    if (!gst_cuda_result (cuda_ret)) {
      GST_WARNING_OBJECT (self,
          "Could not create CUDA stream, will use default stream");
      self->cuda_stream = NULL;
    }

    gst_cuda_context_pop (NULL);
  }

  return self;
}

gboolean
gst_nv_decoder_is_configured (GstNvDecoder * decoder)
{
  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), FALSE);

  return decoder->configured;
}

static void
gst_nv_decoder_reset (GstNvDecoder * self)
{
  g_clear_pointer (&self->frame_pool, g_free);

  if (self->decoder_handle) {
    gst_cuda_context_push (self->context);
    CuvidDestroyDecoder (self->decoder_handle);
    gst_cuda_context_pop (NULL);
    self->decoder_handle = NULL;
  }

  self->output_type = GST_NV_DECODER_OUTPUT_TYPE_SYSTEM;
  self->configured = FALSE;
}

gboolean
gst_nv_decoder_configure (GstNvDecoder * decoder, cudaVideoCodec codec,
    GstVideoInfo * info, gint coded_width, gint coded_height,
    guint coded_bitdepth, guint pool_size)
{
  CUVIDDECODECREATEINFO create_info = { 0, };
  GstVideoFormat format;
  gboolean ret;

  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), FALSE);
  g_return_val_if_fail (codec < cudaVideoCodec_NumCodecs, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (coded_width >= GST_VIDEO_INFO_WIDTH (info), FALSE);
  g_return_val_if_fail (coded_height >= GST_VIDEO_INFO_HEIGHT (info), FALSE);
  g_return_val_if_fail (coded_bitdepth >= 8, FALSE);
  g_return_val_if_fail (pool_size > 0, FALSE);

  gst_nv_decoder_reset (decoder);

  decoder->info = *info;
  gst_video_info_set_format (&decoder->coded_info, GST_VIDEO_INFO_FORMAT (info),
      coded_width, coded_height);

  format = GST_VIDEO_INFO_FORMAT (info);

  /* FIXME: check aligned resolution or actual coded resolution */
  create_info.ulWidth = GST_VIDEO_INFO_WIDTH (&decoder->coded_info);
  create_info.ulHeight = GST_VIDEO_INFO_HEIGHT (&decoder->coded_info);
  create_info.ulNumDecodeSurfaces = pool_size;
  create_info.CodecType = codec;
  create_info.ChromaFormat = chroma_format_from_video_format (format);
  create_info.ulCreationFlags = cudaVideoCreate_Default;
  create_info.bitDepthMinus8 = coded_bitdepth - 8;
  create_info.ulIntraDecodeOnly = 0;

  create_info.display_area.left = 0;
  create_info.display_area.top = 0;
  create_info.display_area.right = GST_VIDEO_INFO_WIDTH (info);
  create_info.display_area.bottom = GST_VIDEO_INFO_HEIGHT (info);
  create_info.OutputFormat = output_format_from_video_format (format);
  create_info.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;

  create_info.ulTargetWidth = GST_VIDEO_INFO_WIDTH (info);
  create_info.ulTargetHeight = GST_VIDEO_INFO_HEIGHT (info);
  /* we always copy decoded picture to output buffer */
  create_info.ulNumOutputSurfaces = 1;

  create_info.target_rect.left = 0;
  create_info.target_rect.top = 0;
  create_info.target_rect.right = GST_VIDEO_INFO_WIDTH (info);
  create_info.target_rect.bottom = GST_VIDEO_INFO_HEIGHT (info);

  if (!gst_cuda_context_push (decoder->context)) {
    GST_ERROR_OBJECT (decoder, "Failed to lock CUDA context");
    return FALSE;
  }

  ret = gst_cuda_result (CuvidCreateDecoder (&decoder->decoder_handle,
          &create_info));
  gst_cuda_context_pop (NULL);

  if (!ret) {
    GST_ERROR_OBJECT (decoder, "Cannot create decoder instance");
    return FALSE;
  }

  if (!gst_nv_decoder_prepare_frame_pool (decoder, pool_size)) {
    GST_ERROR_OBJECT (decoder, "Cannot prepare internal surface buffer pool");
    gst_nv_decoder_reset (decoder);
    return FALSE;
  }

  decoder->configured = TRUE;

  return TRUE;
}

GstNvDecoderFrame *
gst_nv_decoder_new_frame (GstNvDecoder * decoder)
{
  GstNvDecoderFrame *frame;
  gint i;
  gint index_to_use = -1;

  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), NULL);

  for (i = 0; i < decoder->pool_size; i++) {
    if (decoder->frame_pool[i].available) {
      decoder->frame_pool[i].available = FALSE;
      index_to_use = i;
      break;
    }
  }

  if (index_to_use < 0) {
    GST_ERROR_OBJECT (decoder, "No available frame");
    return NULL;
  }

  frame = g_new0 (GstNvDecoderFrame, 1);
  frame->index = index_to_use;
  frame->decoder = gst_object_ref (decoder);
  frame->ref_count = 1;

  GST_LOG_OBJECT (decoder, "New frame %p (index %d)", frame, frame->index);

  return frame;
}

/* must be called with gst_cuda_context_push */
static gboolean
gst_nv_decoder_frame_map (GstNvDecoderFrame * frame)
{
  GstNvDecoder *self;
  CUVIDPROCPARAMS params = { 0 };

  g_return_val_if_fail (frame != NULL, FALSE);
  g_return_val_if_fail (frame->index >= 0, FALSE);
  g_return_val_if_fail (GST_IS_NV_DECODER (frame->decoder), FALSE);

  self = frame->decoder;

  /* TODO: check interlaced */
  params.progressive_frame = 1;

  if (frame->mapped) {
    GST_WARNING_OBJECT (self, "Frame %p is mapped already", frame);
    return TRUE;
  }

  if (!gst_cuda_result (CuvidMapVideoFrame (self->decoder_handle,
              frame->index, &frame->devptr, &frame->pitch, &params))) {
    GST_ERROR_OBJECT (self, "Cannot map picture");
    return FALSE;
  }

  frame->mapped = TRUE;

  return TRUE;
}

/* must be called with gst_cuda_context_push */
static void
gst_nv_decoder_frame_unmap (GstNvDecoderFrame * frame)
{
  GstNvDecoder *self;

  g_return_if_fail (frame != NULL);
  g_return_if_fail (frame->index >= 0);
  g_return_if_fail (GST_IS_NV_DECODER (frame->decoder));

  self = frame->decoder;

  if (!frame->mapped) {
    GST_WARNING_OBJECT (self, "Frame %p is not mapped", frame);
    return;
  }

  if (!gst_cuda_result (CuvidUnmapVideoFrame (self->decoder_handle,
              frame->devptr))) {
    GST_ERROR_OBJECT (self, "Cannot unmap picture");
  }

  frame->mapped = FALSE;
}

GstNvDecoderFrame *
gst_nv_decoder_frame_ref (GstNvDecoderFrame * frame)
{
  g_assert (frame != NULL);

  g_atomic_int_add (&frame->ref_count, 1);

  return frame;
}

void
gst_nv_decoder_frame_unref (GstNvDecoderFrame * frame)
{
  GstNvDecoder *self;

  g_assert (frame != NULL);

  if (g_atomic_int_dec_and_test (&frame->ref_count)) {
    GST_LOG ("Free frame %p (index %d)", frame, frame->index);

    if (frame->decoder) {
      self = frame->decoder;
      if (frame->mapped && gst_cuda_context_push (self->context)) {
        gst_nv_decoder_frame_unmap (frame);
        gst_cuda_context_pop (NULL);
      }

      if (frame->index < self->pool_size) {
        self->frame_pool[frame->index].available = TRUE;
      } else {
        GST_WARNING_OBJECT (self,
            "Frame %p has invalid index %d", frame, frame->index);
      }

      gst_object_unref (self);
    }

    g_free (frame);
  }
}

gboolean
gst_nv_decoder_decode_picture (GstNvDecoder * decoder, CUVIDPICPARAMS * params)
{
  GstCudaContext *ctx = decoder->context;
  gboolean ret = TRUE;

  GST_LOG_OBJECT (decoder, "picture index: %u", params->CurrPicIdx);

  if (!gst_cuda_context_push (ctx)) {
    GST_ERROR_OBJECT (decoder, "Failed to push CUDA context");
    return FALSE;
  }

  if (!gst_cuda_result (CuvidDecodePicture (decoder->decoder_handle, params))) {
    GST_ERROR_OBJECT (decoder, "Failed to decode picture");
    ret = FALSE;
  }

  if (!gst_cuda_context_pop (NULL)) {
    GST_WARNING_OBJECT (decoder, "Failed to pop CUDA context");
  }

  return ret;
}

#ifdef HAVE_NVCODEC_GST_GL
static gboolean
gst_nv_decoder_register_cuda_resource (GstNvDecoder * self, GstMemory * mem,
    GstCudaGraphicsResource * resource)
{
  GstMapInfo info;
  gboolean ret = FALSE;

  if (!gst_cuda_context_push (self->context)) {
    GST_ERROR_OBJECT (self, "Failed to push CUDA context");
    return FALSE;
  }

  if (gst_memory_map (mem, &info, GST_MAP_READ | GST_MAP_GL)) {
    GstGLMemoryPBO *gl_mem = (GstGLMemoryPBO *) mem;
    GstGLBuffer *gl_buffer = gl_mem->pbo;

    GST_LOG_OBJECT (self,
        "Register glbuffer %d to CUDA resource", gl_buffer->id);

    /* register resource without read/write only flags, since
     * downstream CUDA elements (e.g., nvenc) might want to access
     * this resource later. Instead, use map flags during map/unmap */
    if (gst_cuda_graphics_resource_register_gl_buffer (resource,
            gl_buffer->id, CU_GRAPHICS_REGISTER_FLAGS_NONE)) {
      ret = TRUE;
    } else {
      GST_WARNING_OBJECT (self, "Failed to register memory");
    }

    gst_memory_unmap (mem, &info);
  } else {
    GST_WARNING_OBJECT (self, "Failed to map memory");
  }

  if (!gst_cuda_context_pop (NULL))
    GST_WARNING_OBJECT (self, "Failed to pop CUDA context");

  return ret;
}

static GstCudaGraphicsResource *
gst_nv_decoder_ensure_cuda_graphics_resource (GstNvDecoder * self,
    GstMemory * mem)
{
  GQuark quark;
  GstCudaGraphicsResource *resource;

  if (!gst_is_gl_memory_pbo (mem)) {
    GST_WARNING_OBJECT (self, "memory is not GL PBO memory, %s",
        mem->allocator->mem_type);
    return NULL;
  }

  quark = gst_cuda_quark_from_id (GST_CUDA_QUARK_GRAPHICS_RESOURCE);

  resource = (GstCudaGraphicsResource *)
      gst_mini_object_get_qdata (GST_MINI_OBJECT (mem), quark);

  if (!resource) {
    gboolean ret;

    resource = gst_cuda_graphics_resource_new (self->context,
        GST_OBJECT (GST_GL_BASE_MEMORY_CAST (mem)->context),
        GST_CUDA_GRAPHICS_RESOURCE_GL_BUFFER);

    ret = gst_nv_decoder_register_cuda_resource (self, mem, resource);
    if (!ret) {
      GST_WARNING_OBJECT (self, "Couldn't register resource");
      gst_cuda_graphics_resource_free (resource);

      return NULL;
    }

    gst_mini_object_set_qdata (GST_MINI_OBJECT (mem), quark, resource,
        (GDestroyNotify) gst_cuda_graphics_resource_free);
  }

  return resource;
}

typedef struct
{
  GstNvDecoder *self;
  gboolean ret;
  GstNvDecoderFrame *frame;
  GstBuffer *output_buffer;
} GstNvDecoderCopyToGLData;

static void
gst_nv_decoder_copy_frame_to_gl_internal (GstGLContext * context,
    GstNvDecoderCopyToGLData * data)
{
  GstNvDecoder *self = data->self;
  GstNvDecoderFrame *frame = data->frame;
  GstCudaGraphicsResource **resources;
  guint num_resources;
  guint i;
  CUDA_MEMCPY2D copy_params = { 0, };
  GstVideoInfo *info = &self->info;

  data->ret = TRUE;

  num_resources = gst_buffer_n_memory (data->output_buffer);
  resources = g_newa (GstCudaGraphicsResource *, num_resources);

  for (i = 0; i < num_resources; i++) {
    GstMemory *mem;

    mem = gst_buffer_peek_memory (data->output_buffer, i);
    resources[i] = gst_nv_decoder_ensure_cuda_graphics_resource (self, mem);
    if (!resources[i]) {
      GST_WARNING_OBJECT (self, "could not register %dth memory", i);
      data->ret = FALSE;

      return;
    }

    /* Need PBO -> texture */
    GST_MINI_OBJECT_FLAG_SET (mem, GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD);
  }

  if (!gst_cuda_context_push (self->context)) {
    GST_WARNING_OBJECT (self, "Failed to push CUDA context");
    data->ret = FALSE;
    return;
  }

  copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  copy_params.srcPitch = frame->pitch;
  copy_params.dstMemoryType = CU_MEMORYTYPE_DEVICE;

  for (i = 0; i < num_resources; i++) {
    CUdeviceptr dst_ptr;
    gsize size;
    CUgraphicsResource cuda_resource =
        gst_cuda_graphics_resource_map (resources[i], NULL,
        CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD);

    if (!cuda_resource) {
      GST_WARNING_OBJECT (self, "failed to map CUDA resources");
      data->ret = FALSE;
      goto unmap_video_frame;
    }

    if (!gst_cuda_result (CuGraphicsResourceGetMappedPointer (&dst_ptr, &size,
                cuda_resource))) {
      GST_WARNING_OBJECT (self, "failed to map CUDA resource");
      data->ret = FALSE;
      break;
    }

    copy_params.dstPitch = GST_VIDEO_INFO_PLANE_STRIDE (info, i);
    copy_params.WidthInBytes = GST_VIDEO_INFO_COMP_WIDTH (info, i)
        * GST_VIDEO_INFO_COMP_PSTRIDE (info, i);

    copy_params.srcDevice = frame->devptr +
        (i * frame->pitch * GST_VIDEO_INFO_HEIGHT (&self->info));
    copy_params.dstDevice = dst_ptr;
    copy_params.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

    if (!gst_cuda_result (CuMemcpy2DAsync (&copy_params, NULL))) {
      GST_WARNING_OBJECT (self, "memcpy to mapped array failed");
      data->ret = FALSE;
    }
  }

  gst_cuda_result (CuStreamSynchronize (NULL));

unmap_video_frame:
  for (i = 0; i < num_resources; i++) {
    gst_cuda_graphics_resource_unmap (resources[i], NULL);
  }

  if (!gst_cuda_context_pop (NULL))
    GST_WARNING_OBJECT (self, "Failed to pop CUDA context");
}

static gboolean
gst_nv_decoder_copy_frame_to_gl (GstNvDecoder * decoder,
    GstGLContext * context, GstNvDecoderFrame * frame, GstBuffer * buffer)
{
  GstNvDecoderCopyToGLData data;

  data.self = decoder;
  data.frame = frame;
  data.output_buffer = buffer;

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) gst_nv_decoder_copy_frame_to_gl_internal, &data);

  GST_LOG_OBJECT (decoder, "Copy frame to GL ret %d", data.ret);

  return data.ret;
}
#endif

static gboolean
gst_nv_decoder_copy_frame_to_system (GstNvDecoder * decoder,
    GstNvDecoderFrame * frame, GstBuffer * buffer)
{
  GstVideoFrame video_frame;
  CUDA_MEMCPY2D copy_params = { 0, };
  gint i;
  gboolean ret = FALSE;

  if (!gst_video_frame_map (&video_frame, &decoder->info, buffer,
          GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (decoder, "Couldn't map video frame");
    return FALSE;
  }

  if (!gst_cuda_context_push (decoder->context)) {
    GST_ERROR_OBJECT (decoder, "Failed to push CUDA context");
    gst_video_frame_unmap (&video_frame);
    return FALSE;
  }

  copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  copy_params.srcPitch = frame->pitch;
  copy_params.dstMemoryType = CU_MEMORYTYPE_HOST;
  copy_params.WidthInBytes = GST_VIDEO_INFO_COMP_WIDTH (&decoder->info, 0)
      * GST_VIDEO_INFO_COMP_PSTRIDE (&decoder->info, 0);

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (&video_frame); i++) {
    copy_params.srcDevice = frame->devptr +
        (i * frame->pitch * GST_VIDEO_INFO_HEIGHT (&decoder->info));
    copy_params.dstHost = GST_VIDEO_FRAME_PLANE_DATA (&video_frame, i);
    copy_params.dstPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&video_frame, i);
    copy_params.Height = GST_VIDEO_FRAME_COMP_HEIGHT (&video_frame, i);

    if (!gst_cuda_result (CuMemcpy2DAsync (&copy_params, decoder->cuda_stream))) {
      GST_ERROR_OBJECT (decoder, "failed to copy %dth plane", i);
      goto done;
    }
  }

  gst_cuda_result (CuStreamSynchronize (decoder->cuda_stream));

  ret = TRUE;

done:
  gst_cuda_context_pop (NULL);

  gst_video_frame_unmap (&video_frame);

  GST_LOG_OBJECT (decoder, "Copy frame to system ret %d", ret);

  return ret;
}

static gboolean
gst_nv_decoder_copy_frame_to_cuda (GstNvDecoder * decoder,
    GstNvDecoderFrame * frame, GstBuffer * buffer)
{
  CUDA_MEMCPY2D copy_params = { 0, };
  GstMemory *mem;
  GstCudaMemory *cuda_mem = NULL;
  gint i;
  gboolean ret = FALSE;

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_cuda_memory (mem)) {
    GST_WARNING_OBJECT (decoder, "Not a CUDA memory");
    return FALSE;
  } else {
    GstCudaMemory *cmem = GST_CUDA_MEMORY_CAST (mem);

    if (cmem->context == decoder->context ||
        gst_cuda_context_get_handle (cmem->context) ==
        gst_cuda_context_get_handle (decoder->context) ||
        (gst_cuda_context_can_access_peer (cmem->context, decoder->context) &&
            gst_cuda_context_can_access_peer (decoder->context,
                cmem->context))) {
      cuda_mem = cmem;
    }
  }

  if (!cuda_mem) {
    GST_WARNING_OBJECT (decoder, "Access to CUDA memory is not allowed");
    return FALSE;
  }

  if (!gst_cuda_context_push (decoder->context)) {
    GST_ERROR_OBJECT (decoder, "Failed to push CUDA context");
    return FALSE;
  }

  copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  copy_params.srcPitch = frame->pitch;
  copy_params.dstMemoryType = CU_MEMORYTYPE_DEVICE;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&decoder->info); i++) {
    copy_params.srcDevice = frame->devptr +
        (i * frame->pitch * GST_VIDEO_INFO_HEIGHT (&decoder->info));
    copy_params.dstDevice = cuda_mem->data + cuda_mem->offset[i];
    copy_params.dstPitch = cuda_mem->stride;
    copy_params.WidthInBytes = GST_VIDEO_INFO_COMP_WIDTH (&decoder->info, 0)
        * GST_VIDEO_INFO_COMP_PSTRIDE (&decoder->info, 0);
    copy_params.Height = GST_VIDEO_INFO_COMP_HEIGHT (&decoder->info, i);

    if (!gst_cuda_result (CuMemcpy2DAsync (&copy_params, decoder->cuda_stream))) {
      GST_ERROR_OBJECT (decoder, "failed to copy %dth plane", i);
      goto done;
    }
  }

  gst_cuda_result (CuStreamSynchronize (decoder->cuda_stream));

  ret = TRUE;

done:
  gst_cuda_context_pop (NULL);

  GST_LOG_OBJECT (decoder, "Copy frame to CUDA ret %d", ret);

  return ret;
}

gboolean
gst_nv_decoder_finish_frame (GstNvDecoder * decoder, GstVideoDecoder * videodec,
    GstNvDecoderFrame * frame, GstBuffer ** buffer)
{
  GstBuffer *outbuf = NULL;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), GST_FLOW_ERROR);
  g_return_val_if_fail (frame != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  outbuf = gst_video_decoder_allocate_output_buffer (videodec);
  if (!outbuf) {
    GST_ERROR_OBJECT (videodec, "Couldn't allocate output buffer");
    return FALSE;
  }

  if (!gst_cuda_context_push (decoder->context)) {
    GST_ERROR_OBJECT (decoder, "Failed to push CUDA context");
    goto error;
  }

  if (!gst_nv_decoder_frame_map (frame)) {
    GST_ERROR_OBJECT (decoder, "Couldn't map frame");
    gst_cuda_context_pop (NULL);
    goto error;
  }

  gst_cuda_context_pop (NULL);

  switch (decoder->output_type) {
    case GST_NV_DECODER_OUTPUT_TYPE_SYSTEM:
      ret = gst_nv_decoder_copy_frame_to_system (decoder, frame, outbuf);
      break;
#ifdef HAVE_NVCODEC_GST_GL
    case GST_NV_DECODER_OUTPUT_TYPE_GL:
      g_assert (decoder->gl_context != NULL);

      ret = gst_nv_decoder_copy_frame_to_gl (decoder,
          GST_GL_CONTEXT (decoder->gl_context), frame, outbuf);
      break;
#endif
    case GST_NV_DECODER_OUTPUT_TYPE_CUDA:
      ret = gst_nv_decoder_copy_frame_to_cuda (decoder, frame, outbuf);
      break;
    default:
      g_assert_not_reached ();
      goto error;
  }

  /* FIXME: This is the case where OpenGL context of downstream glbufferpool
   * belongs to non-nvidia (or different device).
   * There should be enhancement to ensure nvdec has compatible OpenGL context
   */
  if (!ret && decoder->output_type == GST_NV_DECODER_OUTPUT_TYPE_GL) {
    GST_WARNING_OBJECT (videodec,
        "Couldn't copy frame to GL memory, fallback to system memory");
    decoder->output_type = GST_NV_DECODER_OUTPUT_TYPE_SYSTEM;

    ret = gst_nv_decoder_copy_frame_to_system (decoder, frame, outbuf);
  }

  gst_cuda_context_push (decoder->context);
  gst_nv_decoder_frame_unmap (frame);
  gst_cuda_context_pop (NULL);

  if (!ret) {
    GST_WARNING_OBJECT (videodec, "Failed to copy frame");
    goto error;
  }

  *buffer = outbuf;

  return TRUE;

error:
  gst_clear_buffer (&outbuf);
  return FALSE;
}

typedef enum
{
  GST_NV_DECODER_FORMAT_FLAG_NONE = (1 << 0),
  GST_NV_DECODER_FORMAT_FLAG_420_8BITS = (1 << 1),
  GST_NV_DECODER_FORMAT_FLAG_420_10BITS = (1 << 2),
  GST_NV_DECODER_FORMAT_FLAG_420_12BITS = (1 << 3),
  GST_NV_DECODER_FORMAT_FLAG_444_8BITS = (1 << 4),
  GST_NV_DECODER_FORMAT_FLAG_444_10BITS = (1 << 5),
  GST_NV_DECODER_FORMAT_FLAG_444_12BITS = (1 << 6),
} GstNvDecoderFormatFlags;

static gboolean
gst_nv_decoder_get_supported_codec_profiles (GValue * profiles,
    cudaVideoCodec codec, GstNvDecoderFormatFlags flags)
{
  GValue val = G_VALUE_INIT;
  gboolean ret = FALSE;

  g_value_init (&val, G_TYPE_STRING);

  switch (codec) {
    case cudaVideoCodec_H264:
      if ((flags & GST_NV_DECODER_FORMAT_FLAG_420_8BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_420_8BITS) {
        g_value_set_static_string (&val, "constrained-baseline");
        gst_value_list_append_value (profiles, &val);

        g_value_set_static_string (&val, "baseline");
        gst_value_list_append_value (profiles, &val);

        g_value_set_static_string (&val, "main");
        gst_value_list_append_value (profiles, &val);

        g_value_set_static_string (&val, "high");
        gst_value_list_append_value (profiles, &val);

        g_value_set_static_string (&val, "constrained-high");
        gst_value_list_append_value (profiles, &val);

        g_value_set_static_string (&val, "progressive-high");
        gst_value_list_append_value (profiles, &val);
      }

      /* NVDEC supports only 4:2:0 8bits h264 decoding.
       * following conditions are for the future enhancement */
      if ((flags & GST_NV_DECODER_FORMAT_FLAG_420_10BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_420_10BITS) {
        g_value_set_static_string (&val, "high-10");
        gst_value_list_append_value (profiles, &val);

        g_value_set_static_string (&val, "progressive-high-10");
        gst_value_list_append_value (profiles, &val);
      }

      if ((flags & GST_NV_DECODER_FORMAT_FLAG_420_12BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_420_12BITS ||
          (flags & GST_NV_DECODER_FORMAT_FLAG_444_8BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_444_8BITS ||
          (flags & GST_NV_DECODER_FORMAT_FLAG_444_10BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_444_10BITS ||
          (flags & GST_NV_DECODER_FORMAT_FLAG_444_12BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_444_12BITS) {
        g_value_set_static_string (&val, "high-4:4:4");
        gst_value_list_append_value (profiles, &val);
      }

      ret = TRUE;
      break;
    case cudaVideoCodec_HEVC:
      if ((flags & GST_NV_DECODER_FORMAT_FLAG_420_8BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_420_8BITS) {
        g_value_set_static_string (&val, "main");
        gst_value_list_append_value (profiles, &val);
      }

      if ((flags & GST_NV_DECODER_FORMAT_FLAG_420_10BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_420_10BITS) {
        g_value_set_static_string (&val, "main-10");
        gst_value_list_append_value (profiles, &val);
      }

      if ((flags & GST_NV_DECODER_FORMAT_FLAG_420_12BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_420_12BITS) {
        g_value_set_static_string (&val, "main-12");
        gst_value_list_append_value (profiles, &val);
      }

      if ((flags & GST_NV_DECODER_FORMAT_FLAG_444_8BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_444_8BITS) {
        g_value_set_static_string (&val, "main-444");
        gst_value_list_append_value (profiles, &val);
      }

      if ((flags & GST_NV_DECODER_FORMAT_FLAG_444_10BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_444_10BITS) {
        g_value_set_static_string (&val, "main-444-10");
        gst_value_list_append_value (profiles, &val);
      }

      if ((flags & GST_NV_DECODER_FORMAT_FLAG_444_12BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_444_12BITS) {
        g_value_set_static_string (&val, "main-444-12");
        gst_value_list_append_value (profiles, &val);
      }

      ret = TRUE;
      break;
    case cudaVideoCodec_VP9:
      if ((flags & GST_NV_DECODER_FORMAT_FLAG_420_8BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_420_8BITS) {
        g_value_set_static_string (&val, "0");
        gst_value_list_append_value (profiles, &val);
      }

      if ((flags & GST_NV_DECODER_FORMAT_FLAG_420_10BITS) ==
          GST_NV_DECODER_FORMAT_FLAG_420_10BITS) {
        g_value_set_static_string (&val, "2");
        gst_value_list_append_value (profiles, &val);
      }

      ret = TRUE;
      break;
    default:
      break;
  }

  g_value_unset (&val);

  return ret;
}

typedef struct
{
  cudaVideoCodec codec;
  const gchar *codec_name;
  const gchar *sink_caps_string;
} GstNvdecoderCodecMap;

const GstNvdecoderCodecMap codec_map_list[] = {
  {cudaVideoCodec_MPEG1, "mpegvideo",
      "video/mpeg, mpegversion = (int) 1, systemstream = (boolean) false"},
  {cudaVideoCodec_MPEG2, "mpeg2video",
      "video/mpeg, mpegversion = (int) 2, systemstream = (boolean) false"},
  {cudaVideoCodec_MPEG4, "mpeg4video",
      "video/mpeg, mpegversion = (int) 4, systemstream = (boolean) false"},
#if 0
  /* FIXME: need verification */
  {cudaVideoCodec_VC1, "vc1"},
#endif
  /* NOTE: common supported h264 profiles for all GPU architecture
   * 4:2:0, baseline, main, and high profiles
   */
  {cudaVideoCodec_H264, "h264",
      "video/x-h264, stream-format = (string) byte-stream"
        ", alignment = (string) au"
        ", profile = (string) { constrained-baseline, baseline, main, high, constrained-high, progressive-high }"},
  {cudaVideoCodec_JPEG, "jpeg", "image/jpeg"},
#if 0
  /* FIXME: need verification */
  {cudaVideoCodec_H264_SVC, "h264svc"},
  {cudaVideoCodec_H264_MVC, "h264mvc"},
#endif
  {cudaVideoCodec_HEVC, "h265",
      "video/x-h265, stream-format = (string) byte-stream"
        ", alignment = (string) au, profile = (string) { main }"},
  {cudaVideoCodec_VP8, "vp8", "video/x-vp8"},
  {cudaVideoCodec_VP9, "vp9", "video/x-vp9"}
};

gboolean
gst_nv_decoder_check_device_caps (CUcontext cuda_ctx, cudaVideoCodec codec,
    GstCaps ** sink_template, GstCaps ** src_template)
{
  CUresult cuda_ret;
  gint max_width = 0, min_width = G_MAXINT;
  gint max_height = 0, min_height = G_MAXINT;
  GstCaps *sink_templ = NULL;
  GstCaps *src_templ = NULL;
  /* FIXME: support 12bits format */
  guint bitdepth_minus8[3] = { 0, 2, 4 };
  GstNvDecoderFormatFlags format_flags = 0;
  gint c_idx, b_idx;
  guint num_support = 0;
  cudaVideoChromaFormat chroma_list[] = {
#if 0
    /* FIXME: support monochrome */
    cudaVideoChromaFormat_Monochrome,
    /* FIXME: Can our OpenGL support NV16 and its 10/12bits variant?? */
    cudaVideoChromaFormat_422,
#endif
    cudaVideoChromaFormat_420,
    cudaVideoChromaFormat_444,
  };
  GValue format_list = G_VALUE_INIT;
  GValue format = G_VALUE_INIT;
  GValue profile_list = G_VALUE_INIT;
  const GstNvdecoderCodecMap *codec_map = NULL;
  guint i;
  gboolean ret = FALSE;

  for (i = 0; i < G_N_ELEMENTS (codec_map_list); i++) {
    if (codec_map_list[i].codec == codec) {
      codec_map = &codec_map_list[i];
      break;
    }
  }

  if (!codec_map) {
    GST_INFO ("No codec map corresponding to codec %d", codec);
    return FALSE;
  }

  if (!gst_cuvid_can_get_decoder_caps ()) {
    GST_INFO ("Too old nvidia driver to query decoder capability");

    src_templ = gst_caps_from_string (GST_VIDEO_CAPS_MAKE ("NV12"));

    {
      GstCaps *cuda_caps = gst_caps_copy (src_templ);
      gst_caps_set_features_simple (cuda_caps,
          gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY));

#if HAVE_NVCODEC_GST_GL
      {
        GstCaps *gl_caps = gst_caps_copy (src_templ);
        gst_caps_set_features_simple (gl_caps,
            gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_GL_MEMORY));
        gst_caps_append (src_templ, gl_caps);
      }
#endif

      gst_caps_append (src_templ, cuda_caps);
    }

    sink_templ = gst_caps_from_string (codec_map->sink_caps_string);

    *src_template = src_templ;
    *sink_template = sink_templ;

    return TRUE;
  }

  g_value_init (&format_list, GST_TYPE_LIST);
  g_value_init (&format, G_TYPE_STRING);
  g_value_init (&profile_list, GST_TYPE_LIST);

  if (CuCtxPushCurrent (cuda_ctx) != CUDA_SUCCESS)
    goto done;

  for (c_idx = 0; c_idx < G_N_ELEMENTS (chroma_list); c_idx++) {
    for (b_idx = 0; b_idx < G_N_ELEMENTS (bitdepth_minus8); b_idx++) {
      CUVIDDECODECAPS decoder_caps = { 0, };
      GstNvDecoderFormatFlags cur_flag = 0;

      decoder_caps.eCodecType = codec;
      decoder_caps.eChromaFormat = chroma_list[c_idx];
      decoder_caps.nBitDepthMinus8 = bitdepth_minus8[b_idx];

      cuda_ret = CuvidGetDecoderCaps (&decoder_caps);
      if (cuda_ret != CUDA_SUCCESS) {
        GST_INFO ("could not query %s decoder capability, ret %d",
            codec_map->codec_name, cuda_ret);
        continue;
      } else if (!decoder_caps.bIsSupported) {
        GST_LOG ("%s bit-depth %d with chroma format %d is not supported",
            codec_map->codec_name, bitdepth_minus8[b_idx] + 8, c_idx);
        continue;
      }

      if (min_width > decoder_caps.nMinWidth)
        min_width = decoder_caps.nMinWidth;
      if (min_height > decoder_caps.nMinHeight)
        min_height = decoder_caps.nMinHeight;
      if (max_width < decoder_caps.nMaxWidth)
        max_width = decoder_caps.nMaxWidth;
      if (max_height < decoder_caps.nMaxHeight)
        max_height = decoder_caps.nMaxHeight;

      if (chroma_list[c_idx] == cudaVideoChromaFormat_420)
        cur_flag = GST_NV_DECODER_FORMAT_FLAG_420_8BITS;
      else
        cur_flag = GST_NV_DECODER_FORMAT_FLAG_444_8BITS;

      format_flags |= (cur_flag << (bitdepth_minus8[b_idx] / 2));

      GST_INFO ("%s bit-depth %d with chroma format %d [%d - %d] x [%d - %d]",
          codec_map->codec_name, bitdepth_minus8[b_idx] + 8, c_idx, min_width,
          max_width, min_height, max_height);

      switch (chroma_list[c_idx]) {
        case cudaVideoChromaFormat_420:
          if (bitdepth_minus8[b_idx] == 0) {
            g_value_set_string (&format, "NV12");
          } else if (bitdepth_minus8[b_idx] == 2) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
            g_value_set_string (&format, "P010_10LE");
#else
            g_value_set_string (&format, "P010_10BE");
#endif
          } else if (bitdepth_minus8[b_idx] == 4) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
            g_value_set_string (&format, "P016_LE");
#else
            g_value_set_string (&format, "P016_BE");
#endif
          } else {
            GST_WARNING ("unhandled bitdepth %d", bitdepth_minus8[b_idx] + 8);
            break;
          }
          num_support++;
          gst_value_list_append_value (&format_list, &format);
          break;
        case cudaVideoChromaFormat_444:
          if (cudaVideoCodec_JPEG == codec) {
            /* NVDEC jpeg decoder can decode 4:4:4 format
             * but it produces 4:2:0 frame */
            break;
          }

          if (bitdepth_minus8[b_idx] == 0) {
            g_value_set_string (&format, "Y444");
          } else if (bitdepth_minus8[b_idx] == 2 || bitdepth_minus8[b_idx] == 4) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
            g_value_set_string (&format, "Y444_16LE");
#else
            g_value_set_string (&format, "Y444_16BE");
#endif
          } else {
            GST_WARNING ("unhandled bitdepth %d", bitdepth_minus8[b_idx] + 8);
            break;
          }
          num_support++;
          gst_value_list_append_value (&format_list, &format);
          break;
        default:
          break;
      }
    }
  }

  if (num_support == 0) {
    GST_INFO ("device can not support %s", codec_map->codec_name);
    goto done;
  }

  src_templ = gst_caps_new_simple ("video/x-raw",
      "width", GST_TYPE_INT_RANGE, min_width, max_width,
      "height", GST_TYPE_INT_RANGE, min_height, max_height,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

  gst_caps_set_value (src_templ, "format", &format_list);

  {
    GstCaps *cuda_caps = gst_caps_copy (src_templ);
    gst_caps_set_features_simple (cuda_caps,
        gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY));

    /* OpenGL specific */
#if HAVE_NVCODEC_GST_GL
    {
      GstCaps *gl_caps = gst_caps_copy (src_templ);
      gst_caps_set_features_simple (gl_caps,
          gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_GL_MEMORY));
      gst_caps_append (src_templ, gl_caps);
    }
#endif

    gst_caps_append (src_templ, cuda_caps);
  }

  sink_templ = gst_caps_from_string (codec_map->sink_caps_string);
  gst_caps_set_simple (sink_templ,
      "width", GST_TYPE_INT_RANGE, min_width, max_width,
      "height", GST_TYPE_INT_RANGE, min_height, max_height, NULL);

  if (gst_nv_decoder_get_supported_codec_profiles (&profile_list, codec,
          format_flags)) {
    gst_caps_set_value (sink_templ, "profile", &profile_list);
  }

  GST_DEBUG ("sink template caps %" GST_PTR_FORMAT, sink_templ);
  GST_DEBUG ("src template caps %" GST_PTR_FORMAT, src_templ);

  CuCtxPopCurrent (NULL);

done:
  g_value_unset (&format_list);
  g_value_unset (&format);
  g_value_unset (&profile_list);

  if (!sink_templ || !src_templ) {
    gst_clear_caps (&sink_templ);
    gst_clear_caps (&src_templ);

    ret = FALSE;
  } else {
    /* class data will be leaked if the element never gets instantiated */
    GST_MINI_OBJECT_FLAG_SET (src_templ, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
    GST_MINI_OBJECT_FLAG_SET (sink_templ, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

    *src_template = src_templ;
    *sink_template = sink_templ;

    ret = TRUE;
  }

  return ret;
}

const gchar *
gst_cuda_video_codec_to_string (cudaVideoCodec codec)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (codec_map_list); i++) {
    if (codec_map_list[i].codec == codec)
      return codec_map_list[i].codec_name;
  }

  return "unknown";
}

gboolean
gst_nv_decoder_handle_set_context (GstNvDecoder * decoder,
    GstElement * videodec, GstContext * context)
{
  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (videodec), FALSE);

#ifdef HAVE_NVCODEC_GST_GL
  if (gst_gl_handle_set_context (videodec, context,
          (GstGLDisplay **) & decoder->gl_display,
          (GstGLContext **) & decoder->other_gl_context)) {
    return TRUE;
  }
#endif

  return FALSE;
}

gboolean
gst_nv_decoder_handle_context_query (GstNvDecoder * decoder,
    GstVideoDecoder * videodec, GstQuery * query)
{
  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (videodec), FALSE);

#ifdef HAVE_NVCODEC_GST_GL
  if (gst_gl_handle_context_query (GST_ELEMENT (videodec), query,
          (GstGLDisplay *) decoder->gl_display,
          (GstGLContext *) decoder->gl_context,
          (GstGLContext *) decoder->other_gl_context)) {
    if (decoder->gl_display)
      gst_gl_display_filter_gl_api (GST_GL_DISPLAY (decoder->gl_display),
          SUPPORTED_GL_APIS);
    return TRUE;
  }
#endif

  return FALSE;
}

#ifdef HAVE_NVCODEC_GST_GL
static void
gst_nv_decoder_check_cuda_device_from_context (GstGLContext * context,
    gboolean * ret)
{
  guint device_count = 0;
  CUdevice device_list[1] = { 0, };
  CUresult cuda_ret;

  *ret = FALSE;

  cuda_ret = CuGLGetDevices (&device_count,
      device_list, 1, CU_GL_DEVICE_LIST_ALL);

  if (!gst_cuda_result (cuda_ret) || device_count == 0)
    return;

  *ret = TRUE;

  return;
}

static gboolean
gst_nv_decoder_ensure_gl_context (GstNvDecoder * decoder, GstElement * videodec)
{
  gboolean ret;
  GstGLDisplay *display;
  GstGLContext *context;

  if (!gst_gl_ensure_element_data (videodec,
          (GstGLDisplay **) & decoder->gl_display,
          (GstGLContext **) & decoder->other_gl_context)) {
    GST_DEBUG_OBJECT (videodec, "No available OpenGL display");
    return FALSE;
  }

  display = GST_GL_DISPLAY (decoder->gl_display);

  if (!gst_gl_query_local_gl_context (videodec, GST_PAD_SRC,
          (GstGLContext **) & decoder->gl_context)) {
    GST_INFO_OBJECT (videodec, "failed to query local OpenGL context");

    gst_clear_object (&decoder->gl_context);
    decoder->gl_context =
        (GstObject *) gst_gl_display_get_gl_context_for_thread (display, NULL);
    if (decoder->gl_context == NULL
        || !gst_gl_display_add_context (display,
            GST_GL_CONTEXT (decoder->gl_context))) {
      gst_clear_object (&decoder->gl_context);
      if (!gst_gl_display_create_context (display,
              (GstGLContext *) decoder->other_gl_context,
              (GstGLContext **) & decoder->gl_context, NULL)) {
        GST_WARNING_OBJECT (videodec, "failed to create OpenGL context");
        return FALSE;
      }

      if (!gst_gl_display_add_context (display,
              (GstGLContext *) decoder->gl_context)) {
        GST_WARNING_OBJECT (videodec,
            "failed to add the OpenGL context to the display");
        return FALSE;
      }
    }
  }

  context = GST_GL_CONTEXT (decoder->gl_context);

  if (!gst_gl_context_check_gl_version (context, SUPPORTED_GL_APIS, 3, 0)) {
    GST_WARNING_OBJECT (videodec,
        "OpenGL context could not support PBO download");
    return FALSE;
  }

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) gst_nv_decoder_check_cuda_device_from_context,
      &ret);

  if (!ret) {
    GST_WARNING_OBJECT (videodec,
        "Current OpenGL context is not CUDA-compatible");
    return FALSE;
  }

  return TRUE;
}
#endif

gboolean
gst_nv_decoder_negotiate (GstNvDecoder * decoder,
    GstVideoDecoder * videodec, GstVideoCodecState * input_state,
    GstVideoCodecState ** output_state)
{
  GstVideoCodecState *state;
  GstVideoInfo *info;

  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), FALSE);
  g_return_val_if_fail (input_state != NULL, FALSE);
  g_return_val_if_fail (output_state != NULL, FALSE);

  if (!decoder->configured) {
    GST_ERROR_OBJECT (videodec, "Should configure decoder first");
    return FALSE;
  }

  info = &decoder->info;
  state = gst_video_decoder_set_interlaced_output_state (videodec,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_INTERLACE_MODE (info),
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info), input_state);
  state->caps = gst_video_info_to_caps (&state->info);

  if (*output_state)
    gst_video_codec_state_unref (*output_state);
  *output_state = state;

  decoder->output_type = GST_NV_DECODER_OUTPUT_TYPE_SYSTEM;

  {
    GstCaps *caps;
    caps = gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (videodec));
    GST_DEBUG_OBJECT (videodec, "Allowed caps %" GST_PTR_FORMAT, caps);

    if (!caps || gst_caps_is_any (caps)) {
      GST_DEBUG_OBJECT (videodec,
          "cannot determine output format, using system memory");
    } else {
      GstCapsFeatures *features;
      guint size = gst_caps_get_size (caps);
      guint i;
      gboolean have_cuda = FALSE;
      gboolean have_gl = FALSE;

      for (i = 0; i < size; i++) {
        features = gst_caps_get_features (caps, i);
        if (features && gst_caps_features_contains (features,
                GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
          GST_DEBUG_OBJECT (videodec, "found CUDA memory feature");
          have_cuda = TRUE;
          break;
        }
#ifdef HAVE_NVCODEC_GST_GL
        if (features && gst_caps_features_contains (features,
                GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
          GST_DEBUG_OBJECT (videodec, "found GL memory feature");
          have_gl = TRUE;
        }
#endif
      }

      if (have_cuda)
        decoder->output_type = GST_NV_DECODER_OUTPUT_TYPE_CUDA;
      else if (have_gl)
        decoder->output_type = GST_NV_DECODER_OUTPUT_TYPE_GL;
    }
    gst_clear_caps (&caps);
  }

#ifdef HAVE_NVCODEC_GST_GL
  if (decoder->output_type == GST_NV_DECODER_OUTPUT_TYPE_GL &&
      !gst_nv_decoder_ensure_gl_context (decoder, GST_ELEMENT (videodec))) {
    GST_WARNING_OBJECT (videodec,
        "OpenGL context is not CUDA-compatible, fallback to system memory");
    decoder->output_type = GST_NV_DECODER_OUTPUT_TYPE_SYSTEM;
  }
#endif

  switch (decoder->output_type) {
    case GST_NV_DECODER_OUTPUT_TYPE_CUDA:
      GST_DEBUG_OBJECT (videodec, "using CUDA memory");
      gst_caps_set_features (state->caps, 0,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, NULL));
      break;
#ifdef HAVE_NVCODEC_GST_GL
    case GST_NV_DECODER_OUTPUT_TYPE_GL:
      GST_DEBUG_OBJECT (videodec, "using GL memory");
      gst_caps_set_features (state->caps, 0,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, NULL));
      gst_caps_set_simple (state->caps, "texture-target", G_TYPE_STRING,
          "2D", NULL);
      break;
#endif
    default:
      GST_DEBUG_OBJECT (videodec, "using system memory");
      break;
  }

  return TRUE;
}

static gboolean
gst_nv_decoder_ensure_cuda_pool (GstNvDecoder * decoder, GstQuery * query)
{
  GstCaps *outcaps;
  GstBufferPool *pool = NULL;
  guint n, size, min, max;
  GstVideoInfo vinfo = { 0, };
  GstStructure *config;

  gst_query_parse_allocation (query, &outcaps, NULL);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool && !GST_IS_CUDA_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = NULL;
    }
  }

  if (!pool) {
    GST_DEBUG_OBJECT (decoder, "no downstream pool, create our pool");
    pool = gst_cuda_buffer_pool_new (decoder->context);

    if (outcaps)
      gst_video_info_from_caps (&vinfo, outcaps);
    size = (guint) vinfo.size;
    min = max = 0;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (pool, config);
  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

  return TRUE;
}

#ifdef HAVE_NVCODEC_GST_GL
static gboolean
gst_nv_decoder_ensure_gl_pool (GstNvDecoder * decoder, GstQuery * query)
{
  GstCaps *outcaps;
  GstBufferPool *pool = NULL;
  guint n, size, min, max;
  GstVideoInfo vinfo = { 0, };
  GstStructure *config;
  GstGLContext *gl_context;

  GST_DEBUG_OBJECT (decoder, "decide allocation");

  if (!decoder->gl_context) {
    GST_ERROR_OBJECT (decoder, "GL context is not available");
    return FALSE;
  }

  gl_context = GST_GL_CONTEXT (decoder->gl_context);

  gst_query_parse_allocation (query, &outcaps, NULL);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  if (pool && !GST_IS_GL_BUFFER_POOL (pool)) {
    gst_object_unref (pool);
    pool = NULL;
  }

  if (!pool) {
    GST_DEBUG_OBJECT (decoder, "no downstream pool, create our pool");
    pool = gst_gl_buffer_pool_new (GST_GL_CONTEXT (gl_context));

    if (outcaps)
      gst_video_info_from_caps (&vinfo, outcaps);
    size = (guint) vinfo.size;
    min = max = 0;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (pool, config);
  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

  return TRUE;
}
#endif

gboolean
gst_nv_decoder_decide_allocation (GstNvDecoder * decoder,
    GstVideoDecoder * videodec, GstQuery * query)
{
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (videodec, "decide allocation");

  switch (decoder->output_type) {
    case GST_NV_DECODER_OUTPUT_TYPE_SYSTEM:
      /* GstVideoDecoder will take care this case */
      break;
#ifdef HAVE_NVCODEC_GST_GL
    case GST_NV_DECODER_OUTPUT_TYPE_GL:
      ret = gst_nv_decoder_ensure_gl_pool (decoder, query);
      break;
#endif
    case GST_NV_DECODER_OUTPUT_TYPE_CUDA:
      ret = gst_nv_decoder_ensure_cuda_pool (decoder, query);
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return ret;
}
