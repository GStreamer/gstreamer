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

static inline gboolean
match_va_format_rgb(const VAImageFormat *fmt1, const VAImageFormat *fmt2)
{
    return (fmt1->byte_order == fmt2->byte_order &&
            fmt1->red_mask   == fmt2->red_mask   &&
            fmt1->green_mask == fmt2->green_mask &&
            fmt1->blue_mask  == fmt2->blue_mask  &&
            fmt1->alpha_mask == fmt2->alpha_mask);
}

static const GstVaapiImageFormatMap *get_map(const VAImageFormat *va_format)
{
    const GstVaapiImageFormatMap *m;

    for (m = gst_vaapi_image_formats; m->format; m++)
        if (m->va_format.fourcc == va_format->fourcc &&
            (m->type == GST_VAAPI_IMAGE_FORMAT_TYPE_RGB ?
             match_va_format_rgb(&m->va_format, va_format) :
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
gst_vaapi_image_format_from_caps(GstCaps *caps)
{
    const GstVaapiImageFormatMap *m;
    GstStructure *structure;
    VAImageFormat *va_format, va_formats[2];
    gint endian, rmask, gmask, bmask, amask = 0;
    guint32 fourcc;

    if (!caps)
        return 0;

    structure = gst_caps_get_structure(caps, 0);
    if (!structure)
        return 0;

    /* Check for YUV format */
    if (gst_structure_get_fourcc(structure, "format", &fourcc))
        return gst_vaapi_image_format_from_fourcc(fourcc);

    /* Check for RGB format */
    gst_structure_get_int(structure, "endianness", &endian);
    gst_structure_get_int(structure, "red_mask",   &rmask);
    gst_structure_get_int(structure, "green_mask", &gmask);
    gst_structure_get_int(structure, "blue_mask",  &bmask);
    gst_structure_get_int(structure, "alpha_mask", &amask);

    va_format = &va_formats[0];
    va_format->byte_order = endian == G_BIG_ENDIAN ? VA_MSB_FIRST : VA_LSB_FIRST;
    va_format->red_mask   = rmask;
    va_format->green_mask = gmask;
    va_format->blue_mask  = bmask;
    va_format->alpha_mask = amask;

    va_format = &va_formats[1];
    va_format->byte_order = endian == G_BIG_ENDIAN ? VA_LSB_FIRST : VA_MSB_FIRST;
    va_format->red_mask   = GUINT32_SWAP_LE_BE(rmask);
    va_format->green_mask = GUINT32_SWAP_LE_BE(gmask);
    va_format->blue_mask  = GUINT32_SWAP_LE_BE(bmask);
    va_format->alpha_mask = GUINT32_SWAP_LE_BE(amask);

    for (m = gst_vaapi_image_formats; m->format; m++)
        if (match_va_format_rgb(&m->va_format, &va_formats[0]) ||
            match_va_format_rgb(&m->va_format, &va_formats[1]))
            return m->format;

    return 0;
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
