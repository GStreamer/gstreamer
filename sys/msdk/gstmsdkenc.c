/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Oblong Industries, Inc.
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

/* TODO:
 *  - Add support for interlaced content
 *  - Add support for MVC AVC
 *  - Wrap more configuration options and maybe move properties to derived
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#ifdef _WIN32
#  include <malloc.h>
#endif

#include <stdlib.h>

#include "gstmsdkenc.h"

static inline void *
_aligned_alloc (size_t alignment, size_t size)
{
#ifdef _WIN32
  return _aligned_malloc (size, alignment);
#else
  void *out;
  if (posix_memalign (&out, alignment, size) != 0)
    out = NULL;
  return out;
#endif
}

#ifndef _WIN32
#define _aligned_free free
#endif

static void gst_msdkenc_close_encoder (GstMsdkEnc * thiz);

GST_DEBUG_CATEGORY_EXTERN (gst_msdkenc_debug);
#define GST_CAT_DEFAULT gst_msdkenc_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { NV12, I420, YV12, YUY2, UYVY, BGRA }, "
        "framerate = (fraction) [0, MAX], "
        "width = (int) [ 16, MAX ], height = (int) [ 16, MAX ],"
        "interlace-mode = (string) progressive")
    );

enum
{
  PROP_0,
  PROP_HARDWARE,
  PROP_ASYNC_DEPTH,
  PROP_TARGET_USAGE,
  PROP_RATE_CONTROL,
  PROP_BITRATE,
  PROP_QPI,
  PROP_QPP,
  PROP_QPB,
  PROP_GOP_SIZE,
  PROP_REF_FRAMES,
  PROP_I_FRAMES,
  PROP_B_FRAMES
};

#define PROP_HARDWARE_DEFAULT            TRUE
#define PROP_ASYNC_DEPTH_DEFAULT         4
#define PROP_TARGET_USAGE_DEFAULT        (MFX_TARGETUSAGE_BALANCED)
#define PROP_RATE_CONTROL_DEFAULT        (MFX_RATECONTROL_CBR)
#define PROP_BITRATE_DEFAULT             (2 * 1024)
#define PROP_QPI_DEFAULT                 0
#define PROP_QPP_DEFAULT                 0
#define PROP_QPB_DEFAULT                 0
#define PROP_GOP_SIZE_DEFAULT            256
#define PROP_REF_FRAMES_DEFAULT          1
#define PROP_I_FRAMES_DEFAULT            0
#define PROP_B_FRAMES_DEFAULT            0

#define GST_MSDKENC_RATE_CONTROL_TYPE (gst_msdkenc_rate_control_get_type())
static GType
gst_msdkenc_rate_control_get_type (void)
{
  static GType type = 0;

  static const GEnumValue values[] = {
    {MFX_RATECONTROL_CBR, "Constant Bitrate", "cbr"},
    {MFX_RATECONTROL_VBR, "Variable Bitrate", "vbr"},
    {MFX_RATECONTROL_CQP, "Constant Quantizer", "cqp"},
    {MFX_RATECONTROL_AVBR, "Average Bitrate", "avbr"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("GstMsdkEncRateControl", values);
  }
  return type;
}

#define gst_msdkenc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkEnc, gst_msdkenc, GST_TYPE_VIDEO_ENCODER);

void
gst_msdkenc_add_extra_param (GstMsdkEnc * thiz, mfxExtBuffer * param)
{
  if (thiz->num_extra_params < MAX_EXTRA_PARAMS) {
    thiz->extra_params[thiz->num_extra_params] = param;
    thiz->num_extra_params++;
  }
}

static void
gst_msdkenc_alloc_surfaces (GstMsdkEnc * thiz, GstVideoFormat format,
    gint width, gint height, guint num_surfaces, mfxFrameSurface1 * surfaces)
{
  gsize Y_size = 0, U_size = 0;
  gsize pitch;
  gsize size;
  gint i;

  width = GST_ROUND_UP_32 (width);
  height = GST_ROUND_UP_32 (height);

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      Y_size = width * height;
      pitch = width;
      size = Y_size + (Y_size >> 1);
      break;
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420:
      Y_size = width * height;
      pitch = width;
      U_size = (width / 2) * (height / 2);
      size = Y_size + 2 * U_size;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      size = 2 * width * height;
      pitch = 2 * width;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      size = 4 * width * height;
      pitch = 4 * width;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  for (i = 0; i < num_surfaces; i++) {
    mfxFrameSurface1 *surface = &surfaces[i];
    mfxU8 *data = _aligned_alloc (32, size);
    if (!data) {
      GST_ERROR_OBJECT (thiz, "Memory allocation failed");
      return;
    }

    surface->Data.MemId = (mfxMemId) data;
    surface->Data.Pitch = pitch;
    surface->Data.Y = data;
    if (U_size) {
      surface->Data.U = data + Y_size;
      surface->Data.V = data + Y_size + U_size;
    } else if (Y_size) {
      surface->Data.UV = data + Y_size;
    }

    if (format == GST_VIDEO_FORMAT_YUY2) {
      surface->Data.U = data + 1;
      surface->Data.V = data + 3;
    } else if (format == GST_VIDEO_FORMAT_UYVY) {
      surface->Data.U = data + 1;
      surface->Data.Y = data + 2;
      surface->Data.V = data + 3;
    } else if (format == GST_VIDEO_FORMAT_BGRA) {
      surface->Data.R = data;
      surface->Data.G = data + 1;
      surface->Data.B = data + 2;
    }
  }
}

static gboolean
gst_msdkenc_init_encoder (GstMsdkEnc * thiz)
{
  GstMsdkEncClass *klass = GST_MSDKENC_GET_CLASS (thiz);
  GstVideoInfo *info;
  mfxSession session;
  mfxStatus status;
  mfxFrameAllocRequest request[2];
  guint i;

  if (!thiz->input_state) {
    GST_DEBUG_OBJECT (thiz, "Have no input state yet");
    return FALSE;
  }
  info = &thiz->input_state->info;

  /* make sure that the encoder is closed */
  gst_msdkenc_close_encoder (thiz);

  thiz->context = gst_msdk_context_new (thiz->hardware);
  if (!thiz->context) {
    GST_ERROR_OBJECT (thiz, "Context creation failed");
    return FALSE;
  }

  GST_OBJECT_LOCK (thiz);
  session = gst_msdk_context_get_session (thiz->context);

  thiz->has_vpp = FALSE;
  if (info->finfo->format != GST_VIDEO_FORMAT_NV12) {
    thiz->vpp_param.IOPattern =
        MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    thiz->vpp_param.vpp.In.Width = GST_ROUND_UP_32 (info->width);
    thiz->vpp_param.vpp.In.Height = GST_ROUND_UP_32 (info->height);
    thiz->vpp_param.vpp.In.CropW = info->width;
    thiz->vpp_param.vpp.In.CropH = info->height;
    thiz->vpp_param.vpp.In.FrameRateExtN = info->fps_n;
    thiz->vpp_param.vpp.In.FrameRateExtD = info->fps_d;
    thiz->vpp_param.vpp.In.AspectRatioW = info->par_n;
    thiz->vpp_param.vpp.In.AspectRatioH = info->par_d;
    thiz->vpp_param.vpp.In.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    switch (info->finfo->format) {
      case GST_VIDEO_FORMAT_NV12:
        thiz->vpp_param.vpp.In.FourCC = MFX_FOURCC_NV12;
        thiz->vpp_param.vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
        break;
      case GST_VIDEO_FORMAT_YV12:
      case GST_VIDEO_FORMAT_I420:
        thiz->vpp_param.vpp.In.FourCC = MFX_FOURCC_YV12;
        thiz->vpp_param.vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
        break;
      case GST_VIDEO_FORMAT_YUY2:
        thiz->vpp_param.vpp.In.FourCC = MFX_FOURCC_YUY2;
        thiz->vpp_param.vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV422;
        break;
      case GST_VIDEO_FORMAT_UYVY:
        thiz->vpp_param.vpp.In.FourCC = MFX_FOURCC_UYVY;
        thiz->vpp_param.vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV422;
        break;
      case GST_VIDEO_FORMAT_BGRA:
        thiz->vpp_param.vpp.In.FourCC = MFX_FOURCC_RGB4;
        thiz->vpp_param.vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV444;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    thiz->vpp_param.vpp.Out = thiz->vpp_param.vpp.In;
    thiz->vpp_param.vpp.Out.FourCC = MFX_FOURCC_NV12;
    thiz->vpp_param.vpp.Out.ChromaFormat = MFX_CHROMAFORMAT_YUV420;

    /* validate parameters and allow the Media SDK to make adjustments */
    status = MFXVideoVPP_Query (session, &thiz->vpp_param, &thiz->vpp_param);
    if (status < MFX_ERR_NONE) {
      GST_ERROR_OBJECT (thiz, "Video VPP Query failed (%s)",
          msdk_status_to_string (status));
      goto no_vpp;
    } else if (status > MFX_ERR_NONE) {
      GST_WARNING_OBJECT (thiz, "Video VPP Query returned: %s",
          msdk_status_to_string (status));
    }

    status = MFXVideoVPP_QueryIOSurf (session, &thiz->vpp_param, request);
    if (status < MFX_ERR_NONE) {
      GST_ERROR_OBJECT (thiz, "VPP Query IO surfaces failed (%s)",
          msdk_status_to_string (status));
      goto no_vpp;
    } else if (status > MFX_ERR_NONE) {
      GST_WARNING_OBJECT (thiz, "VPP Query IO surfaces returned: %s",
          msdk_status_to_string (status));
    }

    thiz->num_vpp_surfaces = request[0].NumFrameSuggested;
    thiz->vpp_surfaces = g_new0 (mfxFrameSurface1, thiz->num_vpp_surfaces);
    for (i = 0; i < thiz->num_vpp_surfaces; i++) {
      memcpy (&thiz->vpp_surfaces[i].Info, &thiz->vpp_param.vpp.In,
          sizeof (mfxFrameInfo));
    }

    status = MFXVideoVPP_Init (session, &thiz->vpp_param);
    if (status < MFX_ERR_NONE) {
      GST_ERROR_OBJECT (thiz, "Init failed (%s)",
          msdk_status_to_string (status));
      goto no_vpp;
    } else if (status > MFX_ERR_NONE) {
      GST_WARNING_OBJECT (thiz, "Init returned: %s",
          msdk_status_to_string (status));
    }

    status = MFXVideoVPP_GetVideoParam (session, &thiz->vpp_param);
    if (status < MFX_ERR_NONE) {
      GST_ERROR_OBJECT (thiz, "Get VPP Parameters failed (%s)",
          msdk_status_to_string (status));
      MFXVideoVPP_Close (session);
      goto no_vpp;
    } else if (status > MFX_ERR_NONE) {
      GST_WARNING_OBJECT (thiz, "Get VPP Parameters returned: %s",
          msdk_status_to_string (status));
    }

    thiz->has_vpp = TRUE;
  }

  thiz->param.AsyncDepth = thiz->async_depth;
  thiz->param.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

  thiz->param.mfx.RateControlMethod = thiz->rate_control;
  thiz->param.mfx.TargetKbps = thiz->bitrate;
  thiz->param.mfx.TargetUsage = thiz->target_usage;
  thiz->param.mfx.GopPicSize = thiz->gop_size;
  thiz->param.mfx.GopRefDist = thiz->b_frames + 1;
  thiz->param.mfx.IdrInterval = thiz->i_frames;
  thiz->param.mfx.NumRefFrame = thiz->ref_frames;
  thiz->param.mfx.EncodedOrder = 0;     /* Take input frames in display order */

  if (thiz->rate_control == MFX_RATECONTROL_CQP) {
    thiz->param.mfx.QPI = thiz->qpi;
    thiz->param.mfx.QPP = thiz->qpp;
    thiz->param.mfx.QPB = thiz->qpb;
  }

  thiz->param.mfx.FrameInfo.Width = GST_ROUND_UP_32 (info->width);
  thiz->param.mfx.FrameInfo.Height = GST_ROUND_UP_32 (info->height);
  thiz->param.mfx.FrameInfo.CropW = info->width;
  thiz->param.mfx.FrameInfo.CropH = info->height;
  thiz->param.mfx.FrameInfo.FrameRateExtN = info->fps_n;
  thiz->param.mfx.FrameInfo.FrameRateExtD = info->fps_d;
  thiz->param.mfx.FrameInfo.AspectRatioW = info->par_n;
  thiz->param.mfx.FrameInfo.AspectRatioH = info->par_d;
  thiz->param.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
  thiz->param.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
  thiz->param.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;

  /* allow subclass configure further */
  if (klass->configure) {
    if (!klass->configure (thiz))
      goto failed;
  }

  if (thiz->num_extra_params) {
    thiz->param.NumExtParam = thiz->num_extra_params;
    thiz->param.ExtParam = thiz->extra_params;
  }

  /* validate parameters and allow the Media SDK to make adjustments */
  status = MFXVideoENCODE_Query (session, &thiz->param, &thiz->param);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "Video Encode Query failed (%s)",
        msdk_status_to_string (status));
    goto failed;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "Video Encode Query returned: %s",
        msdk_status_to_string (status));
  }

  status = MFXVideoENCODE_QueryIOSurf (session, &thiz->param, request);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "Encode Query IO surfaces failed (%s)",
        msdk_status_to_string (status));
    goto failed;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "Encode Query IO surfaces returned: %s",
        msdk_status_to_string (status));
  }

  /* Maximum of VPP output and encoder input, if using VPP */
  if (thiz->has_vpp)
    request[0].NumFrameSuggested =
        MAX (request[0].NumFrameSuggested, request[1].NumFrameSuggested);
  if (request[0].NumFrameSuggested < thiz->param.AsyncDepth) {
    GST_ERROR_OBJECT (thiz, "Required %d surfaces (%d suggested), async %d",
        request[0].NumFrameMin, request[0].NumFrameSuggested,
        thiz->param.AsyncDepth);
    goto failed;
  }

  /* These are VPP output (if any) and encoder input */
  thiz->num_surfaces = request[0].NumFrameSuggested;
  thiz->surfaces = g_new0 (mfxFrameSurface1, thiz->num_surfaces);
  for (i = 0; i < thiz->num_surfaces; i++) {
    memcpy (&thiz->surfaces[i].Info, &thiz->param.mfx.FrameInfo,
        sizeof (mfxFrameInfo));
  }

  if ((GST_ROUND_UP_32 (info->width) != info->width
          || GST_ROUND_UP_32 (info->height) != info->height)) {
    gst_msdkenc_alloc_surfaces (thiz, info->finfo->format, info->width,
        info->height,
        thiz->has_vpp ? thiz->num_vpp_surfaces : thiz->num_surfaces,
        thiz->has_vpp ? thiz->vpp_surfaces : thiz->surfaces);
    GST_DEBUG_OBJECT (thiz,
        "Allocated aligned memory, pixel data will be copied");
  }
  if (thiz->has_vpp) {
    gst_msdkenc_alloc_surfaces (thiz, GST_VIDEO_FORMAT_NV12, info->width,
        info->height, thiz->num_surfaces, thiz->surfaces);
  }

  GST_DEBUG_OBJECT (thiz, "Required %d surfaces (%d suggested), allocated %d",
      request[0].NumFrameMin, request[0].NumFrameSuggested, thiz->num_surfaces);

  status = MFXVideoENCODE_Init (session, &thiz->param);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "Init failed (%s)", msdk_status_to_string (status));
    goto failed;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "Init returned: %s",
        msdk_status_to_string (status));
  }

  status = MFXVideoENCODE_GetVideoParam (session, &thiz->param);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (thiz, "Get Video Parameters failed (%s)",
        msdk_status_to_string (status));
    goto failed;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (thiz, "Get Video Parameters returned: %s",
        msdk_status_to_string (status));
  }

  thiz->num_tasks = thiz->param.AsyncDepth;
  thiz->tasks = g_new0 (MsdkEncTask, thiz->num_tasks);
  for (i = 0; i < thiz->num_tasks; i++) {
    thiz->tasks[i].output_bitstream.Data = _aligned_alloc (32,
        thiz->param.mfx.BufferSizeInKB * 1024);
    if (!thiz->tasks[i].output_bitstream.Data) {
      GST_ERROR_OBJECT (thiz, "Memory allocation failed");
      goto failed;
    }
    thiz->tasks[i].output_bitstream.MaxLength =
        thiz->param.mfx.BufferSizeInKB * 1024;
  }
  thiz->next_task = 0;

  thiz->reconfig = FALSE;

  GST_OBJECT_UNLOCK (thiz);

  return TRUE;

