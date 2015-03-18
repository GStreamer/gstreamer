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

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

static gboolean _do_download (GstGLDownload * download, guint texture_id,
    gpointer data[GST_VIDEO_MAX_PLANES]);
static gboolean _init_download (GstGLDownload * download);
static gboolean _gst_gl_download_perform_with_data_unlocked (GstGLDownload *
    download, GLuint texture_id, GLuint texture_target,
    gpointer data[GST_VIDEO_MAX_PLANES]);
static void gst_gl_download_reset (GstGLDownload * download);

/* *INDENT-ON* */

struct _GstGLDownloadPrivate
{
  const gchar *YUY2_UYVY;
  const gchar *I420_YV12;
  const gchar *AYUV;
  const gchar *ARGB;
  const gchar *vert_shader;

  GstGLMemory *in_tex[GST_VIDEO_MAX_PLANES];
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

  download = GST_GL_DOWNLOAD (object);

  gst_gl_download_reset (download);

  if (download->convert) {
    gst_object_unref (download->convert);
    download->convert = NULL;
  }

  if (download->context) {
    gst_object_unref (download->context);
    download->context = NULL;
  }

  G_OBJECT_CLASS (gst_gl_download_parent_class)->finalize (object);
}

static void
gst_gl_download_reset (GstGLDownload * download)
{
  guint i;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (download->priv->in_tex[i]) {
      gst_memory_unref ((GstMemory *) download->priv->in_tex[i]);
      download->priv->in_tex[i] = NULL;
    }
  }
}

/**
 * gst_gl_download_set_format:
 * @download: a #GstGLDownload
 * @out_info: a #GstVideoInfo
 *
 * Initializes @download with the information required for download.
 */
void
gst_gl_download_set_format (GstGLDownload * download, GstVideoInfo * out_info)
{
  g_return_if_fail (download != NULL);
  g_return_if_fail (GST_VIDEO_INFO_FORMAT (out_info) !=
      GST_VIDEO_FORMAT_UNKNOWN);
  g_return_if_fail (GST_VIDEO_INFO_FORMAT (out_info) !=
      GST_VIDEO_FORMAT_ENCODED);

  GST_OBJECT_LOCK (download);

  if (gst_video_info_is_equal (&download->info, out_info)) {
    GST_OBJECT_UNLOCK (download);
    return;
  }

  gst_gl_download_reset (download);
  download->initted = FALSE;
  download->info = *out_info;

  GST_OBJECT_UNLOCK (download);
}

static GstCaps *
_set_caps_features (const GstCaps * caps, const gchar * feature_name)
{
  GstCaps *tmp = gst_caps_copy (caps);
  guint n = gst_caps_get_size (tmp);
  guint i = 0;

  for (i = 0; i < n; i++) {
    GstCapsFeatures *features;

    features = gst_caps_features_new (feature_name, NULL);
    gst_caps_set_features (tmp, i, features);
  }

  return tmp;
}

