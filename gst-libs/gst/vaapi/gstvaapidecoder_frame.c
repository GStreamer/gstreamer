/*
 *  gstvaapidecoder_frame.c - VA decoder frame
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

/**
 * SECTION:gstvaapidecoder_frame
 * @short_description: VA decoder frame
 */

#include "sysdeps.h"
#include "gstvaapidecoder_frame.h"

static inline const GstVaapiMiniObjectClass *
gst_vaapi_decoder_frame_class(void)
{
    static const GstVaapiMiniObjectClass GstVaapiDecoderFrameClass = {
        sizeof(GstVaapiDecoderFrame),
        (GDestroyNotify)gst_vaapi_decoder_frame_free
    };
    return &GstVaapiDecoderFrameClass;
}

static inline void
free_units(GSList **units_ptr)
{
    GSList * const units = *units_ptr;

    if (units) {
        g_slist_free_full(units, (GDestroyNotify)gst_vaapi_mini_object_unref);
        *units_ptr = NULL;
    }
}

/**
 * gst_vaapi_decoder_frame_new:
 *
 * Creates a new #GstVaapiDecoderFrame object.
 *
 * Returns: The newly allocated #GstVaapiDecoderFrame
 */
GstVaapiDecoderFrame *
gst_vaapi_decoder_frame_new(void)
{
    return (GstVaapiDecoderFrame *)
        gst_vaapi_mini_object_new0(gst_vaapi_decoder_frame_class());
}

/**
 * gst_vaapi_decoder_frame_free:
 * @frame: a #GstVaapiDecoderFrame
 *
 * Deallocates any internal resources bound to the supplied decoder
 * @frame.
 *
 * @note This is an internal function used to implement lightweight
 * sub-classes.
 */
void
gst_vaapi_decoder_frame_free(GstVaapiDecoderFrame *frame)
{
    free_units(&frame->units);
    free_units(&frame->pre_units);
    free_units(&frame->post_units);
}