no_vpp:
failed:
  GST_OBJECT_UNLOCK (thiz);
  if (thiz->context)
    gst_object_replace ((GstObject **) & thiz->context, NULL);
  return FALSE;
}

static void
gst_msdkenc_close_encoder (GstMsdkEnc * thiz)
{
  guint i;
  mfxStatus status;

  if (!thiz->context)
    return;

  GST_DEBUG_OBJECT (thiz, "Closing encoder 0x%p", thiz->context);

  status = MFXVideoENCODE_Close (gst_msdk_context_get_session (thiz->context));
  if (status != MFX_ERR_NONE && status != MFX_ERR_NOT_INITIALIZED) {
    GST_WARNING_OBJECT (thiz, "Encoder close failed (%s)",
        msdk_status_to_string (status));
  }

  if (thiz->tasks) {
    for (i = 0; i < thiz->num_tasks; i++) {
      MsdkEncTask *task = &thiz->tasks[i];
      if (task->output_bitstream.Data) {
        _aligned_free (task->output_bitstream.Data);
      }
    }
  }
  g_free (thiz->tasks);
  thiz->tasks = NULL;

  /* Close VPP before freeing the surfaces. They are shared between encoder
   * and VPP */
  if (thiz->has_vpp) {
    status = MFXVideoVPP_Close (gst_msdk_context_get_session (thiz->context));
    if (status != MFX_ERR_NONE && status != MFX_ERR_NOT_INITIALIZED) {
      GST_WARNING_OBJECT (thiz, "VPP close failed (%s)",
          msdk_status_to_string (status));
    }
  }

  for (i = 0; i < thiz->num_surfaces; i++) {
    mfxFrameSurface1 *surface = &thiz->surfaces[i];
    if (surface->Data.MemId)
      _aligned_free (surface->Data.MemId);
  }
  g_free (thiz->surfaces);
  thiz->surfaces = NULL;

  if (thiz->has_vpp) {
    for (i = 0; i < thiz->num_vpp_surfaces; i++) {
      mfxFrameSurface1 *surface = &thiz->vpp_surfaces[i];
      if (surface->Data.MemId)
        _aligned_free (surface->Data.MemId);
    }
    g_free (thiz->vpp_surfaces);
    thiz->vpp_surfaces = NULL;
  }

  if (thiz->context)
    gst_object_replace ((GstObject **) & thiz->context, NULL);

  memset (&thiz->param, 0, sizeof (thiz->param));
  thiz->num_extra_params = 0;
}

