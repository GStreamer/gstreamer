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

#ifndef _GST_GL_MEMORY_H_
#define _GST_GL_MEMORY_H_

#include <gst/gst.h>
#include <gst/gstallocator.h>
#include <gst/gstmemory.h>
#include <gst/video/video.h>

#include <gst/gl/gstglbasememory.h>

G_BEGIN_DECLS

#define GST_TYPE_GL_MEMORY_ALLOCATOR (gst_gl_memory_allocator_get_type())
GType gst_gl_memory_allocator_get_type(void);

#define GST_IS_GL_MEMORY_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GL_MEMORY_ALLOCATOR))
#define GST_IS_GL_MEMORY_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GL_MEMORY_ALLOCATOR))
#define GST_GL_MEMORY_ALLOCATOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_GL_MEMORY_ALLOCATOR, GstGLMemoryAllocatorClass))
#define GST_GL_MEMORY_ALLOCATOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GL_MEMORY_ALLOCATOR, GstGLMemoryAllocator))
#define GST_GL_MEMORY_ALLOCATOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GL_MEMORY_ALLOCATOR, GstGLMemoryAllocatorClass))
#define GST_GL_MEMORY_ALLOCATOR_CAST(obj)            ((GstGLMemoryAllocator *)(obj))

#define GST_GL_MEMORY_CAST(obj) ((GstGLMemory *) obj)

#define GST_CAPS_FEATURE_MEMORY_GL_MEMORY "memory:GLMemory"
#define GST_GL_MEMORY_VIDEO_FORMATS_STR \
    "{ RGBA, BGRA, RGBx, BGRx, ARGB, ABGR, xRGB, xBGR, RGB, BGR, RGB16, BGR16, " \
    "AYUV, I420, YV12, NV12, NV21, YUY2, UYVY, Y41B, Y42B, Y444, " \
    "GRAY8, GRAY16_LE, GRAY16_BE }"

/**
 * GstGLMemory:
 * @mem: the parent object
 * @context: the #GstGLContext to use for GL operations
 * @tex_id: the GL texture id for this memory
 * @tex_target: the GL texture target for this memory
 * @tex_type: the texture type
 * @info: the texture's #GstVideoInfo
 * @valign: data alignment for system memory mapping
 * @plane: data plane in @info
 * @tex_scaling: GL shader scaling parameters for @valign and/or width/height
 *
 * Represents information about a GL texture
 */
struct _GstGLMemory
{
  GstGLBaseMemory           mem;

  guint                     tex_id;
  GstGLTextureTarget        tex_target;
  GstVideoGLTextureType     tex_type;
  GstVideoInfo              info;
  GstVideoAlignment         valign;
  guint                     plane;
  gfloat                    tex_scaling[2];

  /* <protected> */
  gboolean                  texture_wrapped;
  guint                     unpack_length;
  guint                     tex_width;
};

typedef struct _GstGLVideoAllocationParams GstGLVideoAllocationParams;

#define GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_VIDEO (1 << 3)

struct _GstGLVideoAllocationParams
{
  GstGLAllocationParams  parent;

  GstVideoInfo          *v_info;
  guint                  plane;
  GstVideoAlignment     *valign;
  GstGLTextureTarget     target;
};

gboolean        gst_gl_video_allocation_params_init_full        (GstGLVideoAllocationParams * params,
                                                                 gsize struct_size,
                                                                 guint alloc_flags,
                                                                 GstGLAllocationParamsCopyFunc copy,
                                                                 GstGLAllocationParamsFreeFunc free,
                                                                 GstGLContext * context,
                                                                 GstAllocationParams * alloc_params,
                                                                 GstVideoInfo * v_info,
                                                                 guint plane,
                                                                 GstVideoAlignment * valign,
                                                                 GstGLTextureTarget target,
                                                                 gpointer wrapped_data,
                                                                 guint gl_handle,
                                                                 gpointer user_data,
                                                                 GDestroyNotify notify);
GstGLVideoAllocationParams * gst_gl_video_allocation_params_new (GstGLContext * context,
                                                                 GstAllocationParams * alloc_params,
                                                                 GstVideoInfo * v_info,
                                                                 guint plane,
                                                                 GstVideoAlignment * valign,
                                                                 GstGLTextureTarget target);
