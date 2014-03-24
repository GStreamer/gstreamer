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
static gboolean _init_download_shader (GstGLContext * context,
    GstGLDownload * download);
static gboolean _gst_gl_download_perform_with_data_unlocked (GstGLDownload *
    download, GLuint texture_id, gpointer data[GST_VIDEO_MAX_PLANES]);

#if GST_GL_HAVE_OPENGL
static void _do_download_draw_rgb_opengl (GstGLContext * context,
    GstGLDownload * download);
static void _do_download_draw_yuv_opengl (GstGLContext * context,
    GstGLDownload * download);
#endif
#if GST_GL_HAVE_GLES2
static void _do_download_draw_rgb_gles2 (GstGLContext * context,
    GstGLDownload * download);
static void _do_download_draw_yuv_gles2 (GstGLContext * context,
    GstGLDownload * download);
#endif

/* *INDENT-OFF* */

/* FIXME: use the colormatrix support from videoconvert */

#define RGB_TO_YUV_COEFFICIENTS \
      "uniform vec3 offset;\n" \
      "uniform vec3 ycoeff;\n" \
      "uniform vec3 ucoeff;\n" \
      "uniform vec3 vcoeff;\n"

/* Matrix inverses of the color matrices found in gstglupload */
/* BT. 601 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat bt601_offset[] = {0.0625, 0.5, 0.5};
static const gfloat bt601_ycoeff[] = {0.256816, 0.504154, 0.0979137};
static const gfloat bt601_ucoeff[] = {-0.148246, -0.29102, 0.439266};
static const gfloat bt601_vcoeff[] = {0.439271, -0.367833, -0.071438};

/* BT. 709 standard with the following ranges:
 * Y = [16..235] (of 255)
 * Cb/Cr = [16..240] (of 255)
 */
static const gfloat bt709_offset[] = {0.0625, 0.5, 0.5};
static const gfloat bt709_ycoeff[] = {0.213392, 0.718140,-0.072426};
static const gfloat bt709_ucoeff[] = {0.117608, 0.395793,-0.513401};
static const gfloat bt709_vcoeff[] = {0.420599,-0.467775, 0.047176};

#if GST_GL_HAVE_OPENGL
/* YUY2:y2,u,y1,v
   UYVY:v,y1,u,y2 */
static const gchar *text_shader_YUY2_UYVY_opengl =
    "uniform sampler2D tex;\n"
    "uniform float width;\n"
    RGB_TO_YUV_COEFFICIENTS
    "void main(void) {\n"
    "  vec3 rgb1, rgb2;\n"
    "  float fx,fy,y1,y2,u,v;\n"
    "  fx = gl_TexCoord[0].x;\n"
    "  fy = gl_TexCoord[0].y;\n"
    "  rgb1=texture2D(tex,vec2(fx*2.0,fy)).rgb;\n"
    "  rgb2=texture2D(tex,vec2(fx*2.0+1.0/width,fy)).rgb;\n"
    "  y1=dot(rgb1, ycoeff);\n"
    "  y2=dot(rgb2, ycoeff);\n"
    "  u=dot(rgb1, ucoeff);\n"
    "  v=dot(rgb1, vcoeff);\n"
    "  y1+=offset.x;\n"
    "  y2+=offset.x;\n"
    "  u+=offset.y;\n"
    "  v+=offset.z;\n"
    "  gl_FragColor=vec4(%s);\n"
    "}\n";

static const gchar *text_shader_I420_YV12_opengl =
    "uniform sampler2D tex;\n"
    "uniform float w, h;\n"
    RGB_TO_YUV_COEFFICIENTS
    "void main(void) {\n"
    "  vec3 rgb1, rgb2;\n"
    "  float y,u,v;\n"
    "  vec2 nxy=gl_TexCoord[0].xy;\n"
    "  vec2 nxy2=nxy*2.0;\n"
    "  rgb1=texture2D(tex,nxy).rgb;\n"
    "  rgb2=texture2D(tex,nxy2).rgb;\n"
    "  y=dot(rgb1, ycoeff);\n"
    "  u=dot(rgb2, ucoeff);\n"
    "  v=dot(rgb2, vcoeff);\n"
    "  y+=offset.x;\n"
    "  u+=offset.y;\n"
    "  v+=offset.z;\n"
    "  gl_FragData[0] = vec4(y, 0.0, 0.0, 1.0);\n"
    "  gl_FragData[1] = vec4(u, 0.0, 0.0, 1.0);\n"
    "  gl_FragData[2] = vec4(v, 0.0, 0.0, 1.0);\n"
    "}\n";

