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

#include "gstgldownload.h"
#include "gstglmemory.h"

static void _do_download (GstGLDisplay * display, GstGLDownload * download);
static void _do_download_draw_rgb (GstGLDisplay * display,
    GstGLDownload * download);
static void _do_download_draw_yuv (GstGLDisplay * display,
    GstGLDownload * download);
static void _init_download (GstGLDisplay * display, GstGLDownload * download);
static void _init_download_shader (GstGLDisplay * display,
    GstGLDownload * download);
static gboolean gst_gl_download_perform_with_data_unlocked (GstGLDownload *
    download, GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);
static gboolean gst_gl_download_perform_with_data_unlocked_thread (GstGLDownload
    * download, GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);

/* YUY2:y2,u,y1,v
   UYVY:v,y1,u,y2 */
static gchar *text_shader_download_YUY2_UYVY =
#ifndef OPENGL_ES2
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect tex;\n"
#else
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n" "uniform sampler2D tex;\n"
#endif
    "void main(void) {\n" "  float fx,fy,r,g,b,r2,g2,b2,y1,y2,u,v;\n"
#ifndef OPENGL_ES2
    "  fx = gl_TexCoord[0].x;\n"
    "  fy = gl_TexCoord[0].y;\n"
    "  r=texture2DRect(tex,vec2(fx*2.0,fy)).r;\n"
    "  g=texture2DRect(tex,vec2(fx*2.0,fy)).g;\n"
    "  b=texture2DRect(tex,vec2(fx*2.0,fy)).b;\n"
    "  r2=texture2DRect(tex,vec2(fx*2.0+1.0,fy)).r;\n"
    "  g2=texture2DRect(tex,vec2(fx*2.0+1.0,fy)).g;\n"
    "  b2=texture2DRect(tex,vec2(fx*2.0+1.0,fy)).b;\n"
#else
    "  fx = v_texCoord.x;\n"
    "  fy = v_texCoord.y;\n"
    "  r=texture2D(tex,vec2(fx*2.0,fy)).r;\n"
    "  g=texture2D(tex,vec2(fx*2.0,fy)).g;\n"
    "  b=texture2D(tex,vec2(fx*2.0,fy)).b;\n"
    "  r2=texture2D(tex,vec2(fx*2.0+1.0,fy)).r;\n"
    "  g2=texture2D(tex,vec2(fx*2.0+1.0,fy)).g;\n"
    "  b2=texture2D(tex,vec2(fx*2.0+1.0,fy)).b;\n"
#endif
    "  y1=0.299011*r + 0.586987*g + 0.114001*b;\n"
    "  y2=0.299011*r2 + 0.586987*g2 + 0.114001*b2;\n"
    "  u=-0.148246*r -0.29102*g + 0.439266*b;\n"
    "  v=0.439271*r - 0.367833*g - 0.071438*b ;\n"
    "  y1=0.858885*y1 + 0.0625;\n"
    "  y2=0.858885*y2 + 0.0625;\n"
    "  u=u + 0.5;\n" "  v=v + 0.5;\n" "  gl_FragColor=vec4(%s);\n" "}\n";

/* no OpenGL ES 2.0 support because for now it's not possible
 * to attach multiple textures to a frame buffer object
 */
static gchar *text_shader_download_I420_YV12 =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect tex;\n"
    "uniform float w, h;\n"
    "void main(void) {\n"
    "  float r,g,b,r2,b2,g2,y,u,v;\n"
    "  vec2 nxy=gl_TexCoord[0].xy;\n"
    "  vec2 nxy2=nxy*2.0;\n"
    "  r=texture2DRect(tex,nxy).r;\n"
    "  g=texture2DRect(tex,nxy).g;\n"
    "  b=texture2DRect(tex,nxy).b;\n"
    "  r2=texture2DRect(tex,nxy2).r;\n"
    "  g2=texture2DRect(tex,nxy2).g;\n"
    "  b2=texture2DRect(tex,nxy2).b;\n"
    "  y=0.299011*r + 0.586987*g + 0.114001*b;\n"
    "  u=-0.148246*r2 -0.29102*g2 + 0.439266*b2;\n"
    "  v=0.439271*r2 - 0.367833*g2 - 0.071438*b2 ;\n"
    "  y=0.858885*y + 0.0625;\n"
    "  u=u + 0.5;\n"
    "  v=v + 0.5;\n"
    "  gl_FragData[0] = vec4(y, 0.0, 0.0, 1.0);\n"
    "  gl_FragData[1] = vec4(u, 0.0, 0.0, 1.0);\n"
    "  gl_FragData[2] = vec4(v, 0.0, 0.0, 1.0);\n" "}\n";

