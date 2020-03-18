/*
 *  gstvaapidisplay_egl.h - VA/EGL display abstraction
 *
 *  Copyright (C) 2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) egl later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT EGL WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_DISPLAY_EGL_H
#define GST_VAAPI_DISPLAY_EGL_H

#include <EGL/egl.h>
#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

typedef struct _GstVaapiDisplayEGL GstVaapiDisplayEGL;

#define GST_TYPE_VAAPI_DISPLAY_EGL              (gst_vaapi_display_egl_get_type ())
#define GST_VAAPI_DISPLAY_EGL(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_DISPLAY_EGL, GstVaapiDisplayEGL))

GstVaapiDisplay *
gst_vaapi_display_egl_new (GstVaapiDisplay * display, guint gles_version);

GstVaapiDisplay *
gst_vaapi_display_egl_new_with_native_display (gpointer native_display,
    GstVaapiDisplayType display_type, guint gles_version);

EGLDisplay
gst_vaapi_display_egl_get_gl_display (GstVaapiDisplayEGL * display);

EGLContext
gst_vaapi_display_egl_get_gl_context (GstVaapiDisplayEGL * display);

gboolean
gst_vaapi_display_egl_set_gl_context (GstVaapiDisplayEGL * display,
    EGLContext gl_context);

gboolean
gst_vaapi_display_egl_set_current_display (GstVaapiDisplayEGL * display);

GType
gst_vaapi_display_egl_get_type (void) G_GNUC_CONST;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiDisplayEGL, gst_object_unref)

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_EGL_H */