GstCaps *
gst_gl_download_transform_caps (GstGLContext * context,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *gl_templ, *templ, *result, *tmp;

  templ =
      gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_GL_COLOR_CONVERT_FORMATS));
  gl_templ =
      gst_caps_from_string (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
      (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, GST_GL_COLOR_CONVERT_FORMATS));

  if (direction == GST_PAD_SRC) {
    tmp = gst_caps_intersect_full (caps, templ, GST_CAPS_INTERSECT_FIRST);
    result = _set_caps_features (tmp, GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
    gst_caps_unref (tmp);
    tmp = result;
  } else {
    tmp = gst_caps_ref (caps);
  }

  result =
      gst_gl_color_convert_transform_caps (context, direction, tmp, filter);
  gst_caps_unref (tmp);
  tmp = result;

  if (direction == GST_PAD_SINK) {
    result = _set_caps_features (tmp, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
    gst_caps_unref (tmp);
    tmp = result;
    result = gst_caps_intersect_full (tmp, templ, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
    tmp = result;
  }

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }
  gst_caps_unref (templ);
  gst_caps_unref (gl_templ);

  return result;
}

/**
 * gst_gl_download_perform_with_data:
 * @download: a #GstGLDownload
 * @texture_id: the texture id to download
 * @texture_target: the GL texture target
 * @data: (out): where the downloaded data should go
 *
 * Downloads @texture_id into @data. @data size and format is specified by
 * the #GstVideoFormat passed to gst_gl_download_set_format() 
 *
 * Returns: whether the download was successful
 */
gboolean
gst_gl_download_perform_with_data (GstGLDownload * download,
    GLuint texture_id, GLuint texture_target,
    gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (download != NULL, FALSE);

  GST_OBJECT_LOCK (download);
  ret =
      _gst_gl_download_perform_with_data_unlocked (download,
      texture_id, texture_target, data);
  GST_OBJECT_UNLOCK (download);

  return ret;
}

static gboolean
_gst_gl_download_perform_with_data_unlocked (GstGLDownload * download,
    GLuint texture_id, GLuint texture_target,
    gpointer data[GST_VIDEO_MAX_PLANES])
{
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

  if (!download->priv->in_tex[0]) {
    GstVideoInfo temp_info;

    gst_video_info_set_format (&temp_info, GST_VIDEO_FORMAT_RGBA,
        GST_VIDEO_INFO_WIDTH (&download->info),
        GST_VIDEO_INFO_HEIGHT (&download->info));

    download->priv->in_tex[0] =
        gst_gl_memory_wrapped_texture (download->context,
        texture_id, texture_target, &temp_info, 0, NULL, NULL, NULL);
  }

  download->priv->in_tex[0]->tex_id = texture_id;

  return _do_download (download, texture_id, data);
}

static gboolean
_init_download (GstGLDownload * download)
{
  GstVideoFormat v_format;
  guint out_width, out_height;
  GstVideoInfo in_info;
  GstCaps *in_caps, *out_caps;
  GstCapsFeatures *in_gl_features, *out_gl_features;
  gboolean res;

  v_format = GST_VIDEO_INFO_FORMAT (&download->info);
  out_width = GST_VIDEO_INFO_WIDTH (&download->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&download->info);

  if (download->initted)
    return TRUE;

  GST_TRACE ("initializing texture download for format %s",
      gst_video_format_to_string (v_format));

  if (USING_GLES2 (download->context) && !USING_GLES3 (download->context)) {
    /* GL_RGBA is the only officially supported texture format in GLES2 */
    if (v_format == GST_VIDEO_FORMAT_RGB || v_format == GST_VIDEO_FORMAT_BGR) {
      gst_gl_context_set_error (download->context, "Cannot download RGB "
          "textures in GLES2");
      return FALSE;
    }
  }

  in_gl_features =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
  gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_RGBA, out_width,
      out_height);
  in_caps = gst_video_info_to_caps (&in_info);
  gst_caps_set_features (in_caps, 0, in_gl_features);

  out_gl_features =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
  out_caps = gst_video_info_to_caps (&download->info);
  gst_caps_set_features (out_caps, 0, out_gl_features);

  res = gst_gl_color_convert_set_caps (download->convert, in_caps, out_caps);

  gst_caps_unref (in_caps);
  gst_caps_unref (out_caps);

  return res;
}

static gboolean
_do_download (GstGLDownload * download, guint texture_id,
    gpointer data[GST_VIDEO_MAX_PLANES])
{
  guint out_width, out_height;
  GstBuffer *inbuf, *outbuf;
  GstMapInfo map_info;
  gboolean ret = TRUE;
  gint i;

  out_width = GST_VIDEO_INFO_WIDTH (&download->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&download->info);

  if (!download->initted) {
    if (!_init_download (download))
      return FALSE;
  }

  GST_TRACE ("doing download of texture:%u (%ux%u)",
      download->priv->in_tex[0]->tex_id, out_width, out_height);

  inbuf = gst_buffer_new ();
  gst_buffer_append_memory (inbuf,
      gst_memory_ref ((GstMemory *) download->priv->in_tex[0]));

  outbuf = gst_gl_color_convert_perform (download->convert, inbuf);
  if (!outbuf)
    return FALSE;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&download->info); i++) {
    GstMemory *out_mem = gst_buffer_peek_memory (outbuf, i);
    gpointer temp_data = ((GstGLMemory *) out_mem)->data;
    ((GstGLMemory *) out_mem)->data = data[i];

    gst_gl_memory_download_transfer ((GstGLMemory *) out_mem);

    if (!gst_memory_map (out_mem, &map_info, GST_MAP_READ)) {
      GST_ERROR_OBJECT (download, "Failed to map memory");
      ret = FALSE;
    }
    gst_memory_unmap (out_mem, &map_info);
    ((GstGLMemory *) out_mem)->data = temp_data;
  }

  gst_buffer_unref (inbuf);
  gst_buffer_unref (outbuf);

  return ret;
}
