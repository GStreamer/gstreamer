/*
 *  gstvaapivideoconverter_glx.c - Gst VA video converter
 *
 *  Copyright (C) 2011-2013 Intel Corporation
 *  Copyright (C) 2011 Collabora Ltd.
 *    Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapitexture.h>
#include "gstvaapivideoconverter_glx.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideometa.h"

typedef gboolean (*GstSurfaceUploadFunction)(GstSurfaceConverter *,
    GstSurfaceBuffer *);

static void
gst_vaapi_video_converter_glx_iface_init(GstSurfaceConverterInterface *iface);

G_DEFINE_TYPE_WITH_CODE(
    GstVaapiVideoConverterGLX,
    gst_vaapi_video_converter_glx,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(GST_TYPE_SURFACE_CONVERTER,
                          gst_vaapi_video_converter_glx_iface_init))

#define GST_VAAPI_VIDEO_CONVERTER_GLX_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                 \
        GST_VAAPI_TYPE_VIDEO_CONVERTER_GLX,             \
        GstVaapiVideoConverterGLXPrivate))

struct _GstVaapiVideoConverterGLXPrivate {
    GstVaapiTexture *texture;
};

static gboolean
gst_vaapi_video_converter_glx_upload(GstSurfaceConverter *self,
    GstBuffer *buffer);

static void
gst_vaapi_video_converter_glx_dispose(GObject *object)
{
    GstVaapiVideoConverterGLXPrivate * const priv =
        GST_VAAPI_VIDEO_CONVERTER_GLX(object)->priv;

    g_clear_object(&priv->texture);

    G_OBJECT_CLASS(gst_vaapi_video_converter_glx_parent_class)->dispose(object);
}

static void
gst_vaapi_video_converter_glx_class_init(GstVaapiVideoConverterGLXClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiVideoConverterGLXPrivate));

    object_class->dispose = gst_vaapi_video_converter_glx_dispose;
}

static void
gst_vaapi_video_converter_glx_init(GstVaapiVideoConverterGLX *buffer)
{
    buffer->priv = GST_VAAPI_VIDEO_CONVERTER_GLX_GET_PRIVATE(buffer);
}

static void
gst_vaapi_video_converter_glx_iface_init(GstSurfaceConverterInterface *iface)
{
    iface->upload = (GstSurfaceUploadFunction)
        gst_vaapi_video_converter_glx_upload;
}

/**
 * gst_vaapi_video_converter_glx_new:
 * @surface: the #GstSurfaceBuffer
 * @type: type of the target buffer (must be "opengl")
 * @dest: target of the conversion (must be GL texture id)
 *
 * Creates an empty #GstBuffer. The caller is responsible for
 * completing the initialization of the buffer with the
 * gst_vaapi_video_converter_glx_set_*() functions.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL on error
 */
GstSurfaceConverter *
gst_vaapi_video_converter_glx_new(GstBuffer *buffer, const gchar *type,
    GValue *dest)
{
    GstVaapiVideoMeta * const meta = gst_buffer_get_vaapi_video_meta(buffer);
    GstVaapiTexture *texture;
    GstVaapiVideoConverterGLX *converter;

    /* We only support Open GL texture conversion */
    if (strcmp(type, "opengl") != 0 || !G_VALUE_HOLDS_UINT(dest))
        return NULL;

    /* FIXME Should we assume target and format ? */
    texture = gst_vaapi_texture_new_with_texture(
        gst_vaapi_video_meta_get_display(meta),
        g_value_get_uint(dest), GL_TEXTURE_2D, GL_BGRA);
    if (!texture)
        return NULL;

    converter = g_object_new(GST_VAAPI_TYPE_VIDEO_CONVERTER_GLX, NULL);
    converter->priv->texture = texture;
    return GST_SURFACE_CONVERTER(converter);
}

gboolean
gst_vaapi_video_converter_glx_upload(GstSurfaceConverter *self,
    GstBuffer *buffer)
{
    GstVaapiVideoConverterGLXPrivate * const priv =
        GST_VAAPI_VIDEO_CONVERTER_GLX(self)->priv;
    GstVaapiVideoMeta * const meta = gst_buffer_get_vaapi_video_meta(buffer);
    GstVaapiSurface * const surface = gst_vaapi_video_meta_get_surface(meta);
    GstVaapiDisplay *new_dpy, *old_dpy;

    new_dpy = gst_vaapi_object_get_display(GST_VAAPI_OBJECT(surface));
    old_dpy = gst_vaapi_object_get_display(GST_VAAPI_OBJECT(priv->texture));

    if (old_dpy != new_dpy) {
        const guint texture = gst_vaapi_texture_get_id(priv->texture);

        g_clear_object(&priv->texture);
        priv->texture = gst_vaapi_texture_new_with_texture(new_dpy,
            texture, GL_TEXTURE_2D, GL_BGRA);
    }

    if (!gst_vaapi_apply_composition(surface, buffer))
        GST_WARNING("could not update subtitles");

    return gst_vaapi_texture_put_surface(priv->texture, surface,
        gst_vaapi_video_meta_get_render_flags(meta));
}
