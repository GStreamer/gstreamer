
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

  switch (buffer->type) {
    case GST_GL_BUFFER_XIMAGE:
      GST_DEBUG ("freeing pixmap %ld", buffer->pixmap);
      XFreeGC (buffer->display->display, buffer->gc);
      XFreePixmap (buffer->display->display, buffer->pixmap);
      break;
    case GST_GL_BUFFER_RBO:
      glDeleteRenderbuffersEXT (1, &buffer->rbo);
      break;
    case GST_GL_BUFFER_TEXTURE:
      glDeleteTextures (1, &buffer->texture);
      break;
    default:
      g_assert_not_reached ();
      break;
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
gst_gl_buffer_new (GstGLDisplay * display, GstVideoFormat format,
    int width, int height)
{
  GstGLBuffer *buffer;
  XGCValues values = { 0 };

  g_return_val_if_fail (format == GST_VIDEO_FORMAT_RGBx, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  buffer = (GstGLBuffer *) gst_mini_object_new (GST_TYPE_GL_BUFFER);

  buffer->display = g_object_ref (display);
  buffer->type = GST_GL_BUFFER_TEXTURE;

  buffer->width = width;
  buffer->height = height;

  switch (buffer->type) {
    case GST_GL_BUFFER_XIMAGE:
    {
      buffer->pixmap = XCreatePixmap (display->display,
          DefaultRootWindow (display->display), width, height, 32);
      XSync (display->display, False);

      buffer->gc = XCreateGC (display->display, buffer->pixmap, 0, &values);

      GST_DEBUG ("new pixmap %dx%d xid %ld", width, height, buffer->pixmap);
      break;
    }
    case GST_GL_BUFFER_RBO:
    {
      GLuint fbo;

      gst_gl_display_lock (buffer->display);

      glGenFramebuffersEXT (1, &fbo);
      glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, fbo);

      glGenRenderbuffersEXT (1, &buffer->rbo);
      glBindRenderbufferEXT (GL_RENDERBUFFER_EXT, buffer->rbo);

      glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT,
          GL_COLOR_ATTACHMENT1_EXT, GL_RENDERBUFFER_EXT, buffer->rbo);
      glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_RGB,
          buffer->width, buffer->height);

      glDrawBuffer (GL_COLOR_ATTACHMENT1_EXT);
      glReadBuffer (GL_COLOR_ATTACHMENT1_EXT);
      g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
          GL_FRAMEBUFFER_COMPLETE_EXT);

      glDeleteFramebuffersEXT (1, &fbo);

      gst_gl_display_unlock (buffer->display);
      break;
    }
    case GST_GL_BUFFER_TEXTURE:
      break;
    default:
      g_assert_not_reached ();
  }

  return buffer;
}

void
gst_gl_buffer_upload (GstGLBuffer * buffer, void *data)
{
  Display *display = buffer->display->display;

  GST_DEBUG ("uploading %p %dx%d", data, buffer->width, buffer->height);

  gst_gl_display_lock (buffer->display);

  switch (buffer->type) {
    case GST_GL_BUFFER_XIMAGE:
    {
      XImage *image;
      Visual *visual;
      int depth;
      int bpp;

      visual = DefaultVisual (display, 0);
      depth = 32;
      bpp = 32;

      image = XCreateImage (display, visual, depth, ZPixmap, 0, NULL,
          buffer->width, buffer->height, bpp, 0);
      GST_DEBUG ("image %p", image);
      image->data = data;

      XPutImage (display, buffer->pixmap, buffer->gc,
          image, 0, 0, 0, 0, buffer->width, buffer->height);

      XDestroyImage (image);
      break;
    }
    case GST_GL_BUFFER_RBO:
    {
      unsigned int fbo;

      g_assert (glIsRenderbufferEXT (buffer->rbo));

      glGenFramebuffersEXT (1, &fbo);
      glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, fbo);

      glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT,
          GL_COLOR_ATTACHMENT1_EXT, GL_RENDERBUFFER_EXT, buffer->rbo);

      glDrawBuffer (GL_COLOR_ATTACHMENT1_EXT);
      glReadBuffer (GL_COLOR_ATTACHMENT1_EXT);

      g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
          GL_FRAMEBUFFER_COMPLETE_EXT);

      gst_gl_display_check_error (buffer->display, __LINE__);
      glWindowPos2iARB (0, 0);
      glDrawPixels (buffer->width, buffer->height, GL_RGB,
          GL_UNSIGNED_BYTE, data);

      glDeleteFramebuffersEXT (1, &fbo);

      g_assert (glIsRenderbufferEXT (buffer->rbo));

      break;
    }
    case GST_GL_BUFFER_TEXTURE:
      buffer->texture =
          gst_gl_display_upload_texture_rectangle (buffer->display,
          GST_VIDEO_FORMAT_RGBx, data, buffer->width, buffer->height);
      break;
    default:
      g_assert_not_reached ();
  }

  gst_gl_display_unlock (buffer->display);
}


void
gst_gl_buffer_download (GstGLBuffer * buffer, void *data)
{
  gst_gl_display_lock (buffer->display);

  GST_DEBUG ("downloading");

  switch (buffer->type) {
    case GST_GL_BUFFER_XIMAGE:
    {
      XImage *image;

      image = XGetImage (buffer->display->display, buffer->pixmap,
          0, 0, buffer->width, buffer->height, 0xffffffff, ZPixmap);

      memcpy (data, image->data, buffer->width * buffer->height * 4);

      XDestroyImage (image);
      break;
    }
    case GST_GL_BUFFER_RBO:
    {
      unsigned int fbo;

      glGenFramebuffersEXT (1, &fbo);
      glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, fbo);

      glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT,
          GL_COLOR_ATTACHMENT1_EXT, GL_RENDERBUFFER_EXT, buffer->rbo);

      glDrawBuffer (GL_COLOR_ATTACHMENT1_EXT);
      glReadBuffer (GL_COLOR_ATTACHMENT1_EXT);

      g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
          GL_FRAMEBUFFER_COMPLETE_EXT);

      glReadPixels (0, 0, buffer->width, buffer->height / 2, GL_RGBA,
          GL_UNSIGNED_BYTE, data);

      glDeleteFramebuffersEXT (1, &fbo);

      break;
    }
    case GST_GL_BUFFER_TEXTURE:
    {
      unsigned int fbo;

      glGenFramebuffersEXT (1, &fbo);
      glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, fbo);

      glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
          GL_COLOR_ATTACHMENT1_EXT, GL_TEXTURE_RECTANGLE_ARB,
          buffer->texture, 0);

      glDrawBuffer (GL_COLOR_ATTACHMENT1_EXT);
      glReadBuffer (GL_COLOR_ATTACHMENT1_EXT);

      g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
          GL_FRAMEBUFFER_COMPLETE_EXT);

      glReadPixels (0, 0, buffer->width, buffer->height, GL_RGBA,
          GL_UNSIGNED_BYTE, data);

      glDeleteFramebuffersEXT (1, &fbo);
    }
      break;
    default:
      g_assert_not_reached ();
  }

  gst_gl_display_unlock (buffer->display);
}
