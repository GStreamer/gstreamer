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

#ifdef HAVE_CUDA_GST_GL
#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>
#endif

#include <gst/cuda/gstcudamemory.h>
#include <gst/cuda/gstcudabufferpool.h>
#include <gst/cuda/gstcudastream.h>
#include "gstnvdecoder.h"
#include <string.h>

extern "C"
{
  GST_DEBUG_CATEGORY_EXTERN (gst_nv_decoder_debug);
}

#define GST_CAT_DEFAULT gst_nv_decoder_debug

#ifdef HAVE_CUDA_GST_GL
#define SUPPORTED_GL_APIS (GstGLAPI) (GST_GL_API_OPENGL | GST_GL_API_OPENGL3)
#endif

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

  GstNvDecObject *object;
  GstCudaContext *context;
  GstCudaStream *stream;

  GstVideoInfo info;
  GstVideoInfo coded_info;
  CUVIDDECODECREATEINFO create_info;

  gboolean alloc_aux_frame;
  gboolean configured;
  guint downstream_min_buffers;
  guint num_output_surfaces;

  GMutex lock;

  /* For OpenGL interop. */
  GstObject *gl_display;
  GstObject *gl_context;
  GstObject *other_gl_context;

  GstNvDecoderOutputType output_type;
};

static void gst_nv_decoder_dispose (GObject * object);
static void gst_nv_decoder_finalize (GObject * object);
static void gst_nv_decoder_reset_unlocked (GstNvDecoder * self);

#define parent_class gst_nv_decoder_parent_class
G_DEFINE_TYPE (GstNvDecoder, gst_nv_decoder, GST_TYPE_OBJECT);

static void
gst_nv_decoder_class_init (GstNvDecoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_nv_decoder_dispose;
  gobject_class->finalize = gst_nv_decoder_finalize;
}

static void
gst_nv_decoder_init (GstNvDecoder * self)
{
  g_mutex_init (&self->lock);
}

