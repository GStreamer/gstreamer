/* 
 * GStreamer
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GST_GL_BUFFER_H_
#define _GST_GL_BUFFER_H_

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstgldisplay.h"

typedef struct _GstGLBuffer GstGLBuffer;

#define GST_TYPE_GL_BUFFER (gst_gl_buffer_get_type())

#define GST_IS_GL_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GL_BUFFER))
#define GST_GL_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GL_BUFFER, GstGLBuffer))



struct _GstGLBuffer {
    GstBuffer buffer;

    GstGLDisplay *display;

    GstVideoFormat video_format;

    gint width;
    gint height;
    GLuint texture;
    GLuint texture_u;
    GLuint texture_v;

    gint widthGL;
    gint heightGL;
    GLuint textureGL;
};

GType gst_gl_buffer_get_type (void);

#define gst_gl_buffer_ref(x) ((GstGLBuffer *)(gst_buffer_ref((GstBuffer *)(x))))
#define gst_gl_buffer_unref(x) (gst_buffer_unref((GstBuffer *)(x)))

GstGLBuffer* gst_gl_buffer_new_from_video_format (GstGLDisplay* display, GstVideoFormat format, 
												  gint context_width, gint context_height, 
                                                  gint width, gint height);
gint gst_gl_buffer_format_get_size (GstVideoFormat format, gint width, gint height);
gboolean gst_gl_buffer_format_parse_caps (GstCaps* caps, GstVideoFormat* format,
										  gint* width, gint* height);


#define GST_GL_VIDEO_CAPS \
  "video/x-raw-gl," \
  "width=(int)[1,8000]," \
  "height=(int)[1,6000]," \
  "pixel-aspect-ratio=(fraction)1/1," \
  "framerate=(fraction)[0/1,100/1]"

#endif

