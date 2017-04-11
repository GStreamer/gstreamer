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

#include "gstvaapiobject_priv.h"
#include "gstvaapifilter.h"
#include "gstvaapisurfacepool.h"

G_BEGIN_DECLS

#define GST_VAAPI_WINDOW_CLASS(klass) \
    ((GstVaapiWindowClass *)(klass))

#define GST_VAAPI_WINDOW_GET_CLASS(obj) \
    GST_VAAPI_WINDOW_CLASS(GST_VAAPI_OBJECT_GET_CLASS(obj))

/* GstVaapiWindowClass hooks */
typedef gboolean (*GstVaapiWindowCreateFunc) (GstVaapiWindow * window,
    guint * width, guint * height);
typedef gboolean (*GstVaapiWindowShowFunc) (GstVaapiWindow * window);
typedef gboolean (*GstVaapiWindowHideFunc) (GstVaapiWindow * window);
typedef gboolean (*GstVaapiWindowGetGeometryFunc) (GstVaapiWindow * window,
    gint * px, gint * py, guint * pwidth, guint * pheight);
typedef gboolean (*GstVaapiWindowSetFullscreenFunc) (GstVaapiWindow * window,
    gboolean fullscreen);
typedef gboolean (*GstVaapiWindowResizeFunc) (GstVaapiWindow * window,
    guint width, guint height);
typedef gboolean (*GstVaapiWindowRenderFunc) (GstVaapiWindow * window,
    GstVaapiSurface * surface, const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags);
typedef gboolean (*GstVaapiWindowRenderPixmapFunc) (GstVaapiWindow * window,
    GstVaapiPixmap * pixmap, const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect);
typedef guintptr (*GstVaapiWindowGetVisualIdFunc) (GstVaapiWindow * window);
typedef guintptr (*GstVaapiWindowGetColormapFunc) (GstVaapiWindow * window);
typedef gboolean (*GstVaapiWindowSetUnblockFunc) (GstVaapiWindow * window);
typedef gboolean (*GstVaapiWindowSetUnblockCancelFunc) (GstVaapiWindow * window);

/**
 * GstVaapiWindow:
 *
 * Base class for system-dependent windows.
 */
struct _GstVaapiWindow
{
  /*< private >*/
  GstVaapiObject parent_instance;

  /*< protected >*/
  guint width;
  guint height;
  guint display_width;
  guint display_height;
  guint use_foreign_window:1;
  guint is_fullscreen:1;
  guint check_geometry:1;

  /* for conversion */
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
  GstVaapiObjectClass parent_class;

  /*< protected >*/
  GstVaapiWindowCreateFunc create;
  GstVaapiWindowShowFunc show;
  GstVaapiWindowHideFunc hide;
  GstVaapiWindowGetGeometryFunc get_geometry;
  GstVaapiWindowSetFullscreenFunc set_fullscreen;
  GstVaapiWindowResizeFunc resize;
  GstVaapiWindowRenderFunc render;
  GstVaapiWindowRenderPixmapFunc render_pixmap;
  GstVaapiWindowGetVisualIdFunc get_visual_id;
  GstVaapiWindowGetColormapFunc get_colormap;
  GstVaapiWindowSetUnblockFunc unblock;
  GstVaapiWindowSetUnblockCancelFunc unblock_cancel;
};

GstVaapiWindow *
gst_vaapi_window_new_internal (const GstVaapiWindowClass * window_class,
    GstVaapiDisplay * display, GstVaapiID handle, guint width, guint height);

GstVaapiSurface *
gst_vaapi_window_vpp_convert_internal (GstVaapiWindow * window,
    GstVaapiSurface * surface, const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags);

void
gst_vaapi_window_class_init (GstVaapiWindowClass * klass);

/* Inline reference counting for core libgstvaapi library */
#ifdef IN_LIBGSTVAAPI_CORE
#define gst_vaapi_window_ref_internal(window) \
    ((gpointer)gst_vaapi_object_ref(GST_VAAPI_OBJECT(window)))

#define gst_vaapi_window_unref_internal(window) \
    gst_vaapi_object_unref(GST_VAAPI_OBJECT(window))

#define gst_vaapi_window_replace_internal(old_window_ptr, new_window) \
    gst_vaapi_object_replace((GstVaapiObject **)(old_window_ptr), \
        GST_VAAPI_OBJECT(new_window))

#undef  gst_vaapi_window_ref
#define gst_vaapi_window_ref(window) \
    gst_vaapi_window_ref_internal((window))

#undef  gst_vaapi_window_unref
#define gst_vaapi_window_unref(window) \
    gst_vaapi_window_unref_internal((window))

#undef  gst_vaapi_window_replace
#define gst_vaapi_window_replace(old_window_ptr, new_window) \
    gst_vaapi_window_replace_internal((old_window_ptr), (new_window))
#endif

G_END_DECLS

#endif /* GST_VAAPI_WINDOW_PRIV_H */
