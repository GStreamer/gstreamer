/*
 * GStreamer Android Video Platform Wrapper
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
#  include <config.h>
#endif

#if defined (USE_EGL_RPI) && defined(__GNUC__)
#ifndef __VCCOREVER__
#define __VCCOREVER__ 0x04000000
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC optimize ("gnu89-inline")
#endif

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#if defined (USE_EGL_RPI) && defined(__GNUC__)
#pragma GCC reset_options
#pragma GCC diagnostic pop
#endif

#include <string.h>

#include <gst/gst.h>
#include "video_platform_wrapper.h"

GST_DEBUG_CATEGORY_STATIC (eglgles_platform_wrapper);
#define GST_CAT_DEFAULT eglgles_platform_wrapper

/* XXX: Likely to be removed */
gboolean
platform_wrapper_init (void)
{
  GST_DEBUG_CATEGORY_INIT (eglgles_platform_wrapper,
      "eglglessink-platform", 0,
      "Platform dependent native-window utility routines for EglGles");
  return TRUE;
}

#ifdef USE_EGL_X11
#include <X11/Xlib.h>

typedef struct
{
  Display *display;
} X11WindowData;

EGLNativeWindowType
platform_create_native_window (gint width, gint height, gpointer * window_data)
{
  Display *d;
  Window w;
  int s;
  X11WindowData *data;

  d = XOpenDisplay (NULL);
  if (d == NULL) {
    GST_ERROR ("Can't open X11 display");
    return (EGLNativeWindowType) 0;
  }

  s = DefaultScreen (d);
  w = XCreateSimpleWindow (d, RootWindow (d, s), 10, 10, width, height, 1,
      BlackPixel (d, s), WhitePixel (d, s));
  XStoreName (d, w, "eglglessink");
  XMapWindow (d, w);
  XFlush (d);

  *window_data = data = g_slice_new0 (X11WindowData);
  data->display = d;

  return (EGLNativeWindowType) w;
}

gboolean
platform_destroy_native_window (EGLNativeDisplayType display,
    EGLNativeWindowType window, gpointer * window_data)
{
  X11WindowData *data = *window_data;

  /* XXX: Should proly catch BadWindow */
  XDestroyWindow (data->display, (Window) window);
  XSync (data->display, FALSE);
  XCloseDisplay (data->display);

  g_slice_free (X11WindowData, data);
  *window_data = NULL;
  return TRUE;
}
#endif

#ifdef USE_EGL_MALI_FB
#include <EGL/fbdev_window.h>

EGLNativeWindowType
platform_create_native_window (gint width, gint height, gpointer * window_data)
{
  fbdev_window *w = g_slice_new0 (fbdev_window);

  w->width = width;
  w->height = height;

  return (EGLNativeWindowType) w;
}

gboolean
platform_destroy_native_window (EGLNativeDisplayType display,
    EGLNativeWindowType window, gpointer * window_data)
{
  g_slice_free (fbdev_window, ((fbdev_window *) window));

  return TRUE;
}

