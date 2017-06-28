/*
 * Copyright (C) 2017 Ericsson AB. All rights reserved.
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvdec.h"

#include <cudaGL.h>

typedef enum
{
  GST_NVDEC_QUEUE_ITEM_TYPE_SEQUENCE,
  GST_NVDEC_QUEUE_ITEM_TYPE_DECODE,
  GST_NVDEC_QUEUE_ITEM_TYPE_DISPLAY
} GstNvDecQueueItemType;

typedef struct _GstNvDecQueueItem
{
  GstNvDecQueueItemType type;
  gpointer data;
} GstNvDecQueueItem;

GST_DEBUG_CATEGORY_STATIC (gst_nvdec_debug_category);
#define GST_CAT_DEFAULT gst_nvdec_debug_category

static inline gboolean
cuda_OK (CUresult result)
{
  const gchar *error_name, *error_text;

  if (result != CUDA_SUCCESS) {
    cuGetErrorName (result, &error_name);
    cuGetErrorString (result, &error_text);
    GST_WARNING ("CUDA call failed: %s, %s", error_name, error_text);
    return FALSE;
  }

  return TRUE;
}

G_DEFINE_TYPE (GstNvDecCudaContext, gst_nvdec_cuda_context, G_TYPE_OBJECT);

static void
gst_nvdec_cuda_context_finalize (GObject * object)
{
  GstNvDecCudaContext *self = (GstNvDecCudaContext *) object;

  if (self->lock) {
    GST_DEBUG ("destroying CUDA context lock");
    if (cuda_OK (cuvidCtxLockDestroy (self->lock)))
      self->lock = NULL;
    else
      GST_ERROR ("failed to destroy CUDA context lock");
  }

  if (self->context) {
    GST_DEBUG ("destroying CUDA context");
    if (cuda_OK (cuCtxDestroy (self->context)))
      self->context = NULL;
    else
      GST_ERROR ("failed to destroy CUDA context");
  }

  G_OBJECT_CLASS (gst_nvdec_cuda_context_parent_class)->finalize (object);
}

static void
gst_nvdec_cuda_context_class_init (GstNvDecCudaContextClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_nvdec_cuda_context_finalize;
}

static void
gst_nvdec_cuda_context_init (GstNvDecCudaContext * self)
{
  if (!cuda_OK (cuInit (0)))
    GST_ERROR ("failed to init CUDA");

  if (!cuda_OK (cuCtxCreate (&self->context, CU_CTX_SCHED_AUTO, 0)))
    GST_ERROR ("failed to create CUDA context");

  if (!cuda_OK (cuCtxPopCurrent (NULL)))
    GST_ERROR ("failed to pop current CUDA context");

  if (!cuda_OK (cuvidCtxLockCreate (&self->lock, self->context)))
    GST_ERROR ("failed to create CUDA context lock");
}

typedef struct _GstNvDecCudaGraphicsResourceInfo
{
  GstGLContext *gl_context;
  GstNvDecCudaContext *cuda_context;
  CUgraphicsResource resource;
} GstNvDecCudaGraphicsResourceInfo;

static void
register_cuda_resource (GstGLContext * context, gpointer * args)
{
  GstMemory *mem = GST_MEMORY_CAST (args[0]);
  GstNvDecCudaGraphicsResourceInfo *cgr_info =
      (GstNvDecCudaGraphicsResourceInfo *) args[1];
  GstMapInfo map_info = GST_MAP_INFO_INIT;
  guint texture_id;

  if (!cuda_OK (cuvidCtxLock (cgr_info->cuda_context->lock, 0)))
    GST_WARNING ("failed to lock CUDA context");

  if (gst_memory_map (mem, &map_info, GST_MAP_READ | GST_MAP_GL)) {
    texture_id = *(guint *) map_info.data;

    if (!cuda_OK (cuGraphicsGLRegisterImage (&cgr_info->resource, texture_id,
                GL_TEXTURE_2D, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD)))
      GST_WARNING ("failed to register texture with CUDA");

    gst_memory_unmap (mem, &map_info);
  } else
    GST_WARNING ("failed to map memory");

  if (!cuda_OK (cuvidCtxUnlock (cgr_info->cuda_context->lock, 0)))
    GST_WARNING ("failed to unlock CUDA context");
}

static void
unregister_cuda_resource (GstGLContext * context,
    GstNvDecCudaGraphicsResourceInfo * cgr_info)
{
  if (!cuda_OK (cuvidCtxLock (cgr_info->cuda_context->lock, 0)))
    GST_WARNING ("failed to lock CUDA context");

  if (!cuda_OK (cuGraphicsUnregisterResource ((const CUgraphicsResource)
              cgr_info->resource)))
    GST_WARNING ("failed to unregister resource");

  if (!cuda_OK (cuvidCtxUnlock (cgr_info->cuda_context->lock, 0)))
    GST_WARNING ("failed to unlock CUDA context");
}

static void
free_cgr_info (GstNvDecCudaGraphicsResourceInfo * cgr_info)
{
  gst_gl_context_thread_add (cgr_info->gl_context,
      (GstGLContextThreadFunc) unregister_cuda_resource, cgr_info);
  gst_object_unref (cgr_info->gl_context);
  g_object_unref (cgr_info->cuda_context);
  g_slice_free (GstNvDecCudaGraphicsResourceInfo, cgr_info);
}

static CUgraphicsResource
ensure_cuda_graphics_resource (GstMemory * mem,
    GstNvDecCudaContext * cuda_context)
{
  static GQuark quark = 0;
  GstNvDecCudaGraphicsResourceInfo *cgr_info;
  gpointer args[2];

  if (!gst_is_gl_base_memory (mem)) {
    GST_WARNING ("memory is not GL base memory");
    return NULL;
  }

  if (!quark)
    quark = g_quark_from_static_string ("GstNvDecCudaGraphicsResourceInfo");

  cgr_info = gst_mini_object_get_qdata (GST_MINI_OBJECT (mem), quark);
  if (!cgr_info) {
    cgr_info = g_slice_new (GstNvDecCudaGraphicsResourceInfo);
    cgr_info->gl_context =
        gst_object_ref (GST_GL_BASE_MEMORY_CAST (mem)->context);
    cgr_info->cuda_context = g_object_ref (cuda_context);
    args[0] = mem;
    args[1] = cgr_info;
    gst_gl_context_thread_add (cgr_info->gl_context,
        (GstGLContextThreadFunc) register_cuda_resource, args);
    gst_mini_object_set_qdata (GST_MINI_OBJECT (mem), quark, cgr_info,
        (GDestroyNotify) free_cgr_info);
  }

  return cgr_info->resource;
}

static gboolean gst_nvdec_start (GstVideoDecoder * decoder);
static gboolean gst_nvdec_stop (GstVideoDecoder * decoder);
static gboolean gst_nvdec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_nvdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_nvdec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);
static void gst_nvdec_set_context (GstElement * element, GstContext * context);
static gboolean gst_nvdec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);

static GstStaticPadTemplate gst_nvdec_sink_template =
    GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, stream-format=byte-stream, alignment=au; "
        "video/x-h265, stream-format=byte-stream, alignment=au; "
        "video/mpeg, mpegversion={ 1, 2, 4 }, systemstream=false; "
        "image/jpeg")
    );

static GstStaticPadTemplate gst_nvdec_src_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, "NV12") ", texture-target=2D")
    );

G_DEFINE_TYPE_WITH_CODE (GstNvDec, gst_nvdec, GST_TYPE_VIDEO_DECODER,
    GST_DEBUG_CATEGORY_INIT (gst_nvdec_debug_category, "nvdec", 0,
        "Debug category for the nvdec element"));

static void
gst_nvdec_class_init (GstNvDecClass * klass)
{
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_nvdec_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_nvdec_src_template);

  gst_element_class_set_static_metadata (element_class, "NVDEC video decoder",
      "Decoder/Video", "NVDEC video decoder",
      "Ericsson AB, http://www.ericsson.com");

  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_nvdec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_nvdec_stop);
  video_decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_nvdec_set_format);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_nvdec_handle_frame);
  video_decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_nvdec_decide_allocation);
  video_decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_nvdec_src_query);

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_nvdec_set_context);
}

static void
gst_nvdec_init (GstNvDec * nvdec)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (nvdec), TRUE);
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (nvdec), TRUE);
}

static gboolean
parser_sequence_callback (GstNvDec * nvdec, CUVIDEOFORMAT * format)
{
  GstNvDecQueueItem *item;
  guint width, height;
  CUVIDDECODECREATEINFO create_info = { 0, };
  gboolean ret = TRUE;

  width = format->display_area.right - format->display_area.left;
  height = format->display_area.bottom - format->display_area.top;
  GST_DEBUG_OBJECT (nvdec, "width: %u, height: %u", width, height);

  if (!nvdec->decoder || (nvdec->width != width || nvdec->height != height)) {
    if (!cuda_OK (cuvidCtxLock (nvdec->cuda_context->lock, 0))) {
      GST_ERROR_OBJECT (nvdec, "failed to lock CUDA context");
      return FALSE;
    }

    if (nvdec->decoder) {
      GST_DEBUG_OBJECT (nvdec, "destroying decoder");
      if (!cuda_OK (cuvidDestroyDecoder (nvdec->decoder))) {
        GST_ERROR_OBJECT (nvdec, "failed to destroy decoder");
        ret = FALSE;
      } else
        nvdec->decoder = NULL;
    }

    GST_DEBUG_OBJECT (nvdec, "creating decoder");
    create_info.ulWidth = width;
    create_info.ulHeight = height;
    create_info.ulNumDecodeSurfaces = 20;
    create_info.CodecType = format->codec;
    create_info.ChromaFormat = format->chroma_format;
    create_info.ulCreationFlags = cudaVideoCreate_Default;
    create_info.display_area.left = format->display_area.left;
    create_info.display_area.top = format->display_area.top;
    create_info.display_area.right = format->display_area.right;
    create_info.display_area.bottom = format->display_area.bottom;
    create_info.OutputFormat = cudaVideoSurfaceFormat_NV12;
    create_info.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
    create_info.ulTargetWidth = width;
    create_info.ulTargetHeight = height;
    create_info.ulNumOutputSurfaces = 1;
    create_info.vidLock = nvdec->cuda_context->lock;
    create_info.target_rect.left = 0;
    create_info.target_rect.top = 0;
    create_info.target_rect.right = width;
    create_info.target_rect.bottom = height;

    if (nvdec->decoder
        || !cuda_OK (cuvidCreateDecoder (&nvdec->decoder, &create_info))) {
      GST_ERROR_OBJECT (nvdec, "failed to create decoder");
      ret = FALSE;
    }

    if (!cuda_OK (cuvidCtxUnlock (nvdec->cuda_context->lock, 0))) {
      GST_ERROR_OBJECT (nvdec, "failed to unlock CUDA context");
      ret = FALSE;
    }
  }

  item = g_slice_new (GstNvDecQueueItem);
  item->type = GST_NVDEC_QUEUE_ITEM_TYPE_SEQUENCE;
  item->data = g_memdup (format, sizeof (CUVIDEOFORMAT));
  g_async_queue_push (nvdec->decode_queue, item);

  return ret;
}

static gboolean
parser_decode_callback (GstNvDec * nvdec, CUVIDPICPARAMS * params)
{
  GstNvDecQueueItem *item;

  GST_LOG_OBJECT (nvdec, "picture index: %u", params->CurrPicIdx);

  if (!cuda_OK (cuvidCtxLock (nvdec->cuda_context->lock, 0)))
    GST_WARNING_OBJECT (nvdec, "failed to lock CUDA context");

  if (!cuda_OK (cuvidDecodePicture (nvdec->decoder, params)))
    GST_WARNING_OBJECT (nvdec, "failed to decode picture");

  if (!cuda_OK (cuvidCtxUnlock (nvdec->cuda_context->lock, 0)))
    GST_WARNING_OBJECT (nvdec, "failed to unlock CUDA context");

  item = g_slice_new (GstNvDecQueueItem);
  item->type = GST_NVDEC_QUEUE_ITEM_TYPE_DECODE;
  item->data = g_memdup (params, sizeof (CUVIDPICPARAMS));
  ((CUVIDPICPARAMS *) item->data)->pBitstreamData = NULL;
  ((CUVIDPICPARAMS *) item->data)->pSliceDataOffsets = NULL;
  g_async_queue_push (nvdec->decode_queue, item);

  return TRUE;
}

static gboolean
parser_display_callback (GstNvDec * nvdec, CUVIDPARSERDISPINFO * dispinfo)
{
  GstNvDecQueueItem *item;

  GST_LOG_OBJECT (nvdec, "picture index: %u", dispinfo->picture_index);

  item = g_slice_new (GstNvDecQueueItem);
  item->type = GST_NVDEC_QUEUE_ITEM_TYPE_DISPLAY;
  item->data = g_memdup (dispinfo, sizeof (CUVIDPARSERDISPINFO));
  g_async_queue_push (nvdec->decode_queue, item);

  return TRUE;
}

static gboolean
gst_nvdec_start (GstVideoDecoder * decoder)
{
  GstNvDec *nvdec = GST_NVDEC (decoder);

  GST_DEBUG_OBJECT (nvdec, "creating CUDA context");
  nvdec->cuda_context = g_object_new (gst_nvdec_cuda_context_get_type (), NULL);
  nvdec->decode_queue = g_async_queue_new ();

  if (!nvdec->cuda_context->context || !nvdec->cuda_context->lock) {
    GST_ERROR_OBJECT (nvdec, "failed to create CUDA context or lock");
    return FALSE;
  }

  return TRUE;
}

static gboolean
maybe_destroy_decoder_and_parser (GstNvDec * nvdec)
{
  gboolean ret = TRUE;

  if (!cuda_OK (cuvidCtxLock (nvdec->cuda_context->lock, 0))) {
    GST_ERROR_OBJECT (nvdec, "failed to lock CUDA context");
    return FALSE;
  }

  if (nvdec->decoder) {
    GST_DEBUG_OBJECT (nvdec, "destroying decoder");
    ret = cuda_OK (cuvidDestroyDecoder (nvdec->decoder));
    if (ret)
      nvdec->decoder = NULL;
    else
      GST_ERROR_OBJECT (nvdec, "failed to destroy decoder");
  }

  if (!cuda_OK (cuvidCtxUnlock (nvdec->cuda_context->lock, 0))) {
    GST_ERROR_OBJECT (nvdec, "failed to unlock CUDA context");
    return FALSE;
  }

  if (nvdec->parser) {
    GST_DEBUG_OBJECT (nvdec, "destroying parser");
    if (!cuda_OK (cuvidDestroyVideoParser (nvdec->parser))) {
      GST_ERROR_OBJECT (nvdec, "failed to destroy parser");
      return FALSE;
    }
    nvdec->parser = NULL;
  }

  return ret;
}

static gboolean
gst_nvdec_stop (GstVideoDecoder * decoder)
{
  GstNvDec *nvdec = GST_NVDEC (decoder);
  GstNvDecQueueItem *item;

  GST_DEBUG_OBJECT (nvdec, "stop");

  if (!maybe_destroy_decoder_and_parser (nvdec))
    return FALSE;

  if (nvdec->cuda_context) {
    g_object_unref (nvdec->cuda_context);
    nvdec->cuda_context = NULL;
  }

  if (nvdec->gl_context) {
    gst_object_unref (nvdec->gl_context);
    nvdec->gl_context = NULL;
  }

  if (nvdec->other_gl_context) {
    gst_object_unref (nvdec->other_gl_context);
    nvdec->other_gl_context = NULL;
  }

  if (nvdec->gl_display) {
    gst_object_unref (nvdec->gl_display);
    nvdec->gl_display = NULL;
  }

  if (nvdec->input_state) {
    gst_video_codec_state_unref (nvdec->input_state);
    nvdec->input_state = NULL;
  }

  if (nvdec->decode_queue) {
    if (g_async_queue_length (nvdec->decode_queue) > 0) {
      GST_INFO_OBJECT (nvdec, "decode queue not empty");

      while ((item = g_async_queue_try_pop (nvdec->decode_queue))) {
        g_free (item->data);
        g_slice_free (GstNvDecQueueItem, item);
      }
    }
    g_async_queue_unref (nvdec->decode_queue);
    nvdec->decode_queue = NULL;
  }

  return TRUE;
}

static gboolean
gst_nvdec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstNvDec *nvdec = GST_NVDEC (decoder);
  GstStructure *s;
  const gchar *caps_name;
  gint mpegversion = 0;
  CUVIDPARSERPARAMS parser_params = { 0, };

  GST_DEBUG_OBJECT (nvdec, "set format");

  if (nvdec->input_state)
    gst_video_codec_state_unref (nvdec->input_state);

  nvdec->input_state = gst_video_codec_state_ref (state);

  if (!maybe_destroy_decoder_and_parser (nvdec))
    return FALSE;

  s = gst_caps_get_structure (state->caps, 0);
  caps_name = gst_structure_get_name (s);
  GST_DEBUG_OBJECT (nvdec, "codec is %s", caps_name);

  if (!g_strcmp0 (caps_name, "video/mpeg")) {
    if (gst_structure_get_int (s, "mpegversion", &mpegversion)) {
      switch (mpegversion) {
        case 1:
          parser_params.CodecType = cudaVideoCodec_MPEG1;
          break;
        case 2:
          parser_params.CodecType = cudaVideoCodec_MPEG2;
          break;
        case 4:
          parser_params.CodecType = cudaVideoCodec_MPEG4;
          break;
      }
    }
    if (!mpegversion) {
      GST_ERROR_OBJECT (nvdec, "could not get MPEG version");
      return FALSE;
    }
  } else if (!g_strcmp0 (caps_name, "video/x-h264")) {
    parser_params.CodecType = cudaVideoCodec_H264;
  } else if (!g_strcmp0 (caps_name, "image/jpeg")) {
    parser_params.CodecType = cudaVideoCodec_JPEG;
  } else if (!g_strcmp0 (caps_name, "video/x-h265")) {
    parser_params.CodecType = cudaVideoCodec_HEVC;
  } else {
    GST_ERROR_OBJECT (nvdec, "failed to determine codec type");
    return FALSE;
  }

  parser_params.ulMaxNumDecodeSurfaces = 20;
  parser_params.ulErrorThreshold = 100;
  parser_params.ulMaxDisplayDelay = 0;
  parser_params.ulClockRate = GST_SECOND;
  parser_params.pUserData = nvdec;
  parser_params.pfnSequenceCallback =
      (PFNVIDSEQUENCECALLBACK) parser_sequence_callback;
  parser_params.pfnDecodePicture =
      (PFNVIDDECODECALLBACK) parser_decode_callback;
  parser_params.pfnDisplayPicture =
      (PFNVIDDISPLAYCALLBACK) parser_display_callback;

  GST_DEBUG_OBJECT (nvdec, "creating parser");
  if (!cuda_OK (cuvidCreateVideoParser (&nvdec->parser, &parser_params))) {
    GST_ERROR_OBJECT (nvdec, "failed to create parser");
    return FALSE;
  }

  return TRUE;
}

static void
copy_video_frame_to_gl_textures (GstGLContext * context, gpointer * args)
{
  GstNvDec *nvdec = GST_NVDEC (args[0]);
  CUVIDPARSERDISPINFO *dispinfo = (CUVIDPARSERDISPINFO *) args[1];
  CUgraphicsResource *resources = (CUgraphicsResource *) args[2];
  guint num_resources = GPOINTER_TO_UINT (args[3]);
  CUVIDPROCPARAMS proc_params = { 0, };
  CUdeviceptr dptr;
  CUarray array;
  guint pitch, i;
  CUDA_MEMCPY2D mcpy2d = { 0, };

  GST_LOG_OBJECT (nvdec, "picture index: %u", dispinfo->picture_index);

  proc_params.progressive_frame = dispinfo->progressive_frame;
  proc_params.top_field_first = dispinfo->top_field_first;
  proc_params.unpaired_field = dispinfo->repeat_first_field == -1;

  if (!cuda_OK (cuvidCtxLock (nvdec->cuda_context->lock, 0))) {
    GST_WARNING_OBJECT (nvdec, "failed to lock CUDA context");
    return;
  }

  if (!cuda_OK (cuvidMapVideoFrame (nvdec->decoder, dispinfo->picture_index,
              &dptr, &pitch, &proc_params))) {
    GST_WARNING_OBJECT (nvdec, "failed to map CUDA video frame");
    goto unlock_cuda_context;
  }

  if (!cuda_OK (cuGraphicsMapResources (num_resources, resources, NULL))) {
    GST_WARNING_OBJECT (nvdec, "failed to map CUDA resources");
    goto unmap_video_frame;
  }

  mcpy2d.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  mcpy2d.srcPitch = pitch;
  mcpy2d.dstMemoryType = CU_MEMORYTYPE_ARRAY;
  mcpy2d.dstPitch = nvdec->width;
  mcpy2d.WidthInBytes = nvdec->width;

  for (i = 0; i < num_resources; i++) {
    if (!cuda_OK (cuGraphicsSubResourceGetMappedArray (&array, resources[i], 0,
                0))) {
      GST_WARNING_OBJECT (nvdec, "failed to map CUDA array");
      break;
    }

    mcpy2d.srcDevice = dptr + (i * pitch * nvdec->height);
    mcpy2d.dstArray = array;
    mcpy2d.Height = nvdec->height / (i + 1);

    if (!cuda_OK (cuMemcpy2D (&mcpy2d)))
      GST_WARNING_OBJECT (nvdec, "memcpy to mapped array failed");
  }

  if (!cuda_OK (cuGraphicsUnmapResources (num_resources, resources, NULL)))
    GST_WARNING_OBJECT (nvdec, "failed to unmap CUDA resources");

unmap_video_frame:
  if (!cuda_OK (cuvidUnmapVideoFrame (nvdec->decoder, dptr)))
    GST_WARNING_OBJECT (nvdec, "failed to unmap CUDA video frame");

unlock_cuda_context:
  if (!cuda_OK (cuvidCtxUnlock (nvdec->cuda_context->lock, 0)))
    GST_WARNING_OBJECT (nvdec, "failed to unlock CUDA context");
}

static GstFlowReturn
handle_pending_frames (GstNvDec * nvdec)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (nvdec);
  GList *pending_frames, *list, *tmp;
  GstVideoCodecFrame *pending_frame;
  guint frame_number;
  GstClockTime latency = 0;
  GstNvDecQueueItem *item;
  CUVIDEOFORMAT *format;
  GstVideoCodecState *state;
  guint width, height, fps_n, fps_d, i, num_resources;
  CUVIDPICPARAMS *decode_params;
  CUVIDPARSERDISPINFO *dispinfo;
  CUgraphicsResource *resources;
  gpointer args[4];
  GstMemory *mem;
  GstFlowReturn ret = GST_FLOW_OK;

  /* find the oldest unused, unfinished frame */
  pending_frames = list = gst_video_decoder_get_frames (decoder);
  for (; pending_frames; pending_frames = pending_frames->next) {
    pending_frame = pending_frames->data;
    frame_number =
        GPOINTER_TO_UINT (gst_video_codec_frame_get_user_data (pending_frame));
    if (!frame_number)
      break;
    latency += pending_frame->duration;
  }

  while (ret == GST_FLOW_OK && pending_frames
      && (item =
          (GstNvDecQueueItem *) g_async_queue_try_pop (nvdec->decode_queue))) {
    switch (item->type) {
      case GST_NVDEC_QUEUE_ITEM_TYPE_SEQUENCE:
        if (!nvdec->decoder) {
          GST_ERROR_OBJECT (nvdec, "no decoder");
          ret = GST_FLOW_ERROR;
          break;
        }

        format = (CUVIDEOFORMAT *) item->data;
        width = format->display_area.right - format->display_area.left;
        height = format->display_area.bottom - format->display_area.top;
        fps_n = format->frame_rate.numerator;
        fps_d = MAX (1, format->frame_rate.denominator);

        if (!gst_pad_has_current_caps (GST_VIDEO_DECODER_SRC_PAD (decoder))
            || width != nvdec->width || height != nvdec->height
            || fps_n != nvdec->fps_n || fps_d != nvdec->fps_d) {
          nvdec->width = width;
          nvdec->height = height;
          nvdec->fps_n = fps_n;
          nvdec->fps_d = fps_d;

          state = gst_video_decoder_set_output_state (decoder,
              GST_VIDEO_FORMAT_NV12, nvdec->width, nvdec->height,
              nvdec->input_state);
          state->caps = gst_caps_new_simple ("video/x-raw",
              "format", G_TYPE_STRING, "NV12",
              "width", G_TYPE_INT, nvdec->width,
              "height", G_TYPE_INT, nvdec->height,
              "framerate", GST_TYPE_FRACTION, nvdec->fps_n, nvdec->fps_d,
              "interlace-mode", G_TYPE_STRING, format->progressive_sequence
              ? "progressive" : "interleaved",
              "texture-target", G_TYPE_STRING, "2D", NULL);
          gst_caps_set_features (state->caps, 0,
              gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, NULL));
          gst_video_codec_state_unref (state);

          if (!gst_video_decoder_negotiate (decoder)) {
            GST_WARNING_OBJECT (nvdec, "failed to negotiate with downstream");
            ret = GST_FLOW_NOT_NEGOTIATED;
            break;
          }
        }

        break;

      case GST_NVDEC_QUEUE_ITEM_TYPE_DECODE:
        decode_params = (CUVIDPICPARAMS *) item->data;
        pending_frame = pending_frames->data;
        frame_number = decode_params->CurrPicIdx + 1;
        gst_video_codec_frame_set_user_data (pending_frame,
            GUINT_TO_POINTER (frame_number), NULL);

        if (decode_params->intra_pic_flag)
          GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (pending_frame);

        if (!GST_CLOCK_TIME_IS_VALID (pending_frame->duration)) {
          pending_frame->duration =
              nvdec->fps_n ? GST_SECOND * nvdec->fps_d / nvdec->fps_n : 0;
        }
        latency += pending_frame->duration;

        pending_frames = pending_frames->next;

        break;

      case GST_NVDEC_QUEUE_ITEM_TYPE_DISPLAY:
        dispinfo = (CUVIDPARSERDISPINFO *) item->data;
        for (pending_frame = NULL, tmp = list; !pending_frame && tmp;
            tmp = tmp->next) {
          frame_number =
              GPOINTER_TO_UINT (gst_video_codec_frame_get_user_data
              (tmp->data));
          if (frame_number == dispinfo->picture_index + 1)
            pending_frame = tmp->data;
        }
        if (!pending_frame) {
          GST_INFO_OBJECT (nvdec, "no frame with number %u",
              dispinfo->picture_index + 1);
          break;
        }

        if (dispinfo->timestamp != pending_frame->pts) {
          GST_INFO_OBJECT (nvdec,
              "timestamp mismatch, diff: %" GST_STIME_FORMAT,
              GST_STIME_ARGS (GST_CLOCK_DIFF (dispinfo->timestamp,
                      pending_frame->pts)));
          pending_frame->pts = dispinfo->timestamp;
        }

        if (latency > nvdec->min_latency) {
          nvdec->min_latency = latency;
          gst_video_decoder_set_latency (decoder, nvdec->min_latency,
              nvdec->min_latency);
          GST_DEBUG_OBJECT (nvdec, "latency: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (latency));
        }
        latency -= pending_frame->duration;

        ret = gst_video_decoder_allocate_output_frame (decoder, pending_frame);
        if (ret != GST_FLOW_OK) {
          GST_WARNING_OBJECT (nvdec, "failed to allocate output frame");
          break;
        }

        num_resources = gst_buffer_n_memory (pending_frame->output_buffer);
        resources = g_new (CUgraphicsResource, num_resources);

        for (i = 0; i < num_resources; i++) {
          mem = gst_buffer_get_memory (pending_frame->output_buffer, i);
          resources[i] =
              ensure_cuda_graphics_resource (mem, nvdec->cuda_context);
          GST_MINI_OBJECT_FLAG_SET (mem,
              GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD);
          gst_memory_unref (mem);
        }

        args[0] = nvdec;
        args[1] = dispinfo;
        args[2] = resources;
        args[3] = GUINT_TO_POINTER (num_resources);
        gst_gl_context_thread_add (nvdec->gl_context,
            (GstGLContextThreadFunc) copy_video_frame_to_gl_textures, args);
        g_free (resources);

        if (!dispinfo->progressive_frame) {
          GST_BUFFER_FLAG_SET (pending_frame->output_buffer,
              GST_VIDEO_BUFFER_FLAG_INTERLACED);

          if (dispinfo->top_field_first) {
            GST_BUFFER_FLAG_SET (pending_frame->output_buffer,
                GST_VIDEO_BUFFER_FLAG_TFF);
          }
          if (dispinfo->repeat_first_field == -1) {
            GST_BUFFER_FLAG_SET (pending_frame->output_buffer,
                GST_VIDEO_BUFFER_FLAG_ONEFIELD);
          } else {
            GST_BUFFER_FLAG_SET (pending_frame->output_buffer,
                GST_VIDEO_BUFFER_FLAG_RFF);
          }
        }

        list = g_list_remove (list, pending_frame);
        ret = gst_video_decoder_finish_frame (decoder, pending_frame);
        if (ret != GST_FLOW_OK)
          GST_INFO_OBJECT (nvdec, "failed to finish frame");

        break;

      default:
        g_assert_not_reached ();
    }

    g_free (item->data);
    g_slice_free (GstNvDecQueueItem, item);
  }

  g_list_free_full (list, (GDestroyNotify) gst_video_codec_frame_unref);

  return ret;
}