static const gchar *text_shader_AYUV_opengl =
    "uniform sampler2D tex;\n"
    RGB_TO_YUV_COEFFICIENTS
    "void main(void) {\n"
    "  vec3 rgb;\n"
    "  float y,u,v;\n"
    "  vec2 nxy=gl_TexCoord[0].xy;\n"
    "  rgb=texture2D(tex,nxy).rgb;\n"
    "  y=dot(rgb, ycoeff);\n"
    "  u=dot(rgb, ucoeff);\n"
    "  v=dot(rgb, vcoeff);\n"
    "  y+=offset.x;\n"
    "  u+=offset.y;\n"
    "  v+=offset.z;\n"
    "  gl_FragColor=vec4(y,u,v,1.0);\n"
    "}\n";

#define text_vertex_shader_opengl NULL
#endif /* GST_GL_HAVE_OPENGL */

#if GST_GL_HAVE_GLES2
static const gchar *text_shader_YUY2_UYVY_gles2 =
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D tex;\n"
    RGB_TO_YUV_COEFFICIENTS
    "void main(void) {\n"
    "  vec3 rgb1, rgb2;\n"
    "  float fx,fy,y1,y2,u,v;\n"
    "  fx = v_texCoord.x;\n"
    "  fy = v_texCoord.y;\n"
    "  rgb1=texture2D(tex,vec2(fx*2.0,fy)).rgb;\n"
    "  rgb2=texture2D(tex,vec2(fx*2.0+1.0,fy)).rgb;\n"
    "  y1=dot(rgb1, ycoeff);\n"
    "  y2=dot(rgb2, ycoeff);\n"
    "  u=dot(rgb1, ucoeff);\n"
    "  v=dot(rgb1, vcoeff);\n"
    "  y1+=offset.x;\n"
    "  y2+=offset.x;\n"
    "  u+=offset.y;\n"
    "  v+=offset.z;\n"
    "  gl_FragColor=vec4(%s);\n"
    "}\n";

/* no OpenGL ES 2.0 support because for now it's not possible
 * to attach multiple textures to a frame buffer object
 */
#define text_shader_I420_YV12_gles2 NULL

static const gchar *text_shader_AYUV_gles2 =
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D tex;\n"
    RGB_TO_YUV_COEFFICIENTS
    "void main(void) {\n"
    "  vec3 rgb;\n"
    "  float y,u,v;\n"
    "  vec2 nxy=v_texCoord.xy;\n"
    "  rgb=texture2D(tex,nxy).rgb;\n"
    "  y=dot(rgb, ycoeff);\n"
    "  u=dot(rgb, ucoeff);\n"
    "  v=dot(rgb, vcoeff);\n"
    "  y+=offset.x;\n"
    "  u+=offset.y;\n"
    "  v+=offset.z;\n"
    "  gl_FragColor=vec4(1.0,y,u,v);\n"
    "}\n";

static const gchar *text_shader_ARGB_gles2 =
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D tex;\n"
    "void main(void) {\n"
    "  vec4 rgba;\n"
    "  vec2 nxy = v_texCoord.xy;\n"
    "  rgba.rgb=texture2D(tex,nxy).rgb;\n"
    "  rgba.a = 1.0;\n"
    "  gl_FragColor=vec4(rgba.%c,rgba.%c,rgba.%c,rgba.%c);\n"
    "}\n";

static const gchar *text_vertex_shader_gles2 =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texCoord;   \n"
    "varying vec2 v_texCoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = a_position; \n"
    "   v_texCoord = a_texCoord;  \n"
    "}                            \n";

static const gchar *text_shader_RGB_gles2 =
    "precision mediump float;                            \n"
    "varying vec2 v_texCoord;                            \n"
    "uniform sampler2D tex;                              \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  gl_FragColor = texture2D(tex, v_texCoord );       \n"
    "}                                                   \n";
#endif /* GST_GL_HAVE_GLES2 */

/* *INDENT-ON* */

struct _GstGLDownloadPrivate
{
  const gchar *YUY2_UYVY;
  const gchar *I420_YV12;
  const gchar *AYUV;
  const gchar *ARGB;
  const gchar *vert_shader;

  void (*do_rgb) (GstGLContext * context, GstGLDownload * download);
  void (*do_yuv) (GstGLContext * context, GstGLDownload * download);

