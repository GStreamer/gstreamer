/*
 *  gstvaapivideobuffer.c - Gst VA video buffer
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2013 Intel Corporation
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
 * SECTION:gstvaapivideobuffer
 * @short_description: VA video buffer for GStreamer
 */

#include "sysdeps.h"
#include "gstvaapivideobuffer.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiVideoBuffer,
              gst_vaapi_video_buffer,
              GST_TYPE_SURFACE_BUFFER)

typedef GstSurfaceConverter *(*GstSurfaceConverterCreateFunc)(
    GstSurfaceBuffer *surface, const gchar *type, GValue *dest);

static GstSurfaceConverter *
gst_vaapi_video_buffer_create_converter(GstSurfaceBuffer *surface,
    const gchar *type, GValue *dest)
{
    GstVaapiVideoBuffer * const vbuffer = GST_VAAPI_VIDEO_BUFFER(surface);
    GstSurfaceConverterCreateFunc func;

    func = (GstSurfaceConverterCreateFunc)
        gst_vaapi_video_meta_get_surface_converter(vbuffer->meta);

    return func ? func(surface, type, dest) : NULL;
}

static void
gst_vaapi_video_buffer_class_init(GstVaapiVideoBufferClass *klass)
{
    GstSurfaceBufferClass * const surface_class =
        GST_SURFACE_BUFFER_CLASS(klass);

    surface_class->create_converter = gst_vaapi_video_buffer_create_converter;
}

static void
gst_vaapi_video_buffer_init(GstVaapiVideoBuffer *buffer)
{
}

/**
 * gst_vaapi_video_buffer_new:
 * @meta: a #GstVaapiVideoMeta
 *
 * Creates a #GstBuffer that holds video @meta information.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL or error
 */
GstBuffer *
gst_vaapi_video_buffer_new(GstVaapiVideoMeta *meta)
{
    GstBuffer *buffer;

    g_return_val_if_fail(meta != NULL, NULL);

    buffer = GST_BUFFER_CAST(gst_mini_object_new(GST_TYPE_SURFACE_BUFFER));
    if (!buffer)
        return NULL;

    gst_buffer_set_vaapi_video_meta(buffer, meta);
    return buffer;
}

/**
 * gst_vaapi_video_buffer_get_meta:
 * @buffer: a #GstVaapiVideoBuffer
 *
 * Returns the #GstVaapiVideoMeta associated to this @buffer.
 *
 * Return value: the #GstVaapiVideoMeta bound to the @buffer, or %NULL
 *   if none was found
 */
GstVaapiVideoMeta *
gst_vaapi_video_buffer_get_meta(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), NULL);

    return buffer->meta;
}
