/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Intel Corporation
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>

#include "gstmsdkdec.h"
#include "gstmsdkbufferpool.h"
#include "gstmsdkvideomemory.h"
#include "gstmsdksystemmemory.h"
#include "gstmsdkcontextutil.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkdec_debug);
#define GST_CAT_DEFAULT gst_msdkdec_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { NV12 }, "
        "framerate = (fraction) [0, MAX], "
        "width = (int) [ 16, MAX ], height = (int) [ 16, MAX ],"
        "interlace-mode = (string) progressive")
    );

enum
{
  PROP_0,
  PROP_HARDWARE,
  PROP_ASYNC_DEPTH,
};

#define PROP_HARDWARE_DEFAULT            TRUE
#define PROP_ASYNC_DEPTH_DEFAULT         4

#define gst_msdkdec_parent_class parent_class
G_DEFINE_TYPE (GstMsdkDec, gst_msdkdec, GST_TYPE_VIDEO_DECODER);

typedef struct _MsdkSurface
{
  mfxFrameSurface1 *surface;
  GstBuffer *buf;
  GstVideoFrame data;
  GstVideoFrame copy;
} MsdkSurface;

static gboolean gst_msdkdec_flush (GstVideoDecoder * decoder);

static GstFlowReturn
allocate_output_buffer (GstMsdkDec * thiz, GstBuffer ** buffer)
{
  GstFlowReturn flow;
  GstVideoCodecFrame *frame;
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (thiz);

  frame = gst_video_decoder_get_oldest_frame (decoder);
  if (!frame) {
    if (GST_PAD_IS_FLUSHING (decoder->srcpad))
      return GST_FLOW_FLUSHING;
    else
      return GST_FLOW_ERROR;
  }

  if (!frame->output_buffer) {
    flow = gst_video_decoder_allocate_output_frame (decoder, frame);
    if (flow != GST_FLOW_OK) {
      gst_video_codec_frame_unref (frame);
      return flow;
    }
  }

  *buffer = gst_buffer_ref (frame->output_buffer);
  gst_buffer_replace (&frame->output_buffer, NULL);
  gst_video_codec_frame_unref (frame);
  return GST_FLOW_OK;
}

static void
free_surface (GstMsdkDec * thiz, MsdkSurface * s)
{
  if (s->copy.buffer) {
    gst_video_frame_unmap (&s->copy);
    gst_buffer_unref (s->copy.buffer);
  }

  if (s->data.buffer)
    gst_video_frame_unmap (&s->data);

  gst_buffer_unref (s->buf);

  g_slice_free (MsdkSurface, s);
  thiz->decoded_msdk_surfaces = g_list_remove (thiz->decoded_msdk_surfaces, s);
}

static MsdkSurface *
get_surface (GstMsdkDec * thiz, GstBuffer * buffer)
{
  MsdkSurface *i;

  i = g_slice_new0 (MsdkSurface);

  if (gst_msdk_is_msdk_buffer (buffer)) {
    i->surface = gst_msdk_get_surface_from_buffer (buffer);
    i->buf = buffer;
  } else {
    /* Confirm to activate the side pool */
    if (!gst_buffer_pool_is_active (thiz->pool) &&
        !gst_buffer_pool_set_active (thiz->pool, TRUE)) {
      g_slice_free (MsdkSurface, i);
      return NULL;
    }

    if (!gst_video_frame_map (&i->copy, &thiz->output_info, buffer,
            GST_MAP_WRITE))
      goto failed_unref_buffer;

    if (gst_buffer_pool_acquire_buffer (thiz->pool, &buffer,
            NULL) != GST_FLOW_OK)
      goto failed_unmap_copy;

    i->surface = gst_msdk_get_surface_from_buffer (buffer);
    i->buf = buffer;

    if (!gst_video_frame_map (&i->data, &thiz->pool_info, buffer,
            GST_MAP_READWRITE))
      goto failed_unref_buffer2;
  }

  thiz->decoded_msdk_surfaces = g_list_append (thiz->decoded_msdk_surfaces, i);
  return i;

failed_unref_buffer2:
  gst_buffer_unref (buffer);
  buffer = i->data.buffer;
failed_unmap_copy:
  gst_video_frame_unmap (&i->copy);
failed_unref_buffer:
  gst_buffer_unref (buffer);
  g_slice_free (MsdkSurface, i);

  GST_ERROR_OBJECT (thiz, "failed to handle buffer");
  return NULL;
}

