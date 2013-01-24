/*
 *  gstvaapidecoder_frame.h - VA decoder frame
 *
 *  Copyright (C) 2012-2013 Intel Corporation
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

#ifndef GST_VAAPI_DECODER_FRAME_H
#define GST_VAAPI_DECODER_FRAME_H

#include <gst/vaapi/gstvaapiminiobject.h>
#include <gst/vaapi/gstvaapidecoder_unit.h>

G_BEGIN_DECLS

typedef struct _GstVaapiDecoderFrame            GstVaapiDecoderFrame;

#define GST_VAAPI_DECODER_FRAME(frame) \
    ((GstVaapiDecoderFrame *)(frame))

#define GST_VAAPI_IS_DECODER_FRAME(frame) \
    (GST_VAAPI_DECODER_FRAME(frame) != NULL)

/**
 * GstVaapiDecoderFrameFlags:
 *
 * Flags for #GstVaapiDecoderFrame.
 */
typedef enum {
    GST_VAAPI_DECODER_FRAME_FLAG_LAST        = (1 << 0)
} GstVaapiDecoderFrameFlags;

#define GST_VAAPI_DECODER_FRAME_FLAGS        GST_VAAPI_MINI_OBJECT_FLAGS
#define GST_VAAPI_DECODER_FRAME_FLAG_IS_SET  GST_VAAPI_MINI_OBJECT_FLAG_IS_SET
#define GST_VAAPI_DECODER_FRAME_FLAG_SET     GST_VAAPI_MINI_OBJECT_FLAG_SET
#define GST_VAAPI_DECODER_FRAME_FLAG_UNSET   GST_VAAPI_MINI_OBJECT_FLAG_UNSET

/**
 * GstVaapiDecoderFrame:
 * @output_offset: current offset to the reconstructed #GstBuffer for
 *    this #GstVideoCodecFrame. This is used to initialize the decoder
 *    unit offset
 * @units: list of #GstVaapiDecoderUnit objects (slice data)
 * @pre_units: list of units to decode before GstVaapiDecoder:start_frame()
 * @post_units: list of units to decode after GstVaapiDecoder:end_frame()
 *
 * An extension to #GstVideoCodecFrame with #GstVaapiDecoder specific
 * information. Decoder frames are usually attached to codec frames as
 * the user_data anchor point.
 */
struct _GstVaapiDecoderFrame {
    /*< private >*/
    GstVaapiMiniObject   parent_instance;

    guint                output_offset;
    GArray              *units;
    GArray              *pre_units;
    GArray              *post_units;
};

G_GNUC_INTERNAL
GstVaapiDecoderFrame *
gst_vaapi_decoder_frame_new(guint width, guint height);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_frame_free(GstVaapiDecoderFrame *frame);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_frame_append_unit(GstVaapiDecoderFrame *frame,
    GstVaapiDecoderUnit *unit);

#define gst_vaapi_decoder_frame_ref(frame) \
    gst_vaapi_mini_object_ref(GST_VAAPI_MINI_OBJECT(frame))

#define gst_vaapi_decoder_frame_unref(frame) \
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(frame))

#define gst_vaapi_decoder_frame_replace(old_frame_p, new_frame)         \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_frame_p), \
        (GstVaapiMiniObject *)(new_frame))

G_END_DECLS

#endif /* GST_VAAPI_DECODER_FRAME_H */
