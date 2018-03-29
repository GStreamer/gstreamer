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
#include "gstmsdkbufferpool.h"
#include "gstmsdkvideomemory.h"
#include "gstmsdksystemmemory.h"
#include "gstmsdkcontextutil.h"

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
#define PROP_NUM_SLICES_DEFAULT          0
#define PROP_AVBR_ACCURACY_DEFAULT       0
#define PROP_AVBR_CONVERGENCE_DEFAULT    0
#define PROP_RC_LOOKAHEAD_DEPTH_DEFAULT  10
#define PROP_MAX_VBV_BITRATE_DEFAULT     0
#define PROP_MAX_FRAME_SIZE_DEFAULT      0
#define PROP_MBBRC_DEFAULT               MFX_CODINGOPTION_OFF
#define PROP_ADAPTIVE_I_DEFAULT          MFX_CODINGOPTION_OFF
#define PROP_ADAPTIVE_B_DEFAULT          MFX_CODINGOPTION_OFF

#define gst_msdkenc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkEnc, gst_msdkenc, GST_TYPE_VIDEO_ENCODER);

typedef struct
{
  mfxFrameSurface1 *surface;
  GstBuffer *buf;
} MsdkSurface;

void
gst_msdkenc_add_extra_param (GstMsdkEnc * thiz, mfxExtBuffer * param)
{
  if (thiz->num_extra_params < MAX_EXTRA_PARAMS) {
    thiz->extra_params[thiz->num_extra_params] = param;
    thiz->num_extra_params++;
  }
}

