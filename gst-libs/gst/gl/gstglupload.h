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

#ifndef __GST_GL_UPLOAD_H__
#define __GST_GL_UPLOAD_H__

#include <gst/video/video.h>
#include <gst/gstmemory.h>

#include <gst/gl/gstgl_fwd.h>

G_BEGIN_DECLS

GType gst_gl_upload_get_type (void);
#define GST_TYPE_GL_UPLOAD (gst_gl_upload_get_type())
#define GST_GL_UPLOAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_UPLOAD,GstGLUpload))
#define GST_GL_UPLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_DISPLAY,GstGLUploadClass))
#define GST_IS_GL_UPLOAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_UPLOAD))
#define GST_IS_GL_UPLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_UPLOAD))
#define GST_GL_UPLOAD_CAST(obj) ((GstGLUpload*)(obj))

/**
 * GstGLUpload
 * @parent: the parent object
 * @lock: thread safety
 * @display: a #GstGLDisplay
 * @info: the output video info
 *
 * Upload information for GL textures
 */
struct _GstGLUpload
{
  GObject          parent;

  GMutex           lock;

  GstGLContext    *context;

  /* input data */
  GstVideoInfo    info;

  /* <private> */
  gpointer         data[GST_VIDEO_MAX_PLANES];
  gboolean         initted;

  /* used for the conversion */
  GLuint           fbo;
  GLuint           depth_buffer;
  GLuint           out_texture;
  GLuint           in_texture[GST_VIDEO_MAX_PLANES];
  guint            in_width;
  guint            in_height;
  GstGLShader     *shader;
  GLint            shader_attr_position_loc;
  GLint            shader_attr_texture_loc;

  /* <private> */
  GstGLUploadPrivate *priv;

  gpointer _reserved[GST_PADDING];
};

/**
 * GstGLUploadClass:
 *
 * The #GstGLUploadClass struct only contains private data
 */
struct _GstGLUploadClass
{
  GObjectClass object_class;
};

/**
 * GST_GL_UPLOAD_FORMATS:
 *
 * The currently supported formats that can be uploaded
 */
#define GST_GL_UPLOAD_FORMATS "{ RGB, RGBx, RGBA, BGR, BGRx, BGRA, xRGB, " \
                               "xBGR, ARGB, ABGR, Y444, I420, YV12, Y42B, " \
                               "Y41B, NV12, NV21, YUY2, UYVY, AYUV }"

/**
 * GST_GL_UPLOAD_VIDEO_CAPS:
 *
 * The currently supported #GstCaps that can be uploaded
 */
#define GST_GL_UPLOAD_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_GL_UPLOAD_FORMATS)

GstGLUpload * gst_gl_upload_new            (GstGLContext * context);

gboolean gst_gl_upload_init_format         (GstGLUpload * upload, GstVideoFormat v_format,
                                            guint in_width, guint in_height,
                                            guint out_width, guint out_height);

gboolean gst_gl_upload_perform_with_memory        (GstGLUpload * upload, GstGLMemory * gl_mem);
gboolean gst_gl_upload_perform_with_data          (GstGLUpload * upload, GLuint texture_id,
                                                   gpointer data[GST_VIDEO_MAX_PLANES]);

G_END_DECLS

#endif /* __GST_GL_UPLOAD_H__ */