static GstFlowReturn
gst_nvdec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstNvDec *nvdec = GST_NVDEC (decoder);
  GstMapInfo map_info = GST_MAP_INFO_INIT;
  CUVIDSOURCEDATAPACKET packet = { 0, };

  GST_LOG_OBJECT (nvdec, "handle frame");

  gst_video_codec_frame_set_user_data (frame, GUINT_TO_POINTER (0), NULL);

  if (!gst_buffer_map (frame->input_buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (nvdec, "failed to map input buffer");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

  packet.payload_size = (gulong) map_info.size;
  packet.payload = map_info.data;
  packet.timestamp = frame->pts;
  packet.flags = CUVID_PKT_TIMESTAMP;

  if (GST_BUFFER_IS_DISCONT (frame->input_buffer))
    packet.flags &= CUVID_PKT_DISCONTINUITY;

  if (!cuda_OK (cuvidParseVideoData (nvdec->parser, &packet)))
    GST_WARNING_OBJECT (nvdec, "parser failed");

  gst_buffer_unmap (frame->input_buffer, &map_info);
  gst_video_codec_frame_unref (frame);

  return handle_pending_frames (nvdec);
}

static gboolean
gst_nvdec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstNvDec *nvdec = GST_NVDEC (decoder);
  GstCaps *outcaps;
  GstBufferPool *pool = NULL;
  guint n, size, min, max;
  GstVideoInfo vinfo = { 0, };
  GstStructure *config;

  GST_DEBUG_OBJECT (nvdec, "decide allocation");

  if (!gst_gl_ensure_element_data (nvdec, &nvdec->gl_display,
          &nvdec->other_gl_context)) {
    GST_ERROR_OBJECT (nvdec, "failed to ensure OpenGL display");
    return FALSE;
  }

  if (!gst_gl_query_local_gl_context (GST_ELEMENT (decoder), GST_PAD_SRC,
          &nvdec->gl_context)) {
    GST_INFO_OBJECT (nvdec, "failed to query local OpenGL context");
    if (nvdec->gl_context)
      gst_object_unref (nvdec->gl_context);
    nvdec->gl_context =
        gst_gl_display_get_gl_context_for_thread (nvdec->gl_display, NULL);
    if (!nvdec->gl_context
        || !gst_gl_display_add_context (nvdec->gl_display, nvdec->gl_context)) {
      if (nvdec->gl_context)
        gst_object_unref (nvdec->gl_context);
      if (!gst_gl_display_create_context (nvdec->gl_display,
              nvdec->other_gl_context, &nvdec->gl_context, NULL)) {
        GST_ERROR_OBJECT (nvdec, "failed to create OpenGL context");
        return FALSE;
      }
      if (!gst_gl_display_add_context (nvdec->gl_display, nvdec->gl_context)) {
        GST_ERROR_OBJECT (nvdec,
            "failed to add the OpenGL context to the display");
        return FALSE;
      }
    }
  }

  gst_query_parse_allocation (query, &outcaps, NULL);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (!GST_IS_GL_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = NULL;
    }
  }

  if (!pool) {
    pool = gst_gl_buffer_pool_new (nvdec->gl_context);

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

  return GST_VIDEO_DECODER_CLASS (gst_nvdec_parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_nvdec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstNvDec *nvdec = GST_NVDEC (decoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_gl_handle_context_query (GST_ELEMENT (decoder), query,
              nvdec->gl_display, nvdec->gl_context, nvdec->other_gl_context))
        return TRUE;
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (gst_nvdec_parent_class)->src_query (decoder,
      query);
}

static void
gst_nvdec_set_context (GstElement * element, GstContext * context)
{
  GstNvDec *nvdec = GST_NVDEC (element);
  GST_DEBUG_OBJECT (nvdec, "set context");

  gst_gl_handle_set_context (element, context, &nvdec->gl_display,
      &nvdec->other_gl_context);

  GST_ELEMENT_CLASS (gst_nvdec_parent_class)->set_context (element, context);
}
