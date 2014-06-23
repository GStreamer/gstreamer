/*
 * GStreamer
 * Copyright (C) 2012-2014 Matthew Waters <ystree00@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "gl.h"
#include "gstgluploadmeta.h"

/**
 * SECTION:gstgluploadmeta
 * @short_description: an object that provides #GstVideoGLTextureUploadMeta
 * @see_also: #GstGLUpload, #GstGLMemory
 *
 * #GstGLUploadMeta is an object that uploads data from system memory into GL textures.
 *
 * A #GstGLUpload can be created with gst_gl_upload_new()
 */

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

/* *INDENT-OFF* */

struct _GstGLUploadMetaPrivate
{
  GstBuffer *buffer;
  gboolean initted;

  GstGLMemory *in_tex[GST_VIDEO_MAX_PLANES];
  GstGLMemory *out_tex[GST_VIDEO_MAX_PLANES];
};

GST_DEBUG_CATEGORY_STATIC (gst_gl_upload_meta_debug);
#define GST_CAT_DEFAULT gst_gl_upload_meta_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_upload_meta_debug, "gluploadmeta", 0, "uploadmeta");

G_DEFINE_TYPE_WITH_CODE (GstGLUploadMeta, gst_gl_upload_meta, GST_TYPE_OBJECT, DEBUG_INIT);
static void gst_gl_upload_meta_finalize (GObject * object);
static void gst_gl_upload_meta_reset (GstGLUploadMeta * upload);

#define GST_GL_UPLOAD_META_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_UPLOAD_META, GstGLUploadMetaPrivate))

static void
gst_gl_upload_meta_class_init (GstGLUploadMetaClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLUploadMetaPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_upload_meta_finalize;
}

static void
gst_gl_upload_meta_init (GstGLUploadMeta * upload)
{
  upload->priv = GST_GL_UPLOAD_META_GET_PRIVATE (upload);

  upload->context = NULL;

  gst_video_info_set_format (&upload->info, GST_VIDEO_FORMAT_ENCODED, 0, 0);
}

/**
 * gst_gl_upload_meta_new:
 * @context: a #GstGLContext
 *
 * Returns: a new #GstGLUploadMeta object
 */
GstGLUploadMeta *
gst_gl_upload_meta_new (GstGLContext * context)
{
  GstGLUploadMeta *upload;

  upload = g_object_new (GST_TYPE_GL_UPLOAD_META, NULL);

  upload->context = gst_object_ref (context);

  return upload;
}

static void
gst_gl_upload_meta_finalize (GObject * object)
{
  GstGLUploadMeta *upload;

  upload = GST_GL_UPLOAD_META (object);

  gst_gl_upload_meta_reset (upload);

  if (upload->context) {
    gst_object_unref (upload->context);
    upload->context = NULL;
  }

  G_OBJECT_CLASS (gst_gl_upload_meta_parent_class)->finalize (object);
}

static void
gst_gl_upload_meta_reset (GstGLUploadMeta * upload)
{
  guint i;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (upload->priv->out_tex[i]) {
      gst_memory_unref ((GstMemory *) upload->priv->out_tex[i]);
      upload->priv->out_tex[i] = NULL;
    }
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (upload->priv->in_tex[i]) {
      gst_memory_unref ((GstMemory *) upload->priv->in_tex[i]);
      upload->priv->in_tex[i] = NULL;
    }
  }
}

static void
_gst_gl_upload_meta_set_format_unlocked (GstGLUploadMeta * upload,
    GstVideoInfo *info)
{
  g_return_if_fail (upload != NULL);
  g_return_if_fail (GST_VIDEO_INFO_FORMAT (info) !=
      GST_VIDEO_FORMAT_UNKNOWN);
  g_return_if_fail (GST_VIDEO_INFO_FORMAT (info) !=
      GST_VIDEO_FORMAT_ENCODED);

  if (gst_video_info_is_equal (&upload->info, info))
    return;

  gst_gl_upload_meta_reset (upload);
  upload->info = *info;
  upload->priv->initted = FALSE;
}

/**
 * gst_gl_upload_meta_set_format:
 * @upload: a #GstGLUpload
 * @info: input #GstVideoInfo
 *
 * Initializes @upload with the information required for upload.
 */
void
gst_gl_upload_meta_set_format (GstGLUploadMeta * upload, GstVideoInfo * info)
{
  GST_OBJECT_LOCK (upload);
  _gst_gl_upload_meta_set_format_unlocked (upload, info);
  GST_OBJECT_UNLOCK (upload);
}

/**
 * gst_gl_upload_meta_get_format:
 * @upload: a #GstGLUpload
 *
 * Returns: (transfer none): The #GstVideoInfo set by
 * gst_gl_upload_meta_set_format()
 */
GstVideoInfo *
gst_gl_upload_meta_get_format (GstGLUploadMeta * upload)
{
  GstVideoInfo *ret;

  GST_OBJECT_LOCK (upload);
  ret = &upload->info;
  GST_OBJECT_LOCK (upload);

  return ret;
}

