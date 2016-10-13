/*
 *  gstvaapidisplay_glx_priv.h - Internal VA/GLX interface
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
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

#ifndef GST_VAAPI_DISPLAY_GLX_PRIV_H
#define GST_VAAPI_DISPLAY_GLX_PRIV_H

#include <gst/vaapi/gstvaapiutils_glx.h>
#include <gst/vaapi/gstvaapidisplay_glx.h>
#include <gst/vaapi/gstvaapitexturemap.h>
#include "gstvaapidisplay_x11_priv.h"

G_BEGIN_DECLS

#define GST_VAAPI_IS_DISPLAY_GLX(display) \
   (G_TYPE_CHECK_INSTANCE_TYPE ((display), GST_TYPE_VAAPI_DISPLAY_GLX))

#define GST_VAAPI_DISPLAY_GLX_CAST(display) \
    ((GstVaapiDisplayGLX *)(display))

#define GST_VAAPI_DISPLAY_GLX_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPI_DISPLAY_GLX, GstVaapiDisplayGLXClass))

#define GST_VAAPI_DISPLAY_GLX_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_DISPLAY_GLX, GstVaapiDisplayGLXClass))

typedef struct _GstVaapiDisplayGLXClass         GstVaapiDisplayGLXClass;

/**
 * GstVaapiDisplayGLX:
 *
 * VA/GLX display wrapper.
 */
struct _GstVaapiDisplayGLX
{
  /*< private >*/
  GstVaapiDisplayX11 parent_instance;
  GstVaapiTextureMap *texture_map;
};

/**
 * GstVaapiDisplayGLXClass:
 *
 * VA/GLX display wrapper clas.
 */
struct _GstVaapiDisplayGLXClass
{
  /*< private >*/
  GstVaapiDisplayX11Class parent_class;
};

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_GLX_PRIV_H */
