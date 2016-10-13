/*
 *  gstvaapidisplay_egl_priv.h - Internal VA/EGL interface
 *
 *  Copyright (C) 2014 Splitted-Desktop Systems
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

#ifndef GST_VAAPI_DISPLAY_EGL_PRIV_H
#define GST_VAAPI_DISPLAY_EGL_PRIV_H

#include <gst/vaapi/gstvaapiwindow.h>
#include <gst/vaapi/gstvaapitexturemap.h>
#include "gstvaapidisplay_egl.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiutils_egl.h"

G_BEGIN_DECLS

#define GST_VAAPI_IS_DISPLAY_EGL(display) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((display), GST_TYPE_VAAPI_DISPLAY_EGL))

#define GST_VAAPI_DISPLAY_EGL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPI_DISPLAY_EGL, GstVaapiDisplayEGLClass))

#define GST_VAAPI_DISPLAY_EGL_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_DISPLAY_EGL, GstVaapiDisplayEGLClass))

#define GST_VAAPI_DISPLAY_EGL_CAST(obj) \
    ((GstVaapiDisplayEGL *)(obj))

/**
 * GST_VAAPI_DISPLAY_EGL_DISPLAY:
 * @display: a #GstVaapiDisplay
 *
 * Macro that evaluates to #EglDisplay wrapper for @display.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DISPLAY_EGL_DISPLAY
#define GST_VAAPI_DISPLAY_EGL_DISPLAY(display) \
  (GST_VAAPI_DISPLAY_EGL_CAST (display)->egl_display)

/**
 * GST_VAAPI_DISPLAY_EGL_CONTEXT:
 * @display: a #GstVaapiDisplay
 *
 * Macro that evaluates to #EglContext wrapper for @display.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DISPLAY_EGL_CONTEXT
#define GST_VAAPI_DISPLAY_EGL_CONTEXT(display) \
  gst_vaapi_display_egl_get_context (GST_VAAPI_DISPLAY_EGL (display))

typedef struct _GstVaapiDisplayEGLClass GstVaapiDisplayEGLClass;

/**
 * GstVaapiDisplayEGL:
 *
 * VA/EGL display wrapper.
 */
struct _GstVaapiDisplayEGL
{
  /*< private >*/
  GstVaapiDisplay parent_instance;

  gpointer loader;
  GstVaapiDisplay *display;
  EglDisplay *egl_display;
  EglContext *egl_context;
  guint gles_version;
  GstVaapiTextureMap *texture_map;
};

/**
 * GstVaapiDisplayEGLClass:
 *
 * VA/EGL display wrapper clas.
 */
struct _GstVaapiDisplayEGLClass
{
  /*< private >*/
  GstVaapiDisplayClass parent_class;
};

G_GNUC_INTERNAL
EglContext *
gst_vaapi_display_egl_get_context (GstVaapiDisplayEGL * display);

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_EGL_PRIV_H */
