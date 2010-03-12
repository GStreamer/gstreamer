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

#define DEBUG 1
#include "vaapi_debug.h"

G_DEFINE_TYPE(GstVaapiVideoBuffer, gst_vaapi_video_buffer, GST_TYPE_BUFFER);

#define GST_VAAPI_VIDEO_BUFFER_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_VIDEO_BUFFER,	\
                                 GstVaapiVideoBufferPrivate))

struct _GstVaapiVideoBufferPrivate {
    GstVaapiVideoPool  *pool;
    guint               pool_type;
    GstVaapiImage      *image;
    GstVaapiSurface    *surface;
    guint               flags;
};

enum {
    POOL_TYPE_NONE,
    POOL_TYPE_IMAGE,
    POOL_TYPE_SURFACE
};

static void
gst_vaapi_video_buffer_finalize(GstMiniObject *object)
{
    GstVaapiVideoBufferPrivate *priv = GST_VAAPI_VIDEO_BUFFER(object)->priv;
    GstMiniObjectClass *parent_class;

    if (priv->image) {
        if (priv->pool_type == POOL_TYPE_IMAGE)
            gst_vaapi_video_pool_put_object(priv->pool, priv->image);
        else
            g_object_unref(priv->image);
        priv->image = NULL;
    }

    if (priv->surface) {
        if (priv->pool_type == POOL_TYPE_SURFACE)
            gst_vaapi_video_pool_put_object(priv->pool, priv->surface);
        else
            g_object_unref(priv->surface);
        priv->surface = NULL;
    }

    if (priv->pool) {
        g_object_unref(priv->pool);
        priv->pool = NULL;
    }

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
    priv->pool          = NULL;
    priv->image         = NULL;
    priv->surface       = NULL;
}

static GstBuffer *
gst_vaapi_video_buffer_new(
    GstVaapiVideoPool  *pool,
    GstVaapiImage      *image,
    GstVaapiSurface    *surface
)
{
    gpointer vobject = NULL;
    GstMiniObject *object;
    GstVaapiVideoBuffer *buffer;
    GstVaapiVideoBufferPrivate *priv;

    object = gst_mini_object_new(GST_VAAPI_TYPE_VIDEO_BUFFER);
    if (!object)
        return NULL;

    buffer              = GST_VAAPI_VIDEO_BUFFER(object);
    priv                = buffer->priv;
    priv->pool          = pool;
    priv->pool_type     = POOL_TYPE_NONE;
    priv->image         = image;
    priv->surface       = surface;

    if (pool) {
        vobject = gst_vaapi_video_pool_get_object(pool);
        if (!vobject)
            goto error;

        if (GST_VAAPI_IS_IMAGE(vobject)) {
            priv->pool_type = POOL_TYPE_IMAGE;
            priv->image     = vobject;
        }
        else if (GST_VAAPI_IS_SURFACE(vobject)) {
            priv->pool_type = POOL_TYPE_SURFACE;
            priv->surface   = vobject;
        }
        else
            goto error;
    }
    return GST_BUFFER(buffer);

error:
    if (vobject)
        gst_vaapi_video_pool_put_object(pool, vobject);
    gst_mini_object_unref(object);
    return NULL;
}

GstBuffer *
gst_vaapi_video_buffer_new_from_pool(GstVaapiVideoPool *pool)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    return gst_vaapi_video_buffer_new(g_object_ref(pool), NULL, NULL);
}

GstBuffer *
gst_vaapi_video_buffer_new_with_image(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), NULL);

    return gst_vaapi_video_buffer_new(NULL, g_object_ref(image), NULL);
}

GstBuffer *
gst_vaapi_video_buffer_new_with_surface(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    return gst_vaapi_video_buffer_new(NULL, NULL, g_object_ref(surface));
}

GstVaapiImage *
gst_vaapi_video_buffer_get_image(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), NULL);

    return buffer->priv->image;
}

GstVaapiSurface *
gst_vaapi_video_buffer_get_surface(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), NULL);

    return buffer->priv->surface;
}
