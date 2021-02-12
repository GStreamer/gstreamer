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

#include "gstgtkbasesink.h"


G_BEGIN_DECLS

#define GST_TYPE_GTK_GL_SINK (gst_gtk_gl_sink_get_type())
G_DECLARE_FINAL_TYPE (GstGtkGLSink, gst_gtk_gl_sink, GST, GTK_GL_SINK,
    GstGtkBaseSink)

/**
 * GstGtkGLSink:
 *
 * Opaque #GstGtkGLSink object
 */
struct _GstGtkGLSink
{
  /* <private> */
  GstGtkBaseSink        parent;

  GstGLDisplay         *display;
  GstGLContext         *context;
  GstGLContext         *gtk_context;

  GstGLUpload          *upload;
  GstBuffer            *uploaded_buffer;

  /* read/write with object lock */
  gint                  display_width;
  gint                  display_height;

  gulong                size_allocate_sig_handler;
  gulong                widget_destroy_sig_handler;
};

GST_ELEMENT_REGISTER_DECLARE (gtkglsink);

G_END_DECLS

#endif /* __GST_GTK_GL_SINK_H__ */