  gboolean result;
};

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
  g_type_class_add_private (klass, sizeof (GstGLDownloadPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_download_finalize;
}

static void
gst_gl_download_init (GstGLDownload * download)
{

  download->priv = GST_GL_DOWNLOAD_GET_PRIVATE (download);

  download->context = NULL;

  g_mutex_init (&download->lock);

  download->fbo = 0;
  download->depth_buffer = 0;
  download->in_texture = 0;
  download->shader = NULL;

  download->shader_attr_position_loc = 0;
  download->shader_attr_texture_loc = 0;

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
  GstGLDownloadPrivate *priv;

  download = g_object_new (GST_TYPE_GL_DOWNLOAD, NULL);

  download->context = gst_object_ref (context);
  priv = download->priv;

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (context)) {
    priv->YUY2_UYVY = text_shader_YUY2_UYVY_opengl;
    priv->I420_YV12 = text_shader_I420_YV12_opengl;
    priv->AYUV = text_shader_AYUV_opengl;
    priv->ARGB = NULL;
    priv->vert_shader = text_vertex_shader_opengl;
    priv->do_rgb = _do_download_draw_rgb_opengl;
    priv->do_yuv = _do_download_draw_yuv_opengl;
  }
#endif
#if GST_GL_HAVE_GLES2
  if (USING_GLES2 (context)) {
    priv->YUY2_UYVY = text_shader_YUY2_UYVY_gles2;
    priv->I420_YV12 = text_shader_I420_YV12_gles2;
    priv->AYUV = text_shader_AYUV_gles2;
    priv->ARGB = text_shader_ARGB_gles2;
    priv->vert_shader = text_vertex_shader_gles2;
    priv->do_rgb = _do_download_draw_rgb_gles2;
    priv->do_yuv = _do_download_draw_yuv_gles2;
  }
#endif

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
      gst_gl_context_del_texture (download->context, &download->out_texture[i]);
      download->out_texture[i] = 0;
    }
  }

  if (download->in_texture) {
    gst_gl_context_del_texture (download->context, &download->in_texture);
    download->in_texture = 0;
  }
  if (download->fbo || download->depth_buffer) {
    gst_gl_context_del_fbo (download->context, download->fbo,
        download->depth_buffer);
    download->fbo = 0;
    download->depth_buffer = 0;
  }
  if (download->shader) {
    gst_object_unref (download->shader);
    download->shader = NULL;
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

  gst_gl_context_thread_add (download->context,
      (GstGLContextThreadFunc) _do_download, download);

  return download->priv->result;
}

static void
_init_download (GstGLContext * context, GstGLDownload * download)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint out_width, out_height;

  gl = context->gl_vtable;
  v_format = GST_VIDEO_INFO_FORMAT (&download->info);
  out_width = GST_VIDEO_INFO_WIDTH (&download->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&download->info);

  GST_TRACE ("initializing texture download for format %s",
      gst_video_format_to_string (v_format));

  if (USING_OPENGL (context)) {
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
        goto no_convert;
        break;
      default:
        break;
    }
  }

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
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
      /* color space conversion is needed */
    {

      if (!gl->GenFramebuffers) {
        /* Frame buffer object is a requirement 
         * when using GLSL colorspace conversion
         */
        gst_gl_context_set_error (context,
            "Context, EXT_framebuffer_object supported: no");
        goto error;
      }
      GST_INFO ("Context, EXT_framebuffer_object supported: yes");

      /* setup FBO */
      gl->GenFramebuffers (1, &download->fbo);
      gl->BindFramebuffer (GL_FRAMEBUFFER, download->fbo);

      /* setup the render buffer for depth */
      gl->GenRenderbuffers (1, &download->depth_buffer);
      gl->BindRenderbuffer (GL_RENDERBUFFER, download->depth_buffer);
#if GST_GL_HAVE_OPENGL
      if (USING_OPENGL (context)) {
        gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
            out_width, out_height);
        gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
            out_width, out_height);
      }
#endif
#if GST_GL_HAVE_GLES2
      if (USING_GLES2 (context)) {
        gl->RenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT16,
            out_width, out_height);
      }
#endif

      /* setup a first texture to render to */
      gl->GenTextures (1, &download->out_texture[0]);
      gl->BindTexture (GL_TEXTURE_2D, download->out_texture[0]);
      gl->TexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8,
          out_width, out_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

      /* attach the first texture to the FBO to renderer to */
      gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
          GL_TEXTURE_2D, download->out_texture[0], 0);

      if (v_format == GST_VIDEO_FORMAT_I420 ||
          v_format == GST_VIDEO_FORMAT_YV12) {
        /* setup a second texture to render to */
        gl->GenTextures (1, &download->out_texture[1]);
        gl->BindTexture (GL_TEXTURE_2D, download->out_texture[1]);
        gl->TexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8,
            out_width, out_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        /* attach the second texture to the FBO to renderer to */
        gl->FramebufferTexture2D (GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, download->out_texture[1], 0);

        /* setup a third texture to render to */
        gl->GenTextures (1, &download->out_texture[2]);
        gl->BindTexture (GL_TEXTURE_2D, download->out_texture[2]);
        gl->TexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8,
            out_width, out_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        /* attach the third texture to the FBO to renderer to */
        gl->FramebufferTexture2D (GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, download->out_texture[2], 0);
      }

      /* attach the depth render buffer to the FBO */
      gl->FramebufferRenderbuffer (GL_FRAMEBUFFER,
          GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, download->depth_buffer);
      if (USING_OPENGL (context)) {
        gl->FramebufferRenderbuffer (GL_FRAMEBUFFER,
            GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, download->depth_buffer);
      }

      if (!gst_gl_context_check_framebuffer_status (context)) {
        gst_gl_context_set_error (context, "GL framebuffer status incomplete");
        goto error;
      }

      /* unbind the FBO */
      gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
    }
      break;
    default:
      break;
      gst_gl_context_set_error (context, "Unsupported download video format %d",
          v_format);
      g_assert_not_reached ();
  }