typedef struct
{
  GstVideoCodecFrame *frame;
  GstVideoFrame vframe;
} FrameData;

static FrameData *
gst_msdkenc_queue_frame (GstMsdkEnc * thiz, GstVideoCodecFrame * frame,
    GstVideoInfo * info)
{
  GstVideoFrame vframe;
  FrameData *fdata;

  if (!gst_video_frame_map (&vframe, info, frame->input_buffer, GST_MAP_READ))
    return NULL;

  fdata = g_slice_new (FrameData);
  fdata->frame = gst_video_codec_frame_ref (frame);
  fdata->vframe = vframe;

  thiz->pending_frames = g_list_prepend (thiz->pending_frames, fdata);

  return fdata;
}

static void
gst_msdkenc_dequeue_frame (GstMsdkEnc * thiz, GstVideoCodecFrame * frame)
{
  GList *l;

  for (l = thiz->pending_frames; l; l = l->next) {
    FrameData *fdata = l->data;

    if (fdata->frame != frame)
      continue;

    if (fdata->vframe.buffer)
      gst_video_frame_unmap (&fdata->vframe);
    gst_video_codec_frame_unref (fdata->frame);
    g_slice_free (FrameData, fdata);

    thiz->pending_frames = g_list_delete_link (thiz->pending_frames, l);
    return;
  }
}

