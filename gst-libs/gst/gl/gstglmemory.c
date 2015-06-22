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
#include "gstglutils.h"

/**
 * SECTION:gstglmemory
 * @short_description: memory subclass for GL textures
 * @see_also: #GstMemory, #GstAllocator, #GstGLBufferPool
 *
 * GstGLMemory is a #GstGLBaseBuffer subclass providing support for the mapping of
 * GL textures.  
 *
 * #GstGLMemory is created through gst_gl_memory_alloc() or system memory can
 * be wrapped through gst_gl_memory_wrapped().
 *
 * Data is uploaded or downloaded from the GPU as is necessary.
 */

/* Implementation notes
 *
 * PBO transfer's are implemented using GstGLBaseBuffer.  We just need to
 * ensure that the texture data is written/read to/from before/after calling
 * the parent class which performs the pbo buffer transfer.
 */

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

#define GL_MEM_WIDTH(gl_mem) _get_plane_width (&gl_mem->info, gl_mem->plane)
#define GL_MEM_HEIGHT(gl_mem) _get_plane_height (&gl_mem->info, gl_mem->plane)
#define GL_MEM_STRIDE(gl_mem) GST_VIDEO_INFO_PLANE_STRIDE (&gl_mem->info, gl_mem->plane)

#define CONTEXT_SUPPORTS_PBO_UPLOAD(context) \
    (gst_gl_context_check_gl_version (context, \
        GST_GL_API_OPENGL | GST_GL_API_OPENGL3, 2, 1) \
        || gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))
#define CONTEXT_SUPPORTS_PBO_DOWNLOAD(context) \
    (gst_gl_context_check_gl_version (context, \
        GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2, 3, 0))

GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_MEMORY);
#define GST_CAT_DEFAULT GST_CAT_GL_MEMORY

static GstAllocator *_gl_allocator;

/* compatability definitions... */
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
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
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_STREAM_COPY
#define GL_STREAM_COPY 0x88E2
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

G_DEFINE_TYPE (GstGLAllocator, gst_gl_allocator,
    GST_TYPE_GL_BASE_BUFFER_ALLOCATOR);

typedef struct
{
  /* in */
  GstGLMemory *src;
  GstVideoGLTextureType out_format;
  guint out_width, out_height;
  guint out_stride;
  gboolean respecify;
  guint tex_target;
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

static inline guint
_gl_texture_type_n_bytes (GstVideoGLTextureType tex_format)
{
  guint format, type;

  format = gst_gl_format_from_gl_texture_type (tex_format);
  type = GL_UNSIGNED_BYTE;
  if (tex_format == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    type = GL_UNSIGNED_SHORT_5_6_5;

  return _gl_format_type_n_bytes (format, type);
}

guint
gst_gl_format_from_gl_texture_type (GstVideoGLTextureType tex_format)
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

GstVideoGLTextureType
gst_gl_texture_type_from_format (GstGLContext * context,
    GstVideoFormat v_format, guint plane)
{
  gboolean texture_rg =
      gst_gl_context_check_feature (context, "GL_EXT_texture_rg")
      || gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0)
      || gst_gl_context_check_feature (context, "GL_ARB_texture_rg")
      || gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 0);
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
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      return GST_VIDEO_GL_TEXTURE_TYPE_RGB16;
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

guint
gst_gl_sized_gl_format_from_gl_format_type (GstGLContext * context,
    guint format, guint type)
{
  gboolean ext_texture_rg =
      gst_gl_context_check_feature (context, "GL_EXT_texture_rg");

  switch (format) {
    case GL_RGBA:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          return USING_GLES2 (context)
              && !USING_GLES3 (context) ? GL_RGBA : GL_RGBA8;
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
          if (!USING_GLES3 (context) && USING_GLES2 (context) && ext_texture_rg)
            return GL_RG;
          return GL_RG8;
          break;
      }
      break;
    case GL_RED:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          if (!USING_GLES3 (context) && USING_GLES2 (context) && ext_texture_rg)
            return GL_RED;
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
  GLenum gl_target;
  GLenum gl_format;
  GLenum gl_type;
  guint result;
} GenTexture;

/* find the difference between the start of the plane and where the video
 * data starts in the plane */
static gsize
_find_plane_frame_start (GstGLMemory * gl_mem)
{
  gsize plane_start;
  gint i;

  /* find the start of the plane data including padding */
  plane_start = 0;
  for (i = 0; i < gl_mem->plane; i++) {
    plane_start +=
        gst_gl_get_plane_data_size (&gl_mem->info, &gl_mem->valign, i);
  }

  /* offset between the plane data start and where the video frame starts */
  return (GST_VIDEO_INFO_PLANE_OFFSET (&gl_mem->info,
          gl_mem->plane)) - plane_start + gl_mem->mem.mem.offset;
}

static void
_upload_memory (GstGLMemory * gl_mem, GstMapInfo * info, gsize maxsize)
{
  GstGLContext *context = gl_mem->mem.context;
  const GstGLFuncs *gl;
  GLenum gl_format, gl_type, gl_target;
  gpointer data;
  gsize plane_start;

  if ((gl_mem->transfer_state & GST_GL_MEMORY_TRANSFER_NEED_UPLOAD) == 0)
    return;

  gl = context->gl_vtable;

  gl_type = GL_UNSIGNED_BYTE;
  if (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    gl_type = GL_UNSIGNED_SHORT_5_6_5;

  gl_format = gst_gl_format_from_gl_texture_type (gl_mem->tex_type);
  gl_target = gl_mem->tex_target;

  if (USING_OPENGL (context) || USING_GLES3 (context)
      || USING_OPENGL3 (context)) {
    gl->PixelStorei (GL_UNPACK_ROW_LENGTH, gl_mem->unpack_length);
  } else if (USING_GLES2 (context)) {
    gl->PixelStorei (GL_UNPACK_ALIGNMENT, gl_mem->unpack_length);
  }

  GST_LOG ("upload for texture id:%u, with pbo %u %ux%u",
      gl_mem->tex_id, gl_mem->mem.id, gl_mem->tex_width,
      GL_MEM_HEIGHT (gl_mem));

  /* find the start of the plane data including padding */
  plane_start = _find_plane_frame_start (gl_mem);

  if (gl_mem->mem.id && CONTEXT_SUPPORTS_PBO_UPLOAD (context)) {
    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, gl_mem->mem.id);
    data = (void *) plane_start;
  } else {
    data = (gpointer) ((gintptr) plane_start + (gintptr) gl_mem->mem.data);
  }

  gl->BindTexture (gl_target, gl_mem->tex_id);
  gl->TexSubImage2D (gl_target, 0, 0, 0, gl_mem->tex_width,
      GL_MEM_HEIGHT (gl_mem), gl_format, gl_type, data);

  if (gl_mem->mem.id && CONTEXT_SUPPORTS_PBO_UPLOAD (context))
    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);

  /* Reset to default values */
  if (USING_OPENGL (context) || USING_GLES3 (context)) {
    gl->PixelStorei (GL_UNPACK_ROW_LENGTH, 0);
  } else if (USING_GLES2 (context)) {
    gl->PixelStorei (GL_UNPACK_ALIGNMENT, 4);
  }

  gl->BindTexture (gl_target, 0);

  gl_mem->transfer_state &= ~GST_GL_MEMORY_TRANSFER_NEED_UPLOAD;
}

