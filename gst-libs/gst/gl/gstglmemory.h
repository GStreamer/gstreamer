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
#include <gst/gstallocator.h>

#include "gstgldisplay.h"
#include "gstgldownload.h"
#include "gstglupload.h"

G_BEGIN_DECLS

typedef struct _GstGLUpload GstGLUpload;
typedef struct _GstGLDownload GstGLDownload;

#define GST_TYPE_GL_ALLOCATOR (gst_gl_allocator_get_type())
GType gst_gl_allocator_get_type(void);

#define GST_IS_GL_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GL_ALLOCATOR))
#define GST_IS_GL_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GL_ALLOCATOR))
#define GST_GL_ALLOCATOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_GL_ALLOCATOR, GstGLAllocatorClass))
#define GST_GL_ALLOCATOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GL_ALLOCATOR, GstGLAllocator))
#define GST_GL_ALLOCATOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GL_ALLOCATOR, GstGLAllocatorClass))
#define GST_GL_ALLOCATOR_CAST(obj)            ((GstGLAllocator *)(obj))

typedef struct _GstGLMemory GstGLMemory;
typedef struct _GstGLAllocator GstGLAllocator;
typedef struct _GstGLAllocatorClass GstGLAllocatorClass;

typedef enum
{
  GST_GL_MEMORY_FLAG_DOWNLOAD_INITTED = (GST_MEMORY_FLAG_LAST << 0),
  GST_GL_MEMORY_FLAG_UPLOAD_INITTED   = (GST_MEMORY_FLAG_LAST << 1),
  GST_GL_MEMORY_FLAG_NEED_DOWNLOAD   = (GST_MEMORY_FLAG_LAST << 2),
  GST_GL_MEMORY_FLAG_NEED_UPLOAD     = (GST_MEMORY_FLAG_LAST << 3)
} GstGLMemoryFlags;

/**
 * GST_MAP_GL:
 *
 * Flag indicating that we should map the GL object instead of to system memory.
 *
 * Combining #GST_MAP_GL with #GST_MAP_WRITE has the same semantics as though
 * you are writing to OpenGL. Conversely, combining #GST_MAP_GL with
 * #GST_MAP_READ has the same semantics as though you are reading from OpenGL.
 */
#define GST_MAP_GL GST_MAP_FLAG_LAST << 1

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

  GstGLDownload     *download;
  GLenum             render_target;
  GstGLUpload       *upload;

  GstMapFlags        map_flags;
  gpointer           data;

  gboolean           wrapped;
  GDestroyNotify     notify;
  gpointer           user_data;
};

#define GST_GL_MEMORY_ALLOCATOR   "GLMemory"

#define GST_GL_MEMORY_FLAGS(mem) GST_MEMORY_FLAGS(mem)
#define GST_GL_MEMORY_FLAG_IS_SET(mem,flag) GST_MEMORY_FLAG_IS_SET(mem,flag)
#define GST_GL_MEMORY_FLAG_SET(mem,flag) GST_MINI_OBJECT_FLAG_SET(mem,flag)
#define GST_GL_MEMORY_FLAG_UNSET(mem,flag) GST_MEMORY_FLAG_UNSET(mem,flag)

void gst_gl_memory_init (void);

GstMemory * gst_gl_memory_alloc (GstGLDisplay * display, GstVideoFormat format,
                                 gsize width, gsize height);

GstGLMemory * gst_gl_memory_wrapped (GstGLDisplay * display, GstVideoFormat format,
                                     guint width, guint height, gpointer data,
                                     gpointer user_data, GDestroyNotify notify);

gboolean gst_is_gl_memory (GstMemory * mem);

struct _GstGLAllocator
{
  GstAllocator parent;
};

struct _GstGLAllocatorClass
{
  GstAllocatorClass parent_class;
};

G_END_DECLS

#endif /* _GST_GL_MEMORY_H_ */
