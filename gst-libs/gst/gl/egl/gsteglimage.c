/*
 * GStreamer
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2014 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
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
 * SECTION:gsteglimage
 * @short_description: EGLImage abstraction
 * @title: GstEGLImage
 * @see_also: #GstGLMemoryEGL, #GstGLContext
 *
 * #GstEGLImage represents and holds an #EGLImage handle.
 *
 * A #GstEGLImage can be created from a dmabuf with gst_egl_image_from_dmabuf()
 * or #GstGLMemoryEGL provides a #GstAllocator to allocate #EGLImage's bound to
 * and OpenGL texture.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsteglimage.h"
#include <gst/gl/egl/gstgldisplay_egl.h>
#include <string.h>

#if GST_GL_HAVE_DMABUF
#include <gst/allocators/gstdmabuf.h>
#include <libdrm/drm_fourcc.h>

#ifndef DRM_FORMAT_R8
#define DRM_FORMAT_R8 fourcc_code('R', '8', ' ', ' ')
#endif

#ifndef DRM_FORMAT_RG88
#define DRM_FORMAT_RG88 fourcc_code('R', 'G', '8', '8')
#endif

#ifndef DRM_FORMAT_GR88
#define DRM_FORMAT_GR88 fourcc_code('G', 'R', '8', '8')
#endif
#endif

#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#endif

#ifndef EGL_LINUX_DRM_FOURCC_EXT
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#endif

#ifndef EGL_DMA_BUF_PLANE0_FD_EXT
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#endif

#ifndef EGL_DMA_BUF_PLANE0_OFFSET_EXT
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#endif

#ifndef EGL_DMA_BUF_PLANE0_PITCH_EXT
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#endif

GST_DEFINE_MINI_OBJECT_TYPE (GstEGLImage, gst_egl_image);

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_egl_image_ensure_debug_category()

static GstDebugCategory *
gst_egl_image_ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    GstDebugCategory *cat = NULL;

    GST_DEBUG_CATEGORY_INIT (cat, "gleglimage", 0, "EGLImage wrapper");

    g_once_init_leave (&cat_gonce, (gsize) cat);
  }

  return (GstDebugCategory *) cat_gonce;
}
#endif /* GST_DISABLE_GST_DEBUG */

/**
 * gst_egl_image_get_image:
 * @image: a #GstEGLImage
 *
 * Returns: the #EGLImageKHR of @image
 */
EGLImageKHR
gst_egl_image_get_image (GstEGLImage * image)
{
  g_return_val_if_fail (GST_IS_EGL_IMAGE (image), EGL_NO_IMAGE_KHR);

  return image->image;
}

static void
_gst_egl_image_free_thread (GstGLContext * context, GstEGLImage * image)
{
  if (image->destroy_notify)
    image->destroy_notify (image, image->destroy_data);
}

static void
_gst_egl_image_free (GstMiniObject * object)
{
  GstEGLImage *image = GST_EGL_IMAGE (object);

  if (image->context) {
    gst_gl_context_thread_add (GST_GL_CONTEXT (image->context),
        (GstGLContextThreadFunc) _gst_egl_image_free_thread, image);
    gst_object_unref (image->context);
  }
}

static GstMiniObject *
_gst_egl_image_copy (GstMiniObject * obj)
{
  return gst_mini_object_ref (obj);
}

/**
 * gst_egl_image_new_wrapped:
 * @context: a #GstGLContext (must be an EGL context)
 * @image: the image to wrap
 * @format: the #GstGLFormat
 * @user_data: user data
 * @user_data_destroy: called when @user_data is no longer needed
 *
 * Returns: a new #GstEGLImage wrapping @image
 */
GstEGLImage *
gst_egl_image_new_wrapped (GstGLContext * context, EGLImageKHR image,
    GstGLFormat format, gpointer user_data,
    GstEGLImageDestroyNotify user_data_destroy)
{
  GstEGLImage *img = NULL;

  g_return_val_if_fail (context != NULL, NULL);
  g_return_val_if_fail ((gst_gl_context_get_gl_platform (context) &
          GST_GL_PLATFORM_EGL) != 0, NULL);
  g_return_val_if_fail (image != EGL_NO_IMAGE_KHR, NULL);

  img = g_new0 (GstEGLImage, 1);
  gst_mini_object_init (GST_MINI_OBJECT_CAST (img), 0, GST_TYPE_EGL_IMAGE,
      (GstMiniObjectCopyFunction) _gst_egl_image_copy, NULL,
      (GstMiniObjectFreeFunction) _gst_egl_image_free);

  img->context = gst_object_ref (context);
  img->image = image;
  img->format = format;

  img->destroy_data = user_data;
  img->destroy_notify = user_data_destroy;

  return img;
}

