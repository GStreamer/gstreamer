/*
 *  gstvaapidisplay_glx.h - VA/GLX display abstraction
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

#ifndef GST_VAAPI_DISPLAY_GLX_H
#define GST_VAAPI_DISPLAY_GLX_H

#include <GL/gl.h>
#include <GL/glx.h>
#include <gst/vaapi/gstvaapidisplay_x11.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DISPLAY_GLX \
    (gst_vaapi_display_glx_get_type())

#define GST_VAAPI_DISPLAY_GLX(obj)                              \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_DISPLAY_GLX,     \
                                GstVaapiDisplayGLX))

#define GST_VAAPI_DISPLAY_GLX_CLASS(klass)                      \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_DISPLAY_GLX,        \
                             GstVaapiDisplayGLXClass))

#define GST_VAAPI_IS_DISPLAY_GLX(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DISPLAY_GLX))

#define GST_VAAPI_IS_DISPLAY_GLX_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DISPLAY_GLX))

#define GST_VAAPI_DISPLAY_GLX_GET_CLASS(obj)                    \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_DISPLAY_GLX,      \
                               GstVaapiDisplayGLXClass))

typedef struct _GstVaapiDisplayGLX              GstVaapiDisplayGLX;
typedef struct _GstVaapiDisplayGLXClass         GstVaapiDisplayGLXClass;

/**
 * GstVaapiDisplayGLX:
 *
 * VA/GLX display wrapper.
 */
struct _GstVaapiDisplayGLX {
    /*< private >*/
    GstVaapiDisplayX11 parent_instance;
};


/**
 * GstVaapiDisplayGLXClass:
 *
 * VA/GLX display wrapper clas.
 */
struct _GstVaapiDisplayGLXClass {
    /*< private >*/
    GstVaapiDisplayX11Class parent_class;
};

GType
gst_vaapi_display_glx_get_type(void) G_GNUC_CONST;

GstVaapiDisplay *
gst_vaapi_display_glx_new(const gchar *display_name);

GstVaapiDisplay *
gst_vaapi_display_glx_new_with_display(Display *x11_display);

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_GLX_H */
