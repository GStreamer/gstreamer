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

#define USING_OPENGL(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL)
#define USING_OPENGL3(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL3)
#define USING_GLES(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES)
#define USING_GLES2(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES2)
#define USING_GLES3(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES3)

static gboolean _upload_memory (GstGLUpload * upload);
//static gboolean _do_upload_fill (GstGLContext * context, GstGLUpload * upload);
//static gboolean _do_upload_make (GstGLContext * context, GstGLUpload * upload);
static gboolean _init_upload (GstGLUpload * upload);
//static gboolean _init_upload_fbo (GstGLContext * context, GstGLUpload * upload);
static gboolean _gst_gl_upload_perform_with_data_unlocked (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);
static void _do_upload_with_meta (GstGLContext * context, GstGLUpload * upload);
static void gst_gl_upload_reset (GstGLUpload * upload);

/* *INDENT-OFF* */

#define YUV_TO_RGB_COEFFICIENTS \
      "uniform vec3 offset;\n" \
      "uniform vec3 rcoeff;\n" \
      "uniform vec3 gcoeff;\n" \
      "uniform vec3 bcoeff;\n"

/* FIXME: use the colormatrix support from videoconvert */
struct TexData
{
  guint format, type, width, height;
  gfloat tex_scaling[2];
  guint unpack_length;
};

struct _GstGLUploadPrivate
{
  gboolean result;

  struct TexData texture_info[GST_VIDEO_MAX_PLANES];

  GstBuffer *buffer;
  GstVideoFrame frame;
  GstVideoGLTextureUploadMeta *meta;
  guint tex_id;
  gboolean mapped;
};

GST_DEBUG_CATEGORY_STATIC (gst_gl_upload_debug);
#define GST_CAT_DEFAULT gst_gl_upload_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_upload_debug, "glupload", 0, "upload");

G_DEFINE_TYPE_WITH_CODE (GstGLUpload, gst_gl_upload, GST_TYPE_OBJECT, DEBUG_INIT);
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

  g_mutex_init (&upload->lock);
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

  g_mutex_clear (&upload->lock);

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
}

static void
_gst_gl_upload_set_format_unlocked (GstGLUpload * upload,
    GstVideoInfo *in_info)
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
  g_mutex_lock (&upload->lock);

  _gst_gl_upload_set_format_unlocked (upload, in_info);

  g_mutex_unlock (&upload->lock);
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

  g_mutex_lock (&upload->lock);

  ret = &upload->in_info;

  g_mutex_unlock (&upload->lock);

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

    if (!upload->out_tex)
      upload->out_tex = (GstGLMemory *) gst_gl_memory_alloc (upload->context,
          GST_VIDEO_GL_TEXTURE_TYPE_RGBA, GST_VIDEO_INFO_WIDTH (&upload->in_info),
          GST_VIDEO_INFO_HEIGHT (&upload->in_info),
          GST_VIDEO_INFO_PLANE_STRIDE (&upload->in_info, 0));

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

  /* update the video info from the one updated by frame_map using video meta */
  gst_gl_upload_set_format (upload, &upload->priv->frame.info);

  if (!upload->priv->tex_id)
    gst_gl_context_gen_texture (upload->context, &upload->priv->tex_id,
        GST_VIDEO_FORMAT_RGBA, GST_VIDEO_INFO_WIDTH (&upload->in_info),
        GST_VIDEO_INFO_HEIGHT (&upload->in_info));

  if (!gst_gl_upload_perform_with_data (upload, upload->priv->tex_id,
          upload->priv->frame.data)) {
    return FALSE;
  }

  upload->priv->mapped = TRUE;
  *tex_id = upload->priv->tex_id;
  return TRUE;
}

void
gst_gl_upload_release_buffer (GstGLUpload * upload)
{
  g_return_if_fail (upload != NULL);

  if (upload->priv->mapped)
    gst_video_frame_unmap (&upload->priv->frame);
}

/*
 * Uploads as a result of a call to gst_video_gl_texture_upload_meta_upload().
 * i.e. provider of GstVideoGLTextureUploadMeta
 */
static gboolean
_do_upload_for_meta (GstGLUpload * upload, GstVideoGLTextureUploadMeta * meta)
{
  GstVideoInfo in_info;
  GstVideoFrame frame;
  GstMemory *mem;
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (meta != NULL, FALSE);

  /* GstGLMemory */
  mem = gst_buffer_peek_memory (upload->priv->buffer, 0);

  if (gst_is_gl_memory (mem)) {
    GstGLMemory *gl_mem = (GstGLMemory *) mem;

    upload->in_tex[0] = gl_mem;
    ret = _upload_memory (upload);
    upload->in_tex[0] = NULL;

    if (ret)
      return TRUE;
  }

  if (!gst_video_frame_map (&frame, &in_info, upload->priv->buffer,
          GST_MAP_READ)) {
    GST_ERROR ("failed to map video frame");
    return FALSE;
  }

  /* update the video info from the one updated by frame_map using video meta */
  gst_gl_upload_set_format (upload, &upload->priv->frame.info);

  if (!upload->priv->tex_id)
    gst_gl_context_gen_texture (upload->context, &upload->priv->tex_id,
        GST_VIDEO_FORMAT_RGBA, GST_VIDEO_INFO_WIDTH (&upload->in_info),
        GST_VIDEO_INFO_HEIGHT (&upload->in_info));

  ret = _gst_gl_upload_perform_with_data_unlocked (upload,
      upload->out_tex->tex_id, frame.data);

  gst_video_frame_unmap (&frame);

  return ret;
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

  g_mutex_lock (&upload->lock);

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

  g_mutex_unlock (&upload->lock);

  return ret;
}