static inline void
_calculate_unpack_length (GstGLMemory * gl_mem, GstGLContext * context)
{
  guint n_gl_bytes;

  gl_mem->tex_scaling[0] = 1.0f;
  gl_mem->tex_scaling[1] = 1.0f;
  gl_mem->unpack_length = 1;
  gl_mem->tex_width = GL_MEM_WIDTH (gl_mem);

  n_gl_bytes = _gl_texture_type_n_bytes (gl_mem->tex_type);
  if (n_gl_bytes == 0) {
    GST_ERROR ("Unsupported texture type %d", gl_mem->tex_type);
    return;
  }

  if (USING_OPENGL (context) || USING_GLES3 (context)
      || USING_OPENGL3 (context)) {
    gl_mem->unpack_length = GL_MEM_STRIDE (gl_mem) / n_gl_bytes;
  } else if (USING_GLES2 (context)) {
    guint j = 8;

    while (j >= n_gl_bytes) {
      /* GST_ROUND_UP_j(GL_MEM_WIDTH (gl_mem) * n_gl_bytes) */
      guint round_up_j =
          ((GL_MEM_WIDTH (gl_mem) * n_gl_bytes) + j - 1) & ~(j - 1);

      if (round_up_j == GL_MEM_STRIDE (gl_mem)) {
        GST_LOG ("Found alignment of %u based on width "
            "(with plane width:%u, plane stride:%u and pixel stride:%u. "
            "RU%u(%u*%u) = %u)", j, GL_MEM_WIDTH (gl_mem),
            GL_MEM_STRIDE (gl_mem), n_gl_bytes, j, GL_MEM_WIDTH (gl_mem),
            n_gl_bytes, round_up_j);

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
        /* GST_ROUND_UP_j((GL_MEM_STRIDE (gl_mem)) */
        guint round_up_j = ((GL_MEM_STRIDE (gl_mem)) + j - 1) & ~(j - 1);

        if (round_up_j == (GL_MEM_STRIDE (gl_mem))) {
          GST_LOG ("Found alignment of %u based on "
              "stride (with plane stride:%u and pixel stride:%u. "
              "RU%u(%u) = %u)", j, GL_MEM_STRIDE (gl_mem), n_gl_bytes, j,
              GL_MEM_STRIDE (gl_mem), round_up_j);

          gl_mem->unpack_length = j;
          gl_mem->tex_scaling[0] =
              (gfloat) (GL_MEM_WIDTH (gl_mem) * n_gl_bytes) /
              (gfloat) GL_MEM_STRIDE (gl_mem);
          gl_mem->tex_width = GL_MEM_STRIDE (gl_mem) / n_gl_bytes;
          break;
        }
        j >>= 1;
      }

      if (j < n_gl_bytes) {
        GST_ERROR
            ("Failed to find matching alignment. Image may "
            "look corrupted. plane width:%u, plane stride:%u and pixel "
            "stride:%u", GL_MEM_WIDTH (gl_mem), GL_MEM_STRIDE (gl_mem),
            n_gl_bytes);
      }
    }
  }
}

