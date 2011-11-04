/*
 *  gstvaapipluginutil.h - VA-API plugins private helper
 *
 *  Copyright (C) 2011 Intel Corporation
 *  Copyright (C) 2011 Collabora
 *    Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
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

#include <gst/gst.h>
#include <gst/video/videocontext.h>
#include <gst/vaapi/gstvaapidisplay.h>

gboolean gst_vaapi_ensure_display (gpointer element, GstVaapiDisplay **display);
void gst_vaapi_set_display (const gchar *type, const GValue *value, GstVaapiDisplay **display);
gboolean gst_vaapi_reply_to_query (GstQuery *query, GstVaapiDisplay *display);