static gchar *text_shader_download_AYUV =
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
    "  r=texture2DRect(tex,nxy).r;\n"
    "  g=texture2DRect(tex,nxy).g;\n" "  b=texture2DRect(tex,nxy).b;\n"
#else
    "  vec2 nxy=v_texCoord.xy;\n"
    "  r=texture2D(tex,nxy).r;\n"
    "  g=texture2D(tex,nxy).g;\n" "  b=texture2D(tex,nxy).b;\n"
#endif
    "  y=0.299011*r + 0.586987*g + 0.114001*b;\n"
    "  u=-0.148246*r -0.29102*g + 0.439266*b;\n"
    "  v=0.439271*r - 0.367833*g - 0.071438*b ;\n"
    "  y=0.858885*y + 0.0625;\n" "  u=u + 0.5;\n" "  v=v + 0.5;\n"
#ifndef OPENGL_ES2
    "  gl_FragColor=vec4(y,u,v,1.0);\n"
#else
    "  gl_FragColor=vec4(1.0,y,u,v);\n"
#endif
    "}\n";

#ifdef OPENGL_ES2
static gchar *text_vertex_shader_download =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texCoord;   \n"
    "varying vec2 v_texCoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = a_position; \n"
    "   v_texCoord = a_texCoord;  \n" "}                            \n";

static gchar *text_fragment_shader_download_RGB =
    "precision mediump float;                            \n"
    "varying vec2 v_texCoord;                            \n"
    "uniform sampler2D s_texture;                        \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
    "}                                                   \n";
#endif

GST_DEBUG_CATEGORY_STATIC (gst_gl_download_debug);
#define GST_CAT_DEFAULT gst_gl_download_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_download_debug, "gldownload", 0, "download");

G_DEFINE_TYPE_WITH_CODE (GstGLDownload, gst_gl_download, G_TYPE_OBJECT,
    DEBUG_INIT);
static void gst_gl_download_finalize (GObject * object);

#define GST_GL_DOWNLOAD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GST_TYPE_GL_DOWNLOAD, GstGLDownloadPrivate))

static void
gst_gl_download_class_init (GstGLDownloadClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_gl_download_finalize;
}

static void
gst_gl_download_init (GstGLDownload * download)
{
  download->display = NULL;

  g_mutex_init (&download->lock);

  download->fbo = 0;
  download->depth_buffer = 0;
  download->in_texture = 0;
  download->shader = NULL;

#ifdef OPENGL_ES2
  download->shader_attr_position_loc = 0;
  download->shader_attr_texture_loc = 0;
#endif

  gst_video_info_init (&download->info);
}

GstGLDownload *
gst_gl_download_new (GstGLDisplay * display)
{
  GstGLDownload *download;

  download = g_object_new (GST_TYPE_GL_DOWNLOAD, NULL);

  download->display = g_object_ref (display);

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
      gst_gl_display_del_texture (download->display, &download->out_texture[i]);
      download->out_texture[i] = 0;
    }
  }
  if (download->in_texture) {
    gst_gl_display_del_texture (download->display, &download->in_texture);
    download->in_texture = 0;
  }
  if (download->fbo || download->depth_buffer) {
    gst_gl_display_del_fbo (download->display, download->fbo,
        download->depth_buffer);
    download->fbo = 0;
    download->depth_buffer = 0;
  }
  if (download->shader) {
    g_object_unref (G_OBJECT (download->shader));
    download->shader = NULL;
  }

  if (download->display) {
    g_object_unref (G_OBJECT (download->display));
    download->display = NULL;
  }

  g_mutex_clear (&download->lock);
}

