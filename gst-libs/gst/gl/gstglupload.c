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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "gstglupload.h"
#include "gstglmemory.h"

/**
 * SECTION:gstglupload
 * @short_description: an object that uploads to GL textures
 * @see_also: #GstGLDownload, #GstGLMemory
 *
 * #GstGLUpload is an object that uploads data from system memory into GL textures.
 *
 * A #GstGLUpload can be created with gst_gl_upload_new() or found with
 * gst_gl_display_find_upload().
 *
 * All of the _thread() variants should only be called within the GL thread
 * they don't try to take a lock on the associated #GstGLDisplay and
 * don't dispatch to the GL thread. Rather they run the required code in the
 * calling thread.
 */

static void _do_upload (GstGLDisplay * display, GstGLUpload * upload);
static void _do_upload_draw (GstGLDisplay * display, GstGLUpload * upload);
static void _do_upload_fill (GstGLDisplay * display, GstGLUpload * upload);
static void _do_upload_make (GstGLDisplay * display, GstGLUpload * upload);
static void _init_upload (GstGLDisplay * display, GstGLUpload * upload);
static void _init_upload_fbo (GstGLDisplay * display, GstGLUpload * upload);
static gboolean gst_gl_upload_perform_with_data_unlocked (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);
static gboolean gst_gl_upload_perform_with_data_unlocked_thread (GstGLUpload *
    upload, GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);

/* YUY2:r,g,a
   UYVY:a,b,r */
static gchar *text_shader_YUY2_UYVY =
#ifndef OPENGL_ES2
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect Ytex, UVtex;\n"
#else
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n" "uniform sampler2D Ytex, UVtex;\n"
#endif
    "void main(void) {\n" "  float fx, fy, y, u, v, r, g, b;\n"
#ifndef OPENGL_ES2
    "  fx = gl_TexCoord[0].x;\n"
    "  fy = gl_TexCoord[0].y;\n"
    "  y = texture2DRect(Ytex,vec2(fx,fy)).%c;\n"
    "  u = texture2DRect(UVtex,vec2(fx*0.5,fy)).%c;\n"
    "  v = texture2DRect(UVtex,vec2(fx*0.5,fy)).%c;\n"
#else
    "  fx = v_texCoord.x;\n"
    "  fy = v_texCoord.y;\n"
    "  y = texture2D(Ytex,vec2(fx,fy)).%c;\n"
    "  u = texture2D(UVtex,vec2(fx*0.5,fy)).%c;\n"
    "  v = texture2D(UVtex,vec2(fx*0.5,fy)).%c;\n"
#endif
    "  y=1.164*(y-0.0627);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r = y+1.5958*v;\n"
    "  g = y-0.39173*u-0.81290*v;\n"
    "  b = y+2.017*u;\n" "  gl_FragColor = vec4(r, g, b, 1.0);\n" "}\n";

/* ATI: "*0.5", ""
   normal: "", "*0.5" */
static gchar *text_shader_I420_YV12 =
#ifndef OPENGL_ES2
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect Ytex,Utex,Vtex;\n"
#else
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n" "uniform sampler2D Ytex,Utex,Vtex;\n"
#endif
    "void main(void) {\n" "  float r,g,b,y,u,v;\n"
#ifndef OPENGL_ES2
    "  vec2 nxy = gl_TexCoord[0].xy;\n"
    "  y=texture2DRect(Ytex,nxy%s).r;\n"
    "  u=texture2DRect(Utex,nxy%s).r;\n" "  v=texture2DRect(Vtex,nxy*0.5).r;\n"
#else
    "  vec2 nxy = v_texCoord.xy;\n"
    "  y=texture2D(Ytex,nxy).r;\n"
    "  u=texture2D(Utex,nxy).r;\n" "  v=texture2D(Vtex,nxy).r;\n"
#endif
    "  y=1.1643*(y-0.0625);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r=y+1.5958*v;\n"
    "  g=y-0.39173*u-0.81290*v;\n"
    "  b=y+2.017*u;\n" "  gl_FragColor=vec4(r,g,b,1.0);\n" "}\n";

static gchar *text_shader_AYUV =
#ifndef OPENGL_ES2
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect tex;\n"
#else
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n" "uniform sampler2D tex;\n"
#endif
    "void main(void) {\n" "  float r,g,b,y,u,v;\n"
#ifndef OPENGL_ES2
    "  vec2 nxy=gl_TexCoord[0].xy;\n"
    "  y=texture2DRect(tex,nxy).r;\n"
    "  u=texture2DRect(tex,nxy).g;\n" "  v=texture2DRect(tex,nxy).b;\n"
#else
    "  vec2 nxy = v_texCoord.xy;\n"
    "  y=texture2D(tex,nxy).g;\n"
    "  u=texture2D(tex,nxy).b;\n" "  v=texture2D(tex,nxy).a;\n"
#endif
    "  y=1.1643*(y-0.0625);\n"
    "  u=u-0.5;\n"
    "  v=v-0.5;\n"
    "  r=y+1.5958*v;\n"
    "  g=y-0.39173*u-0.81290*v;\n"
    "  b=y+2.017*u;\n" "  gl_FragColor=vec4(r,g,b,1.0);\n" "}\n";

