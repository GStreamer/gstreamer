
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gstglbuffer.h>
#include <gstgldisplay.h>
#include <GL/glext.h>
#include <unistd.h>
#include "glextensions.h"

#include <string.h>

static GObjectClass *gst_gl_buffer_parent_class;

static void
gst_gl_buffer_finalize (GstGLBuffer * buffer)
{
  gst_gl_display_lock (buffer->display);

  glDeleteTextures (1, &buffer->texture);
  if (buffer->texture_u) {
    glDeleteTextures (1, &buffer->texture_u);
  }
  if (buffer->texture_v) {
    glDeleteTextures (1, &buffer->texture_v);
  }

  gst_gl_display_unlock (buffer->display);
  g_object_unref (buffer->display);

  GST_MINI_OBJECT_CLASS (gst_gl_buffer_parent_class)->
      finalize (GST_MINI_OBJECT (buffer));
}

static void
gst_gl_buffer_init (GstGLBuffer * buffer, gpointer g_class)
{

}

static void
gst_gl_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gst_gl_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_gl_buffer_finalize;
}


GType
gst_gl_buffer_get_type (void)
{
  static GType _gst_gl_buffer_type;

  if (G_UNLIKELY (_gst_gl_buffer_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_gl_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstGLBuffer),
      0,
      (GInstanceInitFunc) gst_gl_buffer_init,
      NULL
    };
    _gst_gl_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstGLBuffer", &info, 0);
  }
  return _gst_gl_buffer_type;
}


GstGLBuffer *
gst_gl_buffer_new (GstGLDisplay * display, int width, int height)
{
  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  return gst_gl_buffer_new_with_format (display, GST_GL_BUFFER_FORMAT_RGBA,
      width, height);
}

GstGLBuffer *
gst_gl_buffer_new_with_format (GstGLDisplay * display,
    GstGLBufferFormat format, int width, int height)
{
  GstGLBuffer *buffer;

  g_return_val_if_fail (format != GST_GL_BUFFER_FORMAT_UNKNOWN, NULL);
  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  buffer = (GstGLBuffer *) gst_mini_object_new (GST_TYPE_GL_BUFFER);

  buffer->display = g_object_ref (display);
  buffer->width = width;
  buffer->height = height;
  buffer->format = format;
  GST_BUFFER_SIZE (buffer) = gst_gl_buffer_format_get_size (format, width,
      height);

  gst_gl_display_lock (buffer->display);
  glGenTextures (1, &buffer->texture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture);
  switch (format) {
    case GST_GL_BUFFER_FORMAT_RGBA:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          buffer->width, buffer->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      break;
    case GST_GL_BUFFER_FORMAT_RGB:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB,
          buffer->width, buffer->height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
      break;
    case GST_GL_BUFFER_FORMAT_YUYV:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA,
          buffer->width, buffer->height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      break;
    case GST_GL_BUFFER_FORMAT_PLANAR444:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          buffer->width, buffer->height,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

      glGenTextures (1, &buffer->texture_u);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture_u);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          buffer->width, buffer->height,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

      glGenTextures (1, &buffer->texture_v);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture_v);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          buffer->width, buffer->height,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
      break;
    case GST_GL_BUFFER_FORMAT_PLANAR422:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          buffer->width, buffer->height,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

      glGenTextures (1, &buffer->texture_u);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture_u);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          GST_ROUND_UP_2 (buffer->width) / 2, buffer->height,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

      glGenTextures (1, &buffer->texture_v);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture_v);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          GST_ROUND_UP_2 (buffer->width) / 2, buffer->height,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
      break;
    case GST_GL_BUFFER_FORMAT_PLANAR420:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          buffer->width, buffer->height,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

      glGenTextures (1, &buffer->texture_u);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture_u);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          GST_ROUND_UP_2 (buffer->width) / 2,
          GST_ROUND_UP_2 (buffer->height) / 2,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

      glGenTextures (1, &buffer->texture_v);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture_v);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          GST_ROUND_UP_2 (buffer->width) / 2,
          GST_ROUND_UP_2 (buffer->height) / 2,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
      break;
    default:
      g_warning ("GL buffer format not handled");
  }

  gst_gl_display_unlock (buffer->display);

  return buffer;
}

GstGLBuffer *
gst_gl_buffer_new_from_video_format (GstGLDisplay * display,
    GstVideoFormat video_format, int width, int height)
{
  GstGLBuffer *buffer;

  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  buffer = gst_gl_buffer_new_with_format (display,
      gst_gl_buffer_format_from_video_format (video_format), width, height);

  switch (video_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
      buffer->is_yuv = FALSE;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      /* counterintuitive: when you use a GL_YCBCR_MESA texture, the
       * colorspace is automatically converted, so it's referred to
       * as RGB */
      buffer->is_yuv = FALSE;
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      buffer->is_yuv = TRUE;
      break;
    default:
      g_assert_not_reached ();
  }

  return buffer;
}

