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

#include <gst/wayland/wayland-prelude.h>

G_BEGIN_DECLS

#define GST_TYPE_WL_OUTPUT (gst_wl_output_get_type ())

G_DECLARE_FINAL_TYPE (GstWlOutput, gst_wl_output, GST, WL_OUTPUT, GObject);

GST_WL_API
struct wl_output * gst_wl_output_get_wl_output (GstWlOutput *self);

GST_WL_API
guint32 gst_wl_output_get_id (GstWlOutput *self);

GST_WL_API
const gchar * gst_wl_output_get_name (GstWlOutput *self);

GST_WL_API
void gst_wl_output_info (GstWlOutput *self);

GST_WL_API
const gchar * gst_wl_output_get_decscription (GstWlOutput *self);

GST_WL_API
const gchar * gst_wl_output_get_make (GstWlOutput *self);

GST_WL_API
const gchar * gst_wl_output_get_model (GstWlOutput *self);

GST_WL_API
gint gst_wl_output_get_scale (GstWlOutput * self);

GST_WL_API
gint gst_wl_output_get_x (GstWlOutput *self);

GST_WL_API
gint gst_wl_output_get_y (GstWlOutput *self);

GST_WL_API
gint gst_wl_output_get_physical_width (GstWlOutput *self);

GST_WL_API
gint gst_wl_output_get_physical_height (GstWlOutput *self);

GST_WL_API
enum wl_output_subpixel gst_wl_output_get_subpixel (GstWlOutput *self);

GST_WL_API
enum wl_output_transform gst_wl_output_get_transform (GstWlOutput *self);

GST_WL_API
guint gst_wl_output_get_mode_flags (GstWlOutput *self);

GST_WL_API
gint gst_wl_output_get_width (GstWlOutput *self);

GST_WL_API
gint gst_wl_output_get_height (GstWlOutput *self);

GST_WL_API
gint gst_wl_output_get_refresh (GstWlOutput *self);

G_END_DECLS