static void
gst_msdkdec_close_decoder (GstMsdkDec * thiz)
{
  mfxStatus status;

  if (!thiz->context || !thiz->initialized)
    return;

  GST_DEBUG_OBJECT (thiz, "Closing decoder with context %" GST_PTR_FORMAT,
      thiz->context);

  if (thiz->use_video_memory)
    gst_msdk_frame_free (thiz->context, &thiz->alloc_resp);

  status = MFXVideoDECODE_Close (gst_msdk_context_get_session (thiz->context));
  if (status != MFX_ERR_NONE && status != MFX_ERR_NOT_INITIALIZED) {
    GST_WARNING_OBJECT (thiz, "Decoder close failed (%s)",
        msdk_status_to_string (status));
  }

  g_array_set_size (thiz->tasks, 0);
  g_ptr_array_set_size (thiz->extra_params, 0);

  memset (&thiz->param, 0, sizeof (thiz->param));
  thiz->initialized = FALSE;
  gst_adapter_clear (thiz->adapter);
}

static void
gst_msdkdec_set_context (GstElement * element, GstContext * context)
{
  GstMsdkContext *msdk_context = NULL;
  GstMsdkDec *thiz = GST_MSDKDEC (element);

  if (gst_msdk_context_get_context (context, &msdk_context)) {
    gst_object_replace ((GstObject **) & thiz->context,
        (GstObject *) msdk_context);
    gst_object_unref (msdk_context);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_msdkdec_init_decoder (GstMsdkDec * thiz)
{
  GstMsdkDecClass *klass = GST_MSDKDEC_GET_CLASS (thiz);
  GstVideoInfo *info;
  mfxSession session;
  mfxStatus status;
  mfxFrameAllocRequest request;

  if (thiz->initialized)
    return TRUE;

  if (!thiz->context) {
    GST_WARNING_OBJECT (thiz, "No MSDK Context");
    return FALSE;
  }

  if (!thiz->input_state) {
    GST_DEBUG_OBJECT (thiz, "Have no input state yet");
    return FALSE;
  }
  info = &thiz->input_state->info;

  GST_OBJECT_LOCK (thiz);

  if (thiz->use_video_memory) {
    gst_msdk_set_frame_allocator (thiz->context);
    thiz->param.IOPattern = MFX_IOPATTERN_OUT_VIDEO_MEMORY;
  } else {
    thiz->param.IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
  }

  GST_INFO_OBJECT (thiz, "This MSDK decoder uses %s memory",
      thiz->use_video_memory ? "video" : "system");

  thiz->param.AsyncDepth = thiz->async_depth;
  thiz->param.mfx.FrameInfo.Width = GST_ROUND_UP_32 (info->width);
  thiz->param.mfx.FrameInfo.Height = GST_ROUND_UP_32 (info->height);
  thiz->param.mfx.FrameInfo.CropW = info->width;
  thiz->param.mfx.FrameInfo.CropH = info->height;

  /* Set framerate only if provided.
   * If not, framerate will be assumed inside the driver */
  if (info->fps_n > 0 && info->fps_d > 0) {
    thiz->param.mfx.FrameInfo.FrameRateExtN = info->fps_n;
    thiz->param.mfx.FrameInfo.FrameRateExtD = info->fps_d;
  }
  thiz->param.mfx.FrameInfo.AspectRatioW = info->par_n;
  thiz->param.mfx.FrameInfo.AspectRatioH = info->par_d;
  thiz->param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
  thiz->param.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
  thiz->param.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
  thiz->param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

  /* allow subclass configure further */
  if (klass->configure) {
    if (!klass->configure (thiz))
      goto failed;
  }

  thiz->param.NumExtParam = thiz->extra_params->len;
  thiz->param.ExtParam = (mfxExtBuffer **) thiz->extra_params->pdata;

  session = gst_msdk_context_get_session (thiz->context);
  /* validate parameters and allow the Media SDK to make adjustments */
  status = MFXVideoDECODE_Query (session, &thiz->param, &thiz->param);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "Video Decode Query failed (%s)",
        msdk_status_to_string (status));
    goto failed;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "Video Decode Query returned: %s",
        msdk_status_to_string (status));
  }

  status = MFXVideoDECODE_QueryIOSurf (session, &thiz->param, &request);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "Query IO surfaces failed (%s)",
        msdk_status_to_string (status));
    goto failed;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "Query IO surfaces returned: %s",
        msdk_status_to_string (status));
  }

  if (request.NumFrameSuggested < thiz->param.AsyncDepth) {
    GST_ERROR_OBJECT (thiz, "Required %d surfaces (%d suggested), async %d",
        request.NumFrameMin, request.NumFrameSuggested, thiz->param.AsyncDepth);
    goto failed;
  }

  if (thiz->use_video_memory) {
    gint shared_async_depth;

    shared_async_depth =
        gst_msdk_context_get_shared_async_depth (thiz->context);
    request.NumFrameSuggested += shared_async_depth;

    request.Type |= MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;
    gst_msdk_frame_alloc (thiz->context, &request, &thiz->alloc_resp);
  }

  GST_DEBUG_OBJECT (thiz, "Required %d surfaces (%d suggested)",
      request.NumFrameMin, request.NumFrameSuggested);

  status = MFXVideoDECODE_Init (session, &thiz->param);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "Init failed (%s)", msdk_status_to_string (status));
    goto failed;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "Init returned: %s",
        msdk_status_to_string (status));
  }

  status = MFXVideoDECODE_GetVideoParam (session, &thiz->param);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "Get Video Parameters failed (%s)",
        msdk_status_to_string (status));
    goto failed;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "Get Video Parameters returned: %s",
        msdk_status_to_string (status));
  }

  g_array_set_size (thiz->tasks, 0);
  g_array_set_size (thiz->tasks, thiz->param.AsyncDepth);
  thiz->next_task = 0;

  GST_OBJECT_UNLOCK (thiz);

  thiz->initialized = TRUE;
  return TRUE;

