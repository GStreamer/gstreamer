/*
 *  gstvaapiutils_x11.h - X11 utilties
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

#ifndef GST_VAAPI_UTILS_X11_H
#define GST_VAAPI_UTILS_X11_H

#include "config.h"
#include <X11/Xlib.h>
#include <glib.h>

G_GNUC_INTERNAL
void
x11_trap_errors(void);

G_GNUC_INTERNAL
int
x11_untrap_errors(void);

G_GNUC_INTERNAL
Window
x11_create_window(Display *dpy, guint w, guint h, Visual *vis, Colormap cmap);

G_GNUC_INTERNAL
gboolean
x11_get_geometry(
    Display    *dpy,
    Drawable    drawable,
    gint       *px,
    gint       *py,
    guint      *pwidth,
    guint      *pheight
);

#endif /* GST_VAAPI_UTILS_X11_H */
