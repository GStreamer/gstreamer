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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/video/video.h>

#include "gstglmemory.h"

/**
 * SECTION:gstglmemory
 * @short_description: memory subclass for GL textures
 * @see_also: #GstMemory, #GstAllocator, #GstGLBufferPool
 *
 * GstGLMemory is a #GstMemory subclass providing support for the mapping of
 * GL textures.  
 *
 * #GstGLMemory is created through gst_gl_memory_alloc() or system memory can
 * be wrapped through gst_gl_memory_wrapped().
 *
 * Data is uploaded or downloaded from the GPU as is necessary.
 */

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_MEMORY);
#define GST_CAT_DEFUALT GST_CAT_GL_MEMORY

static GstAllocator *_gl_allocator;

/* compatability definitions... */
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_STREAM_COPY
#define GL_STREAM_COPY 0x88E2
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

typedef struct
{
  /* in */
  GstGLMemory *src;
  GstVideoGLTextureType out_format;
  guint out_width, out_height;
  guint out_stride;
  gboolean respecify;
  /* inout */
  guint tex_id;
  /* out */
  gboolean result;
} GstGLMemoryCopyParams;

static inline guint
_gl_format_n_components (guint format)
{
  switch (format) {
    case GST_VIDEO_GL_TEXTURE_TYPE_RGBA:
    case GL_RGBA:
      return 4;
    case GST_VIDEO_GL_TEXTURE_TYPE_RGB:
    case GST_VIDEO_GL_TEXTURE_TYPE_RGB16:
    case GL_RGB:
      return 3;
    case GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA:
    case GST_VIDEO_GL_TEXTURE_TYPE_RG:
    case GL_LUMINANCE_ALPHA:
    case GL_RG:
      return 2;
    case GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE:
    case GST_VIDEO_GL_TEXTURE_TYPE_R:
    case GL_LUMINANCE:
    case GL_RED:
      return 1;
    default:
      return 0;
  }
}

static inline guint
_gl_type_n_components (guint type)
{
  switch (type) {
    case GL_UNSIGNED_BYTE:
      return 1;
    case GL_UNSIGNED_SHORT_5_6_5:
      return 3;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static inline guint
_gl_type_n_bytes (guint type)
{
  switch (type) {
    case GL_UNSIGNED_BYTE:
      return 1;
    case GL_UNSIGNED_SHORT_5_6_5:
      return 2;
    default:
      g_assert_not_reached ();
      return 0;
  }
}

static inline guint
_gl_format_type_n_bytes (guint format, guint type)
{
  return _gl_format_n_components (format) / _gl_type_n_components (type) *
      _gl_type_n_bytes (type);
}

static inline GLenum
_gst_gl_format_from_gl_texture_type (GstVideoGLTextureType tex_format)
{
  switch (tex_format) {
    case GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA:
      return GL_LUMINANCE_ALPHA;
    case GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE:
      return GL_LUMINANCE;
    case GST_VIDEO_GL_TEXTURE_TYPE_RGBA:
      return GL_RGBA;
    case GST_VIDEO_GL_TEXTURE_TYPE_RGB:
    case GST_VIDEO_GL_TEXTURE_TYPE_RGB16:
      return GL_RGB;
    case GST_VIDEO_GL_TEXTURE_TYPE_RG:
      return GL_RG;
    case GST_VIDEO_GL_TEXTURE_TYPE_R:
      return GL_RED;
    default:
      return GST_VIDEO_GL_TEXTURE_TYPE_RGBA;
  }
}

static inline guint
_gl_texture_type_n_bytes (GstVideoGLTextureType tex_format)
{
  guint format, type;

  format = _gst_gl_format_from_gl_texture_type (tex_format);
  type = GL_UNSIGNED_BYTE;
  if (tex_format == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    type = GL_UNSIGNED_SHORT_5_6_5;

  return _gl_format_type_n_bytes (format, type);
}

GstVideoGLTextureType
gst_gl_texture_type_from_format (GstGLContext * context,
    GstVideoFormat v_format, guint plane)
{
#if GST_GL_HAVE_PLATFORM_EAGL
  gboolean texture_rg = FALSE;
#else
  gboolean texture_rg =
      gst_gl_context_check_feature (context, "GL_EXT_texture_rg")
      || gst_gl_context_check_feature (context, "GL_ARB_texture_rg");
#endif
  guint n_plane_components;

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_AYUV:
      n_plane_components = 4;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      n_plane_components = 3;
      break;
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      n_plane_components = 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      n_plane_components = plane == 0 ? 1 : 2;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      n_plane_components = 1;
      break;
    default:
      n_plane_components = 4;
      g_assert_not_reached ();
      break;
  }

  switch (n_plane_components) {
    case 4:
      return GST_VIDEO_GL_TEXTURE_TYPE_RGBA;
      break;
    case 3:
      return GST_VIDEO_GL_TEXTURE_TYPE_RGB;
      break;
    case 2:
      return texture_rg ? GST_VIDEO_GL_TEXTURE_TYPE_RG :
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA;
      break;
    case 1:
      return texture_rg ? GST_VIDEO_GL_TEXTURE_TYPE_R :
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return GST_VIDEO_GL_TEXTURE_TYPE_RGBA;
}

static inline GLenum
_sized_gl_format_from_gl_format_type (GLenum format, GLenum type)
{
  switch (format) {
    case GL_RGBA:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          return GL_RGBA8;
          break;
      }
      break;
    case GL_RGB:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          return GL_RGB8;
          break;
        case GL_UNSIGNED_SHORT_5_6_5:
          return GL_RGB;
          break;
      }
      break;
    case GL_RG:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          return GL_RG8;
          break;
      }
      break;
    case GL_RED:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          return GL_R8;
          break;
      }
      break;
    case GL_LUMINANCE:
      return GL_LUMINANCE;
      break;
    case GL_LUMINANCE_ALPHA:
      return GL_LUMINANCE_ALPHA;
      break;
    case GL_ALPHA:
      return GL_ALPHA;
      break;
    default:
      break;
  }

  g_assert_not_reached ();
  return 0;
}