static inline gboolean
_init_format_pre (GstGLDownload * download, GstVideoFormat v_format,
    guint width, guint height)
{
  g_return_val_if_fail (download != NULL, FALSE);
  g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);
  g_return_val_if_fail (v_format != GST_VIDEO_FORMAT_ENCODED, FALSE);
  g_return_val_if_fail (width > 0 && height > 0, FALSE);

  return TRUE;
}

gboolean
gst_gl_download_init_format (GstGLDownload * download, GstVideoFormat v_format,
    guint width, guint height)
{
  GstVideoInfo info;

  if (!_init_format_pre (download, v_format, width, height))
    return FALSE;

  g_mutex_lock (&download->lock);

  if (download->initted) {
    g_mutex_unlock (&download->lock);
    return FALSE;
  } else {
    download->initted = TRUE;
  }

  gst_video_info_set_format (&info, v_format, width, height);

  download->info = info;

  gst_gl_display_thread_add (download->display,
      (GstGLDisplayThreadFunc) _init_download, download);

  g_mutex_unlock (&download->lock);

  return TRUE;
}

gboolean
gst_gl_download_init_format_thread (GstGLDownload * download,
    GstVideoFormat v_format, guint width, guint height)
{
  GstVideoInfo info;

  if (!_init_format_pre (download, v_format, width, height))
    return FALSE;

  g_mutex_lock (&download->lock);

  if (download->initted) {
    g_mutex_unlock (&download->lock);
    return FALSE;
  } else {
    download->initted = TRUE;
  }

  gst_video_info_set_format (&info, v_format, width, height);

  download->info = info;

  _init_download (download->display, download);

  g_mutex_unlock (&download->lock);

  return TRUE;
}

static inline gboolean
_perform_with_memory_pre (GstGLDownload * download, GstGLMemory * gl_mem)
{
  g_return_val_if_fail (download != NULL, FALSE);

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_DOWNLOAD_INITTED))
    return FALSE;

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD)) {
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_gl_download_perform_with_memory (GstGLDownload * download,
    GstGLMemory * gl_mem)
{
  gpointer data[GST_VIDEO_MAX_PLANES];
  guint i;
  gboolean ret;

  if (!_perform_with_memory_pre (download, gl_mem))
    return FALSE;

  g_mutex_lock (&download->lock);

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&download->info); i++) {
    data[i] = (guint8 *) gl_mem->data +
        GST_VIDEO_INFO_PLANE_OFFSET (&download->info, i);
  }

  ret =
      gst_gl_download_perform_with_data_unlocked (download, gl_mem->tex_id,
      data);

  GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);

  g_mutex_unlock (&download->lock);

  return ret;
}

gboolean
gst_gl_download_perform_with_data (GstGLDownload * download, GLuint texture_id,
    gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (download != NULL, FALSE);

  g_mutex_lock (&download->lock);

  ret = gst_gl_download_perform_with_data_unlocked (download, texture_id, data);

  g_mutex_unlock (&download->lock);

  return ret;
}

static inline gboolean
_perform_with_data_unlocked_pre (GstGLDownload * download, GLuint texture_id,
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

  download->in_texture = texture_id;
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&download->info); i++) {
    download->data[i] = data[i];
  }

  return TRUE;
}

static gboolean
gst_gl_download_perform_with_data_unlocked (GstGLDownload * download,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  if (!_perform_with_data_unlocked_pre (download, texture_id, data))
    return FALSE;

  gst_gl_display_thread_add (download->display,
      (GstGLDisplayThreadFunc) _do_download, download);

  return TRUE;
}

gboolean
gst_gl_download_perform_with_memory_thread (GstGLDownload * download,
    GstGLMemory * gl_mem)
{
  gpointer data[GST_VIDEO_MAX_PLANES];
  guint i;
  gboolean ret;

  if (!_perform_with_memory_pre (download, gl_mem))
    return FALSE;

  g_mutex_lock (&download->lock);

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&download->info); i++) {
    data[i] = (guint8 *) gl_mem->data +
        GST_VIDEO_INFO_PLANE_OFFSET (&download->info, i);
  }

  ret =
      gst_gl_download_perform_with_data_unlocked_thread (download,
      gl_mem->tex_id, data);

  GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);

  g_mutex_unlock (&download->lock);

  return ret;
}