failed:
  GST_OBJECT_UNLOCK (thiz);
  return FALSE;
}

static gboolean
gst_msdkdec_set_src_caps (GstMsdkDec * thiz)
{
  GstVideoCodecState *output_state;
  GstVideoAlignment align;
  guint width, height;

  width = GST_VIDEO_INFO_WIDTH (&thiz->input_state->info);
  height = GST_VIDEO_INFO_HEIGHT (&thiz->input_state->info);

  output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (thiz),
      GST_VIDEO_FORMAT_NV12, width, height, thiz->input_state);

  gst_msdk_set_video_alignment (&output_state->info, &align);
  gst_video_info_align (&output_state->info, &align);
  thiz->output_info = output_state->info;
  if (output_state->caps)
    gst_caps_unref (output_state->caps);
  output_state->caps = gst_video_info_to_caps (&output_state->info);
  gst_video_codec_state_unref (output_state);

  return TRUE;
}

static void
gst_msdkdec_set_latency (GstMsdkDec * thiz)
{
  GstVideoInfo *info = &thiz->input_state->info;
  gint min_delayed_frames;
  GstClockTime latency;

  min_delayed_frames = thiz->tasks->len;

  if (info->fps_n) {
    latency = gst_util_uint64_scale_ceil (GST_SECOND * info->fps_d,
        min_delayed_frames, info->fps_n);
  } else {
    /* FIXME: Assume 25fps. This is better than reporting no latency at
     * all and then later failing in live pipelines
     */
    latency = gst_util_uint64_scale_ceil (GST_SECOND * 1,
        min_delayed_frames, 25);
  }

  GST_INFO_OBJECT (thiz,
      "Updating latency to %" GST_TIME_FORMAT " (%d frames)",
      GST_TIME_ARGS (latency), min_delayed_frames);

  gst_video_decoder_set_latency (GST_VIDEO_DECODER (thiz), latency, latency);
}

static gint
_find_msdk_surface (gconstpointer msdk_surface, gconstpointer comp_surface)
{
  MsdkSurface *cached_surface = (MsdkSurface *) msdk_surface;
  mfxFrameSurface1 *_surface = (mfxFrameSurface1 *) comp_surface;

  return cached_surface ? cached_surface->surface != _surface : -1;
}