static guint
_new_texture (GstGLContext * context, guint target, guint internal_format,
    guint format, guint type, guint width, guint height)
{
  const GstGLFuncs *gl = context->gl_vtable;
  guint tex_id;

  gl->GenTextures (1, &tex_id);
  gl->BindTexture (target, tex_id);
  gl->TexImage2D (target, 0, internal_format, width, height, 0, format, type,
      NULL);

  gl->TexParameteri (target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  return tex_id;
}

static gboolean
_gl_mem_create (GstGLMemory * gl_mem, GError ** error)
{
  GstGLContext *context = gl_mem->mem.context;
  const GstGLFuncs *gl = context->gl_vtable;
  GLenum internal_format;
  GLenum tex_format;
  GLenum tex_type;

  tex_format = gst_gl_format_from_gl_texture_type (gl_mem->tex_type);
  tex_type = GL_UNSIGNED_BYTE;
  if (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    tex_type = GL_UNSIGNED_SHORT_5_6_5;

  GST_TRACE ("Generating texture format:%u type:%u dimensions:%ux%u",
      tex_format, tex_type, gl_mem->tex_width, GL_MEM_HEIGHT (gl_mem));

  internal_format =
      gst_gl_sized_gl_format_from_gl_format_type (context, tex_format,
      tex_type);

  if (!gl_mem->tex_id) {
    gl_mem->tex_id =
        _new_texture (context, gl_mem->tex_target, internal_format, tex_format,
        tex_type, gl_mem->tex_width, GL_MEM_HEIGHT (gl_mem));
  }

  GST_LOG ("generated texture id:%d", gl_mem->tex_id);

  if (USING_OPENGL (context) || USING_OPENGL3 (context)
      || USING_GLES3 (context)) {
    /* FIXME: lazy init this for resource constrained platforms
     * Will need to fix pbo detection based on the existence of the mem.id then */
    gl->GenBuffers (1, &gl_mem->mem.id);
    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, gl_mem->mem.id);
    gl->BufferData (GL_PIXEL_UNPACK_BUFFER, gl_mem->mem.mem.maxsize, NULL,
        GL_STREAM_DRAW);
    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);
    GST_LOG ("generated pbo %u", gl_mem->mem.id);
  }

  return TRUE;
}

static void
_gl_mem_init (GstGLMemory * mem, GstAllocator * allocator, GstMemory * parent,
    GstGLContext * context, GstAllocationParams * params, GstVideoInfo * info,
    GstVideoAlignment * valign, guint plane, gpointer user_data,
    GDestroyNotify notify)
{
  gsize size;

  g_return_if_fail (plane < GST_VIDEO_INFO_N_PLANES (info));

  mem->info = *info;
  if (valign)
    mem->valign = *valign;
  else
    gst_video_alignment_reset (&mem->valign);

  size = gst_gl_get_plane_data_size (info, valign, plane);

  /* we always operate on 2D textures unless we're dealing with wrapped textures */
  mem->tex_target = GL_TEXTURE_2D;
  mem->tex_type =
      gst_gl_texture_type_from_format (context, GST_VIDEO_INFO_FORMAT (info),
      plane);
  mem->plane = plane;
  mem->notify = notify;
  mem->user_data = user_data;

  _calculate_unpack_length (mem, context);

  /* calls _gl_mem_create() */
  gst_gl_base_buffer_init ((GstGLBaseBuffer *) mem, allocator, parent, context,
      params, size);

  GST_DEBUG ("new GL texture context:%" GST_PTR_FORMAT " memory:%p format:%u "
      "dimensions:%ux%u stride:%u size:%" G_GSIZE_FORMAT, context, mem,
      mem->tex_type, mem->tex_width, GL_MEM_HEIGHT (mem), GL_MEM_STRIDE (mem),
      mem->mem.mem.size);
}

static GstGLMemory *
_gl_mem_new (GstAllocator * allocator, GstMemory * parent,
    GstGLContext * context, GstAllocationParams * params, GstVideoInfo * info,
    GstVideoAlignment * valign, guint plane, gpointer user_data,
    GDestroyNotify notify)
{
  GstGLMemory *mem;
  mem = g_slice_new0 (GstGLMemory);
  mem->texture_wrapped = FALSE;

  _gl_mem_init (mem, allocator, parent, context, params, info, valign, plane,
      user_data, notify);

  return mem;
}

static gboolean
_gl_mem_read_pixels (GstGLMemory * gl_mem, gpointer read_pointer)
{
  GstGLContext *context = gl_mem->mem.context;
  const GstGLFuncs *gl = context->gl_vtable;
  guint format, type;
  guint fbo;

  format = gst_gl_format_from_gl_texture_type (gl_mem->tex_type);
  type = GL_UNSIGNED_BYTE;
  if (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    type = GL_UNSIGNED_SHORT_5_6_5;

  /* FIXME: avoid creating a framebuffer every download/copy */
  gl->GenFramebuffers (1, &fbo);
  gl->BindFramebuffer (GL_FRAMEBUFFER, fbo);

  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      gl_mem->tex_target, gl_mem->tex_id, 0);

  if (!gst_gl_context_check_framebuffer_status (context)) {
    GST_CAT_WARNING (GST_CAT_GL_MEMORY,
        "Could not create framebuffer to read pixels for memory %p", gl_mem);
    gl->DeleteFramebuffers (1, &fbo);
    return FALSE;
  }

  gl->ReadPixels (0, 0, gl_mem->tex_width, GL_MEM_HEIGHT (gl_mem), format,
      type, read_pointer);

  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  gl->DeleteFramebuffers (1, &fbo);

  return TRUE;
}

