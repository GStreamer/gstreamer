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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define EGL_EGLEXT_PROTOTYPES

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

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
