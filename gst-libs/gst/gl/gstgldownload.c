/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystree00@gmail.com>
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
#include "gstgldownload.h"

/**
 * SECTION:gstgldownload
 * @short_description: an object that downloads GL textures
 * @see_also: #GstGLUpload, #GstGLMemory
 *
 * #GstGLDownload is an object that downloads GL textures into system memory.
 *
 * A #GstGLDownload can be created with gst_gl_download_new()
 */

#define USING_OPENGL(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL)
#define USING_OPENGL3(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL3)
#define USING_GLES(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES)
#define USING_GLES2(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES2)
#define USING_GLES3(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES3)

static void _do_download (GstGLContext * context, GstGLDownload * download);
static void _init_download (GstGLContext * context, GstGLDownload * download);
static gboolean _gst_gl_download_perform_with_data_unlocked (GstGLDownload *
    download, GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);

/* *INDENT-ON* */

struct _GstGLDownloadPrivate
{
  const gchar *YUY2_UYVY;
  const gchar *I420_YV12;
  const gchar *AYUV;
  const gchar *ARGB;
  const gchar *vert_shader;

  gboolean result;
};

GST_DEBUG_CATEGORY_STATIC (gst_gl_download_debug);
#define GST_CAT_DEFAULT gst_gl_download_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_download_debug, "gldownload", 0, "download");

G_DEFINE_TYPE_WITH_CODE (GstGLDownload, gst_gl_download, GST_TYPE_OBJECT,
    DEBUG_INIT);
static void gst_gl_download_finalize (GObject * object);

#define GST_GL_DOWNLOAD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_DOWNLOAD, GstGLDownloadPrivate))

static void
gst_gl_download_class_init (GstGLDownloadClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLDownloadPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_download_finalize;
}

static void
gst_gl_download_init (GstGLDownload * download)
{

  download->priv = GST_GL_DOWNLOAD_GET_PRIVATE (download);

  g_mutex_init (&download->lock);

  gst_video_info_init (&download->info);
}

/**
 * gst_gl_download_new:
 * @context: a #GstGLContext
 *
 * Returns: a new #GstGLDownload object
 */
GstGLDownload *
gst_gl_download_new (GstGLContext * context)
{
  GstGLDownload *download;

  download = g_object_new (GST_TYPE_GL_DOWNLOAD, NULL);

  download->context = gst_object_ref (context);
  download->convert = gst_gl_color_convert_new (context);

  return download;
}

static void
gst_gl_download_finalize (GObject * object)
{
  GstGLDownload *download;
  guint i;

  download = GST_GL_DOWNLOAD (object);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (download->out_texture[i]) {
      gst_memory_unref ((GstMemory *) download->out_texture[i]);
      download->out_texture[i] = NULL;
    }
  }

  if (download->in_texture) {
    gst_gl_context_del_texture (download->context, &download->in_texture);
    download->in_texture = 0;
  }
  if (download->convert) {
    gst_object_unref (download->convert);
    download->convert = NULL;
  }

  if (download->context) {
    gst_object_unref (download->context);
    download->context = NULL;
  }

  g_mutex_clear (&download->lock);

  G_OBJECT_CLASS (gst_gl_download_parent_class)->finalize (object);
}

/**
 * gst_gl_download_init_format:
 * @download: a #GstGLDownload
 * @v_format: a #GstVideoFormat
 * @out_width: the width to download to
 * @out_height: the height to download to
 *
 * Initializes @download with the information required for download.
 *
 * Returns: whether the initialization was successful
 */
gboolean
gst_gl_download_init_format (GstGLDownload * download, GstVideoFormat v_format,
    guint out_width, guint out_height)
{
  GstVideoInfo info;
  gboolean ret;

  g_return_val_if_fail (download != NULL, FALSE);
  g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_ENCODED, FALSE);
  g_return_val_if_fail (out_width > 0 && out_height > 0, FALSE);

  g_mutex_lock (&download->lock);

  if (download->initted) {
    g_mutex_unlock (&download->lock);
    return FALSE;
  }

  gst_video_info_set_format (&info, v_format, out_width, out_height);

  download->info = info;

  gst_gl_context_thread_add (download->context,
      (GstGLContextThreadFunc) _init_download, download);

  ret = download->initted = download->priv->result;

  g_mutex_unlock (&download->lock);

  return ret;
}

/**
 * gst_gl_download_perform_with_memory:
 * @download: a #GstGLDownload
 * @gl_mem: a #GstGLMemory
 *
 * Downloads the texture in @gl_mem
 *
 * Returns: whether the download was successful
 */