no_convert:
  download->priv->result = _init_download_shader (context, download);
  return;

error:
  {
    download->priv->result = FALSE;
    return;
  }
}

static gboolean
_create_shader (GstGLContext * context, const gchar * vertex_src,
    const gchar * fragment_src, GstGLShader ** out_shader)
{
  GstGLShader *shader;
  GError *error = NULL;

  g_return_val_if_fail (vertex_src != NULL || fragment_src != NULL, FALSE);

  shader = gst_gl_shader_new (context);

  if (vertex_src)
    gst_gl_shader_set_vertex_source (shader, vertex_src);
  if (fragment_src)
    gst_gl_shader_set_fragment_source (shader, fragment_src);

  if (!gst_gl_shader_compile (shader, &error)) {
    gst_gl_context_set_error (context, "%s", error->message);
    g_error_free (error);
    gst_gl_context_clear_shader (context);
    gst_object_unref (shader);
    return FALSE;
  }

  *out_shader = shader;
  return TRUE;
}

static gboolean
_init_download_shader (GstGLContext * context, GstGLDownload * download)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;

  gl = download->context->gl_vtable;
  v_format = GST_VIDEO_INFO_FORMAT (&download->info);

  if (GST_VIDEO_FORMAT_INFO_IS_RGB (download->info.finfo)
      && !USING_GLES2 (context)) {
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
        return TRUE;
        break;
      default:
        break;
    }
  }

  /* color space conversion is needed */

  /* check if fragment shader is available, then load them
   * GLSL is a requirement for download
   */
  if (!gl->CreateProgramObject && !gl->CreateProgram) {
    /* colorspace conversion is not possible */
    gst_gl_context_set_error (context,
        "Context, ARB_fragment_shader supported: no");
    return FALSE;;
  }

  switch (v_format) {
    case GST_VIDEO_FORMAT_YUY2:
    {
      gchar text_shader_download_YUY2[2048];

      sprintf (text_shader_download_YUY2,
          download->priv->YUY2_UYVY, "y2,u,y1,v");

      if (_create_shader (context, download->priv->vert_shader,
              text_shader_download_YUY2, &download->shader)) {
        if (USING_GLES2 (context)) {
          download->shader_attr_position_loc =
              gst_gl_shader_get_attribute_location (download->shader,
              "a_position");
          download->shader_attr_texture_loc =
              gst_gl_shader_get_attribute_location (download->shader,
              "a_texCoord");
        }
      }
    }
      break;
    case GST_VIDEO_FORMAT_UYVY:
    {
      gchar text_shader_download_UYVY[2048];

      sprintf (text_shader_download_UYVY,
          download->priv->YUY2_UYVY, "v,y1,u,y2");

      if (_create_shader (context, download->priv->vert_shader,
              text_shader_download_UYVY, &download->shader)) {
        if (USING_GLES2 (context)) {
          download->shader_attr_position_loc =
              gst_gl_shader_get_attribute_location (download->shader,
              "a_position");
          download->shader_attr_texture_loc =
              gst_gl_shader_get_attribute_location (download->shader,
              "a_texCoord");
        }
      }
    }
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    {
      _create_shader (context, download->priv->vert_shader,
          download->priv->I420_YV12, &download->shader);
      break;
    }
    case GST_VIDEO_FORMAT_AYUV:
    {
      if (_create_shader (context, download->priv->vert_shader,
              download->priv->AYUV, &download->shader)) {
        if (USING_GLES2 (context)) {
          download->shader_attr_position_loc =
              gst_gl_shader_get_attribute_location (download->shader,
              "a_position");
          download->shader_attr_texture_loc =
              gst_gl_shader_get_attribute_location (download->shader,
              "a_texCoord");
        }
      }
      break;
    }
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
#if GST_GL_HAVE_GLES2
    {
      gchar text_shader_ARGB[2048];

      switch (v_format) {
        case GST_VIDEO_FORMAT_BGR:
        case GST_VIDEO_FORMAT_BGRx:
        case GST_VIDEO_FORMAT_BGRA:
          sprintf (text_shader_ARGB, download->priv->ARGB, 'b', 'g', 'r', 'a');
          break;
        case GST_VIDEO_FORMAT_xRGB:
        case GST_VIDEO_FORMAT_ARGB:
          sprintf (text_shader_ARGB, download->priv->ARGB, 'a', 'r', 'g', 'b');
          break;
        case GST_VIDEO_FORMAT_xBGR:
        case GST_VIDEO_FORMAT_ABGR:
          sprintf (text_shader_ARGB, download->priv->ARGB, 'a', 'b', 'g', 'r');
          break;
        default:
          memcpy (text_shader_ARGB, text_shader_RGB_gles2,
              strlen (text_shader_RGB_gles2) + 1);
          break;
      }

      if (_create_shader (context, download->priv->vert_shader,
              text_shader_ARGB, &download->shader)) {
        download->shader_attr_position_loc =
            gst_gl_shader_get_attribute_location (download->shader,
            "a_position");
        download->shader_attr_texture_loc =
            gst_gl_shader_get_attribute_location (download->shader,
            "a_texCoord");
      }
      break;
    }
#else
      g_assert_not_reached ();
      return FALSE;
      break;
#endif
    default:
      gst_gl_context_set_error (context,
          "Unsupported download video format %d", v_format);
      g_assert_not_reached ();
      return FALSE;
      break;
  }

  return TRUE;
}

