/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include <gst/gst.h>
#include <gst/va/gstva.h>
#include <gst/video/video.h>
#include <va/va.h>

G_BEGIN_DECLS

GST_VA_API
GstVideoFormat        gst_va_video_format_from_va_fourcc  (guint fourcc);

GST_VA_API
guint                 gst_va_fourcc_from_video_format     (GstVideoFormat format);

GST_VA_API
GstVideoFormat        gst_va_video_format_from_drm_fourcc (guint fourcc);

GST_VA_API
guint                 gst_va_drm_fourcc_from_video_format (GstVideoFormat format);

GST_VA_API
guint                 gst_va_chroma_from_video_format     (GstVideoFormat format);

GST_VA_API
guint                 gst_va_chroma_from_va_fourcc        (guint va_fourcc);

GST_VA_API
const VAImageFormat * gst_va_image_format_from_video_format (GstVideoFormat format);

GST_VA_API
GstVideoFormat        gst_va_video_format_from_va_image_format (const VAImageFormat * va_format);

GST_VA_API
GstVideoFormat        gst_va_video_surface_format_from_image_format (GstVideoFormat image_format,
                                                                     GArray * surface_formats);

GST_VA_API
gboolean              gst_va_dma_drm_info_to_video_info   (const GstVideoInfoDmaDrm * drm_info,
                                                           GstVideoInfo * info);

GST_VA_API
void                  gst_va_video_format_fix_map         (VAImageFormat * image_formats,
                                                           gint num);

G_END_DECLS