static gboolean
_read_pixels_to_pbo (GstGLMemory * gl_mem)
{
  const GstGLFuncs *gl = gl_mem->mem.context->gl_vtable;

  if (!gl_mem->mem.id || !CONTEXT_SUPPORTS_PBO_DOWNLOAD (gl_mem->mem.context)
      || gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
      || gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA)
    /* unsupported */
    return FALSE;

  if (gl_mem->transfer_state & GST_GL_MEMORY_TRANSFER_NEED_DOWNLOAD) {
    /* copy texture data into into the pbo and map that */
    gsize plane_start = _find_plane_frame_start (gl_mem);

    gl->BindBuffer (GL_PIXEL_PACK_BUFFER, gl_mem->mem.id);

    if (!_gl_mem_read_pixels (gl_mem, (gpointer) plane_start)) {
      gl->BindBuffer (GL_PIXEL_PACK_BUFFER, 0);
      return FALSE;
    }

    gl->BindBuffer (GL_PIXEL_PACK_BUFFER, 0);
    gl_mem->transfer_state &= ~GST_GL_MEMORY_TRANSFER_NEED_DOWNLOAD;
  }

  return TRUE;
}

static gpointer
_pbo_download_transfer (GstGLMemory * gl_mem, GstMapInfo * info, gsize size)
{
  GstGLBaseBufferAllocatorClass *alloc_class;
  gpointer data;

  GST_DEBUG ("downloading texture %u using pbo %u", gl_mem->tex_id,
      gl_mem->mem.id);

  alloc_class =
      GST_GL_BASE_BUFFER_ALLOCATOR_CLASS (gst_gl_allocator_parent_class);

  /* texture -> pbo */
  if (info->flags & GST_MAP_READ)
    if (!_read_pixels_to_pbo (gl_mem))
      return NULL;

  /* get a cpu accessible mapping from the pbo */
  gl_mem->mem.target = GL_PIXEL_PACK_BUFFER;
  /* pbo -> data */
  data = alloc_class->map_buffer ((GstGLBaseBuffer *) gl_mem, info, size);

  return data;
}

static gpointer
_gl_mem_download_get_tex_image (GstGLMemory * gl_mem, GstMapInfo * info,
    gsize size)
{
  GstGLContext *context = gl_mem->mem.context;
  const GstGLFuncs *gl = context->gl_vtable;

  if (size != -1 && size != ((GstMemory *) gl_mem)->maxsize)
    return NULL;

  if (USING_GLES2 (context) || USING_GLES3 (context))
    return NULL;

  /* taken care of by read pixels */
  if (gl_mem->tex_type != GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
      && gl_mem->tex_type != GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA)
    return NULL;

  gst_gl_base_buffer_alloc_data ((GstGLBaseBuffer *) gl_mem);

  if (info->flags & GST_MAP_READ
      && gl_mem->transfer_state & GST_GL_MEMORY_TRANSFER_NEED_DOWNLOAD) {
    guint format, type;

    format = gst_gl_format_from_gl_texture_type (gl_mem->tex_type);
    type = GL_UNSIGNED_BYTE;
    if (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
      type = GL_UNSIGNED_SHORT_5_6_5;

    gl->BindTexture (gl_mem->tex_target, gl_mem->tex_id);
    gl->GetTexImage (gl_mem->tex_target, 0, format, type, gl_mem->mem.data);
    gl->BindTexture (gl_mem->tex_target, 0);
  }

  return gl_mem->mem.data;
}

static gpointer
_gl_mem_download_read_pixels (GstGLMemory * gl_mem, GstMapInfo * info,
    gsize size)
{
  if (size != -1 && size != ((GstMemory *) gl_mem)->maxsize)
    return NULL;

  gst_gl_base_buffer_alloc_data ((GstGLBaseBuffer *) gl_mem);

  if (info->flags & GST_MAP_READ
      && gl_mem->transfer_state & GST_GL_MEMORY_TRANSFER_NEED_DOWNLOAD) {
    if (!_gl_mem_read_pixels (gl_mem, gl_mem->mem.data))
      return NULL;
  }

  return gl_mem->mem.data;
}

static gpointer
_gl_mem_map_cpu_access (GstGLMemory * gl_mem, GstMapInfo * info, gsize size)
{
  gpointer data;

  data = _pbo_download_transfer (gl_mem, info, size);
  if (!data)
    data = _gl_mem_download_get_tex_image (gl_mem, info, size);

  if (!data)
    data = _gl_mem_download_read_pixels (gl_mem, info, size);

  return data;
}

static gpointer
_gl_mem_map_buffer (GstGLMemory * gl_mem, GstMapInfo * info, gsize maxsize)
{
  GstGLBaseBufferAllocatorClass *alloc_class;
  gpointer data;

  alloc_class =
      GST_GL_BASE_BUFFER_ALLOCATOR_CLASS (gst_gl_allocator_parent_class);

  if ((info->flags & GST_MAP_GL) == GST_MAP_GL) {
    if ((info->flags & GST_MAP_READ) == GST_MAP_READ) {
      GST_TRACE ("mapping GL texture:%u for reading", gl_mem->tex_id);

      if (gl_mem->mem.id && CONTEXT_SUPPORTS_PBO_UPLOAD (gl_mem->mem.context)) {
        gl_mem->mem.target = GL_PIXEL_UNPACK_BUFFER;
        /* data -> pbo */
        alloc_class->map_buffer ((GstGLBaseBuffer *) gl_mem, info, maxsize);
      }
      /* pbo -> texture */
      _upload_memory (gl_mem, info, maxsize);
    }

    if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE) {
      GST_TRACE ("mapping GL texture:%u for writing", gl_mem->tex_id);
      gl_mem->transfer_state |= GST_GL_MEMORY_TRANSFER_NEED_DOWNLOAD;
    }
    gl_mem->transfer_state &= ~GST_GL_MEMORY_TRANSFER_NEED_UPLOAD;

    data = &gl_mem->tex_id;
  } else {                      /* not GL */
    data = _gl_mem_map_cpu_access (gl_mem, info, maxsize);
    if (info->flags & GST_MAP_WRITE)
      gl_mem->transfer_state |= GST_GL_MEMORY_TRANSFER_NEED_UPLOAD;
    gl_mem->transfer_state &= ~GST_GL_MEMORY_TRANSFER_NEED_DOWNLOAD;
  }

  return data;
}

