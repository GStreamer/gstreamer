/*
 *  gstvaapiimageformat.h - VA image format abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011 Intel Corporation
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

#ifndef GST_VAAPI_IMAGE_FORMAT_H
#define GST_VAAPI_IMAGE_FORMAT_H

#include <gst/gstvalue.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef enum _GstVaapiImageFormat               GstVaapiImageFormat;

/**
 * GstVaapiImageFormat:
 * @GST_VAAPI_IMAGE_NV12:
 *   planar YUV 4:2:0, 12-bit, 1 plane for Y and 1 plane for UV
 * @GST_VAAPI_IMAGE_YV12:
 *   planar YUV 4:2:0, 12-bit, 3 planes for Y V U
 * @GST_VAAPI_IMAGE_I420:
 *   planar YUV 4:2:0, 12-bit, 3 planes for Y U V
 * @GST_VAAPI_IMAGE_AYUV:
 *   packed YUV 4:4:4, 32-bit, A Y U V, native endian byte-order
 * @GST_VAAPI_IMAGE_ARGB:
 *   packed RGB 8:8:8, 32-bit, A R G B
 * @GST_VAAPI_IMAGE_RGBA:
 *   packed RGB 8:8:8, 32-bit, R G B A
 * @GST_VAAPI_IMAGE_ABGR:
 *   packed RGB 8:8:8, 32-bit, A B G R
 * @GST_VAAPI_IMAGE_BGRA:
 *   packed RGB 8:8:8, 32-bit, B G R A
 *
 * The set of all image formats for #GstVaapiImage.
 */
enum _GstVaapiImageFormat {
    GST_VAAPI_IMAGE_NV12 = GST_MAKE_FOURCC('N','V','1','2'),
    GST_VAAPI_IMAGE_YV12 = GST_MAKE_FOURCC('Y','V','1','2'),
    GST_VAAPI_IMAGE_I420 = GST_MAKE_FOURCC('I','4','2','0'),
    GST_VAAPI_IMAGE_AYUV = GST_MAKE_FOURCC('A','Y','U','V'),
    GST_VAAPI_IMAGE_ARGB = GST_MAKE_FOURCC('A','R','G','B'),
    GST_VAAPI_IMAGE_RGBA = GST_MAKE_FOURCC('R','G','B','A'),
    GST_VAAPI_IMAGE_ABGR = GST_MAKE_FOURCC('A','B','G','R'),
    GST_VAAPI_IMAGE_BGRA = GST_MAKE_FOURCC('B','G','R','A'),
};

gboolean
gst_vaapi_image_format_is_rgb(GstVaapiImageFormat format);

gboolean
gst_vaapi_image_format_is_yuv(GstVaapiImageFormat format);

GstVaapiImageFormat
gst_vaapi_image_format(const VAImageFormat *va_format);

GstVaapiImageFormat
gst_vaapi_image_format_from_caps(GstCaps *caps);

GstVaapiImageFormat
gst_vaapi_image_format_from_fourcc(guint32 fourcc);

GstVaapiImageFormat
gst_vaapi_image_format_from_video(GstVideoFormat format);

const VAImageFormat *
gst_vaapi_image_format_get_va_format(GstVaapiImageFormat format);

GstCaps *
gst_vaapi_image_format_get_caps(GstVaapiImageFormat format);

guint
gst_vaapi_image_format_get_score(GstVaapiImageFormat format);

G_END_DECLS

#endif /* GST_GST_VAAPI_IMAGE_H */