static void
gst_nv_decoder_dispose (GObject * object)
{
  GstNvDecoder *self = GST_NV_DECODER (object);

  gst_nv_decoder_reset_unlocked (self);

  gst_clear_cuda_stream (&self->stream);
  gst_clear_object (&self->context);
  gst_clear_object (&self->gl_display);
  gst_clear_object (&self->gl_context);
  gst_clear_object (&self->other_gl_context);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_nv_decoder_finalize (GObject * object)
{
  GstNvDecoder *self = GST_NV_DECODER (object);

  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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

GstNvDecoder *
gst_nv_decoder_new (GstCudaContext * context)
{
  GstNvDecoder *self;

  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (context), nullptr);

  self = (GstNvDecoder *) g_object_new (GST_TYPE_NV_DECODER, nullptr);
  self->context = (GstCudaContext *) gst_object_ref (context);
  gst_object_ref_sink (self);

  self->stream = gst_cuda_stream_new (self->context);
  if (!self->stream) {
    GST_WARNING_OBJECT (self,
        "Could not create CUDA stream, will use default stream");
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
gst_nv_decoder_reset_unlocked (GstNvDecoder * self)
{
  if (self->object)
    gst_nv_dec_object_set_flushing (self->object, TRUE);

  gst_clear_object (&self->object);

  self->output_type = GST_NV_DECODER_OUTPUT_TYPE_SYSTEM;
  self->configured = FALSE;
  self->downstream_min_buffers = 0;
  self->num_output_surfaces = 0;
}

gboolean
gst_nv_decoder_configure (GstNvDecoder * decoder, cudaVideoCodec codec,
    GstVideoInfo * info, gint coded_width, gint coded_height,
    guint coded_bitdepth, guint pool_size, gboolean alloc_aux_frame,
    guint num_output_surfaces, guint init_max_width, guint init_max_height)
{
  CUVIDDECODECREATEINFO create_info = { 0, };
  GstVideoFormat format, prev_format = GST_VIDEO_FORMAT_UNKNOWN;
  guint alloc_size;

  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), FALSE);
  g_return_val_if_fail (codec < cudaVideoCodec_NumCodecs, FALSE);
  g_return_val_if_fail (info != nullptr, FALSE);
  g_return_val_if_fail (coded_width >= GST_VIDEO_INFO_WIDTH (info), FALSE);
  g_return_val_if_fail (coded_height >= GST_VIDEO_INFO_HEIGHT (info), FALSE);
  g_return_val_if_fail (coded_bitdepth >= 8, FALSE);
  g_return_val_if_fail (pool_size > 0, FALSE);

  format = GST_VIDEO_INFO_FORMAT (info);
  if (decoder->info.finfo)
    prev_format = GST_VIDEO_INFO_FORMAT (&decoder->info);

  /* Additional 2 frame margin */
  pool_size += 2;

  /* Need pool size * 2 for decode-only (used for reference) frame
   * and output frame, AV1 film grain case for example */
  decoder->alloc_aux_frame = alloc_aux_frame;
  if (alloc_aux_frame) {
    alloc_size = pool_size * 2;
  } else {
    alloc_size = pool_size;
  }

  decoder->info = *info;
  gst_video_info_set_format (&decoder->coded_info, format,
      coded_width, coded_height);

  g_mutex_lock (&decoder->lock);
  if (decoder->object) {
    GST_DEBUG_OBJECT (decoder,
        "Configured max resolution %ux%u %s (bit-depth %u), "
        "new resolution %ux%u %s (bit-depth %u)",
        (guint) decoder->create_info.ulMaxWidth,
        (guint) decoder->create_info.ulMaxHeight,
        gst_video_format_to_string (prev_format),
        (guint) decoder->create_info.bitDepthMinus8 + 8,
        (guint) coded_width, (guint) coded_height,
        gst_video_format_to_string (format), coded_bitdepth);

    if (format == prev_format &&
        (guint) coded_width <= decoder->create_info.ulMaxWidth &&
        (guint) coded_height <= decoder->create_info.ulMaxHeight &&
        coded_bitdepth == (guint) decoder->create_info.bitDepthMinus8 + 8) {
      CUVIDRECONFIGUREDECODERINFO reconfig_info = { 0, };

      reconfig_info.ulWidth = GST_VIDEO_INFO_WIDTH (&decoder->coded_info);
      reconfig_info.ulHeight = GST_VIDEO_INFO_HEIGHT (&decoder->coded_info);
      reconfig_info.ulTargetWidth = GST_VIDEO_INFO_WIDTH (info);
      reconfig_info.ulTargetHeight = GST_VIDEO_INFO_HEIGHT (info);
      reconfig_info.ulNumDecodeSurfaces = alloc_size;
      reconfig_info.display_area.right = GST_VIDEO_INFO_WIDTH (info);
      reconfig_info.display_area.bottom = GST_VIDEO_INFO_HEIGHT (info);
      reconfig_info.target_rect.right = GST_VIDEO_INFO_WIDTH (info);
      reconfig_info.target_rect.bottom = GST_VIDEO_INFO_HEIGHT (info);

      if (gst_nv_dec_object_reconfigure (decoder->object,
              &reconfig_info, info, alloc_aux_frame)) {
        GST_DEBUG_OBJECT (decoder, "Reconfigured");
        decoder->configured = TRUE;
        g_mutex_unlock (&decoder->lock);
        return TRUE;
      } else {
        GST_WARNING_OBJECT (decoder,
            "Couldn't reconfigure decoder, creating new decoder instance");
      }
    } else {
      GST_DEBUG_OBJECT (decoder, "Need new decoder instance");
    }
  }

  gst_nv_decoder_reset_unlocked (decoder);
  g_mutex_unlock (&decoder->lock);

  decoder->num_output_surfaces = num_output_surfaces;

  create_info.ulWidth = GST_VIDEO_INFO_WIDTH (&decoder->coded_info);
  create_info.ulHeight = GST_VIDEO_INFO_HEIGHT (&decoder->coded_info);
  create_info.ulNumDecodeSurfaces = alloc_size;
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
  /* Will be updated on negotiate() */
  create_info.ulNumOutputSurfaces = 1;

  create_info.target_rect.left = 0;
  create_info.target_rect.top = 0;
  create_info.target_rect.right = GST_VIDEO_INFO_WIDTH (info);
  create_info.target_rect.bottom = GST_VIDEO_INFO_HEIGHT (info);

  create_info.ulMaxWidth = MAX (create_info.ulWidth, init_max_width);
  create_info.ulMaxHeight = MAX (create_info.ulHeight, init_max_height);

  decoder->create_info = create_info;
  decoder->configured = TRUE;

  return TRUE;
}

GstFlowReturn
gst_nv_decoder_acquire_surface (GstNvDecoder * decoder,
    GstNvDecSurface ** surface)
{
  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), GST_FLOW_ERROR);

  if (!decoder->object) {
    if (decoder->output_type == GST_NV_DECODER_OUTPUT_TYPE_CUDA) {
      if (decoder->num_output_surfaces == 0 ||
          decoder->num_output_surfaces < decoder->downstream_min_buffers) {
        /* Auto mode or user specified num-output-surfaces value is too small */
        decoder->create_info.ulNumOutputSurfaces =
            decoder->downstream_min_buffers + 2;
      } else {
        /* Otherwise use user provided value */
        decoder->create_info.ulNumOutputSurfaces = decoder->num_output_surfaces;
      }

      GST_INFO_OBJECT (decoder, "Updating ulNumOutputSurfaces to %u, "
          "user requested %u, min-downstream %u",
          (guint) decoder->create_info.ulNumOutputSurfaces,
          decoder->num_output_surfaces, decoder->downstream_min_buffers);
    }

    g_mutex_lock (&decoder->lock);
    decoder->object = gst_nv_dec_object_new (decoder->context,
        &decoder->create_info, &decoder->info, decoder->alloc_aux_frame);
    g_mutex_unlock (&decoder->lock);
    if (!decoder->object) {
      GST_ERROR_OBJECT (decoder, "Couldn't create decoder object");
      return GST_FLOW_ERROR;
    }
  }

  return gst_nv_dec_object_acquire_surface (decoder->object, surface);
}