static void
_gl_mem_unmap_cpu_access (GstGLMemory * gl_mem, GstMapInfo * info)
{
  GstGLBaseBufferAllocatorClass *alloc_class;
  const GstGLFuncs *gl;

  alloc_class =
      GST_GL_BASE_BUFFER_ALLOCATOR_CLASS (gst_gl_allocator_parent_class);
  gl = gl_mem->mem.context->gl_vtable;

  if (!gl_mem->mem.id)
    /* PBO's not supported */
    return;

  gl_mem->mem.target = GL_PIXEL_PACK_BUFFER;
  alloc_class->unmap_buffer ((GstGLBaseBuffer *) gl_mem, info);
  gl->BindBuffer (GL_PIXEL_PACK_BUFFER, 0);
}

static void
_gl_mem_unmap_buffer (GstGLMemory * gl_mem, GstMapInfo * info)
{
  if ((info->flags & GST_MAP_GL) == 0) {
    _gl_mem_unmap_cpu_access (gl_mem, info);
    if (info->flags & GST_MAP_WRITE)
      gl_mem->transfer_state |= GST_GL_MEMORY_TRANSFER_NEED_UPLOAD;
  } else {
    if (info->flags & GST_MAP_WRITE)
      gl_mem->transfer_state |= GST_GL_MEMORY_TRANSFER_NEED_DOWNLOAD;
  }
}

