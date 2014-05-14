/*
 * GStreamer
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2014 Julien Isorce <julien.isorce@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsteglimagememory.h"
#include "gstglcontext_egl.h"

GST_DEBUG_CATEGORY_STATIC (GST_CAT_EGL_IMAGE_MEMORY);
#define GST_CAT_DEFAULT GST_CAT_EGL_IMAGE_MEMORY

typedef void (*GstEGLImageDestroyNotify) (GstGLContextEGL * context,
    gpointer data);

typedef struct
{
  GstMemory parent;

  GstGLContextEGL *context;
  EGLImageKHR image;
  GstVideoGLTextureType type;
  GstVideoGLTextureOrientation orientation;

  gpointer user_data;
  GstEGLImageDestroyNotify user_data_destroy;
} GstEGLImageMemory;

#define GST_EGL_IMAGE_MEMORY(mem) ((GstEGLImageMemory*)(mem))

gboolean
gst_is_egl_image_memory (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, FALSE);
  g_return_val_if_fail (mem->allocator != NULL, FALSE);

  return g_strcmp0 (mem->allocator->mem_type, GST_EGL_IMAGE_MEMORY_TYPE) == 0;
}

EGLImageKHR
gst_egl_image_memory_get_image (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem), EGL_NO_IMAGE_KHR);

  if (mem->parent)
    mem = mem->parent;

  return GST_EGL_IMAGE_MEMORY (mem)->image;
}

EGLDisplay
gst_egl_image_memory_get_display (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem), NULL);

  if (mem->parent)
    mem = mem->parent;

  return GST_EGL_IMAGE_MEMORY (mem)->context->egl_display;
}

GstVideoGLTextureOrientation
gst_egl_image_memory_get_orientation (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem),
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL);

  if (mem->parent)
    mem = mem->parent;

  return GST_EGL_IMAGE_MEMORY (mem)->orientation;
}

void
gst_egl_image_memory_set_orientation (GstMemory * mem,
    GstVideoGLTextureOrientation orientation)
{
  g_return_if_fail (gst_is_egl_image_memory (mem));

  if (mem->parent)
    mem = mem->parent;

  GST_EGL_IMAGE_MEMORY (mem)->orientation = orientation;
}

static GstMemory *
gst_egl_image_allocator_alloc_vfunc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_warning
      ("Use gst_egl_image_allocator_alloc() to allocate from this allocator");

  return NULL;
}

static void
gst_egl_image_allocator_free_vfunc (GstAllocator * allocator, GstMemory * mem)
{
  GstEGLImageMemory *emem = (GstEGLImageMemory *) mem;

  g_return_if_fail (gst_is_egl_image_memory (mem));

  /* Shared memory should not destroy all the data */
  if (!mem->parent) {
    emem->context->eglDestroyImage (emem->context->egl_display, emem->image);

    if (emem->user_data_destroy)
      emem->user_data_destroy (emem->context, emem->user_data);

    gst_object_unref (emem->context);
    emem->context = NULL;
  }

  g_slice_free (GstEGLImageMemory, emem);
}

static gpointer
gst_egl_image_mem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  return NULL;
}

static void
gst_egl_image_mem_unmap (GstMemory * mem)
{
}

static GstMemory *
gst_egl_image_mem_share (GstMemory * mem, gssize offset, gssize size)
{
  return NULL;
}

static GstMemory *
gst_egl_image_mem_copy (GstMemory * mem, gssize offset, gssize size)
{
  return NULL;
}

static gboolean
gst_egl_image_mem_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  return FALSE;
}

typedef struct _GstEGLImageAllocator GstEGLImageAllocator;
typedef struct _GstEGLImageAllocatorClass GstEGLImageAllocatorClass;

struct _GstEGLImageAllocator
{
  GstAllocator parent;
};

struct _GstEGLImageAllocatorClass
{
  GstAllocatorClass parent_class;
};

GType gst_egl_image_allocator_get_type (void);
G_DEFINE_TYPE (GstEGLImageAllocator, gst_egl_image_allocator,
    GST_TYPE_ALLOCATOR);

#define GST_TYPE_EGL_IMAGE_ALLOCATOR   (gst_egl_image_mem_allocator_get_type())
#define GST_IS_EGL_IMAGE_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_EGL_IMAGE_ALLOCATOR))

static void
gst_egl_image_allocator_class_init (GstEGLImageAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = gst_egl_image_allocator_alloc_vfunc;
  allocator_class->free = gst_egl_image_allocator_free_vfunc;
}

