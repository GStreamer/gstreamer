
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
gst_gl_buffer_new (GstGLDisplay * display, GstGLBufferFormat format,
    int width, int height)
{
  GstGLBuffer *buffer;

  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  buffer = (GstGLBuffer *) gst_mini_object_new (GST_TYPE_GL_BUFFER);

  buffer->display = g_object_ref (display);
  buffer->width = width;
  buffer->height = height;

  gst_gl_display_lock (buffer->display);
  glGenTextures (1, &buffer->texture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture);
  switch (format) {
    case GST_GL_BUFFER_FORMAT_RGBA:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          buffer->width, buffer->height, 0, GL_RGBA, GL_FLOAT, NULL);
      break;
    case GST_GL_BUFFER_FORMAT_RGB:
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB,
          buffer->width, buffer->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      break;
    default:
      g_warning ("GL buffer format not handled");
  }

  gst_gl_display_unlock (buffer->display);

  return buffer;
}

GstGLBuffer *
gst_gl_buffer_new_from_data (GstGLDisplay * display, GstVideoFormat format,
    int width, int height, void *data)
{
  GstGLBuffer *buffer;
  int comp;

  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);
  g_return_val_if_fail (data != NULL, NULL);

  GST_DEBUG ("uploading %p %dx%d", data, width, height);

  buffer = (GstGLBuffer *) gst_mini_object_new (GST_TYPE_GL_BUFFER);
  buffer->display = g_object_ref (display);
  buffer->width = width;
  buffer->height = height;

  gst_gl_display_lock (buffer->display);
  glGenTextures (1, &buffer->texture);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture);

  switch (format) {
    case GST_VIDEO_FORMAT_RGBx:
      buffer->format = GST_GL_BUFFER_FORMAT_RGB;
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          buffer->width, buffer->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_BGRx:
      buffer->format = GST_GL_BUFFER_FORMAT_RGB;
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          buffer->width, buffer->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_BYTE, data);
      break;
    case GST_VIDEO_FORMAT_xRGB:
      buffer->format = GST_GL_BUFFER_FORMAT_RGB;
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          buffer->width, buffer->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_xBGR:
      buffer->format = GST_GL_BUFFER_FORMAT_RGB;
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          buffer->width, buffer->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      buffer->format = GST_GL_BUFFER_FORMAT_YUYV;
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, data);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      buffer->format = GST_GL_BUFFER_FORMAT_YUYV;
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA, width, height,
          0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, data);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      buffer->format = GST_GL_BUFFER_FORMAT_RGB;
      buffer->is_yuv = TRUE;
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
          buffer->width, buffer->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      buffer->format = GST_GL_BUFFER_FORMAT_PLANAR420;
      buffer->is_yuv = TRUE;
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          buffer->width, buffer->height,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
          GL_LUMINANCE, GL_UNSIGNED_BYTE, data);

      glGenTextures (1, &buffer->texture_u);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture_u);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          GST_ROUND_UP_2 (buffer->width) / 2,
          GST_ROUND_UP_2 (buffer->height) / 2,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
      comp = (format == GST_VIDEO_FORMAT_I420) ? 1 : 2;
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (buffer->width) / 2,
          GST_ROUND_UP_2 (buffer->height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE,
          (guint8 *) data +
          gst_video_format_get_component_offset (format, comp, width, height));

      glGenTextures (1, &buffer->texture_v);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, buffer->texture_v);
      glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
          GST_ROUND_UP_2 (buffer->width) / 2,
          GST_ROUND_UP_2 (buffer->height) / 2,
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
      comp = (format == GST_VIDEO_FORMAT_I420) ? 2 : 1;
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
          GST_ROUND_UP_2 (buffer->width) / 2,
          GST_ROUND_UP_2 (buffer->height) / 2,
          GL_LUMINANCE, GL_UNSIGNED_BYTE,
          (guint8 *) data +
          gst_video_format_get_component_offset (format, comp, width, height));
      break;
    default:
      g_assert_not_reached ();
  }

  gst_gl_display_unlock (buffer->display);

  return buffer;
}


void
gst_gl_buffer_download (GstGLBuffer * buffer, void *data)
{
  GLuint fbo;

  GST_DEBUG ("downloading");

  gst_gl_display_lock (buffer->display);

  glGenFramebuffersEXT (1, &fbo);
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, fbo);

  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
      GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_RECTANGLE_ARB, buffer->texture, 0);

  glDrawBuffer (GL_COLOR_ATTACHMENT1_EXT);
  glReadBuffer (GL_COLOR_ATTACHMENT1_EXT);

  g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
      GL_FRAMEBUFFER_COMPLETE_EXT);

  /* needs a reset function */
  glMatrixMode (GL_COLOR);
  glLoadIdentity ();
  glPixelTransferf (GL_POST_COLOR_MATRIX_RED_BIAS, 0);
  glPixelTransferf (GL_POST_COLOR_MATRIX_GREEN_BIAS, 0);
  glPixelTransferf (GL_POST_COLOR_MATRIX_BLUE_BIAS, 0);

  glReadPixels (0, 0, buffer->width, buffer->height, GL_RGBA,
      GL_UNSIGNED_BYTE, data);

  glDeleteFramebuffersEXT (1, &fbo);

  gst_gl_display_unlock (buffer->display);
}