gboolean
gst_gl_download_perform_with_data_thread (GstGLDownload * download,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  gboolean ret;

  g_return_val_if_fail (download != NULL, FALSE);

  g_mutex_lock (&download->lock);

  ret = gst_gl_download_perform_with_data_unlocked_thread (download,
      texture_id, data);

  g_mutex_unlock (&download->lock);

  return ret;
}

static gboolean
gst_gl_download_perform_with_data_unlocked_thread (GstGLDownload * download,
    GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES])
{
  if (!_perform_with_data_unlocked_pre (download, texture_id, data))
    return FALSE;

  _do_download (download->display, download);

  return TRUE;
}

static inline guint64 *
_gen_key (GstVideoFormat v_format, guint width, guint height)
{
  guint64 *key;

  /* this limits the width and the height to 2^29-1 = 536870911 */
  key = g_malloc (sizeof (guint64 *));
  *key = v_format | ((guint64) width << 6) | ((guint64) height << 35);
  return key;
}

static inline GstGLDownload *
_find_download (GstGLDisplay * display, guint64 * key)
{
  GstGLDownload *ret;

  ret = g_hash_table_lookup (display->downloads, key);

  if (!ret) {
    ret = gst_gl_download_new (display);

    g_hash_table_insert (display->downloads, key, ret);
  }

  return ret;
}

GstGLDownload *
gst_gl_display_find_download (GstGLDisplay * display, GstVideoFormat v_format,
    guint width, guint height)
{
  GstGLDownload *ret;
  guint64 *key;

  key = _gen_key (v_format, width, height);

  gst_gl_display_lock (display);

  ret = _find_download (display, key);

  gst_gl_display_unlock (display);

  return ret;
}

GstGLDownload *
gst_gl_display_find_download_thread (GstGLDisplay * display,
    GstVideoFormat v_format, guint width, guint height)
{
  GstGLDownload *ret;
  guint64 *key;

  key = _gen_key (v_format, width, height);

  ret = _find_download (display, key);

  return ret;
}

static void
_init_download (GstGLDisplay * display, GstGLDownload * download)
{
  GstVideoFormat v_format;
  guint width, height;

  width = GST_VIDEO_INFO_WIDTH (&download->info);
  height = GST_VIDEO_INFO_HEIGHT (&download->info);
  v_format = GST_VIDEO_INFO_FORMAT (&download->info);

  GST_TRACE ("initializing texture download for format %d", v_format);

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
      /* color space conversion is not needed */
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
      /* color space conversion is needed */
    {

      if (!GLEW_EXT_framebuffer_object) {
        /* Frame buffer object is a requirement 
         * when using GLSL colorspace conversion
         */
        gst_gl_display_set_error (display,
            "Context, EXT_framebuffer_object supported: no");
      }
      GST_INFO ("Context, EXT_framebuffer_object supported: yes");

      /* setup FBO */
      if (!download->fbo && !download->depth_buffer) {
        glGenFramebuffersEXT (1, &download->fbo);
        glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, download->fbo);

        /* setup the render buffer for depth */
        glGenRenderbuffersEXT (1, &download->depth_buffer);
        glBindRenderbufferEXT (GL_RENDERBUFFER_EXT, download->depth_buffer);
#ifndef OPENGL_ES2
        glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
            width, height);
        glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT,
            width, height);
#else
        glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT16,
            width, height);
#endif
      }

      /* setup a first texture to render to */
      glGenTextures (1, &download->out_texture[0]);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, download->out_texture[0]);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
          width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
          GL_LINEAR);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
          GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
          GL_CLAMP_TO_EDGE);

      /* attach the first texture to the FBO to renderer to */
      glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
          GL_TEXTURE_RECTANGLE_ARB, download->out_texture[0], 0);

      if (v_format == GST_VIDEO_FORMAT_I420 ||
          v_format == GST_VIDEO_FORMAT_YV12) {
        /* setup a second texture to render to */
        glGenTextures (1, &download->out_texture[1]);
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, download->out_texture[1]);
        glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
            width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
            GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
            GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE);

        /* attach the second texture to the FBO to renderer to */
        glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
            GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_RECTANGLE_ARB,
            download->out_texture[1], 0);

        /* setup a third texture to render to */
        glGenTextures (1, &download->out_texture[2]);
        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, download->out_texture[2]);
        glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
            width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER,
            GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER,
            GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE);

        /* attach the third texture to the FBO to renderer to */
        glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
            GL_COLOR_ATTACHMENT2_EXT, GL_TEXTURE_RECTANGLE_ARB,
            download->out_texture[2], 0);
      }

      /* attach the depth render buffer to the FBO */
      glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT,
          GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, download->depth_buffer);