gboolean
gst_nv_decoder_decode (GstNvDecoder * decoder, CUVIDPICPARAMS * params)
{
  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), FALSE);
  g_return_val_if_fail (decoder->object != nullptr, FALSE);

  return gst_nv_dec_object_decode (decoder->object, params);
}

#ifdef HAVE_CUDA_GST_GL
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

  if (gst_memory_map (mem, &info, (GstMapFlags) (GST_MAP_READ | GST_MAP_GL))) {
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

  if (!gst_cuda_context_pop (nullptr))
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
    return nullptr;
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

      return nullptr;
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
  GstNvDecSurface *surface;
  GstBuffer *output_buffer;
} GstNvDecoderCopyToGLData;

static void
gst_nv_decoder_copy_frame_to_gl_internal (GstGLContext * context,
    GstNvDecoderCopyToGLData * data)
{
  GstNvDecoder *self = data->self;
  GstNvDecSurface *surface = data->surface;
  GstCudaGraphicsResource **resources;
  guint num_resources;
  guint i;
  CUDA_MEMCPY2D copy_params = { 0, };
  GstVideoInfo *info = &self->info;
  CUstream stream = gst_cuda_stream_get_handle (self->stream);

  data->ret = TRUE;

  num_resources = gst_buffer_n_memory (data->output_buffer);
  resources = g_newa (GstCudaGraphicsResource *, num_resources);

  if (!gst_cuda_context_push (self->context)) {
    GST_WARNING_OBJECT (self, "Failed to push CUDA context");
    data->ret = FALSE;
    return;
  }

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

  copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  copy_params.srcPitch = surface->pitch;
  copy_params.dstMemoryType = CU_MEMORYTYPE_DEVICE;

  for (i = 0; i < num_resources; i++) {
    CUdeviceptr dst_ptr;
    gsize size;
    CUgraphicsResource cuda_resource =
        gst_cuda_graphics_resource_map (resources[i], nullptr,
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

    copy_params.srcDevice = surface->devptr +
        (i * surface->pitch * GST_VIDEO_INFO_HEIGHT (&self->info));
    copy_params.dstDevice = dst_ptr;
    copy_params.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

    if (!gst_cuda_result (CuMemcpy2DAsync (&copy_params, stream))) {
      GST_WARNING_OBJECT (self, "memcpy to mapped array failed");
      data->ret = FALSE;
    }
  }

  gst_cuda_result (CuStreamSynchronize (stream));

unmap_video_frame:
  for (i = 0; i < num_resources; i++) {
    gst_cuda_graphics_resource_unmap (resources[i], nullptr);
  }

  if (!gst_cuda_context_pop (nullptr))
    GST_WARNING_OBJECT (self, "Failed to pop CUDA context");
}

static gboolean
gst_nv_decoder_copy_frame_to_gl (GstNvDecoder * decoder,
    GstGLContext * context, GstNvDecSurface * surface, GstBuffer * buffer)
{
  GstNvDecoderCopyToGLData data;

  data.self = decoder;
  data.surface = surface;
  data.output_buffer = buffer;

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) gst_nv_decoder_copy_frame_to_gl_internal, &data);

  GST_LOG_OBJECT (decoder, "Copy frame to GL ret %d", data.ret);

  return data.ret;
}
#endif