static void
_gl_mem_copy_thread (GstGLContext * context, gpointer data)
{
  const GstGLFuncs *gl;
  GstGLMemoryCopyParams *copy_params;
  GstGLMemory *src;
  guint tex_id;
  GLuint out_tex_target;
  GLuint fboId;
  gsize out_width, out_height, out_stride;
  GLuint out_gl_format, out_gl_type;
  GLuint in_gl_format, in_gl_type;
  gsize in_size, out_size;

  copy_params = (GstGLMemoryCopyParams *) data;
  src = copy_params->src;
  tex_id = copy_params->tex_id;
  out_tex_target = copy_params->tex_target;
  out_width = copy_params->out_width;
  out_height = copy_params->out_height;
  out_stride = copy_params->out_stride;

  gl = context->gl_vtable;
  out_gl_format = gst_gl_format_from_gl_texture_type (copy_params->out_format);
  out_gl_type = GL_UNSIGNED_BYTE;
  if (copy_params->out_format == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    out_gl_type = GL_UNSIGNED_SHORT_5_6_5;
  in_gl_format = gst_gl_format_from_gl_texture_type (src->tex_type);
  in_gl_type = GL_UNSIGNED_BYTE;
  if (src->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    in_gl_type = GL_UNSIGNED_SHORT_5_6_5;

  if (!gl->GenFramebuffers) {
    gst_gl_context_set_error (context,
        "Context, EXT_framebuffer_object not supported");
    goto error;
  }

  in_size = GL_MEM_HEIGHT (src) * GL_MEM_STRIDE (src);
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
    guint internal_format;
    guint out_gl_type;

    out_gl_type = GL_UNSIGNED_BYTE;
    if (copy_params->out_format == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
      out_gl_type = GL_UNSIGNED_SHORT_5_6_5;

    internal_format =
        gst_gl_sized_gl_format_from_gl_format_type (context, out_gl_format,
        out_gl_type);

    tex_id =
        _new_texture (context, out_tex_target, internal_format, out_gl_format,
        out_gl_type, copy_params->out_width, copy_params->out_height);
  }

  if (!tex_id) {
    GST_WARNING ("Could not create GL texture with context:%p", context);
  }

  GST_LOG ("copying memory %p, tex %u into texture %i",
      src, src->tex_id, tex_id);

  /* FIXME: try and avoid creating and destroying fbo's every copy... */
  /* create a framebuffer object */
  gl->GenFramebuffers (1, &fboId);
  gl->BindFramebuffer (GL_FRAMEBUFFER, fboId);

  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      src->tex_target, src->tex_id, 0);

//  if (!gst_gl_context_check_framebuffer_status (src->context))
//    goto fbo_error;

  gl->BindTexture (out_tex_target, tex_id);
  if (copy_params->respecify) {
    if (!gl->GenBuffers || !src->mem.id) {
      gst_gl_context_set_error (context, "Cannot reinterpret texture contents "
          "without pixel buffer objects");
      gl->BindTexture (out_tex_target, 0);
      goto fbo_error;
    }

    if (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES2
        && (in_gl_format != GL_RGBA || in_gl_type != GL_UNSIGNED_BYTE)) {
      gst_gl_context_set_error (context, "Cannot copy non RGBA/UNSIGNED_BYTE "
          "textures on GLES2");
      gl->BindTexture (out_tex_target, 0);
      goto fbo_error;
    }

    GST_TRACE ("copying texture data with size of %u*%u*%u",
        _gl_format_type_n_bytes (in_gl_format, in_gl_type), src->tex_width,
        GL_MEM_HEIGHT (src));

    /* copy tex */
    gl->BindBuffer (GL_PIXEL_PACK_BUFFER, src->mem.id);
    gl->BufferData (GL_PIXEL_PACK_BUFFER, in_size, NULL, GL_STREAM_COPY);
    gl->ReadPixels (0, 0, src->tex_width, GL_MEM_HEIGHT (src), in_gl_format,
        in_gl_type, 0);
    gl->BindBuffer (GL_PIXEL_PACK_BUFFER, 0);

    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, src->mem.id);
    gl->TexSubImage2D (out_tex_target, 0, 0, 0, out_width, out_height,
        out_gl_format, out_gl_type, 0);

    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);
  } else {                      /* different sizes */
    gl->CopyTexImage2D (out_tex_target, 0, out_gl_format, 0, 0, out_width,
        out_height, 0);
  }

  gl->BindTexture (out_tex_target, 0);
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
  GstGLAllocator *allocator = (GstGLAllocator *) src->mem.mem.allocator;
  GstMemory *ret = NULL;

  /* If not doing a full copy, then copy to sysmem, the 2D represention of the
   * texture would become wrong */
  if (offset > 0 || size < src->mem.mem.size) {
    ret = allocator->fallback_mem_copy (&src->mem.mem, offset, size);
  } else if (GST_MEMORY_FLAG_IS_SET (src, GST_GL_BASE_BUFFER_FLAG_NEED_UPLOAD)) {
    GstAllocationParams params = { 0, src->mem.mem.align, 0, 0 };
    GstGLMemory *dest;

    dest = _gl_mem_new (src->mem.mem.allocator, NULL, src->mem.context, &params,
        &src->info, &src->valign, src->plane, NULL, NULL);
    dest = (GstGLMemory *) gst_gl_base_buffer_alloc_data ((GstGLBaseBuffer *)
        dest);

    if (dest == NULL) {
      GST_WARNING ("Could not copy GL Memory");
      goto done;
    }

    memcpy (dest->mem.data, (guint8 *) src->mem.data + src->mem.mem.offset,
        src->mem.mem.size);
    GST_MINI_OBJECT_FLAG_SET (dest, GST_GL_BASE_BUFFER_FLAG_NEED_UPLOAD);
    dest->transfer_state |= GST_GL_MEMORY_TRANSFER_NEED_UPLOAD;
    ret = (GstMemory *) dest;
  } else {
    GstAllocationParams params = { 0, src->mem.mem.align, 0, 0 };
    GstGLMemoryCopyParams copy_params;
    GstGLMemory *dest;

    copy_params.src = src;
    copy_params.tex_id = 0;
    copy_params.out_format = src->tex_type;
    copy_params.tex_target = src->tex_target;
    copy_params.out_width = src->tex_width;
    copy_params.out_height = GL_MEM_HEIGHT (src);
    copy_params.out_stride = GL_MEM_STRIDE (src);
    copy_params.respecify = FALSE;

    _gl_mem_copy_thread (src->mem.context, &copy_params);

    if (!copy_params.result) {
      GST_WARNING ("Could not copy GL Memory");
      goto done;
    }

    dest = g_slice_new0 (GstGLMemory);
    /* don't create our own texture */
    dest->texture_wrapped = TRUE;
    _gl_mem_init (dest, src->mem.mem.allocator, NULL, src->mem.context, &params,
        &src->info, &src->valign, src->plane, NULL, NULL);
    dest->texture_wrapped = FALSE;

    dest->tex_id = copy_params.tex_id;
    dest->tex_target = copy_params.tex_target;
    dest = (GstGLMemory *) gst_gl_base_buffer_alloc_data ((GstGLBaseBuffer *)
        dest);
    GST_MINI_OBJECT_FLAG_SET (dest, GST_GL_BASE_BUFFER_FLAG_NEED_DOWNLOAD);
    dest->transfer_state |= GST_GL_MEMORY_TRANSFER_NEED_DOWNLOAD;
    ret = (GstMemory *) dest;
  }

done:
  return ret;
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
_gl_mem_destroy (GstGLMemory * gl_mem)
{
  const GstGLFuncs *gl = gl_mem->mem.context->gl_vtable;

  if (gl_mem->tex_id && !gl_mem->texture_wrapped)
    gl->DeleteTextures (1, &gl_mem->tex_id);

  if (gl_mem->mem.id)
    gl->DeleteBuffers (1, &gl_mem->mem.id);
}

static void
_gl_mem_free (GstAllocator * allocator, GstMemory * mem)
{
  GstGLMemory *gl_mem = (GstGLMemory *) mem;

  GST_ALLOCATOR_CLASS (gst_gl_allocator_parent_class)->free (allocator, mem);

  if (gl_mem->notify)
    gl_mem->notify (gl_mem->user_data);

  g_slice_free (GstGLMemory, gl_mem);
}

