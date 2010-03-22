/*
 *  gstvaapiwindow_priv.h - VA window abstraction (private API)
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef GST_VAAPI_WINDOW_PRIVATE_H
#define GST_VAAPI_WINDOW_PRIVATE_H

#include "config.h"

G_BEGIN_DECLS

void
_gst_vaapi_window_set_fullscreen(GstVaapiWindow *window, gboolean fullscreen)
    attribute_hidden;

gboolean
_gst_vaapi_window_set_size(GstVaapiWindow *window, guint width, guint height)
    attribute_hidden;

G_END_DECLS

#endif /* GST_VAAPI_WINDOW_H */
