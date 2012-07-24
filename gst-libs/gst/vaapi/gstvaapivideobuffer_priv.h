/*
 *  gstvaapivideobuffer_priv.h - Gstreamer/VA video buffer (private interface)
 *
 *  Copyright (C) 2011 Intel Corporation
 *  Copyright (C) 2011 Collabora Ltd.
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

#ifndef GST_VAAPI_VIDEO_BUFFER_PRIV_H
#define GST_VAAPI_VIDEO_BUFFER_PRIV_H

#include <gst/vaapi/gstvaapivideobuffer.h>

G_BEGIN_DECLS

/* Private API for gstreamer-vaapi plugin elements only */

GstBuffer *
gst_vaapi_video_buffer_typed_new(GType type, GstVaapiDisplay *display);

GstBuffer *
gst_vaapi_video_buffer_typed_new_from_pool(GType type, GstVaapiVideoPool *pool);

GstBuffer *
gst_vaapi_video_buffer_typed_new_from_buffer(GType type, GstBuffer *buffer);

GstBuffer *
gst_vaapi_video_buffer_typed_new_with_image(GType type, GstVaapiImage *image);

GstBuffer *
gst_vaapi_video_buffer_typed_new_with_surface(
    GType            type,
    GstVaapiSurface *surface
);

GstBuffer *
gst_vaapi_video_buffer_typed_new_with_surface_proxy(
    GType                 type,
    GstVaapiSurfaceProxy *proxy
);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_BUFFER_PRIV_H */