static gboolean
gst_nv_decoder_copy_frame_to_system (GstNvDecoder * decoder,
    GstNvDecSurface * surface, GstBuffer * buffer)
{
  GstVideoFrame video_frame;
  CUDA_MEMCPY2D copy_params = { 0, };
  gboolean ret = FALSE;
  CUstream stream = gst_cuda_stream_get_handle (decoder->stream);

  if (!gst_video_frame_map (&video_frame, &decoder->info, buffer,
          GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (decoder, "Couldn't map video frame");
    return FALSE;
  }

  copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  copy_params.srcPitch = surface->pitch;
  copy_params.dstMemoryType = CU_MEMORYTYPE_HOST;
  copy_params.WidthInBytes = GST_VIDEO_INFO_COMP_WIDTH (&decoder->info, 0)
      * GST_VIDEO_INFO_COMP_PSTRIDE (&decoder->info, 0);

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&video_frame); i++) {
    copy_params.srcDevice = surface->devptr +
        (i * surface->pitch * GST_VIDEO_INFO_HEIGHT (&decoder->info));
    copy_params.dstHost = GST_VIDEO_FRAME_PLANE_DATA (&video_frame, i);
    copy_params.dstPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&video_frame, i);
    copy_params.Height = GST_VIDEO_FRAME_COMP_HEIGHT (&video_frame, i);

    if (!gst_cuda_result (CuMemcpy2DAsync (&copy_params, stream))) {
      GST_ERROR_OBJECT (decoder, "failed to copy %dth plane", i);
      goto done;
    }
  }

  gst_cuda_result (CuStreamSynchronize (stream));

  ret = TRUE;

done:
  gst_video_frame_unmap (&video_frame);

  GST_LOG_OBJECT (decoder, "Copy frame to system ret %d", ret);

  return ret;
}

static gboolean
gst_nv_decoder_copy_frame_to_cuda (GstNvDecoder * decoder,
    GstNvDecSurface * surface, GstBuffer * buffer, GstCudaStream * stream)
{
  CUDA_MEMCPY2D copy_params = { 0, };
  GstMemory *mem;
  gboolean ret = FALSE;
  GstVideoFrame video_frame;
  CUstream stream_handle = gst_cuda_stream_get_handle (stream);

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_cuda_memory (mem)) {
    GST_WARNING_OBJECT (decoder, "Not a CUDA memory");
    return FALSE;
  }

  if (!gst_video_frame_map (&video_frame,
          &decoder->info, buffer,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_CUDA))) {
    GST_ERROR_OBJECT (decoder, "frame map failure");
    return FALSE;
  }

  copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  copy_params.srcPitch = surface->pitch;
  copy_params.dstMemoryType = CU_MEMORYTYPE_DEVICE;

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (&decoder->info); i++) {
    copy_params.srcDevice = surface->devptr +
        (i * surface->pitch * GST_VIDEO_INFO_HEIGHT (&decoder->info));
    copy_params.dstDevice =
        (CUdeviceptr) GST_VIDEO_FRAME_PLANE_DATA (&video_frame, i);
    copy_params.dstPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&video_frame, i);
    copy_params.WidthInBytes = GST_VIDEO_INFO_COMP_WIDTH (&decoder->info, 0)
        * GST_VIDEO_INFO_COMP_PSTRIDE (&decoder->info, 0);
    copy_params.Height = GST_VIDEO_INFO_COMP_HEIGHT (&decoder->info, i);

    if (!gst_cuda_result (CuMemcpy2DAsync (&copy_params, stream_handle))) {
      GST_ERROR_OBJECT (decoder, "failed to copy %dth plane", i);
      goto done;
    }
  }

  /* Don't sync if we are using downstream memory's stream */
  if (!stream)
    gst_cuda_result (CuStreamSynchronize (nullptr));
  else
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);

  ret = TRUE;

