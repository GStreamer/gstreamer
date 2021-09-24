/*
 *  gstvaapiwindow_glx.h - VA/GLX window abstraction
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

#ifndef GST_VAAPI_WINDOW_GLX_H
#define GST_VAAPI_WINDOW_GLX_H

#include <GL/glx.h>
#include <gst/gst.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiwindow_x11.h>
#include <gst/vaapi/gstvaapitexture.h>
#include <gst/vaapi/gstvaapitypes.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_WINDOW_GLX (gst_vaapi_window_glx_get_type ())
#define GST_VAAPI_WINDOW_GLX(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_WINDOW_GLX, GstVaapiWindowGLX))
#define GST_VAAPI_IS_WINDOW_GLX(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_WINDOW_GLX))

typedef struct _GstVaapiWindowGLX GstVaapiWindowGLX;

GType
gst_vaapi_window_glx_get_type (void) G_GNUC_CONST;

GstVaapiWindow *
gst_vaapi_window_glx_new (GstVaapiDisplay * display, guint width, guint height);

GstVaapiWindow *
gst_vaapi_window_glx_new_with_xid (GstVaapiDisplay * display, Window xid);

GLXContext
gst_vaapi_window_glx_get_context (GstVaapiWindowGLX * window);

gboolean
gst_vaapi_window_glx_set_context (GstVaapiWindowGLX * window, GLXContext ctx);

gboolean
gst_vaapi_window_glx_make_current (GstVaapiWindowGLX * window);

void
gst_vaapi_window_glx_swap_buffers (GstVaapiWindowGLX * window);

gboolean
gst_vaapi_window_glx_put_texture (GstVaapiWindowGLX * window,
    GstVaapiTexture * texture, const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiWindowGLX, gst_object_unref)

G_END_DECLS

#endif /* GST_VAAPI_WINDOW_GLX_H */
