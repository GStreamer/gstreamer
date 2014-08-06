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
#include "gstglupload.h"

#if GST_GL_HAVE_PLATFORM_EGL
#include "egl/gsteglimagememory.h"
#endif

/**
 * SECTION:gstglupload
 * @short_description: an object that uploads to GL textures
 * @see_also: #GstGLDownload, #GstGLMemory
 *
 * #GstGLUpload is an object that uploads data from system memory into GL textures.
 *
 * A #GstGLUpload can be created with gst_gl_upload_new()
 */

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

static gboolean _upload_memory (GstGLUpload * upload);
static gboolean _init_upload (GstGLUpload * upload);
static gboolean _gst_gl_upload_perform_with_data_unlocked (GstGLUpload * upload,
    GLuint * texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);
static void _do_upload_with_meta (GstGLContext * context, GstGLUpload * upload);
static void gst_gl_upload_reset (GstGLUpload * upload);

struct _GstGLUploadPrivate
{
  gboolean result;
  guint tex_id;

  gboolean mapped;
  GstVideoFrame frame;

  GstVideoGLTextureUploadMeta *meta;

  GstBuffer *outbuf;
  gboolean released;
};

GST_DEBUG_CATEGORY_STATIC (gst_gl_upload_debug);
#define GST_CAT_DEFAULT gst_gl_upload_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_upload_debug, "glupload", 0, "upload");

G_DEFINE_TYPE_WITH_CODE (GstGLUpload, gst_gl_upload, GST_TYPE_OBJECT,
    DEBUG_INIT);
static void gst_gl_upload_finalize (GObject * object);

#define GST_GL_UPLOAD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_UPLOAD, GstGLUploadPrivate))

static void
gst_gl_upload_class_init (GstGLUploadClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLUploadPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_upload_finalize;
}

static void
gst_gl_upload_init (GstGLUpload * upload)
{
  upload->priv = GST_GL_UPLOAD_GET_PRIVATE (upload);

  upload->context = NULL;
  upload->priv->tex_id = 0;

  gst_video_info_set_format (&upload->in_info, GST_VIDEO_FORMAT_ENCODED, 0, 0);
}

/**
 * gst_gl_upload_new:
 * @context: a #GstGLContext
 *
 * Returns: a new #GstGLUpload object
 */
GstGLUpload *
gst_gl_upload_new (GstGLContext * context)
{
  GstGLUpload *upload;

  upload = g_object_new (GST_TYPE_GL_UPLOAD, NULL);

  upload->context = gst_object_ref (context);
  upload->convert = gst_gl_color_convert_new (context);

  return upload;
}

static void
gst_gl_upload_finalize (GObject * object)
{
  GstGLUpload *upload;

  upload = GST_GL_UPLOAD (object);

  gst_gl_upload_reset (upload);

  if (upload->context) {
    gst_object_unref (upload->context);
    upload->context = NULL;
  }

  G_OBJECT_CLASS (gst_gl_upload_parent_class)->finalize (object);
}

static void
gst_gl_upload_reset (GstGLUpload * upload)
{
  guint i;

  if (upload->priv->tex_id) {
    gst_gl_context_del_texture (upload->context, &upload->priv->tex_id);
    upload->priv->tex_id = 0;
  }

  if (upload->convert) {
    gst_object_unref (upload->convert);
    upload->convert = NULL;
  }

  if (upload->out_tex) {
    gst_memory_unref ((GstMemory *) upload->out_tex);
    upload->out_tex = NULL;
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (upload->in_tex[i]) {
      gst_memory_unref ((GstMemory *) upload->in_tex[i]);
      upload->in_tex[i] = NULL;
    }
  }

  gst_gl_upload_release_buffer (upload);
}

static void
_gst_gl_upload_set_format_unlocked (GstGLUpload * upload,
    GstVideoInfo * in_info)
{
  g_return_if_fail (upload != NULL);
  g_return_if_fail (GST_VIDEO_INFO_FORMAT (in_info) !=
      GST_VIDEO_FORMAT_UNKNOWN);
  g_return_if_fail (GST_VIDEO_INFO_FORMAT (in_info) !=
      GST_VIDEO_FORMAT_ENCODED);

  if (gst_video_info_is_equal (&upload->in_info, in_info))
    return;

  gst_gl_upload_reset (upload);
  upload->convert = gst_gl_color_convert_new (upload->context);
  upload->in_info = *in_info;
  upload->initted = FALSE;
}