GstGLVideoAllocationParams * gst_gl_video_allocation_params_new_wrapped_data    (GstGLContext * context,
                                                                                 GstAllocationParams * alloc_params,
                                                                                 GstVideoInfo * v_info,
                                                                                 guint plane,
                                                                                 GstVideoAlignment * valign,
                                                                                 GstGLTextureTarget target,
                                                                                 gpointer wrapped_data,
                                                                                 gpointer user_data,
                                                                                 GDestroyNotify notify);

GstGLVideoAllocationParams * gst_gl_video_allocation_params_new_wrapped_texture (GstGLContext * context,
                                                                                 GstAllocationParams * alloc_params,
                                                                                 GstVideoInfo * v_info,
                                                                                 guint plane,
                                                                                 GstVideoAlignment * valign,
                                                                                 GstGLTextureTarget target,
                                                                                 guint tex_id,
                                                                                 gpointer user_data,
                                                                                 GDestroyNotify notify);

/* subclass usage */
void            gst_gl_video_allocation_params_free_data    (GstGLVideoAllocationParams * params);
/* subclass usage */
void            gst_gl_video_allocation_params_copy_data    (GstGLVideoAllocationParams * src_vid,
                                                             GstGLVideoAllocationParams * dest_vid);

/**
 * GstGLMemoryAllocator
 *
 * Opaque #GstGLMemoryAllocator struct
 */
struct _GstGLMemoryAllocator
{
  GstGLBaseMemoryAllocator parent;
};

/**
 * GstGLMemoryAllocatorClass:
 *
 * The #GstGLMemoryAllocatorClass only contains private data
 */
struct _GstGLMemoryAllocatorClass
{
  GstGLBaseMemoryAllocatorClass             parent_class;

  GstGLBaseMemoryAllocatorMapFunction       map;
  GstGLBaseMemoryAllocatorCopyFunction      copy;
  GstGLBaseMemoryAllocatorUnmapFunction     unmap;
};

#include <gst/gl/gstglbasememory.h>

/**
 * GST_GL_MEMORY_ALLOCATOR_NAME:
 *
 * The name of the GL memory allocator
 */
#define GST_GL_MEMORY_ALLOCATOR_NAME   "GLMemory"

void            gst_gl_memory_init_once (void);
gboolean        gst_is_gl_memory (GstMemory * mem);

void            gst_gl_memory_init              (GstGLMemory * mem,
                                                 GstAllocator * allocator,
                                                 GstMemory * parent,
                                                 GstGLContext * context,
                                                 GstGLTextureTarget target,
                                                 GstAllocationParams *params,
                                                 GstVideoInfo * info,
                                                 guint plane,
                                                 GstVideoAlignment *valign,
                                                 gpointer user_data,
                                                 GDestroyNotify notify);

gboolean        gst_gl_memory_copy_into         (GstGLMemory *gl_mem,
                                                 guint tex_id,
                                                 GstGLTextureTarget target,
                                                 GstVideoGLTextureType tex_type,
                                                 gint width,
                                                 gint height);
gboolean        gst_gl_memory_copy_teximage     (GstGLMemory * src,
                                                 guint tex_id,
                                                 GstGLTextureTarget out_target,
                                                 GstVideoGLTextureType out_tex_type,
                                                 gint width,
                                                 gint height);

gboolean        gst_gl_memory_read_pixels       (GstGLMemory * gl_mem,
                                                 gpointer read_pointer);
void            gst_gl_memory_texsubimage       (GstGLMemory * gl_mem,
                                                 gpointer read_pointer);

/* accessors */
gint                    gst_gl_memory_get_texture_width     (GstGLMemory * gl_mem);
gint                    gst_gl_memory_get_texture_height    (GstGLMemory * gl_mem);
GstVideoGLTextureType   gst_gl_memory_get_texture_type      (GstGLMemory * gl_mem);
GstGLTextureTarget      gst_gl_memory_get_texture_target    (GstGLMemory * gl_mem);
guint                   gst_gl_memory_get_texture_id        (GstGLMemory * gl_mem);

gboolean                gst_gl_memory_setup_buffer          (GstGLMemoryAllocator * allocator,
                                                             GstBuffer * buffer,
                                                             GstGLVideoAllocationParams * params);


GstGLMemoryAllocator *  gst_gl_memory_allocator_get_default (GstGLContext *context);

G_END_DECLS

#endif /* _GST_GL_MEMORY_H_ */
