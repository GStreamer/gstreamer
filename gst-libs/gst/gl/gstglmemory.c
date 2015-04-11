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
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif

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

static inline GLenum
_sized_gl_format_from_gl_format_type (GstGLContext * context, GLenum format,
    GLenum type)
{
  gboolean ext_texture_rg =
      gst_gl_context_check_feature (context, "GL_EXT_texture_rg");

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
          if (ext_texture_rg)
            return GL_RG;
          return GL_RG8;
          break;
      }
      break;
    case GL_RED:
      switch (type) {
        case GL_UNSIGNED_BYTE:
          if (ext_texture_rg)
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

static void
_generate_texture (GstGLContext * context, GenTexture * data)
{
  const GstGLFuncs *gl = context->gl_vtable;
  GLenum internal_format;

  GST_TRACE ("Generating texture format:%u type:%u dimensions:%ux%u",
      data->gl_format, data->gl_type, data->width, data->height);

  internal_format =
      _sized_gl_format_from_gl_format_type (context, data->gl_format,
      data->gl_type);

  gl->GenTextures (1, &data->result);
  gl->BindTexture (data->gl_target, data->result);
  gl->TexImage2D (data->gl_target, 0, internal_format, data->width,
      data->height, 0, data->gl_format, data->gl_type, NULL);

  gl->TexParameteri (data->gl_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (data->gl_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (data->gl_target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (data->gl_target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  GST_LOG ("generated texture id:%d", data->result);
}

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
          gl_mem->plane)) - plane_start + gl_mem->mem.offset;
}

static void
_upload_memory (GstGLContext * context, GstGLMemory * gl_mem)
{
  const GstGLFuncs *gl;
  GLenum gl_format, gl_type, gl_target;
  gpointer data;
  gsize plane_start;

  if (!GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD)) {
    return;
  }

  gl = context->gl_vtable;

  gl_type = GL_UNSIGNED_BYTE;
  if (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    gl_type = GL_UNSIGNED_SHORT_5_6_5;

  gl_format = _gst_gl_format_from_gl_texture_type (gl_mem->tex_type);
  gl_target = gl_mem->tex_target;

  if (USING_OPENGL (context) || USING_GLES3 (context)
      || USING_OPENGL3 (context)) {
    gl->PixelStorei (GL_UNPACK_ROW_LENGTH, gl_mem->unpack_length);
  } else if (USING_GLES2 (context)) {
    gl->PixelStorei (GL_UNPACK_ALIGNMENT, gl_mem->unpack_length);
  }

  GST_LOG ("upload for texture id:%u, with pbo %u %ux%u",
      gl_mem->tex_id, gl_mem->transfer_pbo, gl_mem->tex_width,
      GL_MEM_HEIGHT (gl_mem));

  /* find the start of the plane data including padding */
  plane_start = _find_plane_frame_start (gl_mem);

  if (gl_mem->transfer_pbo && CONTEXT_SUPPORTS_PBO_UPLOAD (context)) {
    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, gl_mem->transfer_pbo);
    data = (void *) plane_start;
  } else {
    data = (gpointer) ((gintptr) plane_start + (gintptr) gl_mem->data);
  }

  gl->BindTexture (gl_target, gl_mem->tex_id);
  gl->TexSubImage2D (gl_target, 0, 0, 0, gl_mem->tex_width,
      GL_MEM_HEIGHT (gl_mem), gl_format, gl_type, data);

  if (gl_mem->transfer_pbo && CONTEXT_SUPPORTS_PBO_UPLOAD (context))
    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, 0);

  /* Reset to default values */
  if (USING_OPENGL (context) || USING_GLES3 (context)) {
    gl->PixelStorei (GL_UNPACK_ROW_LENGTH, 0);
  } else if (USING_GLES2 (context)) {
    gl->PixelStorei (GL_UNPACK_ALIGNMENT, 4);
  }

  gl->BindTexture (gl_target, 0);

  GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);
}