/**
 * gst_gl_upload_set_format:
 * @upload: a #GstGLUpload
 * @in_info: input #GstVideoInfo
 *
 * Initializes @upload with the information required for upload.
 */
void
gst_gl_upload_set_format (GstGLUpload * upload, GstVideoInfo * in_info)
{
  GST_OBJECT_LOCK (upload);
  _gst_gl_upload_set_format_unlocked (upload, in_info);
  GST_OBJECT_UNLOCK (upload);
}

/**
 * gst_gl_upload_get_format:
 * @upload: a #GstGLUpload
 *
 * Returns: (transfer none): The #GstVideoInfo set by gst_gl_upload_set_format()
 */
GstVideoInfo *
gst_gl_upload_get_format (GstGLUpload * upload)
{
  GstVideoInfo *ret;

  GST_OBJECT_LOCK (upload);
  ret = &upload->in_info;
  GST_OBJECT_UNLOCK (upload);

  return ret;
}

/**
 * gst_gl_upload_perform_with_buffer:
 * @upload: a #GstGLUpload
 * @buffer: a #GstBuffer
 * @tex_id: resulting texture
 *
 * Uploads @buffer to the texture given by @tex_id.  @tex_id is valid
 * until gst_gl_upload_release_buffer() is called.
 *
 * Returns: whether the upload was successful
 */
gboolean
gst_gl_upload_perform_with_buffer (GstGLUpload * upload, GstBuffer * buffer,
    guint * tex_id)
{
  GstMemory *mem;
  GstVideoGLTextureUploadMeta *gl_tex_upload_meta;
  guint texture_ids[] = { 0, 0, 0, 0 };
  gint i;
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (tex_id != NULL, FALSE);
  g_return_val_if_fail (gst_buffer_n_memory (buffer) > 0, FALSE);

  gst_gl_upload_release_buffer (upload);

  /* GstGLMemory */
  mem = gst_buffer_peek_memory (buffer, 0);

  if (gst_is_gl_memory (mem)) {
    if (GST_VIDEO_INFO_FORMAT (&upload->in_info) == GST_VIDEO_FORMAT_RGBA) {
      GstMapInfo map_info;

      gst_memory_map (mem, &map_info, GST_MAP_READ | GST_MAP_GL);
      gst_memory_unmap (mem, &map_info);

      *tex_id = ((GstGLMemory *) mem)->tex_id;
      return TRUE;
    }

    GST_LOG_OBJECT (upload, "Attempting upload with GstGLMemory");
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->in_info); i++) {
      upload->in_tex[i] = (GstGLMemory *) gst_buffer_peek_memory (buffer, i);
    }

    ret = _upload_memory (upload);

    *tex_id = upload->out_tex->tex_id;
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->in_info); i++) {
      upload->in_tex[i] = NULL;
    }
    return ret;
  }
#if GST_GL_HAVE_PLATFORM_EGL
  if (!upload->priv->tex_id && gst_is_egl_image_memory (mem))
    gst_gl_context_gen_texture (upload->context, &upload->priv->tex_id,
        GST_VIDEO_FORMAT_RGBA, 0, 0);