#ifdef OPENGL_ES2
static gchar *text_vertex_shader =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texCoord;   \n"
    "varying vec2 v_texCoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = a_position; \n"
    "   v_texCoord = a_texCoord;  \n" "}                            \n";
#endif

GST_DEBUG_CATEGORY_STATIC (gst_gl_upload_debug);
#define GST_CAT_DEFAULT gst_gl_upload_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_upload_debug, "glupload", 0, "upload");

G_DEFINE_TYPE_WITH_CODE (GstGLUpload, gst_gl_upload, G_TYPE_OBJECT, DEBUG_INIT);
static void gst_gl_upload_finalize (GObject * object);

#define GST_GL_UPLOAD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_UPLOAD, GstGLUploadPrivate))

static void
gst_gl_upload_class_init (GstGLUploadClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_gl_upload_finalize;
}

static void
gst_gl_upload_init (GstGLUpload * upload)
{
  upload->display = NULL;

  g_mutex_init (&upload->lock);

  upload->fbo = 0;
  upload->depth_buffer = 0;
  upload->out_texture = 0;
  upload->shader = NULL;

#ifdef OPENGL_ES2
  upload->shader_attr_position_loc = 0;
  upload->shader_attr_texture_loc = 0;
#endif

  gst_video_info_init (&upload->info);
}

/**
 * gst_gl_upload_new:
 * @display: a #GstGLDisplay
 *
 * Returns: a new #GstGLUpload object
 */
GstGLUpload *
gst_gl_upload_new (GstGLDisplay * display)
{
  GstGLUpload *upload;

  upload = g_object_new (GST_TYPE_GL_UPLOAD, NULL);

  upload->display = g_object_ref (display);

  g_mutex_init (&upload->lock);

  return upload;
}

static void
gst_gl_upload_finalize (GObject * object)
{
  GstGLUpload *upload;
  guint i;

  upload = GST_GL_UPLOAD (object);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (upload->in_texture[i]) {
      gst_gl_display_del_texture (upload->display, &upload->in_texture[i]);
      upload->in_texture[i] = 0;
    }
  }
  if (upload->out_texture) {
    gst_gl_display_del_texture (upload->display, &upload->out_texture);
    upload->out_texture = 0;
  }
  if (upload->fbo || upload->depth_buffer) {
    gst_gl_display_del_fbo (upload->display, upload->fbo, upload->depth_buffer);
    upload->fbo = 0;
    upload->depth_buffer = 0;
  }
  if (upload->shader) {
    g_object_unref (G_OBJECT (upload->shader));
    upload->shader = NULL;
  }

  if (upload->display) {
    g_object_unref (G_OBJECT (upload->display));
    upload->display = NULL;
  }
}

/**
 * gst_gl_upload_init_format:
 * @upload: a #GstGLUpload
 * @v_format: a #GstVideoFormat
 * @in_width: the width of the data to upload
 * @in_height: the height of the data to upload
 * @out_width: the width to upload to
 * @out_height: the height to upload to
 *
 * Initializes @upload with the information required for upload.
 *
 * Returns: whether the initialization was successful
 */
gboolean
gst_gl_upload_init_format (GstGLUpload * upload, GstVideoFormat v_format,
    guint in_width, guint in_height, guint out_width, guint out_height)
{
  GstVideoInfo info;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_ENCODED, FALSE);
  g_return_val_if_fail (in_width > 0 && in_height > 0, FALSE);
  g_return_val_if_fail (out_width > 0 && out_height > 0, FALSE);

  g_mutex_lock (&upload->lock);

  if (upload->initted) {
    g_mutex_unlock (&upload->lock);
    return FALSE;
  } else {
    upload->initted = TRUE;
  }

  gst_video_info_set_format (&info, v_format, out_width, out_height);

  upload->info = info;
  upload->in_width = in_width;
  upload->in_height = in_height;

  gst_gl_display_thread_add (upload->display,
      (GstGLDisplayThreadFunc) _init_upload, upload);

  g_mutex_unlock (&upload->lock);

  return TRUE;
}

/**
 * gst_gl_upload_init_format:
 * @upload: a #GstGLUpload
 * @v_format: a #GstVideoFormat
 * @in_width: the width of the data to upload
 * @in_height: the height of the data to upload
 * @out_width: the width to upload to
 * @out_height: the height to upload to
 *
 * Initializes @upload with the information required for upload.
 *
 * Returns: whether the initialization was successful
 */
gboolean
gst_gl_upload_init_format_thread (GstGLUpload * upload, GstVideoFormat v_format,
    guint in_width, guint in_height, guint out_width, guint out_height)
{
  GstVideoInfo info;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_ENCODED, FALSE);
  g_return_val_if_fail (in_width > 0 && in_height > 0, FALSE);
  g_return_val_if_fail (out_width > 0 && out_height > 0, FALSE);

  g_mutex_lock (&upload->lock);

  if (upload->initted) {
    g_mutex_unlock (&upload->lock);
    return FALSE;
  } else {
    upload->initted = TRUE;
  }

  gst_video_info_set_format (&info, v_format, out_width, out_height);

  upload->info = info;
  upload->in_width = in_width;
  upload->in_height = in_height;

  _init_upload (upload->display, upload);

  g_mutex_unlock (&upload->lock);

  return TRUE;
}

