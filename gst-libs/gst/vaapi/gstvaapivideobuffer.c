/*
 *  gstvaapivideobuffer.c - Gst VA video buffer
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
#include "gstvaapivideobuffer.h"
#include <gst/vaapi/gstvaapiimagepool.h>
#include <gst/vaapi/gstvaapisurfacepool.h>

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiVideoBuffer, gst_vaapi_video_buffer, GST_TYPE_BUFFER);

#define GST_VAAPI_VIDEO_BUFFER_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_VIDEO_BUFFER,	\
                                 GstVaapiVideoBufferPrivate))

struct _GstVaapiVideoBufferPrivate {
    GstVaapiVideoPool  *image_pool;
    GstVaapiImage      *image;
    GstVaapiVideoPool  *surface_pool;
    GstVaapiSurface    *surface;
    guint               flags;
};

static void
gst_vaapi_video_buffer_destroy_image(GstVaapiVideoBuffer *buffer)
{
    GstVaapiVideoBufferPrivate * const priv = buffer->priv;

    if (priv->image) {
        if (priv->image_pool)
            gst_vaapi_video_pool_put_object(priv->image_pool, priv->image);
        else
            g_object_unref(priv->image);
        priv->image = NULL;
    }

    if (priv->image_pool) {
        g_object_unref(priv->image_pool);
        priv->image_pool = NULL;
    }
}

static void
gst_vaapi_video_buffer_destroy_surface(GstVaapiVideoBuffer *buffer)
{
    GstVaapiVideoBufferPrivate * const priv = buffer->priv;

    if (priv->surface) {
        if (priv->surface_pool)
            gst_vaapi_video_pool_put_object(priv->surface_pool, priv->surface);
        else
            g_object_unref(priv->surface);
        priv->surface = NULL;
    }

    if (priv->surface_pool) {
        g_object_unref(priv->surface_pool);
        priv->surface_pool = NULL;
    }
}

static void
gst_vaapi_video_buffer_finalize(GstMiniObject *object)
{
    GstVaapiVideoBuffer * const buffer = GST_VAAPI_VIDEO_BUFFER(object);
    GstMiniObjectClass *parent_class;

    gst_vaapi_video_buffer_destroy_image(buffer);
    gst_vaapi_video_buffer_destroy_surface(buffer);

    parent_class = GST_MINI_OBJECT_CLASS(gst_vaapi_video_buffer_parent_class);
    if (parent_class->finalize)
        parent_class->finalize(object);
}

static void
gst_vaapi_video_buffer_class_init(GstVaapiVideoBufferClass *klass)
{
    GstMiniObjectClass * const object_class = GST_MINI_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiVideoBufferPrivate));

    object_class->finalize = gst_vaapi_video_buffer_finalize;
}

static void
gst_vaapi_video_buffer_init(GstVaapiVideoBuffer *buffer)
{
    GstVaapiVideoBufferPrivate *priv;

    priv                = GST_VAAPI_VIDEO_BUFFER_GET_PRIVATE(buffer);
    buffer->priv        = priv;
    priv->image_pool    = NULL;
    priv->image         = NULL;
    priv->surface_pool  = NULL;
    priv->surface       = NULL;
}

static inline GstVaapiVideoBuffer *gst_vaapi_video_buffer_new(void)
{
    GstMiniObject *object;

    object = gst_mini_object_new(GST_VAAPI_TYPE_VIDEO_BUFFER);
    if (!object)
        return NULL;

    return GST_VAAPI_VIDEO_BUFFER(object);
}

GstBuffer *
gst_vaapi_video_buffer_new_from_pool(GstVaapiVideoPool *pool)
{
    GstVaapiVideoBuffer *buffer;
    gboolean is_image_pool, is_surface_pool;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    is_image_pool   = GST_VAAPI_IS_IMAGE_POOL(pool);
    is_surface_pool = GST_VAAPI_IS_SURFACE_POOL(pool);

    if (!is_image_pool && !is_surface_pool)
        return NULL;

    buffer = gst_vaapi_video_buffer_new();
    if (buffer &&
        ((is_image_pool &&
          gst_vaapi_video_buffer_set_image_from_pool(buffer, pool)) ||
         (is_surface_pool &&
          gst_vaapi_video_buffer_set_surface_from_pool(buffer, pool))))
        return GST_BUFFER(buffer);

    gst_mini_object_unref(GST_MINI_OBJECT(buffer));
    return NULL;
}

GstBuffer *
gst_vaapi_video_buffer_new_with_image(GstVaapiImage *image)
{
    GstVaapiVideoBuffer *buffer;

    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), NULL);

    buffer = gst_vaapi_video_buffer_new();
    if (buffer)
        gst_vaapi_video_buffer_set_image(buffer, image);
    return GST_BUFFER(buffer);
}

GstBuffer *
gst_vaapi_video_buffer_new_with_surface(GstVaapiSurface *surface)
{
    GstVaapiVideoBuffer *buffer;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    buffer = gst_vaapi_video_buffer_new();
    if (buffer)
        gst_vaapi_video_buffer_set_surface(buffer, surface);
    return GST_BUFFER(buffer);
}

GstVaapiImage *
gst_vaapi_video_buffer_get_image(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), NULL);

    return buffer->priv->image;
}

void
gst_vaapi_video_buffer_set_image(
    GstVaapiVideoBuffer *buffer,
    GstVaapiImage       *image
)
{
    g_return_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer));
    g_return_if_fail(GST_VAAPI_IS_IMAGE(image));

    gst_vaapi_video_buffer_destroy_image(buffer);

    if (image)
        buffer->priv->image = g_object_ref(image);
}

gboolean
gst_vaapi_video_buffer_set_image_from_pool(
    GstVaapiVideoBuffer *buffer,
    GstVaapiVideoPool   *pool
)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE_POOL(pool), FALSE);

    gst_vaapi_video_buffer_destroy_image(buffer);

    if (pool) {
        buffer->priv->image = gst_vaapi_video_pool_get_object(pool);
        if (!buffer->priv->image)
            return FALSE;
        buffer->priv->image_pool = g_object_ref(pool);
    }
    return TRUE;
}

GstVaapiSurface *
gst_vaapi_video_buffer_get_surface(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), NULL);

    return buffer->priv->surface;
}

void
gst_vaapi_video_buffer_set_surface(
    GstVaapiVideoBuffer *buffer,
    GstVaapiSurface     *surface
)
{
    g_return_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer));
    g_return_if_fail(GST_VAAPI_IS_SURFACE(surface));

    gst_vaapi_video_buffer_destroy_surface(buffer);

    if (surface)
        buffer->priv->surface = g_object_ref(surface);
}

gboolean
gst_vaapi_video_buffer_set_surface_from_pool(
    GstVaapiVideoBuffer *buffer,
    GstVaapiVideoPool   *pool
)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_POOL(pool), FALSE);

    gst_vaapi_video_buffer_destroy_surface(buffer);

    if (pool) {
        buffer->priv->surface = gst_vaapi_video_pool_get_object(pool);
        if (!buffer->priv->surface)
            return FALSE;
        buffer->priv->surface_pool = g_object_ref(pool);
    }
    return TRUE;
}
