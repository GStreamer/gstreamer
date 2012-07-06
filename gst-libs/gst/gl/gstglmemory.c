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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>

#include "gstglmemory.h"

/*GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_MEMORY);
#define GST_CAT_DEFUALT GST_CAT_GL_MEMORY
GST_DEBUG_CATEGORY_STATIC (GST_CAT_MEMORY);*/

static GstAllocator *_gl_allocator;

typedef struct
{
  GstGLMemory *src;
  GLuint tex_id;
} GstGLMemoryCopyParams;

static void
_gl_mem_init (GstGLMemory * mem, GstAllocator * allocator, GstMemory * parent,
    GstGLDisplay * display, GstVideoFormat v_format, gsize width, gsize height)
{
  gst_memory_init (GST_MEMORY_CAST (mem), GST_MEMORY_FLAG_NO_SHARE,
      allocator, parent, 0, 0, 0, 0);

  mem->display = g_object_ref (display);
  mem->gl_format = GL_RGBA;
  mem->v_format = v_format;
  mem->width = width;
  mem->height = height;

  GST_DEBUG ("new GL memory");
}

static GstGLMemory *
_gl_mem_new (GstAllocator * allocator, GstMemory * parent,
    GstGLDisplay * display, GstVideoFormat v_format, gsize width, gsize height)
{
  GstGLMemory *mem;
  GLuint tex_id;

  gst_gl_display_gen_texture (display, &tex_id, v_format, width, height);
  if (!tex_id) {
    GST_WARNING ("Could not create GL texture with display:%p", display);
  }

  GST_TRACE ("created texture %u", tex_id);

  mem = g_slice_alloc (sizeof (GstGLMemory));
  _gl_mem_init (mem, allocator, parent, display, v_format, width, height);

  mem->tex_id = tex_id;

  return mem;
}

GstMemory *
_gl_allocator_alloc_func (GstAllocator * allocator, gsize size,
    GstAllocationParams * params, gpointer user_data)
{
  g_warning ("use gst_gl_memory_alloc () to allocate from this "
      "GstGLMemory allocator");

  return NULL;
}

gpointer
_gl_mem_map_func (GstGLMemory * gl_mem, gsize maxsize, GstMapFlags flags)
{
  /* should we perform a {up,down}load? */
  return NULL;
}

void
_gl_mem_unmap_func (GstGLMemory * gl_mem)
{
}

void
_gl_mem_free_func (GstGLMemory * gl_mem)
{
  gst_gl_display_del_texture (gl_mem->display, &gl_mem->tex_id);

  g_object_unref (gl_mem->display);

  g_slice_free (GstGLMemory, gl_mem);
}

void
_gl_mem_copy_thread (GstGLDisplay * display, gpointer data)
{
  GstGLMemoryCopyParams *copy_params;
  GstGLMemory *src;
  GLuint tex_id;
  GLuint rboId, fboId;
  GLenum status;
  gsize width, height;
  GLuint gl_format;
  GstVideoFormat v_format;

  copy_params = (GstGLMemoryCopyParams *) data;
  src = copy_params->src;
  width = src->width;
  height = src->height;
  v_format = src->v_format;
  gl_format = src->gl_format;

  if (!GLEW_EXT_framebuffer_object) {
    //turn off the pipeline because Frame buffer object is a not present
    gst_gl_display_set_error (display,
        "Context, EXT_framebuffer_object not supported");
    return;
  }

  gst_gl_display_gen_texture_thread (src->display, &tex_id, v_format, width,
      height);
  if (!tex_id) {
    GST_WARNING ("Could not create GL texture with display:%p", src->display);
  }

  GST_DEBUG ("created texture %i", tex_id);

  /* create a framebuffer object */
  glGenFramebuffersEXT (1, &fboId);
  glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, fboId);

  /* create a renderbuffer object */
  glGenRenderbuffersEXT (1, &rboId);
  glBindRenderbufferEXT (GL_RENDERBUFFER_EXT, rboId);

#ifndef OPENGL_ES2
  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT, width,
      height);
  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH24_STENCIL8_EXT,
      width, height);
#else
  glRenderbufferStorageEXT (GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT16,
      width, height);
#endif
  /* attach the renderbuffer to depth attachment point */
  glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
      GL_RENDERBUFFER_EXT, rboId);

#ifndef OPENGL_ES2
  glFramebufferRenderbufferEXT (GL_FRAMEBUFFER_EXT,
      GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, rboId);