gboolean
gst_gl_download_perform_with_memory (GstGLDownload * download,
    GstGLMemory * gl_mem)
{
  gpointer data[GST_VIDEO_MAX_PLANES];
  guint i;
  gboolean ret;

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_DOWNLOAD_INITTED))
    return FALSE;

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD)) {
    return FALSE;
  }

  g_mutex_lock (&download->lock);

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&download->info); i++) {
    data[i] = (guint8 *) gl_mem->data +
        GST_VIDEO_INFO_PLANE_OFFSET (&download->info, i);
  }

  ret =
      _gst_gl_download_perform_with_data_unlocked (download, gl_mem->tex_id,
      data);

  if (ret)
    GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);

  g_mutex_unlock (&download->lock);

  return ret;
}

/**
 * gst_gl_download_perform_with_data:
 * @download: a #GstGLDownload
 * @texture_id: the texture id to download
 * @data: (out): where the downloaded data should go
 *
 * Downloads @texture_id into @data. @data size and format is specified by
 * the #GstVideoFormat passed to gst_gl_download_init_format() 
 *
 * Returns: whether the download was successful
 */
gboolean
gst_gl_download_perform_with_data (GstGLDownload * download, GLuint texture_id,
    gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (download != NULL, FALSE);

  g_mutex_lock (&download->lock);

  ret =
      _gst_gl_download_perform_with_data_unlocked (download, texture_id, data);

  g_mutex_unlock (&download->lock);

  return ret;
}

static gboolean
_gst_gl_download_perform_with_data_unlocked (GstGLDownload * download,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean realloc = FALSE;
  gpointer temp_data;
  guint i;

  g_return_val_if_fail (download != NULL, FALSE);
  g_return_val_if_fail (texture_id > 0, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&download->info) !=
      GST_VIDEO_FORMAT_UNKNOWN
      && GST_VIDEO_INFO_FORMAT (&download->info) != GST_VIDEO_FORMAT_ENCODED,
      FALSE);

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&download->info); i++) {
    g_return_val_if_fail (data[i] != NULL, FALSE);
  }

  download->in_texture = texture_id;
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&download->info); i++) {
    if (download->data[i] != data[i])
      realloc = TRUE;
  }

  if (realloc) {
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&download->info); i++) {
      if (download->out_texture[i])
        gst_memory_unref ((GstMemory *) download->out_texture[i]);
      download->data[i] = data[i];
    }

    if (GST_VIDEO_INFO_FORMAT (&download->info) == GST_VIDEO_FORMAT_YV12) {
      /* YV12 same as I420 except planes 1+2 swapped */
      temp_data = download->data[1];
      download->data[1] = download->data[2];
      download->data[2] = temp_data;
    }

    gst_gl_memory_setup_wrapped (download->context, &download->info,
        download->data, download->out_texture);
  }

  gst_gl_context_thread_add (download->context,
      (GstGLContextThreadFunc) _do_download, download);

  return download->priv->result;
}

static void
_init_download (GstGLContext * context, GstGLDownload * download)
{
  GstVideoFormat v_format;
  guint out_width, out_height;
  GstVideoInfo in_info;

  v_format = GST_VIDEO_INFO_FORMAT (&download->info);
  out_width = GST_VIDEO_INFO_WIDTH (&download->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&download->info);

  GST_TRACE ("initializing texture download for format %s",
      gst_video_format_to_string (v_format));

  if (USING_GLES2 (context) && !USING_GLES3 (context)) {
    /* GL_RGBA is the only officially supported texture format in GLES2 */
    if (v_format == GST_VIDEO_FORMAT_RGB || v_format == GST_VIDEO_FORMAT_BGR) {
      gst_gl_context_set_error (context, "Cannot download RGB textures in "
          "GLES2");
      download->priv->result = FALSE;
      return;
    }
  }

  gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_RGBA, out_width,
      out_height);

  gst_gl_color_convert_set_format (download->convert, &in_info,
      &download->info);

  download->priv->result = TRUE;
}

static void
_do_download (GstGLContext * context, GstGLDownload * download)
{
  guint out_width, out_height;
  GstMapInfo map_info;
  GstGLMemory *in_tex[GST_VIDEO_MAX_PLANES] = { 0, };
  gint i;

  out_width = GST_VIDEO_INFO_WIDTH (&download->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&download->info);

  GST_TRACE ("doing YUV download of texture:%u (%ux%u)",
      download->in_texture, out_width, out_height);

  in_tex[0] = gst_gl_memory_wrapped_texture (context, download->in_texture,
      GST_VIDEO_GL_TEXTURE_TYPE_RGBA, out_width, out_height, NULL, NULL);

  download->priv->result =
      gst_gl_color_convert_perform (download->convert, in_tex,
      download->out_texture);
  if (!download->priv->result)
    return;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&download->info); i++) {
    gst_memory_map ((GstMemory *) download->out_texture[i], &map_info,
        GST_MAP_READ);
    gst_memory_unmap ((GstMemory *) download->out_texture[i], &map_info);
  }

  gst_memory_unref ((GstMemory *) in_tex[0]);
}