static void
gst_msdkenc_dequeue_all_frames (GstMsdkEnc * thiz)
{
  GList *l;

  for (l = thiz->pending_frames; l; l = l->next) {
    FrameData *fdata = l->data;

    gst_video_frame_unmap (&fdata->vframe);
    gst_video_codec_frame_unref (fdata->frame);
    g_slice_free (FrameData, fdata);
  }
  g_list_free (thiz->pending_frames);
  thiz->pending_frames = NULL;
}

static MsdkEncTask *
gst_msdkenc_get_free_task (GstMsdkEnc * thiz)
{
  MsdkEncTask *tasks = thiz->tasks;
  guint size = thiz->num_tasks;
  guint start = thiz->next_task;
  guint i;

  if (tasks) {
    for (i = 0; i < size; i++) {
      guint t = (start + i) % size;
      if (tasks[t].sync_point == NULL)
        return &tasks[t];
    }
  }
  return NULL;
}

static void
gst_msdkenc_reset_task (MsdkEncTask * task)
{
  task->input_frame = NULL;
  task->output_bitstream.DataLength = 0;
  task->sync_point = NULL;
  task->more_data = FALSE;
}

static GstFlowReturn
gst_msdkenc_finish_frame (GstMsdkEnc * thiz, MsdkEncTask * task,
    gboolean discard)
{
  GstVideoCodecFrame *frame = task->input_frame;

  if (task->more_data) {
    GstVideoCodecFrame *frame;

    frame =
        gst_video_encoder_get_frame (GST_VIDEO_ENCODER_CAST (thiz),
        task->pending_frame_number);
    if (frame) {
      gst_msdkenc_dequeue_frame (thiz, frame);
      gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (thiz), frame);
      gst_msdkenc_reset_task (task);
      return GST_FLOW_OK;
    } else {
      GST_ERROR_OBJECT (thiz,
          "Couldn't find the pending frame %d to be finished",
          task->pending_frame_number);
      return GST_FLOW_ERROR;
    }
  }

  if (!task->sync_point) {
    return GST_FLOW_OK;
  }

  /* Wait for encoding operation to complete */
  MFXVideoCORE_SyncOperation (gst_msdk_context_get_session (thiz->context),
      task->sync_point, 10000);
  if (!discard && task->output_bitstream.DataLength) {
    GstBuffer *out_buf = NULL;
    guint8 *data =
        task->output_bitstream.Data + task->output_bitstream.DataOffset;
    gsize size = task->output_bitstream.DataLength;
    out_buf = gst_buffer_new_allocate (NULL, size, NULL);
    gst_buffer_fill (out_buf, 0, data, size);
    frame->output_buffer = out_buf;
    frame->pts =
        gst_util_uint64_scale (task->output_bitstream.TimeStamp, GST_SECOND,
        90000);
    frame->dts =
        gst_util_uint64_scale (task->output_bitstream.DecodeTimeStamp,
        GST_SECOND, 90000);

    if ((task->output_bitstream.FrameType & MFX_FRAMETYPE_IDR) != 0 ||
        (task->output_bitstream.FrameType & MFX_FRAMETYPE_xIDR) != 0) {
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    }

    /* Mark task as available */
    gst_msdkenc_reset_task (task);
  }

  gst_msdkenc_dequeue_frame (thiz, frame);
  return gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (thiz), frame);
}