done:
  gst_video_frame_unmap (&video_frame);

  GST_LOG_OBJECT (decoder, "Copy frame to CUDA ret %d", ret);

  return ret;
}

GstFlowReturn
gst_nv_decoder_finish_surface (GstNvDecoder * decoder,
    GstVideoDecoder * videodec, GstVideoCodecState * input_state,
    GstNvDecSurface * surface, GstBuffer ** buffer)
{
  GstBuffer *outbuf = nullptr;
  gboolean ret = FALSE;
  GstCudaStream *stream;
  GstFlowReturn flow_ret;

  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), GST_FLOW_ERROR);
  g_return_val_if_fail (decoder->object != nullptr, GST_FLOW_ERROR);
  g_return_val_if_fail (surface != nullptr, GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != nullptr, GST_FLOW_ERROR);

  if (input_state) {
    if (!gst_nv_decoder_negotiate (decoder, videodec, input_state)) {
      GST_ERROR_OBJECT (videodec, "Couldn't re-negotiate with updated state");
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  if (!gst_cuda_context_push (decoder->context)) {
    GST_ERROR_OBJECT (decoder, "Couldn't push context");
    return GST_FLOW_ERROR;
  }

  stream = decoder->stream;
  flow_ret = gst_nv_dec_object_map_surface (decoder->object, surface, stream);
  if (flow_ret != GST_FLOW_OK) {
    gst_cuda_context_pop (nullptr);
    return flow_ret;
  }

  if (decoder->output_type == GST_NV_DECODER_OUTPUT_TYPE_CUDA &&
      (guint) decoder->create_info.ulNumOutputSurfaces >=
      decoder->downstream_min_buffers) {
    GstMemory *mem;
    GstCudaMemory *cmem;
    GstBuffer *buf;
    GstVideoInfo *info = &decoder->info;

    flow_ret = gst_nv_dec_object_export_surface (decoder->object,
        surface, stream, &mem);
    if (flow_ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (decoder, "Couldn't export surface");
      gst_nv_dec_object_unmap_surface (decoder->object, surface);
      gst_cuda_context_pop (nullptr);
      return flow_ret;
    }

    gst_cuda_context_pop (nullptr);

    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);

    if (stream)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);

    buf = gst_buffer_new ();
    cmem = GST_CUDA_MEMORY_CAST (mem);
    gst_buffer_append_memory (buf, mem);
    gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        cmem->info.offset, cmem->info.stride);

    *buffer = buf;
    return GST_FLOW_OK;
  }

  outbuf = gst_video_decoder_allocate_output_buffer (videodec);
  if (!outbuf) {
    GST_ERROR_OBJECT (videodec, "Couldn't allocate output buffer");
    gst_nv_dec_object_unmap_surface (decoder->object, surface);
    gst_cuda_context_pop (nullptr);
    return GST_FLOW_ERROR;
  }

  switch (decoder->output_type) {
    case GST_NV_DECODER_OUTPUT_TYPE_SYSTEM:
      ret = gst_nv_decoder_copy_frame_to_system (decoder, surface, outbuf);
      break;
#ifdef HAVE_CUDA_GST_GL
    case GST_NV_DECODER_OUTPUT_TYPE_GL:
      g_assert (decoder->gl_context != nullptr);

      ret = gst_nv_decoder_copy_frame_to_gl (decoder,
          GST_GL_CONTEXT (decoder->gl_context), surface, outbuf);
      break;
#endif
    case GST_NV_DECODER_OUTPUT_TYPE_CUDA:
      ret = gst_nv_decoder_copy_frame_to_cuda (decoder,
          surface, outbuf, stream);
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

    ret = gst_nv_decoder_copy_frame_to_system (decoder, surface, outbuf);
  }

  gst_nv_dec_object_unmap_surface (decoder->object, surface);
  gst_cuda_context_pop (nullptr);

  if (!ret) {
    GST_WARNING_OBJECT (videodec, "Failed to copy frame");
    goto error;
  }

  *buffer = outbuf;

  return GST_FLOW_OK;