static inline guint
_get_plane_width (GstVideoInfo * info, guint plane)
{
  if (GST_VIDEO_INFO_IS_YUV (info))
    /* For now component width and plane width are the same and the
     * plane-component mapping matches
     */
    return GST_VIDEO_INFO_COMP_WIDTH (info, plane);
  else                          /* RGB, GRAY */
    return GST_VIDEO_INFO_WIDTH (info);
}

static inline guint
_get_plane_height (GstVideoInfo * info, guint plane)
{
  if (GST_VIDEO_INFO_IS_YUV (info))
    /* For now component width and plane width are the same and the
     * plane-component mapping matches
     */
    return GST_VIDEO_INFO_COMP_HEIGHT (info, plane);
  else                          /* RGB, GRAY */
    return GST_VIDEO_INFO_HEIGHT (info);
}

typedef struct _GenTexture
{
  guint width, height;
  GLenum gl_format;
  GLenum gl_type;
  guint result;
} GenTexture;

static void
_generate_texture (GstGLContext * context, GenTexture * data)
{
  const GstGLFuncs *gl = context->gl_vtable;
  GLenum internal_format;

  GST_CAT_TRACE (GST_CAT_GL_MEMORY,
      "Generating texture format:%u type:%u dimensions:%ux%u", data->gl_format,
      data->gl_type, data->width, data->height);

  internal_format =
      _sized_gl_format_from_gl_format_type (data->gl_format, data->gl_type);

  gl->GenTextures (1, &data->result);
  gl->BindTexture (GL_TEXTURE_2D, data->result);
  gl->TexImage2D (GL_TEXTURE_2D, 0, internal_format, data->width,
      data->height, 0, data->gl_format, data->gl_type, NULL);

  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  GST_CAT_LOG (GST_CAT_GL_MEMORY, "generated texture id:%d", data->result);
}

static void
_upload_memory (GstGLContext * context, GstGLMemory * gl_mem)
{
  const GstGLFuncs *gl;
  GLenum gl_format, gl_type;

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD)) {
    return;
  }

  gl = context->gl_vtable;

  gl_type = GL_UNSIGNED_BYTE;
  if (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    gl_type = GL_UNSIGNED_SHORT_5_6_5;

  gl_format = _gst_gl_format_from_gl_texture_type (gl_mem->tex_type);

  if (USING_OPENGL (context) || USING_GLES3 (context)) {
    gl->PixelStorei (GL_UNPACK_ROW_LENGTH, gl_mem->unpack_length);
  } else if (USING_GLES2 (context)) {
    gl->PixelStorei (GL_UNPACK_ALIGNMENT, gl_mem->unpack_length);
  }

  GST_CAT_LOG (GST_CAT_GL_MEMORY, "upload for texture id:%u, %ux%u",
      gl_mem->tex_id, gl_mem->width, gl_mem->height);

  gl->BindTexture (GL_TEXTURE_2D, gl_mem->tex_id);
  gl->TexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, gl_mem->width, gl_mem->height,
      gl_format, gl_type, gl_mem->data);

  /* Reset to default values */
  if (USING_OPENGL (context) || USING_GLES3 (context)) {
    gl->PixelStorei (GL_UNPACK_ROW_LENGTH, 0);
  } else if (USING_GLES2 (context)) {
    gl->PixelStorei (GL_UNPACK_ALIGNMENT, 4);
  }

  gl->BindTexture (GL_TEXTURE_2D, 0);

  GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);
}

