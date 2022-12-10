/*
 *  gstvaapisurface_egl.c - VA surface abstraction (EGL interop)
 *
 *  Copyright (C) 2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "sysdeps.h"

#include "gstvaapisurface_egl.h"

#include "gstvaapibufferproxy_priv.h"
#include "gstvaapicompat.h"
#include "gstvaapidisplay_egl_priv.h"
#include "gstvaapifilter.h"
#include "gstvaapiimage_priv.h"
#include "gstvaapisurface_drm.h"
#include "gstvaapisurface_priv.h"

#if GST_VAAPI_USE_DRM
#include <drm_fourcc.h>
#else
#define DRM_FORMAT_MOD_LINEAR 0ULL
#define DRM_FORMAT_MOD_INVALID ((1ULL << 56) - 1)
#endif

typedef struct
{
  GstVaapiDisplayEGL *display;
  EGLImageKHR image;
  GstVideoFormat format;
  guint width;
  guint height;
  guint mem_types;
  GstVaapiSurface *surface;     /* result */
} CreateSurfaceWithEGLImageArgs;

static GstVaapiSurface *
do_create_surface_with_egl_image_unlocked (GstVaapiDisplayEGL * display,
    EGLImageKHR image, GstVideoFormat format, guint width, guint height,
    guint mem_types)
{
  GstVaapiDisplay *const base_display = GST_VAAPI_DISPLAY_CAST (display);
  EglContext *const ctx = GST_VAAPI_DISPLAY_EGL_CONTEXT (display);
  EglVTable *vtable;

  if (!ctx || !(vtable = egl_context_get_vtable (ctx, FALSE)))
    return NULL;

  if ((mem_types & VA_SURFACE_ATTRIB_MEM_TYPE_KERNEL_DRM)
      && vtable->has_EGL_MESA_drm_image) {
    gsize size, offset[GST_VIDEO_MAX_PLANES] = { 0, };
    gint name, stride[GST_VIDEO_MAX_PLANES] = { 0, };

    /* EGL_MESA_drm_image extension */
    if (!vtable->eglExportDRMImageMESA (ctx->display->base.handle.p, image,
            &name, NULL, &stride[0]))
      goto error_export_image_gem_buf;

    size = height * stride[0];
    /*
     * XXX: The below surface creation may fail on Intel due to:
     *   https://github.com/01org/intel-vaapi-driver/issues/222
     * A permanent fix for that problem will be released in intel-vaapi-driver
     * version 1.8.4 and later, and also in 1.8.3-1ubuntu1.
     * However if you don't have that fix then a simple workaround is to
     * uncomment this line of code:
     *   size = GST_ROUND_UP_32 (height) * stride[0];
     */

    return gst_vaapi_surface_new_with_gem_buf_handle (base_display, name, size,
        format, width, height, offset, stride);
  }

  if ((mem_types & VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME)
      && vtable->has_EGL_MESA_image_dma_buf_export) {
    int fourcc, num_planes, fd;
    EGLint offset = 0;
    EGLint stride = 0;
    EGLuint64KHR modifier;
    GstVideoInfo vi;

    if (!vtable->eglExportDMABUFImageQueryMESA (ctx->display->base.handle.p,
            image, &fourcc, &num_planes, &modifier))
      goto error_export_dma_buf_image_query;

    /* Don't allow multi-plane dmabufs */
    if (num_planes != 1)
      goto error_bad_parameters;

    /* FIXME: We don't support modifiers */
    if (modifier != DRM_FORMAT_MOD_LINEAR && modifier != DRM_FORMAT_MOD_INVALID)
      goto error_bad_parameters;

    /* fix color format if needed */
    if (fourcc == GST_MAKE_FOURCC ('A', 'B', '2', '4'))
      format = gst_vaapi_video_format_from_va_fourcc (VA_FOURCC_RGBA);
    else if (fourcc == GST_MAKE_FOURCC ('A', 'R', '2', '4'))
      format = gst_vaapi_video_format_from_va_fourcc (VA_FOURCC_BGRA);

    if (!vtable->eglExportDMABUFImageMESA (ctx->display->base.handle.p, image,
            &fd, &stride, &offset))
      goto error_export_dma_buf_image;

    gst_video_info_set_format (&vi, format, width, height);
    GST_VIDEO_INFO_PLANE_OFFSET (&vi, 0) = offset;
    GST_VIDEO_INFO_PLANE_STRIDE (&vi, 0) = stride;

    return gst_vaapi_surface_new_with_dma_buf_handle (base_display, fd, &vi);
  }
#ifndef GST_DISABLE_GST_DEBUG
  {
    GString *str = g_string_new (NULL);

    if (mem_types & VA_SURFACE_ATTRIB_MEM_TYPE_VA)
      g_string_append (str, "VA ");
    if (mem_types & VA_SURFACE_ATTRIB_MEM_TYPE_V4L2)
      g_string_append (str, "V4L2 ");
    if (mem_types & VA_SURFACE_ATTRIB_MEM_TYPE_USER_PTR)
      g_string_append (str, "PTR ");
#if VA_CHECK_VERSION(1,1,0)
    if (mem_types & VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2)
      g_string_append (str, "PRIME_2 ");
#endif

    GST_ERROR ("missing EGL extensions for memory types: %s", str->str);
    g_string_free (str, TRUE);
  }
#endif

  return NULL;

  /* ERRORS */
error_export_image_gem_buf:
  {
    GST_ERROR ("failed to export EGL image to GEM buffer");
    return NULL;
  }
error_export_dma_buf_image_query:
  {
    GST_ERROR ("failed to query EGL image for dmabuf export");
    return NULL;
  }
error_bad_parameters:
  {
    GST_ERROR ("multi-planed nor non-linear dmabufs are not supported");
    return NULL;
  }
error_export_dma_buf_image:
  {
    GST_ERROR ("missing EGL_MESA_image_dma_buf_export extension");
    return NULL;
  }
}

