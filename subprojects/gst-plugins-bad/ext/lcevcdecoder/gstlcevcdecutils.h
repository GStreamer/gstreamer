/* GStreamer
 *  Copyright (C) <2024> V-Nova International Limited
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

#ifndef __GST_LCEVC_DEC_UTILS_H__
#define __GST_LCEVC_DEC_UTILS_H__

#include <gst/gst.h>
#include <gst/video/video-info.h>

#include <LCEVC/lcevc_dec.h>

G_BEGIN_DECLS

/* TODO: Only I420 and NV12 are currently working with the SDK */
#define GST_LCEVC_DEC_UTILS_SUPPORTED_FORMATS \
    "{ I420, NV12 }"

LCEVC_ColorFormat gst_lcevc_dec_utils_get_color_format (GstVideoFormat format);

gboolean gst_lcevc_dec_utils_alloc_picture_handle (
    LCEVC_DecoderHandle decoder_handle, GstVideoFrame *frame,
    LCEVC_PictureHandle *picture_handle);

G_END_DECLS

#endif /* __GST_LCEVC_DEC_UTILS_H__ */
