/*
 *  gstvaapidecoder_h264.h - H.264 decoder
 *
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_DECODER_H264_H
#define GST_VAAPI_DECODER_H264_H

#include <gst/vaapi/gstvaapidecoder.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_DECODER_H264 \
    (gst_vaapi_decoder_h264_get_type ())
#define GST_VAAPI_DECODER_H264(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_DECODER_H264, GstVaapiDecoderH264))
#define GST_VAAPI_IS_DECODER_H264(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_DECODER_H264))

typedef struct _GstVaapiDecoderH264             GstVaapiDecoderH264;

/**
 * GstVaapiStreamAlignH264:
 * @GST_VAAPI_STREAM_ALIGN_H264_NONE: Generic H.264 stream buffers
 * @GST_VAAPI_STREAM_ALIGN_H264_NALU: H.264 stream buffers aligned NAL
 *   unit boundaries
 * @GST_VAAPI_STREAM_ALIGN_H264_AU: H.264 stream buffers aligned on
 *   access unit boundaries
 *
 * Set of possible buffer alignments for H.264 streams.
 */
typedef enum {
    GST_VAAPI_STREAM_ALIGN_H264_NONE,
    GST_VAAPI_STREAM_ALIGN_H264_NALU,
    GST_VAAPI_STREAM_ALIGN_H264_AU
} GstVaapiStreamAlignH264;

GType
gst_vaapi_decoder_h264_get_type (void) G_GNUC_CONST;

GstVaapiDecoder *
gst_vaapi_decoder_h264_new (GstVaapiDisplay *display, GstCaps *caps);

void
gst_vaapi_decoder_h264_set_alignment(GstVaapiDecoderH264 *decoder,
    GstVaapiStreamAlignH264 alignment);

gboolean
gst_vaapi_decoder_h264_get_low_latency(GstVaapiDecoderH264 * decoder);

void
gst_vaapi_decoder_h264_set_low_latency(GstVaapiDecoderH264 * decoder,
    gboolean force_low_latency);

void
gst_vaapi_decoder_h264_set_base_only(GstVaapiDecoderH264 * decoder,
    gboolean base_only);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiDecoderH264, gst_object_unref)

G_END_DECLS

#endif /* GST_VAAPI_DECODER_H264_H */