/* Called in the gl thread */
static void
_do_download (GstGLContext * context, GstGLDownload * download)
{
  GstVideoFormat v_format;
  guint out_width, out_height;

  v_format = GST_VIDEO_INFO_FORMAT (&download->info);
  out_width = GST_VIDEO_INFO_WIDTH (&download->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&download->info);

  GST_TRACE ("downloading texture:%u format:%d, dimensions:%ux%u",
      download->in_texture, v_format, out_width, out_height);

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
      download->priv->do_rgb (context, download);
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_AYUV:
      /* color space conversion is needed */
      download->priv->do_yuv (context, download);
      break;
    default:
      gst_gl_context_set_error (context, "Unsupported download video format %d",
          v_format);
      g_assert_not_reached ();
      break;
  }

  download->priv->result = TRUE;
}

#if GST_GL_HAVE_OPENGL
static void
_do_download_draw_rgb_opengl (GstGLContext * context, GstGLDownload * download)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;

  gl = download->context->gl_vtable;

  gst_gl_context_clear_shader (context);

  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, download->in_texture);

  v_format = GST_VIDEO_INFO_FORMAT (&download->info);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
      gl->GetTexImage (GL_TEXTURE_2D, 0, GL_RGBA,
          GL_UNSIGNED_BYTE, download->data[0]);
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gl->GetTexImage (GL_TEXTURE_2D, 0, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8, download->data[0]);
#else
      gl->GetTexImage (GL_TEXTURE_2D, 0, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#endif /* G_BYTE_ORDER */
      break;
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      gl->GetTexImage (GL_TEXTURE_2D, 0, GL_BGRA,
          GL_UNSIGNED_BYTE, download->data[0]);
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gl->GetTexImage (GL_TEXTURE_2D, 0, GL_RGBA,
          GL_UNSIGNED_INT_8_8_8_8, download->data[0]);
#else
      glGetTexImage (GL_TEXTURE_2D, 0, GL_RGBA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#endif /* G_BYTE_ORDER */
      break;
    case GST_VIDEO_FORMAT_RGB:
      gl->GetTexImage (GL_TEXTURE_2D, 0, GL_RGB,
          GL_UNSIGNED_BYTE, download->data[0]);
      break;
    case GST_VIDEO_FORMAT_BGR:
      gl->GetTexImage (GL_TEXTURE_2D, 0, GL_BGR,
          GL_UNSIGNED_BYTE, download->data[0]);
      break;
    default:
      gst_gl_context_set_error (context,
          "Download video format inconsistency %d", v_format);
      g_assert_not_reached ();
      break;
  }

  gl->Disable (GL_TEXTURE_2D);
}
#endif