#ifndef OPENGL_ES2
      glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT,
          GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT,
          download->depth_buffer);
#endif

      gst_gl_display_check_framebuffer_status ();

      if (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) !=
          GL_FRAMEBUFFER_COMPLETE_EXT) {
        gst_gl_display_set_error (display, "GL framebuffer status incomplete");
      }

      /* unbind the FBO */
      glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
    }
      break;
    default:
      break;
      gst_gl_display_set_error (display, "Unsupported download video format %d",
          v_format);
      g_assert_not_reached ();
  }

  _init_download_shader (display, download);
}

static void
_init_download_shader (GstGLDisplay * display, GstGLDownload * download)
{
  GstVideoFormat v_format;

  v_format = GST_VIDEO_INFO_FORMAT (&download->info);

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
      /* color space conversion is not needed */
#ifdef OPENGL_ES2
    {
      /* glGetTexImage2D not available in OpenGL ES 2.0 */
      GError *error = NULL;
      download->shader = gst_gl_shader_new ();

      gst_gl_shader_set_vertex_source (download->shader,
          text_vertex_shader_download);
      gst_gl_shader_set_fragment_source (download->shader,
          text_fragment_shader_download_RGB);

      gst_gl_shader_compile (download->shader, &error);
      if (error) {
        gst_gl_display_set_error (download, "%s", error->message);
        g_error_free (error);
        error = NULL;
        gst_gl_shader_use (NULL);
        g_object_unref (G_OBJECT (download->shader));
        download->shader = NULL;
      } else {
        download->shader_attr_position_loc =
            gst_gl_shader_get_attribute_location (download->shader,
            "a_position");
        download->shader_attr_texture_loc =
            gst_gl_shader_get_attribute_location (download->shader,
            "a_texCoord");
      }
    }
#endif
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
      /* color space conversion is needed */
    {
      /* check if fragment shader is available, then load them
       * GLSL is a requirement for donwload
       */
      if (!GLEW_ARB_fragment_shader) {
        /* colorspace conversion is not possible */
        gst_gl_display_set_error (display,
            "Context, ARB_fragment_shader supported: no");
        return;
      }
#ifdef OPENGL_ES2
      GError *error = NULL;
#endif

      switch (v_format) {
        case GST_VIDEO_FORMAT_YUY2:
        {
          gchar text_shader_download_YUY2[2048];
          sprintf (text_shader_download_YUY2,
              text_shader_download_YUY2_UYVY, "y2,u,y1,v");

          download->shader = gst_gl_shader_new ();
#ifndef OPENGL_ES2
          if (!gst_gl_shader_compile_and_check (download->shader,
                  text_shader_download_YUY2, GST_GL_SHADER_FRAGMENT_SOURCE)) {
            gst_gl_display_set_error (display,
                "Failed to initialize shader for downloading YUY2");
            g_object_unref (G_OBJECT (download->shader));
            download->shader = NULL;
          }
#else
          gst_gl_shader_set_vertex_source (download->shader,
              text_vertex_shader_download);
          gst_gl_shader_set_fragment_source (download->shader,
              text_shader_download_YUY2);

          gst_gl_shader_compile (download->shader, &error);
          if (error) {
            gst_gl_display_set_error (display, "%s", error->message);
            g_error_free (error);
            error = NULL;
            gst_gl_shader_use (NULL);
            g_object_unref (G_OBJECT (download->shader));
            download->shader = NULL;
          } else {
            download->shader_attr_position_loc =
                gst_gl_shader_get_attribute_location
                (download->shader, "a_position");
            download->shader_attr_texture_loc =
                gst_gl_shader_get_attribute_location
                (download->shader, "a_texCoord");
          }
#endif
        }
          break;
        case GST_VIDEO_FORMAT_UYVY:
        {
          gchar text_shader_download_UYVY[2048];
          sprintf (text_shader_download_UYVY,
              text_shader_download_YUY2_UYVY, "v,y1,u,y2");

          download->shader = gst_gl_shader_new ();

#ifndef OPENGL_ES2
          if (!gst_gl_shader_compile_and_check (download->shader,
                  text_shader_download_UYVY, GST_GL_SHADER_FRAGMENT_SOURCE)) {
            gst_gl_display_set_error (display,
                "Failed to initialize shader for downloading UYVY");
            g_object_unref (G_OBJECT (download->shader));
            download->shader = NULL;
          }
#else
          gst_gl_shader_set_vertex_source (download->shader,
              text_vertex_shader_download);
          gst_gl_shader_set_fragment_source (download->shader,
              text_shader_download_UYVY);

          gst_gl_shader_compile (download->shader, &error);
          if (error) {
            gst_gl_display_set_error (display, "%s", error->message);
            g_error_free (error);
            error = NULL;
            gst_gl_shader_use (NULL);
            g_object_unref (G_OBJECT (download->shader));
            download->shader = NULL;
          } else {
            download->shader_attr_position_loc =
                gst_gl_shader_get_attribute_location
                (download->shader, "a_position");
            download->shader_attr_texture_loc =
                gst_gl_shader_get_attribute_location
                (download->shader, "a_texCoord");
          }
#endif

        }
          break;
        case GST_VIDEO_FORMAT_I420:
        case GST_VIDEO_FORMAT_YV12:
          download->shader = gst_gl_shader_new ();
          if (!gst_gl_shader_compile_and_check (download->shader,
                  text_shader_download_I420_YV12,
                  GST_GL_SHADER_FRAGMENT_SOURCE)) {
            gst_gl_display_set_error (display,
                "Failed to initialize shader for downloading I420 or YV12");
            g_object_unref (G_OBJECT (download->shader));
            download->shader = NULL;
          }
          break;
        case GST_VIDEO_FORMAT_AYUV:
          download->shader = gst_gl_shader_new ();

#ifndef OPENGL_ES2
          if (!gst_gl_shader_compile_and_check (download->shader,
                  text_shader_download_AYUV, GST_GL_SHADER_FRAGMENT_SOURCE)) {
            gst_gl_display_set_error (display,
                "Failed to initialize shader for downloading AYUV");
            g_object_unref (G_OBJECT (download->shader));
            download->shader = NULL;
          }
#else
          gst_gl_shader_set_vertex_source (download->shader,
              text_vertex_shader_download);
          gst_gl_shader_set_fragment_source (download->shader,
              text_shader_download_AYUV);

          gst_gl_shader_compile (download->shader, &error);
          if (error) {
            gst_gl_display_set_error (display, "%s", error->message);
            g_error_free (error);
            error = NULL;
            gst_gl_shader_use (NULL);
            g_object_unref (G_OBJECT (download->shader));
            download->shader = NULL;
          } else {
            download->shader_attr_position_loc =
                gst_gl_shader_get_attribute_location
                (download->shader, "a_position");
            download->shader_attr_texture_loc =
                gst_gl_shader_get_attribute_location
                (download->shader, "a_texCoord");
          }
#endif
          break;
        default:
          gst_gl_display_set_error (display,
              "Unsupported download video format %d", v_format);
          g_assert_not_reached ();
          break;
      }
    }
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported download video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }
}

