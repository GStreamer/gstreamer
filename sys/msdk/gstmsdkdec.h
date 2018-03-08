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

#ifndef __GST_MSDKDEC_H__
#define __GST_MSDKDEC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "msdk.h"
#include "gstmsdkcontext.h"

G_BEGIN_DECLS

#define GST_TYPE_MSDKDEC \
  (gst_msdkdec_get_type())
#define GST_MSDKDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MSDKDEC,GstMsdkDec))
#define GST_MSDKDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MSDKDEC,GstMsdkDecClass))
#define GST_MSDKDEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_MSDKDEC,GstMsdkDecClass))
#define GST_IS_MSDKDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MSDKDEC))
#define GST_IS_MSDKDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MSDKDEC))

typedef struct _GstMsdkDec GstMsdkDec;
typedef struct _GstMsdkDecClass GstMsdkDecClass;
typedef struct _MsdkDecTask MsdkDecTask;

struct _GstMsdkDec
{
  GstVideoDecoder element;

  /* input description */
  GstVideoCodecState *input_state;
  GstVideoInfo output_info;
  GstBufferPool *pool;
  GstVideoInfo pool_info;
  mfxFrameAllocResponse alloc_resp;
  gboolean use_video_memory;
  gboolean initialized;

  /* for packetization */
  GstAdapter *adapter;
  gboolean is_packetized;

  /* MFX context */
  GstMsdkContext *context;
  mfxVideoParam param;
  GPtrArray *extra_params;
  GArray *tasks;
  guint next_task;

  GList *decoded_msdk_surfaces;

  /* element properties */
  gboolean hardware;
  guint async_depth;
};

struct _GstMsdkDecClass
{
  GstVideoDecoderClass parent_class;

  gboolean (*configure) (GstMsdkDec * decoder);
};

struct _MsdkDecTask
{
  mfxFrameSurface1 *surface;
  mfxSyncPoint sync_point;
};

GType gst_msdkdec_get_type (void);

G_END_DECLS

#endif /* __GST_MSDKDEC_H__ */
