/* Video conversion api function
 * Copyright (C) 2014 Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_VIDEO_CONVERTER_H__
#define __GST_VIDEO_CONVERTER_H__

#include <gst/video/video.h>

G_BEGIN_DECLS

typedef enum {
  GST_VIDEO_DITHER_NONE,
  GST_VIDEO_DITHER_VERTERR,
  GST_VIDEO_DITHER_HALFTONE
} GstVideoDitherMethod;

typedef struct _GstVideoConverter GstVideoConverter;

GstVideoConverter *  gst_video_converter_new            (GstVideoInfo *in_info,
                                                         GstVideoInfo *out_info,
                                                         GstStructure *config);
void                 gst_video_converter_free           (GstVideoConverter * convert);

gboolean             gst_video_converter_set_config     (GstVideoConverter * convert, GstStructure *config);
const GstStructure * gst_video_converter_get_config     (GstVideoConverter * convert);

void                 gst_video_converter_frame          (GstVideoConverter * convert,
                                                         GstVideoFrame *dest, const GstVideoFrame *src);


G_END_DECLS

#endif /* __GST_VIDEO_CONVERTER_H__ */
