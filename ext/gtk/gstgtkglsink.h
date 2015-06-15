/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_GTK_GL_SINK_H__
#define __GST_GTK_GL_SINK_H__

#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>
#include <gst/gl/gl.h>

typedef struct _GstGtkGLSink GstGtkGLSink;
typedef struct _GstGtkGLSinkClass GstGtkGLSinkClass;
typedef struct _GstGtkGLSinkPrivate GstGtkGLSinkPrivate;

#include <gtkgstglwidget.h>

G_BEGIN_DECLS

GType gst_gtk_gl_sink_get_type (void);
#define GST_TYPE_GTK_GL_SINK            (gst_gtk_gl_sink_get_type())
#define GST_GTK_GL_SINK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GTK_GL_SINK,GstGtkGLSink))
#define GST_GTK_GL_SINK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GTK_GL_SINK,GstGtkGLSinkClass))
#define GST_IS_GTK_GL_SINK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GTK_GL_SINK))
#define GST_IS_GTK_GL_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GTK_GL_SINK))
#define GST_GTK_GL_SINK_CAST(obj)       ((GstGtkGLSink*)(obj))

/**
 * GstGtkGLSink:
 *
 * Opaque #GstGtkGLSink object
 */
struct _GstGtkGLSink
{
  /* <private> */
  GstVideoSink          parent;

  GtkGstGLWidget       *widget;

  GstVideoInfo          v_info;
  GstBufferPool        *pool;

  GstGLDisplay         *display;
  GstGLContext         *context;
  GstGLContext         *gtk_context;

  GstGLUpload          *upload;
  GstBuffer            *uploaded_buffer;

  /* properties */
  gboolean              force_aspect_ratio;
  GBinding             *bind_force_aspect_ratio;

  gint                  par_n;
  gint                  par_d;
  GBinding             *bind_pixel_aspect_ratio;

  gboolean              ignore_alpha;
  GBinding             *bind_ignore_alpha;

  GstGtkGLSinkPrivate  *priv;
};

/**
 * GstGtkGLSinkClass:
 *
 * The #GstGtkGLSinkClass struct only contains private data
 */
struct _GstGtkGLSinkClass
{
  /* <private> */
  GstVideoSinkClass object_class;
};

GstGtkGLSink *    gst_gtk_gl_sink_new (void);

G_END_DECLS

#endif /* __GST_GTK_GL_SINK_H__ */