static GstFlowReturn
gst_msdkenc_encode_frame (GstMsdkEnc * thiz, mfxFrameSurface1 * surface,
    GstVideoCodecFrame * input_frame)
{
  mfxSession session;
  MsdkEncTask *task;
  mfxStatus status;

  if (G_UNLIKELY (thiz->context == NULL)) {
    gst_msdkenc_dequeue_frame (thiz, input_frame);
    gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (thiz), input_frame);
    return GST_FLOW_NOT_NEGOTIATED;
  }
  session = gst_msdk_context_get_session (thiz->context);

  task = gst_msdkenc_get_free_task (thiz);

  for (;;) {
    status = MFXVideoENCODE_EncodeFrameAsync (session, NULL, surface,
        &task->output_bitstream, &task->sync_point);
    if (status != MFX_WRN_DEVICE_BUSY)
      break;
    /* If device is busy, wait 1ms and retry, as per MSDK's recomendation */
    g_usleep (1000);
  };

  if (status != MFX_ERR_NONE && status != MFX_ERR_MORE_DATA) {
    GST_ELEMENT_ERROR (thiz, STREAM, ENCODE, ("Encode frame failed."),
        ("MSDK encode error (%s)", msdk_status_to_string (status)));
    gst_msdkenc_dequeue_frame (thiz, input_frame);
    gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (thiz), input_frame);
    return GST_FLOW_ERROR;
  }

  if (task->sync_point) {
    task->input_frame = input_frame;
    thiz->next_task = ((task - thiz->tasks) + 1) % thiz->num_tasks;
  } else if (status == MFX_ERR_MORE_DATA) {
    task->more_data = TRUE;
    task->pending_frame_number = input_frame->system_frame_number;
    gst_video_codec_frame_unref (input_frame);
    thiz->next_task = ((task - thiz->tasks) + 1) % thiz->num_tasks;
  }

  /* Ensure that next task is available */
  task = thiz->tasks + thiz->next_task;
  return gst_msdkenc_finish_frame (thiz, task, FALSE);
}