#if GST_GL_HAVE_GLES2
static void
_do_download_draw_rgb_gles2 (GstGLContext * context, GstGLDownload * download)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint out_width, out_height;

  GLint viewport_dim[4];

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

  gl = download->context->gl_vtable;

  out_width = GST_VIDEO_INFO_WIDTH (&download->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&download->info);

  gst_gl_context_check_framebuffer_status (context);
  gl->BindFramebuffer (GL_FRAMEBUFFER, download->fbo);

  gl->GetIntegerv (GL_VIEWPORT, viewport_dim);

  gl->Viewport (0, 0, out_width, out_height);

  gl->ClearColor (0.0, 0.0, 0.0, 0.0);
  gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gst_gl_shader_use (download->shader);

  gl->VertexAttribPointer (download->shader_attr_position_loc, 3,
      GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
  gl->VertexAttribPointer (download->shader_attr_texture_loc, 2,
      GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

  gl->EnableVertexAttribArray (download->shader_attr_position_loc);
  gl->EnableVertexAttribArray (download->shader_attr_texture_loc);

  gl->ActiveTexture (GL_TEXTURE0);
  gst_gl_shader_set_uniform_1i (download->shader, "tex", 0);
  gl->BindTexture (GL_TEXTURE_2D, download->in_texture);

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  gst_gl_context_clear_shader (context);

  gl->Viewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);

  v_format = GST_VIDEO_INFO_FORMAT (&download->info);

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      gl->ReadPixels (0, 0, out_width, out_height, GL_RGBA, GL_UNSIGNED_BYTE,
          download->data[0]);
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      gl->ReadPixels (0, 0, out_width, out_height, GL_RGB, GL_UNSIGNED_BYTE,
          download->data[0]);
      break;
    default:
      gst_gl_context_set_error (context,
          "Download video format inconsistency %d", v_format);
      g_assert_not_reached ();
      break;
  }

  gst_gl_context_check_framebuffer_status (context);
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
}
#endif