static GstFlowReturn
gst_msdkdec_finish_task (GstMsdkDec * thiz, MsdkDecTask * task)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (thiz);
  GstFlowReturn flow;
  GstVideoCodecFrame *frame;
  MsdkSurface *surface;
  mfxStatus status;
  GList *l;

  if (G_LIKELY (task->sync_point)) {
    status =
        MFXVideoCORE_SyncOperation (gst_msdk_context_get_session
        (thiz->context), task->sync_point, 300000);
    if (status != MFX_ERR_NONE) {
      GST_ERROR_OBJECT (thiz, "failed to do sync operation");
      return GST_FLOW_ERROR;
    }

    frame = gst_video_decoder_get_oldest_frame (decoder);
    task->sync_point = NULL;

    l = g_list_find_custom (thiz->decoded_msdk_surfaces, task->surface,
        _find_msdk_surface);
    if (l) {
      surface = l->data;
    } else {
      GST_ERROR_OBJECT (thiz, "Couldn't find the cached MSDK surface");
      return GST_FLOW_ERROR;
    }

    if (G_LIKELY (frame)) {
      if (G_LIKELY (surface->copy.buffer == NULL)) {
        frame->output_buffer = gst_buffer_ref (surface->buf);
      } else {
        gst_video_frame_copy (&surface->copy, &surface->data);
        frame->output_buffer = gst_buffer_ref (surface->copy.buffer);
      }
    }

    free_surface (thiz, surface);

    if (!frame)
      return GST_FLOW_FLUSHING;
    gst_video_codec_frame_unref (frame);

    flow = gst_video_decoder_finish_frame (decoder, frame);
    return flow;
  }
  return GST_FLOW_OK;
}

static gboolean
gst_msdkdec_start (GstVideoDecoder * decoder)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);

  if (gst_msdk_context_prepare (GST_ELEMENT_CAST (thiz), &thiz->context)) {
    GST_INFO_OBJECT (thiz, "Found context %" GST_PTR_FORMAT " from neighbour",
        thiz->context);
    thiz->use_video_memory = TRUE;

    if (gst_msdk_context_get_job_type (thiz->context) & GST_MSDK_JOB_DECODER) {
      GstMsdkContext *parent_context;

      parent_context = thiz->context;
      thiz->context = gst_msdk_context_new_with_parent (parent_context);

      gst_msdk_context_add_shared_async_depth (thiz->context,
          gst_msdk_context_get_shared_async_depth (parent_context));
      gst_object_unref (parent_context);

      GST_INFO_OBJECT (thiz,
          "Creating new context %" GST_PTR_FORMAT " with joined session",
          thiz->context);
    } else {
      gst_msdk_context_add_job_type (thiz->context, GST_MSDK_JOB_DECODER);
    }
  } else {
    gst_msdk_context_ensure_context (GST_ELEMENT_CAST (thiz), thiz->hardware,
        GST_MSDK_JOB_DECODER);
    GST_INFO_OBJECT (thiz, "Creating new context %" GST_PTR_FORMAT,
        thiz->context);
  }

  gst_msdk_context_add_shared_async_depth (thiz->context, thiz->async_depth);

  return TRUE;
}

static gboolean
gst_msdkdec_close (GstVideoDecoder * decoder)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);

  if (thiz->context)
    gst_object_replace ((GstObject **) & thiz->context, NULL);

  return TRUE;
}

static gboolean
gst_msdkdec_stop (GstVideoDecoder * decoder)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);

  gst_msdkdec_flush (decoder);

  if (thiz->input_state) {
    gst_video_codec_state_unref (thiz->input_state);
    thiz->input_state = NULL;
  }
  if (thiz->pool) {
    gst_object_unref (thiz->pool);
    thiz->pool = NULL;
  }
  gst_video_info_init (&thiz->output_info);
  gst_video_info_init (&thiz->pool_info);

  gst_msdkdec_close_decoder (thiz);
  return TRUE;
}

static gboolean
gst_msdkdec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);

  if (thiz->input_state)
    gst_video_codec_state_unref (thiz->input_state);
  thiz->input_state = gst_video_codec_state_ref (state);

  if (!gst_msdkdec_set_src_caps (thiz))
    return FALSE;

  gst_msdkdec_set_latency (thiz);
  return TRUE;
}

