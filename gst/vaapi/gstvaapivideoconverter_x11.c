/*
 *  gstvaapivideoconverter_x11.h - VA video converter to X11 pixmap
 *
 *  Copyright (C) 2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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
#include <gst/vaapi/gstvaapipixmap_x11.h>
#include "gstvaapivideoconverter_x11.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideometa.h"

#if GST_CHECK_VERSION(1,0,0)
typedef gboolean (*GstSurfaceUploadFunction) (GstSurfaceConverter *,
    GstBuffer *);
#else
typedef gboolean (*GstSurfaceUploadFunction) (GstSurfaceConverter *,
    GstSurfaceBuffer *);
#endif

static void
gst_vaapi_video_converter_x11_iface_init (GstSurfaceConverterInterface * iface);

G_DEFINE_TYPE_WITH_CODE (GstVaapiVideoConverterX11,
    gst_vaapi_video_converter_x11,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_SURFACE_CONVERTER,
        gst_vaapi_video_converter_x11_iface_init));

#define GST_VAAPI_VIDEO_CONVERTER_X11_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_VAAPI_TYPE_VIDEO_CONVERTER_X11, \
      GstVaapiVideoConverterX11Private))

struct _GstVaapiVideoConverterX11Private
{
  GstVaapiPixmap *pixmap;
  XID pixmap_id;
};

static gboolean
gst_vaapi_video_converter_x11_upload (GstSurfaceConverter * self,
    GstBuffer * buffer);

static void
gst_vaapi_video_converter_x11_dispose (GObject * object)
{
  GstVaapiVideoConverterX11Private *const priv =
      GST_VAAPI_VIDEO_CONVERTER_X11 (object)->priv;

  gst_vaapi_pixmap_replace (&priv->pixmap, NULL);

  G_OBJECT_CLASS (gst_vaapi_video_converter_x11_parent_class)->dispose (object);
}

static void
gst_vaapi_video_converter_x11_class_init (GstVaapiVideoConverterX11Class *
    klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstVaapiVideoConverterX11Private));

  object_class->dispose = gst_vaapi_video_converter_x11_dispose;
}

static void
gst_vaapi_video_converter_x11_init (GstVaapiVideoConverterX11 * buffer)
{
  buffer->priv = GST_VAAPI_VIDEO_CONVERTER_X11_GET_PRIVATE (buffer);
}

static void
gst_vaapi_video_converter_x11_iface_init (GstSurfaceConverterInterface * iface)
{
  iface->upload = (GstSurfaceUploadFunction)
      gst_vaapi_video_converter_x11_upload;
}

static gboolean
set_pixmap (GstVaapiVideoConverterX11 * converter, GstVaapiDisplay * display,
    XID pixmap_id)
{
  GstVaapiVideoConverterX11Private *const priv = converter->priv;
  GstVaapiPixmap *pixmap;

  pixmap = gst_vaapi_pixmap_x11_new_with_xid (display, pixmap_id);
  if (!pixmap)
    return FALSE;

  gst_vaapi_pixmap_replace (&priv->pixmap, pixmap);
  gst_vaapi_pixmap_unref (pixmap);
  priv->pixmap_id = pixmap_id;
  return TRUE;
}

/**
 * gst_vaapi_video_converter_x11_new:
 * @surface: the #GstSurfaceBuffer
 * @type: type of the target buffer (must be "x11-pixmap")
 * @dest: target of the conversion (must be an X11 pixmap id)
 *
 * Creates an empty #GstBuffer. The caller is responsible for
 * completing the initialization of the buffer with the
 * gst_vaapi_video_converter_x11_set_*() functions.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL on error
 */
GstSurfaceConverter *
gst_vaapi_video_converter_x11_new (GstBuffer * buffer, const gchar * type,
    GValue * dest)
{
  GstVaapiVideoMeta *const meta = gst_buffer_get_vaapi_video_meta (buffer);
  GstVaapiVideoConverterX11 *converter;

  /* We only support X11 pixmap conversion */
  if (strcmp (type, "x11-pixmap") != 0 || !G_VALUE_HOLDS_UINT (dest))
    return NULL;

  converter = g_object_new (GST_VAAPI_TYPE_VIDEO_CONVERTER_X11, NULL);
  if (!converter)
    return NULL;

  if (!set_pixmap (converter, gst_vaapi_video_meta_get_display (meta),
          g_value_get_uint (dest)))
    goto error;
  return GST_SURFACE_CONVERTER (converter);

error:
  g_object_unref (converter);
  return NULL;
}

gboolean
gst_vaapi_video_converter_x11_upload (GstSurfaceConverter * self,
    GstBuffer * buffer)
{
  GstVaapiVideoConverterX11 *const converter =
      GST_VAAPI_VIDEO_CONVERTER_X11 (self);
  GstVaapiVideoConverterX11Private *const priv = converter->priv;
  GstVaapiVideoMeta *const meta = gst_buffer_get_vaapi_video_meta (buffer);
  const GstVaapiRectangle *crop_rect = NULL;
  GstVaapiSurface *surface;
  GstVaapiDisplay *old_display, *new_display;

  g_return_val_if_fail (meta != NULL, FALSE);

  surface = gst_vaapi_video_meta_get_surface (meta);
  if (!surface)
    return FALSE;

  old_display = gst_vaapi_object_get_display (GST_VAAPI_OBJECT (priv->pixmap));
  new_display = gst_vaapi_object_get_display (GST_VAAPI_OBJECT (surface));

  if (old_display != new_display) {
    if (!set_pixmap (converter, new_display, priv->pixmap_id))
      return FALSE;
  }

  if (!gst_vaapi_apply_composition (surface, buffer))
    GST_WARNING ("could not update subtitles");

#if GST_CHECK_VERSION(1,0,0)
  GstVideoCropMeta *const crop_meta = gst_buffer_get_video_crop_meta (buffer);
  if (crop_meta) {
    GstVaapiRectangle crop_rect_tmp;
    crop_rect = &crop_rect_tmp;
    crop_rect_tmp.x = crop_meta->x;
    crop_rect_tmp.y = crop_meta->y;
    crop_rect_tmp.width = crop_meta->width;
    crop_rect_tmp.height = crop_meta->height;
  }
#endif
  if (!crop_rect)
    crop_rect = gst_vaapi_video_meta_get_render_rect (meta);

  return gst_vaapi_pixmap_put_surface (priv->pixmap, surface, crop_rect,
      gst_vaapi_video_meta_get_render_flags (meta));
}
