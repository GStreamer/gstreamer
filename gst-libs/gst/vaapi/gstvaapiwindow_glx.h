/*
 *  gstvaapiwindow_glx.h - VA/GLX window abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiwindow_x11.h>
#include <gst/vaapi/gstvaapitexture.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_WINDOW_GLX \
    (gst_vaapi_window_glx_get_type())

#define GST_VAAPI_WINDOW_GLX(obj)                               \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_WINDOW_GLX,      \
                                GstVaapiWindowGLX))

#define GST_VAAPI_WINDOW_GLX_CLASS(klass)                       \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_WINDOW_GLX,         \
                             GstVaapiWindowGLXClass))

#define GST_VAAPI_IS_WINDOW_GLX(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_WINDOW_GLX))

#define GST_VAAPI_IS_WINDOW_GLX_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_WINDOW_GLX))

#define GST_VAAPI_WINDOW_GLX_GET_CLASS(obj)                     \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_WINDOW_GLX,       \
                               GstVaapiWindowGLXClass))

typedef struct _GstVaapiWindowGLX               GstVaapiWindowGLX;
typedef struct _GstVaapiWindowGLXPrivate        GstVaapiWindowGLXPrivate;
typedef struct _GstVaapiWindowGLXClass          GstVaapiWindowGLXClass;

/**
 * GstVaapiWindowGLX:
 *
 * An X11 #Window suitable for GLX rendering.
 */
struct _GstVaapiWindowGLX {
    /*< private >*/
    GstVaapiWindowX11 parent_instance;

    GstVaapiWindowGLXPrivate *priv;
};

/**
 * GstVaapiWindowGLXClass:
 *
 * An X11 #Window suitable for GLX rendering.
 */
struct _GstVaapiWindowGLXClass {
    /*< private >*/
    GstVaapiWindowX11Class parent_class;
};

GType
gst_vaapi_window_glx_get_type(void) G_GNUC_CONST;

GstVaapiWindow *
gst_vaapi_window_glx_new(GstVaapiDisplay *display, guint width, guint height);

GstVaapiWindow *
gst_vaapi_window_glx_new_with_xid(GstVaapiDisplay *display, Window xid);

GLXContext
gst_vaapi_window_glx_get_context(GstVaapiWindowGLX *window);

gboolean
gst_vaapi_window_glx_set_context(GstVaapiWindowGLX *window, GLXContext ctx);

gboolean
gst_vaapi_window_glx_make_current(GstVaapiWindowGLX *window);

void
gst_vaapi_window_glx_swap_buffers(GstVaapiWindowGLX *window);

gboolean
gst_vaapi_window_glx_put_texture(
    GstVaapiWindowGLX       *window,
    GstVaapiTexture         *texture,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect
);

G_END_DECLS

#endif /* GST_VAAPI_WINDOW_GLX_H */