static void
release_msdk_surfaces (GstMsdkDec * thiz)
{
  GList *l;
  MsdkSurface *surface;

  for (l = thiz->decoded_msdk_surfaces; l; l = l->next) {
    surface = l->data;
    free_surface (thiz, surface);
  }
}

static GstFlowReturn
gst_msdkdec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);
  GstFlowReturn flow;
  GstBuffer *buffer;
  MsdkDecTask *task = NULL;
  mfxBitstream bitstream;
  MsdkSurface *surface = NULL;
  mfxSession session;
  mfxStatus status;
  GstMapInfo map_info;
  guint i;
  gsize data_size;

  if (!thiz->initialized)
    gst_video_decoder_negotiate (decoder);

  if (!gst_buffer_map (frame->input_buffer, &map_info, GST_MAP_READ))
    return GST_FLOW_ERROR;

  memset (&bitstream, 0, sizeof (bitstream));

  if (thiz->is_packetized) {
    /* Packetized stream: We prefer to have a parser as connected upstream
     * element to the decoder */
    bitstream.Data = map_info.data;
    bitstream.DataLength = map_info.size;
    bitstream.MaxLength = map_info.size;
  } else {
    /* Non packetized streams: eg: vc1 advanced profile with per buffer bdu */
    gst_adapter_push (thiz->adapter, gst_buffer_ref (frame->input_buffer));
    data_size = gst_adapter_available (thiz->adapter);

    bitstream.Data = (mfxU8 *) gst_adapter_map (thiz->adapter, data_size);
    bitstream.DataLength = (mfxU32) data_size;
    bitstream.MaxLength = bitstream.DataLength;
  }
  GST_INFO_OBJECT (thiz,
      "mfxBitStream=> DataLength:%d DataOffset:%d MaxLength:%d",
      bitstream.DataLength, bitstream.DataOffset, bitstream.MaxLength);

  session = gst_msdk_context_get_session (thiz->context);
  for (;;) {
    task = &g_array_index (thiz->tasks, MsdkDecTask, thiz->next_task);
    flow = gst_msdkdec_finish_task (thiz, task);
    if (flow != GST_FLOW_OK)
      goto error;
    if (!surface) {
      flow = allocate_output_buffer (thiz, &buffer);
      if (flow != GST_FLOW_OK)
        goto error;
      surface = get_surface (thiz, buffer);
      if (!surface) {
        /* Can't get a surface for some reason, finish tasks to see if
           a surface becomes available. */
        for (i = 0; i < thiz->tasks->len - 1; i++) {
          thiz->next_task = (thiz->next_task + 1) % thiz->tasks->len;
          task = &g_array_index (thiz->tasks, MsdkDecTask, thiz->next_task);
          flow = gst_msdkdec_finish_task (thiz, task);
          if (flow != GST_FLOW_OK)
            goto error;
          surface = get_surface (thiz, buffer);
          if (surface)
            break;
        }
        if (!surface) {
          GST_ERROR_OBJECT (thiz, "Couldn't get a surface");
          flow = GST_FLOW_ERROR;
          goto error;
        }
      }
    }

    status =
        MFXVideoDECODE_DecodeFrameAsync (session, &bitstream, surface->surface,
        &task->surface, &task->sync_point);
    if (G_LIKELY (status == MFX_ERR_NONE)) {
      thiz->next_task = (thiz->next_task + 1) % thiz->tasks->len;

      if (surface->surface->Data.Locked > 0 || !thiz->use_video_memory)
        surface = NULL;

      if (bitstream.DataLength == 0) {
        flow = GST_FLOW_OK;
        break;
      }
    } else if (status == MFX_ERR_MORE_DATA) {
      if (surface->surface->Data.Locked > 0)
        surface = NULL;
      flow = GST_FLOW_OK;
      break;
    } else if (status == MFX_ERR_MORE_SURFACE) {
      surface = NULL;
      continue;
    } else if (status == MFX_WRN_DEVICE_BUSY) {
      /* If device is busy, wait 1ms and retry, as per MSDK's recomendation */
      g_usleep (1000);

      /* If the current surface is still busy, we should do sync oepration
       * then tries to decode again
       */
      thiz->next_task = (thiz->next_task + 1) % thiz->tasks->len;
    } else if (status < MFX_ERR_NONE) {
      GST_ERROR_OBJECT (thiz, "DecodeFrameAsync failed (%s)",
          msdk_status_to_string (status));
      flow = GST_FLOW_ERROR;
      break;
    }
  }

  if (!thiz->is_packetized) {
    /* flush out the data which is already consumed by msdk */
    gst_adapter_flush (thiz->adapter, bitstream.DataOffset);
    flow = GST_FLOW_OK;
  }

  if (surface)
    free_surface (thiz, surface);

  gst_buffer_unmap (frame->input_buffer, &map_info);
  return flow;