static void
gst_gl_allocator_class_init (GstGLAllocatorClass * klass)
{
  GstGLBaseBufferAllocatorClass *gl_base;
  GstAllocatorClass *allocator_class;

  gl_base = (GstGLBaseBufferAllocatorClass *) klass;
  allocator_class = (GstAllocatorClass *) klass;

  gl_base->create = (GstGLBaseBufferAllocatorCreateFunction) _gl_mem_create;
  gl_base->map_buffer =
      (GstGLBaseBufferAllocatorMapBufferFunction) _gl_mem_map_buffer;
  gl_base->unmap_buffer =
      (GstGLBaseBufferAllocatorUnmapBufferFunction) _gl_mem_unmap_buffer;
  gl_base->copy = (GstGLBaseBufferAllocatorCopyFunction) _gl_mem_copy;
  gl_base->destroy = (GstGLBaseBufferAllocatorDestroyFunction) _gl_mem_destroy;

  allocator_class->alloc = _gl_mem_alloc;
  allocator_class->free = _gl_mem_free;
}

static void
gst_gl_allocator_init (GstGLAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  /* Keep the fallback copy function around, we will need it when copying with
   * at an offset or smaller size */
  allocator->fallback_mem_copy = alloc->mem_copy;

  alloc->mem_type = GST_GL_MEMORY_ALLOCATOR;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
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
  copy_params.tex_target = gl_mem->tex_target;
  copy_params.tex_id = tex_id;
  copy_params.out_format = tex_type;
  copy_params.out_width = width;
  copy_params.out_height = height;
  copy_params.out_stride = stride;
  copy_params.respecify = respecify;

  gst_gl_context_thread_add (gl_mem->mem.context, _gl_mem_copy_thread,
      &copy_params);

  return copy_params.result;
}

/**
 * gst_gl_memory_wrapped_texture:
 * @context: a #GstGLContext
 * @texture_id: the GL texture handle
 * @texture_target: the GL texture target
 * @info: the #GstVideoInfo of the memory
 * @plane: The plane this memory will represent
 * @user_data: user data
 * @notify: Destroy callback for the user data
 *
 * Wraps a texture handle into a #GstGLMemory.
 *
 * Returns: a newly allocated #GstGLMemory
 */
GstGLMemory *
gst_gl_memory_wrapped_texture (GstGLContext * context,
    guint texture_id, guint texture_target,
    GstVideoInfo * info, guint plane, GstVideoAlignment * valign,
    gpointer user_data, GDestroyNotify notify)
{
  GstGLMemory *mem;

  mem = g_slice_new0 (GstGLMemory);

  mem->tex_id = texture_id;

  _gl_mem_init (mem, _gl_allocator, NULL, context, NULL, info, valign, plane,
      user_data, notify);

  mem->tex_target = texture_target;
  mem->texture_wrapped = TRUE;

  mem = (GstGLMemory *) gst_gl_base_buffer_alloc_data ((GstGLBaseBuffer *) mem);
  GST_MINI_OBJECT_FLAG_SET (mem, GST_GL_BASE_BUFFER_FLAG_NEED_DOWNLOAD);
  mem->transfer_state |= GST_GL_MEMORY_TRANSFER_NEED_DOWNLOAD;

  return mem;
}

/**
 * gst_gl_memory_alloc:
 * @context:a #GstGLContext
 * @params: a #GstAllocationParams
 * @info: the #GstVideoInfo of the memory
 * @plane: the plane this memory will represent
 * @valign: the #GstVideoAlignment applied to @info
 *
 * Allocated a new #GstGlMemory.
 *
 * Returns: a #GstMemory object with a GL texture specified by @vinfo
 *          from @context
 */
GstMemory *
gst_gl_memory_alloc (GstGLContext * context, GstAllocationParams * params,
    GstVideoInfo * info, guint plane, GstVideoAlignment * valign)
{
  GstGLMemory *mem;

  mem = _gl_mem_new (_gl_allocator, NULL, context, params, info, valign, plane,
      NULL, NULL);
  mem = (GstGLMemory *) gst_gl_base_buffer_alloc_data ((GstGLBaseBuffer *) mem);

  return (GstMemory *) mem;
}

/**
 * gst_gl_memory_wrapped:
 * @context:a #GstGLContext
 * @info: the #GstVideoInfo of the memory and data
 * @plane: the plane this memory will represent
 * @valign: the #GstVideoAlignment applied to @info
 * @data: the data to wrap
 * @user_data: data called with for @notify
 * @notify: function called with @user_data when @data needs to be freed
 * 
 * Wrapped @data into a #GstGLMemory. This version will account for padding
 * added to the allocation and expressed through @valign.
 *
 * Returns: a #GstGLMemory object with a GL texture specified by @v_info
 *          from @context and contents specified by @data
 */
GstGLMemory *
gst_gl_memory_wrapped (GstGLContext * context, GstVideoInfo * info,
    guint plane, GstVideoAlignment * valign, gpointer data, gpointer user_data,
    GDestroyNotify notify)
{
  GstGLMemory *mem;

  mem = _gl_mem_new (_gl_allocator, NULL, context, NULL, info, valign, plane,
      user_data, notify);
  if (!mem)
    return NULL;

  mem->mem.data = data;

  GST_MINI_OBJECT_FLAG_SET (mem, GST_GL_BASE_BUFFER_FLAG_NEED_UPLOAD);
  mem->transfer_state |= GST_GL_MEMORY_TRANSFER_NEED_UPLOAD;

  return mem;
}