static inline void
_calculate_unpack_length (GstGLMemory * gl_mem)
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

  if (USING_OPENGL (gl_mem->context) || USING_GLES3 (gl_mem->context)
      || USING_OPENGL3 (gl_mem->context)) {
    gl_mem->unpack_length = GL_MEM_STRIDE (gl_mem) / n_gl_bytes;
  } else if (USING_GLES2 (gl_mem->context)) {
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

static void
_transfer_download (GstGLContext * context, GstGLMemory * gl_mem)
{
  const GstGLFuncs *gl;
  gsize plane_start;
  gsize size;
  guint format, type;
  guint fboId;

  if (!CONTEXT_SUPPORTS_PBO_DOWNLOAD (context)
      || gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
      || gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA)
    /* not supported */
    return;

  gl = context->gl_vtable;

  if (!gl_mem->transfer_pbo)
    gl->GenBuffers (1, &gl_mem->transfer_pbo);

  GST_DEBUG ("downloading texture %u using pbo %u",
      gl_mem->tex_id, gl_mem->transfer_pbo);

  size = gst_gl_get_plane_data_size (&gl_mem->info, &gl_mem->valign,
      gl_mem->plane);
  plane_start = _find_plane_frame_start (gl_mem);
  format = _gst_gl_format_from_gl_texture_type (gl_mem->tex_type);
  type = GL_UNSIGNED_BYTE;
  if (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    type = GL_UNSIGNED_SHORT_5_6_5;

  gl->BindBuffer (GL_PIXEL_PACK_BUFFER, gl_mem->transfer_pbo);
  gl->BufferData (GL_PIXEL_PACK_BUFFER, size, NULL, GL_STREAM_READ);

  /* FIXME: try and avoid creating and destroying fbo's every download... */
  /* create a framebuffer object */
  gl->GenFramebuffers (1, &fboId);
  gl->BindFramebuffer (GL_FRAMEBUFFER, fboId);

  gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D, gl_mem->tex_id, 0);

  if (!gst_gl_context_check_framebuffer_status (context)) {
    GST_ERROR ("failed to download texture");
    goto fbo_error;
  }

  gl->ReadPixels (0, 0, gl_mem->tex_width, GL_MEM_HEIGHT (gl_mem), format,
      type, (void *) plane_start);

fbo_error:
  gl->BindFramebuffer (GL_FRAMEBUFFER, 0);
  gl->DeleteFramebuffers (1, &fboId);

  gl->BindBuffer (GL_PIXEL_PACK_BUFFER, 0);
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

  GST_LOG ("downloading memory %p, tex %u into %p",
      gl_mem, gl_mem->tex_id, gl_mem->data);

  if (gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE
      || gl_mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA) {
    gl->BindTexture (gl_mem->tex_target, gl_mem->tex_id);
    gl->GetTexImage (gl_mem->tex_target, 0, format, type, gl_mem->data);
    gl->BindTexture (gl_mem->tex_target, 0);
  } else if (gl_mem->transfer_pbo && CONTEXT_SUPPORTS_PBO_DOWNLOAD (context)) {
    gsize size, plane_start;
    gpointer map_data = NULL;

    size = gst_gl_get_plane_data_size (&gl_mem->info, &gl_mem->valign,
        gl_mem->plane);

    gl->BindBuffer (GL_PIXEL_PACK_BUFFER, gl_mem->transfer_pbo);
    map_data =
        gl->MapBufferRange (GL_PIXEL_PACK_BUFFER, 0, size, GL_MAP_READ_BIT);
    if (!map_data) {
      GST_WARNING ("error mapping buffer for download");
      gl->BindBuffer (GL_PIXEL_PACK_BUFFER, 0);
      goto read_pixels;
    }

    /* FIXME: COPY! use glMapBuffer + glSync everywhere to remove this */
    plane_start = _find_plane_frame_start (gl_mem);
    memcpy ((guint8 *) gl_mem->data + plane_start, map_data, size);

    gl->UnmapBuffer (GL_PIXEL_PACK_BUFFER);
    gl->BindBuffer (GL_PIXEL_PACK_BUFFER, 0);
  } else {
  read_pixels:
    /* FIXME: try and avoid creating and destroying fbo's every download... */
    /* create a framebuffer object */
    gl->GenFramebuffers (1, &fboId);
    gl->BindFramebuffer (GL_FRAMEBUFFER, fboId);

    gl->FramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        gl_mem->tex_target, gl_mem->tex_id, 0);

    if (!gst_gl_context_check_framebuffer_status (context))
      goto fbo_error;

    gl->ReadPixels (0, 0, gl_mem->tex_width, GL_MEM_HEIGHT (gl_mem), format,
        type, gl_mem->data);

    gl->BindFramebuffer (GL_FRAMEBUFFER, 0);

  fbo_error:
    gl->DeleteFramebuffers (1, &fboId);
  }

error:
  return;
}

