/* GStreamer
 * Copyright (C) 2025 Igalia, S.L.
 *               Author: Victor Jaquez <vjaquez@igalia.com>
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
#include <gst/video/video.h>

#define Y4M_VIDEO_FORMATS "{ " \
  "I420, Y42B, Y41B, Y444, A444, GRAY8, I420_12LE, I422_12LE, "         \
  "Y444_12LE, I420_10LE, I422_10LE, Y444_10LE, GRAY10_LE16, GRAY16_LE " \
  "}"

gboolean              gst_y4m_video_unpadded_info (GstVideoInfo * y4m_info,
                                                   const GstVideoInfo * vinfo);

gboolean              gst_y4m_video_get_format_from_chroma_tag (const char * chroma_tag,
                                                                GstVideoFormat * format,
                                                                GstVideoChromaSite * chroma_site);

const char *          gst_y4m_video_get_chroma_tag_from_format (GstVideoFormat format,
                                                                GstVideoChromaSite chroma_site);

gboolean              gst_y4m_video_get_format_from_yscss_tag  (const char * yscss_tag,
                                                                GstVideoFormat * format,
                                                                GstVideoChromaSite * chroma_site);

const char *          gst_y4m_video_get_yscss_tag_from_format  (GstVideoFormat format,
                                                                GstVideoChromaSite chroma_site);

GstVideoColorRange    gst_y4m_video_get_color_range_from_range_tag (const char * range_tag);

const char *          gst_y4m_video_get_range_tag_from_color_range (GstVideoColorRange range);
