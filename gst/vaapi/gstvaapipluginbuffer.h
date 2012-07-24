/*
 *  gstvaapipluginbuffer.h - Private GStreamer/VA video buffers
 *
 *  Copyright (C) 2012 Intel Corporation
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

#ifndef GST_VAAPI_PLUGIN_BUFFER_H
#define GST_VAAPI_PLUGIN_BUFFER_H

G_GNUC_INTERNAL
GstBuffer *
gst_vaapi_video_buffer_new(GstVaapiDisplay *display);

G_GNUC_INTERNAL
GstBuffer *
gst_vaapi_video_buffer_new_from_pool(GstVaapiVideoPool *pool);

G_GNUC_INTERNAL
GstBuffer *
gst_vaapi_video_buffer_new_from_buffer(GstBuffer *buffer);

G_GNUC_INTERNAL
GstBuffer *
gst_vaapi_video_buffer_new_with_image(GstVaapiImage *image);

G_GNUC_INTERNAL
GstBuffer *
gst_vaapi_video_buffer_new_with_surface(GstVaapiSurface *surface);

G_GNUC_INTERNAL
GstBuffer *
gst_vaapi_video_buffer_new_with_surface_proxy(GstVaapiSurfaceProxy *proxy);

#endif /* GST_VAAPI_PLUGIN_BUFFER_H */