static void
do_create_surface_with_egl_image (CreateSurfaceWithEGLImageArgs * args)
{
  GST_VAAPI_DISPLAY_LOCK (args->display);
  args->surface = do_create_surface_with_egl_image_unlocked (args->display,
      args->image, args->format, args->width, args->height, args->mem_types);
  GST_VAAPI_DISPLAY_UNLOCK (args->display);
}

// Creates VA surface with EGLImage buffer as backing storage (internal)
static inline GstVaapiSurface *
create_surface_with_egl_image (GstVaapiDisplayEGL * display, EGLImageKHR image,
    GstVideoFormat format, guint width, guint height, guint mem_types)
{
  CreateSurfaceWithEGLImageArgs args =
      { display, image, format, width, height, mem_types };

  if (!egl_context_run (GST_VAAPI_DISPLAY_EGL_CONTEXT (display),
          (EglContextRunFunc) do_create_surface_with_egl_image, &args))
    return NULL;
  return args.surface;
}

// Creates VA surface from an EGLImage buffer copy (internal)
static GstVaapiSurface *
create_surface_from_egl_image (GstVaapiDisplayEGL * display,
    const GstVideoInfo * vip, EGLImageKHR image, GstVideoFormat format,
    guint width, guint height, guint flags)
{
  GstVaapiDisplay *const base_display = GST_VAAPI_DISPLAY_CAST (display);
  GstVaapiSurface *img_surface = NULL, *out_surface = NULL;
  gboolean use_native_format = TRUE;
  GstVaapiFilter *filter = NULL;
  GstVaapiFilterStatus filter_status;

  img_surface = create_surface_with_egl_image (display, image, format,
      width, height, 0);
  if (!img_surface)
    return NULL;

  if (vip) {
    use_native_format =
        GST_VIDEO_INFO_FORMAT (vip) == GST_VIDEO_FORMAT_ENCODED ||
        GST_VIDEO_INFO_FORMAT (vip) == GST_VIDEO_FORMAT_UNKNOWN;

    if (GST_VIDEO_INFO_WIDTH (vip) && GST_VIDEO_INFO_HEIGHT (vip)) {
      width = GST_VIDEO_INFO_WIDTH (vip);
      height = GST_VIDEO_INFO_HEIGHT (vip);
    }
  }

  if (use_native_format) {
    out_surface = gst_vaapi_surface_new (base_display,
        GST_VAAPI_CHROMA_TYPE_YUV420, width, height);
  } else {
    out_surface = gst_vaapi_surface_new_with_format (base_display,
        GST_VIDEO_INFO_FORMAT (vip), width, height, 0);
  }
  if (!out_surface)
    goto error_create_surface;

  filter = gst_vaapi_filter_new (base_display);
  if (!filter)
    goto error_create_filter;

  filter_status = gst_vaapi_filter_process (filter,
      img_surface, out_surface, flags);
  if (filter_status != GST_VAAPI_FILTER_STATUS_SUCCESS)
    goto error_convert_surface;

  gst_vaapi_surface_unref (img_surface);
  gst_object_unref (filter);
  return out_surface;

  /* ERRORS */
error_create_surface:
  GST_ERROR ("failed to create output surface format:%s size:%dx%d",
      gst_vaapi_video_format_to_string (vip ? GST_VIDEO_INFO_FORMAT (vip) :
          GST_VIDEO_FORMAT_ENCODED), width, height);
  goto error_cleanup;
error_convert_surface:
  GST_ERROR ("failed to transfer EGL image to VA surface (status = %d)",
      filter_status);
  goto error_cleanup;
error_create_filter:
  GST_ERROR ("failed to create video processing filter");
  // fall-through
error_cleanup:
  gst_mini_object_replace ((GstMiniObject **) & img_surface, NULL);
  gst_mini_object_replace ((GstMiniObject **) & out_surface, NULL);
  gst_vaapi_filter_replace (&filter, NULL);
  return NULL;
}