static gboolean
_perform_with_gl_memory (GstGLUploadMeta * upload, GstVideoGLTextureUploadMeta *
    meta, guint texture_id[4])
{
  gboolean res = TRUE;
  gint i;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->info); i++) {
    GstGLMemory *in_mem = upload->priv->in_tex[i];

    if (GST_GL_MEMORY_FLAG_IS_SET (in_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD)) {
      GstMapInfo map_info;
      guint tex_id;

      tex_id = in_mem->tex_id;
      in_mem->tex_id = texture_id[i];

      if (!gst_memory_map ((GstMemory *) in_mem, &map_info, GST_MAP_READ | GST_MAP_GL)) {
        GST_WARNING_OBJECT (upload, "Failed to map GL memory");
        res = FALSE;
      }
      gst_memory_unmap ((GstMemory *) in_mem, &map_info);

      in_mem->tex_id = tex_id;
      GST_GL_MEMORY_FLAG_SET (in_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);
    } else {
      GstGLMemory *out_mem;

      if (!upload->priv->out_tex[i])
        upload->priv->out_tex[i] = gst_gl_memory_wrapped_texture (upload->context,
            texture_id[i], meta->texture_type[i],
            GST_VIDEO_INFO_WIDTH (&upload->info),
            GST_VIDEO_INFO_HEIGHT (&upload->info), NULL, NULL);

      out_mem = upload->priv->out_tex[i];

      if (out_mem->tex_id != texture_id[i]) {
        out_mem->tex_id = texture_id[i];
        GST_GL_MEMORY_FLAG_SET (out_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);
      }

      if (!(res = gst_gl_memory_copy_into_texture (in_mem, out_mem->tex_id,
            out_mem->tex_type, out_mem->width, out_mem->height, out_mem->stride,
            FALSE)))
        break;
    }
  }

  return res;
}

static gboolean
_perform_with_data_unlocked (GstGLUploadMeta * upload,
    GstVideoGLTextureUploadMeta * meta, 
    gpointer data[GST_VIDEO_MAX_PLANES], guint texture_id[4])
{
  guint i;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->info); i++) {
    if (!upload->priv->in_tex[i])
      upload->priv->in_tex[i] = gst_gl_memory_wrapped (upload->context,
          meta->texture_type[i], GST_VIDEO_INFO_WIDTH (&upload->info),
          GST_VIDEO_INFO_HEIGHT (&upload->info),
          GST_VIDEO_INFO_PLANE_STRIDE (&upload->info, i), data[i], NULL, NULL);

    upload->priv->in_tex[i]->data = data[i];
  }

  return _perform_with_gl_memory (upload, meta, texture_id);
}

static gboolean
_perform_for_gl_texture_upload_meta (GstVideoGLTextureUploadMeta *
    meta, guint texture_id[4])
{
  GstGLUploadMeta *upload;
  GstVideoFrame frame;
  GstMemory *mem;
  gboolean ret;
  guint i, n;

  g_return_val_if_fail (meta != NULL, FALSE);
  g_return_val_if_fail (texture_id != NULL, FALSE);

  upload = meta->user_data;

  GST_OBJECT_LOCK (upload);

  if (!upload->priv->initted) {
    GstVideoInfo info;
    GstVideoMeta *v_meta = gst_buffer_get_video_meta (upload->priv->buffer);
    gint i;

    if (!(ret = v_meta != NULL))
      goto out;

    gst_video_info_init (&info);
    info.finfo = gst_video_format_get_info (v_meta->format);
    info.width = v_meta->width;
    info.height = v_meta->height;

    for (i = 0; i < info.finfo->n_planes; i++) {
      info.offset[i] = v_meta->offset[i];
      info.stride[i] = v_meta->stride[i];
    }

    _gst_gl_upload_meta_set_format_unlocked (upload, &info);
    upload->priv->initted = TRUE;
  }

  GST_LOG ("Uploading for meta with textures %i,%i,%i,%i", texture_id[0],
      texture_id[1], texture_id[2], texture_id[3]);

  /* GstGLMemory */
  n = gst_buffer_n_memory (upload->priv->buffer);
  mem = gst_buffer_peek_memory (upload->priv->buffer, 0);

  if (gst_is_gl_memory (mem) && n == GST_VIDEO_INFO_N_PLANES (&upload->info)) {
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->info); i++)
      upload->priv->in_tex[i] = (GstGLMemory *) gst_buffer_peek_memory (upload->priv->buffer, i);

    ret = _perform_with_gl_memory (upload, meta, texture_id);

    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->info); i++)
      upload->priv->in_tex[i] = NULL;

    if (ret)
      goto out;
  }

  if (!(ret = gst_video_frame_map (&frame, &upload->info, upload->priv->buffer,
          GST_MAP_READ))) {
    GST_ERROR ("failed to map video frame");
    goto out;
  }

  /* update the video info from the one updated by frame_map using video meta */
  _gst_gl_upload_meta_set_format_unlocked (upload, &frame.info);

  ret = _perform_with_data_unlocked (upload, meta, frame.data, texture_id);

  gst_video_frame_unmap (&frame);

out:
  GST_OBJECT_UNLOCK (upload);
  return ret;
}

/**
 * gst_gl_upload_meta_add_to_buffer:
 * @upload: a #GstGLUploadMeta
 * @buffer: a #GstBuffer
 *
 * Adds a #GstVideoGLTextureUploadMeta on @buffer using @upload
 *
 * Returns: whether it was successful
 */
gboolean
gst_gl_upload_meta_add_to_buffer (GstGLUploadMeta * upload, GstBuffer * buffer)
{
  GstVideoGLTextureType texture_types[GST_VIDEO_MAX_PLANES];
  GstVideoMeta *v_meta;
  gint i;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);
  v_meta = gst_buffer_get_video_meta (buffer);
  g_return_val_if_fail (v_meta != NULL, FALSE);

  upload->priv->buffer = buffer;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    texture_types[i] = gst_gl_texture_type_from_format (upload->context, v_meta->format, i);
  }

  gst_buffer_add_video_gl_texture_upload_meta (buffer,
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL, 1, texture_types,
      _perform_for_gl_texture_upload_meta, gst_object_ref (upload),
      gst_object_ref, gst_object_unref);

  return TRUE;
}
