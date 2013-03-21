/*
 *  gstvaapivideobuffer.c - Gstreamer/VA video buffer
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

#include "gst/vaapi/sysdeps.h"
#include <gst/video/gstsurfacebuffer.h>
#include "gstvaapivideobuffer.h"

#define GST_VAAPI_TYPE_VIDEO_BUFFER \
    (gst_vaapi_video_buffer_get_type())

#define GST_VAAPI_VIDEO_BUFFER(obj)             \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),          \
        GST_VAAPI_TYPE_VIDEO_BUFFER,            \
        GstVaapiVideoBuffer))

#define GST_VAAPI_VIDEO_BUFFER_CLASS(klass)     \
    (G_TYPE_CHECK_CLASS_CAST((klass),           \
        GST_VAAPI_TYPE_VIDEO_BUFFER,            \
        GstVaapiVideoBufferClass))

#define GST_VAAPI_IS_VIDEO_BUFFER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_VIDEO_BUFFER))

#define GST_VAAPI_IS_VIDEO_BUFFER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_VIDEO_BUFFER))

#define GST_VAAPI_VIDEO_BUFFER_GET_CLASS(obj)   \
    (G_TYPE_INSTANCE_GET_CLASS((obj),           \
        GST_VAAPI_TYPE_VIDEO_BUFFER,            \
        GstVaapiVideoBufferClass))

typedef struct _GstVaapiVideoBufferClass        GstVaapiVideoBufferClass;

/**
 * GstVaapiVideoBuffer:
 *
 * A #GstBuffer holding video objects (#GstVaapiSurface and #GstVaapiImage).
 */
struct _GstVaapiVideoBuffer {
    /*< private >*/
    GstSurfaceBuffer parent_instance;

    GstVaapiVideoMeta *meta;
};

/**
 * GstVaapiVideoBufferClass:
 *
 * A #GstBuffer holding video objects
 */
struct _GstVaapiVideoBufferClass {
    /*< private >*/
    GstSurfaceBufferClass parent_class;
};

GType
gst_vaapi_video_buffer_get_type(void) G_GNUC_CONST;

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

GstVaapiVideoMeta *
gst_vaapi_video_buffer_get_meta(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), NULL);

    return buffer->meta;
}