#endif

  if (!upload->priv->tex_id)
    gst_gl_context_gen_texture (upload->context, &upload->priv->tex_id,
        GST_VIDEO_FORMAT_RGBA, GST_VIDEO_INFO_WIDTH (&upload->in_info),
        GST_VIDEO_INFO_HEIGHT (&upload->in_info));

  /* GstVideoGLTextureUploadMeta */
  gl_tex_upload_meta = gst_buffer_get_video_gl_texture_upload_meta (buffer);
  if (gl_tex_upload_meta) {
    GST_LOG_OBJECT (upload, "Attempting upload with "
        "GstVideoGLTextureUploadMeta");
    texture_ids[0] = upload->priv->tex_id;

    if (!gst_gl_upload_perform_with_gl_texture_upload_meta (upload,
            gl_tex_upload_meta, texture_ids)) {
      GST_DEBUG_OBJECT (upload, "Upload with GstVideoGLTextureUploadMeta "
          "failed");
    } else {
      upload->priv->mapped = FALSE;
      *tex_id = upload->priv->tex_id;
      return TRUE;
    }
  }

  GST_LOG_OBJECT (upload, "Attempting upload with raw data");
  /* GstVideoMeta map */
  if (!gst_video_frame_map (&upload->priv->frame, &upload->in_info, buffer,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (upload, "Failed to map memory");
    return FALSE;
  }
  upload->priv->mapped = TRUE;

  /* update the video info from the one updated by frame_map using video meta */
  gst_gl_upload_set_format (upload, &upload->priv->frame.info);

  if (!gst_gl_upload_perform_with_data (upload, tex_id,
          upload->priv->frame.data)) {
    return FALSE;
  }

  return TRUE;
}

void
gst_gl_upload_release_buffer (GstGLUpload * upload)
{
  g_return_if_fail (upload != NULL);

  if (upload->priv->mapped)
    gst_video_frame_unmap (&upload->priv->frame);
  upload->priv->mapped = FALSE;

  if (upload->priv->outbuf) {
    gst_buffer_unref (upload->priv->outbuf);
    upload->priv->outbuf = NULL;
  }

  upload->priv->released = TRUE;
}

/*
 * Uploads using gst_video_gl_texture_upload_meta_upload().
 * i.e. consumer of GstVideoGLTextureUploadMeta
 */
static void
_do_upload_with_meta (GstGLContext * context, GstGLUpload * upload)
{
  guint texture_ids[] = { upload->priv->tex_id, 0, 0, 0 };

  if (!gst_video_gl_texture_upload_meta_upload (upload->priv->meta,
          texture_ids))
    goto error;

  upload->priv->result = TRUE;
  return;

error:
  upload->priv->result = FALSE;
}

/**
 * gst_gl_upload_perform_with_gl_texture_upload_meta:
 * @upload: a #GstGLUpload
 * @meta: a #GstVideoGLTextureUploadMeta
 * @texture_id: resulting GL textures to place the data into.
 *
 * Uploads @meta into @texture_id.
 *
 * Returns: whether the upload was successful
 */
gboolean
gst_gl_upload_perform_with_gl_texture_upload_meta (GstGLUpload * upload,
    GstVideoGLTextureUploadMeta * meta, guint texture_id[4])
{
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (meta != NULL, FALSE);

  if (meta->texture_orientation !=
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL)
    GST_FIXME_OBJECT (upload, "only x-normal,y-normal textures supported, "
        "the images will not appear the right way up");
  if (meta->texture_type[0] != GST_VIDEO_GL_TEXTURE_TYPE_RGBA) {
    GST_FIXME_OBJECT (upload, "only single rgba texture supported");
    return FALSE;
  }

  GST_OBJECT_LOCK (upload);

  upload->priv->meta = meta;
  if (!upload->priv->tex_id)
    gst_gl_context_gen_texture (upload->context, &upload->priv->tex_id,
        GST_VIDEO_FORMAT_RGBA, GST_VIDEO_INFO_WIDTH (&upload->in_info),
        GST_VIDEO_INFO_HEIGHT (&upload->in_info));

  GST_LOG ("Uploading with GLTextureUploadMeta with textures %i,%i,%i,%i",
      texture_id[0], texture_id[1], texture_id[2], texture_id[3]);

  gst_gl_context_thread_add (upload->context,
      (GstGLContextThreadFunc) _do_upload_with_meta, upload);

  ret = upload->priv->result;

  GST_OBJECT_UNLOCK (upload);

  return ret;
}

/**
 * gst_gl_upload_perform_with_data:
 * @upload: a #GstGLUpload
 * @texture_id: (out): the texture id to upload into
 * @data: where the downloaded data should go
 *
 * Uploads @data into @texture_id. data size and format is specified by
 * the #GstVideoInfo<!--  -->s passed to gst_gl_upload_set_format() 
 *
 * Returns: whether the upload was successful
 */
