
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
  GST_GL_BUFFER_FORMAT_UNKNOWN,
  GST_GL_BUFFER_FORMAT_RGBA,
  GST_GL_BUFFER_FORMAT_RGB,
  GST_GL_BUFFER_FORMAT_YUYV,
  GST_GL_BUFFER_FORMAT_PLANAR444,
  GST_GL_BUFFER_FORMAT_PLANAR422,
  GST_GL_BUFFER_FORMAT_PLANAR420
} GstGLBufferFormat;

struct _GstGLBuffer {
  GstBuffer buffer;

  GstGLDisplay *display;

  GstGLBufferFormat format;
  gboolean is_yuv;

  GLuint texture;
  GLuint texture_u;
  GLuint texture_v;

  int width;
  int height;
};

GType gst_gl_buffer_get_type (void);

GstGLBuffer * gst_gl_buffer_new (GstGLDisplay *display,
    int width, int height);
GstGLBuffer * gst_gl_buffer_new_with_format (GstGLDisplay *display,
    GstGLBufferFormat format, int width, int height);
GstGLBuffer * gst_gl_buffer_new_from_video_format (GstGLDisplay *display,
    GstVideoFormat format, int width, int height);
void gst_gl_buffer_upload (GstGLBuffer *buffer,
    GstVideoFormat format, void *data);
void gst_gl_buffer_download (GstGLBuffer *buffer, GstVideoFormat format,
    void *data);


#define GST_GL_VIDEO_CAPS \
  "video/x-raw-gl," \
  "format=(int)1," \
  "is_yuv=(boolean)FALSE," \
  "width=(int)[1,2048]," \
  "height=(int)[1,2048]," \
  "pixel-aspect-ratio=(fraction)1/1," \
  "framerate=(fraction)[0/1,100/1]"

GstGLBufferFormat gst_gl_buffer_format_from_video_format (GstVideoFormat format);
int gst_gl_buffer_format_get_size (GstGLBufferFormat format, int width,
    int height);
gboolean gst_gl_buffer_format_parse_caps (GstCaps *caps, GstGLBufferFormat *format,
    int *width, int *height);


#endif

