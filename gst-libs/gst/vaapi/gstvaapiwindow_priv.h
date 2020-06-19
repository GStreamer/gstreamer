/*
 *  gstvaapiwindow_priv.h - VA window abstraction (private definitions)
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

#ifndef GST_VAAPI_WINDOW_PRIV_H
#define GST_VAAPI_WINDOW_PRIV_H

#include "gstvaapidisplay.h"
#include "gstvaapifilter.h"
#include "gstvaapisurfacepool.h"

G_BEGIN_DECLS

#define GST_VAAPI_WINDOW_CAST(window) \
    ((GstVaapiWindow *)(window))

#define GST_VAAPI_WINDOW_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPI_WINDOW, GstVaapiWindowClass))

#define GST_VAAPI_IS_WINDOW_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_WINDOW))

#define GST_VAAPI_WINDOW_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_WINDOW, GstVaapiWindowClass))

#define GST_VAAPI_WINDOW_DISPLAY(window) \
   (GST_VAAPI_WINDOW_CAST (window)->display)

#define GST_VAAPI_WINDOW_LOCK_DISPLAY(window) \
   GST_VAAPI_DISPLAY_LOCK (GST_VAAPI_WINDOW_DISPLAY (window))

#define GST_VAAPI_WINDOW_UNLOCK_DISPLAY(window) \
   GST_VAAPI_DISPLAY_UNLOCK (GST_VAAPI_WINDOW_DISPLAY (window))

#define GST_VAAPI_WINDOW_NATIVE_DISPLAY(window) \
    GST_VAAPI_DISPLAY_NATIVE (GST_VAAPI_WINDOW_DISPLAY (window))

#define GST_VAAPI_WINDOW_ID(window) \
    (GST_VAAPI_WINDOW_CAST (window)->native_id)

#define GST_VAAPI_WINDOW_VADISPLAY(window) \
    GST_VAAPI_DISPLAY_VADISPLAY (GST_VAAPI_WINDOW_DISPLAY (window))

/**
 * GstVaapiWindow:
 *
 * Base class for system-dependent windows.
 */
struct _GstVaapiWindow
{
  /*< private >*/
  GstObject parent_instance;
  GstVaapiDisplay *display;
  GstVaapiID native_id;

  /*< protected >*/
  guint width;
  guint height;
  guint display_width;
  guint display_height;
  guint use_foreign_window:1;
  guint is_fullscreen:1;
  guint check_geometry:1;

  /* for conversion */
  GstVideoFormat surface_pool_format;
  guint surface_pool_flags;
  GstVaapiVideoPool *surface_pool;
  GstVaapiFilter *filter;
  gboolean has_vpp;
};

/**
 * GstVaapiWindowClass:
 * @create: virtual function to create a window with width and height
 * @show: virtual function to show (map) a window
 * @hide: virtual function to hide (unmap) a window
 * @get_geometry: virtual function to get the current window geometry
 * @set_fullscreen: virtual function to change window fullscreen state
 * @resize: virtual function to resize a window
 * @render: virtual function to render a #GstVaapiSurface into a window
 * @get_visual_id: virtual function to get the desired visual id used to
 *   create the window
 * @get_colormap: virtual function to get the desired colormap used to
 *   create the window, or the currently allocated one
 * @unblock: virtual function to unblock a rendering surface operation
 * @unblock_cancel: virtual function to cancel the previous unblock
 *   request.
 *
 * Base class for system-dependent windows.
 */
struct _GstVaapiWindowClass
{
  /*< private >*/
  GstObjectClass parent_class;

  /*< protected >*/
  gboolean (*create) (GstVaapiWindow * window, guint * width, guint * height);
  gboolean (*show) (GstVaapiWindow * window);
  gboolean (*hide) (GstVaapiWindow * window);
  gboolean (*get_geometry) (GstVaapiWindow * window, gint * px, gint * py,
      guint * pwidth, guint * pheight);
  gboolean (*set_fullscreen) (GstVaapiWindow * window, gboolean fullscreen);
  gboolean (*resize) (GstVaapiWindow * window, guint width, guint height);
  gboolean (*render) (GstVaapiWindow * window, GstVaapiSurface * surface,
      const GstVaapiRectangle * src_rect, const GstVaapiRectangle * dst_rect,
      guint flags);
  guintptr (*get_visual_id) (GstVaapiWindow * window);
  guintptr (*get_colormap) (GstVaapiWindow * window);
  gboolean (*unblock) (GstVaapiWindow * window);
  gboolean (*unblock_cancel) (GstVaapiWindow * window);
  void (*set_render_rect) (GstVaapiWindow * window, gint x, gint y, gint width, gint height);
};

GstVaapiWindow *
gst_vaapi_window_new_internal (GType type, GstVaapiDisplay * display,
    GstVaapiID handle, guint width, guint height);

GstVaapiSurface *
gst_vaapi_window_vpp_convert_internal (GstVaapiWindow * window,
    GstVaapiSurface * surface, const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags);

void
gst_vaapi_window_set_vpp_format_internal (GstVaapiWindow * window,
    GstVideoFormat format, guint flags);

G_END_DECLS

#endif /* GST_VAAPI_WINDOW_PRIV_H */