static void
gst_msdkenc_set_context (GstElement * element, GstContext * context)
{
  GstMsdkContext *msdk_context = NULL;
  GstMsdkEnc *thiz = GST_MSDKENC (element);

  if (gst_msdk_context_get_context (context, &msdk_context)) {
    gst_object_replace ((GstObject **) & thiz->context,
        (GstObject *) msdk_context);
    gst_object_unref (msdk_context);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static void
ensure_bitrate_control (GstMsdkEnc * thiz)
{
  mfxInfoMFX *mfx = &thiz->param.mfx;
  mfxExtCodingOption2 *option2 = &thiz->option2;
  mfxExtCodingOption3 *option3 = &thiz->option3;

  mfx->RateControlMethod = thiz->rate_control;
  /* No effect in CQP varient algorithms */
  mfx->TargetKbps = thiz->bitrate;
  mfx->MaxKbps = thiz->max_vbv_bitrate;

  switch (mfx->RateControlMethod) {
    case MFX_RATECONTROL_CQP:
      mfx->QPI = thiz->qpi;
      mfx->QPP = thiz->qpp;
      mfx->QPB = thiz->qpb;
      break;

    case MFX_RATECONTROL_LA_ICQ:
      option2->LookAheadDepth = thiz->lookahead_depth;
    case MFX_RATECONTROL_ICQ:
      mfx->ICQQuality = CLAMP (thiz->qpi, 1, 51);
      break;

    case MFX_RATECONTROL_LA:   /* VBR with LA. Only supported in H264?? */
    case MFX_RATECONTROL_LA_HRD:       /* VBR with LA, HRD compliant */
      option2->LookAheadDepth = thiz->lookahead_depth;
      break;

    case MFX_RATECONTROL_QVBR:
      option3->QVBRQuality = CLAMP (thiz->qpi, 1, 51);
      thiz->enable_extopt3 = TRUE;
      break;

    case MFX_RATECONTROL_AVBR:
      mfx->Accuracy = thiz->accuracy;
      mfx->Convergence = thiz->convergence;

    case MFX_RATECONTROL_VBR:
      option2->MaxFrameSize = thiz->max_frame_size * 1000;
      break;

    case MFX_RATECONTROL_VCM:
      /*Non HRD compliant mode with no B-frame and interlaced support */
      thiz->param.mfx.GopRefDist = 0;
      break;

    case MFX_RATECONTROL_CBR:
      break;

    default:
      GST_ERROR ("Unsupported RateControl!");
      break;
  }
}

void
gst_msdkenc_ensure_extended_coding_options (GstMsdkEnc * thiz)
{
  mfxExtCodingOption2 *option2 = &thiz->option2;
  mfxExtCodingOption3 *option3 = &thiz->option3;

  /* Fill ExtendedCodingOption2, set non-zero defaults too */
  option2->Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
  option2->Header.BufferSz = sizeof (thiz->option2);
  option2->MBBRC = thiz->mbbrc;
  option2->AdaptiveI = thiz->adaptive_i;
  option2->AdaptiveB = thiz->adaptive_b;
  option2->BitrateLimit = MFX_CODINGOPTION_OFF;
  option2->EnableMAD = MFX_CODINGOPTION_OFF;
  option2->UseRawRef = MFX_CODINGOPTION_OFF;
  gst_msdkenc_add_extra_param (thiz, (mfxExtBuffer *) option2);

  if (thiz->enable_extopt3) {
    option3->Header.BufferId = MFX_EXTBUFF_CODING_OPTION3;
    option3->Header.BufferSz = sizeof (thiz->option3);
    gst_msdkenc_add_extra_param (thiz, (mfxExtBuffer *) option3);
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
  session = gst_msdk_context_get_session (thiz->context);

  thiz->has_vpp = FALSE;
  if (thiz->use_video_memory)
    gst_msdk_set_frame_allocator (thiz->context);

  if (info->finfo->format != GST_VIDEO_FORMAT_NV12) {
    if (thiz->use_video_memory)
      thiz->vpp_param.IOPattern =
          MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;
    else
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

    if (thiz->use_video_memory)
      request[0].NumFrameSuggested +=
          gst_msdk_context_get_shared_async_depth (thiz->context);
    thiz->num_vpp_surfaces = request[0].NumFrameSuggested;

    if (thiz->use_video_memory)
      gst_msdk_frame_alloc (thiz->context, &(request[0]),
          &thiz->vpp_alloc_resp);

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
  if (thiz->use_video_memory)
    thiz->param.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;
  else
    thiz->param.IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;

  thiz->param.mfx.TargetUsage = thiz->target_usage;
  thiz->param.mfx.GopPicSize = thiz->gop_size;
  thiz->param.mfx.GopRefDist = thiz->b_frames + 1;
  thiz->param.mfx.IdrInterval = thiz->i_frames;
  thiz->param.mfx.NumSlice = thiz->num_slices;
  thiz->param.mfx.NumRefFrame = thiz->ref_frames;
  thiz->param.mfx.EncodedOrder = 0;     /* Take input frames in display order */

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

  /* ensure bitrate control parameters */
  ensure_bitrate_control (thiz);

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

  if (thiz->has_vpp)
    request[0].NumFrameSuggested += thiz->num_vpp_surfaces + 1 - 4;

  if (thiz->use_video_memory)
    gst_msdk_frame_alloc (thiz->context, &(request[0]), &thiz->alloc_resp);

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

  /* This is VPP output (if any) and encoder input */
  thiz->num_surfaces = request[0].NumFrameSuggested;

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
  thiz->initialized = TRUE;

  GST_OBJECT_UNLOCK (thiz);

  return TRUE;

no_vpp:
failed:
  GST_OBJECT_UNLOCK (thiz);
  return FALSE;
}

static void
gst_msdkenc_close_encoder (GstMsdkEnc * thiz)
{
  guint i;
  mfxStatus status;

  if (!thiz->context || !thiz->initialized)
    return;

  GST_DEBUG_OBJECT (thiz, "Closing encoder with context %" GST_PTR_FORMAT,
      thiz->context);

  gst_object_replace ((GstObject **) & thiz->msdk_pool, NULL);
  gst_object_replace ((GstObject **) & thiz->msdk_converted_pool, NULL);

  if (thiz->use_video_memory)
    gst_msdk_frame_free (thiz->context, &thiz->alloc_resp);

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
    if (thiz->use_video_memory)
      gst_msdk_frame_free (thiz->context, &thiz->vpp_alloc_resp);

    status = MFXVideoVPP_Close (gst_msdk_context_get_session (thiz->context));
    if (status != MFX_ERR_NONE && status != MFX_ERR_NOT_INITIALIZED) {
      GST_WARNING_OBJECT (thiz, "VPP close failed (%s)",
          msdk_status_to_string (status));
    }
  }

  memset (&thiz->param, 0, sizeof (thiz->param));
  thiz->num_extra_params = 0;
  thiz->initialized = FALSE;
}

typedef struct
{
  GstVideoCodecFrame *frame;
  MsdkSurface *frame_surface;
  MsdkSurface *converted_surface;
} FrameData;

static FrameData *
gst_msdkenc_queue_frame (GstMsdkEnc * thiz, GstVideoCodecFrame * frame,
    GstVideoInfo * info)
{
  FrameData *fdata;

  fdata = g_slice_new (FrameData);
  fdata->frame = gst_video_codec_frame_ref (frame);

  thiz->pending_frames = g_list_prepend (thiz->pending_frames, fdata);

  return fdata;
}

static MsdkSurface *
gst_msdkenc_create_surface (mfxFrameSurface1 * surface, GstBuffer * buf)
{
  MsdkSurface *msdk_surface;
  msdk_surface = g_slice_new0 (MsdkSurface);
  msdk_surface->surface = surface;
  msdk_surface->buf = buf;

  return msdk_surface;
}

static void
gst_msdkenc_free_surface (MsdkSurface * surface)
{
  if (surface->buf)
    gst_buffer_unref (surface->buf);

  g_slice_free (MsdkSurface, surface);
}

static void
gst_msdkenc_free_frame_data (GstMsdkEnc * thiz, FrameData * fdata)
{
  if (fdata->frame_surface)
    gst_msdkenc_free_surface (fdata->frame_surface);
  if (thiz->has_vpp)
    gst_msdkenc_free_surface (fdata->converted_surface);

  gst_video_codec_frame_unref (fdata->frame);
  g_slice_free (FrameData, fdata);
}

static void
gst_msdkenc_dequeue_frame (GstMsdkEnc * thiz, GstVideoCodecFrame * frame)
{
  GList *l;

  for (l = thiz->pending_frames; l; l = l->next) {
    FrameData *fdata = l->data;

    if (fdata->frame != frame)
      continue;

    gst_msdkenc_free_frame_data (thiz, fdata);

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

    gst_msdkenc_free_frame_data (thiz, fdata);
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
  task->output_bitstream.DataLength = 0;
  task->sync_point = NULL;
}

static GstFlowReturn
gst_msdkenc_finish_frame (GstMsdkEnc * thiz, MsdkEncTask * task,
    gboolean discard)
{
  GstVideoCodecFrame *frame;

  if (!task->sync_point)
    return GST_FLOW_OK;

  frame = gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (thiz));

  if (!frame) {
    GST_ERROR_OBJECT (thiz, "failed to get a frame");
    return GST_FLOW_ERROR;
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

  gst_video_codec_frame_unref (frame);
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
    /* Force key-frame if needed */
    if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (input_frame))
      thiz->enc_cntrl.FrameType =
          MFX_FRAMETYPE_I | MFX_FRAMETYPE_IDR | MFX_FRAMETYPE_REF;
    else
      thiz->enc_cntrl.FrameType = MFX_FRAMETYPE_UNKNOWN;

    status =
        MFXVideoENCODE_EncodeFrameAsync (session, &thiz->enc_cntrl, surface,
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
    thiz->next_task = ((task - thiz->tasks) + 1) % thiz->num_tasks;
  } else if (status == MFX_ERR_MORE_DATA) {
    gst_msdkenc_dequeue_frame (thiz, input_frame);
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
  mfxStatus status;
  mfxSession session;
  MsdkEncTask *task;
  guint i, t;

  if (!thiz->tasks)
    return;

  session = gst_msdk_context_get_session (thiz->context);

  for (;;) {
    task = thiz->tasks + thiz->next_task;
    gst_msdkenc_finish_frame (thiz, task, FALSE);

    status = MFXVideoENCODE_EncodeFrameAsync (session, NULL, NULL,
        &task->output_bitstream, &task->sync_point);

    if (status != MFX_ERR_NONE && status != MFX_ERR_MORE_DATA) {
      GST_ELEMENT_ERROR (thiz, STREAM, ENCODE, ("Encode frame failed."),
          ("MSDK encode error (%s)", msdk_status_to_string (status)));
    }

    if (task->sync_point) {
      thiz->next_task = ((task - thiz->tasks) + 1) % thiz->num_tasks;
    } else if (status == MFX_ERR_MORE_DATA) {
      break;
    }
  };

  t = thiz->next_task;
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

static GstBufferPool *
gst_msdkenc_create_buffer_pool (GstMsdkEnc * thiz, GstCaps * caps,
    guint num_buffers, gboolean set_align)
{
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;
  GstVideoAlignment align;
  GstAllocationParams params = { 0, 31, 0, 0, };
  mfxFrameAllocResponse *alloc_resp = NULL;

  if (thiz->has_vpp)
    alloc_resp = set_align ? &thiz->vpp_alloc_resp : &thiz->alloc_resp;
  else
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

  if (set_align)
    thiz->aligned_info = info;

  return pool;

error_no_pool:
  {
    GST_INFO_OBJECT (thiz, "failed to create bufferpool");
    return FALSE;
  }
error_no_allocator:
  {
    GST_INFO_OBJECT (thiz, "failed to create allocator");
    return FALSE;
  }
error_pool_config:
  {
    GST_INFO_OBJECT (thiz, "failed to set config");
    return FALSE;
  }
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

  /* TODO: Currently d3d allocator is not implemented.
   * So encoder uses system memory by default on Windows.
   */
#ifndef _WIN32
  thiz->use_video_memory = TRUE;
#else
  thiz->use_video_memory = FALSE;
#endif

  GST_INFO_OBJECT (encoder, "This MSDK encoder uses %s memory",
      thiz->use_video_memory ? "video" : "system");

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

  if (!thiz->msdk_pool) {
    guint num_buffers = gst_msdkenc_maximum_delayed_frames (thiz) + 1;
    thiz->msdk_pool =
        gst_msdkenc_create_buffer_pool (thiz, thiz->input_state->caps,
        num_buffers, TRUE);
  }

  gst_msdkenc_set_latency (thiz);

  /* Create another bufferpool if VPP requires */
  if (thiz->has_vpp) {
    GstVideoInfo *info = &thiz->input_state->info;
    GstVideoInfo nv12_info;
    GstCaps *caps;
    GstBufferPool *pool = NULL;

    gst_video_info_init (&nv12_info);
    gst_video_info_set_format (&nv12_info, GST_VIDEO_FORMAT_NV12, info->width,
        info->height);
    caps = gst_video_info_to_caps (&nv12_info);

    pool =
        gst_msdkenc_create_buffer_pool (thiz, caps, thiz->num_surfaces, FALSE);

    thiz->msdk_converted_pool = pool;
    gst_caps_unref (caps);
  }

  return TRUE;
}

static MsdkSurface *
gst_msdkenc_get_surface_from_pool (GstMsdkEnc * thiz, GstBufferPool * pool,
    GstBufferPoolAcquireParams * params)
{
  GstBuffer *new_buffer;
  mfxFrameSurface1 *new_surface;
  MsdkSurface *msdk_surface;

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (pool, "failed to activate buffer pool");
    return NULL;
  }

  if (gst_buffer_pool_acquire_buffer (pool, &new_buffer, params) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (pool, "failed to acquire a buffer from pool");
    return NULL;
  }

  if (gst_msdk_is_msdk_buffer (new_buffer))
    new_surface = gst_msdk_get_surface_from_buffer (new_buffer);
  else {
    GST_ERROR_OBJECT (pool, "the acquired memory is not MSDK memory");
    return NULL;
  }

  msdk_surface = gst_msdkenc_create_surface (new_surface, new_buffer);

  return msdk_surface;
}

static MsdkSurface *
gst_msdkenc_get_surface_from_frame (GstMsdkEnc * thiz,
    GstVideoCodecFrame * frame)
{
  GstVideoFrame src_frame, out_frame;
  MsdkSurface *msdk_surface;
  GstBuffer *inbuf;

  inbuf = frame->input_buffer;
  if (gst_msdk_is_msdk_buffer (inbuf)) {
    msdk_surface = g_slice_new0 (MsdkSurface);
    msdk_surface->surface = gst_msdk_get_surface_from_buffer (inbuf);
    return msdk_surface;
  }

  /* If upstream hasn't accpeted the proposed msdk bufferpool,
   * just copy frame to msdk buffer and take a surface from it.
   */
  if (!(msdk_surface =
          gst_msdkenc_get_surface_from_pool (thiz, thiz->msdk_pool, NULL)))
    goto error;

  if (!gst_video_frame_map (&src_frame, &thiz->input_state->info, inbuf,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (thiz, "failed to map the frame for source");
    goto error;
  }

  if (!gst_video_frame_map (&out_frame, &thiz->aligned_info, msdk_surface->buf,
          GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (thiz, "failed to map the frame for destination");
    gst_video_frame_unmap (&src_frame);
    goto error;
  }

  if (!gst_video_frame_copy (&out_frame, &src_frame)) {
    GST_ERROR_OBJECT (thiz, "failed to copy frame");
    gst_video_frame_unmap (&out_frame);
    gst_video_frame_unmap (&src_frame);
    goto error;
  }

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&src_frame);

  gst_buffer_replace (&frame->input_buffer, msdk_surface->buf);
  gst_buffer_unref (msdk_surface->buf);
  msdk_surface->buf = NULL;

  return msdk_surface;

error:
  if (msdk_surface) {
    if (msdk_surface->buf)
      gst_buffer_unref (msdk_surface->buf);
    g_slice_free (MsdkSurface, msdk_surface);
  }

  return NULL;
}

static GstFlowReturn
gst_msdkenc_handle_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstMsdkEnc *thiz = GST_MSDKENC (encoder);
  GstVideoInfo *info = &thiz->input_state->info;
  FrameData *fdata;
  MsdkSurface *surface;

  if (thiz->reconfig) {
    gst_msdkenc_flush_frames (thiz, FALSE);
    gst_msdkenc_set_format (encoder, NULL);
  }

  if (G_UNLIKELY (thiz->context == NULL))
    goto not_inited;

  if (thiz->has_vpp) {
    MsdkSurface *vpp_surface;
    GstVideoFrame vframe;
    mfxSession session;
    mfxSyncPoint vpp_sync_point = NULL;
    mfxStatus status;

    vpp_surface = gst_msdkenc_get_surface_from_frame (thiz, frame);
    if (!vpp_surface)
      goto invalid_surface;
    surface =
        gst_msdkenc_get_surface_from_pool (thiz, thiz->msdk_converted_pool,
        NULL);
    if (!surface)
      goto invalid_surface;

    if (!gst_video_frame_map (&vframe, info, frame->input_buffer, GST_MAP_READ))
      goto invalid_frame;

    if (frame->pts != GST_CLOCK_TIME_NONE) {
      vpp_surface->surface->Data.TimeStamp =
          gst_util_uint64_scale (frame->pts, 90000, GST_SECOND);
      surface->surface->Data.TimeStamp =
          gst_util_uint64_scale (frame->pts, 90000, GST_SECOND);
    } else {
      vpp_surface->surface->Data.TimeStamp = MFX_TIMESTAMP_UNKNOWN;
      surface->surface->Data.TimeStamp = MFX_TIMESTAMP_UNKNOWN;
    }

    session = gst_msdk_context_get_session (thiz->context);
    for (;;) {
      status =
          MFXVideoVPP_RunFrameVPPAsync (session, vpp_surface->surface,
          surface->surface, NULL, &vpp_sync_point);
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
    fdata->frame_surface = vpp_surface;
    fdata->converted_surface = surface;

    thiz->pending_frames = g_list_prepend (thiz->pending_frames, fdata);
  } else {
    surface = gst_msdkenc_get_surface_from_frame (thiz, frame);
    if (!surface)
      goto invalid_surface;

    fdata = gst_msdkenc_queue_frame (thiz, frame, info);
    if (!fdata)
      goto invalid_frame;

    fdata->frame_surface = surface;

    if (frame->pts != GST_CLOCK_TIME_NONE) {
      surface->surface->Data.TimeStamp =
          gst_util_uint64_scale (frame->pts, 90000, GST_SECOND);
    } else {
      surface->surface->Data.TimeStamp = MFX_TIMESTAMP_UNKNOWN;
    }
  }

  return gst_msdkenc_encode_frame (thiz, surface->surface, frame);

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
  GstMsdkEnc *thiz = GST_MSDKENC (encoder);

  if (gst_msdk_context_prepare (GST_ELEMENT_CAST (thiz), &thiz->context)) {
    GST_INFO_OBJECT (thiz, "Found context %" GST_PTR_FORMAT " from neighbour",
        thiz->context);

    if (gst_msdk_context_get_job_type (thiz->context) & GST_MSDK_JOB_ENCODER) {
      GstMsdkContext *parent_context;

      parent_context = thiz->context;
      thiz->context = gst_msdk_context_new_with_parent (parent_context);
      gst_object_unref (parent_context);

      GST_INFO_OBJECT (thiz,
          "Creating new context %" GST_PTR_FORMAT " with joined session",
          thiz->context);
    } else {
      gst_msdk_context_add_job_type (thiz->context, GST_MSDK_JOB_ENCODER);
    }
  } else {
    gst_msdk_context_ensure_context (GST_ELEMENT_CAST (thiz), thiz->hardware,
        GST_MSDK_JOB_ENCODER);

    GST_INFO_OBJECT (thiz, "Creating new context %" GST_PTR_FORMAT,
        thiz->context);
  }

  gst_msdk_context_add_shared_async_depth (thiz->context, thiz->async_depth);

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

  gst_object_replace ((GstObject **) & thiz->context, NULL);

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
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;
  GstCaps *caps;
  guint num_buffers;

  if (!thiz->input_state)
    return FALSE;

  gst_query_parse_allocation (query, &caps, NULL);

  if (!caps) {
    GST_INFO_OBJECT (encoder, "failed to get caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_INFO_OBJECT (encoder, "failed to get video info");
    return FALSE;
  }

  num_buffers = gst_msdkenc_maximum_delayed_frames (thiz) + 1;
  pool = gst_msdkenc_create_buffer_pool (thiz, caps, num_buffers, TRUE);

  gst_query_add_allocation_pool (query, pool, GST_VIDEO_INFO_SIZE (&info),
      num_buffers, 0);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  if (pool) {
    GstStructure *config;
    GstAllocationParams params = { 0, 31, 0, 0, };

    config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));

    if (gst_buffer_pool_config_get_allocator (config, &allocator, NULL))
      gst_query_add_allocation_param (query, allocator, &params);
    gst_structure_free (config);
  }

  gst_object_unref (pool);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}


static void
gst_msdkenc_finalize (GObject * object)
{
  GstMsdkEnc *thiz = GST_MSDKENC (object);

  if (thiz->input_state)
    gst_video_codec_state_unref (thiz->input_state);
  thiz->input_state = NULL;

  gst_object_replace ((GstObject **) & thiz->msdk_pool, NULL);
  gst_object_replace ((GstObject **) & thiz->msdk_converted_pool, NULL);

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

  gobject_class->finalize = gst_msdkenc_finalize;

  element_class->set_context = gst_msdkenc_set_context;

  gstencoder_class->set_format = GST_DEBUG_FUNCPTR (gst_msdkenc_set_format);
  gstencoder_class->handle_frame = GST_DEBUG_FUNCPTR (gst_msdkenc_handle_frame);
  gstencoder_class->start = GST_DEBUG_FUNCPTR (gst_msdkenc_start);
  gstencoder_class->stop = GST_DEBUG_FUNCPTR (gst_msdkenc_stop);
  gstencoder_class->flush = GST_DEBUG_FUNCPTR (gst_msdkenc_flush);
  gstencoder_class->finish = GST_DEBUG_FUNCPTR (gst_msdkenc_finish);
  gstencoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_msdkenc_propose_allocation);

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
  thiz->max_frame_size = PROP_MAX_FRAME_SIZE_DEFAULT;
  thiz->max_vbv_bitrate = PROP_MAX_VBV_BITRATE_DEFAULT;
  thiz->accuracy = PROP_AVBR_ACCURACY_DEFAULT;
  thiz->convergence = PROP_AVBR_ACCURACY_DEFAULT;
  thiz->lookahead_depth = PROP_RC_LOOKAHEAD_DEPTH_DEFAULT;
  thiz->qpi = PROP_QPI_DEFAULT;
  thiz->qpp = PROP_QPP_DEFAULT;
  thiz->qpb = PROP_QPB_DEFAULT;
  thiz->gop_size = PROP_GOP_SIZE_DEFAULT;
  thiz->ref_frames = PROP_REF_FRAMES_DEFAULT;
  thiz->i_frames = PROP_I_FRAMES_DEFAULT;
  thiz->b_frames = PROP_B_FRAMES_DEFAULT;
  thiz->num_slices = PROP_NUM_SLICES_DEFAULT;
  thiz->mbbrc = PROP_MBBRC_DEFAULT;
  thiz->adaptive_i = PROP_ADAPTIVE_I_DEFAULT;
  thiz->adaptive_b = PROP_ADAPTIVE_B_DEFAULT;
}

/* gst_msdkenc_set_common_property:
 *
 * This is a helper function to set the common property
 * of base encoder from subclass implementation.
 */
gboolean
gst_msdkenc_set_common_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkEnc *thiz = GST_MSDKENC (object);
  GstState state;
  gboolean ret = TRUE;

  GST_OBJECT_LOCK (thiz);

  state = GST_STATE (thiz);
  if ((state != GST_STATE_READY && state != GST_STATE_NULL) &&
      !(pspec->flags & GST_PARAM_MUTABLE_PLAYING)) {
    ret = FALSE;
    goto wrong_state;
  }

  switch (prop_id) {
    case GST_MSDKENC_PROP_HARDWARE:
      thiz->hardware = g_value_get_boolean (value);
      break;
    case GST_MSDKENC_PROP_ASYNC_DEPTH:
      thiz->async_depth = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_TARGET_USAGE:
      thiz->target_usage = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_RATE_CONTROL:
      thiz->rate_control = g_value_get_enum (value);
      break;
    case GST_MSDKENC_PROP_BITRATE:
      thiz->bitrate = g_value_get_uint (value);
      thiz->reconfig = TRUE;
      break;
    case GST_MSDKENC_PROP_MAX_FRAME_SIZE:
      thiz->max_frame_size = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_MAX_VBV_BITRATE:
      thiz->max_vbv_bitrate = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_AVBR_ACCURACY:
      thiz->accuracy = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_AVBR_CONVERGENCE:
      thiz->convergence = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_RC_LOOKAHEAD_DEPTH:
      thiz->lookahead_depth = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_QPI:
      thiz->qpi = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_QPP:
      thiz->qpp = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_QPB:
      thiz->qpb = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_GOP_SIZE:
      thiz->gop_size = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_REF_FRAMES:
      thiz->ref_frames = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_I_FRAMES:
      thiz->i_frames = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_B_FRAMES:
      thiz->b_frames = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_NUM_SLICES:
      thiz->num_slices = g_value_get_uint (value);
      break;
    case GST_MSDKENC_PROP_MBBRC:
      thiz->mbbrc = g_value_get_enum (value);
      break;
    case GST_MSDKENC_PROP_ADAPTIVE_I:
      thiz->adaptive_i = g_value_get_enum (value);
      break;
    case GST_MSDKENC_PROP_ADAPTIVE_B:
      thiz->adaptive_b = g_value_get_enum (value);
      break;
    default:
      ret = FALSE;
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
  return ret;

  /* ERROR */
wrong_state:
  {
    GST_WARNING_OBJECT (thiz, "setting property in wrong state");
    GST_OBJECT_UNLOCK (thiz);
    return ret;
  }
}

/* gst_msdkenc_get_common_property:
 *
 * This is a helper function to get the common property
 * of base encoder from subclass implementation.
 */
gboolean
gst_msdkenc_get_common_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMsdkEnc *thiz = GST_MSDKENC (object);
  gboolean ret = TRUE;

  GST_OBJECT_LOCK (thiz);
  switch (prop_id) {
    case GST_MSDKENC_PROP_HARDWARE:
      g_value_set_boolean (value, thiz->hardware);
      break;
    case GST_MSDKENC_PROP_ASYNC_DEPTH:
      g_value_set_uint (value, thiz->async_depth);
      break;
    case GST_MSDKENC_PROP_TARGET_USAGE:
      g_value_set_uint (value, thiz->target_usage);
      break;
    case GST_MSDKENC_PROP_RATE_CONTROL:
      g_value_set_enum (value, thiz->rate_control);
      break;
    case GST_MSDKENC_PROP_BITRATE:
      g_value_set_uint (value, thiz->bitrate);
      break;
    case GST_MSDKENC_PROP_MAX_FRAME_SIZE:
      g_value_set_uint (value, thiz->max_frame_size);
      break;
    case GST_MSDKENC_PROP_MAX_VBV_BITRATE:
      g_value_set_uint (value, thiz->max_vbv_bitrate);
      break;
    case GST_MSDKENC_PROP_AVBR_ACCURACY:
      g_value_set_uint (value, thiz->accuracy);
      break;
    case GST_MSDKENC_PROP_AVBR_CONVERGENCE:
      g_value_set_uint (value, thiz->convergence);
      break;
    case GST_MSDKENC_PROP_RC_LOOKAHEAD_DEPTH:
      g_value_set_uint (value, thiz->lookahead_depth);
      break;
    case GST_MSDKENC_PROP_QPI:
      g_value_set_uint (value, thiz->qpi);
      break;
    case GST_MSDKENC_PROP_QPP:
      g_value_set_uint (value, thiz->qpp);
      break;
    case GST_MSDKENC_PROP_QPB:
      g_value_set_uint (value, thiz->qpb);
      break;
    case GST_MSDKENC_PROP_GOP_SIZE:
      g_value_set_uint (value, thiz->gop_size);
      break;
    case GST_MSDKENC_PROP_REF_FRAMES:
      g_value_set_uint (value, thiz->ref_frames);
      break;
    case GST_MSDKENC_PROP_I_FRAMES:
      g_value_set_uint (value, thiz->i_frames);
      break;
    case GST_MSDKENC_PROP_B_FRAMES:
      g_value_set_uint (value, thiz->b_frames);
      break;
    case GST_MSDKENC_PROP_NUM_SLICES:
      g_value_set_uint (value, thiz->num_slices);
      break;
    case GST_MSDKENC_PROP_MBBRC:
      g_value_set_enum (value, thiz->mbbrc);
      break;
    case GST_MSDKENC_PROP_ADAPTIVE_I:
      g_value_set_enum (value, thiz->adaptive_i);
      break;
    case GST_MSDKENC_PROP_ADAPTIVE_B:
      g_value_set_enum (value, thiz->adaptive_b);
      break;
    default:
      ret = FALSE;
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
  return ret;
}

/* gst_msdkenc_install_common_properties:
 * @thiz: a #GstMsdkEnc
 *
 * This is a helper function to install common properties
 * of base encoder from subclass implementation.
 * Encoders like jpeg do't require all the common properties
 * and they can avoid installing it into base gobject.
 */
void
gst_msdkenc_install_common_properties (GstMsdkEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GParamSpec *obj_properties[GST_MSDKENC_PROP_MAX] = { NULL, };

  obj_properties[GST_MSDKENC_PROP_HARDWARE] =
      g_param_spec_boolean ("hardware", "Hardware", "Enable hardware encoders",
      PROP_HARDWARE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_ASYNC_DEPTH] =
      g_param_spec_uint ("async-depth", "Async Depth",
      "Depth of asynchronous pipeline",
      1, 20, PROP_ASYNC_DEPTH_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_TARGET_USAGE] =
      g_param_spec_uint ("target-usage", "Target Usage",
      "1: Best quality, 4: Balanced, 7: Best speed",
      1, 7, PROP_TARGET_USAGE_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_RATE_CONTROL] =
      g_param_spec_enum ("rate-control", "Rate Control",
      "Rate control method", gst_msdkenc_rate_control_get_type (),
      PROP_RATE_CONTROL_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_BITRATE] =
      g_param_spec_uint ("bitrate", "Bitrate", "Bitrate in kbit/sec", 1,
      2000 * 1024, PROP_BITRATE_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);

  obj_properties[GST_MSDKENC_PROP_MAX_FRAME_SIZE] =
      g_param_spec_uint ("max-frame-size", "Max Frame Size",
      "Maximum possible size (in kb) of any compressed frames (0: auto-calculate)",
      0, G_MAXUINT16, PROP_MAX_FRAME_SIZE_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_MAX_VBV_BITRATE] =
      g_param_spec_uint ("max-vbv-bitrate", "Max VBV Bitrate",
      "Maximum bitrate(kbit/sec) at which data enters Video Buffering Verifier (0: auto-calculate)",
      0, G_MAXUINT16, PROP_MAX_VBV_BITRATE_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_AVBR_ACCURACY] =
      g_param_spec_uint ("accuracy", "Accuracy", "The AVBR Accuracy in "
      "the unit of tenth of percent", 0, G_MAXUINT16,
      PROP_AVBR_ACCURACY_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_AVBR_CONVERGENCE] =
      g_param_spec_uint ("convergence", "Convergence",
      "The AVBR Convergence in the unit of 100 frames", 0, G_MAXUINT16,
      PROP_AVBR_CONVERGENCE_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_RC_LOOKAHEAD_DEPTH] =
      g_param_spec_uint ("rc-lookahead", "Look-ahead depth",
      "Number of frames to look ahead for Rate control", 10, 100,
      PROP_RC_LOOKAHEAD_DEPTH_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_QPI] =
      g_param_spec_uint ("qpi", "QPI",
      "Constant quantizer for I frames (0 unlimited). Also used as "
      "ICQQuality or QVBRQuality for different RateControl methods",
      0, 51, PROP_QPI_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_QPP] =
      g_param_spec_uint ("qpp", "QPP",
      "Constant quantizer for P frames (0 unlimited)",
      0, 51, PROP_QPP_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_QPB] =
      g_param_spec_uint ("qpb", "QPB",
      "Constant quantizer for B frames (0 unlimited)",
      0, 51, PROP_QPB_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_GOP_SIZE] =
      g_param_spec_uint ("gop-size", "GOP Size", "GOP Size", 0,
      G_MAXINT, PROP_GOP_SIZE_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_REF_FRAMES] =
      g_param_spec_uint ("ref-frames", "Reference Frames",
      "Number of reference frames",
      0, G_MAXINT, PROP_REF_FRAMES_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_I_FRAMES] =
      g_param_spec_uint ("i-frames", "I Frames",
      "Number of I frames between IDR frames",
      0, G_MAXINT, PROP_I_FRAMES_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_B_FRAMES] =
      g_param_spec_uint ("b-frames", "B Frames",
      "Number of B frames between I and P frames",
      0, G_MAXINT, PROP_B_FRAMES_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_NUM_SLICES] =
      g_param_spec_uint ("num-slices", "Number of Slices",
      "Number of slices per frame, Zero tells the encoder to "
      "choose any slice partitioning allowed by the codec standard",
      0, G_MAXINT, PROP_NUM_SLICES_DEFAULT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_MBBRC] =
      g_param_spec_enum ("mbbrc", "MB level bitrate control",
      "Macroblock level bitrate control",
      gst_msdkenc_mbbrc_get_type (),
      PROP_MBBRC_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_ADAPTIVE_I] =
      g_param_spec_enum ("i-adapt", "Adaptive I-Frame Insertion",
      "Adaptive I-Frame Insertion control",
      gst_msdkenc_adaptive_i_get_type (),
      PROP_ADAPTIVE_I_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_properties[GST_MSDKENC_PROP_ADAPTIVE_B] =
      g_param_spec_enum ("b-adapt", "Adaptive B-Frame Insertion",
      "Adaptive B-Frame Insertion control",
      gst_msdkenc_adaptive_b_get_type (),
      PROP_ADAPTIVE_B_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
      GST_MSDKENC_PROP_MAX, obj_properties);
}
