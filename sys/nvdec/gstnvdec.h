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

#ifndef __GST_NVDEC_H__
#define __GST_NVDEC_H__

#include <gst/gl/gl.h>
#include <nvcuvid.h>

G_BEGIN_DECLS

typedef struct _GstNvDecCudaContext GstNvDecCudaContext;
typedef struct _GstNvDecCudaContextClass GstNvDecCudaContextClass;

struct _GstNvDecCudaContext
{
  GObject parent;

  CUcontext context;
  CUvideoctxlock lock;
};

struct _GstNvDecCudaContextClass
{
  GObjectClass parent_class;
};

GType gst_nvdec_cuda_context_get_type (void);


#define GST_TYPE_NVDEC          (gst_nvdec_get_type())
#define GST_NVDEC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_NVDEC, GstNvDec))
#define GST_NVDEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_NVDEC, GstNvDecClass))
#define GST_IS_NVDEC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_NVDEC))
#define GST_IS_NVDEC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_NVDEC))

typedef struct _GstNvDec GstNvDec;
typedef struct _GstNvDecClass GstNvDecClass;

struct _GstNvDec
{
  GstVideoDecoder parent;

  GstGLDisplay *gl_display;
  GstGLContext *gl_context;
  GstGLContext *other_gl_context;

  GstNvDecCudaContext *cuda_context;
  CUvideoparser parser;
  CUvideodecoder decoder;
  GAsyncQueue *decode_queue;

  guint width;
  guint height;
  guint fps_n;
  guint fps_d;
  GstClockTime min_latency;
  GstVideoCodecState *input_state;
};

struct _GstNvDecClass
{
  GstVideoDecoderClass parent_class;
};

GType gst_nvdec_get_type (void);

G_END_DECLS

#endif /* __GST_NVDEC_H__ */