static inline void
_calculate_unpack_length (GstGLMemory * gl_mem)
{
  guint n_gl_bytes;

  gl_mem->tex_scaling[0] = 1.0f;
  gl_mem->tex_scaling[1] = 1.0f;
  gl_mem->unpack_length = 1;

  n_gl_bytes = _gl_texture_type_n_bytes (gl_mem->tex_type);
  if (n_gl_bytes == 0) {
    GST_CAT_ERROR (GST_CAT_GL_MEMORY, "Unsupported texture type %d",
        gl_mem->tex_type);
    return;
  }

  if (USING_OPENGL (gl_mem->context) || USING_GLES3 (gl_mem->context)) {
    gl_mem->unpack_length = gl_mem->stride / n_gl_bytes;
  } else if (USING_GLES2 (gl_mem->context)) {
    guint j = 8;

    while (j >= n_gl_bytes) {
      /* GST_ROUND_UP_j(gl_mem->width * n_gl_bytes) */
      guint round_up_j = ((gl_mem->width * n_gl_bytes) + j - 1) & ~(j - 1);

      if (round_up_j == gl_mem->stride) {
        GST_CAT_LOG (GST_CAT_GL_MEMORY, "Found alignment of %u based on width "
            "(with plane width:%u, plane stride:%u and pixel stride:%u. "
            "RU%u(%u*%u) = %u)", j, gl_mem->width, gl_mem->stride, n_gl_bytes,
            j, gl_mem->width, n_gl_bytes, round_up_j);

        gl_mem->unpack_length = j;
        break;
      }
      j >>= 1;
    }

    if (j < n_gl_bytes) {
      /* Failed to find a suitable alignment, try based on plane_stride and
       * scale in the shader.  Useful for alignments that are greater than 8.
       */
      j = 8;

      while (j >= n_gl_bytes) {
        /* GST_ROUND_UP_j(gl_mem->stride) */
        guint round_up_j = ((gl_mem->stride) + j - 1) & ~(j - 1);

        if (round_up_j == gl_mem->stride) {
          GST_CAT_LOG (GST_CAT_GL_MEMORY, "Found alignment of %u based on "
              "stride (with plane stride:%u and pixel stride:%u. "
              "RU%u(%u) = %u)", j, gl_mem->stride, n_gl_bytes, j,
              gl_mem->stride, round_up_j);

          gl_mem->unpack_length = j;
          gl_mem->tex_scaling[0] =
              (gfloat) (gl_mem->width * n_gl_bytes) / (gfloat) gl_mem->stride;
          gl_mem->width = gl_mem->stride / n_gl_bytes;
          break;
        }
        j >>= 1;
      }

      if (j < n_gl_bytes) {
        GST_CAT_ERROR
            (GST_CAT_GL_MEMORY, "Failed to find matching alignment. Image may "
            "look corrupted. plane width:%u, plane stride:%u and pixel "
            "stride:%u", gl_mem->width, gl_mem->stride, n_gl_bytes);
      }
    }
  }
}

