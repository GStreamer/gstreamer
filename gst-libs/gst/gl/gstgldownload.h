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

#ifndef __GST_GL_DOWNLOAD_H__
#define __GST_GL_DOWNLOAD_H__

#include <gst/video/video.h>
#include <gst/gstmemory.h>

#include <gst/gl/gstgl_fwd.h>

G_BEGIN_DECLS

GType gst_gl_download_get_type (void);
#define GST_TYPE_GL_DOWNLOAD (gst_gl_download_get_type())
#define GST_GL_DOWNLOAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_DOWNLOAD,GstGLDownload))
#define GST_GL_DOWNLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_DISPLAY,GstGLDownloadClass))
#define GST_IS_GL_DOWNLOAD(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_DOWNLOAD))
#define GST_IS_GL_DOWNLOAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_DOWNLOAD))
#define GST_GL_DOWNLOAD_CAST(obj) ((GstGLDownload*)(obj))

/**
 * GstGLDownload
 * @parent: the parent object
 * @lock: thread safety
 * @display: a #GstGLDisplay
 * @info: the output video info
 *
 * Download information about GL textures
 */
struct _GstGLDownload
{
  GObject          parent;

  GMutex           lock;

  GstGLDisplay     *display;

  /* output data */
  GstVideoInfo     info;

  /* <private> */
  gpointer         data[GST_VIDEO_MAX_PLANES];
  gboolean         initted;

  /* used for the conversion */
  GLuint           fbo;
  GLuint           depth_buffer;
  GLuint           in_texture;
  GLuint           out_texture[GST_VIDEO_MAX_PLANES];
  GstGLShader     *shader;
  GLint            shader_attr_position_loc;
  GLint            shader_attr_texture_loc;

  GstGLDownloadPrivate *priv;

  gpointer _reserved[GST_PADDING];
};

/**
 * GstGLDownloadClass:
 *
 * The #GstGLDownloadClass struct only contains private data
 */
struct _GstGLDownloadClass
{
  GObjectClass object_class;
};

/**
 * GST_GL_DOWNLOAD_FORMATS:
 *
 * The currently supported formats that can be downloaded
 */
#if !GST_GL_HAVE_GLES2
# define GST_GL_DOWNLOAD_FORMATS "{ RGB, RGBx, RGBA, BGR, BGRx, BGRA, xRGB, " \
                                 "xBGR, ARGB, ABGR, I420, YV12, YUY2, UYVY, AYUV }"
#else /* GST_GL_HAVE_GLES2 */
# define GST_GL_DOWNLOAD_FORMATS "{ RGB, RGBx, RGBA, I420, YV12, YUY2, UYVY, AYUV }"
#endif /* !GST_GL_HAVE_GLES2 */

/**
 * GST_GL_DOWNLOAD_VIDEO_CAPS:
 *
 * The currently supported #GstCaps that can be downloaded
 */
#define GST_GL_DOWNLOAD_VIDEO_CAPS GST_VIDEO_CAPS_MAKE (GST_GL_DOWNLOAD_FORMATS)

GstGLDownload * gst_gl_download_new          (GstGLDisplay * display);

gboolean gst_gl_download_init_format                (GstGLDownload * download, GstVideoFormat v_format,
                                                     guint out_width, guint out_height);
gboolean gst_gl_download_init_format_thread         (GstGLDownload * download, GstVideoFormat v_format,
                                                     guint out_width, guint out_height);

gboolean gst_gl_download_perform_with_memory_thread (GstGLDownload * download, GstGLMemory * gl_mem);
gboolean gst_gl_download_perform_with_data_thread   (GstGLDownload * download, GLuint texture_id,
                                                     gpointer data[GST_VIDEO_MAX_PLANES]);

gboolean gst_gl_download_perform_with_memory        (GstGLDownload * download, GstGLMemory * gl_mem);
gboolean gst_gl_download_perform_with_data          (GstGLDownload * download, GLuint texture_id,
                                                     gpointer data[GST_VIDEO_MAX_PLANES]);

G_END_DECLS

#endif /* __GST_GL_DOWNLOAD_H__ */
