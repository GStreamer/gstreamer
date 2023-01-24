/*
 *  gstvaapivideometa_texture.c - GStreamer/VA video meta (GLTextureUpload)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *  Copyright (C) 2013 Igalia
 *    Author: Víctor Manuel Jáquez Leal <vjaquez@igalia.com>
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

#include "gstcompat.h"
#include "gst/vaapi/ogl_compat.h"
#include "gstvaapivideometa.h"
#include "gstvaapivideometa_texture.h"
#include "gstvaapipluginutil.h"

#if GST_VAAPI_USE_GLX
#include <gst/vaapi/gstvaapitexture_glx.h>
#endif

#define DEFAULT_FORMAT GST_VIDEO_FORMAT_RGBA

#if (GST_VAAPI_USE_GLX || GST_VAAPI_USE_EGL)
struct _GstVaapiVideoMetaTexture
{
  GstVaapiTexture *texture;
  GstVideoGLTextureType texture_type[4];
  guint gl_format;
  guint width;
  guint height;
};

static guint
get_texture_orientation_flags (GstVideoGLTextureOrientation orientation)
{
  guint flags;

  switch (orientation) {
    case GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP:
      flags = GST_VAAPI_TEXTURE_ORIENTATION_FLAG_Y_INVERTED;
      break;
    case GST_VIDEO_GL_TEXTURE_ORIENTATION_X_FLIP_Y_NORMAL:
      flags = GST_VAAPI_TEXTURE_ORIENTATION_FLAG_X_INVERTED;
      break;
    case GST_VIDEO_GL_TEXTURE_ORIENTATION_X_FLIP_Y_FLIP:
      flags = GST_VAAPI_TEXTURE_ORIENTATION_FLAG_X_INVERTED |
          GST_VAAPI_TEXTURE_ORIENTATION_FLAG_Y_INVERTED;
      break;
    default:
      flags = 0;
      break;
  }
  return flags;
}

static gboolean
meta_texture_ensure_format (GstVaapiVideoMetaTexture * meta,
    GstVideoFormat format)
{
  memset (meta->texture_type, 0, sizeof (meta->texture_type));

  switch (format) {
    case GST_VIDEO_FORMAT_RGBA:
      meta->gl_format = GL_RGBA;
      meta->texture_type[0] = GST_VIDEO_GL_TEXTURE_TYPE_RGBA;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      meta->gl_format = GL_BGRA_EXT;
      /* FIXME: add GST_VIDEO_GL_TEXTURE_TYPE_BGRA extension */
      meta->texture_type[0] = GST_VIDEO_GL_TEXTURE_TYPE_RGBA;
      break;
    default:
      goto error_unsupported_format;
  }
  return TRUE;

  /* ERRORS */
error_unsupported_format:
  GST_ERROR ("unsupported texture format %s",
      gst_video_format_to_string (format));
  return FALSE;
}

static gboolean
meta_texture_ensure_info_from_buffer (GstVaapiVideoMetaTexture * meta,
    GstBuffer * buffer)
{
  GstVideoMeta *vmeta;
  GstVideoFormat format;

  if (!buffer || !(vmeta = gst_buffer_get_video_meta (buffer))) {
    format = DEFAULT_FORMAT;
    meta->width = 0;
    meta->height = 0;
  } else {
    const GstVideoFormatInfo *const fmt_info =
        gst_video_format_get_info (vmeta->format);
    format = (fmt_info && GST_VIDEO_FORMAT_INFO_IS_RGB (fmt_info)) ?
        vmeta->format : DEFAULT_FORMAT;
    meta->width = vmeta->width;
    meta->height = vmeta->height;
  }
  return meta_texture_ensure_format (meta, format);
}

static void
meta_texture_free (GstVaapiVideoMetaTexture * meta)
{
  if (G_UNLIKELY (!meta))
    return;

  gst_mini_object_replace ((GstMiniObject **) & meta->texture, NULL);
  g_free (meta);
}

