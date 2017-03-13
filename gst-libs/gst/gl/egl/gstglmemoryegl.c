/*
 * GStreamer
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2014 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2015 Igalia
 *    Author: Gwang Yoon Hwang <yoon@igalia.com>
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

/**
 * SECTION:gstglmemoryegl
 * @short_description: memory subclass for EGLImage's
 * @see_also: #GstGLMemory, #GstGLAllocator, #GstGLBufferPool
 *
 * #GstGLMemoryEGL is created or wrapped through gst_gl_base_memory_alloc()
 * with #GstGLVideoAllocationParams.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gl/egl/gstglmemoryegl.h>
#include <gst/gl/egl/gstglcontext_egl.h>

static GstAllocator *_gl_memory_egl_allocator;

GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_MEMORY);
#define GST_CAT_DEFAULT GST_CAT_GL_MEMORY

#define parent_class gst_gl_memory_egl_allocator_parent_class
G_DEFINE_TYPE (GstGLMemoryEGLAllocator, gst_gl_memory_egl_allocator,
    GST_TYPE_GL_MEMORY_ALLOCATOR);

/**
 * gst_is_gl_memory_egl:
 * @mem: a #GstMemory to test
 *
 * Returns: whether @mem is a #GstGLMemoryEGL
 *
 * Since: 1.10
 */
gboolean
gst_is_gl_memory_egl (GstMemory * mem)
{
  return mem != NULL && mem->allocator != NULL
      && g_type_is_a (G_OBJECT_TYPE (mem->allocator),
      GST_TYPE_GL_MEMORY_EGL_ALLOCATOR);
}

static GstGLMemoryEGL *
_gl_mem_get_parent (GstGLMemoryEGL * gl_mem)
{
  GstGLMemoryEGL *parent = (GstGLMemoryEGL *) gl_mem->mem.mem.mem.parent;
  return parent ? parent : gl_mem;
}

/**
 * gst_gl_memory_egl_get_image:
 * @mem: a #GstGLMemoryEGL
 *
 * Returns: The EGLImage held by @mem
 *
 * Since: 1.10
 */
EGLImageKHR
gst_gl_memory_egl_get_image (GstGLMemoryEGL * mem)
{
  g_return_val_if_fail (gst_is_gl_memory_egl (GST_MEMORY_CAST (mem)),
      EGL_NO_IMAGE_KHR);
  return gst_egl_image_get_image (_gl_mem_get_parent (mem)->image);
}

/**
 * gst_gl_memory_egl_get_display:
 * @mem: a #GstGLMemoryEGL
 *
 * Returns: The EGLDisplay @mem is associated with
 *
 * Since: 1.10
 */
EGLDisplay
gst_gl_memory_egl_get_display (GstGLMemoryEGL * mem)
{
  g_return_val_if_fail (gst_is_gl_memory_egl (GST_MEMORY_CAST (mem)), NULL);
  return GST_GL_CONTEXT_EGL (_gl_mem_get_parent (mem)->mem.mem.
      context)->egl_display;
}

static GstMemory *
_gl_mem_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_warning ("Use gst_gl_base_memory_allocator_alloc() to allocate from this "
      "GstGLMemoryEGL allocator");

  return NULL;
}

static void
_gl_mem_destroy (GstGLMemoryEGL * mem)
{
  if (mem->image)
    gst_egl_image_unref (mem->image);

  GST_GL_BASE_MEMORY_ALLOCATOR_CLASS (parent_class)->destroy ((GstGLBaseMemory
          *) mem);
}

static GstGLMemoryEGL *
_gl_mem_egl_alloc (GstGLBaseMemoryAllocator * allocator,
    GstGLVideoAllocationParams * params)
{
  guint alloc_flags = params->parent.alloc_flags;
  GstGLMemoryEGL *mem;

  g_return_val_if_fail (alloc_flags & GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_VIDEO,
      NULL);
  g_return_val_if_fail ((alloc_flags &
          GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_WRAP_SYSMEM) == 0, NULL);
  if (alloc_flags & GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_WRAP_GPU_HANDLE) {
    g_return_val_if_fail (GST_IS_EGL_IMAGE (params->parent.gl_handle), NULL);
  }

  mem = g_new0 (GstGLMemoryEGL, 1);
  if (alloc_flags & GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_WRAP_GPU_HANDLE) {
    if (params->target != GST_GL_TEXTURE_TARGET_2D) {
      g_free (mem);
      GST_CAT_ERROR (GST_CAT_GL_MEMORY, "GstGLMemoryEGL only supports wrapping "
          "2D textures");
      return NULL;
    }
    mem->image = gst_egl_image_ref (params->parent.gl_handle);
  }

  gst_gl_memory_init (GST_GL_MEMORY_CAST (mem), GST_ALLOCATOR_CAST (allocator),
      NULL, params->parent.context, params->target, params->tex_format,
      params->parent.alloc_params, params->v_info, params->plane,
      params->valign, params->parent.user_data, params->parent.notify);

  return mem;
}