/* Called in the gl thread */
static void
_do_download (GstGLDisplay * display, GstGLDownload * download)
{
  GstVideoFormat v_format;
  guint width, height;

  width = GST_VIDEO_INFO_WIDTH (&download->info);
  height = GST_VIDEO_INFO_HEIGHT (&download->info);
  v_format = GST_VIDEO_INFO_FORMAT (&download->info);

  GST_TRACE ("downloading texture:%u format:%d, dimensions:%ux%u",
      download->in_texture, v_format, width, height);

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
      /* color space conversion is not needed */
      _do_download_draw_rgb (display, download);
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
      /* color space conversion is needed */
      _do_download_draw_yuv (display, download);
      break;
    default:
      gst_gl_display_set_error (display, "Unsupported download video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }
}

/* called by _do_download (in the gl thread) */
static void
_do_download_draw_rgb (GstGLDisplay * display, GstGLDownload * download)
{
  GstVideoFormat v_format;

#ifndef OPENGL_ES2
  if (download->display->colorspace_conversion ==
      GST_GL_DISPLAY_CONVERSION_GLSL)
    glUseProgramObjectARB (0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, download->in_texture);
#else
  guint width, height;

  width = GST_VIDEO_INFO_WIDTH (&download->info);
  height = GST_VIDEO_INFO_HEIGHT (&download->info);

  const GLfloat vVertices[] = { 1.0f, -1.0f, 0.0f,
    1.0f, 0.0f,
    -1.0f, -1.0f, 0.0f,
    0.0f, 0.0f,
    -1.0f, 1.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 1.0f, 0.0f,
    1.0f, 1.0f
  };

  GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

  glViewport (0, 0, width, height);

  glClearColor (0.0, 0.0, 0.0, 0.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gst_gl_shader_use (download->shader);

  glVertexAttribPointer (download->shader_attr_position_loc, 3,
      GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
  glVertexAttribPointer (download->shader_attr_texture_loc, 2,
      GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

  glEnableVertexAttribArray (download->shader_attr_position_loc);
  glEnableVertexAttribArray (download->shader_attr_texture_loc);

  glActiveTextureARB (GL_TEXTURE0_ARB);
  gst_gl_shader_set_uniform_1i (download->shader, "s_texture", 0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, download->in_texture);

  glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  glUseProgramObjectARB (0);
#endif

  v_format = GST_VIDEO_INFO_FORMAT (&download->info);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
#ifndef OPENGL_ES2
      glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          GL_UNSIGNED_BYTE, download->data[0]);
#else
      glReadPixels (0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
          download->data[0]);
#endif
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
#ifndef OPENGL_ES2
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8, download->data[0]);
#else
      glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#endif /* G_BYTE_ORDER */
#else /* OPENGL_ES2 */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      glReadPixels (0, 0, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8,
          download->data[0]);
#else
      glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#endif /* G_BYTE_ORDER */
#endif /* !OPENGL_ES2 */
      break;
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
#ifndef OPENGL_ES2
      glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_BGRA,
          GL_UNSIGNED_BYTE, download->data[0]);
#endif
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
#ifndef OPENGL_ES2
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          GL_UNSIGNED_INT_8_8_8_8, download->data[0]);
#else
      glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#endif /* G_BYTE_ORDER */
