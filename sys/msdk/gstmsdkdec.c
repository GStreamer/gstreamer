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
        "interlace-mode = (string) progressive;"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_DMABUF,
            "{ NV12 }") ";")
    );

#define PROP_HARDWARE_DEFAULT            TRUE
#define PROP_ASYNC_DEPTH_DEFAULT         1

#define IS_ALIGNED(i, n) (((i) & ((n)-1)) == 0)

#define gst_msdkdec_parent_class parent_class
G_DEFINE_TYPE (GstMsdkDec, gst_msdkdec, GST_TYPE_VIDEO_DECODER);

typedef struct _MsdkSurface
{
  mfxFrameSurface1 *surface;
  GstBuffer *buf;
  GstVideoFrame data;
  GstVideoFrame copy;
} MsdkSurface;

static gboolean gst_msdkdec_drain (GstVideoDecoder * decoder);
static gboolean gst_msdkdec_flush (GstVideoDecoder * decoder);
static gboolean gst_msdkdec_negotiate (GstMsdkDec * thiz, gboolean hard_reset);

static GstVideoCodecFrame *
gst_msdkdec_get_oldest_frame (GstVideoDecoder * decoder)
{
  GstVideoCodecFrame *frame = NULL, *old_frame = NULL;
  GList *frames, *l;
  gint count = 0;

  frames = gst_video_decoder_get_frames (decoder);

  for (l = frames; l != NULL; l = l->next) {
    GstVideoCodecFrame *f = l->data;

    if (!GST_CLOCK_TIME_IS_VALID (f->pts)) {
      GST_INFO
          ("Frame doesn't have a valid pts yet, Use gst_video_decoder_get_oldest_frame()"
          "with out considering the PTS for selecting the frame to be finished");
      old_frame = gst_video_decoder_get_oldest_frame (decoder);
      break;
    }

    if (!frame || frame->pts > f->pts)
      frame = f;

    count++;
  }

  if (old_frame)
    frame = old_frame;

  if (frame) {
    GST_LOG_OBJECT (decoder,
        "Oldest frame is %d %" GST_TIME_FORMAT " and %d frames left",
        frame->system_frame_number, GST_TIME_ARGS (frame->pts), count - 1);
    gst_video_codec_frame_ref (frame);
  }

  if (old_frame)
    gst_video_codec_frame_unref (old_frame);

  g_list_free_full (frames, (GDestroyNotify) gst_video_codec_frame_unref);

  return frame;
}