/* FIXME: Move to gst-libs/gst/egl */
#if 0
#include <mali_egl_image.h>
#include <ump/ump.h>
#include <ump/ump_ref_drv.h>
#include <gst/video/video.h>
static gpointer
eglimage_map (GstMemory * gmem, gsize maxsize, GstMapFlags flags)
{
  GstEGLImageMemory *mem;
  gint i;

  g_return_val_if_fail (strcmp (gmem->allocator->mem_type,
          GST_EGL_IMAGE_MEMORY_NAME) == 0, FALSE);

  mem = GST_EGL_IMAGE_MEMORY (gmem);

  g_mutex_lock (&mem->lock);
  for (i = 0; i < mem->n_textures; i++) {
    if (mem->memory_refcount[i]) {
      /* Only multiple READ maps are allowed */
      if ((mem->memory_flags[i] & GST_MAP_WRITE)) {
        g_mutex_unlock (&mem->lock);
        return NULL;
      }
    }
  }

  if (!mem->mapped_memory_refcount) {
    EGLint attribs[] = {
      MALI_EGL_IMAGE_PLANE, MALI_EGL_IMAGE_PLANE_Y,
      MALI_EGL_IMAGE_ACCESS_MODE, MALI_EGL_IMAGE_ACCESS_READ_ONLY,
      EGL_NONE
    };
    GstVideoInfo info;
    mali_egl_image *mali_egl_image;
    guint8 *plane_memory, *p;
    gint stride, h;
    gint j;

    gst_video_info_set_format (&info, mem->format, mem->width, mem->height);

    mem->mapped_memory = g_malloc (mem->parent.size);

    for (i = 0; i < mem->n_textures; i++) {
      mali_egl_image = mali_egl_image_lock_ptr (mem->image[i]);
      if (!mali_egl_image) {
        g_free (mem->mapped_memory);
        GST_ERROR ("Failed to lock Mali EGL image: 0x%04x",
            mali_egl_image_get_error ());
        g_mutex_unlock (&mem->lock);
        return NULL;
      }
      plane_memory = mali_egl_image_map_buffer (mali_egl_image, attribs);
      if (!plane_memory) {
        mali_egl_image_unlock_ptr (mem->image[i]);
        g_free (mem->mapped_memory);
        GST_ERROR ("Failed to lock Mali map image: 0x%04x",
            mali_egl_image_get_error ());
        g_mutex_unlock (&mem->lock);
        return NULL;
      }

      p = ((guint8 *) mem->mapped_memory) + mem->offset[i];
      stride = mem->stride[i];
      h = GST_VIDEO_INFO_COMP_HEIGHT (&info, i);
      for (j = 0; j < h; j++) {
        memcpy (p, plane_memory, stride);
        p += mem->stride[i];
        plane_memory += mem->stride[i];
      }

      mali_egl_image_unmap_buffer (mem->image[i], attribs);
      mali_egl_image_unlock_ptr (mem->image[i]);
    }
  } else {
    /* Only multiple READ maps are allowed */
    if ((mem->mapped_memory_flags & GST_MAP_WRITE)) {
      g_mutex_unlock (&mem->lock);
      return NULL;
    }
  }
  mem->mapped_memory_refcount++;

  g_mutex_unlock (&mem->lock);

  return mem->mapped_memory;
}

static void
eglimage_unmap (GstMemory * gmem)
{
  GstEGLImageMemory *mem;
  gint i;

  g_return_if_fail (strcmp (gmem->allocator->mem_type,
          GST_EGL_IMAGE_MEMORY_NAME) == 0);

  mem = GST_EGL_IMAGE_MEMORY (gmem);
  g_return_if_fail (mem->mapped_memory);

  g_mutex_lock (&mem->lock);

  mem->mapped_memory_refcount--;
  if (mem->mapped_memory_refcount > 0) {
    g_mutex_unlock (&mem->lock);
    return;
  }

  /* Write back */
  if ((mem->mapped_memory_flags & GST_MAP_WRITE)) {
    EGLint attribs[] = {
      MALI_EGL_IMAGE_PLANE, MALI_EGL_IMAGE_PLANE_Y,
      MALI_EGL_IMAGE_ACCESS_MODE, MALI_EGL_IMAGE_ACCESS_WRITE_ONLY,
      EGL_NONE
    };
    GstVideoInfo info;
    mali_egl_image *mali_egl_image;
    guint8 *plane_memory, *p;
    gint stride, h;
    gint j;

    gst_video_info_set_format (&info, mem->format, mem->width, mem->height);

    for (i = 0; i < mem->n_textures; i++) {
      mali_egl_image = mali_egl_image_lock_ptr (mem->image[i]);
      if (!mali_egl_image) {
        g_free (mem->mapped_memory);
        GST_ERROR ("Failed to lock Mali EGL image: 0x%04x",
            mali_egl_image_get_error ());
        g_mutex_unlock (&mem->lock);
        return;
      }
      plane_memory = mali_egl_image_map_buffer (mali_egl_image, attribs);
      if (!plane_memory) {
        mali_egl_image_unlock_ptr (mem->image[i]);
        g_free (mem->mapped_memory);
        GST_ERROR ("Failed to lock Mali map image: 0x%04x",
            mali_egl_image_get_error ());
        g_mutex_unlock (&mem->lock);
        return;
      }

      p = ((guint8 *) mem->mapped_memory) + mem->offset[i];
      stride = mem->stride[i];
      h = GST_VIDEO_INFO_COMP_HEIGHT (&info, i);
      for (j = 0; j < h; j++) {
        memcpy (plane_memory, p, stride);
        p += mem->stride[i];
        plane_memory += mem->stride[i];
      }

      mali_egl_image_unmap_buffer (mem->image[i], attribs);
      mali_egl_image_unlock_ptr (mem->image[i]);
    }
  }
  g_free (mem->mapped_memory);

  g_mutex_unlock (&mem->lock);
}

