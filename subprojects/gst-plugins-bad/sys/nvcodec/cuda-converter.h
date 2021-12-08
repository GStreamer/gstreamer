/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_CUDA_CONVERTER_H__
#define __GST_CUDA_CONVERTER_H__

#include <gst/video/video.h>
#include <gst/cuda/gstcudacontext.h>
#include <gst/cuda/gstcudamemory.h>

G_BEGIN_DECLS

typedef struct _GstCudaConverter GstCudaConverter;

GstCudaConverter *    gst_cuda_converter_new           (GstVideoInfo * in_info,
                                                        GstVideoInfo * out_info,
                                                        GstCudaContext * cuda_ctx);

void                 gst_cuda_converter_free           (GstCudaConverter * convert);

gboolean             gst_cuda_converter_convert_frame  (GstCudaConverter * convert,
                                                        GstVideoFrame * src_frame,
                                                        GstVideoFrame * dst_frame,
                                                        CUstream cuda_stream);

G_END_DECLS

#endif /* __GST_CUDA_CONVERTER_H__ */
