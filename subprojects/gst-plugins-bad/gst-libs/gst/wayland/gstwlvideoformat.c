/* GStreamer Wayland Library
 *
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2012 Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2014 Collabora Ltd.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwlvideoformat.h"

#include <drm_fourcc.h>

#define GST_CAT_DEFAULT gst_wl_videoformat_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

void
gst_wl_videoformat_init_once (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_wl_videoformat_debug, "wl_videoformat", 0,
        "wl_videoformat library");

    g_once_init_leave (&_init, 1);
  }
}

enum wl_shm_format
gst_video_format_to_wl_shm_format (GstVideoFormat format)
{
  guint32 drm_format;
  guint64 modifier;

  drm_format = gst_video_dma_drm_format_from_gst_format (format, &modifier);

  if (drm_format == DRM_FORMAT_INVALID || modifier != DRM_FORMAT_MOD_LINEAR) {
    GST_WARNING ("wayland shm video format not found");
    return -1;
  }

  if (drm_format == DRM_FORMAT_XRGB8888)
    drm_format = WL_SHM_FORMAT_XRGB8888;
  else if (drm_format == DRM_FORMAT_ARGB8888)
    drm_format = WL_SHM_FORMAT_ARGB8888;

  return drm_format;
}

guint32
gst_video_format_to_wl_dmabuf_format (GstVideoFormat format)
{
  guint32 drm_format;
  guint64 modifier;

  drm_format = gst_video_dma_drm_format_from_gst_format (format, &modifier);

  if (drm_format == DRM_FORMAT_INVALID || modifier != DRM_FORMAT_MOD_LINEAR) {
    GST_WARNING ("wayland dmabuf video format not found");
    return DRM_FORMAT_INVALID;
  }

  return drm_format;
}

GstVideoFormat
gst_wl_shm_format_to_video_format (enum wl_shm_format wl_format)
{
  if (wl_format == WL_SHM_FORMAT_XRGB8888)
    wl_format = DRM_FORMAT_XRGB8888;
  else if (wl_format == WL_SHM_FORMAT_ARGB8888)
    wl_format = DRM_FORMAT_ARGB8888;

  return gst_wl_dmabuf_format_to_video_format (wl_format);
}

GstVideoFormat
gst_wl_dmabuf_format_to_video_format (guint wl_format)
{
  return gst_video_dma_drm_format_to_gst_format (wl_format,
      DRM_FORMAT_MOD_LINEAR);
}

const gchar *
gst_wl_shm_format_to_string (enum wl_shm_format wl_format)
{
  return gst_video_format_to_string
      (gst_wl_shm_format_to_video_format (wl_format));
}
