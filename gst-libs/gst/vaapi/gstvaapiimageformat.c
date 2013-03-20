/*
 *  gstvaapiimageformat.c - VA image format abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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
 * SECTION:gstvaapiimageformat
 * @short_description: VA image format abstraction
 */

#include "sysdeps.h"
#include <gst/video/video.h>
#include "gstvaapicompat.h"
#include "gstvaapiimageformat.h"

typedef struct _GstVaapiImageFormatMap          GstVaapiImageFormatMap;

typedef enum {
    GST_VAAPI_IMAGE_FORMAT_TYPE_YCBCR = 1,      /* YUV */
    GST_VAAPI_IMAGE_FORMAT_TYPE_RGB,            /* RGB */
    GST_VAAPI_IMAGE_FORMAT_TYPE_INDEXED         /* paletted */
} GstVaapiImageFormatType;

struct _GstVaapiImageFormatMap {
    GstVaapiImageFormatType     type;
    GstVaapiImageFormat         format;
    const char                 *caps_str;
    VAImageFormat               va_format;
};

#if GST_CHECK_VERSION(1,0,0)
# define GST_VIDEO_CAPS_MAKE_YUV(FORMAT) \
    GST_VIDEO_CAPS_MAKE(#FORMAT)
# define GST_VIDEO_CAPS_MAKE_RGB(FORMAT) \
    GST_VIDEO_CAPS_MAKE(#FORMAT)
#else
# define GST_VIDEO_CAPS_MAKE_YUV(FORMAT) \
    GST_VIDEO_CAPS_YUV(#FORMAT)
# define GST_VIDEO_CAPS_MAKE_RGB(FORMAT) \
    GST_VIDEO_CAPS_##FORMAT
#endif

#define DEF(TYPE, FORMAT, CAPS_STR)                                     \
    GST_VAAPI_IMAGE_FORMAT_TYPE_##TYPE,                                 \
    GST_VAAPI_IMAGE_##FORMAT,                                           \
    CAPS_STR
#define DEF_YUV(FORMAT, FOURCC, ENDIAN, BPP)                            \
    { DEF(YCBCR, FORMAT, GST_VIDEO_CAPS_MAKE_YUV(FORMAT)),              \
        { VA_FOURCC FOURCC, VA_##ENDIAN##_FIRST, BPP, }, }
#define DEF_RGB(FORMAT, FOURCC, ENDIAN, BPP, DEPTH, R,G,B,A)            \
    { DEF(RGB, FORMAT, GST_VIDEO_CAPS_MAKE_RGB(FORMAT)),                \
        { VA_FOURCC FOURCC, VA_##ENDIAN##_FIRST, BPP, DEPTH, R,G,B,A }, }

/* Image formats, listed in HW order preference */
static const GstVaapiImageFormatMap gst_vaapi_image_formats[] = {
    DEF_YUV(NV12, ('N','V','1','2'), LSB, 12),
    DEF_YUV(YV12, ('Y','V','1','2'), LSB, 12),
    DEF_YUV(I420, ('I','4','2','0'), LSB, 12),
    DEF_YUV(AYUV, ('A','Y','U','V'), LSB, 32),
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

static const GstVaapiImageFormatMap *
get_map(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap *m;

    for (m = gst_vaapi_image_formats; m->format; m++)
        if (m->format == format)
            return m;
    return NULL;
}

/**
 * gst_vaapi_image_format_is_rgb:
 * @format: a #GstVaapiImageFormat
 *
 * Checks whether the format is an RGB format.
 *
 * Return value: %TRUE if @format is RGB format
 */
gboolean
gst_vaapi_image_format_is_rgb(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap * const m = get_map(format);

    return m ? (m->type == GST_VAAPI_IMAGE_FORMAT_TYPE_RGB) : FALSE;
}

/**
 * gst_vaapi_image_format_is_yuv:
 * @format: a #GstVaapiImageFormat
 *
 * Checks whether the format is an YUV format.
 *
 * Return value: %TRUE if @format is YUV format
 */
gboolean
gst_vaapi_image_format_is_yuv(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap * const m = get_map(format);

    return m ? (m->type == GST_VAAPI_IMAGE_FORMAT_TYPE_YCBCR) : FALSE;
}

/**
 * gst_vaapi_image_format:
 * @va_format: a #VAImageFormat
 *
 * Converts a VA image format into the corresponding #GstVaapiImageFormat.
 * If the image format cannot be represented by #GstVaapiImageFormat,
 * then zero is returned.
 *
 * Return value: the #GstVaapiImageFormat describing the @va_format
 */
GstVaapiImageFormat
gst_vaapi_image_format(const VAImageFormat *va_format)
{
    const GstVaapiImageFormatMap *m;

    for (m = gst_vaapi_image_formats; m->format; m++)
        if (m->va_format.fourcc == va_format->fourcc &&
            (m->type == GST_VAAPI_IMAGE_FORMAT_TYPE_RGB ?
             match_va_format_rgb(&m->va_format, va_format) :
             TRUE))
            return m->format;

    return 0;
}

/**
 * gst_vaapi_image_format_from_caps:
 * @caps: a #GstCaps
 *
 * Converts @caps into the corresponding #GstVaapiImageFormat. If the
 * image format cannot be represented by #GstVaapiImageFormat, then
 * zero is returned.
 *
 * Return value: the #GstVaapiImageFormat describing the @caps
 */
GstVaapiImageFormat
gst_vaapi_image_format_from_caps(GstCaps *caps)
{
    GstStructure *structure;

    if (!caps)
        return 0;

    structure = gst_caps_get_structure(caps, 0);
    if (!structure)
        return 0;
    return gst_vaapi_image_format_from_structure(structure);
}

/**
 * gst_vaapi_image_format_from_structure:
 * @structure: a #GstStructure
 *
 * Converts @structure into the corresponding #GstVaapiImageFormat. If
 * the image format cannot be represented by #GstVaapiImageFormat,
 * then zero is returned.
 *
 * Return value: the #GstVaapiImageFormat describing the @structure
 */
GstVaapiImageFormat
gst_vaapi_image_format_from_structure(GstStructure *structure)
{
    const GstVaapiImageFormatMap *m;
    VAImageFormat *va_format, va_formats[2];
    gint endian, rmask, gmask, bmask, amask = 0;
    guint32 fourcc;

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

/**
 * gst_vaapi_image_format_from_fourcc:
 * @fourcc: a FOURCC value
 *
 * Converts a FOURCC value into the corresponding #GstVaapiImageFormat.
 * If the image format cannot be represented by #GstVaapiImageFormat,
 * then zero is returned.
 *
 * Return value: the #GstVaapiImageFormat describing the FOURCC value
 */
GstVaapiImageFormat
gst_vaapi_image_format_from_fourcc(guint32 fourcc)
{
    return (GstVaapiImageFormat)fourcc;
}

/**
 * gst_vaapi_image_format_from_video:
 * @format: a #GstVideoFormat
 *
 * Converts a #GstVideoFormat into the corresponding
 * #GstVaapiImageFormat.  If the image format cannot be represented by
 * #GstVaapiImageFormat, then zero is returned.
 *
 * Return value: the #GstVaapiImageFormat describing the video format
 */
GstVaapiImageFormat
gst_vaapi_image_format_from_video(GstVideoFormat format)
{
    GstVaapiImageFormat va_format;

    switch (format) {
    case GST_VIDEO_FORMAT_NV12: va_format = GST_VAAPI_IMAGE_NV12;   break;
    case GST_VIDEO_FORMAT_YV12: va_format = GST_VAAPI_IMAGE_YV12;   break;
    case GST_VIDEO_FORMAT_I420: va_format = GST_VAAPI_IMAGE_I420;   break;
    case GST_VIDEO_FORMAT_AYUV: va_format = GST_VAAPI_IMAGE_AYUV;   break;
    case GST_VIDEO_FORMAT_ARGB: va_format = GST_VAAPI_IMAGE_ARGB;   break;
    case GST_VIDEO_FORMAT_RGBA: va_format = GST_VAAPI_IMAGE_RGBA;   break;
    case GST_VIDEO_FORMAT_ABGR: va_format = GST_VAAPI_IMAGE_ABGR;   break;
    case GST_VIDEO_FORMAT_BGRA: va_format = GST_VAAPI_IMAGE_BGRA;   break;
    default:                    va_format = (GstVaapiImageFormat)0; break;
    }
    return va_format;
}

/**
 * gst_vaapi_image_format_get_va_format:
 * @format: a #GstVaapiImageFormat
 *
 * Converts a #GstVaapiImageFormat into the corresponding VA image
 * format. If no matching VA image format was found, %NULL is returned
 * and this error must be reported to be fixed.
 *
 * Return value: the VA image format, or %NULL if none was found
 */
const VAImageFormat *
gst_vaapi_image_format_get_va_format(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap * const m = get_map(format);

    return m ? &m->va_format : NULL;
}

/**
 * gst_vaapi_image_format_get_caps:
 * @format: a #GstVaapiImageFormat
 *
 * Converts a #GstVaapiImageFormat into the corresponding #GstCaps. If
 * no matching caps were found, %NULL is returned.
 *
 * Return value: the newly allocated #GstCaps, or %NULL if none was found
 */
GstCaps *
gst_vaapi_image_format_get_caps(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap * const m = get_map(format);

    return m ? gst_caps_from_string(m->caps_str) : NULL;
}

/**
 * gst_vaapi_image_format_get_score:
 * @format: a #GstVaapiImageFormat
 *
 * Determines how "native" is this @format. The lower is the returned
 * score, the best format this is for the underlying hardware.
 *
 * Return value: the @format score, or %G_MAXUINT if none was found
 */
guint
gst_vaapi_image_format_get_score(GstVaapiImageFormat format)
{
    const GstVaapiImageFormatMap * const m = get_map(format);

    return m ? (m - &gst_vaapi_image_formats[0]) : G_MAXUINT;
}
