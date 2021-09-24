/*
 *  gstvaapivideometa.h - Gstreamer/VA video meta
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
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

#ifndef GST_VAAPI_VIDEO_META_H
#define GST_VAAPI_VIDEO_META_H

#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiimage.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapisurfaceproxy.h>
#include <gst/vaapi/gstvaapivideopool.h>

G_BEGIN_DECLS

typedef struct _GstVaapiVideoMeta GstVaapiVideoMeta;

#define GST_VAAPI_VIDEO_META_API_TYPE \
  gst_vaapi_video_meta_api_get_type ()

G_GNUC_INTERNAL
GType
gst_vaapi_video_meta_api_get_type (void) G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiVideoMeta *
gst_vaapi_video_meta_copy (GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
GstVaapiVideoMeta *
gst_vaapi_video_meta_new (GstVaapiDisplay * display);

G_GNUC_INTERNAL
GstVaapiVideoMeta *
gst_vaapi_video_meta_new_from_pool (GstVaapiVideoPool * pool);

G_GNUC_INTERNAL
GstVaapiVideoMeta *
gst_vaapi_video_meta_new_with_image (GstVaapiImage * image);

G_GNUC_INTERNAL
GstVaapiVideoMeta *
gst_vaapi_video_meta_new_with_surface_proxy (GstVaapiSurfaceProxy * proxy);

G_GNUC_INTERNAL
GstVaapiVideoMeta *
gst_vaapi_video_meta_ref (GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
void
gst_vaapi_video_meta_unref (GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
void
gst_vaapi_video_meta_replace (GstVaapiVideoMeta ** old_meta_ptr,
    GstVaapiVideoMeta * new_meta);

G_GNUC_INTERNAL
GstVaapiDisplay *
gst_vaapi_video_meta_get_display (GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
GstVaapiImage *
gst_vaapi_video_meta_get_image (GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
void
gst_vaapi_video_meta_set_image (GstVaapiVideoMeta * meta,
    GstVaapiImage * image);

G_GNUC_INTERNAL
gboolean
gst_vaapi_video_meta_set_image_from_pool (GstVaapiVideoMeta * meta,
    GstVaapiVideoPool * pool);

G_GNUC_INTERNAL
GstVaapiSurface *
gst_vaapi_video_meta_get_surface (GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
GstVaapiSurfaceProxy *
gst_vaapi_video_meta_get_surface_proxy (GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
void
gst_vaapi_video_meta_set_surface_proxy (GstVaapiVideoMeta * meta,
    GstVaapiSurfaceProxy * proxy);

G_GNUC_INTERNAL
guint
gst_vaapi_video_meta_get_render_flags (GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
void
gst_vaapi_video_meta_set_render_flags (GstVaapiVideoMeta * meta, guint flags);

G_GNUC_INTERNAL
const GstVaapiRectangle *
gst_vaapi_video_meta_get_render_rect (GstVaapiVideoMeta * meta);

G_GNUC_INTERNAL
void
gst_vaapi_video_meta_set_render_rect (GstVaapiVideoMeta * meta,
    const GstVaapiRectangle * rect);

G_GNUC_INTERNAL
GstVaapiVideoMeta *
gst_buffer_get_vaapi_video_meta (GstBuffer * buffer);

G_GNUC_INTERNAL
void
gst_buffer_set_vaapi_video_meta (GstBuffer * buffer, GstVaapiVideoMeta * meta);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_META_H */
