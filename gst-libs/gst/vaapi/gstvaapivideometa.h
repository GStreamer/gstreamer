/*
 *  gstvaapivideometa.h - Gstreamer/VA video meta
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2013 Intel Corporation
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

#ifndef GST_VAAPI_VIDEO_META_H
#define GST_VAAPI_VIDEO_META_H

#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiimage.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapisurfaceproxy.h>
#include <gst/vaapi/gstvaapivideopool.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_VIDEO_META \
    (gst_vaapi_video_meta_get_type())

typedef struct _GstVaapiVideoMeta               GstVaapiVideoMeta;

GType
gst_vaapi_video_meta_get_type(void) G_GNUC_CONST;

GstVaapiVideoMeta *
gst_vaapi_video_meta_new(GstVaapiDisplay *display);

GstVaapiVideoMeta *
gst_vaapi_video_meta_new_from_pool(GstVaapiVideoPool *pool);

GstVaapiVideoMeta *
gst_vaapi_video_meta_new_with_image(GstVaapiImage *image);

GstVaapiVideoMeta *
gst_vaapi_video_meta_new_with_surface(GstVaapiSurface *surface);

GstVaapiVideoMeta *
gst_vaapi_video_meta_new_with_surface_proxy(GstVaapiSurfaceProxy *proxy);

GstVaapiVideoMeta *
gst_vaapi_video_meta_ref(GstVaapiVideoMeta *meta);

void
gst_vaapi_video_meta_unref(GstVaapiVideoMeta *meta);

void
gst_vaapi_video_meta_replace(GstVaapiVideoMeta **old_meta_ptr,
    GstVaapiVideoMeta *new_meta);

GstVaapiDisplay *
gst_vaapi_video_meta_get_display(GstVaapiVideoMeta *meta);

GstVaapiImage *
gst_vaapi_video_meta_get_image(GstVaapiVideoMeta *meta);

void
gst_vaapi_video_meta_set_image(GstVaapiVideoMeta *meta, GstVaapiImage *image);

gboolean
gst_vaapi_video_meta_set_image_from_pool(GstVaapiVideoMeta *meta,
    GstVaapiVideoPool *pool);

GstVaapiSurface *
gst_vaapi_video_meta_get_surface(GstVaapiVideoMeta *meta);

void
gst_vaapi_video_meta_set_surface(GstVaapiVideoMeta *meta,
    GstVaapiSurface *surface);

gboolean
gst_vaapi_video_meta_set_surface_from_pool(GstVaapiVideoMeta *meta,
    GstVaapiVideoPool *pool);

GstVaapiSurfaceProxy *
gst_vaapi_video_meta_get_surface_proxy(GstVaapiVideoMeta *meta);

void
gst_vaapi_video_meta_set_surface_proxy(GstVaapiVideoMeta *meta,
    GstVaapiSurfaceProxy *proxy);

GFunc
gst_vaapi_video_meta_get_surface_converter(GstVaapiVideoMeta *meta);

void
gst_vaapi_video_meta_set_surface_converter(GstVaapiVideoMeta *meta, GFunc func);

guint
gst_vaapi_video_meta_get_render_flags(GstVaapiVideoMeta *meta);

void
gst_vaapi_video_meta_set_render_flags(GstVaapiVideoMeta *meta, guint flags);

GstVaapiVideoMeta *
gst_buffer_get_vaapi_video_meta(GstBuffer *buffer);

void
gst_buffer_set_vaapi_video_meta(GstBuffer *buffer, GstVaapiVideoMeta *meta);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_META_H */
