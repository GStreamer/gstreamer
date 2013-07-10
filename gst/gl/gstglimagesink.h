/*
 * GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005,2006,2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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

#ifndef _GLIMAGESINK_H_
#define _GLIMAGESINK_H_

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include "gl.h"

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_debug_glimage_sink);

#define GST_TYPE_GLIMAGE_SINK \
    (gst_glimage_sink_get_type())
#define GST_GLIMAGE_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GLIMAGE_SINK,GstGLImageSink))
#define GST_GLIMAGE_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GLIMAGE_SINK,GstGLImageSinkClass))
#define GST_IS_GLIMAGE_SINK(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GLIMAGE_SINK))
#define GST_IS_GLIMAGE_SINK_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GLIMAGE_SINK))

typedef struct _GstGLImageSink GstGLImageSink;
typedef struct _GstGLImageSinkClass GstGLImageSinkClass;

struct _GstGLImageSink
{
    GstVideoSink video_sink;

    //properties
    gchar *display_name;

    gulong window_id;
    gulong new_window_id;

    //caps
    GstVideoInfo info;

    GstGLDisplay *display;
    GstBuffer *stored_buffer;
    GstGLUpload *upload;
    GLuint     tex_id;

    CRCB clientReshapeCallback;
    CDCB clientDrawCallback;
    gpointer client_data;

    volatile gint to_quit;
    gboolean keep_aspect_ratio;
    GValue *par;

    GstBufferPool *pool;

  /* action redisplay */
  GLuint redisplay_texture;
  GLuint redisplay_texture_width;
  GLuint redisplay_texture_height;
#if GST_GL_HAVE_GLES2
  GstGLShader *redisplay_shader;
  gchar *redisplay_vertex_shader_str_gles2;
  gchar *redisplay_fragment_shader_str_gles2;
  GLint redisplay_attr_position_loc;
  GLint redisplay_attr_texture_loc;
#endif

};

struct _GstGLImageSinkClass
{
    GstVideoSinkClass video_sink_class;
};

GType gst_glimage_sink_get_type(void);

G_END_DECLS

#endif