#endif /* !OPENGL_ES2 */
      break;
    case GST_VIDEO_FORMAT_RGB:
#ifndef OPENGL_ES2
      glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB,
          GL_UNSIGNED_BYTE, download->data[0]);
#else
      glReadPixels (0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE,
          download->data[0]);
#endif
      break;
    case GST_VIDEO_FORMAT_BGR:
#ifndef OPENGL_ES2
      glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_BGR,
          GL_UNSIGNED_BYTE, download->data[0]);
#endif
      break;
    default:
      gst_gl_display_set_error (display,
          "Download video format inconsistency %d", v_format);
      g_assert_not_reached ();
      break;
  }

#ifndef OPENGL_ES2
  glReadBuffer (GL_NONE);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);
#endif
}


/* called by _do_download (in the gl thread) */
static void
_do_download_draw_yuv (GstGLDisplay * display, GstGLDownload * download)
{
  GstVideoFormat v_format;
  guint width, height;

  GLenum multipleRT[] = {
    GL_COLOR_ATTACHMENT0_EXT,
    GL_COLOR_ATTACHMENT1_EXT,
    GL_COLOR_ATTACHMENT2_EXT
  };

#ifdef OPENGL_ES2
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

  width = GST_VIDEO_INFO_WIDTH (&download->info);
  height = GST_VIDEO_INFO_HEIGHT (&download->info);
  v_format = GST_VIDEO_INFO_FORMAT (&download->info);

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, download->fbo);

#ifndef OPENGL_ES2
  glPushAttrib (GL_VIEWPORT_BIT);

  glMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();
  gluOrtho2D (0.0, width, 0.0, height);

  glMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity ();
#else /* OPENGL_ES2 */
  glGetIntegerv (GL_VIEWPORT, viewport_dim);