error:
  gst_nv_dec_object_unmap_surface (decoder->object, surface);
  gst_clear_buffer (&outbuf);
  return GST_FLOW_ERROR;
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
    case cudaVideoCodec_AV1:
      g_value_set_static_string (&val, "main");
      gst_value_list_append_value (profiles, &val);
      ret = TRUE;
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
  {cudaVideoCodec_VP9, "vp9", "video/x-vp9"},
  {cudaVideoCodec_AV1, "av1", "video/x-av1, alignment = (string) frame"}
};

gboolean
gst_nv_decoder_check_device_caps (CUcontext cuda_ctx, cudaVideoCodec codec,
    GstCaps ** sink_template, GstCaps ** src_template)
{
  CUresult cuda_ret;
  guint max_width = 0, min_width = G_MAXINT;
  guint max_height = 0, min_height = G_MAXINT;
  GstCaps *sink_templ = nullptr;
  GstCaps *src_templ = nullptr;
  /* FIXME: support 12bits format */
  guint bitdepth_minus8[3] = { 0, 2, 4 };
  GstNvDecoderFormatFlags format_flags = (GstNvDecoderFormatFlags) 0;
  guint c_idx, b_idx;
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
  const GstNvdecoderCodecMap *codec_map = nullptr;
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

#ifdef HAVE_CUDA_GST_GL
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
      CUVIDDECODECAPS decoder_caps;
      GstNvDecoderFormatFlags cur_flag = (GstNvDecoderFormatFlags) 0;

      memset (&decoder_caps, 0, sizeof (CUVIDDECODECAPS));

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

      format_flags = (GstNvDecoderFormatFlags) (format_flags |
          (cur_flag << (bitdepth_minus8[b_idx] / 2)));

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
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, nullptr);

  gst_caps_set_value (src_templ, "format", &format_list);

  {
    GstCaps *cuda_caps = gst_caps_copy (src_templ);
    gst_caps_set_features_simple (cuda_caps,
        gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY));

    /* OpenGL specific */
#ifdef HAVE_CUDA_GST_GL
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
      "height", GST_TYPE_INT_RANGE, min_height, max_height, nullptr);

  if (gst_nv_decoder_get_supported_codec_profiles (&profile_list, codec,
          format_flags)) {
    gst_caps_set_value (sink_templ, "profile", &profile_list);
  }

  GST_DEBUG ("sink template caps %" GST_PTR_FORMAT, sink_templ);
  GST_DEBUG ("src template caps %" GST_PTR_FORMAT, src_templ);

  CuCtxPopCurrent (nullptr);

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
  for (guint i = 0; i < G_N_ELEMENTS (codec_map_list); i++) {
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

#ifdef HAVE_CUDA_GST_GL
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

#ifdef HAVE_CUDA_GST_GL
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

#ifdef HAVE_CUDA_GST_GL
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
        (GstObject *) gst_gl_display_get_gl_context_for_thread (display,
        nullptr);
    if (decoder->gl_context == nullptr
        || !gst_gl_display_add_context (display,
            GST_GL_CONTEXT (decoder->gl_context))) {
      gst_clear_object (&decoder->gl_context);
      if (!gst_gl_display_create_context (display,
              (GstGLContext *) decoder->other_gl_context,
              (GstGLContext **) & decoder->gl_context, nullptr)) {
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
    GstVideoDecoder * videodec, GstVideoCodecState * input_state)
{
  GstVideoCodecState *state;
  GstVideoInfo *info;

  g_return_val_if_fail (GST_IS_NV_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), FALSE);
  g_return_val_if_fail (input_state != nullptr, FALSE);

  if (!decoder->configured) {
    GST_ERROR_OBJECT (videodec, "Should configure decoder first");
    return FALSE;
  }

  info = &decoder->info;
  state = gst_video_decoder_set_interlaced_output_state (videodec,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_INTERLACE_MODE (info),
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info), input_state);
  state->caps = gst_video_info_to_caps (&state->info);

  /* decoder baseclass will hold other reference to output state */
  gst_video_codec_state_unref (state);

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
#ifdef HAVE_CUDA_GST_GL
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

#ifdef HAVE_CUDA_GST_GL
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
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, nullptr));
      break;
