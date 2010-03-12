/*
 *  gstvaapiimageformat.c - VA image format abstraction
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "config.h"
#include <glib.h>
#include <gst/video/video.h>
#include "gstvaapiimageformat.h"

typedef enum _GstVaapiImageFormatType           GstVaapiImageFormatType;
typedef struct _GstVaapiImageFormatMap          GstVaapiImageFormatMap;

enum _GstVaapiImageFormatType {
    GST_VAAPI_IMAGE_FORMAT_TYPE_YCBCR = 1,
    GST_VAAPI_IMAGE_FORMAT_TYPE_RGB,
    GST_VAAPI_IMAGE_FORMAT_TYPE_INDEXED
};

struct _GstVaapiImageFormatMap {
    GstVaapiImageFormatType     type;
    GstVaapiImageFormat         format;
    const char                 *caps_str;
    VAImageFormat               va_format;
};

#define DEF(TYPE, FORMAT, CAPS_STR)                                     \
    GST_VAAPI_IMAGE_FORMAT_TYPE_##TYPE,                                 \
    GST_VAAPI_IMAGE_##FORMAT,                                           \
    CAPS_STR
#define DEF_YUV(FORMAT, FOURCC, ENDIAN, BPP)                            \
    { DEF(YCBCR, FORMAT, GST_VIDEO_CAPS_YUV(#FORMAT)),                  \
        { VA_FOURCC FOURCC, VA_##ENDIAN##_FIRST, BPP, }, }
#define DEF_RGB(FORMAT, FOURCC, ENDIAN, BPP, DEPTH, R,G,B,A)            \
    { DEF(RGB, FORMAT, GST_VIDEO_CAPS_##FORMAT),                        \
        { VA_FOURCC FOURCC, VA_##ENDIAN##_FIRST, BPP, DEPTH, R,G,B,A }, }

/* Image formats, listed in HW order preference */
static const GstVaapiImageFormatMap gst_vaapi_image_formats[] = {
    DEF_YUV(NV12, ('N','V','1','2'), LSB, 12),
    DEF_YUV(YV12, ('Y','V','1','2'), LSB, 12),
    DEF_YUV(I420, ('I','4','2','0'), LSB, 12),
#if G_BYTE_ORDER == G_BIG_ENDIAN
    DEF_RGB(ARGB, ('A','R','G','B'), MSB, 32,
            32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000),
    DEF_RGB(ABGR, ('A','B','G','R'), MSB, 32,
            32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000),
#elif G_BYTE_ORDER == G_LITTLE_ENDIAN
    DEF_RGB(BGRA, ('B','G','R','A'), LSB, 32,
            32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000),
    DEF_RGB(RGBA, ('R','G','B','A'), LSB, 32,
            32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000),
#endif
    { 0, }
};

#undef DEF_RGB
#undef DEF_YUV
#undef DEF

static const GstVaapiImageFormatMap *get_map(const VAImageFormat *va_format)
{
    const GstVaapiImageFormatMap *m;

    for (m = gst_vaapi_image_formats; m->format; m++)
        if (m->va_format.fourcc == va_format->fourcc &&
            (m->type == GST_VAAPI_IMAGE_FORMAT_TYPE_RGB ?
             (m->va_format.byte_order == va_format->byte_order &&
              m->va_format.red_mask   == va_format->red_mask   &&
              m->va_format.green_mask == va_format->green_mask &&
              m->va_format.blue_mask  == va_format->blue_mask  &&
              m->va_format.alpha_mask == va_format->alpha_mask) :
             TRUE))
            return m;
    return NULL;
}

static const GstVaapiImageFormatMap *
get_map_from_gst_vaapi_image_format(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap *m;

    for (m = gst_vaapi_image_formats; m->format; m++)
        if (m->format == format)
            return m;
    return NULL;
}

gboolean
gst_vaapi_image_format_is_rgb(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap *m;

    m = get_map_from_gst_vaapi_image_format(format);
    if (!m)
        return FALSE;

    return m->type == GST_VAAPI_IMAGE_FORMAT_TYPE_RGB;
}

gboolean
gst_vaapi_image_format_is_yuv(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap *m;

    m = get_map_from_gst_vaapi_image_format(format);
    if (!m)
        return FALSE;

    return m->type == GST_VAAPI_IMAGE_FORMAT_TYPE_YCBCR;
}

GstVaapiImageFormat
gst_vaapi_image_format(const VAImageFormat *va_format)
{
    const GstVaapiImageFormatMap * const m = get_map(va_format);

    if (!m)
        return 0;

    return m->format;
}

GstVaapiImageFormat
gst_vaapi_image_format_from_fourcc(guint32 fourcc)
{
    return (GstVaapiImageFormat)fourcc;
}

const VAImageFormat *
gst_vaapi_image_format_get_va_format(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap *m;

    m = get_map_from_gst_vaapi_image_format(format);
    if (!m)
        return NULL;

    return &m->va_format;
}

GstCaps *
gst_vaapi_image_format_get_caps(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap *m;

    m = get_map_from_gst_vaapi_image_format(format);
    if (!m)
        return NULL;

    return gst_caps_from_string(m->caps_str);
}

guint
gst_vaapi_image_format_get_score(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap *m;

    m = get_map_from_gst_vaapi_image_format(format);
    if (!m)
        return G_MAXUINT;

    return m - &gst_vaapi_image_formats[0];
}
