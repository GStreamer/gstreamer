/*
 *  gstvaapiwindow_x11_priv.h - VA/X11 window abstraction (private definitions)
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

#ifndef GST_VAAPI_WINDOW_X11_PRIV_H
#define GST_VAAPI_WINDOW_X11_PRIV_H

#include "gstvaapiwindow_priv.h"

#if HAVE_XRENDER
# include <X11/extensions/Xrender.h>
#endif

G_BEGIN_DECLS

#define GST_VAAPI_WINDOW_X11_CAST(obj) ((GstVaapiWindowX11 *)(obj))
#define GST_VAAPI_WINDOW_X11_GET_PRIVATE(obj) \
    gst_vaapi_window_x11_get_instance_private (GST_VAAPI_WINDOW_X11_CAST (obj))

#define GST_VAAPI_WINDOW_X11_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_WINDOW_X11, GstVaapiWindowX11Class))

typedef struct _GstVaapiWindowX11Private GstVaapiWindowX11Private;
typedef struct _GstVaapiWindowX11Class GstVaapiWindowX11Class;

struct _GstVaapiWindowX11Private
{
  Atom atom_NET_WM_STATE;
  Atom atom_NET_WM_STATE_FULLSCREEN;
  guint is_mapped:1;
  guint fullscreen_on_map:1;
  gboolean need_vpp;
};

/**
 * GstVaapiWindowX11:
 *
 * An X11 Window wrapper.
 */
struct _GstVaapiWindowX11
{
  /*< private >*/
  GstVaapiWindow parent_instance;
};

/**
 * GstVaapiWindowX11Class:
 *
 * An X11 Window wrapper class.
 */
struct _GstVaapiWindowX11Class
{
  /*< private >*/
  GstVaapiWindowClass parent_class;
};

G_END_DECLS

#endif /* GST_VAAPI_WINDOW_X11_PRIV_H */