static void
gst_egl_image_allocator_init (GstEGLImageAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_EGL_IMAGE_MEMORY_TYPE;
  alloc->mem_map = gst_egl_image_mem_map;
  alloc->mem_unmap = gst_egl_image_mem_unmap;
  alloc->mem_share = gst_egl_image_mem_share;
  alloc->mem_copy = gst_egl_image_mem_copy;
  alloc->mem_is_span = gst_egl_image_mem_is_span;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static gpointer
gst_egl_image_allocator_init_instance (gpointer data)
{
  GstAllocator *allocator =
      g_object_new (gst_egl_image_allocator_get_type (), NULL);;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_EGL_IMAGE_MEMORY, "eglimagememory", 0,
      "EGLImage Memory");

  gst_allocator_register (GST_EGL_IMAGE_MEMORY_TYPE,
      gst_object_ref (allocator));

  return allocator;
}

static GstEGLImageAllocator *
gst_egl_image_allocator_obtain (void)
{
  static GOnce once = G_ONCE_INIT;

  g_once (&once, gst_egl_image_allocator_init_instance, NULL);

  g_return_val_if_fail (once.retval != NULL, NULL);

  return (GstEGLImageAllocator *) (g_object_ref (once.retval));
}

void
gst_egl_image_memory_init (void)
{
  gst_egl_image_allocator_obtain ();
}

static void
gst_egl_image_memory_del_gl_texture (GstGLContext * context, gpointer tex)
{
  GLuint textures[1] = { GPOINTER_TO_UINT (tex) };

  gst_gl_context_del_texture (context, textures);
}

static GstMemory *
gst_egl_image_allocator_wrap (GstEGLImageAllocator * allocator,
    GstGLContextEGL * context, EGLImageKHR image, GstVideoGLTextureType type,
    GstMemoryFlags flags, gsize size, gpointer user_data,
    GstEGLImageDestroyNotify user_data_destroy)
{
  GstEGLImageMemory *mem = NULL;

  g_return_val_if_fail (context != NULL, NULL);
  g_return_val_if_fail (image != EGL_NO_IMAGE_KHR, NULL);

  if (!allocator) {
    allocator = gst_egl_image_allocator_obtain ();
  }

  mem = g_slice_new (GstEGLImageMemory);
  gst_memory_init (GST_MEMORY_CAST (mem), flags,
      GST_ALLOCATOR (allocator), NULL, size, 0, 0, size);

  gst_object_unref (allocator);

  mem->context = gst_object_ref (context);
  mem->image = image;
  mem->type = type;
  mem->orientation = GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL;

  mem->user_data = user_data;
  mem->user_data_destroy = user_data_destroy;

  return GST_MEMORY_CAST (mem);
}

#if 0
static GstMemory *
gst_egl_image_allocator_alloc (GstAllocator * allocator,
    GstGLContextEGL * context, GstVideoGLTextureType type, gint width,
    gint height, gsize size)
{
  /* EGL_NO_CONTEXT */
  return NULL;
}
#endif

static gboolean
gst_eglimage_to_gl_texture_upload_meta (GstVideoGLTextureUploadMeta *
    meta, guint texture_id[4])
{
  gint i = 0;
  gint n = 0;

  g_return_val_if_fail (meta != NULL, FALSE);
  g_return_val_if_fail (texture_id != NULL, FALSE);

  GST_DEBUG ("Uploading for meta with textures %i,%i,%i,%i", texture_id[0],
      texture_id[1], texture_id[2], texture_id[3]);

  n = gst_buffer_n_memory (meta->buffer);

  for (i = 0; i < n; i++) {
    GstMemory *mem = gst_buffer_peek_memory (meta->buffer, i);
    const GstGLFuncs *gl = NULL;

    if (!gst_is_egl_image_memory (mem)) {
      GST_WARNING ("memory %p does not hold an EGLImage", mem);
      return FALSE;
    }

    gl = GST_GL_CONTEXT (GST_EGL_IMAGE_MEMORY (mem)->context)->gl_vtable;

    if (i == 0)
      gl->ActiveTexture (GL_TEXTURE0);
    else if (i == 1)
      gl->ActiveTexture (GL_TEXTURE1);
    else if (i == 2)
      gl->ActiveTexture (GL_TEXTURE2);

    gl->BindTexture (GL_TEXTURE_2D, texture_id[i]);
    gl->EGLImageTargetTexture2D (GL_TEXTURE_2D,
        gst_egl_image_memory_get_image (mem));
  }

  if (GST_IS_GL_BUFFER_POOL (meta->buffer->pool))
    gst_gl_buffer_pool_replace_last_buffer (GST_GL_BUFFER_POOL (meta->
            buffer->pool), meta->buffer);

  return TRUE;
}