static gboolean
eglimage_video_map (GstVideoMeta * meta, guint plane,
    GstMapInfo * info, gpointer * data, gint * stride, GstMapFlags flags)
{
  GstMemory *gmem;
  GstEGLImageMemory *mem;
  GstVideoInfo vinfo;

  if (gst_buffer_n_memory (meta->buffer) != 1)
    return default_map_video (meta, plane, info, data, stride, flags);

  gmem = gst_buffer_peek_memory (meta->buffer, 0);
  if (strcmp (gmem->allocator->mem_type, GST_EGL_IMAGE_MEMORY_NAME) != 0)
    return default_map_video (meta, plane, info, data, stride, flags);

  mem = GST_EGL_IMAGE_MEMORY ((gmem->parent ? gmem->parent : gmem));

  g_mutex_lock (&mem->lock);
  if (mem->format == GST_VIDEO_FORMAT_YV12) {
    if (plane == 1)
      plane = 2;
    else if (plane == 2)
      plane = 1;
  }

  if (mem->mapped_memory_refcount) {
    /* Only multiple READ maps are allowed */
    if ((mem->mapped_memory_flags & GST_MAP_WRITE)) {
      g_mutex_unlock (&mem->lock);
      return FALSE;
    }
  }

  if (!mem->memory_refcount[plane]) {
    EGLint attribs[] = {
      MALI_EGL_IMAGE_PLANE, MALI_EGL_IMAGE_PLANE_Y,
      MALI_EGL_IMAGE_ACCESS_MODE, MALI_EGL_IMAGE_ACCESS_READ_WRITE,
      EGL_NONE
    };

    if ((flags & GST_MAP_READ) && (flags & GST_MAP_WRITE))
      attribs[3] = MALI_EGL_IMAGE_ACCESS_READ_WRITE;
    else if ((flags & GST_MAP_READ))
      attribs[3] = MALI_EGL_IMAGE_ACCESS_READ_ONLY;
    else if ((flags & GST_MAP_WRITE))
      attribs[3] = MALI_EGL_IMAGE_ACCESS_WRITE_ONLY;

    mem->memory_platform_data[plane] =
        mali_egl_image_lock_ptr (mem->image[plane]);
    if (!mem->memory_platform_data[plane]) {
      GST_ERROR ("Failed to lock Mali EGL image: 0x%04x",
          mali_egl_image_get_error ());
      goto map_error;
    }

    mem->memory[plane] =
        mali_egl_image_map_buffer (mem->memory_platform_data[plane], attribs);
    if (!mem->memory[plane])
      goto map_error;

    mem->memory_flags[plane] = flags;
  } else {
    /* Only multiple READ maps are allowed */
    if ((mem->memory_flags[plane] & GST_MAP_WRITE)) {
      g_mutex_unlock (&mem->lock);
      return FALSE;
    }
  }

  mem->memory_refcount[plane]++;
  gst_video_info_set_format (&vinfo, mem->format, mem->width, mem->height);

  *data = mem->memory[plane];
  *stride = mem->stride[plane];

  g_mutex_unlock (&mem->lock);
  return TRUE;

map_error:
  {
    EGLint attribs[] = {
      MALI_EGL_IMAGE_PLANE, MALI_EGL_IMAGE_PLANE_Y,
      EGL_NONE
    };
    GST_ERROR ("Failed to map Mali EGL image: 0x%04x",
        mali_egl_image_get_error ());

    if (mem->memory_platform_data[plane]) {
      mali_egl_image_unmap_buffer (mem->image[plane], attribs);
      mali_egl_image_unlock_ptr (mem->image[plane]);
    }
    mem->memory[plane] = NULL;
    mem->memory_platform_data[plane] = NULL;

    g_mutex_unlock (&mem->lock);

    return FALSE;
  }
}