void
gst_gl_buffer_upload (GstGLBuffer * buffer, GstVideoFormat video_format,
    void *data)
{
  int width = buffer->width;
  int height = buffer->height;

  GST_DEBUG ("uploading %p %dx%d", data, width, height);

  g_return_if_fail (buffer->format ==
      gst_gl_buffer_format_from_video_format (video_format));

  gst_gl_display_lock (buffer->display);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture);

  switch (video_format) {
    case GST_VIDEO_FORMAT_RGBx:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_BGRx:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_xRGB:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_xBGR:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, data);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, data);
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, data);

      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture_u);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (buffer->width) / 2,
          GST_ROUND_UP_2 (buffer->height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE,
          (guint8 *) data +
          gst_video_format_get_component_offset (video_format, 1, width,
              height));

      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture_v);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (buffer->width) / 2,
          GST_ROUND_UP_2 (buffer->height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE,
          (guint8 *) data +
          gst_video_format_get_component_offset (video_format, 2, width,
              height));
      break;
    default:
      g_assert_not_reached ();
  }

  gst_gl_display_unlock (buffer->display);
}


void
gst_gl_buffer_download (GstGLBuffer * buffer, GstVideoFormat video_format,
    void *data)
{
  GLuint fbo;

  g_return_if_fail (buffer->format ==
      gst_gl_buffer_format_from_video_format (video_format));

  GST_DEBUG ("downloading");

  gst_gl_display_lock (buffer->display);

  /* we need a reset function */
  glMatrixMode (GL_COLOR);
  glLoadIdentity ();
  glPixelTransferf (GL_POST_COLOR_MATRIX_RED_BIAS, 0);
  glPixelTransferf (GL_POST_COLOR_MATRIX_GREEN_BIAS, 0);
  glPixelTransferf (GL_POST_COLOR_MATRIX_BLUE_BIAS, 0);

  glGenFramebuffersEXT (1, &fbo);
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, fbo);

  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
      GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_RECTANGLE_ARB, buffer->texture, 0);

  glDrawBuffer (GL_COLOR_ATTACHMENT1_EXT);
  glReadBuffer (GL_COLOR_ATTACHMENT1_EXT);

  g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
      GL_FRAMEBUFFER_COMPLETE_EXT);

  switch (video_format) {
    case GST_VIDEO_FORMAT_RGBx:
      glReadPixels (0, 0, buffer->width, buffer->height, GL_RGBA,
          GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_BGRx:
      glReadPixels (0, 0, buffer->width, buffer->height, GL_BGRA,
          GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_xBGR:
      glReadPixels (0, 0, buffer->width, buffer->height, GL_RGBA,
          GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_xRGB:
      glReadPixels (0, 0, buffer->width, buffer->height, GL_BGRA,
          GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      g_warning ("video format not supported for download from GL texture");
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      glReadPixels (0, 0, buffer->width, buffer->height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, data);

      glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
          GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_RECTANGLE_ARB,
          buffer->texture_u, 0);
      glReadPixels (0, 0,
          GST_ROUND_UP_2 (buffer->width) / 2,
          GST_ROUND_UP_2 (buffer->height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE,
          (guint8 *) data +
          gst_video_format_get_component_offset (video_format, 1, buffer->width,
              buffer->height));

      glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
          GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_RECTANGLE_ARB,
          buffer->texture_v, 0);
      glReadPixels (0, 0,
          GST_ROUND_UP_2 (buffer->width) / 2,
          GST_ROUND_UP_2 (buffer->height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE,
          (guint8 *) data +
          gst_video_format_get_component_offset (video_format, 2, buffer->width,
              buffer->height));
      break;
    default:
      g_assert_not_reached ();
  }

  glDeleteFramebuffersEXT (1, &fbo);

  gst_gl_display_unlock (buffer->display);
}



/* buffer format */

GstGLBufferFormat
gst_gl_buffer_format_from_video_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_AYUV:
      return GST_GL_BUFFER_FORMAT_RGBA;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      return GST_GL_BUFFER_FORMAT_RGB;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      return GST_GL_BUFFER_FORMAT_YUYV;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      return GST_GL_BUFFER_FORMAT_PLANAR420;
    case GST_VIDEO_FORMAT_UNKNOWN:
      return GST_GL_BUFFER_FORMAT_UNKNOWN;
  }

  g_return_val_if_reached (GST_GL_BUFFER_FORMAT_UNKNOWN);
}

int
gst_gl_buffer_format_get_size (GstGLBufferFormat format, int width, int height)
{
  /* this is not strictly true, but it's used for compatibility with
   * queue and BaseTransform */
  return width * height * 4;
}

gboolean
gst_gl_buffer_format_parse_caps (GstCaps * caps, GstGLBufferFormat * format,
    int *width, int *height)
{
  GstStructure *structure;
  int format_as_int;
  gboolean ret;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_has_name (structure, "video/x-raw-gl")) {
    return FALSE;
  }

  ret = gst_structure_get_int (structure, "format", &format_as_int);
  *format = format_as_int;
  ret &= gst_structure_get_int (structure, "width", width);
  ret &= gst_structure_get_int (structure, "height", height);

  return ret;
}