static GstFlowReturn
allocate_output_buffer (GstMsdkDec * thiz, GstBuffer ** buffer)
{
  GstFlowReturn flow;
  GstVideoCodecFrame *frame;
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (thiz);

  frame = gst_msdkdec_get_oldest_frame (decoder);
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
  thiz->decoded_msdk_surfaces = g_list_remove (thiz->decoded_msdk_surfaces, s);

  g_slice_free (MsdkSurface, s);
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

    if (!gst_video_frame_map (&i->copy, &thiz->non_msdk_pool_info, buffer,
            GST_MAP_WRITE))
      goto failed_unref_buffer;

    if (gst_buffer_pool_acquire_buffer (thiz->pool, &buffer,
            NULL) != GST_FLOW_OK)
      goto failed_unmap_copy;

    i->surface = gst_msdk_get_surface_from_buffer (buffer);
    i->buf = buffer;

    if (!gst_video_frame_map (&i->data, &thiz->output_info, buffer,
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
gst_msdkdec_close_decoder (GstMsdkDec * thiz, gboolean reset_param)
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

  if (reset_param)
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

  /* We expect msdk to fill the width and height values */
  g_return_val_if_fail (thiz->param.mfx.FrameInfo.Width
      && thiz->param.mfx.FrameInfo.Height, FALSE);

  /* Force 32 bit rounding to avoid messing up of memory alignment when
   * dealing with different allocators */
  /* Fixme: msdk sometimes only requires 16 bit rounding, optimization possible */
  thiz->param.mfx.FrameInfo.Width =
      GST_ROUND_UP_16 (thiz->param.mfx.FrameInfo.Width);
  thiz->param.mfx.FrameInfo.Height =
      GST_ROUND_UP_32 (thiz->param.mfx.FrameInfo.Height);
  /* Set framerate only if provided.
   * If not, framerate will be assumed inside the driver.
   * Also we respect the upstream provided fps values */
  if (info->fps_n > 0 && info->fps_d > 0
      && info->fps_n != thiz->param.mfx.FrameInfo.FrameRateExtN
      && info->fps_d != thiz->param.mfx.FrameInfo.FrameRateExtD) {
    thiz->param.mfx.FrameInfo.FrameRateExtN = info->fps_n;
    thiz->param.mfx.FrameInfo.FrameRateExtD = info->fps_d;
  }

  if (info->par_n && info->par_d && !thiz->param.mfx.FrameInfo.AspectRatioW
      && !thiz->param.mfx.FrameInfo.AspectRatioH) {
    thiz->param.mfx.FrameInfo.AspectRatioW = info->par_n;
    thiz->param.mfx.FrameInfo.AspectRatioH = info->par_d;
  }

  thiz->param.mfx.FrameInfo.PicStruct =
      thiz->param.mfx.FrameInfo.PicStruct ? thiz->param.mfx.
      FrameInfo.PicStruct : MFX_PICSTRUCT_PROGRESSIVE;
  thiz->param.mfx.FrameInfo.FourCC =
      thiz->param.mfx.FrameInfo.FourCC ? thiz->param.mfx.
      FrameInfo.FourCC : MFX_FOURCC_NV12;
  thiz->param.mfx.FrameInfo.ChromaFormat =
      thiz->param.mfx.FrameInfo.ChromaFormat ? thiz->param.mfx.
      FrameInfo.ChromaFormat : MFX_CHROMAFORMAT_YUV420;

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

  /* Force the structure to MFX_PICSTRUCT_PROGRESSIVE if it is unknow to
   * work-around MSDK issue:
   * https://github.com/Intel-Media-SDK/MediaSDK/issues/1139
   */
  if (thiz->param.mfx.FrameInfo.PicStruct == MFX_PICSTRUCT_UNKNOWN)
    thiz->param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;

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

  /* account the downstream requirement */
  if (G_LIKELY (thiz->min_prealloc_buffers))
    request.NumFrameSuggested += thiz->min_prealloc_buffers;
  else
    GST_WARNING_OBJECT (thiz,
        "Allocating resources without considering the downstream requirement"
        "or extra scratch surface count");

  if (thiz->use_video_memory) {
    gint shared_async_depth;

    shared_async_depth =
        gst_msdk_context_get_shared_async_depth (thiz->context);
    request.NumFrameSuggested += shared_async_depth;

    request.Type |= MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;
    if (thiz->use_dmabuf)
      request.Type |= MFX_MEMTYPE_EXPORT_FRAME;
    gst_msdk_frame_alloc (thiz->context, &request, &thiz->alloc_resp);
  }

  /* update the prealloc_buffer count which will be used later
   * as GstBufferPool min_buffers */
  thiz->min_prealloc_buffers = request.NumFrameSuggested;

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

  g_array_set_size (thiz->tasks, 0);    /* resets array content */
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
_gst_caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint i;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, i);
    /* Skip ANY features, we need an exact match for correct evaluation */
    if (gst_caps_features_is_any (features))
      continue;
    if (gst_caps_features_contains (features, feature))
      return TRUE;
  }

  return FALSE;
}

