/* GStreamer Wayland Library
 *
 * Copyright (C) 2025 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#pragma once

#include <gst/wayland/wayland.h>

G_BEGIN_DECLS

/* <private> */
GstWlOutput *gst_wl_output_new (struct wl_output *output, guint32 id);

void gst_wl_output_set_name (GstWlOutput * self, const gchar *name);

void gst_wl_output_set_description (GstWlOutput * self, const gchar *description);

void gst_wl_output_set_scale (GstWlOutput * self, gint scale_factor);

void gst_wl_output_set_geometry (GstWlOutput * self, gint x,gint y,
                                 gint physical_width, gint physical_height,
                                 enum wl_output_subpixel subpixel,
                                 const gchar * make, const gchar * model,
                                 enum wl_output_transform transform);

void gst_wl_output_set_mode (GstWlOutput * self, guint flags, gint width,
                             gint height, gint refresh);

G_END_DECLS
