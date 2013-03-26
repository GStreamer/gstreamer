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
#if USE_GLX
# include "gstvaapivideoconverter_glx.h"
#endif

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

static GFunc
get_surface_converter(GstVaapiDisplay *display)
{
    GFunc func;

    switch (gst_vaapi_display_get_display_type(display)) {
#if USE_GLX
    case GST_VAAPI_DISPLAY_TYPE_GLX:
        func = (GFunc)gst_vaapi_video_converter_glx_new;
        break;
#endif
    default:
        func = NULL;
        break;
    }
    return func;
}

static GstBuffer *
new_vbuffer(GstVaapiVideoMeta *meta)
{
    GstBuffer *buffer;

    g_return_val_if_fail(meta != NULL, NULL);

    gst_vaapi_video_meta_set_surface_converter(meta,
        get_surface_converter(gst_vaapi_video_meta_get_display(meta)));

    buffer = GST_BUFFER_CAST(gst_mini_object_new(GST_TYPE_SURFACE_BUFFER));
    if (buffer)
        gst_buffer_set_vaapi_video_meta(buffer, meta);
    gst_vaapi_video_meta_unref(meta);
    return buffer;
}

GstBuffer *
gst_vaapi_video_buffer_new(GstVaapiVideoMeta *meta)
{
    g_return_val_if_fail(meta != NULL, NULL);

    return new_vbuffer(gst_vaapi_video_meta_ref(meta));
}

GstBuffer *
gst_vaapi_video_buffer_new_from_pool(GstVaapiVideoPool *pool)
{
    return new_vbuffer(gst_vaapi_video_meta_new_from_pool(pool));
}

GstBuffer *
gst_vaapi_video_buffer_new_from_buffer(GstBuffer *buffer)
{
    GstVaapiVideoMeta * const meta = gst_buffer_get_vaapi_video_meta(buffer);

    return meta ? new_vbuffer(gst_vaapi_video_meta_ref(meta)) : NULL;
}

GstBuffer *
gst_vaapi_video_buffer_new_with_image(GstVaapiImage *image)
{
    return new_vbuffer(gst_vaapi_video_meta_new_with_image(image));
}

GstBuffer *
gst_vaapi_video_buffer_new_with_surface(GstVaapiSurface *surface)
{
    return new_vbuffer(gst_vaapi_video_meta_new_with_surface(surface));
}

GstBuffer *
gst_vaapi_video_buffer_new_with_surface_proxy(GstVaapiSurfaceProxy *proxy)
{
    return new_vbuffer(gst_vaapi_video_meta_new_with_surface_proxy(proxy));
}