static void
_download_memory (GstGLContext * context, GstGLMemory * gl_mem)
{
  const GstGLFuncs *gl;
  guint format, type;
  GLuint fboId;

  gl = context->gl_vtable;
  format = _gst_gl_format_from_gl_texture_type (gl_mem->tex_type);
  type = GL_UNSIGNED_BYTE;
  if (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    type = GL_UNSIGNED_SHORT_5_6_5;

  if (!gl->GenFramebuffers) {
    gst_gl_context_set_error (context, "Cannot download GL texture "
        "without support for Framebuffers");
    goto error;
  }

  if (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES2
      && (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
          || gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA)) {
    gst_gl_context_set_error (context, "Cannot download GL luminance/"
        "luminance alpha textures");
    goto error;
  }

  GST_CAT_LOG (GST_CAT_GL_MEMORY, "downloading memory %p, tex %u into %p",
      gl_mem, gl_mem->tex_id, gl_mem->data);

  if (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
      || gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA) {
    gl->BindTexture (GL_TEXTURE_2D, gl_mem->tex_id);
    gl->GetTexImage (GL_TEXTURE_2D, 0, format, type, gl_mem->data);
    gl->BindTexture (GL_TEXTURE_2D, 0);
  } else {
    /* FIXME: try and avoid creating and destroying fbo's every download... */
    /* create a framebuffer object */
    gl->GenFramebuffers (1, &fboId);
    gl->BindFramebuffer (GL_FRAMEBUFFER, fboId);

    gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, gl_mem->tex_id, 0);

    if (!gst_gl_context_check_framebuffer_status (context))
      goto fbo_error;

    gl->ReadPixels (0, 0, gl_mem->width, gl_mem->height, format, type,
        gl_mem->data);

    gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  fbo_error:
    gl->DeleteFramebuffers (1, &fboId);
  }

error:
  return;
}

static void
_gl_mem_init (GstGLMemory * mem, GstAllocator * allocator, GstMemory * parent,
    GstGLContext * context, GstVideoGLTextureType tex_type, gint width,
    gint height, gint stride, gpointer user_data, GDestroyNotify notify)
{
  gsize maxsize;

  maxsize = stride * height;

  gst_memory_init (GST_MEMORY_CAST (mem), GST_MEMORY_FLAG_NO_SHARE,
      allocator, parent, maxsize, 0, 0, maxsize);

  mem->context = gst_object_ref (context);
  mem->tex_type = tex_type;
  mem->width = width;
  mem->height = height;
  mem->stride = stride;
  mem->notify = notify;
  mem->user_data = user_data;
  mem->data_wrapped = FALSE;
  mem->texture_wrapped = FALSE;

  _calculate_unpack_length (mem);

  GST_CAT_DEBUG (GST_CAT_GL_MEMORY, "new GL texture memory:%p format:%u "
      "dimensions:%ux%u", mem, tex_type, width, height);
}

static GstGLMemory *
_gl_mem_new (GstAllocator * allocator, GstMemory * parent,
    GstGLContext * context, GstVideoGLTextureType tex_type, gint width,
    gint height, gint stride, gpointer user_data, GDestroyNotify notify)
{
  GstGLMemory *mem;
  GenTexture data = { 0, };
  mem = g_slice_new0 (GstGLMemory);
  _gl_mem_init (mem, allocator, parent, context, tex_type, width, height,
      stride, user_data, notify);

  data.width = mem->width;
  data.height = mem->height;
  data.gl_format = _gst_gl_format_from_gl_texture_type (tex_type);
  data.gl_type = GL_UNSIGNED_BYTE;
  if (tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    data.gl_type = GL_UNSIGNED_SHORT_5_6_5;

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) _generate_texture, &data);
  if (!data.result) {
    GST_CAT_WARNING (GST_CAT_GL_MEMORY,
        "Could not create GL texture with context:%p", context);
  }

  GST_CAT_TRACE (GST_CAT_GL_MEMORY, "created texture %u", data.result);

  mem->tex_id = data.result;

  return mem;
}

static gpointer
_gl_mem_map (GstGLMemory * gl_mem, gsize maxsize, GstMapFlags flags)
{
  gpointer data;

  g_return_val_if_fail (maxsize == gl_mem->mem.maxsize, NULL);

  if ((flags & GST_MAP_GL) == GST_MAP_GL) {
    if ((flags & GST_MAP_READ) == GST_MAP_READ) {
      GST_CAT_TRACE (GST_CAT_GL_MEMORY, "mapping GL texture:%u for reading",
          gl_mem->tex_id);
      if (GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD)) {
        gst_gl_context_thread_add (gl_mem->context,
            (GstGLContextThreadFunc) _upload_memory, gl_mem);
        GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);
      }
    } else {
      GST_CAT_TRACE (GST_CAT_GL_MEMORY, "mapping GL texture:%u for writing",
          gl_mem->tex_id);
    }

    data = &gl_mem->tex_id;
  } else {                      /* not GL */
    if ((flags & GST_MAP_READ) == GST_MAP_READ) {
      GST_CAT_TRACE (GST_CAT_GL_MEMORY,
          "mapping GL texture:%u for reading from system memory",
          gl_mem->tex_id);
      if (GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD)) {
        gst_gl_context_thread_add (gl_mem->context,
            (GstGLContextThreadFunc) _download_memory, gl_mem);
        GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);
      }
    } else {
      GST_CAT_TRACE (GST_CAT_GL_MEMORY,
          "mapping GL texture:%u for writing to system memory", gl_mem->tex_id);
    }

    data = gl_mem->data;
  }

  gl_mem->map_flags = flags;

  return data;
}

