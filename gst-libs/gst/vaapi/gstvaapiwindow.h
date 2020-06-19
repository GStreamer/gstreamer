/*
 *  gstvaapiwindow.h - VA window abstraction
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

#ifndef GST_VAAPI_WINDOW_H
#define GST_VAAPI_WINDOW_H

#include <gst/gst.h>
#include <gst/vaapi/gstvaapitypes.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapisurface.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_WINDOW                    (gst_vaapi_window_get_type ())
#define GST_VAAPI_WINDOW(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_WINDOW, GstVaapiWindow))
#define GST_VAAPI_IS_WINDOW(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_WINDOW))

typedef struct _GstVaapiWindow GstVaapiWindow;
typedef struct _GstVaapiWindowClass GstVaapiWindowClass;

GType
gst_vaapi_window_get_type (void) G_GNUC_CONST;

GstVaapiWindow *
gst_vaapi_window_new (GstVaapiDisplay * display, guint width, guint height);

void
gst_vaapi_window_replace (GstVaapiWindow ** old_window_ptr,
    GstVaapiWindow * new_window);

GstVaapiDisplay *
gst_vaapi_window_get_display (GstVaapiWindow * window);

void
gst_vaapi_window_show (GstVaapiWindow * window);

void
gst_vaapi_window_hide (GstVaapiWindow * window);

gboolean
gst_vaapi_window_get_fullscreen (GstVaapiWindow * window);

void
gst_vaapi_window_set_fullscreen (GstVaapiWindow * window, gboolean fullscreen);

guint
gst_vaapi_window_get_width (GstVaapiWindow * window);

guint
gst_vaapi_window_get_height (GstVaapiWindow * window);

void
gst_vaapi_window_get_size (GstVaapiWindow * window, guint * width_ptr,
    guint * height_ptr);

void
gst_vaapi_window_set_width (GstVaapiWindow * window, guint width);

void
gst_vaapi_window_set_height (GstVaapiWindow * window, guint height);

void
gst_vaapi_window_set_size (GstVaapiWindow * window, guint width, guint height);

void
gst_vaapi_window_set_render_rectangle (GstVaapiWindow * window, gint x, gint y,
    gint width, gint height);

gboolean
gst_vaapi_window_put_surface (GstVaapiWindow * window,
    GstVaapiSurface * surface, const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags);

void
gst_vaapi_window_reconfigure (GstVaapiWindow * window);

gboolean
gst_vaapi_window_unblock (GstVaapiWindow * window);

gboolean
gst_vaapi_window_unblock_cancel (GstVaapiWindow * window);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiWindow, gst_object_unref)

G_END_DECLS

#endif /* GST_VAAPI_WINDOW_H */
