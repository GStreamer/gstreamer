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

#ifndef __GST_MSDKENC_H__
#define __GST_MSDKENC_H__

#include <gst/gst.h>
#include <gst/video/gstvideoencoder.h>
#include "msdk.h"
#include "gstmsdkcontext.h"

G_BEGIN_DECLS

#define GST_TYPE_MSDKENC \
  (gst_msdkenc_get_type())
#define GST_MSDKENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MSDKENC,GstMsdkEnc))
#define GST_MSDKENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MSDKENC,GstMsdkEncClass))
#define GST_MSDKENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_MSDKENC,GstMsdkEncClass))
#define GST_IS_MSDKENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MSDKENC))
#define GST_IS_MSDKENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MSDKENC))

#define MAX_EXTRA_PARAMS 8

typedef struct _GstMsdkEnc GstMsdkEnc;
typedef struct _GstMsdkEncClass GstMsdkEncClass;
typedef struct _MsdkEncTask MsdkEncTask;

struct _GstMsdkEnc
{
  GstVideoEncoder element;

  /* input description */
  GstVideoCodecState *input_state;

  /* List of frame/buffer mapping structs for
   * pending frames */
  GList *pending_frames;

  /* MFX context */
  GstMsdkContext *context;
  mfxVideoParam param;
  guint num_surfaces;
  guint num_tasks;
  MsdkEncTask *tasks;
  guint next_task;

  gboolean has_vpp;
  mfxVideoParam vpp_param;
  guint num_vpp_surfaces;
  /* Input interfaces, output above */
  mfxFrameAllocResponse vpp_alloc_resp;
  mfxFrameAllocResponse alloc_resp;

  mfxExtBuffer *extra_params[MAX_EXTRA_PARAMS];
  guint num_extra_params;

  GstBufferPool *msdk_pool;
  GstBufferPool *msdk_converted_pool;
  GstVideoInfo aligned_info;
  gboolean use_video_memory;

  /* element properties */
  gboolean hardware;

  guint async_depth;
  guint target_usage;
  guint rate_control;
  guint bitrate;
  guint qpi;
  guint qpp;
  guint qpb;
  guint gop_size;
  guint ref_frames;
  guint i_frames;
  guint b_frames;

  gboolean reconfig;
};

struct _GstMsdkEncClass
{
  GstVideoEncoderClass parent_class;

  gboolean (*set_format) (GstMsdkEnc * encoder);
  gboolean (*configure) (GstMsdkEnc * encoder);
  GstCaps *(*set_src_caps) (GstMsdkEnc * encoder);
};

struct _MsdkEncTask
{
  GstVideoCodecFrame *input_frame;
  mfxSyncPoint sync_point;
  mfxBitstream output_bitstream;
  gboolean more_data;
  guint pending_frame_number;
};

GType gst_msdkenc_get_type (void);

void gst_msdkenc_add_extra_param (GstMsdkEnc * thiz, mfxExtBuffer * param);

G_END_DECLS

#endif /* __GST_MSDKENC_H__ */