static void
_gl_mem_unmap (GstGLMemory * gl_mem)
{
  if ((gl_mem->map_flags & GST_MAP_WRITE) == GST_MAP_WRITE) {
    if ((gl_mem->map_flags & GST_MAP_GL) == GST_MAP_GL) {
      GST_GL_MEMORY_FLAG_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);
    } else {
      GST_GL_MEMORY_FLAG_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);
    }
  }

  gl_mem->map_flags = 0;
}

static void
_gl_mem_copy_thread (GstGLContext * context, gpointer data)
{
  const GstGLFuncs *gl;
  GstGLMemoryCopyParams *copy_params;
  GstGLMemory *src;
  guint tex_id;
  GLuint fboId;
  gsize out_width, out_height, out_stride;
  GLuint out_gl_format, out_gl_type;
  GLuint in_gl_format, in_gl_type;
  gsize in_size, out_size;

  copy_params = (GstGLMemoryCopyParams *) data;
  src = copy_params->src;
  tex_id = copy_params->tex_id;
  out_width = copy_params->out_width;
  out_height = copy_params->out_height;
  out_stride = copy_params->out_stride;

  gl = src->context->gl_vtable;
  out_gl_format = _gst_gl_format_from_gl_texture_type (copy_params->out_format);
  out_gl_type = GL_UNSIGNED_BYTE;
  if (copy_params->out_format == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    out_gl_type = GL_UNSIGNED_SHORT_5_6_5;
  in_gl_format = _gst_gl_format_from_gl_texture_type (src->tex_type);
  in_gl_type = GL_UNSIGNED_BYTE;
  if (src->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    in_gl_type = GL_UNSIGNED_SHORT_5_6_5;

  if (!gl->GenFramebuffers) {
    gst_gl_context_set_error (src->context,
        "Context, EXT_framebuffer_object not supported");
    goto error;
  }

  in_size = src->height * src->stride;
  out_size = out_height * out_stride;

  if (copy_params->respecify) {
    if (in_size != out_size) {
      GST_ERROR ("Cannot copy between textures with backing data of different"
          "sizes. input %" G_GSIZE_FORMAT " output %" G_GSIZE_FORMAT, in_size,
          out_size);
      goto error;
    }
  }

  if (!tex_id) {
    GenTexture data = { 0, };
    data.width = copy_params->out_width;
    data.height = copy_params->out_height;
    data.gl_format = out_gl_format;
    data.gl_type = GL_UNSIGNED_BYTE;
    if (copy_params->out_format == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
      data.gl_type = GL_UNSIGNED_SHORT_5_6_5;

    _generate_texture (context, &data);
    tex_id = data.result;
  }

  if (!tex_id) {
    GST_CAT_WARNING (GST_CAT_GL_MEMORY,
        "Could not create GL texture with context:%p", src->context);
  }

  GST_CAT_LOG (GST_CAT_GL_MEMORY, "copying memory %p, tex %u into texture %i",
      src, src->tex_id, tex_id);

  /* FIXME: try and avoid creating and destroying fbo's every copy... */
  /* create a framebuffer object */
  gl->GenFramebuffers (1, &fboId);
  gl->BindFramebuffer (GL_FRAMEBUFFER, fboId);

  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D, src->tex_id, 0);

//  if (!gst_gl_context_check_framebuffer_status (src->context))
//    goto fbo_error;

  gl->BindTexture (GL_TEXTURE_2D, tex_id);
  if (copy_params->respecify) {
    if (!gl->GenBuffers) {
      gst_gl_context_set_error (context, "Cannot reinterpret texture contents "
          "without buffer objects");
      gl->BindTexture (GL_TEXTURE_2D, 0);
      goto fbo_error;
    }

    if (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES2
        && (in_gl_format != GL_RGBA || in_gl_type != GL_UNSIGNED_BYTE)) {
      gst_gl_context_set_error (context, "Cannot copy non RGBA/UNSIGNED_BYTE "
          "textures on GLES2");
      gl->BindTexture (GL_TEXTURE_2D, 0);
      goto fbo_error;
    }

    if (!src->pbo)
      gl->GenBuffers (1, &src->pbo);

    GST_TRACE ("copying texture data with size of %u*%u*%u",
        _gl_format_type_n_bytes (in_gl_format, in_gl_type), src->width,
        src->height);

    /* copy tex */
    gl->BindBuffer (GL_PIXEL_PACK_BUFFER, src->pbo);
    gl->BufferData (GL_PIXEL_PACK_BUFFER, in_size, NULL, GL_STREAM_COPY);
    gl->ReadPixels (0, 0, src->width, src->height, in_gl_format, in_gl_type, 0);
    gl->BindBuffer (GL_PIXEL_PACK_BUFFER, 0);

    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, src->pbo);
    gl->TexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, out_width, out_height,
        out_gl_format, out_gl_type, 0);

    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);
  } else {                      /* different sizes */
    gl->CopyTexImage2D (GL_TEXTURE_2D, 0, out_gl_format, 0, 0, out_width,
        out_height, 0);
  }

  gl->BindTexture (GL_TEXTURE_2D, 0);
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  gl->DeleteFramebuffers (1, &fboId);

  copy_params->tex_id = tex_id;
  copy_params->result = TRUE;

  return;