static gboolean
srcpad_can_dmabuf (GstMsdkDec * thiz)
{
  gboolean ret = FALSE;
  GstCaps *caps, *out_caps;
  GstPad *srcpad;

  srcpad = GST_VIDEO_DECODER_SRC_PAD (thiz);
  caps = gst_pad_get_pad_template_caps (srcpad);

  out_caps = gst_pad_peer_query_caps (srcpad, caps);
  if (!out_caps)
    goto done;

  if (gst_caps_is_any (out_caps) || gst_caps_is_empty (out_caps)
      || out_caps == caps)
    goto done;

  if (_gst_caps_has_feature (out_caps, GST_CAPS_FEATURE_MEMORY_DMABUF))
    ret = TRUE;

done:
  if (caps)
    gst_caps_unref (caps);
  if (out_caps)
    gst_caps_unref (out_caps);
  return ret;
}

static gboolean
gst_msdkdec_set_src_caps (GstMsdkDec * thiz, gboolean need_allocation)
{
  GstVideoCodecState *output_state;
  GstVideoInfo *vinfo;
  GstVideoAlignment align;
  GstCaps *allocation_caps = NULL;
  GstVideoFormat format;
  guint width, height;
  const gchar *format_str;

  /* use display width and display height in output state which
   * will be using for caps negotiation */
  width =
      thiz->param.mfx.FrameInfo.CropW ? thiz->param.mfx.
      FrameInfo.CropW : GST_VIDEO_INFO_WIDTH (&thiz->input_state->info);
  height =
      thiz->param.mfx.FrameInfo.CropH ? thiz->param.mfx.
      FrameInfo.CropH : GST_VIDEO_INFO_HEIGHT (&thiz->input_state->info);

  format =
      gst_msdk_get_video_format_from_mfx_fourcc (thiz->param.mfx.
      FrameInfo.FourCC);

  if (format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_WARNING_OBJECT (thiz, "Failed to find a valid video format\n");
    return FALSE;
  }

  output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (thiz),
      format, width, height, thiz->input_state);
  if (!output_state)
    return FALSE;

  /* Ensure output_state->caps and info has same width and height
   * Also mandate the 32 bit alignment */
  vinfo = &output_state->info;
  gst_msdk_set_video_alignment (vinfo, &align);
  gst_video_info_align (vinfo, &align);
  output_state->caps = gst_video_info_to_caps (vinfo);
  if (srcpad_can_dmabuf (thiz))
    gst_caps_set_features (output_state->caps, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DMABUF, NULL));
  thiz->output_info = output_state->info;

  if (need_allocation) {
    /* Find allocation width and height */
    width =
        GST_ROUND_UP_16 (thiz->param.mfx.FrameInfo.Width ? thiz->param.mfx.
        FrameInfo.Width : GST_VIDEO_INFO_WIDTH (&output_state->info));
    height =
        GST_ROUND_UP_32 (thiz->param.mfx.FrameInfo.Height ? thiz->param.mfx.
        FrameInfo.Height : GST_VIDEO_INFO_HEIGHT (&output_state->info));

    /* set allocation width and height in allocation_caps
     * which may or may not be similar to the output_state caps */
    allocation_caps = gst_caps_copy (output_state->caps);
    format_str =
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&thiz->output_info));
    gst_caps_set_simple (allocation_caps, "width", G_TYPE_INT, width, "height",
        G_TYPE_INT, height, "format", G_TYPE_STRING, format_str, NULL);
    GST_INFO_OBJECT (thiz, "new alloc caps = %" GST_PTR_FORMAT,
        allocation_caps);
    gst_caps_replace (&thiz->allocation_caps, allocation_caps);
  } else {
    /* We keep the allocation parameters as it is to avoid pool renegotiation.
     * For codecs like VP9, dynamic resolution change doesn't requires allocation
     * reset if the new video frame resolution is lower than the
     * already configured one */
    allocation_caps = gst_caps_copy (thiz->allocation_caps);
  }

  gst_caps_replace (&output_state->allocation_caps, allocation_caps);
  if (allocation_caps)
    gst_caps_unref (allocation_caps);

  gst_video_codec_state_unref (output_state);
  return TRUE;
}

