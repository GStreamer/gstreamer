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

#include "gst/vaapi/sysdeps.h"
#include "gst/vaapi/ogl_compat.h"
#include "gstvaapivideometa.h"
#include "gstvaapivideometa_texture.h"
#include "gstvaapipluginutil.h"

#if USE_GLX
#include <gst/vaapi/gstvaapitexture_glx.h>
#endif

#define DEFAULT_FORMAT GST_VIDEO_FORMAT_RGBA

#if GST_CHECK_VERSION(1,1,0) && (USE_GLX || USE_EGL)
struct _GstVaapiVideoMetaTexture
{
  GstVaapiTexture *texture;
  GstVideoGLTextureType texture_type;
  guint gl_format;
  guint width;
  guint height;
};

static gboolean
meta_texture_ensure_format (GstVaapiVideoMetaTexture * meta,
    GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_RGBA:
      meta->gl_format = GL_RGBA;
      meta->texture_type = GST_VIDEO_GL_TEXTURE_TYPE_RGBA;
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

static void
meta_texture_ensure_size_from_buffer (GstVaapiVideoMetaTexture * meta,
    GstBuffer * buffer)
{
  GstVideoMeta *vmeta;

  if (!buffer || !(vmeta = gst_buffer_get_video_meta (buffer))) {
    meta->width = 0;
    meta->height = 0;
  } else {
    meta->width = vmeta->width;
    meta->height = vmeta->height;
  }
}

static void
meta_texture_free (GstVaapiVideoMetaTexture * meta)
{
  if (G_UNLIKELY (!meta))
    return;

  gst_vaapi_texture_replace (&meta->texture, NULL);
  g_slice_free (GstVaapiVideoMetaTexture, meta);
}

static GstVaapiVideoMetaTexture *
meta_texture_new (void)
{
  GstVaapiVideoMetaTexture *meta;

  meta = g_slice_new (GstVaapiVideoMetaTexture);
  if (!meta)
    return NULL;

  meta->texture = NULL;
  meta_texture_ensure_format (meta, DEFAULT_FORMAT);
  meta_texture_ensure_size_from_buffer (meta, NULL);
  return meta;
}

static GstVaapiVideoMetaTexture *
meta_texture_copy (GstVaapiVideoMetaTexture * meta)
{
  GstVaapiVideoMetaTexture *copy;

  copy = meta_texture_new ();
  if (!copy)
    return NULL;

  copy->texture_type = meta->texture_type;
  copy->gl_format = meta->gl_format;
  copy->width = meta->width;
  copy->height = meta->height;
  gst_vaapi_texture_replace (&copy->texture, meta->texture);
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
  GstVaapiDisplay *const dpy = GST_VAAPI_OBJECT_DISPLAY (surface);

  if (!gst_vaapi_display_has_opengl (dpy))
    return FALSE;

  if (!meta_texture->texture ||
      /* Check whether VA display changed */
      GST_VAAPI_OBJECT_DISPLAY (meta_texture->texture) != dpy ||
      /* Check whether texture id changed */
      gst_vaapi_texture_get_id (meta_texture->texture) != texture_id[0]) {
    /* FIXME: should we assume target? */
    GstVaapiTexture *const texture =
        gst_vaapi_texture_new_wrapped (dpy, texture_id[0],
        GL_TEXTURE_2D, meta_texture->gl_format, meta_texture->width,
        meta_texture->height);
    gst_vaapi_texture_replace (&meta_texture->texture, texture);
    if (!texture)
      return FALSE;
    gst_vaapi_texture_unref (texture);
  }
  return gst_vaapi_texture_put_surface (meta_texture->texture, surface,
      gst_vaapi_surface_proxy_get_crop_rect (proxy),
      gst_vaapi_video_meta_get_render_flags (vmeta));
}

gboolean
gst_buffer_add_texture_upload_meta (GstBuffer * buffer)
{
  GstVideoGLTextureUploadMeta *meta = NULL;
  GstVaapiVideoMetaTexture *meta_texture;

  if (!buffer)
    return FALSE;

  meta_texture = meta_texture_new ();
  if (!meta_texture)
    return FALSE;

  meta_texture_ensure_size_from_buffer (meta_texture, buffer);
  meta = gst_buffer_add_video_gl_texture_upload_meta (buffer,
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL,
      1, &meta_texture->texture_type, gst_vaapi_texture_upload,
      meta_texture, (GBoxedCopyFunc) meta_texture_copy,
      (GBoxedFreeFunc) meta_texture_free);
  if (!meta)
    goto error;
  return TRUE;

error:
  meta_texture_free (meta_texture);
  return FALSE;
}

gboolean
gst_buffer_ensure_texture_upload_meta (GstBuffer * buffer)
{
  GstVideoGLTextureUploadMeta *const meta =
      gst_buffer_get_video_gl_texture_upload_meta (buffer);

  if (meta) {
    meta_texture_ensure_size_from_buffer (meta->user_data, buffer);
    return TRUE;
  }
  return gst_buffer_add_texture_upload_meta (buffer);
}
#endif