/* ERRORS */
fbo_error:
  {
    gl->DeleteFramebuffers (1, &fboId);

    copy_params->tex_id = 0;
    copy_params->result = FALSE;
    return;
  }

error:
  {
    copy_params->result = FALSE;
    return;
  }
}

static GstMemory *
_gl_mem_copy (GstGLMemory * src, gssize offset, gssize size)
{
  GstGLMemory *dest;

  if (GST_GL_MEMORY_FLAG_IS_SET (src, GST_GL_MEMORY_FLAG_NEED_UPLOAD)) {
    dest = _gl_mem_new (src->mem.allocator, NULL, src->context, src->tex_type,
        src->width, src->height, src->stride, NULL, NULL);
    dest->data = g_malloc (src->mem.maxsize);
    memcpy (dest->data, src->data, src->mem.maxsize);
    GST_GL_MEMORY_FLAG_SET (dest, GST_GL_MEMORY_FLAG_NEED_UPLOAD);
  } else {
    GstGLMemoryCopyParams copy_params;

    copy_params.src = src;
    copy_params.tex_id = 0;
    copy_params.out_format = src->tex_type;
    copy_params.out_width = src->width;
    copy_params.out_height = src->height;
    copy_params.out_stride = src->height;
    copy_params.respecify = FALSE;

    gst_gl_context_thread_add (src->context, _gl_mem_copy_thread, &copy_params);

    dest = g_slice_new0 (GstGLMemory);
    _gl_mem_init (dest, src->mem.allocator, NULL, src->context, src->tex_type,
        src->width, src->height, src->stride, NULL, NULL);

    if (!copy_params.result) {
      GST_CAT_WARNING (GST_CAT_GL_MEMORY, "Could not copy GL Memory");
      gst_memory_unref ((GstMemory *) dest);
      return NULL;
    }

    dest->tex_id = copy_params.tex_id;
    dest->data = g_malloc (src->mem.maxsize);
    if (dest->data == NULL) {
      GST_CAT_WARNING (GST_CAT_GL_MEMORY, "Could not copy GL Memory");
      gst_memory_unref ((GstMemory *) dest);
      return NULL;
    }
    GST_GL_MEMORY_FLAG_SET (dest, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);
  }

  return (GstMemory *) dest;
}

static GstMemory *
_gl_mem_share (GstGLMemory * mem, gssize offset, gssize size)
{
  return NULL;
}

static gboolean
_gl_mem_is_span (GstGLMemory * mem1, GstGLMemory * mem2, gsize * offset)
{
  return FALSE;
}

static GstMemory *
_gl_mem_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_warning ("use gst_gl_memory_alloc () to allocate from this "
      "GstGLMemory allocator");

  return NULL;
}

static void
_destroy_gl_objects (GstGLContext * context, GstGLMemory * gl_mem)
{
  const GstGLFuncs *gl = context->gl_vtable;

  if (gl_mem->tex_id && !gl_mem->texture_wrapped)
    gl->DeleteTextures (1, &gl_mem->tex_id);

  if (gl_mem->pbo)
    gl->DeleteBuffers (1, &gl_mem->pbo);
}

