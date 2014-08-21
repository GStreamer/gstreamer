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
#include "gstvaapivideometa.h"
#include "gstvaapivideometa_texture.h"
#include "gstvaapipluginutil.h"

#if GST_CHECK_VERSION(1,1,0) && USE_GLX
struct _GstVaapiVideoMetaTexture
{
  GstVaapiTexture *texture;
};

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
  return meta;
}

static GstVaapiVideoMetaTexture *
meta_texture_copy (GstVaapiVideoMetaTexture * meta)
{
  GstVaapiVideoMetaTexture *copy;

  copy = meta_texture_new ();
  if (!copy)
    return NULL;

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
  GstVaapiSurface *const surface = gst_vaapi_video_meta_get_surface (vmeta);
  GstVaapiDisplay *const dpy = GST_VAAPI_OBJECT_DISPLAY (surface);

  if (gst_vaapi_display_get_display_type (dpy) != GST_VAAPI_DISPLAY_TYPE_GLX)
    return FALSE;

  if (!meta_texture->texture ||
      /* Check whether VA display changed */
      GST_VAAPI_OBJECT_DISPLAY (meta_texture->texture) != dpy ||
      /* Check whether texture id changed */
      gst_vaapi_texture_get_id (meta_texture->texture) != texture_id[0]) {
    /* FIXME: should we assume target? */
    GstVaapiTexture *const texture =
        gst_vaapi_texture_new_with_texture (dpy, texture_id[0],
        GL_TEXTURE_2D, GL_RGBA);
    gst_vaapi_texture_replace (&meta_texture->texture, texture);
    if (!texture)
      return FALSE;
    gst_vaapi_texture_unref (texture);
  }
  return gst_vaapi_texture_put_surface (meta_texture->texture, surface,
      gst_vaapi_video_meta_get_render_flags (vmeta));
}

gboolean
gst_buffer_add_texture_upload_meta (GstBuffer * buffer)
{
  GstVideoGLTextureUploadMeta *meta = NULL;
  GstVideoGLTextureType tex_type[] = { GST_VIDEO_GL_TEXTURE_TYPE_RGBA };
  GstVaapiVideoMetaTexture *meta_texture;

  if (!buffer)
    return FALSE;

  meta_texture = meta_texture_new ();
  if (!meta_texture)
    return FALSE;

  meta = gst_buffer_add_video_gl_texture_upload_meta (buffer,
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL,
      1, tex_type, gst_vaapi_texture_upload,
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
  return gst_buffer_get_video_gl_texture_upload_meta (buffer) ||
      gst_buffer_add_texture_upload_meta (buffer);
}
#endif