static EGLImageKHR
_gst_egl_image_create (GstGLContext * context, guint target,
    EGLClientBuffer buffer, guintptr * attribs)
{
  EGLDisplay egl_display = EGL_DEFAULT_DISPLAY;
  EGLContext egl_context = EGL_NO_CONTEXT;
  EGLImageKHR img = EGL_NO_IMAGE_KHR;
  GstGLDisplayEGL *display_egl;
  gint plat_major, plat_minor;
  guint attrib_len = 0;

  gst_gl_context_get_gl_platform_version (context, &plat_major, &plat_minor);

  display_egl = gst_gl_display_egl_from_gl_display (context->display);
  if (!display_egl) {
    GST_WARNING_OBJECT (context, "Failed to retrieve GstGLDisplayEGL from %"
        GST_PTR_FORMAT, context->display);
    return EGL_NO_IMAGE_KHR;
  }
  egl_display =
      (EGLDisplay) gst_gl_display_get_handle (GST_GL_DISPLAY (display_egl));
  gst_object_unref (display_egl);

  if (target != EGL_LINUX_DMA_BUF_EXT)
    egl_context = (EGLContext) gst_gl_context_get_gl_context (context);

  if (attribs)
    while (attribs[attrib_len++] != EGL_NONE) {
    }
#ifdef EGL_VERSION_1_5
  if (GST_GL_CHECK_GL_VERSION (plat_major, plat_minor, 1, 5)) {
    EGLImageKHR (*gst_eglCreateImage) (EGLDisplay dpy, EGLContext ctx,
        EGLenum target, EGLClientBuffer buffer, const EGLAttrib * attrib_list);
    EGLAttrib *egl_attribs = NULL;
    guint i;

    gst_eglCreateImage = gst_gl_context_get_proc_address (context,
        "eglCreateImage");
    if (!gst_eglCreateImage) {
      GST_ERROR_OBJECT (context, "\"eglCreateImage\" not exposed by the "
          "implementation as required by EGL >= 1.5");
      return EGL_NO_IMAGE_KHR;
    }

    if (attribs) {
      egl_attribs = g_new0 (EGLAttrib, attrib_len);
      for (i = 0; i < attrib_len; i++)
        egl_attribs[i] = (EGLAttrib) attribs[i];
    }

    img = gst_eglCreateImage (egl_display, egl_context, target, buffer,
        egl_attribs);

    g_free (egl_attribs);
  } else
#endif
  {
    EGLImageKHR (*gst_eglCreateImageKHR) (EGLDisplay dpy, EGLContext ctx,
        EGLenum target, EGLClientBuffer buffer, const EGLint * attrib_list);
    EGLint *egl_attribs = NULL;
    gint i;

    gst_eglCreateImageKHR = gst_gl_context_get_proc_address (context,
        "eglCreateImageKHR");
    if (!gst_eglCreateImageKHR) {
      GST_WARNING_OBJECT (context, "\"eglCreateImageKHR\" not exposed by the "
          "implementation");
      return EGL_NO_IMAGE_KHR;
    }

    if (attribs) {
      egl_attribs = g_new0 (EGLint, attrib_len);
      for (i = 0; i < attrib_len; i++)
        egl_attribs[i] = (EGLint) attribs[i];
    }

    img = gst_eglCreateImageKHR (egl_display, egl_context, target, buffer,
        egl_attribs);

    g_free (egl_attribs);
  }

  return img;
}

static void
_gst_egl_image_destroy (GstGLContext * context, EGLImageKHR image)
{
  EGLBoolean (*gst_eglDestroyImage) (EGLDisplay dpy, EGLImageKHR image);
  EGLDisplay egl_display = EGL_DEFAULT_DISPLAY;
  GstGLDisplayEGL *display_egl;

  gst_eglDestroyImage = gst_gl_context_get_proc_address (context,
      "eglDestroyImage");
  if (!gst_eglDestroyImage) {
    gst_eglDestroyImage = gst_gl_context_get_proc_address (context,
        "eglDestroyImageKHR");
    if (!gst_eglDestroyImage) {
      GST_ERROR_OBJECT (context, "\"eglDestroyImage\" not exposed by the "
          "implementation");
      return;
    }
  }

  display_egl = gst_gl_display_egl_from_gl_display (context->display);
  if (!display_egl) {
    GST_WARNING_OBJECT (context, "Failed to retrieve GstGLDisplayEGL from %"
        GST_PTR_FORMAT, context->display);
    return;
  }
  egl_display =
      (EGLDisplay) gst_gl_display_get_handle (GST_GL_DISPLAY (display_egl));
  gst_object_unref (display_egl);

  if (!gst_eglDestroyImage (egl_display, image))
    GST_WARNING_OBJECT (context, "eglDestroyImage failed");
}

static void
_destroy_egl_image (GstEGLImage * image, gpointer user_data)
{
  _gst_egl_image_destroy (image->context, image->image);
}

/**
 * gst_egl_image_from_texture:
 * @context: a #GstGLContext (must be an EGL context)
 * @gl_mem: a #GstGLMemory
 * @attribs: additional attributes to add to the eglCreateImage() call.
 *
 * Returns: (transfer full): a #GstEGLImage wrapping @gl_mem or %NULL on failure
 */
