/*
 *  test-windows.c - Test GstVaapiWindow
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2013 Intel Corporation
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

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapiimage.h>
#if GST_VAAPI_USE_DRM
# include <gst/vaapi/gstvaapidisplay_drm.h>
# include <gst/vaapi/gstvaapiwindow_drm.h>
#endif
#if GST_VAAPI_USE_X11
# include <gst/vaapi/gstvaapidisplay_x11.h>
# include <gst/vaapi/gstvaapiwindow_x11.h>
#endif
#if GST_VAAPI_USE_WAYLAND
# include <gst/vaapi/gstvaapidisplay_wayland.h>
# include <gst/vaapi/gstvaapiwindow_wayland.h>
#endif
#if GST_VAAPI_USE_EGL
# include <gst/vaapi/gstvaapidisplay_egl.h>
# include <gst/vaapi/gstvaapiwindow_egl.h>
#endif
#include "image.h"

static inline void
pause (void)
{
  g_print ("Press any key to continue...\n");
  getchar ();
}

static GstVaapiSurface *
create_test_surface (GstVaapiDisplay * display, guint width, guint height)
{
  GstVaapiImage *image = NULL;
  GstVaapiSurface *surface;
  guint i;

  static const GstVaapiChromaType chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
  static const GstVideoFormat image_formats[] = {
    GST_VIDEO_FORMAT_NV12,
    GST_VIDEO_FORMAT_YV12,
    GST_VIDEO_FORMAT_I420,
    GST_VIDEO_FORMAT_VUYA,
    GST_VIDEO_FORMAT_ARGB,
    GST_VIDEO_FORMAT_BGRA,
    GST_VIDEO_FORMAT_RGBA,
    GST_VIDEO_FORMAT_ABGR,
    GST_VIDEO_FORMAT_UNKNOWN
  };

  surface = gst_vaapi_surface_new (display, chroma_type, width, height);
  if (!surface)
    g_error ("could not create Gst/VA surface");

  for (i = 0; image_formats[i] != GST_VIDEO_FORMAT_UNKNOWN; i++) {
    const GstVideoFormat format = image_formats[i];

    image = image_generate (display, format, width, height);
    if (!image)
      break;
    if (image_upload (image, surface))
      break;
  }
  if (!image)
    g_error ("could not create Gst/VA image");

  if (!gst_vaapi_surface_sync (surface))
    g_error ("could not complete image upload");

  gst_vaapi_image_unref (image);
  return surface;
}

int
main (int argc, char *argv[])
{
  GstVaapiDisplay *display;
  GstVaapiWindow *window;
  GstVaapiSurface *surface;
  guint flags = GST_VAAPI_PICTURE_STRUCTURE_FRAME;

  static const guint width = 320;
  static const guint height = 240;
  static const guint win_width = 640;
  static const guint win_height = 480;

  gst_init (&argc, &argv);

#if GST_VAAPI_USE_DRM
  display = gst_vaapi_display_drm_new (NULL);
  if (!display)
    g_error ("could not create Gst/VA (DRM) display");

  surface = create_test_surface (display, width, height);
  if (!surface)
    g_error ("could not create Gst/VA surface");

  g_print ("#\n");
  g_print ("# Create window with gst_vaapi_window_drm_new()\n");
  g_print ("#\n");
  {
    window = gst_vaapi_window_drm_new (display, win_width, win_height);
    if (!window)
      g_error ("could not create dummy window");

    gst_vaapi_window_show (window);

    if (!gst_vaapi_window_put_surface (window, surface, NULL, NULL, flags))
      g_error ("could not render surface");

    pause ();
    gst_object_unref (window);
  }

  gst_vaapi_surface_unref (surface);
  gst_object_unref (display);
#endif

#if GST_VAAPI_USE_X11
  display = gst_vaapi_display_x11_new (NULL);
  if (!display)
    g_error ("could not create Gst/VA display");

  surface = create_test_surface (display, width, height);
  if (!surface)
    g_error ("could not create Gst/VA surface");

  g_print ("#\n");
  g_print ("# Create window with gst_vaapi_window_x11_new()\n");
  g_print ("#\n");
  {
    window = gst_vaapi_window_x11_new (display, win_width, win_height);
    if (!window)
      g_error ("could not create window");

    gst_vaapi_window_show (window);

    if (!gst_vaapi_window_put_surface (window, surface, NULL, NULL, flags))
      g_error ("could not render surface");

    pause ();
    gst_object_unref (window);
  }

  g_print ("#\n");
  g_print ("# Create window with gst_vaapi_window_x11_new_with_xid()\n");
  g_print ("#\n");
  {
    Display *const dpy =
        gst_vaapi_display_x11_get_display (GST_VAAPI_DISPLAY_X11 (display));
    Window rootwin, win;
    int screen;
    unsigned long white_pixel, black_pixel;

    screen = DefaultScreen (dpy);
    rootwin = RootWindow (dpy, screen);
    white_pixel = WhitePixel (dpy, screen);
    black_pixel = BlackPixel (dpy, screen);

    win = XCreateSimpleWindow (dpy,
        rootwin, 0, 0, win_width, win_height, 0, black_pixel, white_pixel);
    if (!win)
      g_error ("could not create X window");

    window = gst_vaapi_window_x11_new_with_xid (display, win);
    if (!window)
      g_error ("could not create window");

    gst_vaapi_window_show (window);

    if (!gst_vaapi_window_put_surface (window, surface, NULL, NULL, flags))
      g_error ("could not render surface");

    pause ();
    gst_object_unref (window);
    XUnmapWindow (dpy, win);
    XDestroyWindow (dpy, win);
  }

  gst_vaapi_surface_unref (surface);
  gst_object_unref (display);
#endif

#if GST_VAAPI_USE_WAYLAND
  display = gst_vaapi_display_wayland_new (NULL);
  if (!display)
    g_error ("could not create Gst/VA (Wayland) display");

  surface = create_test_surface (display, width, height);
  if (!surface)
    g_error ("could not create Gst/VA surface");

  g_print ("#\n");
  g_print ("# Create window with gst_vaapi_window_wayland_new()\n");
  g_print ("#\n");
  {
    window = gst_vaapi_window_wayland_new (display, win_width, win_height);
    if (!window)
      g_error ("could not create window");

    gst_vaapi_window_show (window);

    if (!gst_vaapi_window_put_surface (window, surface, NULL, NULL, flags))
      g_error ("could not render surface");

    pause ();
    gst_object_unref (window);
  }

  gst_vaapi_surface_unref (surface);
  gst_object_unref (display);
#endif

  gst_deinit ();
  return 0;
}