error:
  gst_buffer_unmap (frame->input_buffer, &map_info);
  gst_video_decoder_drop_frame (decoder, frame);

  return flow;
}

static GstBufferPool *
gst_msdkdec_create_buffer_pool (GstMsdkDec * thiz, GstCaps * caps,
    guint num_buffers)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;
  GstVideoAlignment align;
  GstAllocationParams params = { 0, 31, 0, 0, };
  mfxFrameAllocResponse *alloc_resp = NULL;

  alloc_resp = &thiz->alloc_resp;

  pool = gst_msdk_buffer_pool_new (thiz->context, alloc_resp);
  if (!pool)
    goto error_no_pool;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_INFO_OBJECT (thiz, "failed to get video info");
    return FALSE;
  }

  gst_msdk_set_video_alignment (&info, &align);
  gst_video_info_align (&info, &align);

  if (thiz->use_video_memory)
    allocator = gst_msdk_video_allocator_new (thiz->context, &info, alloc_resp);
  else
    allocator = gst_msdk_system_allocator_new (&info);

  if (!allocator)
    goto error_no_allocator;

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));
  gst_buffer_pool_config_set_params (config, caps, info.size, num_buffers, 0);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (thiz->use_video_memory)
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_MSDK_USE_VIDEO_MEMORY);

  gst_buffer_pool_config_set_video_alignment (config, &align);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);
  gst_object_unref (allocator);

  if (!gst_buffer_pool_set_config (pool, config))
    goto error_pool_config;

  return pool;

error_no_pool:
  {
    GST_INFO_OBJECT (thiz, "failed to create bufferpool");
    return NULL;
  }
error_no_allocator:
  {
    GST_INFO_OBJECT (thiz, "failed to create allocator");
    return NULL;
  }
error_pool_config:
  {
    GST_INFO_OBJECT (thiz, "failed to set config");
    return NULL;
  }
}

static gboolean
gst_msdkdec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);
  GstVideoInfo info_from_caps, info_aligned;
  GstVideoAlignment alignment;
  GstBufferPool *pool = NULL;
  GstStructure *pool_config = NULL;
  GstCaps *pool_caps;
  guint size, min_buffers, max_buffers;

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query))
    return FALSE;

  /* Get the buffer pool config decided by the base class. The base
     class ensures that there will always be at least a 0th pool in
     the query. */
  gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);
  pool_config = gst_buffer_pool_get_config (pool);

  /* If downstream's pool is MSDK bufferpool, decoder is using video memory */
  if (GST_IS_MSDK_BUFFER_POOL (pool))
    thiz->use_video_memory = TRUE;

  /* Get the caps of pool and increase the min and max buffers by async_depth,
   * we will always have that number of decode operations in-flight */
  gst_buffer_pool_config_get_params (pool_config, &pool_caps, &size,
      &min_buffers, &max_buffers);
  min_buffers += thiz->async_depth;
  if (max_buffers)
    max_buffers += thiz->async_depth;

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)
      && gst_buffer_pool_has_option (pool,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
    GstStructure *config;
    GstAllocator *allocator;

    /* If downstream supports video meta and video alignment,
     * we can replace our own msdk bufferpool and use it
     */
    GST_INFO_OBJECT (decoder, "create new MSDK bufferpool");

    /* Remove downstream's pool */
    gst_structure_free (pool_config);
    gst_object_unref (pool);

    /* FIXME: this might break renegotiation.
     * We should re-create msdk bufferpool, but it breaks decoding. */
    if (!thiz->pool) {
      thiz->pool =
          gst_msdkdec_create_buffer_pool (thiz, pool_caps, min_buffers);
      if (!thiz->pool)
        goto failed_to_create_pool;
    }
    pool = gst_object_ref (thiz->pool);

    /* Set the allocator of new msdk bufferpool */
    config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));

    if (gst_buffer_pool_config_get_allocator (config, &allocator, NULL))
      gst_query_set_nth_allocation_param (query, 0, allocator, NULL);
    gst_structure_free (config);
  } else {
    /* If not, we just make a side-pool that will be decoded into and
     * the copied from.
     */
    GST_INFO_OBJECT (decoder, "create new MSDK bufferpool as a side-pool");
    thiz->pool =
        gst_msdkdec_create_buffer_pool (thiz, pool_caps, thiz->async_depth);
    if (!thiz->pool)
      goto failed_to_create_pool;

    /* Update params to downstream's pool */
    gst_buffer_pool_config_set_params (pool_config, pool_caps, size,
        min_buffers, max_buffers);
    if (!gst_buffer_pool_set_config (pool, pool_config))
      goto error_set_config;

    /* Check if the pool's caps will meet msdk's alignment
     * requirements by default and get aligned video info.
     */
    gst_video_info_from_caps (&info_from_caps, pool_caps);
    info_aligned = info_from_caps;
    gst_msdk_set_video_alignment (&info_from_caps, &alignment);
    gst_video_info_align (&info_aligned, &alignment);

    thiz->output_info = info_from_caps;
    thiz->pool_info = info_aligned;
  }

  gst_query_set_nth_allocation_pool (query, 0, pool, size, min_buffers,
      max_buffers);

  if (pool)
    gst_object_unref (pool);

  /* Initialize MSDK decoder before new bufferpool tries to alloc each buffer,
   * which requires information of frame allocation.
   */
  if (!gst_msdkdec_init_decoder (thiz))
    return FALSE;

  return TRUE;