#endif

  glViewport (0, 0, width, height);

  switch (v_format) {
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
    {
#ifndef OPENGL_ES2
      glDrawBuffer (GL_COLOR_ATTACHMENT0_EXT);
#endif

      glClearColor (0.0, 0.0, 0.0, 0.0);
      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      gst_gl_shader_use (download->shader);

#ifndef OPENGL_ES2
      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();
#else
      glVertexAttribPointer (download->shader_attr_position_loc, 3,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
      glVertexAttribPointer (download->shader_attr_texture_loc, 2,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

      glEnableVertexAttribArray (download->shader_attr_position_loc);
      glEnableVertexAttribArray (download->shader_attr_texture_loc);
#endif

      glActiveTextureARB (GL_TEXTURE0_ARB);
      gst_gl_shader_set_uniform_1i (download->shader, "tex", 0);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, download->in_texture);
    }
      break;

    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    {
#ifndef OPENGL_ES2
      glDrawBuffers (3, multipleRT);
#endif

      glClearColor (0.0, 0.0, 0.0, 0.0);
      glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      gst_gl_shader_use (download->shader);

#ifndef OPENGL_ES2
      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();
#endif

      glActiveTextureARB (GL_TEXTURE0_ARB);
      gst_gl_shader_set_uniform_1i (download->shader, "tex", 0);
      gst_gl_shader_set_uniform_1f (download->shader, "w", (gfloat) width);
      gst_gl_shader_set_uniform_1f (download->shader, "h", (gfloat) height);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, download->in_texture);
    }
      break;

    default:
      break;
      gst_gl_display_set_error (display,
          "Download video format inconsistensy %d", v_format);

  }

#ifndef OPENGL_ES2
  glBegin (GL_QUADS);
  glTexCoord2i (0, 0);
  glVertex2f (-1.0f, -1.0f);
  glTexCoord2i (width, 0);
  glVertex2f (1.0f, -1.0f);
  glTexCoord2i (width, height);
  glVertex2f (1.0f, 1.0f);
  glTexCoord2i (0, height);
  glVertex2f (-1.0f, 1.0f);
  glEnd ();

  glDrawBuffer (GL_NONE);
#else /* OPENGL_ES2 */
  glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
#endif

  /* don't check if GLSL is available
   * because download yuv is not available
   * without GLSL (whereas rgb is)
   */
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

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, download->fbo);
#ifndef OPENGL_ES2
  glReadBuffer (GL_COLOR_ATTACHMENT0_EXT);
#endif

  switch (v_format) {
    case GST_VIDEO_FORMAT_AYUV:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      glReadPixels (0, 0, width, height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8, download->data[0]);
#else
      glReadPixels (0, 0, width, height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#endif
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2, height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#else
      glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2, height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8, download->data[0]);
#endif
      break;
    case GST_VIDEO_FORMAT_I420:
    {
      glReadPixels (0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[0]);

#ifndef OPENGL_ES2
      glReadBuffer (GL_COLOR_ATTACHMENT1_EXT);
#endif

      glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2,
          GST_ROUND_UP_2 (height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[1]);

#ifndef OPENGL_ES2
      glReadBuffer (GL_COLOR_ATTACHMENT2_EXT);
#endif

      glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2,
          GST_ROUND_UP_2 (height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[2]);
    }
      break;
    case GST_VIDEO_FORMAT_YV12:
    {
      glReadPixels (0, 0, width, height, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[0]);

#ifndef OPENGL_ES2
      glReadBuffer (GL_COLOR_ATTACHMENT1_EXT);
#endif

      glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2,
          GST_ROUND_UP_2 (height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[2]);

#ifndef OPENGL_ES2
      glReadBuffer (GL_COLOR_ATTACHMENT2_EXT);
#endif

      glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2,
          GST_ROUND_UP_2 (height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[1]);
    }
      break;
    default:
      break;
      gst_gl_display_set_error (display,
          "Download video format inconsistensy %d", v_format);
      g_assert_not_reached ();
  }
#ifndef OPENGL_ES2
  glReadBuffer (GL_NONE);
#endif

  gst_gl_display_check_framebuffer_status ();

  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
}