#ifdef HAVE_CUDA_GST_GL
    case GST_NV_DECODER_OUTPUT_TYPE_GL:
      GST_DEBUG_OBJECT (videodec, "using GL memory");
      gst_caps_set_features (state->caps, 0,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, nullptr));
      gst_caps_set_simple (state->caps, "texture-target", G_TYPE_STRING,
          "2D", nullptr);
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
  GstBufferPool *pool = nullptr;
  guint n, size, min = 0, max = 0;
  GstVideoInfo vinfo = { 0, };
  GstStructure *config;
  GstCudaStream *stream;

  gst_query_parse_allocation (query, &outcaps, nullptr);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool && !GST_IS_CUDA_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = nullptr;
    }
  }

  if (!pool) {
    GST_DEBUG_OBJECT (decoder, "no downstream pool, create our pool");
    pool = gst_cuda_buffer_pool_new (decoder->context);

    if (outcaps)
      gst_video_info_from_caps (&vinfo, outcaps);
    size = (guint) vinfo.size;
  }

  config = gst_buffer_pool_get_config (pool);
  stream = gst_buffer_pool_config_get_cuda_stream (config);
  if (stream) {
    GST_DEBUG_OBJECT (decoder, "Downstream CUDA stream is available");
    gst_clear_cuda_stream (&decoder->stream);
    decoder->stream = stream;
  } else if (decoder->stream) {
    GST_DEBUG_OBJECT (decoder,
        "Downstream CUDA stream is not available, use ours");
    gst_buffer_pool_config_set_cuda_stream (config, decoder->stream);
  }

  decoder->downstream_min_buffers = min;
  GST_DEBUG_OBJECT (decoder, "Downstream min buffers %d", min);

  /* Since we don't use downstream buffer pool, pre-allocation is unnecessary */
  min = 0;

  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_set_config (pool, config);

  /* Get updated size by cuda buffer pool */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

  return TRUE;
}

#ifdef HAVE_CUDA_GST_GL
static gboolean
gst_nv_decoder_ensure_gl_pool (GstNvDecoder * decoder, GstQuery * query)
{
  GstCaps *outcaps;
  GstBufferPool *pool = nullptr;
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

  gst_query_parse_allocation (query, &outcaps, nullptr);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  if (pool && !GST_IS_GL_BUFFER_POOL (pool)) {
    gst_object_unref (pool);
    pool = nullptr;
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
#ifdef HAVE_CUDA_GST_GL
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

void
gst_nv_decoder_set_flushing (GstNvDecoder * decoder, gboolean flushing)
{
  g_mutex_lock (&decoder->lock);
  if (decoder->object)
    gst_nv_dec_object_set_flushing (decoder->object, flushing);
  g_mutex_unlock (&decoder->lock);
}

void
gst_nv_decoder_reset (GstNvDecoder * decoder)
{
  g_mutex_lock (&decoder->lock);
  gst_nv_decoder_reset_unlocked (decoder);
  g_mutex_unlock (&decoder->lock);
}

guint
gst_nv_decoder_get_max_output_size (guint coded_size, guint user_requested,
    guint device_max)
{
  if (user_requested <= coded_size)
    return coded_size;

  user_requested = GST_ROUND_UP_16 (user_requested);

  return MIN (user_requested, device_max);
}