failed_to_create_pool:
  GST_ERROR_OBJECT (decoder, "failed to set buffer pool config");
  if (pool)
    gst_object_unref (pool);
  return FALSE;

error_set_config:
  GST_ERROR_OBJECT (decoder, "failed to set buffer pool config");
  if (pool)
    gst_object_unref (pool);
  return FALSE;
}

static GstFlowReturn
gst_msdkdec_drain (GstVideoDecoder * decoder)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);
  GstFlowReturn flow;
  GstBuffer *buffer;
  MsdkDecTask *task;
  MsdkSurface *surface = NULL;
  mfxSession session;
  mfxStatus status;
  guint i;

  if (!thiz->initialized)
    return GST_FLOW_OK;
  session = gst_msdk_context_get_session (thiz->context);

  for (;;) {
    task = &g_array_index (thiz->tasks, MsdkDecTask, thiz->next_task);
    if ((flow = gst_msdkdec_finish_task (thiz, task)) != GST_FLOW_OK) {
      if (flow != GST_FLOW_FLUSHING)
        GST_WARNING_OBJECT (decoder,
            "failed to finish the task %p, but keep draining for the remaining frames",
            task);
    }

    if (!surface) {
      flow = allocate_output_buffer (thiz, &buffer);
      if (flow != GST_FLOW_OK)
        return flow;
      surface = get_surface (thiz, buffer);
      if (!surface)
        return GST_FLOW_ERROR;
    }

    status =
        MFXVideoDECODE_DecodeFrameAsync (session, NULL, surface->surface,
        &task->surface, &task->sync_point);
    if (G_LIKELY (status == MFX_ERR_NONE)) {
      thiz->next_task = (thiz->next_task + 1) % thiz->tasks->len;

      if (surface->surface->Data.Locked == 0)
        free_surface (thiz, surface);
      surface = NULL;
    } else if (status == MFX_WRN_VIDEO_PARAM_CHANGED) {
      continue;
    } else if (status == MFX_WRN_DEVICE_BUSY) {
      /* If device is busy, wait 1ms and retry, as per MSDK's recomendation */
      g_usleep (1000);

      /* If the current surface is still busy, we should do sync oepration
       * then tries to decode again
       */
      thiz->next_task = (thiz->next_task + 1) % thiz->tasks->len;
    } else if (status == MFX_ERR_MORE_DATA) {
      break;
    } else if (status == MFX_ERR_MORE_SURFACE) {
      surface = NULL;
      continue;
    } else if (status < MFX_ERR_NONE)
      return GST_FLOW_ERROR;
  }
  if (surface)
    free_surface (thiz, surface);

  for (i = 0; i < thiz->tasks->len; i++) {
    task = &g_array_index (thiz->tasks, MsdkDecTask, thiz->next_task);
    gst_msdkdec_finish_task (thiz, task);
    thiz->next_task = (thiz->next_task + 1) % thiz->tasks->len;
  }

  release_msdk_surfaces (thiz);

  return GST_FLOW_OK;
}