static void
gst_msdkdec_set_latency (GstMsdkDec * thiz)
{
  GstVideoInfo *info = &thiz->input_state->info;
  gint min_delayed_frames;
  GstClockTime latency;

  min_delayed_frames = thiz->async_depth;

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
  }

  if (G_LIKELY (task->sync_point || (task->surface && task->decode_only))) {
    gboolean decode_only = task->decode_only;

    frame = gst_msdkdec_get_oldest_frame (decoder);

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
    task->sync_point = NULL;
    task->surface = NULL;
    task->decode_only = FALSE;

    if (!frame)
      return GST_FLOW_FLUSHING;
    gst_video_codec_frame_unref (frame);

    if (decode_only)
      GST_VIDEO_CODEC_FRAME_SET_DECODE_ONLY (frame);
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
      GstMsdkContext *parent_context, *msdk_context;

      parent_context = thiz->context;
      msdk_context = gst_msdk_context_new_with_parent (parent_context);

      if (!msdk_context) {
        GST_ERROR_OBJECT (thiz, "Context creation failed");
        return FALSE;
      }

      thiz->context = msdk_context;

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
  gst_video_info_init (&thiz->non_msdk_pool_info);

  gst_msdkdec_close_decoder (thiz, TRUE);
  return TRUE;
}

static gboolean
gst_msdkdec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);

  if (thiz->input_state) {
    /* mark for re-negotiation if display resolution changes */
    if ((GST_VIDEO_INFO_WIDTH (&thiz->input_state->info) !=
            GST_VIDEO_INFO_WIDTH (&state->info)) ||
        GST_VIDEO_INFO_HEIGHT (&thiz->input_state->info) !=
        GST_VIDEO_INFO_HEIGHT (&state->info))
      thiz->do_renego = TRUE;
    gst_video_codec_state_unref (thiz->input_state);
  }
  thiz->input_state = gst_video_codec_state_ref (state);

  /* we don't set output state here to avoid caching of mismatched
   * video information if there is dynamic resolution change in the stream.
   * All negotiation code is consolidated in gst_msdkdec_negotiate() and
   * this will be invoked from handle_frame() */

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

/* This will get invoked in the following situations:
 * 1: begining of the stream, which requires initialization (== complete reset)
 * 2: upstream notified a resolution change and set do_renego to TRUE.
 *    new resoulution may or may not requires full reset
 * 3: upstream failed to notify the resoulution change but
 *    msdk detected the change (eg: vp9 stream in ivf elementary form
 *     with varying resolution frames).
 *
 * for any input configuration change, we deal with notification
 * from upstream and also use msdk apis to handle the parameter initialization
 * efficiently
 */
static gboolean
gst_msdkdec_negotiate (GstMsdkDec * thiz, gboolean hard_reset)
{
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (thiz);
  GST_DEBUG_OBJECT (thiz,
      "Start Negotiating caps, pool and Init the msdk decdoer subsystem");

  if (hard_reset) {
    /* Retrieve any pending frames and push them downstream */
    if (gst_msdkdec_drain (GST_VIDEO_DECODER (thiz)) != GST_FLOW_OK)
      goto error_drain;

    /* This will initiate the allocation query which will help to flush
     * all the pending buffers in the pipeline so that we can stop
     * the active bufferpool and safely invoke gst_msdk_frame_free() */
    if (thiz->initialized) {
      GstCaps *caps = gst_pad_get_current_caps (decoder->srcpad);
      GstQuery *query = NULL;
      if (caps) {
        query = gst_query_new_allocation (caps, FALSE);
        gst_pad_peer_query (decoder->srcpad, query);
        gst_query_unref (query);
        gst_caps_unref (caps);
      }
    }

    /* De-initialize the decoder if it is already active */
    /* Not resetting the mfxVideoParam since it already
     * possessing the required parameters for new session decode */
    gst_msdkdec_close_decoder (thiz, FALSE);

    /* request for pool renegotiation by setting do_realloc */
    thiz->do_realloc = TRUE;
  }

  /* At this point all pending frames(if there is any) are pushed downsteram
   * and we are ready to negotiate the output caps */
  if (!gst_msdkdec_set_src_caps (thiz, hard_reset))
    return FALSE;

  /* this will initiate the allocation query, we create the
   * bufferpool in decide_allocation inorder to account
   * the downstream min_buffer requirement
   * Required initializations for MediaSDK operations
   * will all be inited from decide_allocation after considering
   * some of the downstream requirements */
  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (thiz)))
    goto error_negotiate;

  thiz->do_renego = FALSE;
  thiz->do_realloc = FALSE;

  return TRUE;