static guint
gst_msdkenc_maximum_delayed_frames (GstMsdkEnc * thiz)
{
  return thiz->num_tasks;
}

static void
gst_msdkenc_set_latency (GstMsdkEnc * thiz)
{
  GstVideoInfo *info = &thiz->input_state->info;
  gint max_delayed_frames;
  GstClockTime latency;

  max_delayed_frames = gst_msdkenc_maximum_delayed_frames (thiz);

  if (info->fps_n) {
    latency = gst_util_uint64_scale_ceil (GST_SECOND * info->fps_d,
        max_delayed_frames, info->fps_n);
  } else {
    /* FIXME: Assume 25fps. This is better than reporting no latency at
     * all and then later failing in live pipelines
     */
    latency = gst_util_uint64_scale_ceil (GST_SECOND * 1,
        max_delayed_frames, 25);
  }

  GST_INFO_OBJECT (thiz,
      "Updating latency to %" GST_TIME_FORMAT " (%d frames)",
      GST_TIME_ARGS (latency), max_delayed_frames);

  gst_video_encoder_set_latency (GST_VIDEO_ENCODER (thiz), latency, latency);
}

static void
gst_msdkenc_flush_frames (GstMsdkEnc * thiz, gboolean discard)
{
  guint i, t = thiz->next_task;

  if (!thiz->tasks)
    return;

  for (i = 0; i < thiz->num_tasks; i++) {
    gst_msdkenc_finish_frame (thiz, &thiz->tasks[t], discard);
    t = (t + 1) % thiz->num_tasks;
  }
}

static gboolean
gst_msdkenc_set_src_caps (GstMsdkEnc * thiz)
{
  GstMsdkEncClass *klass = GST_MSDKENC_GET_CLASS (thiz);
  GstCaps *outcaps = NULL;
  GstVideoCodecState *state;
  GstTagList *tags;

  if (klass->set_src_caps)
    outcaps = klass->set_src_caps (thiz);

  if (!outcaps)
    return FALSE;

  state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (thiz),
      outcaps, thiz->input_state);
  GST_DEBUG_OBJECT (thiz, "output caps: %" GST_PTR_FORMAT, state->caps);

  gst_video_codec_state_unref (state);

  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER, "msdkenc",
      GST_TAG_MAXIMUM_BITRATE, thiz->bitrate * 1024,
      GST_TAG_NOMINAL_BITRATE, thiz->bitrate * 1024, NULL);
  gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (thiz), tags,
      GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (tags);

  return TRUE;
}

static gboolean
gst_msdkenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstMsdkEnc *thiz = GST_MSDKENC (encoder);
  GstMsdkEncClass *klass = GST_MSDKENC_GET_CLASS (thiz);

  if (state) {
    if (thiz->input_state)
      gst_video_codec_state_unref (thiz->input_state);
    thiz->input_state = gst_video_codec_state_ref (state);
  }

  if (klass->set_format) {
    if (!klass->set_format (thiz))
      return FALSE;
  }

  if (!gst_msdkenc_init_encoder (thiz))
    return FALSE;

  if (!gst_msdkenc_set_src_caps (thiz)) {
    gst_msdkenc_close_encoder (thiz);
    return FALSE;
  }

  gst_msdkenc_set_latency (thiz);

  return TRUE;
}

static GstFlowReturn
gst_msdkenc_handle_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstMsdkEnc *thiz = GST_MSDKENC (encoder);
  GstVideoInfo *info = &thiz->input_state->info;
  FrameData *fdata;
  mfxFrameSurface1 *surface;

  if (thiz->reconfig) {
    gst_msdkenc_flush_frames (thiz, FALSE);
    gst_msdkenc_set_format (encoder, NULL);
  }

  if (G_UNLIKELY (thiz->context == NULL))
    goto not_inited;

  if (thiz->has_vpp) {
    mfxFrameSurface1 *vpp_surface;
    GstVideoFrame vframe;
    mfxSession session;
    mfxSyncPoint vpp_sync_point = NULL;
    mfxStatus status;

    vpp_surface =
        msdk_get_free_surface (thiz->vpp_surfaces, thiz->num_vpp_surfaces);
    if (!vpp_surface)
      goto invalid_surface;
    surface = msdk_get_free_surface (thiz->surfaces, thiz->num_surfaces);
    if (!surface)
      goto invalid_surface;

    if (!gst_video_frame_map (&vframe, info, frame->input_buffer, GST_MAP_READ))
      goto invalid_frame;

    msdk_frame_to_surface (&vframe, vpp_surface);
    if (frame->pts != GST_CLOCK_TIME_NONE) {
      vpp_surface->Data.TimeStamp =
          gst_util_uint64_scale (frame->pts, 90000, GST_SECOND);
      surface->Data.TimeStamp =
          gst_util_uint64_scale (frame->pts, 90000, GST_SECOND);
    } else {
      vpp_surface->Data.TimeStamp = MFX_TIMESTAMP_UNKNOWN;
      surface->Data.TimeStamp = MFX_TIMESTAMP_UNKNOWN;
    }

    session = gst_msdk_context_get_session (thiz->context);
    for (;;) {
      status =
          MFXVideoVPP_RunFrameVPPAsync (session, vpp_surface, surface, NULL,
          &vpp_sync_point);
      if (status != MFX_WRN_DEVICE_BUSY)
        break;
      /* If device is busy, wait 1ms and retry, as per MSDK's recomendation */
      g_usleep (1000);
    };

    gst_video_frame_unmap (&vframe);

    if (status != MFX_ERR_NONE && status != MFX_ERR_MORE_DATA) {
      GST_ELEMENT_ERROR (thiz, STREAM, ENCODE, ("Converting frame failed."),
          ("MSDK VPP error (%s)", msdk_status_to_string (status)));
      gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (thiz), frame);
      return GST_FLOW_ERROR;
    }

    fdata = g_slice_new0 (FrameData);
    fdata->frame = gst_video_codec_frame_ref (frame);

    thiz->pending_frames = g_list_prepend (thiz->pending_frames, fdata);
  } else {
    surface = msdk_get_free_surface (thiz->surfaces, thiz->num_surfaces);
    if (!surface)
      goto invalid_surface;

    fdata = gst_msdkenc_queue_frame (thiz, frame, info);
    if (!fdata)
      goto invalid_frame;

    msdk_frame_to_surface (&fdata->vframe, surface);
    if (frame->pts != GST_CLOCK_TIME_NONE) {
      surface->Data.TimeStamp =
          gst_util_uint64_scale (frame->pts, 90000, GST_SECOND);
    } else {
      surface->Data.TimeStamp = MFX_TIMESTAMP_UNKNOWN;
    }
  }

  return gst_msdkenc_encode_frame (thiz, surface, frame);

