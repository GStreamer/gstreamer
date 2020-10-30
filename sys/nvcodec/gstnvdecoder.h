/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifndef __GST_NV_DECODER_H__
#define __GST_NV_DECODER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstcudautils.h"
#include "gstcuvidloader.h"

G_BEGIN_DECLS

#define GST_TYPE_NV_DECODER (gst_nv_decoder_get_type())
G_DECLARE_FINAL_TYPE (GstNvDecoder,
    gst_nv_decoder, GST, NV_DECODER, GstObject);

typedef struct _GstNvDecoderFrame
{
  /* CUVIDPICPARAMS::CurrPicIdx */
  gint index;
  guintptr devptr;
  guint pitch;

  gboolean mapped;

  /*< private >*/
  GstNvDecoder *decoder;

  gint ref_count;
} GstNvDecoderFrame;

typedef enum
{
  GST_NV_DECOCER_OUTPUT_TYPE_SYSTEM = 0,
  GST_NV_DECOCER_OUTPUT_TYPE_GL,
  GST_NV_DECOCER_OUTPUT_TYPE_CUDA,
  /* FIXME: add support D3D11 memory */
} GstNvDecoderOutputType;

GstNvDecoder * gst_nv_decoder_new (GstCudaContext * context,
                                   cudaVideoCodec codec,
                                   GstVideoInfo * info,
                                   guint pool_size);

GstNvDecoderFrame * gst_nv_decoder_new_frame (GstNvDecoder * decoder);

GstNvDecoderFrame * gst_nv_decoder_frame_ref (GstNvDecoderFrame * frame);

void gst_nv_decoder_frame_unref (GstNvDecoderFrame * frame);

gboolean gst_nv_decoder_decode_picture (GstNvDecoder * decoder,
                                        CUVIDPICPARAMS * params);

gboolean gst_nv_decoder_finish_frame (GstNvDecoder * decoder,
                                      GstNvDecoderOutputType output_type,
                                      GstObject * graphics_context,
                                      GstNvDecoderFrame *frame,
                                      GstBuffer *buffer);

/* utils for class registration */
gboolean gst_nv_decoder_check_device_caps (CUcontext cuda_ctx,
                                           cudaVideoCodec codec,
                                           GstCaps **sink_template,
                                           GstCaps **src_template);

const gchar * gst_cuda_video_codec_to_string (cudaVideoCodec codec);

/* helper methods */
gboolean gst_nv_decoder_ensure_element_data  (GstElement * decoder,
                                              guint cuda_device_id,
                                              GstCudaContext ** cuda_context,
                                              CUstream * cuda_stream,
                                              GstObject ** gl_display,
                                              GstObject ** other_gl_context);

void     gst_nv_decoder_set_context          (GstElement * decoder,
                                              GstContext * context,
                                              guint cuda_device_id,
                                              GstCudaContext ** cuda_context,
                                              GstObject ** gl_display,
                                              GstObject ** other_gl_context);

gboolean gst_nv_decoder_handle_context_query (GstElement * decoder,
                                              GstQuery * query,
                                              GstCudaContext * cuda_context,
                                              GstObject * gl_display,
                                              GstObject * gl_context,
                                              GstObject * other_gl_context);

gboolean gst_nv_decoder_negotiate            (GstVideoDecoder * decoder,
                                              GstVideoCodecState * input_state,
                                              GstVideoFormat format,
                                              guint width,
                                              guint height,
                                              GstObject * gl_display,
                                              GstObject * other_gl_context,
                                              GstObject ** gl_context,
                                              GstVideoCodecState ** output_state,
                                              GstNvDecoderOutputType * output_type);

gboolean gst_nv_decoder_decide_allocation (GstNvDecoder * nvdec,
                                           GstVideoDecoder * decocer,
                                           GstQuery * query,
                                           GstObject * gl_context,
                                           GstNvDecoderOutputType output_type);

G_END_DECLS

#endif /* __GST_NV_DECODER_H__ */
