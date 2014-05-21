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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_GL_MEMORY_H_
#define _GST_GL_MEMORY_H_

#include <gst/gst.h>
#include <gst/gstallocator.h>
#include <gst/gstmemory.h>
#include <gst/video/video.h>

#include <gst/gl/gl.h>

G_BEGIN_DECLS

#define GST_TYPE_GL_ALLOCATOR (gst_gl_allocator_get_type())
GType gst_gl_allocator_get_type(void);

#define GST_IS_GL_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GL_ALLOCATOR))
#define GST_IS_GL_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GL_ALLOCATOR))
#define GST_GL_ALLOCATOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_GL_ALLOCATOR, GstGLAllocatorClass))
#define GST_GL_ALLOCATOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GL_ALLOCATOR, GstGLAllocator))
#define GST_GL_ALLOCATOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GL_ALLOCATOR, GstGLAllocatorClass))
#define GST_GL_ALLOCATOR_CAST(obj)            ((GstGLAllocator *)(obj))

/**
 * GstGLMemoryFlags:
 *
 * Flags indicating the current state of a #GstGLMemory
 */
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
 * @context: the #GstGLContext to use for GL operations
 * @tex_id: the texture id for this memory
 * @v_format: the video format of this texture
 * @gl_format: the format of the texture
 * @width: width of the texture
 * @height: height of the texture
 * @download: the object used to download this texture into @v_format
 * @upload: the object used to upload this texture from @v_format
 *
 * Represents information about a GL texture
 */
struct _GstGLMemory
{
  GstMemory                    mem;

  GstGLContext         *context;
  guint                 tex_id;
  GstVideoGLTextureType tex_type;
  gint                  width;
  gint                  height;
  gint                  stride;
  gfloat                tex_scaling[2];

  /* <private> */
  GstMapFlags           map_flags;
  gpointer              data;

  gboolean              data_wrapped;
  gboolean              texture_wrapped;
  GDestroyNotify        notify;
  gpointer              user_data;
  guint                 pbo;
  guint                 unpack_length;

  gpointer              _gst_reserved[GST_PADDING];
};

#define GST_CAPS_FEATURE_MEMORY_GL_MEMORY "memory:GLMemory"

/**
 * GST_GL_MEMORY_ALLOCATOR:
 *
 * The name of the GL memore allocator
 */
#define GST_GL_MEMORY_ALLOCATOR   "GLMemory"

/**
 * GST_GL_MEMORY_FLAGS:
 * @mem: a #GstGLMemory
 *
 * Get the currently set flags on @mem
 */
#define GST_GL_MEMORY_FLAGS(mem) GST_MEMORY_FLAGS(mem)

/**
 * GST_GL_MEMORY_FLAG_IS_SET:
 * @mem: a #GstGLMemory
 * @flag: a flag
 *
 * Whether @flag is set on @mem
 */
#define GST_GL_MEMORY_FLAG_IS_SET(mem,flag) GST_MEMORY_FLAG_IS_SET(mem,flag)

/**
 * GST_GL_MEMORY_FLAG_SET:
 * @mem: a #GstGLMemory
 * @flag: a flag
 *
 * Set @flag on @mem
 */
#define GST_GL_MEMORY_FLAG_SET(mem,flag) GST_MINI_OBJECT_FLAG_SET(mem,flag)

/**
 * GST_GL_MEMORY_FLAG_UNSET:
 * @mem: a #GstGLMemory
 * @flag: a flag
 *
 * Unset @flag on @mem
 */
#define GST_GL_MEMORY_FLAG_UNSET(mem,flag) GST_MEMORY_FLAG_UNSET(mem,flag)

void          gst_gl_memory_init (void);
gboolean      gst_is_gl_memory (GstMemory * mem);

GstMemory *   gst_gl_memory_alloc   (GstGLContext * context, GstVideoGLTextureType tex_type, 
                                     gint width, gint height, gint stride);
GstGLMemory * gst_gl_memory_wrapped (GstGLContext * context, GstVideoGLTextureType tex_type, 
                                     gint width, gint height, gint stride,
                                     gpointer data, gpointer user_data,
                                     GDestroyNotify notify);
GstGLMemory * gst_gl_memory_wrapped_texture (GstGLContext * context, guint texture_id,
                                             GstVideoGLTextureType tex_type, 
                                             gint width, gint height,
                                             gpointer user_data, GDestroyNotify notify);

gboolean      gst_gl_memory_copy_into_texture (GstGLMemory *gl_mem, guint tex_id,
                                               GstVideoGLTextureType tex_type, 
                                               gint width, gint height, gint stride,
                                               gboolean respecify);

gboolean      gst_gl_memory_setup_buffer  (GstGLContext * context, GstVideoInfo * info,
                                           GstBuffer * buffer);
gboolean      gst_gl_memory_setup_wrapped (GstGLContext * context, GstVideoInfo * info,
                                           gpointer data[GST_VIDEO_MAX_PLANES],
                                           GstGLMemory *textures[GST_VIDEO_MAX_PLANES]);

GstVideoGLTextureType gst_gl_texture_type_from_format (GstGLContext *context, GstVideoFormat v_format, guint plane);

/**
 * GstGLAllocator
 *
 * Opaque #GstGLAllocator struct
 */
struct _GstGLAllocator
{
  GstAllocator parent;
};

/**
 * GstGLAllocatorClass:
 *
 * The #GstGLAllocatorClass only contains private data
 */
struct _GstGLAllocatorClass
{
  GstAllocatorClass parent_class;
};

G_END_DECLS

#endif /* _GST_GL_MEMORY_H_ */