GstEGLImage *
gst_egl_image_from_texture (GstGLContext * context, GstGLMemory * gl_mem,
    guintptr * attribs)
{
  EGLenum egl_target;
  EGLImageKHR img;

  if (gl_mem->tex_target != GST_GL_TEXTURE_TARGET_2D) {
    GST_FIXME_OBJECT (context, "Only know how to create EGLImage's from 2D "
        "textures");
    return NULL;
  }

  egl_target = EGL_GL_TEXTURE_2D_KHR;

  img = _gst_egl_image_create (context, egl_target,
      (EGLClientBuffer) (guintptr) gl_mem->tex_id, attribs);
  if (!img)
    return NULL;

  return gst_egl_image_new_wrapped (context, img, gl_mem->tex_format, NULL,
      (GstEGLImageDestroyNotify) _destroy_egl_image);
}

#if GST_GL_HAVE_DMABUF
/*
 * GStreamer format descriptions differ from DRM formats as the representation
 * is relative to a register, hence in native endianness. To reduce the driver
 * requirement, we only import with a subset of texture formats and use
 * shaders to convert. This way we avoid having to use external texture
 * target.
 */
static int
_drm_fourcc_from_info (GstVideoInfo * info, int plane)
{
  GstVideoFormat format = GST_VIDEO_INFO_FORMAT (info);
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  const gint rgba_fourcc = DRM_FORMAT_ABGR8888;
  const gint rgb_fourcc = DRM_FORMAT_BGR888;
  const gint rg_fourcc = DRM_FORMAT_GR88;
#else
  const gint rgba_fourcc = DRM_FORMAT_RGBA8888;
  const gint rgb_fourcc = DRM_FORMAT_RGB888;
  const gint rg_fourcc = DRM_FORMAT_RG88;
#endif

  GST_DEBUG ("Getting DRM fourcc for %s plane %i",
      gst_video_format_to_string (format), plane);

  switch (format) {
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      return DRM_FORMAT_RGB565;

    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      return rgb_fourcc;

    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_AYUV:
      return rgba_fourcc;

    case GST_VIDEO_FORMAT_GRAY8:
      return DRM_FORMAT_R8;

    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_GRAY16_BE:
      return rg_fourcc;

    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      return plane == 0 ? DRM_FORMAT_R8 : rg_fourcc;

    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y444:
      return DRM_FORMAT_R8;

    default:
      GST_ERROR ("Unsupported format for DMABuf.");
      return -1;
  }
}

/**
 * gst_egl_image_from_dmabuf:
 * @context: a #GstGLContext (must be an EGL context)
 * @dmabuf: the DMA-Buf file descriptor
 * @in_info: the #GstVideoInfo in @dmabuf
 * @plane: the plane in @in_info to create and #GstEGLImage for
 * @offset: the byte-offset in the data
 *
 * Returns: a #GstEGLImage wrapping @dmabuf or %NULL on failure
 */
GstEGLImage *
gst_egl_image_from_dmabuf (GstGLContext * context,
    gint dmabuf, GstVideoInfo * in_info, gint plane, gsize offset)
{
  GstGLFormat format;
  guintptr attribs[13];
  EGLImageKHR img;
  gint atti = 0;
  gint fourcc;
  gint i;

  fourcc = _drm_fourcc_from_info (in_info, plane);
  format = gst_gl_format_from_video_info (context, in_info, plane);

  GST_DEBUG ("fourcc %.4s (%d) plane %d (%dx%d)",
      (char *) &fourcc, fourcc, plane,
      GST_VIDEO_INFO_COMP_WIDTH (in_info, plane),
      GST_VIDEO_INFO_COMP_HEIGHT (in_info, plane));

  attribs[atti++] = EGL_WIDTH;
  attribs[atti++] = GST_VIDEO_INFO_COMP_WIDTH (in_info, plane);
  attribs[atti++] = EGL_HEIGHT;
  attribs[atti++] = GST_VIDEO_INFO_COMP_HEIGHT (in_info, plane);
  attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
  attribs[atti++] = fourcc;
  attribs[atti++] = EGL_DMA_BUF_PLANE0_FD_EXT;
  attribs[atti++] = dmabuf;
  attribs[atti++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
  attribs[atti++] = offset;
  attribs[atti++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
  attribs[atti++] = GST_VIDEO_INFO_PLANE_STRIDE (in_info, plane);
  attribs[atti] = EGL_NONE;

  for (i = 0; i < atti; i++)
    GST_LOG ("attr %i: %" G_GINTPTR_FORMAT, i, attribs[i]);

  g_assert (atti == 12);

  img = _gst_egl_image_create (context, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
  if (!img) {
    GST_WARNING ("eglCreateImage failed: %s",
        gst_egl_get_error_string (eglGetError ()));
    return NULL;
  }

  return gst_egl_image_new_wrapped (context, img, format, NULL,
      (GstEGLImageDestroyNotify) _destroy_egl_image);
}
#endif /* GST_GL_HAVE_DMABUF */