/* ERRORS */
not_inited:
  {
    GST_WARNING_OBJECT (encoder, "Got buffer before set_caps was called");
    return GST_FLOW_NOT_NEGOTIATED;
  }
invalid_surface:
  {
    GST_ERROR_OBJECT (encoder, "Surface pool is full");
    return GST_FLOW_ERROR;
  }
invalid_frame:
  {
    GST_WARNING_OBJECT (encoder, "Failed to map frame");
    return GST_FLOW_OK;
  }
}

static gboolean
gst_msdkenc_start (GstVideoEncoder * encoder)
{
  /* Set the minimum pts to some huge value (1000 hours). This keeps
     the dts at the start of the stream from needing to be
     negative. */
  gst_video_encoder_set_min_pts (encoder, GST_SECOND * 60 * 60 * 1000);

  return TRUE;
}

static gboolean
gst_msdkenc_stop (GstVideoEncoder * encoder)
{
  GstMsdkEnc *thiz = GST_MSDKENC (encoder);

  gst_msdkenc_flush_frames (thiz, TRUE);
  gst_msdkenc_close_encoder (thiz);
  gst_msdkenc_dequeue_all_frames (thiz);

  if (thiz->input_state)
    gst_video_codec_state_unref (thiz->input_state);
  thiz->input_state = NULL;

  return TRUE;
}

static gboolean
gst_msdkenc_flush (GstVideoEncoder * encoder)
{
  GstMsdkEnc *thiz = GST_MSDKENC (encoder);

  gst_msdkenc_flush_frames (thiz, TRUE);
  gst_msdkenc_close_encoder (thiz);
  gst_msdkenc_dequeue_all_frames (thiz);

  gst_msdkenc_init_encoder (thiz);

  return TRUE;
}

static GstFlowReturn
gst_msdkenc_finish (GstVideoEncoder * encoder)
{
  GstMsdkEnc *thiz = GST_MSDKENC (encoder);

  gst_msdkenc_flush_frames (thiz, FALSE);

  return GST_FLOW_OK;
}

static gboolean
gst_msdkenc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  GstMsdkEnc *thiz = GST_MSDKENC (encoder);
  GstVideoInfo *info;
  guint num_buffers;

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  if (!thiz->input_state)
    return FALSE;

  info = &thiz->input_state->info;
  num_buffers = gst_msdkenc_maximum_delayed_frames (thiz) + 1;

  gst_query_add_allocation_pool (query, NULL, info->size, num_buffers, 0);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

static void
gst_msdkenc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstMsdkEnc *thiz = GST_MSDKENC (object);
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
    case PROP_TARGET_USAGE:
      thiz->target_usage = g_value_get_uint (value);
      break;
    case PROP_RATE_CONTROL:
      thiz->rate_control = g_value_get_enum (value);
      break;
    case PROP_BITRATE:
      thiz->bitrate = g_value_get_uint (value);
      thiz->reconfig = TRUE;
      break;
    case PROP_QPI:
      thiz->qpi = g_value_get_uint (value);
      break;
    case PROP_QPP:
      thiz->qpp = g_value_get_uint (value);
      break;
    case PROP_QPB:
      thiz->qpb = g_value_get_uint (value);
      break;
    case PROP_GOP_SIZE:
      thiz->gop_size = g_value_get_uint (value);
      break;
    case PROP_REF_FRAMES:
      thiz->ref_frames = g_value_get_uint (value);
      break;
    case PROP_I_FRAMES:
      thiz->i_frames = g_value_get_uint (value);
      break;
    case PROP_B_FRAMES:
      thiz->b_frames = g_value_get_uint (value);
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
gst_msdkenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkEnc *thiz = GST_MSDKENC (object);

  GST_OBJECT_LOCK (thiz);
  switch (prop_id) {
    case PROP_HARDWARE:
      g_value_set_boolean (value, thiz->hardware);
      break;
    case PROP_ASYNC_DEPTH:
      g_value_set_uint (value, thiz->async_depth);
      break;
    case PROP_TARGET_USAGE:
      g_value_set_uint (value, thiz->target_usage);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, thiz->rate_control);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, thiz->bitrate);
      break;
    case PROP_QPI:
      g_value_set_uint (value, thiz->qpi);
      break;
    case PROP_QPP:
      g_value_set_uint (value, thiz->qpp);
      break;
    case PROP_QPB:
      g_value_set_uint (value, thiz->qpb);
      break;
    case PROP_GOP_SIZE:
      g_value_set_uint (value, thiz->gop_size);
      break;
    case PROP_REF_FRAMES:
      g_value_set_uint (value, thiz->ref_frames);
      break;
    case PROP_I_FRAMES:
      g_value_set_uint (value, thiz->i_frames);
      break;
    case PROP_B_FRAMES:
      g_value_set_uint (value, thiz->b_frames);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static void
