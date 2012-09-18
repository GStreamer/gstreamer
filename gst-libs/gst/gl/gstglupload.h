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

#ifndef __GST_GL_UPLOAD_H__
#define __GST_GL_UPLOAD_H__

#include <gst/video/video.h>
#include <gst/gstmemory.h>

#include "gstglshader.h"
#include "gstgldisplay.h"

G_BEGIN_DECLS

/* forward declare */
typedef struct _GstGLMemory GstGLMemory;
typedef struct _GstGLDisplay GstGLDisplay;

GType gst_gl_upload_get_type (void);
#define GST_TYPE_GL_UPLOAD (gst_gl_upload_get_type())
#define GST_GL_UPLOAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_UPLOAD,GstGLUpload))
#define GST_GL_UPLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_DISPLAY,GstGLUploadClass))
#define GST_IS_GL_UPLOAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_UPLOAD))
#define GST_IS_GL_UPLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_UPLOAD))
#define GST_GL_UPLOAD_CAST(obj) ((GstGLUpload*)(obj))

typedef struct _GstGLUpload GstGLUpload;
typedef struct _GstGLUploadClass GstGLUploadClass;

struct _GstGLUpload
{
  GObject          parent;

  GMutex           lock;

  GstGLDisplay    *display;

  gpointer         data[GST_VIDEO_MAX_PLANES];
  gboolean         initted;

  /* used for the conversion */
  GLuint           fbo;
  GLuint           depth_buffer;
  GLuint           out_texture;
  GLuint           in_texture[GST_VIDEO_MAX_PLANES];
  GstGLShader     *shader;
#ifdef OPENGL_ES2
  GLint            shader_attr_position_loc;
  GLint            shader_attr_texture_loc;
#endif

  /* output data */
  GstVideoInfo    info;

  /* <private> */
  gpointer _reserved[GST_PADDING];
};

struct _GstGLUploadClass
{
  GObjectClass object_class;
};

#ifndef OPENGL_ES2
# define GST_GL_UPLOAD_FORMATS "{ RGB, RGBx, RGBA, BGR, BGRx, BGRA, xRGB, " \
                               "xBGR, ARGB, ABGR, I420, YV12, YUY2, UYVY, AYUV }"
#else /* OPENGL_ES2 */
# define GST_GL_UPLOAD_FORMATS "{ RGB, RGBx, RGBA, I420, YV12, YUY2, UYVY, AYUV }"
#endif

#define GST_GL_UPLOAD_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_GL_UPLOAD_FORMATS)

GstGLUpload * gst_gl_upload_new            (GstGLDisplay * display);

gboolean gst_gl_upload_init_format         (GstGLUpload * upload, GstVideoFormat v_format,
                                            guint width, guint height);
gboolean gst_gl_upload_init_format_thread  (GstGLUpload * upload, GstVideoFormat v_format,
                                            guint width, guint height);

gboolean gst_gl_upload_perform_with_memory        (GstGLUpload * upload, GstGLMemory * gl_mem);
gboolean gst_gl_upload_perform_with_data          (GstGLUpload * upload, GLuint texture_id,
                                                   gpointer data[GST_VIDEO_MAX_PLANES]);
gboolean gst_gl_upload_perform_with_memory_thread (GstGLUpload * upload, GstGLMemory * gl_mem);
gboolean gst_gl_upload_perform_with_data_thread   (GstGLUpload * upload, GLuint texture_id,
                                                   gpointer data[GST_VIDEO_MAX_PLANES]);

GstGLUpload * gst_gl_display_find_upload        (GstGLDisplay * display, GstVideoFormat v_format,
                                                 guint width, guint height);
GstGLUpload * gst_gl_display_find_upload_thread (GstGLDisplay * display, GstVideoFormat v_format,
                                                 guint width, guint height);

G_END_DECLS

#endif /* __GST_GL_UPLOAD_H__ */