/**
 * gst_vaapi_surface_new_from_egl_image:
 * @display: a #GstVaapiDisplay
 * @vip: the desired (optional) #GstVideoInfo constraints
 * @image: the EGL image to import
 * @format: the EGL @image format
 * @width: the EGL @image width in pixels
 * @height: the EGL @image height in pixels
 * @flags: postprocessing flags. See #GstVaapiSurfaceRenderFlags
 *
 * Creates a new #GstVaapiSurface with a copy of the EGL image
 * contents. i.e. the input EGL @image can be disposed and the
 * resulting VA surface would still be valid with the contents at the
 * time this function was called.
 *
 * If @vip is %NULL, then the resulting VA surface will be created
 * with the same video format and size as the original @image. If @vip
 * is non-%NULL and the desired format is GST_VIDEO_FORMAT_ENCODED,
 * then the resulting VA surface will have the best "native" HW
 * format, usually NV12.
 *
 * Return value: the newly allocated #GstVaapiSurface object, or %NULL
 *   if creation from EGL image failed, or is not supported
 */
GstVaapiSurface *
gst_vaapi_surface_new_from_egl_image (GstVaapiDisplay * base_display,
    const GstVideoInfo * vip, EGLImageKHR image, GstVideoFormat format,
    guint width, guint height, guint flags)
{
  GstVaapiDisplayEGL *display;

  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_EGL (base_display), NULL);
  g_return_val_if_fail (image != EGL_NO_IMAGE_KHR, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  display = GST_VAAPI_DISPLAY_EGL (base_display);
  if (!display || !GST_VAAPI_IS_DISPLAY_EGL (display))
    goto error_invalid_display;
  return create_surface_from_egl_image (display, vip, image, format,
      width, height, flags);

  /* ERRORS */
error_invalid_display:
  GST_ERROR ("invalid display (NULL or not of EGL class");
  return NULL;
}

/**
 * gst_vaapi_surface_new_with_egl_image:
 * @display: a #GstVaapiDisplay
 * @image: the EGL image to import
 * @format: the EGL @image format
 * @width: the EGL @image width in pixels
 * @height: the EGL @image height in pixels
 * @mem_types: the supported memory types
 *
 * Creates a new #GstVaapiSurface bound to an external EGL image.
 *
 * The caller maintains the lifetime of the EGL image object. In
 * particular, the EGL image shall not be destroyed before the last
 * reference to the resulting VA surface is released.
 *
 * Return value: the newly allocated #GstVaapiSurface object, or %NULL
 *   if creation from EGL image failed, or is not supported
 */
GstVaapiSurface *
gst_vaapi_surface_new_with_egl_image (GstVaapiDisplay * base_display,
    EGLImageKHR image, GstVideoFormat format, guint width, guint height,
    guint mem_types)
{
  GstVaapiDisplayEGL *display;

  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_EGL (base_display), NULL);
  g_return_val_if_fail (image != EGL_NO_IMAGE_KHR, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  display = GST_VAAPI_DISPLAY_EGL (base_display);
  if (!display || !GST_VAAPI_IS_DISPLAY_EGL (display))
    goto error_invalid_display;
  return create_surface_with_egl_image (display, image, format, width, height,
      mem_types);

  /* ERRORS */
error_invalid_display:
  GST_ERROR ("invalid display (NULL or not of EGL class");
  return NULL;
}
