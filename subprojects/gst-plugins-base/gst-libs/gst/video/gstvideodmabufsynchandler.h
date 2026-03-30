/* GStreamer video dmabuf sync_handler
 *
 * Copyright (C) 2026 Pengutronix, Michael Olbrich <m.olbrich@pengutronix.de>
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

#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * GstVideoDmabufSyncHandler:
 *
 * Private instance object for #GstVideoDmabufSyncHandler.
 *
 * Since: 1.30
 */

/**
 * GST_TYPE_VIDEO_DMABUF_SYNC_HANDLER:
 *
 * Macro that returns the #GstVideoDmabufSyncHandler type.
 *
 * Since: 1.30
 */
#define GST_TYPE_VIDEO_DMABUF_SYNC_HANDLER                                    \
  gst_video_dmabuf_sync_handler_get_type()
GST_VIDEO_API
G_DECLARE_FINAL_TYPE(GstVideoDmabufSyncHandler, gst_video_dmabuf_sync_handler,
                     GST, VIDEO_DMABUF_SYNC_HANDLER, GstObject)

/**
 * GstVideoDmabufSyncHandlerReleaseBuffer:
 *
 * @user_data privated data as configured in gst_video_dmabuf_sync_handler_new()
 * @buffer the buffer that is released
 *
 * The prototype of a function that will be called to release a buffer.
 *
 * Since: 1.30
 */
typedef void (* GstVideoDmabufSyncHandlerReleaseBuffer) (gpointer user_data,
                                                         gpointer buffer);

GST_VIDEO_API
GstVideoDmabufSyncHandler *gst_video_dmabuf_sync_handler_new (GstVideoDmabufSyncHandlerReleaseBuffer release_buffer,
                                                              gpointer user_data);

GST_VIDEO_API
gboolean gst_video_dmabuf_sync_handler_start (GstVideoDmabufSyncHandler *sync_handler);

GST_VIDEO_API
gboolean gst_video_dmabuf_sync_handler_stop  (GstVideoDmabufSyncHandler *sync_handler);

GST_VIDEO_API
void gst_video_dmabuf_sync_handler_release_buffer (GstVideoDmabufSyncHandler *sync_handler,
                                                   gpointer buffer,
                                                   GstMemory ** memories,
                                                   guint n_memories);

G_END_DECLS