static gboolean
eglimage_video_unmap (GstVideoMeta * meta, guint plane, GstMapInfo * info)
{
  GstMemory *gmem;
  GstEGLImageMemory *mem;
  EGLint attribs[] = {
    MALI_EGL_IMAGE_PLANE, MALI_EGL_IMAGE_PLANE_Y,
    EGL_NONE
  };

  if (gst_buffer_n_memory (meta->buffer) != 1)
    return default_unmap_video (meta, plane, info);

  gmem = gst_buffer_peek_memory (meta->buffer, 0);
  if (strcmp (gmem->allocator->mem_type, GST_EGL_IMAGE_MEMORY_NAME) != 0)
    return default_unmap_video (meta, plane, info);

  mem = GST_EGL_IMAGE_MEMORY ((gmem->parent ? gmem->parent : gmem));

  g_mutex_lock (&mem->lock);
  if (mem->format == GST_VIDEO_FORMAT_YV12) {
    if (plane == 1)
      plane = 2;
    else if (plane == 2)
      plane = 1;
  }

  if (!mem->memory_refcount[plane]) {
    g_mutex_unlock (&mem->lock);
    g_return_val_if_reached (FALSE);
  }

  mem->memory_refcount[plane]--;
  if (mem->memory_refcount[plane] > 0) {
    g_mutex_unlock (&mem->lock);
    return TRUE;
  }

  /* Unmaps automatically */
  if (mem->memory_platform_data[plane]) {
    mali_egl_image_unmap_buffer (mem->image[plane], attribs);
    mali_egl_image_unlock_ptr (mem->image[plane]);
  }
  mem->memory[plane] = NULL;
  mem->memory_platform_data[plane] = NULL;

  g_mutex_unlock (&mem->lock);

  return TRUE;
}

gboolean
platform_can_map_eglimage (GstMemoryMapFunction * map,
    GstMemoryUnmapFunction * unmap, PlatformMapVideo * video_map,
    PlatformUnmapVideo * video_unmap)
{
  *map = eglimage_map;
  *unmap = eglimage_unmap;
  *video_map = eglimage_video_map;
  *video_unmap = eglimage_video_unmap;
  return TRUE;
}

gboolean
platform_has_custom_eglimage_alloc (void)
{
  return TRUE;
}