error_drain:
  GST_ERROR_OBJECT (thiz, "Failed to Drain the queued decoded frames");
  return FALSE;

error_negotiate:
  GST_ERROR_OBJECT (thiz, "Failed to renegotiation");
  return FALSE;
}

static GstFlowReturn
gst_msdkdec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);
  GstMsdkDecClass *klass = GST_MSDKDEC_GET_CLASS (thiz);
  GstFlowReturn flow;
  GstBuffer *buffer, *input_buffer = NULL;
  GstVideoInfo alloc_info;
  MsdkDecTask *task = NULL;
  mfxBitstream bitstream;
  MsdkSurface *surface = NULL;
  mfxSession session;
  mfxStatus status;
  GstMapInfo map_info;
  guint i;
  gsize data_size;
  gboolean hard_reset = FALSE;

  /* configure the subclass in order to fill the CodecID field of
   * mfxVideoParam and also to load the PluginID for some of the
   * codecs which is mandatory to invoke the
   * MFXVideoDECODE_DecodeHeader API.
   *
   * For non packetized formats (currently only vc1), there
   * could be headers received as codec_data which are not available
   * instream and in that case subclass implementation will
   * push it to the internal adapter. We invoke the subclass configure
   * well early to make sure the codec_data received has been correctly
   * pushed to the adapter by the subclasses before doing
   * the DecodeHeader() later on
   */
  if (!thiz->initialized || thiz->do_renego) {
    /* Clear the internal adapter in renegotiation for non-packetized
     * formats */
    if (!thiz->is_packetized)
      gst_adapter_clear (thiz->adapter);

    if (!klass->configure || !klass->configure (thiz)) {
      flow = GST_FLOW_OK;
      goto error;
    }
  }

  /* Current frame-codec could be pushed and released before this
   * function ends -- because msdkdec pushes the oldest frame,
   * according its PTS, and it could be this very same frame-codec
   * among others pending frame-codecs.
   *
   * Instead of copying the input data into the mfxBitstream, let's
   * keep an extra reference to frame-codec's input buffer */
  input_buffer = gst_buffer_ref (frame->input_buffer);
  if (!gst_buffer_map (input_buffer, &map_info, GST_MAP_READ)) {
    gst_buffer_unref (input_buffer);
    return GST_FLOW_ERROR;
  }

  memset (&bitstream, 0, sizeof (bitstream));

  if (thiz->is_packetized) {
    /* Packetized stream: We prefer to have a parser as connected upstream
     * element to the decoder */
    bitstream.Data = map_info.data;
    bitstream.DataLength = map_info.size;
    bitstream.MaxLength = map_info.size;
    bitstream.DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;
  } else {
    /* Non packetized streams: eg: vc1 advanced profile with per buffer bdu */
    gst_adapter_push (thiz->adapter, gst_buffer_ref (input_buffer));
    data_size = gst_adapter_available (thiz->adapter);

    bitstream.Data = (mfxU8 *) gst_adapter_map (thiz->adapter, data_size);
    bitstream.DataLength = (mfxU32) data_size;
    bitstream.MaxLength = bitstream.DataLength;
  }
  GST_INFO_OBJECT (thiz,
      "mfxBitStream=> DataLength:%d DataOffset:%d MaxLength:%d",
      bitstream.DataLength, bitstream.DataOffset, bitstream.MaxLength);

  session = gst_msdk_context_get_session (thiz->context);

  if (!thiz->initialized || thiz->do_renego) {

    /* gstreamer caps will not bring all the necessary parameters
     * required for optimal decode configuration. For eg: the required numbers
     * of surfaces to be allocated can be calculated based on H264 SEI header
     * and this information can't be retrieved from the negotiated caps.
     * So instead of introducing the codecparser dependency to parse the headers
     * inside msdk plugin, we simply use the mfx apis to extract header information */
    status = MFXVideoDECODE_DecodeHeader (session, &bitstream, &thiz->param);
    if (status == MFX_ERR_MORE_DATA) {
      flow = GST_FLOW_OK;
      goto done;
    }

    if (!thiz->initialized)
      hard_reset = TRUE;
    else if (thiz->allocation_caps) {
      gst_video_info_from_caps (&alloc_info, thiz->allocation_caps);

      /* Check whether we need complete reset for dynamic resolution change */
      if (thiz->param.mfx.FrameInfo.Width > GST_VIDEO_INFO_WIDTH (&alloc_info)
          || thiz->param.mfx.FrameInfo.Height >
          GST_VIDEO_INFO_HEIGHT (&alloc_info))
        hard_reset = TRUE;
    }

    /* if subclass requested for the force reset */
    if (thiz->force_reset_on_res_change)
      hard_reset = TRUE;

    /* Config changed dynamically and we are going to do a full reset,
     * this will unref the input frame which has the new configuration.
     * Keep a ref to the input_frame to keep it alive */
    if (thiz->initialized && thiz->do_renego)
      gst_video_codec_frame_ref (frame);

    if (!gst_msdkdec_negotiate (thiz, hard_reset)) {
      GST_ELEMENT_ERROR (thiz, CORE, NEGOTIATION,
          ("Could not negotiate the stream"), (NULL));
      flow = GST_FLOW_ERROR;
      goto error;
    }
  }

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

    /* media-sdk requires complete reset since the surface is inadaquate to
     * do further decoding */
    if (status == MFX_ERR_INCOMPATIBLE_VIDEO_PARAM) {
      /* Requires memory re-allocation, do a hard reset */
      if (!gst_msdkdec_negotiate (thiz, TRUE))
        goto error;
      status =
          MFXVideoDECODE_DecodeFrameAsync (session, &bitstream,
          surface->surface, &task->surface, &task->sync_point);
    }

    if (G_LIKELY (status == MFX_ERR_NONE)
        || (status == MFX_WRN_VIDEO_PARAM_CHANGED)) {
      thiz->next_task = (thiz->next_task + 1) % thiz->tasks->len;

      if (surface->surface->Data.Locked > 0 || !thiz->use_video_memory)
        surface = NULL;

      if (bitstream.DataLength == 0) {
        flow = GST_FLOW_OK;
        break;
      }
    } else if (status == MFX_ERR_MORE_DATA) {
      if (task->surface) {
        task->decode_only = TRUE;
        thiz->next_task = (thiz->next_task + 1) % thiz->tasks->len;
      }

      if (surface->surface->Data.Locked > 0)
        surface = NULL;
      flow = GST_VIDEO_DECODER_FLOW_NEED_DATA;
      break;
    } else if (status == MFX_ERR_MORE_SURFACE) {
      surface = NULL;
      continue;
    } else if (status == MFX_WRN_DEVICE_BUSY) {
      /* If device is busy, wait 1ms and retry, as per MSDK's recomendation */
      g_usleep (1000);

      if (task->surface &&
          task->surface == surface->surface && !task->sync_point) {
        free_surface (thiz, surface);
        surface = NULL;
      }

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

done:
  if (surface)
    free_surface (thiz, surface);

  gst_buffer_unmap (input_buffer, &map_info);
  gst_buffer_unref (input_buffer);
  return flow;

error:
  if (input_buffer) {
    gst_buffer_unmap (input_buffer, &map_info);
    gst_buffer_unref (input_buffer);
  }
  gst_video_decoder_drop_frame (decoder, frame);

  return flow;
}