static GstVaapiVideoMetaTexture *
meta_texture_new (void)
{
  GstVaapiVideoMetaTexture *meta;

  meta = g_new (GstVaapiVideoMetaTexture, 1);
  if (!meta)
    return NULL;

  meta->texture = NULL;
  if (!meta_texture_ensure_info_from_buffer (meta, NULL))
    goto error;
  return meta;

  /* ERRORS */
error:
  {
    meta_texture_free (meta);
    return NULL;
  }
}

static GstVaapiVideoMetaTexture *
meta_texture_copy (GstVaapiVideoMetaTexture * meta)
{
  GstVaapiVideoMetaTexture *copy;

  copy = meta_texture_new ();
  if (!copy)
    return NULL;

  memcpy (copy->texture_type, meta->texture_type, sizeof (meta->texture_type));
  copy->gl_format = meta->gl_format;
  copy->width = meta->width;
  copy->height = meta->height;

  gst_mini_object_replace ((GstMiniObject **) & copy->texture,
      (GstMiniObject *) meta->texture);
  return copy;
}

static gboolean
gst_vaapi_texture_upload (GstVideoGLTextureUploadMeta * meta,
    guint texture_id[4])
{
  GstVaapiVideoMeta *const vmeta =
      gst_buffer_get_vaapi_video_meta (meta->buffer);
  GstVaapiVideoMetaTexture *const meta_texture = meta->user_data;
  GstVaapiSurfaceProxy *const proxy =
      gst_vaapi_video_meta_get_surface_proxy (vmeta);
  GstVaapiSurface *const surface = gst_vaapi_surface_proxy_get_surface (proxy);
  GstVaapiDisplay *const dpy = gst_vaapi_surface_get_display (surface);
  GstVaapiTexture *texture = NULL;

  if (!gst_vaapi_display_has_opengl (dpy))
    return FALSE;

  if (meta_texture->texture
      /* Check whether VA display changed */
      && GST_VAAPI_TEXTURE_DISPLAY (meta_texture->texture) == dpy
      /* Check whether texture id changed */
      && (gst_vaapi_texture_get_id (meta_texture->texture) == texture_id[0])) {
    texture = meta_texture->texture;
  }

  if (!texture) {
    /* FIXME: should we assume target? */
    texture =
        gst_vaapi_texture_new_wrapped (dpy, texture_id[0],
        GL_TEXTURE_2D, meta_texture->gl_format, meta_texture->width,
        meta_texture->height);
  }

  if (meta_texture->texture != texture) {
    gst_mini_object_replace ((GstMiniObject **) & meta_texture->texture,
        (GstMiniObject *) texture);
  }

  if (!texture)
    return FALSE;

  gst_vaapi_texture_set_orientation_flags (meta_texture->texture,
      get_texture_orientation_flags (meta->texture_orientation));

  return gst_vaapi_texture_put_surface (meta_texture->texture, surface,
      gst_vaapi_surface_proxy_get_crop_rect (proxy),
      gst_vaapi_video_meta_get_render_flags (vmeta));
}

GstMeta *
gst_buffer_add_texture_upload_meta (GstBuffer * buffer)
{
  GstVaapiVideoMetaTexture *meta_texture;

  if (!buffer)
    return FALSE;

  meta_texture = meta_texture_new ();
  if (!meta_texture)
    return FALSE;

  if (!meta_texture_ensure_info_from_buffer (meta_texture, buffer))
    goto error;

  return (GstMeta *) gst_buffer_add_video_gl_texture_upload_meta (buffer,
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL, 1,
      meta_texture->texture_type, gst_vaapi_texture_upload, meta_texture,
      (GBoxedCopyFunc) meta_texture_copy, (GBoxedFreeFunc) meta_texture_free);

  /* ERRORS */
error:
  {
    meta_texture_free (meta_texture);
    return NULL;
  }
}

gboolean
gst_buffer_ensure_texture_upload_meta (GstBuffer * buffer)
{
  GstVideoGLTextureUploadMeta *const meta =
      gst_buffer_get_video_gl_texture_upload_meta (buffer);

  return meta ?
      meta_texture_ensure_info_from_buffer (meta->user_data, buffer) :
      (gst_buffer_add_texture_upload_meta (buffer) != NULL);
}
#endif