/**
 * gst_gl_upload_perform_with_memory:
 * @upload: a #GstGLUpload
 * @gl_mem: a #GstGLMemory
 *
 * Uploads the texture in @gl_mem
 *
 * Returns: whether the upload was successful
 */
gboolean
gst_gl_upload_perform_with_memory (GstGLUpload * upload, GstGLMemory * gl_mem)
{
  gpointer data[GST_VIDEO_MAX_PLANES];
  guint i;
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_UPLOAD_INITTED))
    return FALSE;

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD))
    return FALSE;

  g_mutex_lock (&upload->lock);

  upload->in_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  upload->in_height = GST_VIDEO_INFO_HEIGHT (&upload->info);

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->info); i++) {
    data[i] = (guint8 *) gl_mem->data +
        GST_VIDEO_INFO_PLANE_OFFSET (&upload->info, i);
  }

  ret = gst_gl_upload_perform_with_data_unlocked (upload, gl_mem->tex_id, data);

  GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);

  g_mutex_unlock (&upload->lock);

  return ret;
}

/**
 * gst_gl_upload_perform_with_data:
 * @upload: a #GstGLUpload
 * @texture_id: the texture id to download
 * @data: where the downloaded data should go
 *
 * Uploads @data into @texture_id. @data size and format is specified by
 * the #GstVideoFormat passed to gst_gl_upload_init_format() 
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

  ret = gst_gl_upload_perform_with_data_unlocked (upload, texture_id, data);

  g_mutex_unlock (&upload->lock);

  return ret;
}

static inline gboolean
_perform_with_data_unlocked_pre (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  guint i;

  g_return_val_if_fail (upload != NULL, FALSE);
  g_return_val_if_fail (texture_id > 0, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&upload->info) !=
      GST_VIDEO_FORMAT_UNKNOWN
      && GST_VIDEO_INFO_FORMAT (&upload->info) != GST_VIDEO_FORMAT_ENCODED,
      FALSE);

  upload->out_texture = texture_id;
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->info); i++) {
    upload->data[i] = data[i];
  }

  return TRUE;
}

static gboolean
gst_gl_upload_perform_with_data_unlocked (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  if (!_perform_with_data_unlocked_pre (upload, texture_id, data))
    return FALSE;

  gst_gl_display_thread_add (upload->display,
      (GstGLDisplayThreadFunc) _do_upload, upload);

  return TRUE;
}

/**
 * gst_gl_upload_perform_with_memory_thread:
 * @upload: a #GstGLUpload
 * @gl_mem: a #GstGLMemory
 *
 * Uploads the texture in @gl_mem
 *
 * Returns: whether the upload was successful
 */
gboolean
gst_gl_upload_perform_with_memory_thread (GstGLUpload * upload,
    GstGLMemory * gl_mem)
{
  gpointer data[GST_VIDEO_MAX_PLANES];
  guint i;
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_UPLOAD_INITTED))
    return FALSE;

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD))
    return FALSE;

  g_mutex_lock (&upload->lock);

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&upload->info); i++) {
    data[i] = (guint8 *) gl_mem->data +
        GST_VIDEO_INFO_PLANE_OFFSET (&upload->info, i);
  }

  ret =
      gst_gl_upload_perform_with_data_unlocked_thread (upload, gl_mem->tex_id,
      data);

  GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);

  g_mutex_unlock (&upload->lock);

  return ret;
}

/**
 * gst_gl_upload_perform_with_data_thread:
 * @upload: a #GstGLUpload
 * @texture_id: the texture id to download
 * @data: where the downloaded data should go
 *
 * Uploads @data into @texture_id. @data size and format is specified by
 * the #GstVideoFormat passed to gst_gl_upload_init_format() 
 *
 * Returns: whether the upload was successful
 */
gboolean
gst_gl_upload_perform_with_data_thread (GstGLUpload * upload, GLuint texture_id,
    gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (upload != NULL, FALSE);

  g_mutex_lock (&upload->lock);

  ret =
      gst_gl_upload_perform_with_data_unlocked_thread (upload, texture_id,
      data);

  g_mutex_unlock (&upload->lock);

  return ret;
}

static gboolean
gst_gl_upload_perform_with_data_unlocked_thread (GstGLUpload * upload,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  if (!_perform_with_data_unlocked_pre (upload, texture_id, data))
    return FALSE;

  _do_upload (upload->display, upload);

  return TRUE;
}

/**
 * gst_gl_display_find_upload_unlocked:
 * @display: a #GstGLDisplay
 * @v_format: a #GstVideoFormat
 * @in_width: the width of the data to upload
 * @in_height: the height of the data to upload
 * @out_width: the width to upload to
 * @out_height: the height to upload to
 *
 * Finds a #GstGLDownload with the required upload settings, creating one
 * if needed.  The returned object may not be initialized so you still
 * have to call gst_gl_upload_init_format.
 *
 * This function is safe to be called in the GL thread
 *
 * Returns: a #GstGLUpload object with the required settings
 */