gboolean
gst_gl_upload_perform_with_data (GstGLUpload * upload, GLuint * texture_id,
    gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);

  GST_OBJECT_LOCK (upload);
  ret = _gst_gl_upload_perform_with_data_unlocked (upload, texture_id, data);
  GST_OBJECT_UNLOCK (upload);

  return ret;
}

static gboolean
_gst_gl_upload_perform_with_data_unlocked (GstGLUpload * upload,
    GLuint * texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;
  guint i;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (texture_id != NULL, FALSE);

  if (!upload->in_tex[0])
    gst_gl_memory_setup_wrapped (upload->context, &upload->in_info, data,
        upload->in_tex);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (upload->in_tex[i]) {
      upload->in_tex[i]->data = data[i];
      GST_GL_MEMORY_FLAG_SET (upload->in_tex[i],
          GST_GL_MEMORY_FLAG_NEED_UPLOAD);
    }
  }

  ret = _upload_memory (upload);
  *texture_id = upload->out_tex->tex_id;

  return ret;
}

/* Called in the gl thread */
static gboolean
_init_upload (GstGLUpload * upload)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  GstVideoInfo out_info;

  gl = upload->context->gl_vtable;

  v_format = GST_VIDEO_INFO_FORMAT (&upload->in_info);

  GST_INFO ("Initializing texture upload for format:%s",
      gst_video_format_to_string (v_format));

  if (!gl->CreateProgramObject && !gl->CreateProgram) {
    gst_gl_context_set_error (upload->context,
        "Cannot upload YUV formats without OpenGL shaders");
    goto error;
  }

  gst_video_info_set_format (&out_info, GST_VIDEO_FORMAT_RGBA,
      GST_VIDEO_INFO_WIDTH (&upload->in_info),
      GST_VIDEO_INFO_HEIGHT (&upload->in_info));

  gst_gl_color_convert_set_format (upload->convert, &upload->in_info,
      &out_info);

  upload->out_tex = gst_gl_memory_wrapped_texture (upload->context, 0,
      GST_VIDEO_GL_TEXTURE_TYPE_RGBA, GST_VIDEO_INFO_WIDTH (&upload->in_info),
      GST_VIDEO_INFO_HEIGHT (&upload->in_info), NULL, NULL);

  upload->initted = TRUE;

  return TRUE;

error:
  return FALSE;
}

static gboolean
_upload_memory (GstGLUpload * upload)
{
  guint in_width, in_height;
  guint in_texture[GST_VIDEO_MAX_PLANES];
  GstBuffer *inbuf;
  GstVideoFrame out_frame;
  GstVideoInfo out_info;
  gint i;

  in_width = GST_VIDEO_INFO_WIDTH (&upload->in_info);
  in_height = GST_VIDEO_INFO_HEIGHT (&upload->in_info);

  if (!upload->initted) {
    if (!_init_upload (upload)) {
      return FALSE;
    }
  }

  inbuf = gst_buffer_new ();
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->in_info); i++) {
    in_texture[i] = upload->in_tex[i]->tex_id;
    gst_buffer_append_memory (inbuf,
        gst_memory_ref ((GstMemory *) upload->in_tex[i]));
  }

  GST_TRACE ("uploading with textures:%u,%u,%u dimensions:%ux%u",
      in_texture[0], in_texture[1], in_texture[2], in_width, in_height);

  upload->priv->outbuf = gst_gl_color_convert_perform (upload->convert, inbuf);
  gst_buffer_unref (inbuf);

  gst_video_info_set_format (&out_info, GST_VIDEO_FORMAT_RGBA, in_width,
      in_height);
  if (!gst_video_frame_map (&out_frame, &out_info, upload->priv->outbuf,
          GST_MAP_READ | GST_MAP_GL)) {
    gst_buffer_unref (upload->priv->outbuf);
    upload->priv->outbuf = NULL;
    return FALSE;
  }

  upload->out_tex->tex_id = *(guint *) out_frame.data[0];

  gst_video_frame_unmap (&out_frame);
  upload->priv->released = FALSE;

  return TRUE;
}