static gboolean
_gst_gl_upload_perform_for_gl_texture_upload_meta (GstVideoGLTextureUploadMeta *
    meta, guint texture_id[4])
{
  GstGLUpload *upload;
  gboolean ret;

  g_return_val_if_fail (meta != NULL, FALSE);
  g_return_val_if_fail (texture_id != NULL, FALSE);

  upload = meta->user_data;

  g_mutex_lock (&upload->lock);

  if (!upload->initted) {
    GstVideoInfo in_info;
    GstVideoMeta *v_meta = gst_buffer_get_video_meta (upload->priv->buffer);
    gint i;

    if (v_meta == NULL)
      return FALSE;

    gst_video_info_init (&in_info);
    in_info.finfo = gst_video_format_get_info (v_meta->format);
    in_info.width = v_meta->width;
    in_info.height = v_meta->height;

    for (i = 0; i < in_info.finfo->n_planes; i++) {
      in_info.offset[i] = v_meta->offset[i];
      in_info.stride[i] = v_meta->stride[i];
    }

    _gst_gl_upload_set_format_unlocked (upload, &in_info);

    upload->out_tex = gst_gl_memory_wrapped_texture (upload->context,
        texture_id[0], GST_VIDEO_GL_TEXTURE_TYPE_RGBA,
        GST_VIDEO_INFO_WIDTH (&upload->in_info),
        GST_VIDEO_INFO_HEIGHT (&upload->in_info), NULL, NULL);
  }

  /* FIXME: kinda breaks the abstraction */
  if (upload->out_tex->tex_id != texture_id[0]) {
    upload->out_tex->tex_id = texture_id[0];
    GST_GL_MEMORY_FLAG_SET (upload->out_tex, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);
  }

  GST_LOG ("Uploading for meta with textures %i,%i,%i,%i", texture_id[0],
      texture_id[1], texture_id[2], texture_id[3]);

  ret = _do_upload_for_meta (upload, meta);

  g_mutex_unlock (&upload->lock);

  return ret;
}

/**
 * gst_gl_upload_add_video_gl_texture_upload_meta:
 * @upload: a #GstGLUpload
 * @buffer: a #GstBuffer
 *
 * Adds a #GstVideoGLTextureUploadMeta on @buffer using @upload
 *
 * Returns: whether it was successful
 */
gboolean
gst_gl_upload_add_video_gl_texture_upload_meta (GstGLUpload * upload,
    GstBuffer * buffer)
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
    texture_types[i] = gst_gl_texture_type_from_format (v_meta->format, i);
  }

  gst_buffer_add_video_gl_texture_upload_meta (buffer,
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL, 1, texture_types,
      _gst_gl_upload_perform_for_gl_texture_upload_meta,
      gst_object_ref (upload), gst_object_ref, gst_object_unref);

  return TRUE;
}

/**
 * gst_gl_upload_perform_with_data:
 * @upload: a #GstGLUpload
 * @texture_id: the texture id to download
 * @data: where the downloaded data should go
 *
 * Uploads @data into @texture_id. data size and format is specified by
 * the #GstVideoInfo<!--  -->s passed to gst_gl_upload_set_format() 
 *
 * Returns: whether the upload was successful
 */
gboolean
gst_gl_upload_perform_with_data (GstGLUpload * upload, GLuint texture_id,
    gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);

  g_mutex_lock (&upload->lock);

  if (!upload->out_tex)
    upload->out_tex = gst_gl_memory_wrapped_texture (upload->context, texture_id,
        GST_VIDEO_GL_TEXTURE_TYPE_RGBA, GST_VIDEO_INFO_WIDTH (&upload->in_info),
        GST_VIDEO_INFO_HEIGHT (&upload->in_info), NULL, NULL);

  /* FIXME: kinda breaks the abstraction */
  if (upload->out_tex->tex_id != texture_id) {
    upload->out_tex->tex_id = texture_id;
    GST_GL_MEMORY_FLAG_SET (upload->out_tex, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);
  }

  ret = _gst_gl_upload_perform_with_data_unlocked (upload, texture_id, data);

  g_mutex_unlock (&upload->lock);

  return ret;
}

static gboolean
_gst_gl_upload_perform_with_data_unlocked (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  guint i;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (texture_id > 0, FALSE);

  if (!upload->in_tex[0])
    gst_gl_memory_setup_wrapped (upload->context, &upload->in_info, data,
        upload->in_tex);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (upload->in_tex[i] && upload->in_tex[i]->data != data[i]) {
      upload->in_tex[i]->data = data[i];
      GST_GL_MEMORY_FLAG_SET (upload->in_tex[i], GST_GL_MEMORY_FLAG_NEED_UPLOAD);
    }
  }

  GST_LOG ("Uploading data into texture %u", texture_id);

  return _upload_memory (upload);
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

  gst_gl_color_convert_set_format (upload->convert, &upload->in_info, &out_info);

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
  GstGLMemory *out_texture[GST_VIDEO_MAX_PLANES] = {upload->out_tex, 0, 0, 0};
  gint i;

  in_width = GST_VIDEO_INFO_WIDTH (&upload->in_info);
  in_height = GST_VIDEO_INFO_HEIGHT (&upload->in_info);

  if (!upload->initted) {
    if (!_init_upload (upload)) {
      return FALSE;
    }
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->in_info); i++) {
    in_texture[i] = upload->in_tex[i]->tex_id;
  }

  GST_TRACE ("uploading to texture:%u with textures:%u,%u,%u dimensions:%ux%u", 
      out_texture[0]->tex_id, in_texture[0], in_texture[1], in_texture[2],
      in_width, in_height);

  return gst_gl_color_convert_perform (upload->convert, upload->in_tex, out_texture);
}