static void
_gl_mem_init (GstGLMemory * mem, GstAllocator * allocator, GstMemory * parent,
    GstGLContext * context, GstAllocationParams * params, GstVideoInfo * info,
    GstVideoAlignment * valign, guint plane, gpointer user_data,
    GDestroyNotify notify)
{
  gsize size, maxsize;
  gsize align = gst_memory_alignment, offset = 0;

  g_return_if_fail (plane < GST_VIDEO_INFO_N_PLANES (info));

  mem->info = *info;
  if (valign)
    mem->valign = *valign;
  else
    gst_video_alignment_reset (&mem->valign);

  size = maxsize = gst_gl_get_plane_data_size (info, valign, plane);

  if (params) {
    align |= params->align;
    offset = params->prefix;
    maxsize += params->prefix + params->padding + align;
  }

  gst_memory_init (GST_MEMORY_CAST (mem), 0, allocator, parent, maxsize, align,
      offset, size);

  mem->context = gst_object_ref (context);
  mem->tex_type =
      gst_gl_texture_type_from_format (context, GST_VIDEO_INFO_FORMAT (info),
      plane);
  /* we always operate on 2D textures unless we're dealing with wrapped textures */
  mem->tex_target = GL_TEXTURE_2D;
  mem->plane = plane;
  mem->notify = notify;
  mem->user_data = user_data;
  mem->texture_wrapped = FALSE;

  g_mutex_init (&mem->lock);

  _calculate_unpack_length (mem);

  GST_DEBUG ("new GL texture context:%" GST_PTR_FORMAT " memory:%p format:%u "
      "dimensions:%ux%u stride:%u size:%" G_GSIZE_FORMAT, context, mem,
      mem->tex_type, mem->tex_width, GL_MEM_HEIGHT (mem), GL_MEM_STRIDE (mem),
      maxsize);
}

static GstGLMemory *
_gl_mem_new (GstAllocator * allocator, GstMemory * parent,
    GstGLContext * context, GstAllocationParams * params, GstVideoInfo * info,
    GstVideoAlignment * valign, guint plane, gpointer user_data,
    GDestroyNotify notify)
{
  GstGLMemory *mem;
  GenTexture data = { 0, };
  mem = g_slice_new0 (GstGLMemory);
  _gl_mem_init (mem, allocator, parent, context, params, info, valign, plane,
      user_data, notify);

  data.width = mem->tex_width;
  data.height = GL_MEM_HEIGHT (mem);
  data.gl_format = _gst_gl_format_from_gl_texture_type (mem->tex_type);
  data.gl_type = GL_UNSIGNED_BYTE;
  data.gl_target = mem->tex_target;
  if (mem->tex_type == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
    data.gl_type = GL_UNSIGNED_SHORT_5_6_5;

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) _generate_texture, &data);
  if (!data.result) {
    GST_WARNING ("Could not create GL texture with context:%" GST_PTR_FORMAT,
        context);
  }

  GST_TRACE ("created texture %u", data.result);

  mem->tex_id = data.result;
  mem->tex_target = data.gl_target;

  return mem;
}

static GstGLMemory *
_gl_mem_alloc_data (GstGLMemory * mem)
{
  guint8 *data;
  gsize align, aoffset;

  data = g_try_malloc (mem->mem.maxsize);
  mem->alloc_data = mem->data = data;

  if (data == NULL) {
    gst_memory_unref ((GstMemory *) mem);
    return NULL;
  }

  /* do alignment */
  align = mem->mem.align;
  if ((aoffset = ((guintptr) data & align))) {
    aoffset = (align + 1) - aoffset;
    data += aoffset;
    mem->mem.maxsize -= aoffset;
    mem->data = data;
  }

  return mem;
}