GstGLUpload *
gst_gl_display_find_upload_unlocked (GstGLDisplay * display,
    GstVideoFormat v_format, guint in_width, guint in_height,
    guint out_width, guint out_height)
{
  GstGLUpload *ret;
  GSList *walk;

  walk = display->uploads;

  while (walk) {
    ret = walk->data;

    if (ret && v_format == GST_VIDEO_INFO_FORMAT (&ret->info) &&
        out_width == GST_VIDEO_INFO_WIDTH (&ret->info) &&
        out_height == GST_VIDEO_INFO_HEIGHT (&ret->info) &&
        in_width == ret->in_width && in_height == ret->in_height)
      break;

    ret = NULL;
    walk = g_slist_next (walk);
  }

  if (!ret) {
    ret = gst_gl_upload_new (display);

    display->uploads = g_slist_prepend (display->uploads, ret);
  }

  return ret;
}

/**
 * gst_gl_display_find_upload:
 * @display: a #GstGLDisplay
 * @v_format: a #GstVideoFormat
 * @in_width: the width of the data to upload
 * @in_height: the height of the data to upload
 * @out_width: the width to upload to
 * @out_height: the height to upload to
 *
 * Finds a #GstGLDownload with the required upload settings, creating one
 * if needed.  The returned object may not be initialized so you still
 * have to call gst_gl_upload_init_format.
 *
 * Returns: a #GstGLUpload object with the required settings
 */
GstGLUpload *
gst_gl_display_find_upload (GstGLDisplay * display, GstVideoFormat v_format,
    guint in_width, guint in_height, guint out_width, guint out_height)
{
  GstGLUpload *ret;

  gst_gl_display_lock (display);

  ret =
      gst_gl_display_find_upload_unlocked (display, v_format, in_width,
      in_height, out_width, out_height);

  gst_gl_display_unlock (display);

  return ret;
}