gboolean
gst_egl_image_memory_setup_buffer (GstGLContext * ctx, GstVideoInfo * info,
    GstBuffer * buffer)
{
  gint i = 0;
  gint stride[3];
  gsize offset[3];
  GstMemory *mem[3] = { NULL, NULL, NULL };
  guint n_mem = 0;
  GstMemoryFlags flags = 0;
  EGLImageKHR image = EGL_NO_IMAGE_KHR;
  EGLClientBuffer client_buffer_tex[3] = { 0, 0, 0 };
  GstVideoGLTextureType texture_types[] = { 0, 0, 0, 0 };
  GstEGLImageAllocator *allocator = gst_egl_image_allocator_obtain ();
  GstGLContextEGL *context = GST_GL_CONTEXT_EGL (ctx);

  g_return_val_if_fail (ctx, FALSE);
  g_return_val_if_fail (info, FALSE);
  g_return_val_if_fail (buffer, FALSE);
  g_return_val_if_fail (gst_gl_context_check_feature (ctx,
          "EGL_KHR_image_base"), FALSE);

  memset (stride, 0, sizeof (stride));
  memset (offset, 0, sizeof (offset));

  flags |= GST_MEMORY_FLAG_NOT_MAPPABLE;
  flags |= GST_MEMORY_FLAG_NO_SHARE;

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_AYUV:
    {
      gsize size = 0;

      switch (GST_VIDEO_INFO_FORMAT (info)) {
        case GST_VIDEO_FORMAT_RGB:
        case GST_VIDEO_FORMAT_BGR:
        case GST_VIDEO_FORMAT_RGB16:
        {
          texture_types[0] = GST_VIDEO_GL_TEXTURE_TYPE_RGB;
          break;
        }
        case GST_VIDEO_FORMAT_RGBA:
        case GST_VIDEO_FORMAT_BGRA:
        case GST_VIDEO_FORMAT_ARGB:
        case GST_VIDEO_FORMAT_ABGR:
        case GST_VIDEO_FORMAT_RGBx:
        case GST_VIDEO_FORMAT_BGRx:
        case GST_VIDEO_FORMAT_xRGB:
        case GST_VIDEO_FORMAT_xBGR:
        case GST_VIDEO_FORMAT_AYUV:
        {
          texture_types[0] = GST_VIDEO_GL_TEXTURE_TYPE_RGBA;
          break;
        }
        default:
          g_assert_not_reached ();
          break;
      }
#if 0
      mem[0] =
          gst_egl_image_allocator_alloc (allocator, context,
          texture_types[0], GST_VIDEO_INFO_WIDTH (info),
          GST_VIDEO_INFO_HEIGHT (info), size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else
#endif
      {
        gst_gl_generate_texture_full (GST_GL_CONTEXT (context), info, 0, stride,
            offset, &size, (GLuint *) & client_buffer_tex[0]);

        image = context->eglCreateImage (context->egl_display,
            context->egl_context, EGL_GL_TEXTURE_2D_KHR, client_buffer_tex[0],
            NULL);
        if (eglGetError () != EGL_SUCCESS)
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, context,
            image, texture_types[0], flags, size, client_buffer_tex[0],
            (GstEGLImageDestroyNotify) gst_egl_image_memory_del_gl_texture);
        n_mem = 1;
      }
      break;
    }

    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    {
      gsize size[2];

      texture_types[0] = GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE;
      texture_types[1] = GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA;
#if 0
      mem[0] =
          gst_egl_image_allocator_alloc (allocator, context,
          texture_types[0], GST_VIDEO_INFO_COMP_WIDTH (info,
              0), GST_VIDEO_INFO_COMP_HEIGHT (info, 0), size[0]);
      mem[1] =
          gst_egl_image_allocator_alloc (allocator, context,
          texture_types[1],
          GST_VIDEO_INFO_COMP_WIDTH (info, 1),
          GST_VIDEO_INFO_COMP_HEIGHT (info, 1), size[1]);

      if (mem[0] && mem[1]) {
        stride[0] = size[0] / GST_VIDEO_INFO_HEIGHT (info);
        offset[1] = size[0];
        stride[1] = size[1] / GST_VIDEO_INFO_HEIGHT (info);
        n_mem = 2;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[1], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        if (mem[0])
          gst_memory_unref (mem[0]);
        if (mem[1])
          gst_memory_unref (mem[1]);
        mem[0] = mem[1] = NULL;
      }
#endif
      {
        for (i = 0; i < 2; i++) {
          gst_gl_generate_texture_full (GST_GL_CONTEXT (context), info, 0,
              stride, offset, size, (GLuint *) & client_buffer_tex[i]);

          image = context->eglCreateImage (context->egl_display,
              context->egl_context, EGL_GL_TEXTURE_2D_KHR, client_buffer_tex[i],
              NULL);
          if (eglGetError () != EGL_SUCCESS)
            goto mem_error;

          mem[i] =
              gst_egl_image_allocator_wrap (allocator, context,
              image, texture_types[i], flags, size[i], client_buffer_tex[i],
              (GstEGLImageDestroyNotify) gst_egl_image_memory_del_gl_texture);
        }

        n_mem = 2;
      }
      break;
    }
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
    {
      gsize size[3];

      texture_types[0] = GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE;
      texture_types[1] = GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE;
      texture_types[2] = GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE;
#if 0
      mem[0] =
          gst_egl_image_allocator_alloc (allocator, context,
          texture_types[0], GST_VIDEO_INFO_COMP_WIDTH (info,
              0), GST_VIDEO_INFO_COMP_HEIGHT (info, 0), size[0]);
      mem[1] =
          gst_egl_image_allocator_alloc (allocator, context,
          texture_types[1], GST_VIDEO_INFO_COMP_WIDTH (info,
              1), GST_VIDEO_INFO_COMP_HEIGHT (info, 1), size[1]);
      mem[2] =
          gst_egl_image_allocator_alloc (allocator, context,
          texture_types[2], GST_VIDEO_INFO_COMP_WIDTH (info,
              2), GST_VIDEO_INFO_COMP_HEIGHT (info, 2), size[2]);

      if (mem[0] && mem[1] && mem[2]) {
        stride[0] = size[0] / GST_VIDEO_INFO_HEIGHT (info);
        offset[1] = size[0];
        stride[1] = size[1] / GST_VIDEO_INFO_HEIGHT (info);
        offset[2] = size[1];
        stride[2] = size[2] / GST_VIDEO_INFO_HEIGHT (info);
        n_mem = 3;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[1], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[2], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        if (mem[0])
          gst_memory_unref (mem[0]);
        if (mem[1])
          gst_memory_unref (mem[1]);
        if (mem[2])
          gst_memory_unref (mem[2]);
        mem[0] = mem[1] = mem[2] = NULL;
      }
#endif
      {
        for (i = 0; i < 3; i++) {
          gst_gl_generate_texture_full (GST_GL_CONTEXT (context), info, i,
              stride, offset, size, (GLuint *) & client_buffer_tex[i]);

          image = context->eglCreateImage (context->egl_display,
              context->egl_context, EGL_GL_TEXTURE_2D_KHR, client_buffer_tex[i],
              NULL);
          if (eglGetError () != EGL_SUCCESS)
            goto mem_error;

          mem[i] =
              gst_egl_image_allocator_wrap (allocator, context,
              image, texture_types[i], flags, size[i], client_buffer_tex[i],
              (GstEGLImageDestroyNotify) gst_egl_image_memory_del_gl_texture);
        }

        n_mem = 3;
      }
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  gst_buffer_add_video_meta_full (buffer, 0, GST_VIDEO_INFO_FORMAT (info),
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info),
      GST_VIDEO_INFO_N_PLANES (info), offset, stride);

  gst_buffer_add_video_gl_texture_upload_meta (buffer,
      gst_egl_image_memory_get_orientation (mem[0]), n_mem, texture_types,
      gst_eglimage_to_gl_texture_upload_meta, NULL, NULL, NULL);

  for (i = 0; i < n_mem; i++)
    gst_buffer_append_memory (buffer, mem[i]);

  return TRUE;

mem_error:
  {
    GST_CAT_ERROR (GST_CAT_DEFAULT, "Failed to create EGLImage");

    for (i = 0; i < 3; i++) {
      if (client_buffer_tex[i])
        gst_gl_context_del_texture (ctx, (GLuint *) & client_buffer_tex[i]);
      if (mem[i])
        gst_memory_unref (mem[i]);
    }

    return FALSE;
  }
}
