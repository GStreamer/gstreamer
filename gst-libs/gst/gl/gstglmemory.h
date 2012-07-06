/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#ifndef _GST_GL_MEMORY_H_
#define _GST_GL_MEMORY_H_

#include <gst/gst.h>
#include <gst/gstmemory.h>

#include "gstgldisplay.h"

G_BEGIN_DECLS

#define GST_TYPE_GL_MEMORY (gst_gl_memory_get_type())
GType gst_gl_memory_get_type(void);

typedef struct _GstGLMemoryInitParams GstGLMemoryInitParams;
typedef struct _GstGLMemory GstGLMemory;

/**
 * GstGLMemory:
 * @mem: the parent object
 * @display: the #GstGLDisplay to use
 * @tex_id: the texture id
 * @gl_format: the format of the texture
 * @width: width of the texture
 * @height: height of the texture
 *
 * Represents information about a GL texture
 */
struct _GstGLMemory
{
  GstMemory          mem;

  GstGLDisplay      *display;
  GLuint             tex_id;
  GstVideoFormat     v_format;
  GLenum             gl_format;
  GLuint             width;
  GLuint             height;
};

#define GST_GL_MEMORY_ALLOCATOR   "GLMemory"

void gst_gl_memory_init (void);

GstMemory * gst_gl_memory_alloc (GstGLDisplay * display, GstVideoFormat format,
                                gsize width, gsize height);

G_END_DECLS

#endif /* _GST_GL_MEMORY_H_ */