/* Called in the gl thread */
void
_init_upload (GstGLDisplay * display, GstGLUpload * upload)
{
  GstVideoFormat v_format;
  guint in_width, in_height, out_width, out_height;

  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);
  in_width = upload->in_width;
  in_height = upload->in_height;
  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);

  GST_TRACE ("initializing texture upload for format:%d", v_format);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      if (in_width != out_width || in_height != out_height)
        _init_upload_fbo (display, upload);
      /* color space conversion is not needed */
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
      /* color space conversion is needed */
    {
      /* check if fragment shader is available, then load them */
      /* shouldn't we require ARB_shading_language_100? --Filippo */
      if (GLEW_ARB_fragment_shader) {

#ifdef OPENGL_ES2
        GError *error = NULL;
#endif

        GST_INFO ("Context, ARB_fragment_shader supported: yes");

        display->colorspace_conversion = GST_GL_DISPLAY_CONVERSION_GLSL;

        _init_upload_fbo (display, upload);

        switch (v_format) {
          case GST_VIDEO_FORMAT_YUY2:
          {
            gchar text_shader_YUY2[2048];
            sprintf (text_shader_YUY2, text_shader_YUY2_UYVY, 'r', 'g', 'a');

            upload->shader = gst_gl_shader_new ();
#ifndef OPENGL_ES2
            if (!gst_gl_shader_compile_and_check (upload->shader,
                    text_shader_YUY2, GST_GL_SHADER_FRAGMENT_SOURCE)) {
              gst_gl_display_set_error (display,
                  "Failed to initialize shader for uploading YUY2");
              g_object_unref (G_OBJECT (upload->shader));
              upload->shader = NULL;
            }
#else
            gst_gl_shader_set_vertex_source (upload->shader,
                text_vertex_shader);
            gst_gl_shader_set_fragment_source (upload->shader,
                text_shader_YUY2);

            gst_gl_shader_compile (upload->shader, &error);
            if (error) {
              gst_gl_display_set_error (display, "%s", error->message);
              g_error_free (error);
              error = NULL;
              gst_gl_shader_use (NULL);
              g_object_unref (G_OBJECT (upload->shader));
              upload->shader = NULL;
            } else {
              upload->shader_attr_position_loc =
                  gst_gl_shader_get_attribute_location
                  (upload->shader, "a_position");
              upload->shader_attr_texture_loc =
                  gst_gl_shader_get_attribute_location
                  (upload->shader, "a_texCoord");
            }
#endif
          }
            break;
          case GST_VIDEO_FORMAT_UYVY:
          {
            gchar text_shader_UYVY[2048];
            sprintf (text_shader_UYVY,
#ifndef OPENGL_ES2
                text_shader_YUY2_UYVY, 'a', 'b', 'r');
#else
                text_shader_YUY2_UYVY, 'a', 'r', 'b');
#endif

            upload->shader = gst_gl_shader_new ();

#ifndef OPENGL_ES2
            if (!gst_gl_shader_compile_and_check (upload->shader,
                    text_shader_UYVY, GST_GL_SHADER_FRAGMENT_SOURCE)) {
              gst_gl_display_set_error (display,
                  "Failed to initialize shader for uploading UYVY");
              g_object_unref (G_OBJECT (upload->shader));
              upload->shader = NULL;
            }
#else
            gst_gl_shader_set_vertex_source (upload->shader,
                text_vertex_shader);
            gst_gl_shader_set_fragment_source (upload->shader,
                text_shader_UYVY);

            gst_gl_shader_compile (upload->shader, &error);
            if (error) {
              gst_gl_display_set_error (display, "%s", error->message);
              g_error_free (error);
              error = NULL;
              gst_gl_shader_use (NULL);
              g_object_unref (G_OBJECT (upload->shader));
              upload->shader = NULL;
            } else {
              upload->shader_attr_position_loc =
                  gst_gl_shader_get_attribute_location
                  (upload->shader, "a_position");
              upload->shader_attr_texture_loc =
                  gst_gl_shader_get_attribute_location
                  (upload->shader, "a_texCoord");
            }
#endif
          }
            break;
          case GST_VIDEO_FORMAT_I420:
          case GST_VIDEO_FORMAT_YV12:
          {
#ifndef OPENGL_ES2
            gchar text_shader[2048];
            if ((g_ascii_strncasecmp ("ATI", (gchar *) glGetString (GL_VENDOR),
                        3) == 0)
                && (g_ascii_strncasecmp ("ATI Mobility Radeon HD",
                        (gchar *) glGetString (GL_RENDERER), 22) != 0)
                && (g_ascii_strncasecmp ("ATI Radeon HD",
                        (gchar *) glGetString (GL_RENDERER), 13) != 0))
              sprintf (text_shader, text_shader_I420_YV12, "*0.5", "");
            else
              sprintf (text_shader, text_shader_I420_YV12, "", "*0.5");
#endif

            upload->shader = gst_gl_shader_new ();

#ifndef OPENGL_ES2
            if (!gst_gl_shader_compile_and_check
                (upload->shader, text_shader, GST_GL_SHADER_FRAGMENT_SOURCE)) {
              gst_gl_display_set_error (display,
                  "Failed to initialize shader for uploading I420 or YV12");
              g_object_unref (G_OBJECT (upload->shader));
              upload->shader = NULL;
            }
#else
            gst_gl_shader_set_vertex_source (upload->shader,
                text_vertex_shader);
            gst_gl_shader_set_fragment_source (upload->shader,
                text_shader_I420_YV12);

            gst_gl_shader_compile (upload->shader, &error);
            if (error) {
              gst_gl_display_set_error (display, "%s", error->message);
              g_error_free (error);
              error = NULL;
              gst_gl_shader_use (NULL);
              g_object_unref (G_OBJECT (upload->shader));
              upload->shader = NULL;
            } else {
              upload->shader_attr_position_loc =
                  gst_gl_shader_get_attribute_location
                  (upload->shader, "a_position");
              upload->shader_attr_texture_loc =
                  gst_gl_shader_get_attribute_location
                  (upload->shader, "a_texCoord");
            }
#endif
          }
            break;
          case GST_VIDEO_FORMAT_AYUV:
          {
            upload->shader = gst_gl_shader_new ();

#ifndef OPENGL_ES2
            if (!gst_gl_shader_compile_and_check (upload->shader,
                    text_shader_AYUV, GST_GL_SHADER_FRAGMENT_SOURCE)) {
              gst_gl_display_set_error (display,
                  "Failed to initialize shader for uploading AYUV");
              g_object_unref (G_OBJECT (upload->shader));
              upload->shader = NULL;
            }
#else
            gst_gl_shader_set_vertex_source (upload->shader,
                text_vertex_shader);
            gst_gl_shader_set_fragment_source (upload->shader,
                text_shader_AYUV);

            gst_gl_shader_compile (upload->shader, &error);
            if (error) {
              gst_gl_display_set_error (display, "%s", error->message);
              g_error_free (error);
              error = NULL;
              gst_gl_shader_use (NULL);
              g_object_unref (G_OBJECT (upload->shader));
              upload->shader = NULL;
            } else {
              upload->shader_attr_position_loc =
                  gst_gl_shader_get_attribute_location
                  (upload->shader, "a_position");
              upload->shader_attr_texture_loc =
                  gst_gl_shader_get_attribute_location
                  (upload->shader, "a_texCoord");
            }
#endif
          }
            break;
          default:
            gst_gl_display_set_error (display,
                "Unsupported upload video format %d", v_format);
            break;
        }
      }
      /* check if YCBCR MESA is available */
      else if (GLEW_MESA_ycbcr_texture) {
        /* GLSL and Color Matrix are not available on your drivers,
         * switch to YCBCR MESA
         */
        GST_INFO ("Context, ARB_fragment_shader supported: no");
        GST_INFO ("Context, GLEW_MESA_ycbcr_texture supported: yes");

        display->colorspace_conversion = GST_GL_DISPLAY_CONVERSION_MESA;

        switch (v_format) {
          case GST_VIDEO_FORMAT_YUY2:
          case GST_VIDEO_FORMAT_UYVY:
            /* color space conversion is not needed */
            break;
          case GST_VIDEO_FORMAT_I420:
          case GST_VIDEO_FORMAT_YV12:
          case GST_VIDEO_FORMAT_AYUV:
            /* MESA only supports YUY2 and UYVY */
            gst_gl_display_set_error (display,
                "Your MESA version only supports YUY2 and UYVY (GLSL is required for others yuv formats)");
            break;
          default:
            gst_gl_display_set_error (display,
                "Unsupported upload video format %d", v_format);
            break;
        }
      }
      /* check if color matrix is available (not supported) */
      else if (GLEW_ARB_imaging) {
        /* GLSL is not available on your drivers, switch to Color Matrix */
        GST_INFO ("Context, ARB_fragment_shader supported: no");
        GST_INFO ("Context, GLEW_MESA_ycbcr_texture supported: no");
        GST_INFO ("Context, GLEW_ARB_imaging supported: yes");

        display->colorspace_conversion = GST_GL_DISPLAY_CONVERSION_MATRIX;

        gst_gl_display_set_error (display,
            "Colorspace conversion using Color Matrix is not yet supported");
      } else {
        /* colorspace conversion is not possible */
        gst_gl_display_set_error (display,
            "ARB_fragment_shader supported, GLEW_ARB_imaging supported, GLEW_MESA_ycbcr_texture supported, not supported");
      }
    }
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      break;
  }
}