static void
_gl_mem_free (GstAllocator * allocator, GstMemory * mem)
{
  GstGLMemory *gl_mem = (GstGLMemory *) mem;

  gst_gl_context_thread_add (gl_mem->context,
      (GstGLContextThreadFunc) _destroy_gl_objects, gl_mem);

  if (gl_mem->notify)
    gl_mem->notify (gl_mem->user_data);

  if (gl_mem->data && !gl_mem->data_wrapped) {
    g_free (gl_mem->data);
    gl_mem->data = NULL;
  }

  gst_object_unref (gl_mem->context);
  g_slice_free (GstGLMemory, gl_mem);
}

/**
 * gst_gl_memory_copy_into_texture:
 * @gl_mem:a #GstGLMemory
 * @tex_id:OpenGL texture id
 * @tex_type: a #GstVideoGLTextureType
 * @width: width of @tex_id
 * @height: height of @tex_id
 * @stride: stride of the backing texture data
 * @respecify: whether to copy the data or copy per texel
 *
 * Copies @gl_mem into the texture specfified by @tex_id.  The format of @tex_id
 * is specified by @tex_type, @width and @height.
 *
 * If @respecify is %TRUE, then the copy is performed in terms of the texture
 * data.  This is useful for splitting RGBA textures into RG or R textures or
 * vice versa. The requirement for this to succeed is that the backing texture
 * data must be the same size, i.e. say a RGBA8 texture is converted into a RG8
 * texture, then the RG texture must have twice as many pixels available for
 * output as the RGBA texture.
 *
 * Otherwise, if @respecify is %FALSE, then the copy is performed per texel
 * using glCopyTexImage.  See the OpenGL specification for details on the
 * mappings between texture formats.
 *
 * Returns: Whether the copy suceeded
 */
gboolean
gst_gl_memory_copy_into_texture (GstGLMemory * gl_mem, guint tex_id,
    GstVideoGLTextureType tex_type, gint width, gint height, gint stride,
    gboolean respecify)
{
  GstGLMemoryCopyParams copy_params;

  copy_params.src = gl_mem;
  copy_params.tex_id = tex_id;
  copy_params.out_format = tex_type;
  copy_params.out_width = width;
  copy_params.out_height = height;
  copy_params.out_stride = stride;
  copy_params.respecify = respecify;

  gst_gl_context_thread_add (gl_mem->context, _gl_mem_copy_thread,
      &copy_params);

  return copy_params.result;
}

GstGLMemory *
gst_gl_memory_wrapped_texture (GstGLContext * context, guint texture_id,
    GstVideoGLTextureType tex_type, gint width, gint height, gpointer user_data,
    GDestroyNotify notify)
{
  GstGLMemory *mem;
  guint n_gl_bytes = _gl_texture_type_n_bytes (tex_type);

  mem = g_slice_new0 (GstGLMemory);
  _gl_mem_init (mem, _gl_allocator, NULL, context, tex_type, width, height,
      width * n_gl_bytes, NULL, NULL);

  mem->tex_id = texture_id;
  mem->texture_wrapped = TRUE;
  mem->data = g_malloc (mem->mem.maxsize);
  if (mem->data == NULL) {
    gst_memory_unref ((GstMemory *) mem);
    return NULL;
  }

  GST_GL_MEMORY_FLAG_SET (mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);

  return mem;
}

/**
 * gst_gl_memory_alloc:
 * @context:a #GstGLContext
 * @v_info: the #GstVideoInfo of the memory
 *
 * Returns: a #GstMemory object with a GL texture specified by @v_info
 *          from @context
 */
GstMemory *
gst_gl_memory_alloc (GstGLContext * context, GstVideoGLTextureType tex_type,
    gint width, gint height, gint stride)
{
  GstGLMemory *mem;

  mem = _gl_mem_new (_gl_allocator, NULL, context, tex_type, width, height,
      stride, NULL, NULL);

  mem->data = g_malloc (mem->mem.maxsize);
  if (mem->data == NULL) {
    gst_memory_unref ((GstMemory *) mem);
    return NULL;
  }

  return (GstMemory *) mem;
}

/**
 * gst_gl_memory_wrapped
 * @context:a #GstGLContext
 * @v_info: the #GstVideoInfo of the memory and data
 * @data: the data to wrap
 * @user_data: data called with for @notify
 * @notify: function called with @user_data when @data needs to be freed
 * 
 * Returns: a #GstGLMemory object with a GL texture specified by @v_info
 *          from @context and contents specified by @data
 */