static GstBufferPool *
gst_msdkdec_create_buffer_pool (GstMsdkDec * thiz, GstVideoInfo * info,
    guint num_buffers)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstVideoAlignment align;
  GstCaps *caps = NULL;
  GstAllocationParams params = { 0, 31, 0, 0, };
  mfxFrameAllocResponse *alloc_resp = NULL;

  g_return_val_if_fail (info, NULL);
  g_return_val_if_fail (GST_VIDEO_INFO_WIDTH (info)
      && GST_VIDEO_INFO_HEIGHT (info), NULL);

  alloc_resp = &thiz->alloc_resp;

  pool = gst_msdk_buffer_pool_new (thiz->context, alloc_resp);
  if (!pool)
    goto error_no_pool;

  if (G_UNLIKELY (!IS_ALIGNED (GST_VIDEO_INFO_WIDTH (info), 16)
          || !IS_ALIGNED (GST_VIDEO_INFO_HEIGHT (info), 32))) {
    gst_msdk_set_video_alignment (info, &align);
    gst_video_info_align (info, &align);
  }

  caps = gst_video_info_to_caps (info);

  /* allocators should use the same width/height/stride/height_alignment of
   * negotiated output caps which is what we configure in msdk_allocator */
  if (thiz->use_dmabuf)
    allocator = gst_msdk_dmabuf_allocator_new (thiz->context, info, alloc_resp);
  else if (thiz->use_video_memory)
    allocator = gst_msdk_video_allocator_new (thiz->context, info, alloc_resp);
  else
    allocator = gst_msdk_system_allocator_new (info);

  if (!allocator)
    goto error_no_allocator;

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));
  gst_buffer_pool_config_set_params (config, caps,
      GST_VIDEO_INFO_SIZE (info), num_buffers, 0);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (thiz->use_video_memory) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_MSDK_USE_VIDEO_MEMORY);
    if (thiz->use_dmabuf)
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_MSDK_USE_DMABUF);
  }

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
    gst_object_unref (pool);
    return NULL;
  }