/* called by _init_upload (in the gl thread) */
void
_init_upload_fbo (GstGLDisplay * display, GstGLUpload * upload)
{
  guint out_width, out_height;
  GLuint fake_texture = 0;      /* a FBO must hava texture to init */

  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);

  if (!GLEW_EXT_framebuffer_object) {
    /* turn off the pipeline because Frame buffer object is a not present */
    gst_gl_display_set_error (display,
        "Context, EXT_framebuffer_object supported: no");
    return;
  }

  GST_INFO ("Context, EXT_framebuffer_object supported: yes");

  /* setup FBO */
  glGenFramebuffersEXT (1, &upload->fbo);
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, upload->fbo);

  /* setup the render buffer for depth */
  glGenRenderbuffersEXT (1, &upload->depth_buffer);
  glBindRenderbufferEXT (GL_RENDERBUFFER_EXT, upload->depth_buffer);
#ifndef OPENGL_ES2
  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
      out_width, out_height);
  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT,
      out_width, out_height);
#else
  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT16,
      out_width, out_height);
#endif

  /* a fake texture is attached to the upload FBO (cannot init without it) */
  glGenTextures (1, &fake_texture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, fake_texture);
  glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, out_width, out_height, 0,
      GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);

  /* attach the texture to the FBO to renderer to */
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, fake_texture, 0);

  /* attach the depth render buffer to the FBO */
  glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
      GL_RENDERBUFFER_EXT, upload->depth_buffer);

#ifndef OPENGL_ES2
  glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT,
      GL_RENDERBUFFER_EXT, upload->depth_buffer);
#endif

  gst_gl_display_check_framebuffer_status ();

  if (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) !=
      GL_FRAMEBUFFER_COMPLETE_EXT)
    gst_gl_display_set_error (display, "GL framebuffer status incomplete");

  /* unbind the FBO */
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);

  glDeleteTextures (1, &fake_texture);

  _do_upload_make (display, upload);
}

/* Called by the idle function in the gl thread */
void
_do_upload (GstGLDisplay * display, GstGLUpload * upload)
{
  GstVideoFormat v_format;
  guint in_width, in_height, out_width, out_height;

  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);
  in_width = upload->in_width;
  in_height = upload->in_height;

  GST_TRACE ("uploading texture:%u dimensions: %ux%u", upload->out_texture,
      out_width, out_height);

  _do_upload_fill (display, upload);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      if (in_width != out_width || in_height != out_height)
        _do_upload_draw (display, upload);
      /* color space conversion is not needed */
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
    {
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
          /* color space conversion is needed */
          _do_upload_draw (display, upload);
          break;
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
          /* color space conversion is needed */
          /* not yet supported */
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          if (in_width != out_width || in_height != out_height)
            _do_upload_draw (display, upload);
          /* color space conversion is not needed */
          break;
        default:
          gst_gl_display_set_error (display, "Unknown colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
    }
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }
}

/* called by gst_gl_display_thread_do_upload (in the gl thread) */
void
_do_upload_make (GstGLDisplay * display, GstGLUpload * upload)
{
  GstVideoFormat v_format;
  guint in_width, in_height;

  in_width = upload->in_width;
  in_height = upload->in_height;
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);

  glGenTextures (1, &upload->in_texture[0]);

  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          in_width, in_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB,
          in_width, in_height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          in_width, in_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, NULL);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
          glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE_ALPHA,
              in_width, in_height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
              NULL);
          glGenTextures (1, &upload->in_texture[1]);
          glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
              in_width, in_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
              NULL);
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, in_width,
              in_height, 0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
          break;
        default:
          gst_gl_display_set_error (display, "Unknown colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
      break;
    case GST_VIDEO_FORMAT_UYVY:
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
          glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE_ALPHA,
              in_width, in_height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
              NULL);
          glGenTextures (1, &upload->in_texture[1]);
          glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
              in_width, in_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
              NULL);
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, in_width,
              in_height, 0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
          break;
        default:
          gst_gl_display_set_error (display, "Unknown colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          in_width, in_height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

      glGenTextures (1, &upload->in_texture[1]);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          GST_ROUND_UP_2 (in_width) / 2,
          GST_ROUND_UP_2 (in_height) / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          NULL);

      glGenTextures (1, &upload->in_texture[2]);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          GST_ROUND_UP_2 (in_width) / 2,
          GST_ROUND_UP_2 (in_height) / 2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          NULL);
      break;

    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }
}