static gboolean
gst_msdkdec_flush (GstVideoDecoder * decoder)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);

  return gst_msdkdec_drain (GST_VIDEO_DECODER_CAST (thiz));
}

static GstFlowReturn
gst_msdkdec_finish (GstVideoDecoder * decoder)
{
  return gst_msdkdec_drain (decoder);
}

static void
gst_msdkdec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstMsdkDec *thiz = GST_MSDKDEC (object);
  GstState state;

  GST_OBJECT_LOCK (thiz);

  state = GST_STATE (thiz);
  if ((state != GST_STATE_READY && state != GST_STATE_NULL) &&
      !(pspec->flags & GST_PARAM_MUTABLE_PLAYING))
    goto wrong_state;

  switch (prop_id) {
    case PROP_HARDWARE:
      thiz->hardware = g_value_get_boolean (value);
      break;
    case PROP_ASYNC_DEPTH:
      thiz->async_depth = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
  return;

  /* ERROR */
wrong_state:
  {
    GST_WARNING_OBJECT (thiz, "setting property in wrong state");
    GST_OBJECT_UNLOCK (thiz);
  }
}

static void
gst_msdkdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkDec *thiz = GST_MSDKDEC (object);

  GST_OBJECT_LOCK (thiz);
  switch (prop_id) {
    case PROP_HARDWARE:
      g_value_set_boolean (value, thiz->hardware);
      break;
    case PROP_ASYNC_DEPTH:
      g_value_set_uint (value, thiz->async_depth);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static void
gst_msdkdec_finalize (GObject * object)
{
  GstMsdkDec *thiz = GST_MSDKDEC (object);

  g_array_unref (thiz->tasks);
  g_ptr_array_unref (thiz->extra_params);
  g_object_unref (thiz->adapter);
}

static void
gst_msdkdec_class_init (GstMsdkDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoDecoderClass *decoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject_class->set_property = gst_msdkdec_set_property;
  gobject_class->get_property = gst_msdkdec_get_property;
  gobject_class->finalize = gst_msdkdec_finalize;

  element_class->set_context = gst_msdkdec_set_context;

  decoder_class->close = GST_DEBUG_FUNCPTR (gst_msdkdec_close);
  decoder_class->start = GST_DEBUG_FUNCPTR (gst_msdkdec_start);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_msdkdec_stop);
  decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_msdkdec_set_format);
  decoder_class->finish = GST_DEBUG_FUNCPTR (gst_msdkdec_finish);
  decoder_class->handle_frame = GST_DEBUG_FUNCPTR (gst_msdkdec_handle_frame);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_msdkdec_decide_allocation);
  decoder_class->flush = GST_DEBUG_FUNCPTR (gst_msdkdec_flush);
  decoder_class->drain = GST_DEBUG_FUNCPTR (gst_msdkdec_drain);

  g_object_class_install_property (gobject_class, PROP_HARDWARE,
      g_param_spec_boolean ("hardware", "Hardware", "Enable hardware decoders",
          PROP_HARDWARE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ASYNC_DEPTH,
      g_param_spec_uint ("async-depth", "Async Depth",
          "Depth of asynchronous pipeline",
          1, 20, PROP_ASYNC_DEPTH_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkdec_init (GstMsdkDec * thiz)
{
  gst_video_info_init (&thiz->output_info);
  gst_video_info_init (&thiz->pool_info);
  thiz->extra_params = g_ptr_array_new_with_free_func (g_free);
  thiz->tasks = g_array_new (FALSE, TRUE, sizeof (MsdkDecTask));
  thiz->hardware = PROP_HARDWARE_DEFAULT;
  thiz->async_depth = PROP_ASYNC_DEPTH_DEFAULT;
  thiz->is_packetized = TRUE;
  thiz->adapter = gst_adapter_new ();
}
