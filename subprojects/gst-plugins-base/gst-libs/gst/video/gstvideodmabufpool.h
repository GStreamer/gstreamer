/* GStreamer video dmabuf pool
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
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <gst/video/gstvideopool.h>

G_BEGIN_DECLS

/**
 * GstVideoDmabufPool:
 *
 * Private instance object for #GstVideoDmabufPool.
 *
 * Since: 1.28
 */

/**
 * GstVideoDmabufPoolClass.parent_class:
 *
 * Parent Class.
 *
 * Since: 1.28
 */

/**
 * GST_TYPE_VIDEO_DMABUF_POOL:
 *
 * Macro that returns the #GstVideoDmabufPool type.
 *
 * Since: 1.28
 */
#define GST_TYPE_VIDEO_DMABUF_POOL gst_video_dmabuf_pool_get_type ()
GST_VIDEO_API
G_DECLARE_FINAL_TYPE (GstVideoDmabufPool, gst_video_dmabuf_pool, GST,
    VIDEO_DMABUF_POOL, GstVideoBufferPool)

GST_VIDEO_API
GstBufferPool *gst_video_dmabuf_pool_new (void);

G_END_DECLS