/* called by gst_gl_display_thread_do_upload (in the gl thread) */
void
_do_upload_fill (GstGLDisplay * display, GstGLUpload * upload)
{
  GstVideoFormat v_format;
  guint in_width, in_height, out_width, out_height;

  in_width = upload->in_width;
  in_height = upload->in_height;
  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
      /* color space conversion is not needed */
      if (in_width != out_width || in_height != out_height)
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      else
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->out_texture);
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
          glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          if (in_width != out_width || in_height != out_height)
            glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
          else
            glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->out_texture);
          break;
        default:
          gst_gl_display_set_error (display, "Unknown colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGB:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_RGB, GL_UNSIGNED_BYTE, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_BGR:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_BGR, GL_UNSIGNED_BYTE, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_RGBA, GL_UNSIGNED_BYTE, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_BGRA, GL_UNSIGNED_BYTE, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, upload->data[0]);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
          glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width,
              in_height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, upload->data[0]);

          glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
              GST_ROUND_UP_2 (in_width) / 2, in_height,
              GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, upload->data[0]);
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width,
              in_height, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA,
              upload->data[0]);
          break;
        default:
          gst_gl_display_set_error (display, "Unknow colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
      break;
    case GST_VIDEO_FORMAT_UYVY:
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
          glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width,
              in_height, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, upload->data[0]);

          glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
              GST_ROUND_UP_2 (in_width) / 2, in_height,
              GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, upload->data[0]);
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
          glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width,
              in_height, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA,
              upload->data[0]);
          break;
        default:
          gst_gl_display_set_error (display, "Unknow colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
      break;
    case GST_VIDEO_FORMAT_I420:
    {
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[0]);

      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, GST_ROUND_UP_2 (in_height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[1]);

      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, GST_ROUND_UP_2 (in_height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[2]);
    }
      break;
    case GST_VIDEO_FORMAT_YV12:        /* same as I420 except plane 1+2 swapped */
    {
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, in_width, in_height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[0]);

      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, GST_ROUND_UP_2 (in_height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[1]);

      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (in_width) / 2, GST_ROUND_UP_2 (in_height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, upload->data[2]);
    }
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }

  /* make sure no texture is in use in our opengl context 
   * in case we want to use the upload texture in an other opengl context
   */
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
}


/* called by _do_upload (in the gl thread) */
void
_do_upload_draw (GstGLDisplay * display, GstGLUpload * upload)
{
  GstVideoFormat v_format;
  guint out_width, out_height;

#ifndef OPENGL_ES2
  guint in_width, in_height;
#else
  GLint viewport_dim[4];

  const GLfloat vVertices[] = { 1.0f, -1.0f, 0.0f,
    1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f,
    0.0f, .0f,
    -1.0f, 1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f, 0.0f,
    1.0f, 1.0f
  };

  GLushort indices[] = { 0, 1, 2, 0, 2, 3 };
#endif

  out_width = GST_VIDEO_INFO_WIDTH (&upload->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&upload->info);
  v_format = GST_VIDEO_INFO_FORMAT (&upload->info);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, upload->fbo);

  /* setup a texture to render to */
#ifndef OPENGL_ES2
  in_width = upload->in_width;
  in_height = upload->in_height;

  glEnable (GL_TEXTURE_RECTANGLE_ARB);
#endif
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->out_texture);

  /* attach the texture to the FBO to renderer to */
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, upload->out_texture, 0);

  if (GLEW_ARB_fragment_shader)
    gst_gl_shader_use (NULL);

#ifndef OPENGL_ES2
  glPushAttrib (GL_VIEWPORT_BIT);

  glMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();
  gluOrtho2D (0.0, out_width, 0.0, out_height);

  glMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity ();
#else /* OPENGL_ES2 */
  glGetIntegerv (GL_VIEWPORT, viewport_dim);
#endif

  glViewport (0, 0, out_width, out_height);

#ifndef OPENGL_ES2
  glDrawBuffer (GL_COLOR_ATTACHMENT0_EXT);