gboolean
platform_alloc_eglimage (EGLDisplay display, EGLContext context, GLint format,
    GLint type, gint width, gint height, GLuint tex_id, EGLImageKHR * image,
    gpointer * image_platform_data)
{
  fbdev_pixmap pixmap;

  pixmap.flags = FBDEV_PIXMAP_SUPPORTS_UMP;
  pixmap.width = width;
  pixmap.height = height;

  switch (format) {
    case GL_LUMINANCE:
      g_return_val_if_fail (type == GL_UNSIGNED_BYTE, FALSE);
      pixmap.red_size = 0;
      pixmap.green_size = 0;
      pixmap.blue_size = 0;
      pixmap.alpha_size = 0;
      pixmap.luminance_size = 8;
      break;
    case GL_LUMINANCE_ALPHA:
      g_return_val_if_fail (type == GL_UNSIGNED_BYTE, FALSE);
      pixmap.red_size = 0;
      pixmap.green_size = 0;
      pixmap.blue_size = 0;
      pixmap.alpha_size = 8;
      pixmap.luminance_size = 8;
      break;
    case GL_RGB:
      if (type == GL_UNSIGNED_BYTE) {
        pixmap.red_size = 8;
        pixmap.green_size = 8;
        pixmap.blue_size = 8;
        pixmap.alpha_size = 0;
        pixmap.luminance_size = 0;
      } else if (type == GL_UNSIGNED_SHORT_5_6_5) {
        pixmap.red_size = 5;
        pixmap.green_size = 6;
        pixmap.blue_size = 5;
        pixmap.alpha_size = 0;
        pixmap.luminance_size = 0;
      } else {
        g_return_val_if_reached (FALSE);
      }
      break;
    case GL_RGBA:
      g_return_val_if_fail (type == GL_UNSIGNED_BYTE, FALSE);
      pixmap.red_size = 8;
      pixmap.green_size = 8;
      pixmap.blue_size = 8;
      pixmap.alpha_size = 8;
      pixmap.luminance_size = 0;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  pixmap.buffer_size =
      pixmap.red_size + pixmap.green_size + pixmap.blue_size +
      pixmap.alpha_size + pixmap.luminance_size;
  pixmap.bytes_per_pixel = pixmap.buffer_size / 8;
  pixmap.format = 0;

  if (ump_open () != UMP_OK) {
    GST_ERROR ("Failed to open UMP");
    return FALSE;
  }

  pixmap.data =
      ump_ref_drv_allocate (GST_ROUND_UP_4 (pixmap.width) * pixmap.height *
      pixmap.bytes_per_pixel, UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR);
  if (pixmap.data == UMP_INVALID_MEMORY_HANDLE) {
    GST_ERROR ("Failed to allocate pixmap data via UMP");
    ump_close ();
    return FALSE;
  }

  *image_platform_data = g_slice_dup (fbdev_pixmap, &pixmap);
  *image =
      eglCreateImageKHR (display, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR,
      (EGLClientBuffer) * image_platform_data, NULL);
  if (!image) {
    GST_ERROR ("Failed to create EGLImage for pixmap");
    ump_reference_release ((ump_handle) pixmap.data);
    ump_close ();
    g_slice_free (fbdev_pixmap, *image_platform_data);
    return FALSE;
  }

  return TRUE;
}

void
platform_free_eglimage (EGLDisplay display, EGLContext context, GLuint tex_id,
    EGLImageKHR * image, gpointer * image_platform_data)
{
  fbdev_pixmap *pixmap = *image_platform_data;

  eglDestroyImageKHR (display, *image);
  ump_reference_release ((ump_handle) pixmap->data);
  ump_close ();
  g_slice_free (fbdev_pixmap, *image_platform_data);
}
#endif
#endif

#ifdef USE_EGL_RPI
#include <bcm_host.h>
#include <gst/video/gstvideosink.h>

typedef struct
{
  EGL_DISPMANX_WINDOW_T w;
  DISPMANX_DISPLAY_HANDLE_T d;
} RPIWindowData;

EGLNativeWindowType
platform_create_native_window (gint width, gint height, gpointer * window_data)
{
  DISPMANX_ELEMENT_HANDLE_T dispman_element;
  DISPMANX_DISPLAY_HANDLE_T dispman_display;
  DISPMANX_UPDATE_HANDLE_T dispman_update;
  RPIWindowData *data;
  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;
  GstVideoRectangle src, dst, res;

  uint32_t dp_height;
  uint32_t dp_width;

  int ret;

  ret = graphics_get_display_size (0, &dp_width, &dp_height);
  if (ret < 0) {
    GST_ERROR ("Can't open display");
    return (EGLNativeWindowType) 0;
  }
  GST_DEBUG ("Got display size: %dx%d\n", dp_width, dp_height);
  GST_DEBUG ("Source size: %dx%d\n", width, height);

  /* Center width*height frame inside dp_width*dp_height */
  src.w = width;
  src.h = height;
  src.x = src.y = 0;
  dst.w = dp_width;
  dst.h = dp_height;
  dst.x = dst.y = 0;
  gst_video_sink_center_rect (src, dst, &res, TRUE);

  dst_rect.x = res.x;
  dst_rect.y = res.y;
  dst_rect.width = res.w;
  dst_rect.height = res.h;

  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = width << 16;
  src_rect.height = height << 16;

  dispman_display = vc_dispmanx_display_open (0);
  dispman_update = vc_dispmanx_update_start (0);
  dispman_element = vc_dispmanx_element_add (dispman_update,
      dispman_display, 0, &dst_rect, 0, &src_rect,
      DISPMANX_PROTECTION_NONE, 0, 0, 0);

  *window_data = data = g_slice_new0 (RPIWindowData);
  data->d = dispman_display;
  data->w.element = dispman_element;
  data->w.width = width;
  data->w.height = height;
  vc_dispmanx_update_submit_sync (dispman_update);

  return (EGLNativeWindowType) data;
}

gboolean
platform_destroy_native_window (EGLNativeDisplayType display,
    EGLNativeWindowType window, gpointer * window_data)
{
  DISPMANX_DISPLAY_HANDLE_T dispman_display;
  DISPMANX_UPDATE_HANDLE_T dispman_update;
  RPIWindowData *data = *window_data;

  dispman_display = data->d;
  dispman_update = vc_dispmanx_update_start (0);
  vc_dispmanx_element_remove (dispman_update, data->w.element);
  vc_dispmanx_update_submit_sync (dispman_update);
  vc_dispmanx_display_close (dispman_display);

  g_slice_free (RPIWindowData, data);
  *window_data = NULL;
  return TRUE;
}
#endif

#if !defined(USE_EGL_X11) && !defined(USE_EGL_MALI_FB) && !defined(USE_EGL_RPI)
/* Dummy functions for creating a native Window */
EGLNativeWindowType
platform_create_native_window (gint width, gint height, gpointer * window_data)
{
  GST_ERROR ("Can't create native window");
  return (EGLNativeWindowType) 0;
}

gboolean
platform_destroy_native_window (EGLNativeDisplayType display,
    EGLNativeWindowType window, gpointer * window_data)
{
  GST_ERROR ("Can't destroy native window");
  return TRUE;
}
#endif