static gpointer
_gl_mem_map (GstGLMemory * gl_mem, gsize maxsize, GstMapFlags flags)
{
  gpointer data;

  g_return_val_if_fail (maxsize == gl_mem->mem.maxsize, NULL);

  g_mutex_lock (&gl_mem->lock);

  if ((flags & GST_MAP_GL) == GST_MAP_GL) {
    if ((flags & GST_MAP_READ) == GST_MAP_READ) {
      GST_TRACE ("mapping GL texture:%u for reading", gl_mem->tex_id);
      if (GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD)) {
        gst_gl_context_thread_add (gl_mem->context,
            (GstGLContextThreadFunc) _upload_memory, gl_mem);
        GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);
      }
    }

    if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE) {
      GST_TRACE ("mapping GL texture:%u for writing", gl_mem->tex_id);
      GST_GL_MEMORY_FLAG_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);
      GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);
    }

    data = &gl_mem->tex_id;
  } else {                      /* not GL */
    if ((flags & GST_MAP_READ) == GST_MAP_READ) {
      GST_TRACE ("mapping GL texture:%u for reading from system memory",
          gl_mem->tex_id);
      if (GST_GL_MEMORY_FLAG_IS_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD)) {
        gst_gl_context_thread_add (gl_mem->context,
            (GstGLContextThreadFunc) _download_memory, gl_mem);
        GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);
      }
    }

    if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE) {
      GST_TRACE ("mapping GL texture:%u for writing to system memory",
          gl_mem->tex_id);
      GST_GL_MEMORY_FLAG_SET (gl_mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);
      GST_GL_MEMORY_FLAG_UNSET (gl_mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);
    }

    data = gl_mem->data;
  }

  /* only store the first map flags, subsequent maps must be a subset of this */
  if (gl_mem->map_count++ == 0)
    gl_mem->map_flags = flags;

  g_mutex_unlock (&gl_mem->lock);

  return data;
}

void
gst_gl_memory_download_transfer (GstGLMemory * gl_mem)
{
  g_mutex_lock (&gl_mem->lock);

  gst_gl_context_thread_add (gl_mem->context,
      (GstGLContextThreadFunc) _transfer_download, gl_mem);

  g_mutex_unlock (&gl_mem->lock);
}