error_pool_config:
  {
    GST_INFO_OBJECT (thiz, "failed to set config");
    gst_object_unref (pool);
    gst_object_unref (allocator);
    return NULL;
  }
}

static gboolean
gst_msdkdec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstMsdkDec *thiz = GST_MSDKDEC (decoder);
  GstBufferPool *pool = NULL;
  GstStructure *pool_config = NULL;
  GstCaps *pool_caps /*, *negotiated_caps */ ;
  guint size, min_buffers, max_buffers;

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query))
    return FALSE;

  /* Get the buffer pool config decided by the base class. The base
     class ensures that there will always be at least a 0th pool in
     the query. */
  gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);
  pool_config = gst_buffer_pool_get_config (pool);

  /* Get the caps of pool and increase the min and max buffers by async_depth,
   * we will always have that number of decode operations in-flight */
  gst_buffer_pool_config_get_params (pool_config, &pool_caps, &size,
      &min_buffers, &max_buffers);
  min_buffers += thiz->async_depth;
  if (max_buffers)
    max_buffers += thiz->async_depth;

  /* increase the min_buffers by 1 for smooth display in render pipeline */
  min_buffers += 1;

  /* this will get updated with msdk requirement */
  thiz->min_prealloc_buffers = min_buffers;

  if (_gst_caps_has_feature (pool_caps, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
    GST_INFO_OBJECT (decoder, "This MSDK decoder uses DMABuf memory");
    thiz->use_video_memory = thiz->use_dmabuf = TRUE;
  }

  /* Initialize MSDK decoder before new bufferpool tries to alloc each buffer,
   * which requires information of frame allocation.
   * No effect if already initialized.
   */
  if (!gst_msdkdec_init_decoder (thiz))
    return FALSE;

  /* get the updated min_buffers which account the msdk requirement too */
  min_buffers = thiz->min_prealloc_buffers;

  /* Decoder always use its own pool. So we create a pool if msdk apis
   * previously requested for allocation (do_realloc = TRUE) */
  if (thiz->do_realloc || !thiz->pool) {
    if (thiz->pool)
      gst_object_replace ((GstObject **) & thiz->pool, NULL);
    GST_INFO_OBJECT (decoder, "create new MSDK bufferpool");
    thiz->pool =
        gst_msdkdec_create_buffer_pool (thiz, &thiz->output_info, min_buffers);
    if (!thiz->pool)
      goto failed_to_create_pool;
  }

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)
      && gst_buffer_pool_has_option (pool,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
    GstStructure *config;
    GstAllocator *allocator;

    /* If downstream supports video meta and video alignment,
     * we can replace our own msdk bufferpool and use it
     */
    /* Remove downstream's pool */
    gst_structure_free (pool_config);
    gst_object_unref (pool);

    pool = gst_object_ref (thiz->pool);

    /* Set the allocator of new msdk bufferpool */
    config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));

    if (gst_buffer_pool_config_get_allocator (config, &allocator, NULL))
      gst_query_set_nth_allocation_param (query, 0, allocator, NULL);
    gst_structure_free (config);
  } else {
    /* Unfortunately, dowstream doesn't have videometa or alignment support,
     * we keep msdk pool as a side-pool that will be decoded into and
     * then copied from.
     */
    GST_INFO_OBJECT (decoder, "Keep MSDK bufferpool as a side-pool");

    /* Update params to downstream's pool */
    gst_buffer_pool_config_set_params (pool_config, pool_caps, size,
        min_buffers, max_buffers);
    if (!gst_buffer_pool_set_config (pool, pool_config))
      goto error_set_config;
    gst_video_info_from_caps (&thiz->non_msdk_pool_info, pool_caps);

    /* update width and height with actual negotiated values */
    GST_VIDEO_INFO_WIDTH (&thiz->non_msdk_pool_info) =
        GST_VIDEO_INFO_WIDTH (&thiz->output_info);
    GST_VIDEO_INFO_HEIGHT (&thiz->non_msdk_pool_info) =
        GST_VIDEO_INFO_HEIGHT (&thiz->output_info);
  }

  gst_query_set_nth_allocation_pool (query, 0, pool, size, min_buffers,
      max_buffers);

  if (pool)
    gst_object_unref (pool);


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
    case GST_MSDKDEC_PROP_HARDWARE:
      thiz->hardware = g_value_get_boolean (value);
      break;
    case GST_MSDKDEC_PROP_ASYNC_DEPTH:
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
    case GST_MSDKDEC_PROP_HARDWARE:
      g_value_set_boolean (value, thiz->hardware);
      break;
    case GST_MSDKDEC_PROP_ASYNC_DEPTH:
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

  g_object_class_install_property (gobject_class, GST_MSDKDEC_PROP_HARDWARE,
      g_param_spec_boolean ("hardware", "Hardware", "Enable hardware decoders",
          PROP_HARDWARE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, GST_MSDKDEC_PROP_ASYNC_DEPTH,
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
  gst_video_info_init (&thiz->non_msdk_pool_info);
  thiz->tasks = g_array_new (FALSE, TRUE, sizeof (MsdkDecTask));
  thiz->hardware = PROP_HARDWARE_DEFAULT;
  thiz->async_depth = PROP_ASYNC_DEPTH_DEFAULT;
  thiz->is_packetized = TRUE;
  thiz->do_renego = TRUE;
  thiz->do_realloc = TRUE;
  thiz->force_reset_on_res_change = TRUE;
  thiz->adapter = gst_adapter_new ();
}