#endif

  glClearColor (0.0, 0.0, 0.0, 0.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    {
#ifndef OPENGL_ES2
      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();
#else
      glVertexAttribPointer (upload->shader_attr_position_loc, 3,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
      glVertexAttribPointer (upload->shader_attr_texture_loc, 2,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

      glEnableVertexAttribArray (upload->shader_attr_position_loc);
      glEnableVertexAttribArray (upload->shader_attr_texture_loc);
#endif

#ifndef OPENGL_ES2
      glEnable (GL_TEXTURE_RECTANGLE_ARB);
#endif
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);
#ifndef OPENGL_ES2
      glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
#endif
    }
      break;

    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    {
      switch (display->colorspace_conversion) {
        case GST_GL_DISPLAY_CONVERSION_GLSL:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
        {
          gst_gl_shader_use (upload->shader);

#ifndef OPENGL_ES2
          glMatrixMode (GL_PROJECTION);
          glLoadIdentity ();
#else
          glVertexAttribPointer (upload->shader_attr_position_loc, 3,
              GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
          glVertexAttribPointer (upload->shader_attr_texture_loc, 2,
              GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

          glEnableVertexAttribArray (upload->shader_attr_position_loc);
          glEnableVertexAttribArray (upload->shader_attr_texture_loc);
#endif

          glActiveTextureARB (GL_TEXTURE1_ARB);
          gst_gl_shader_set_uniform_1i (upload->shader, "UVtex", 1);
          glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
              GL_LINEAR);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
              GL_LINEAR);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
              GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
              GL_CLAMP_TO_EDGE);

          glActiveTextureARB (GL_TEXTURE0_ARB);
          gst_gl_shader_set_uniform_1i (upload->shader, "Ytex", 0);
          glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
              GL_LINEAR);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
              GL_LINEAR);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
              GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
              GL_CLAMP_TO_EDGE);
        }
          break;
        case GST_GL_DISPLAY_CONVERSION_MESA:
        {

#ifndef OPENGL_ES2
          glMatrixMode (GL_PROJECTION);
          glLoadIdentity ();
          glEnable (GL_TEXTURE_RECTANGLE_ARB);
#endif
          glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
              GL_LINEAR);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
              GL_LINEAR);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
              GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
              GL_CLAMP_TO_EDGE);
#ifndef OPENGL_ES2
          glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
#endif
        }
          break;
        default:
          gst_gl_display_set_error (display, "Unknow colorspace conversion %d",
              display->colorspace_conversion);
          g_assert_not_reached ();
          break;
      }
    }
      break;

    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    {
      gst_gl_shader_use (upload->shader);

#ifndef OPENGL_ES2
      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();
#else
      glVertexAttribPointer (upload->shader_attr_position_loc, 3,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
      glVertexAttribPointer (upload->shader_attr_texture_loc, 2,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

      glEnableVertexAttribArray (upload->shader_attr_position_loc);
      glEnableVertexAttribArray (upload->shader_attr_texture_loc);
#endif

      glActiveTextureARB (GL_TEXTURE1_ARB);
      gst_gl_shader_set_uniform_1i (upload->shader, "Utex", 1);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[1]);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);

      glActiveTextureARB (GL_TEXTURE2_ARB);
      gst_gl_shader_set_uniform_1i (upload->shader, "Vtex", 2);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[2]);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);

      glActiveTextureARB (GL_TEXTURE0_ARB);
      gst_gl_shader_set_uniform_1i (upload->shader, "Ytex", 0);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);
    }
      break;

    case GST_VIDEO_FORMAT_AYUV:
    {
      gst_gl_shader_use (upload->shader);

#ifndef OPENGL_ES2
      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();
#else
      glVertexAttribPointer (upload->shader_attr_position_loc, 3,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
      glVertexAttribPointer (upload->shader_attr_texture_loc, 2,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

      glEnableVertexAttribArray (upload->shader_attr_position_loc);
      glEnableVertexAttribArray (upload->shader_attr_texture_loc);
#endif

      glActiveTextureARB (GL_TEXTURE0_ARB);
      gst_gl_shader_set_uniform_1i (upload->shader, "tex", 0);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, upload->in_texture[0]);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);
    }
      break;

    default:
      gst_gl_display_set_error (display, "Unsupported upload video format %d",
          v_format);
      g_assert_not_reached ();
      break;

  }                             /* end switch display->currentVideo_format */

#ifndef OPENGL_ES2
  glBegin (GL_QUADS);
  glTexCoord2i (in_width, 0);
  glVertex2f (1.0f, -1.0f);
  glTexCoord2i (0, 0);
  glVertex2f (-1.0f, -1.0f);
  glTexCoord2i (0, in_height);
  glVertex2f (-1.0f, 1.0f);
  glTexCoord2i (in_width, in_height);
  glVertex2f (1.0f, 1.0f);
  glEnd ();

  glDrawBuffer (GL_NONE);
#else /* OPENGL_ES2 */
  glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
#endif

  /* we are done with the shader */
  if (display->colorspace_conversion == GST_GL_DISPLAY_CONVERSION_GLSL)
    glUseProgramObjectARB (0);

#ifndef OPENGL_ES2
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  glMatrixMode (GL_PROJECTION);
  glPopMatrix ();
  glMatrixMode (GL_MODELVIEW);
  glPopMatrix ();
  glPopAttrib ();
#else
  glViewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);
#endif

  gst_gl_display_check_framebuffer_status ();

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
}