#if GST_GL_HAVE_OPENGL
static void
_do_download_draw_yuv_opengl (GstGLContext * context, GstGLDownload * download)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint out_width = GST_VIDEO_INFO_WIDTH (&download->info);
  guint out_height = GST_VIDEO_INFO_HEIGHT (&download->info);
  const gfloat *cms_offset;
  const gfloat *cms_ycoeff;
  const gfloat *cms_ucoeff;
  const gfloat *cms_vcoeff;

  GLenum multipleRT[] = {
    GL_COLOR_ATTACHMENT0,
    GL_COLOR_ATTACHMENT1,
    GL_COLOR_ATTACHMENT2
  };

  gfloat verts[8] = { 1.0f, -1.0f,
    -1.0f, -1.0f,
    -1.0f, 1.0f,
    1.0f, 1.0f
  };
  gfloat texcoords[8] = { 1.0, 0.0,
    0.0, 0.0,
    0.0, 1.0,
    1.0, 1.0
  };

  gl = context->gl_vtable;

  v_format = GST_VIDEO_INFO_FORMAT (&download->info);

  GST_TRACE ("doing YUV download of texture:%u (%ux%u) using fbo:%u",
      download->in_texture, out_width, out_height, download->fbo);

  gl->BindFramebuffer (GL_FRAMEBUFFER, download->fbo);

  gl->PushAttrib (GL_VIEWPORT_BIT);

  gl->MatrixMode (GL_PROJECTION);
  gl->PushMatrix ();
  gl->LoadIdentity ();
  gluOrtho2D (0.0, out_width, 0.0, out_height);

  gl->MatrixMode (GL_MODELVIEW);
  gl->PushMatrix ();
  gl->LoadIdentity ();

  gl->Viewport (0, 0, out_width, out_height);

  switch (v_format) {
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
    {
      gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

      gl->ClearColor (0.0, 0.0, 0.0, 0.0);
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      gst_gl_shader_use (download->shader);

      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();

      gl->ActiveTexture (GL_TEXTURE0);
      gst_gl_shader_set_uniform_1i (download->shader, "tex", 0);
      gst_gl_shader_set_uniform_1f (download->shader, "width", out_width);
      gl->BindTexture (GL_TEXTURE_2D, download->in_texture);
    }
      break;

    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    {
      gl->DrawBuffers (3, multipleRT);

      gl->ClearColor (0.0, 0.0, 0.0, 0.0);
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      gst_gl_shader_use (download->shader);

      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();

      gl->ActiveTexture (GL_TEXTURE0);
      gst_gl_shader_set_uniform_1i (download->shader, "tex", 0);
      gst_gl_shader_set_uniform_1f (download->shader, "w", (gfloat) out_width);
      gst_gl_shader_set_uniform_1f (download->shader, "h", (gfloat) out_height);
      gl->BindTexture (GL_TEXTURE_2D, download->in_texture);
    }
      break;

    default:
      break;
      gst_gl_context_set_error (context,
          "Download video format inconsistensy %d", v_format);
  }

  if (gst_video_colorimetry_matches (&download->info.colorimetry,
          GST_VIDEO_COLORIMETRY_BT709)) {
    cms_offset = bt709_offset;
    cms_ycoeff = bt709_ycoeff;
    cms_ucoeff = bt709_ucoeff;
    cms_vcoeff = bt709_vcoeff;
  } else if (gst_video_colorimetry_matches (&download->info.colorimetry,
          GST_VIDEO_COLORIMETRY_BT601)) {
    cms_offset = bt601_offset;
    cms_ycoeff = bt601_ycoeff;
    cms_ucoeff = bt601_ucoeff;
    cms_vcoeff = bt601_vcoeff;
  } else {
    /* defaults */
    cms_offset = bt601_offset;
    cms_ycoeff = bt601_ycoeff;
    cms_ucoeff = bt601_ucoeff;
    cms_vcoeff = bt601_vcoeff;
  }

  gst_gl_shader_set_uniform_3fv (download->shader, "offset", 1,
      (gfloat *) cms_offset);
  gst_gl_shader_set_uniform_3fv (download->shader, "ycoeff", 1,
      (gfloat *) cms_ycoeff);
  gst_gl_shader_set_uniform_3fv (download->shader, "ucoeff", 1,
      (gfloat *) cms_ucoeff);
  gst_gl_shader_set_uniform_3fv (download->shader, "vcoeff", 1,
      (gfloat *) cms_vcoeff);

  gl->ClientActiveTexture (GL_TEXTURE0);

  gl->EnableClientState (GL_VERTEX_ARRAY);
  gl->EnableClientState (GL_TEXTURE_COORD_ARRAY);

  gl->VertexPointer (2, GL_FLOAT, 0, &verts);
  gl->TexCoordPointer (2, GL_FLOAT, 0, &texcoords);

  gl->DrawArrays (GL_QUADS, 0, 4);

  gl->DisableClientState (GL_VERTEX_ARRAY);
  gl->DisableClientState (GL_TEXTURE_COORD_ARRAY);

  gl->DrawBuffer (GL_NONE);

  /* don't check if GLSL is available
   * because download yuv is not available
   * without GLSL (whereas rgb is)
   */
  gl->UseProgramObject (0);

  gl->Disable (GL_TEXTURE_2D);
  gl->MatrixMode (GL_PROJECTION);
  gl->PopMatrix ();
  gl->MatrixMode (GL_MODELVIEW);
  gl->PopMatrix ();
  gl->PopAttrib ();

  gst_gl_context_check_framebuffer_status (context);

  gl->BindFramebuffer (GL_FRAMEBUFFER, download->fbo);
  gl->ReadBuffer (GL_COLOR_ATTACHMENT0);

  switch (v_format) {
    case GST_VIDEO_FORMAT_AYUV:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gl->ReadPixels (0, 0, out_width, out_height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8, download->data[0]);
#else
      gl->ReadPixels (0, 0, out_width, out_height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#endif
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2, out_height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#else
      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2, out_height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8, download->data[0]);
#endif
      break;
    case GST_VIDEO_FORMAT_I420:
    {
      gl->ReadPixels (0, 0, out_width, out_height, GL_LUMINANCE,
          GL_UNSIGNED_BYTE, download->data[0]);

      gl->ReadBuffer (GL_COLOR_ATTACHMENT1);

      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2,
          GST_ROUND_UP_2 (out_height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[1]);

      gl->ReadBuffer (GL_COLOR_ATTACHMENT2);

      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2,
          GST_ROUND_UP_2 (out_height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[2]);
    }
      break;
    case GST_VIDEO_FORMAT_YV12:
    {
      gl->ReadPixels (0, 0, out_width, out_height, GL_LUMINANCE,
          GL_UNSIGNED_BYTE, download->data[0]);

      gl->ReadBuffer (GL_COLOR_ATTACHMENT1);

      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2,
          GST_ROUND_UP_2 (out_height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[2]);

      gl->ReadBuffer (GL_COLOR_ATTACHMENT2);

      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2,
          GST_ROUND_UP_2 (out_height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[1]);
    }
      break;
    default:
      break;
      gst_gl_context_set_error (context,
          "Download video format inconsistensy %d", v_format);
      g_assert_not_reached ();
  }
  gl->ReadBuffer (GL_NONE);

  gst_gl_context_check_framebuffer_status (context);

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
}
#endif