static void
_download_transfer (GstGLContext * context, GstGLMemory * gl_mem)
{
  GstGLBaseBuffer *mem = (GstGLBaseBuffer *) gl_mem;

  g_mutex_lock (&mem->lock);
  _read_pixels_to_pbo (gl_mem);
  g_mutex_unlock (&mem->lock);
}

void
gst_gl_memory_download_transfer (GstGLMemory * gl_mem)
{
  g_return_if_fail (gst_is_gl_memory ((GstMemory *) gl_mem));

  gst_gl_context_thread_add (gl_mem->mem.context,
      (GstGLContextThreadFunc) _download_transfer, gl_mem);
}

static void
_upload_transfer (GstGLContext * context, GstGLMemory * gl_mem)
{
  GstGLBaseBufferAllocatorClass *alloc_class;
  GstGLBaseBuffer *mem = (GstGLBaseBuffer *) gl_mem;
  GstMapInfo info;

  alloc_class =
      GST_GL_BASE_BUFFER_ALLOCATOR_CLASS (gst_gl_allocator_parent_class);

  info.flags = GST_MAP_READ | GST_MAP_GL;
  info.memory = (GstMemory *) mem;
  /* from gst_memory_map() */
  info.size = mem->mem.size;
  info.maxsize = mem->mem.maxsize - mem->mem.offset;

  g_mutex_lock (&mem->lock);
  mem->target = GL_PIXEL_UNPACK_BUFFER;
  alloc_class->map_buffer (mem, &info, mem->mem.maxsize);
  alloc_class->unmap_buffer (mem, &info);
  g_mutex_unlock (&mem->lock);
}

void
gst_gl_memory_upload_transfer (GstGLMemory * gl_mem)
{
  g_return_if_fail (gst_is_gl_memory ((GstMemory *) gl_mem));

  if (CONTEXT_SUPPORTS_PBO_UPLOAD (gl_mem->mem.context))
    gst_gl_context_thread_add (gl_mem->mem.context,
        (GstGLContextThreadFunc) _upload_transfer, gl_mem);
}

gint
gst_gl_memory_get_texture_width (GstGLMemory * gl_mem)
{
  g_return_val_if_fail (gst_is_gl_memory ((GstMemory *) gl_mem), 0);

  return gl_mem->tex_width;
}

gint
gst_gl_memory_get_texture_height (GstGLMemory * gl_mem)
{
  g_return_val_if_fail (gst_is_gl_memory ((GstMemory *) gl_mem), 0);

  return _get_plane_height (&gl_mem->info, gl_mem->plane);
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
  return mem != NULL && mem->allocator != NULL
      && g_type_is_a (G_OBJECT_TYPE (mem->allocator), GST_TYPE_GL_ALLOCATOR);
}

/**
 * gst_gl_memory_setup_buffer:
 * @context: a #GstGLContext
 * @params: a #GstAllocationParams
 * @info: a #GstVideoInfo
 * @valign: the #GstVideoAlignment applied to @info
 * @buffer: a #GstBuffer
 *
 * Adds the required #GstGLMemory<!--  -->s with the correct configuration to
 * @buffer based on @info. This version handles padding through @valign.
 *
 * Returns: whether the memory's were sucessfully added.
 */
gboolean
gst_gl_memory_setup_buffer (GstGLContext * context,
    GstAllocationParams * params, GstVideoInfo * info,
    GstVideoAlignment * valign, GstBuffer * buffer)
{
  GstGLMemory *gl_mem[GST_VIDEO_MAX_PLANES] = { NULL, };
  guint n_mem, i, v, views;

  n_mem = GST_VIDEO_INFO_N_PLANES (info);

  if (GST_VIDEO_INFO_MULTIVIEW_MODE (info) ==
      GST_VIDEO_MULTIVIEW_MODE_SEPARATED)
    views = info->views;
  else
    views = 1;

  for (v = 0; v < views; v++) {
    for (i = 0; i < n_mem; i++) {
      gl_mem[i] =
          (GstGLMemory *) gst_gl_memory_alloc (context, params, info, i,
          valign);
      if (gl_mem[i] == NULL)
        return FALSE;

      gst_buffer_append_memory (buffer, (GstMemory *) gl_mem[i]);
    }

    gst_buffer_add_video_meta_full (buffer, v,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), n_mem, info->offset, info->stride);
  }

  return TRUE;
}

/**
 * gst_gl_memory_setup_wrapped:
 * @context: a #GstGLContext
 * @info: a #GstVideoInfo
 * @valign: a #GstVideoInfo
 * @data: a list of per plane data pointers
 * @textures: (transfer out): a list of #GstGLMemory
 *
 * Wraps per plane data pointer in @data into the corresponding entry in
 * @textures based on @info and padding from @valign.
 *
 * Returns: whether the memory's were sucessfully created.
 */
gboolean
gst_gl_memory_setup_wrapped (GstGLContext * context, GstVideoInfo * info,
    GstVideoAlignment * valign, gpointer data[GST_VIDEO_MAX_PLANES],
    GstGLMemory * textures[GST_VIDEO_MAX_PLANES])
{
  gint i;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    textures[i] = (GstGLMemory *) gst_gl_memory_wrapped (context, info, i,
        valign, data[i], NULL, NULL);
  }

  return TRUE;
}