GstGLMemory *
gst_gl_memory_wrapped (GstGLContext * context, GstVideoGLTextureType tex_type,
    gint width, gint height, gint stride, gpointer data, gpointer user_data,
    GDestroyNotify notify)
{
  GstGLMemory *mem;

  mem = _gl_mem_new (_gl_allocator, NULL, context, tex_type, width, height,
      stride, user_data, notify);

  mem->data = data;
  mem->data_wrapped = TRUE;

  GST_GL_MEMORY_FLAG_SET (mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);

  return mem;
}

G_DEFINE_TYPE (GstGLAllocator, gst_gl_allocator, GST_TYPE_ALLOCATOR);

static void
gst_gl_allocator_class_init (GstGLAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _gl_mem_alloc;
  allocator_class->free = _gl_mem_free;
}

static void
gst_gl_allocator_init (GstGLAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_GL_MEMORY_ALLOCATOR;
  alloc->mem_map = (GstMemoryMapFunction) _gl_mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) _gl_mem_unmap;
  alloc->mem_copy = (GstMemoryCopyFunction) _gl_mem_copy;
  alloc->mem_share = (GstMemoryShareFunction) _gl_mem_share;
  alloc->mem_is_span = (GstMemoryIsSpanFunction) _gl_mem_is_span;
}

/**
 * gst_gl_memory_init:
 *
 * Initializes the GL Memory allocator. It is safe to call this function
 * multiple times.  This must be called before any other GstGLMemory operation.
 */
void
gst_gl_memory_init (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_GL_MEMORY, "glmemory", 0, "OpenGL Memory");

    _gl_allocator = g_object_new (gst_gl_allocator_get_type (), NULL);

    gst_allocator_register (GST_GL_MEMORY_ALLOCATOR,
        gst_object_ref (_gl_allocator));
    g_once_init_leave (&_init, 1);
  }
}

/**
 * gst_is_gl_memory:
 * @mem:a #GstMemory
 * 
 * Returns: whether the memory at @mem is a #GstGLMemory
 */
gboolean
gst_is_gl_memory (GstMemory * mem)
{
  return mem != NULL && mem->allocator == _gl_allocator;
}

/**
 * gst_gl_memory_setup_buffer:
 * @context: a #GstGLContext
 * @info: a #GstVideoInfo
 * @buffer: a #GstBuffer
 *
 * Adds the required #GstGLMemory<!--  -->s with the correct configuration to
 * @buffer based on @info.
 *
 * Returns: whether the memory's were sucessfully added.
 */
gboolean
gst_gl_memory_setup_buffer (GstGLContext * context, GstVideoInfo * info,
    GstBuffer * buffer)
{
  GstGLMemory *gl_mem[GST_VIDEO_MAX_PLANES] = { NULL, };
  GstVideoGLTextureType tex_type;
  guint n_mem, i;

  n_mem = GST_VIDEO_INFO_N_PLANES (info);

  for (i = 0; i < n_mem; i++) {
    tex_type =
        gst_gl_texture_type_from_format (context, GST_VIDEO_INFO_FORMAT (info),
        i);
    gl_mem[i] =
        (GstGLMemory *) gst_gl_memory_alloc (context, tex_type,
        _get_plane_width (info, i), _get_plane_height (info, i),
        GST_VIDEO_INFO_PLANE_STRIDE (info, i));

    gst_buffer_append_memory (buffer, (GstMemory *) gl_mem[i]);
  }

  gst_buffer_add_video_meta_full (buffer, 0,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info), n_mem, info->offset, info->stride);

  return TRUE;
}

/**
 * gst_gl_memory_setup_wrapped:
 * @context: a #GstGLContext
 * @info: a #GstVideoInfo
 * @data: a list of per plane data pointers
 * @textures: (transfer out): a list of #GstGLMemory
 *
 * Wraps per plane data pointer in @data into the corresponding entry in
 * @textures based on @info.
 *
 * Returns: whether the memory's were sucessfully created.
 */
gboolean
gst_gl_memory_setup_wrapped (GstGLContext * context, GstVideoInfo * info,
    gpointer data[GST_VIDEO_MAX_PLANES],
    GstGLMemory * textures[GST_VIDEO_MAX_PLANES])
{
  GstVideoGLTextureType tex_type;
  gint i;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    tex_type =
        gst_gl_texture_type_from_format (context, GST_VIDEO_INFO_FORMAT (info),
        i);

    textures[i] = (GstGLMemory *) gst_gl_memory_wrapped (context, tex_type,
        _get_plane_width (info, i), _get_plane_height (info, i),
        GST_VIDEO_INFO_PLANE_STRIDE (info, i), data[i], NULL, NULL);
  }

  return TRUE;
}