static gboolean
_gl_mem_create (GstGLMemoryEGL * gl_mem, GError ** error)
{
  GstGLContext *context = gl_mem->mem.mem.context;
  const GstGLFuncs *gl = context->gl_vtable;
  GstGLBaseMemoryAllocatorClass *alloc_class;

  if (!gst_gl_context_check_feature (GST_GL_CONTEXT (context),
          "EGL_KHR_image_base")) {
    g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_WRONG_API,
        "EGL_KHR_image_base is not supported");
    return FALSE;
  }

  alloc_class = GST_GL_BASE_MEMORY_ALLOCATOR_CLASS (parent_class);
  if (!alloc_class->create ((GstGLBaseMemory *) gl_mem, error))
    return FALSE;

  if (gl_mem->image == NULL) {
    gl_mem->image = gst_egl_image_from_texture (context,
        (GstGLMemory *) gl_mem, NULL);

    if (!gl_mem->image) {
      g_set_error (error, GST_GL_CONTEXT_ERROR, GST_GL_CONTEXT_ERROR_FAILED,
          "Failed to create EGLImage");
      return FALSE;
    }
  } else {
    gl->ActiveTexture (GL_TEXTURE0 + gl_mem->mem.plane);
    gl->BindTexture (GL_TEXTURE_2D, gl_mem->mem.tex_id);
    gl->EGLImageTargetTexture2D (GL_TEXTURE_2D,
        gst_egl_image_get_image (GST_EGL_IMAGE (gl_mem->image)));
  }

  return TRUE;
}

static GstMemory *
_gl_mem_copy (GstGLMemoryEGL * src, gssize offset, gssize size)
{
  GST_CAT_ERROR (GST_CAT_GL_MEMORY, "GstGLMemoryEGL does not support copy");
  return NULL;
}

static void
gst_gl_memory_egl_allocator_class_init (GstGLMemoryEGLAllocatorClass * klass)
{
  GstGLBaseMemoryAllocatorClass *gl_base;
  GstGLMemoryAllocatorClass *gl_tex;
  GstAllocatorClass *allocator_class;

  gl_tex = (GstGLMemoryAllocatorClass *) klass;
  gl_base = (GstGLBaseMemoryAllocatorClass *) klass;
  allocator_class = (GstAllocatorClass *) klass;

  gl_base->alloc = (GstGLBaseMemoryAllocatorAllocFunction) _gl_mem_egl_alloc;
  gl_base->create = (GstGLBaseMemoryAllocatorCreateFunction) _gl_mem_create;
  gl_base->destroy = (GstGLBaseMemoryAllocatorDestroyFunction) _gl_mem_destroy;
  gl_tex->copy = (GstGLBaseMemoryAllocatorCopyFunction) _gl_mem_copy;

  allocator_class->alloc = _gl_mem_alloc;
}

static void
gst_gl_memory_egl_allocator_init (GstGLMemoryEGLAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_GL_MEMORY_EGL_ALLOCATOR_NAME;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

/**
 * gst_gl_memory_egl_init_once:
 *
 * Initializes the GL Memory allocator. It is safe to call this function
 * multiple times.  This must be called before any other GstGLMemoryEGL operation.
 *
 * Since: 1.10
 */
void
gst_gl_memory_egl_init_once (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    gst_gl_memory_init_once ();

    GST_DEBUG_CATEGORY_INIT (GST_CAT_GL_MEMORY, "glmemory", 0,
        "OpenGL Texture with EGLImage memory");

    _gl_memory_egl_allocator =
        g_object_new (GST_TYPE_GL_MEMORY_EGL_ALLOCATOR, NULL);

    /* The allocator is never unreffed */
    GST_OBJECT_FLAG_SET (_gl_memory_egl_allocator,
        GST_OBJECT_FLAG_MAY_BE_LEAKED);

    gst_allocator_register (GST_GL_MEMORY_EGL_ALLOCATOR_NAME,
        gst_object_ref (_gl_memory_egl_allocator));
    g_once_init_leave (&_init, 1);
  }
}
