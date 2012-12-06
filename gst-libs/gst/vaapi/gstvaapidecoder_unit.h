/*
 *  gstvaapidecoder_unit.h - VA decoder units
 *
 *  Copyright (C) 2012 Intel Corporation
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

#ifndef GST_VAAPI_DECODER_UNIT_H
#define GST_VAAPI_DECODER_UNIT_H

#include <gst/gstbuffer.h>
#include <gst/vaapi/gstvaapiminiobject.h>

G_BEGIN_DECLS

typedef struct _GstVaapiDecoderUnit             GstVaapiDecoderUnit;

#define GST_VAAPI_DECODER_UNIT(unit) \
    ((GstVaapiDecoderUnit *)(unit))

#define GST_VAAPI_IS_DECODER_UNIT(unit) \
    (GST_VAAPI_DECODER_UNIT(unit) != NULL)

/**
 * GstVaapiDecoderUnitFlags:
 * @GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START: marks the start of a frame.
 * @GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END: marks the end of a frame.
 * @GST_VAAPI_DECODER_UNIT_FLAG_STREAM_END: marks the end of a stream.
 * @GST_VAAPI_DECODER_UNIT_FLAG_SLICE: marks the unit contains slice data.
 * @GST_VAAPI_DECODER_UNIT_FLAG_SKIP: marks the unit as unused/skipped.
 *
 * Flags for #GstVaapiDecoderUnit.
 */
typedef enum {
    GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START = (1 << 0),
    GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END   = (1 << 1),
    GST_VAAPI_DECODER_UNIT_FLAG_STREAM_END  = (1 << 2),
    GST_VAAPI_DECODER_UNIT_FLAG_SLICE       = (1 << 3),
    GST_VAAPI_DECODER_UNIT_FLAG_SKIP        = (1 << 4),
    GST_VAAPI_DECODER_UNIT_FLAG_LAST        = (1 << 5)
} GstVaapiDecoderUnitFlags;

#define GST_VAAPI_DECODER_UNIT_FLAGS        GST_VAAPI_MINI_OBJECT_FLAGS
#define GST_VAAPI_DECODER_UNIT_FLAG_IS_SET  GST_VAAPI_MINI_OBJECT_FLAG_IS_SET
#define GST_VAAPI_DECODER_UNIT_FLAG_SET     GST_VAAPI_MINI_OBJECT_FLAG_SET
#define GST_VAAPI_DECODER_UNIT_FLAG_UNSET   GST_VAAPI_MINI_OBJECT_FLAG_UNSET

/**
 * GST_VAAPI_DECODER_UNIT_IS_FRAME_START:
 * @unit: a #GstVaapiDecoderUnit
 *
 * Tests if the decoder unit marks the start of a frame.
 *
 * The start of a frame is codec dependent but it may include any new
 * sequence header.
 */
#define GST_VAAPI_DECODER_UNIT_IS_FRAME_START(unit) \
    (GST_VAAPI_DECODER_UNIT_FLAG_IS_SET(unit,   \
        GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START))

/**
 * GST_VAAPI_DECODER_UNIT_IS_FRAME_END:
 * @unit: a #GstVaapiDecoderUnit
 *
 * Tests if the decoder unit marks the end of a frame.
 *
 * The end of a frame is codec dependent but it is usually represented
 * by the last bitstream chunk that holds valid slice data.
 */
#define GST_VAAPI_DECODER_UNIT_IS_FRAME_END(unit) \
    (GST_VAAPI_DECODER_UNIT_FLAG_IS_SET(unit,   \
        GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END))

/**
 * GST_VAAPI_DECODER_UNIT_IS_STREAM_END:
 * @unit: a #GstVaapiDecoderUnit
 *
 * Tests if the decoder unit marks the end of the stream.
 */
#define GST_VAAPI_DECODER_UNIT_IS_STREAM_END(unit) \
    (GST_VAAPI_DECODER_UNIT_FLAG_IS_SET(unit,   \
        GST_VAAPI_DECODER_UNIT_FLAG_STREAM_END))

/**
 * GST_VAAPI_DECODER_UNIT_IS_SLICE:
 * @unit: a #GstVaapiDecoderUnit
 *
 * Tests if the decoder unit contains slice data.
 */
#define GST_VAAPI_DECODER_UNIT_IS_SLICE(unit) \
    (GST_VAAPI_DECODER_UNIT_FLAG_IS_SET(unit,   \
        GST_VAAPI_DECODER_UNIT_FLAG_SLICE))

/**
 * GST_VAAPI_DECODER_UNIT_IS_SKIPPED:
 * @unit: a #GstVaapiDecoderUnit
 *
 * Tests if the decoder unit is not needed for decoding an can be skipped.
 * i.e. #GstVaapiDecoder sub-classes won't see this chunk of bitstream
 * data.
 */
#define GST_VAAPI_DECODER_UNIT_IS_SKIPPED(unit) \
    (GST_VAAPI_DECODER_UNIT_FLAG_IS_SET(unit,   \
        GST_VAAPI_DECODER_UNIT_FLAG_SKIP))

/**
 * GstVaapiDecoderUnit:
 * @size: size in bytes of this bitstream unit
 * @offset: relative offset in bytes to bitstream unit within the
 *    associated #GstVideoCodecFrame input_buffer
 * @buffer: (optional) associated buffer or sub-buffer
 * @parsed_info: parser-specific data (this is codec specific)
 * @parsed_info_destroy_notify: function used to release @parsed_info data
 *
 * A chunk of bitstream data that was parsed.
 */
struct _GstVaapiDecoderUnit {
    /*< private >*/
    GstVaapiMiniObject  parent_instance;

    guint               size;
    guint               offset;
    GstBuffer          *buffer;
    gpointer            parsed_info;
    GDestroyNotify      parsed_info_destroy_notify;
};

G_GNUC_INTERNAL
void
gst_vaapi_decoder_unit_init(GstVaapiDecoderUnit *unit, guint size);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_unit_finalize(GstVaapiDecoderUnit *unit);

G_GNUC_INTERNAL
GstVaapiDecoderUnit *
gst_vaapi_decoder_unit_new(guint size);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_unit_set_buffer(GstVaapiDecoderUnit *unit, GstBuffer *buffer);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_unit_set_parsed_info(GstVaapiDecoderUnit *unit,
    gpointer parsed_info, GDestroyNotify destroy_notify);

#define gst_vaapi_decoder_unit_ref(unit) \
    gst_vaapi_mini_object_ref(GST_VAAPI_MINI_OBJECT(unit))

#define gst_vaapi_decoder_unit_unref(unit) \
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(unit))

#define gst_vaapi_decoder_unit_replace(old_unit_p, new_unit)            \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_unit_p),  \
        (GstVaapiMiniObject *)(new_unit))

G_END_DECLS

#endif /* GST_VAAPI_DECODER_UNIT_H */
