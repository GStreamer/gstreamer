/*
 *  image.c - Image utilities for the tests
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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

#include "image.h"

GstVaapiImage *
image_generate(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format,
    guint               width,
    guint               height
)
{
    const guint w = width;
    const guint h = height;
    GstVaapiImage *image;

    image = gst_vaapi_image_new(display, format, w, h);
    if (!image)
        return NULL;

    if (image_draw_rectangle(image, 0,   0,   w/2, h/2, 0xffff0000) &&
        image_draw_rectangle(image, w/2, 0,   w/2, h/2, 0xff00ff00) &&
        image_draw_rectangle(image, 0,   h/2, w/2, h/2, 0xff0000ff) &&
        image_draw_rectangle(image, w/2, h/2, w/2, h/2, 0xff000000))
        return image;

    g_object_unref(image);
    return NULL;
}

typedef void (*DrawRectFunc)(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
);

static void draw_rect_ARGB(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    guint i, j;

    color = GUINT32_TO_BE(color);

    for (j = 0; j < height; j++) {
        guint32 *p = (guint32 *)(pixels[0] + (y + j) * stride[0] + x * 4);
        for (i = 0; i < width; i++)
            p[i] = color;
    }
}

static void draw_rect_BGRA(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    // Converts ARGB color to BGRA
    color = GUINT32_SWAP_LE_BE(color);

    draw_rect_ARGB(pixels, stride, x, y, width, height, color);
}

static void draw_rect_RGBA(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    // Converts ARGB color to RGBA
    color = ((color >> 24) & 0xff) | ((color & 0xffffff) << 8);

    draw_rect_ARGB(pixels, stride, x, y, width, height, color);
}

static void draw_rect_ABGR(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    // Converts ARGB color to ABGR
    color = ((color & 0xff00ff00)   |
             ((color >> 16) & 0xff) |
             ((color & 0xff) << 16));

    draw_rect_ARGB(pixels, stride, x, y, width, height, color);
}

static void draw_rect_NV12( // Y, UV planes
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    const guchar Y  = color >> 16;
    const guchar Cb = color >> 8;
    const guchar Cr = color;
    guchar *dst;
    guint i, j;

    dst = pixels[0] + y * stride[0] + x;
    for (j = 0; j < height; j++, dst += stride[0])
        for (i = 0; i < width; i++)
            dst[i] = Y;

    x      /= 2;
    y      /= 2;
    width  /= 2;
    height /= 2;

    dst = pixels[1] + y * stride[1] + x * 2;
    for (j = 0; j < height; j++, dst += stride[1])
        for (i = 0; i < width; i++) {
            dst[2*i + 0] = Cb;
            dst[2*i + 1] = Cr;
        }
}

static void draw_rect_YV12( // Y, V, U planes
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    const guchar Y  = color >> 16;
    const guchar Cb = color >> 8;
    const guchar Cr = color;
    guchar *pY, *pU, *pV;
    guint i, j;

    pY = pixels[0] + y * stride[0] + x;
    for (j = 0; j < height; j++, pY += stride[0])
        for (i = 0; i < width; i++)
            pY[i] = Y;

    x      /= 2;
    y      /= 2;
    width  /= 2;
    height /= 2;

    pU = pixels[1] + y * stride[1] + x;
    pV = pixels[2] + y * stride[2] + x;
    for (j = 0; j < height; j++, pU += stride[1], pV += stride[2])
        for (i = 0; i < width; i++) {
            pU[i] = Cb;
            pV[i] = Cr;
        }
}

static void draw_rect_I420( // Y, U, V planes
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    guchar *new_pixels[3] = { pixels[0], pixels[2], pixels[1] };
    guint   new_stride[3] = { stride[0], stride[2], stride[1] };

    draw_rect_YV12(new_pixels, new_stride, x, y, width, height, color);
}

static void draw_rect_AYUV(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    guint i, j;

    color = color | 0xff000000;

    for (j = 0; j < height; j++) {
        guint32 *p = (guint32 *)(pixels[0] + (y + j) * stride[0] + x * 4);
        for (i = 0; i < width; i++)
            p[i] = color;
    }
}

static inline guint32 argb2yuv(guint32 color)
{
    const gint32 r = (color >> 16) & 0xff;
    const gint32 g = (color >>  8) & 0xff;
    const gint32 b = (color      ) & 0xff;

    const guint32 y = (( 263 * r + 516 * g + 100 * b) >> 10) +  16;
    const guint32 u = ((-152 * r - 298 * g + 450 * b) >> 10) + 128;
    const guint32 v = (( 450 * r - 376 * g -  73 * b) >> 10) + 128;

    return (y << 16) | (u << 8) | v;
}

gboolean
image_draw_rectangle(
    GstVaapiImage *image,
    gint           x,
    gint           y,
    guint          width,
    guint          height,
    guint32        color
)
{
    const GstVaapiImageFormat image_format = gst_vaapi_image_get_format(image);
    const guint               image_width  = gst_vaapi_image_get_width(image);
    const guint               image_height = gst_vaapi_image_get_height(image);
    GstVaapiDisplay          *display;
    guchar                   *pixels[3];
    guint                     stride[3];
    DrawRectFunc              draw_rect = NULL;
    guint                     i;

    static const struct {
        GstVaapiImageFormat   format;
        DrawRectFunc          draw_rect;
    }
    map[] = {
#define _(FORMAT) { GST_VAAPI_IMAGE_##FORMAT, draw_rect_##FORMAT }
        _(ARGB),
        _(BGRA),
        _(RGBA),
        _(ABGR),
        _(NV12),
        _(YV12),
        _(I420),
        _(AYUV),
#undef  _
        { 0, }
    };

    for (i = 0; !draw_rect && map[i].format; i++)
        if (map[i].format == image_format)
            draw_rect = map[i].draw_rect;
    if (!draw_rect)
        return FALSE;

    display = gst_vaapi_object_get_display(GST_VAAPI_OBJECT(image));
    if (!display)
        return FALSE;

    if (!gst_vaapi_image_map(image))
        return FALSE;

    for (i = 0; i < gst_vaapi_image_get_plane_count(image); i++) {
        pixels[i] = gst_vaapi_image_get_plane(image, i);
        stride[i] = gst_vaapi_image_get_pitch(image, i);
    }

    if (gst_vaapi_image_format_is_yuv(image_format))
        color = argb2yuv(color);

    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (width > image_width - x)
        width = image_width - x;
    if (height > image_height - y)
        height = image_height - y;

    gst_vaapi_display_lock(display);
    draw_rect(pixels, stride, x, y, width, height, color);
    gst_vaapi_display_unlock(display);
    return gst_vaapi_image_unmap(image);
}

gboolean
image_upload(GstVaapiImage *image, GstVaapiSurface *surface)
{
    GstVaapiDisplay    *display;
    GstVaapiImageFormat format;
    GstVaapiSubpicture *subpicture;

    display = gst_vaapi_object_get_display(GST_VAAPI_OBJECT(surface));
    if (!display)
        return FALSE;

    format = gst_vaapi_image_get_format(image);
    if (!format)
        return FALSE;

    if (gst_vaapi_surface_put_image(surface, image))
        return TRUE;

    g_print("could not upload %" GST_FOURCC_FORMAT" image to surface\n",
            GST_FOURCC_ARGS(format));

    if (!gst_vaapi_display_has_subpicture_format(display, format, NULL))
        return FALSE;

    g_print("trying as a subpicture\n");

    subpicture = gst_vaapi_subpicture_new(image, 0);
    if (!subpicture)
        g_error("could not create VA subpicture");

    if (!gst_vaapi_surface_associate_subpicture(surface, subpicture,
                                                NULL, NULL))
        g_error("could not associate subpicture to surface");

    /* The surface holds a reference to the subpicture. This is safe */
    g_object_unref(subpicture);
    return TRUE;
}