gst_msdkenc_finalize (GObject * object)
{
  GstMsdkEnc *thiz = GST_MSDKENC (object);

  if (thiz->input_state)
    gst_video_codec_state_unref (thiz->input_state);
  thiz->input_state = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_msdkenc_class_init (GstMsdkEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *gstencoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  gstencoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  gobject_class->set_property = gst_msdkenc_set_property;
  gobject_class->get_property = gst_msdkenc_get_property;
  gobject_class->finalize = gst_msdkenc_finalize;

  gstencoder_class->set_format = GST_DEBUG_FUNCPTR (gst_msdkenc_set_format);
  gstencoder_class->handle_frame = GST_DEBUG_FUNCPTR (gst_msdkenc_handle_frame);
  gstencoder_class->start = GST_DEBUG_FUNCPTR (gst_msdkenc_start);
  gstencoder_class->stop = GST_DEBUG_FUNCPTR (gst_msdkenc_stop);
  gstencoder_class->flush = GST_DEBUG_FUNCPTR (gst_msdkenc_flush);
  gstencoder_class->finish = GST_DEBUG_FUNCPTR (gst_msdkenc_finish);
  gstencoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_msdkenc_propose_allocation);

  g_object_class_install_property (gobject_class, PROP_HARDWARE,
      g_param_spec_boolean ("hardware", "Hardware", "Enable hardware encoders",
          PROP_HARDWARE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ASYNC_DEPTH,
      g_param_spec_uint ("async-depth", "Async Depth",
          "Depth of asynchronous pipeline",
          1, 20, PROP_ASYNC_DEPTH_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TARGET_USAGE,
      g_param_spec_uint ("target-usage", "Target Usage",
          "1: Best quality, 4: Balanced, 7: Best speed",
          1, 7, PROP_TARGET_USAGE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate Control",
          "Rate control method", GST_MSDKENC_RATE_CONTROL_TYPE,
          PROP_RATE_CONTROL_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate", "Bitrate in kbit/sec", 1,
          2000 * 1024, PROP_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class, PROP_QPI,
      g_param_spec_uint ("qpi", "QPI",
          "Constant quantizer for I frames (0 unlimited)",
          0, 51, PROP_QPI_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QPP,
      g_param_spec_uint ("qpp", "QPP",
          "Constant quantizer for P frames (0 unlimited)",
          0, 51, PROP_QPP_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_QPB,
      g_param_spec_uint ("qpb", "QPB",
          "Constant quantizer for B frames (0 unlimited)",
          0, 51, PROP_QPB_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_GOP_SIZE,
      g_param_spec_uint ("gop-size", "GOP Size", "GOP Size", 0,
          G_MAXINT, PROP_GOP_SIZE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REF_FRAMES,
      g_param_spec_uint ("ref-frames", "Reference Frames",
          "Number of reference frames",
          0, G_MAXINT, PROP_REF_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_I_FRAMES,
      g_param_spec_uint ("i-frames", "I Frames",
          "Number of I frames between IDR frames",
          0, G_MAXINT, PROP_I_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_B_FRAMES,
      g_param_spec_uint ("b-frames", "B Frames",
          "Number of B frames between I and P frames",
          0, G_MAXINT, PROP_B_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

static void
gst_msdkenc_init (GstMsdkEnc * thiz)
{
  thiz->hardware = PROP_HARDWARE_DEFAULT;
  thiz->async_depth = PROP_ASYNC_DEPTH_DEFAULT;
  thiz->target_usage = PROP_TARGET_USAGE_DEFAULT;
  thiz->rate_control = PROP_RATE_CONTROL_DEFAULT;
  thiz->bitrate = PROP_BITRATE_DEFAULT;
  thiz->qpi = PROP_QPI_DEFAULT;
  thiz->qpp = PROP_QPP_DEFAULT;
  thiz->qpb = PROP_QPB_DEFAULT;
  thiz->gop_size = PROP_GOP_SIZE_DEFAULT;
  thiz->ref_frames = PROP_REF_FRAMES_DEFAULT;
  thiz->i_frames = PROP_I_FRAMES_DEFAULT;
  thiz->b_frames = PROP_B_FRAMES_DEFAULT;
}
