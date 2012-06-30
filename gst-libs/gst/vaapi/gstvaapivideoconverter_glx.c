/*
 *  gstvaapivideoconverter_glx.c - Gst VA video converter
 *
 *  Copyright (C) 2011 Intel Corporation
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

#include "sysdeps.h"
#include <string.h>
#include "gstvaapivideoconverter_glx.h"
#include "gstvaapivideobuffer.h"
#include "gstvaapitexture.h"

static void gst_vaapi_video_converter_glx_iface_init (GstSurfaceConverterInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GstVaapiVideoConverterGLX, gst_vaapi_video_converter_glx,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GST_TYPE_SURFACE_CONVERTER,
                                                gst_vaapi_video_converter_glx_iface_init));

struct _GstVaapiVideoConverterGLXPrivate {
    GstVaapiTexture *texture;
};

static void
gst_vaapi_video_converter_glx_dispose(GObject *object)
{
    GstVaapiVideoConverterGLXPrivate *priv =
      GST_VAAPI_VIDEO_CONVERTER_GLX (object)->priv;

    g_clear_object(&priv->texture);

    G_OBJECT_CLASS (gst_vaapi_video_converter_glx_parent_class)->dispose (object);
}

static void
gst_vaapi_video_converter_glx_class_init(GstVaapiVideoConverterGLXClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (GstVaapiVideoConverterGLXPrivate));
    object_class->dispose = gst_vaapi_video_converter_glx_dispose;
}

static void
gst_vaapi_video_converter_glx_init(GstVaapiVideoConverterGLX *buffer)
{
    buffer->priv = G_TYPE_INSTANCE_GET_PRIVATE(buffer,
                                               GST_VAAPI_TYPE_VIDEO_CONVERTER,
                                               GstVaapiVideoConverterGLXPrivate);
}

static void
gst_vaapi_video_converter_glx_iface_init (GstSurfaceConverterInterface *iface) {
  iface->upload = gst_vaapi_video_converter_glx_upload;
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
gst_vaapi_video_converter_glx_new(GstSurfaceBuffer *surface, const gchar *type,
    GValue *dest)
{
    GstVaapiVideoBuffer *buffer = GST_VAAPI_VIDEO_BUFFER (surface);
    GstVaapiDisplay *display = gst_vaapi_video_buffer_get_display (buffer);
    GstVaapiTexture *texture;
    GstVaapiVideoConverterGLX *converter = NULL;

    /* We only support Open GL texture conversion */
    if (strcmp(type, "opengl") || !G_VALUE_HOLDS_UINT (dest))
        return NULL;

    /* FIXME Should we assume target and format ? */
    texture = gst_vaapi_texture_new_with_texture (display,
                                                  g_value_get_uint (dest),
                                                  GL_TEXTURE_2D,
                                                  GL_BGRA);

    if (texture) {
      converter = g_object_new (GST_VAAPI_TYPE_VIDEO_CONVERTER, NULL);
      converter->priv->texture = texture;
    }

    return GST_SURFACE_CONVERTER (converter);
}

gboolean
gst_vaapi_video_converter_glx_upload (GstSurfaceConverter *converter,
    GstSurfaceBuffer *buffer)
{
  GstVaapiVideoConverterGLXPrivate *priv =
    GST_VAAPI_VIDEO_CONVERTER_GLX (converter)->priv;
  GstVaapiVideoBuffer * const vbuffer = GST_VAAPI_VIDEO_BUFFER (buffer);
  GstVaapiSurface *surface = gst_vaapi_video_buffer_get_surface (vbuffer);
  GstVaapiDisplay *new_dpy, *old_dpy;
  GstVideoOverlayComposition * const composition =
    gst_video_buffer_get_overlay_composition (GST_BUFFER (buffer));

  new_dpy = gst_vaapi_object_get_display (GST_VAAPI_OBJECT (surface));
  old_dpy = gst_vaapi_object_get_display (GST_VAAPI_OBJECT (priv->texture));

  if (old_dpy != new_dpy) {
    guint texture = gst_vaapi_texture_get_id (priv->texture);
    g_object_unref (priv->texture);
    priv->texture = gst_vaapi_texture_new_with_texture (new_dpy,
                                                        texture,
                                                        GL_TEXTURE_2D,
                                                        GL_BGRA);
  }

  if (!gst_vaapi_surface_set_subpictures_from_composition (surface,
           composition, TRUE))
        GST_WARNING ("could not update subtitles");

  return gst_vaapi_texture_put_surface (priv->texture, surface,
      gst_vaapi_video_buffer_get_render_flags (vbuffer));
}