#if GST_GL_HAVE_GLES2
static void
_do_download_draw_yuv_gles2 (GstGLContext * context, GstGLDownload * download)
{
  GstGLFuncs *gl;
  GstVideoFormat v_format;
  guint out_width, out_height;
  const gfloat *cms_offset;
  const gfloat *cms_ycoeff;
  const gfloat *cms_ucoeff;
  const gfloat *cms_vcoeff;

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

  gl = context->gl_vtable;

  out_width = GST_VIDEO_INFO_WIDTH (&download->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&download->info);
  v_format = GST_VIDEO_INFO_FORMAT (&download->info);

  GST_TRACE ("doing YUV download of texture:%u (%ux%u) using fbo:%u",
      download->in_texture, out_width, out_height, download->fbo);

  gst_gl_context_check_framebuffer_status (context);
  gl->BindFramebuffer (GL_FRAMEBUFFER, download->fbo);

  gl->GetIntegerv (GL_VIEWPORT, viewport_dim);

  gl->Viewport (0, 0, out_width, out_height);

  switch (v_format) {
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
    {
      gl->ClearColor (0.0, 0.0, 0.0, 0.0);
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      gst_gl_shader_use (download->shader);

      gl->VertexAttribPointer (download->shader_attr_position_loc, 3,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);
      gl->VertexAttribPointer (download->shader_attr_texture_loc, 2,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

      gl->EnableVertexAttribArray (download->shader_attr_position_loc);
      gl->EnableVertexAttribArray (download->shader_attr_texture_loc);

      gl->ActiveTexture (GL_TEXTURE0);
      gst_gl_shader_set_uniform_1i (download->shader, "tex", 0);
      gl->BindTexture (GL_TEXTURE_2D, download->in_texture);
    }
      break;

    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    {
      gl->ClearColor (0.0, 0.0, 0.0, 0.0);
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      gst_gl_shader_use (download->shader);

      gl->ActiveTexture (GL_TEXTURE0);
      gst_gl_shader_set_uniform_1i (download->shader, "tex", 0);
      gst_gl_shader_set_uniform_1f (download->shader, "w", (gfloat) out_width);
      gst_gl_shader_set_uniform_1f (download->shader, "h", (gfloat) out_height);
      gl->BindTexture (GL_TEXTURE_2D, download->in_texture);
    }
      break;

    default:
      break;
      gst_gl_context_set_error (context,
          "Download video format inconsistensy %d", v_format);

  }

  if (gst_video_colorimetry_matches (&download->info.colorimetry,
          GST_VIDEO_COLORIMETRY_BT709)) {
    cms_offset = bt709_offset;
    cms_ycoeff = bt709_ycoeff;
    cms_ucoeff = bt709_ucoeff;
    cms_vcoeff = bt709_vcoeff;
  } else if (gst_video_colorimetry_matches (&download->info.colorimetry,
          GST_VIDEO_COLORIMETRY_BT601)) {
    cms_offset = bt601_offset;
    cms_ycoeff = bt601_ycoeff;
    cms_ucoeff = bt601_ucoeff;
    cms_vcoeff = bt601_vcoeff;
  } else {
    /* defaults */
    cms_offset = bt601_offset;
    cms_ycoeff = bt601_ycoeff;
    cms_ucoeff = bt601_ucoeff;
    cms_vcoeff = bt601_vcoeff;
  }

  gst_gl_shader_set_uniform_3fv (download->shader, "offset", 1,
      (gfloat *) cms_offset);
  gst_gl_shader_set_uniform_3fv (download->shader, "ycoeff", 1,
      (gfloat *) cms_ycoeff);
  gst_gl_shader_set_uniform_3fv (download->shader, "ucoeff", 1,
      (gfloat *) cms_ucoeff);
  gst_gl_shader_set_uniform_3fv (download->shader, "vcoeff", 1,
      (gfloat *) cms_vcoeff);

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  /* don't check if GLSL is available
   * because download yuv is not available
   * without GLSL (whereas rgb is)
   */
  gst_gl_context_clear_shader (context);

  gl->Viewport (viewport_dim[0], viewport_dim[1], viewport_dim[2],
      viewport_dim[3]);

  switch (v_format) {
    case GST_VIDEO_FORMAT_AYUV:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gl->ReadPixels (0, 0, out_width, out_height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8, download->data[0]);
#else
      gl->ReadPixels (0, 0, out_width, out_height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#endif
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2, out_height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8_REV, download->data[0]);
#else
      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2, out_height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8, download->data[0]);
#endif
      break;
    case GST_VIDEO_FORMAT_I420:
    {
      gl->ReadPixels (0, 0, out_width, out_height, GL_LUMINANCE,
          GL_UNSIGNED_BYTE, download->data[0]);

      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2,
          GST_ROUND_UP_2 (out_height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[1]);

      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2,
          GST_ROUND_UP_2 (out_height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[2]);
    }
      break;
    case GST_VIDEO_FORMAT_YV12:
    {
      gl->ReadPixels (0, 0, out_width, out_height, GL_LUMINANCE,
          GL_UNSIGNED_BYTE, download->data[0]);

      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2,
          GST_ROUND_UP_2 (out_height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[2]);

      gl->ReadPixels (0, 0, GST_ROUND_UP_2 (out_width) / 2,
          GST_ROUND_UP_2 (out_height) / 2, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          download->data[1]);
    }
      break;
    default:
      break;
      gst_gl_context_set_error (context,
          "Download video format inconsistensy %d", v_format);
      g_assert_not_reached ();
  }

  gst_gl_context_check_framebuffer_status (context);
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
}
#endif