#endif

  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
      GL_TEXTURE_RECTANGLE_ARB, src->tex_id, 0);

  /* check FBO status */
  status = glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT);

  if (status != GL_FRAMEBUFFER_COMPLETE) {
    switch (status) {
      case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
        GST_ERROR ("GL_FRAMEBUFFER_UNSUPPORTED");
        break;

      case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
        GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT");
        break;

      case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
        GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT");
        break;

      case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
        GST_ERROR ("GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS");
        break;

      case GL_FRAMEBUFFER_UNDEFINED:
        GST_ERROR ("GL_FRAMEBUFFER_UNDEFINED");
        break;

      default:
        GST_ERROR ("General FBO error");
    }
    goto fbo_error;
  }

  /* copy tex */
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, tex_id);
  glCopyTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, gl_format, 0, 0,
      width, height, 0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);

  glBindFramebuffer (GL_FRAMEBUFFER_EXT, 0);

  glDeleteRenderbuffers (1, &rboId);
  glDeleteFramebuffers (1, &fboId);

  copy_params->tex_id = tex_id;

fbo_error:
  {
    glDeleteRenderbuffers (1, &rboId);
    glDeleteFramebuffers (1, &fboId);

    copy_params->tex_id = 0;
  }
}

GstMemory *
_gl_mem_copy_func (GstGLMemory * src, gssize offset, gssize size)
{
  GstGLMemory *dest;
  GstGLMemoryCopyParams copy_params;

  copy_params = (GstGLMemoryCopyParams) {
  src, 0,};

  gst_gl_display_thread_add (src->display, _gl_mem_copy_thread, &copy_params);

  dest = g_slice_alloc (sizeof (GstGLMemory));
  _gl_mem_init (dest, src->mem.allocator, NULL, src->display, src->v_format,
      src->width, src->height);

  if (!copy_params.tex_id)
    GST_WARNING ("Could not copy GL Memory");

  dest->tex_id = copy_params.tex_id;

  return (GstMemory *) dest;
}

GstGLMemory *
gst_gl_memory_copy (GstGLMemory * src)
{
  return (GstGLMemory *) _gl_mem_copy_func (src, 0, 0);
}

GstMemory *
_gl_mem_share_func (GstGLMemory * mem, gssize offset, gssize size)
{
  return NULL;
}

gboolean
_gl_mem_is_span_func (GstGLMemory * mem1, GstGLMemory * mem2, gsize * offset)
{
  return FALSE;
}

static void
_gl_mem_destroy_notify (gpointer user_data)
{
  GST_LOG ("GLTexture memory allocator freed");
}

/**
 * gst_gl_memory_alloc:
 * @display:a #GstGLDisplay
 * @format: the format for the texture
 * @width: width of the texture
 * @height: height of the texture
 * 
 * Returns: a GstMemory object with a GL texture specified by @format, @width and @height
 *          from @display
 */
GstMemory *
gst_gl_memory_alloc (GstGLDisplay * display, GstVideoFormat format,
    gsize width, gsize height)
{
  return (GstMemory *) _gl_mem_new (_gl_allocator, NULL, display, format, width,
      height);
}

/**
 * gst_gl_memory_init:
 *
 * Initializes the GL Memory allocator. It is safe to call this function multiple times
 *
 * Returns: a #GstAllocator
 */
void
gst_gl_memory_init (void)
{
  static volatile gsize _init = 0;
  static const GstMemoryInfo mem_info = {
    GST_GL_MEMORY_ALLOCATOR,
    (GstAllocatorAllocFunction) _gl_allocator_alloc_func,
    (GstMemoryMapFunction) _gl_mem_map_func,
    (GstMemoryUnmapFunction) _gl_mem_unmap_func,
    (GstMemoryFreeFunction) _gl_mem_free_func,
    (GstMemoryCopyFunction) _gl_mem_copy_func,
    (GstMemoryShareFunction) _gl_mem_share_func,
    (GstMemoryIsSpanFunction) _gl_mem_is_span_func,
  };

  if (g_once_init_enter (&_init)) {
    _gl_allocator = gst_allocator_new (&mem_info, NULL, _gl_mem_destroy_notify);
    gst_allocator_register (GST_GL_MEMORY_ALLOCATOR,
        gst_allocator_ref (_gl_allocator));
    g_once_init_leave (&_init, 1);
  }
}