static void
_gl_mem_unmap (GstGLMemory * gl_mem)
{
  g_mutex_lock (&gl_mem->lock);

  if (--gl_mem->map_count <= 0)
    gl_mem->map_flags = 0;

  g_mutex_unlock (&gl_mem->lock);
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
    GenTexture data = { 0, };
    data.width = copy_params->out_width;
    data.height = copy_params->out_height;
    data.gl_target = out_tex_target;
    data.gl_format = out_gl_format;
    data.gl_type = GL_UNSIGNED_BYTE;
    if (copy_params->out_format == GST_VIDEO_GL_TEXTURE_TYPE_RGB16)
      data.gl_type = GL_UNSIGNED_SHORT_5_6_5;

    _generate_texture (context, &data);
    tex_id = data.result;
  }

  if (!tex_id) {
    GST_WARNING ("Could not create GL texture with context:%p", src->context);
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
    if (!gl->GenBuffers) {
      gst_gl_context_set_error (context, "Cannot reinterpret texture contents "
          "without buffer objects");
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

    if (!src->pbo)
      gl->GenBuffers (1, &src->pbo);

    GST_TRACE ("copying texture data with size of %u*%u*%u",
        _gl_format_type_n_bytes (in_gl_format, in_gl_type), src->tex_width,
        GL_MEM_HEIGHT (src));

    /* copy tex */
    gl->BindBuffer (GL_PIXEL_PACK_BUFFER, src->pbo);
    gl->BufferData (GL_PIXEL_PACK_BUFFER, in_size, NULL, GL_STREAM_COPY);
    gl->ReadPixels (0, 0, src->tex_width, GL_MEM_HEIGHT (src), in_gl_format,
        in_gl_type, 0);
    gl->BindBuffer (GL_PIXEL_PACK_BUFFER, 0);

    gl->BindBuffer (GL_PIXEL_UNPACK_BUFFER, src->pbo);
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
  GstGLAllocator *allocator = (GstGLAllocator *) src->mem.allocator;
  GstMemory *ret = NULL;

  g_mutex_lock (&((GstGLMemory *) src)->lock);

  /* If not doing a full copy, then copy to sysmem, the 2D represention of the
   * texture would become wrong */
  if (offset > 0 || size < src->mem.size) {
    ret = allocator->fallback_mem_copy (&src->mem, offset, size);
  } else if (GST_GL_MEMORY_FLAG_IS_SET (src, GST_GL_MEMORY_FLAG_NEED_UPLOAD)) {
    GstAllocationParams params = { 0, src->mem.align, 0, 0 };
    GstGLMemory *dest;

    dest = _gl_mem_new (src->mem.allocator, NULL, src->context, &params,
        &src->info, &src->valign, src->plane, NULL, NULL);
    dest = _gl_mem_alloc_data (dest);

    if (dest == NULL) {
      GST_WARNING ("Could not copy GL Memory");
      gst_memory_unref ((GstMemory *) dest);
      goto done;
    }

    memcpy (dest->data, (guint8 *) src->data + src->mem.offset, src->mem.size);
    GST_GL_MEMORY_FLAG_SET (dest, GST_GL_MEMORY_FLAG_NEED_UPLOAD);
    ret = (GstMemory *) dest;
  } else {
    GstAllocationParams params = { 0, src->mem.align, 0, 0 };
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

    gst_gl_context_thread_add (src->context, _gl_mem_copy_thread, &copy_params);

    if (!copy_params.result) {
      GST_WARNING ("Could not copy GL Memory");
      goto done;
    }

    dest = g_slice_new0 (GstGLMemory);
    _gl_mem_init (dest, src->mem.allocator, NULL, src->context, &params,
        &src->info, &src->valign, src->plane, NULL, NULL);

    dest->tex_id = copy_params.tex_id;
    dest->tex_target = copy_params.tex_target;
    dest = _gl_mem_alloc_data (dest);
    GST_GL_MEMORY_FLAG_SET (dest, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);
    ret = (GstMemory *) dest;
  }

done:
  g_mutex_unlock (&((GstGLMemory *) src)->lock);

  return ret;
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
  if (gl_mem->transfer_pbo)
    gl->DeleteBuffers (1, &gl_mem->transfer_pbo);
}

static void
_gl_mem_free (GstAllocator * allocator, GstMemory * mem)
{
  GstGLMemory *gl_mem = (GstGLMemory *) mem;

  GST_TRACE ("freeing texture %u", gl_mem->tex_id);

  gst_gl_context_thread_add (gl_mem->context,
      (GstGLContextThreadFunc) _destroy_gl_objects, gl_mem);

  if (gl_mem->notify)
    gl_mem->notify (gl_mem->user_data);

  if (gl_mem->alloc_data) {
    g_free (gl_mem->alloc_data);
    gl_mem->alloc_data = NULL;
    gl_mem->data = NULL;
  }

  g_mutex_clear (&gl_mem->lock);

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
  copy_params.tex_target = gl_mem->tex_target;
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
  _gl_mem_init (mem, _gl_allocator, NULL, context, NULL, info, valign, plane,
      user_data, notify);

  mem->tex_id = texture_id;
  mem->tex_target = texture_target;
  mem->texture_wrapped = TRUE;

  mem = _gl_mem_alloc_data (mem);
  GST_GL_MEMORY_FLAG_SET (mem, GST_GL_MEMORY_FLAG_NEED_DOWNLOAD);

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
  mem = _gl_mem_alloc_data (mem);

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

  mem->data = data;

  GST_GL_MEMORY_FLAG_SET (mem, GST_GL_MEMORY_FLAG_NEED_UPLOAD);

  return mem;
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

  /* Keep the fallback copy function around, we will need it when copying with
   * at an offset or smaller size */
  allocator->fallback_mem_copy = alloc->mem_copy;

  alloc->mem_type = GST_GL_MEMORY_ALLOCATOR;
  alloc->mem_map = (GstMemoryMapFunction) _gl_mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) _gl_mem_unmap;
  alloc->mem_copy = (GstMemoryCopyFunction) _gl_mem_copy;
  alloc->mem_share = (GstMemoryShareFunction) _gl_mem_share;
  alloc->mem_is_span = (GstMemoryIsSpanFunction) _gl_mem_is_span;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
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
  guint n_mem, i;

  n_mem = GST_VIDEO_INFO_N_PLANES (info);

  for (i = 0; i < n_mem; i++) {
    gl_mem[i] =
        (GstGLMemory *) gst_gl_memory_alloc (context, params, info, i, valign);
    if (gl_mem[i] == NULL)
      return FALSE;

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
