/*
 *  gstvaapiwindow_drm.c - VA/DRM window abstraction
 *
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

/**
 * SECTION:gstvaapiwindow_drm
 * @short_description: VA/DRM dummy window abstraction
 */

#include "sysdeps.h"
#include "gstvaapiwindow_drm.h"
#include "gstvaapiwindow_priv.h"
#include "gstvaapidisplay_drm_priv.h"

GST_DEBUG_CATEGORY_EXTERN (gst_debug_vaapi_window);
#define GST_CAT_DEFAULT gst_debug_vaapi_window

typedef struct _GstVaapiWindowDRMClass GstVaapiWindowDRMClass;

/**
 * GstVaapiWindowDRM:
 *
 * A dummy DRM window abstraction.
 */
struct _GstVaapiWindowDRM
{
  /*< private > */
  GstVaapiWindow parent_instance;
};

/**
 * GstVaapiWindowDRMClass:
 *
 * A dummy DRM window abstraction class.
 */
struct _GstVaapiWindowDRMClass
{
  /*< private > */
  GstVaapiWindowClass parent_instance;
};

G_DEFINE_TYPE (GstVaapiWindowDRM, gst_vaapi_window_drm, GST_TYPE_VAAPI_WINDOW);

static gboolean
gst_vaapi_window_drm_show (GstVaapiWindow * window)
{
  return TRUE;
}

static gboolean
gst_vaapi_window_drm_hide (GstVaapiWindow * window)
{
  return TRUE;
}

static gboolean
gst_vaapi_window_drm_create (GstVaapiWindow * window,
    guint * width, guint * height)
{
  return TRUE;
}

static gboolean
gst_vaapi_window_drm_resize (GstVaapiWindow * window, guint width, guint height)
{
  return TRUE;
}

static gboolean
gst_vaapi_window_drm_render (GstVaapiWindow * window,
    GstVaapiSurface * surface,
    const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags)
{
  return TRUE;
}

static void
gst_vaapi_window_drm_class_init (GstVaapiWindowDRMClass * klass)
{
  GstVaapiWindowClass *const window_class = GST_VAAPI_WINDOW_CLASS (klass);

  window_class->create = gst_vaapi_window_drm_create;
  window_class->show = gst_vaapi_window_drm_show;
  window_class->hide = gst_vaapi_window_drm_hide;
  window_class->resize = gst_vaapi_window_drm_resize;
  window_class->render = gst_vaapi_window_drm_render;
}

static void
gst_vaapi_window_drm_init (GstVaapiWindowDRM * window)
{
}

/**
 * gst_vaapi_window_drm_new:
 * @display: a #GstVaapiDisplay
 * @width: the requested window width, in pixels (unused)
 * @height: the requested windo height, in pixels (unused)
 *
 * Creates a dummy window. The window will be attached to the @display.
 * All rendering functions will return success since VA/DRM is a
 * renderless API.
 *
 * Note: this dummy window object is only necessary to fulfill cases
 * where the client application wants to automatically determine the
 * best display to use for the current system. As such, it provides
 * utility functions with the same API (function arguments) to help
 * implement uniform function tables.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_drm_new (GstVaapiDisplay * display, guint width, guint height)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_DRM (display), NULL);

  return gst_vaapi_window_new_internal (GST_TYPE_VAAPI_WINDOW_DRM, display,
      GST_VAAPI_ID_INVALID, width, height);
}
