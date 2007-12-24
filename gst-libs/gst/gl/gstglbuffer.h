
#ifndef _GST_GL_BUFFER_H_
#define _GST_GL_BUFFER_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gstgldisplay.h>

typedef struct _GstGLBuffer GstGLBuffer;

#define GST_TYPE_GL_BUFFER (gst_gl_buffer_get_type())

#define GST_IS_GL_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GL_BUFFER))
#define GST_GL_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GL_BUFFER, GstGLBuffer))

typedef enum {
  GST_GL_BUFFER_UNKNOWN,
  GST_GL_BUFFER_XIMAGE,
  GST_GL_BUFFER_RBO,
  GST_GL_BUFFER_TEXTURE
} GstGLBufferType;

struct _GstGLBuffer {
  GstBuffer buffer;

  GstGLDisplay *display;

  GstGLBufferType type;

  XID pixmap;
  GC gc;

  GLuint rbo;
  GLuint texture;

  int width;
  int height;
};

GType gst_gl_buffer_get_type (void);

GstGLBuffer * gst_gl_buffer_new (GstGLDisplay *display, GstVideoFormat format,
    int width, int height);
void gst_gl_buffer_upload (GstGLBuffer *buffer, void *data);
void gst_gl_buffer_download (GstGLBuffer *buffer, void *data);

#endif

